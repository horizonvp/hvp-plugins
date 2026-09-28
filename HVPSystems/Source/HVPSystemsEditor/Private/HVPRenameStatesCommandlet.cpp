#include "HVPRenameStatesCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataTableEditorUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "K2Node_SwitchString.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogHVPRenameStates, Log, All);

namespace
{
	/** Old (case-folded) -> new. An empty new value means delete. */
	struct FRenameMap
	{
		TMap<FString, FString> Map;

		/** True when In is mapped; Out is the replacement (empty = delete). */
		bool Lookup(const FString& In, FString& Out) const
		{
			const FString* Found = Map.Find(In.ToLower());
			if (!Found)
			{
				return false;
			}
			Out = *Found;
			return true;
		}

		static FRenameMap Parse(const FString& Spec)
		{
			FRenameMap Result;
			TArray<FString> Pairs;
			Spec.ParseIntoArray(Pairs, TEXT(";"), /*InCullEmpty=*/true);
			for (const FString& Pair : Pairs)
			{
				FString Old, New;
				if (!Pair.Split(TEXT("="), &Old, &New))
				{
					UE_LOG(LogHVPRenameStates, Warning, TEXT("Ignoring malformed rename '%s' (want old=new)"), *Pair);
					continue;
				}
				Old.TrimStartAndEndInline();
				New.TrimStartAndEndInline();
				if (Old.IsEmpty())
				{
					continue;
				}
				Result.Map.Add(Old.ToLower(), New);
			}
			return Result;
		}
	};

	struct FRewriter
	{
		const FRenameMap& Renames;
		int32 Changes = 0;

		explicit FRewriter(const FRenameMap& InRenames) : Renames(InRenames) {}

		void Note(const FString& Context, const FString& Old, const FString& New)
		{
			++Changes;
			UE_LOG(LogHVPRenameStates, Log, TEXT("    %s: '%s' -> %s"), *Context, *Old,
				New.IsEmpty() ? TEXT("(removed)") : *FString::Printf(TEXT("'%s'"), *New));
		}

		/** Rewrite one property value living at ValuePtr (already offset into its container). */
		void RewriteValue(const FProperty* Prop, void* ValuePtr, const FString& Context)
		{
			if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			{
				FString& Value = *StrProp->GetPropertyValuePtr(ValuePtr);
				FString New;
				if (!Value.IsEmpty() && Renames.Lookup(Value, New))
				{
					if (New.IsEmpty())
					{
						UE_LOG(LogHVPRenameStates, Warning,
							TEXT("    %s: '%s' is mapped to delete but is a plain string — emptied"), *Context, *Value);
					}
					Note(Context, Value, New);
					Value = New;
				}
				return;
			}
			if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
			{
				FScriptArrayHelper Helper(ArrayProp, ValuePtr);
				if (const FStrProperty* Inner = CastField<FStrProperty>(ArrayProp->Inner))
				{
					for (int32 i = Helper.Num() - 1; i >= 0; --i)
					{
						FString& Value = *Inner->GetPropertyValuePtr(Helper.GetRawPtr(i));
						FString New;
						if (Value.IsEmpty() || !Renames.Lookup(Value, New))
						{
							continue;
						}
						Note(FString::Printf(TEXT("%s[%d]"), *Context, i), Value, New);
						if (New.IsEmpty())
						{
							Helper.RemoveValues(i, 1);
						}
						else
						{
							Value = New;
						}
					}
				}
				else
				{
					for (int32 i = 0; i < Helper.Num(); ++i)
					{
						RewriteValue(ArrayProp->Inner, Helper.GetRawPtr(i), FString::Printf(TEXT("%s[%d]"), *Context, i));
					}
				}
				return;
			}
			if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
			{
				const FStrProperty* Inner = CastField<FStrProperty>(SetProp->ElementProp);
				if (!Inner)
				{
					return;
				}
				FScriptSetHelper Helper(SetProp, ValuePtr);
				TArray<FString> Values;
				bool bChanged = false;
				for (int32 i = 0; i < Helper.GetMaxIndex(); ++i)
				{
					if (!Helper.IsValidIndex(i))
					{
						continue;
					}
					const FString& Value = *Inner->GetPropertyValuePtr(Helper.GetElementPtr(i));
					FString New;
					if (!Value.IsEmpty() && Renames.Lookup(Value, New))
					{
						Note(Context, Value, New);
						bChanged = true;
						if (!New.IsEmpty())
						{
							Values.AddUnique(New);
						}
					}
					else
					{
						Values.AddUnique(Value);
					}
				}
				if (bChanged)
				{
					Helper.EmptyElements();
					for (const FString& Value : Values)
					{
						Helper.AddElement(&Value);
					}
				}
				return;
			}
			if (const FMapProperty* MapProp = CastField<FMapProperty>(Prop))
			{
				FScriptMapHelper Helper(MapProp, ValuePtr);
				const FStrProperty* KeyStr = CastField<FStrProperty>(MapProp->KeyProp);
				for (int32 i = 0; i < Helper.GetMaxIndex(); ++i)
				{
					if (!Helper.IsValidIndex(i))
					{
						continue;
					}
					if (KeyStr)
					{
						const FString& Key = *KeyStr->GetPropertyValuePtr(Helper.GetKeyPtr(i));
						FString New;
						if (!Key.IsEmpty() && Renames.Lookup(Key, New))
						{
							UE_LOG(LogHVPRenameStates, Warning,
								TEXT("    %s: map key '%s' matches a rename but map keys are not rewritten — do it by hand"),
								*Context, *Key);
						}
					}
					RewriteValue(MapProp->ValueProp, Helper.GetValuePtr(i), FString::Printf(TEXT("%s[%d]"), *Context, i));
				}
				return;
			}
			if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
				{
					RewriteProperty(*It, ValuePtr, Context + TEXT(".") + It->GetName());
				}
			}
			// Objects are reached through the package walk, not followed from here; FText and
			// FName values are left alone (row names are handled separately, tags are not states).
		}

		void RewriteProperty(const FProperty* Prop, void* Container, const FString& Context)
		{
			for (int32 Dim = 0; Dim < Prop->ArrayDim; ++Dim)
			{
				RewriteValue(Prop, Prop->ContainerPtrToValuePtr<void>(Container, Dim), Context);
			}
		}

		void RewriteObject(UObject* Object)
		{
			const FString Context = Object->GetPathName();
			for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
			{
				RewriteProperty(*It, Object, Context + TEXT(":") + It->GetName());
			}
		}

		/** Literal string/name pins and Switch-on-String case pins. */
		void RewriteNodePins(UEdGraphNode* Node)
		{
			const FString Context = Node->GetPathName();
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->LinkedTo.Num() > 0 || Pin->DefaultValue.IsEmpty())
				{
					continue;
				}
				const FName Category = Pin->PinType.PinCategory;
				if (Category != UEdGraphSchema_K2::PC_String && Category != UEdGraphSchema_K2::PC_Name)
				{
					continue;
				}
				FString New;
				if (Renames.Lookup(Pin->DefaultValue, New))
				{
					Note(Context + TEXT(" pin ") + Pin->PinName.ToString(), Pin->DefaultValue, New);
					Pin->DefaultValue = New;
				}
			}

			if (UK2Node_SwitchString* Switch = Cast<UK2Node_SwitchString>(Node))
			{
				for (FName& CaseName : Switch->PinNames)
				{
					const FString Case = CaseName.ToString();
					FString New;
					if (!Renames.Lookup(Case, New))
					{
						continue;
					}
					if (New.IsEmpty())
					{
						UE_LOG(LogHVPRenameStates, Warning,
							TEXT("    %s: switch case '%s' is mapped to delete — left in place, remove it by hand"), *Context, *Case);
						continue;
					}
					Note(Context + TEXT(" case"), Case, New);
					// Rename the live pin in place too, so its links survive (a reconstruct would
					// try to re-match links by name and drop them).
					if (UEdGraphPin* CasePin = Switch->FindPin(CaseName))
					{
						CasePin->PinName = FName(*New);
					}
					CaseName = FName(*New);
				}
			}
		}
	};

	bool SavePackageChecked(UPackage* Package, UObject* Asset)
	{
		const bool bIsMap = Asset && Asset->IsA<UWorld>();
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(),
			bIsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);
		UE_LOG(LogHVPRenameStates, Log, TEXT("  save %s: %s"), *Package->GetName(), bSaved ? TEXT("OK") : TEXT("FAILED"));
		return bSaved;
	}

	int32 CompileAndReportRenames(UBlueprint* Blueprint)
	{
		FCompilerResultsLog Results;
		Results.SetSourcePath(Blueprint->GetPathName());
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			const EMessageSeverity::Type Severity = Message->GetSeverity();
			if (Severity == EMessageSeverity::Error)
			{
				UE_LOG(LogHVPRenameStates, Error, TEXT("  [compile] %s"), *Message->ToText().ToString());
			}
			else if (Severity == EMessageSeverity::Warning || Severity == EMessageSeverity::PerformanceWarning)
			{
				UE_LOG(LogHVPRenameStates, Warning, TEXT("  [compile] %s"), *Message->ToText().ToString());
			}
		}
		return Results.NumErrors;
	}

	/** Rename DataTable rows in place (FDataTableEditorUtils keeps row order). */
	int32 RenameRows(UDataTable* Table, const FRenameMap& Renames)
	{
		int32 Changes = 0;
		for (const FName RowName : Table->GetRowNames())
		{
			FString New;
			if (!Renames.Lookup(RowName.ToString(), New))
			{
				continue;
			}
			if (New.IsEmpty())
			{
				UE_LOG(LogHVPRenameStates, Warning,
					TEXT("    %s: row '%s' is mapped to delete — left in place, remove it by hand"),
					*Table->GetPathName(), *RowName.ToString());
				continue;
			}
			if (FDataTableEditorUtils::RenameRow(Table, RowName, FName(*New)))
			{
				++Changes;
				UE_LOG(LogHVPRenameStates, Log, TEXT("    %s: row '%s' -> '%s'"), *Table->GetPathName(), *RowName.ToString(), *New);
			}
			else
			{
				UE_LOG(LogHVPRenameStates, Error, TEXT("    %s: FAILED to rename row '%s' -> '%s'"), *Table->GetPathName(), *RowName.ToString(), *New);
			}
		}
		return Changes;
	}
}

int32 UHVPRenameStatesCommandlet::Main(const FString& Params)
{
	FString RenameSpec, PathSpec;
	FParse::Value(*Params, TEXT("Renames="), RenameSpec);
	FParse::Value(*Params, TEXT("Paths="), PathSpec);
	const bool bDryRun = FParse::Param(*Params, TEXT("DryRun"));

	const FRenameMap Renames = FRenameMap::Parse(RenameSpec);
	if (Renames.Map.IsEmpty())
	{
		UE_LOG(LogHVPRenameStates, Error, TEXT("No renames given. Use -Renames=\"old=new;old2=\" (empty new = delete)."));
		return 1;
	}
	TArray<FString> Paths;
	PathSpec.ParseIntoArray(Paths, TEXT(","), /*InCullEmpty=*/true);
	if (Paths.IsEmpty())
	{
		Paths.Add(TEXT("/Game"));
	}
	UE_LOG(LogHVPRenameStates, Display, TEXT("%d rename(s) under %s%s"), Renames.Map.Num(),
		*FString::Join(Paths, TEXT(", ")), bDryRun ? TEXT(" [DRY RUN]") : TEXT(""));
	for (const TPair<FString, FString>& Pair : Renames.Map)
	{
		UE_LOG(LogHVPRenameStates, Display, TEXT("  %s -> %s"), *Pair.Key,
			Pair.Value.IsEmpty() ? TEXT("(delete)") : *Pair.Value);
	}

	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegistryModule.Get();
	Registry.SearchAllAssets(/*bSynchronousSearch=*/true);

	TArray<FAssetData> Assets;
	for (const FString& Path : Paths)
	{
		Registry.GetAssetsByPath(FName(*Path), Assets, /*bRecursive=*/true);
	}
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });

	int32 TotalChanges = 0;
	int32 TotalErrors = 0;
	TArray<FString> ChangedPackages;

	for (const FAssetData& AssetData : Assets)
	{
		UClass* AssetClass = AssetData.GetClass();
		if (!AssetClass)
		{
			continue;
		}
		const bool bBlueprint = AssetClass->IsChildOf<UBlueprint>();
		const bool bWorld = AssetClass->IsChildOf<UWorld>();
		const bool bTable = AssetClass->IsChildOf<UDataTable>();
		if (!bBlueprint && !bWorld && !bTable)
		{
			continue;
		}

		UObject* Asset = AssetData.GetAsset();
		if (!Asset)
		{
			UE_LOG(LogHVPRenameStates, Warning, TEXT("Could not load %s"), *AssetData.GetObjectPathString());
			continue;
		}
		UPackage* Package = Asset->GetOutermost();

		FRewriter Rewriter(Renames);
		UE_LOG(LogHVPRenameStates, Log, TEXT("=== %s"), *Package->GetName());

		if (bTable)
		{
			Rewriter.Changes += RenameRows(Cast<UDataTable>(Asset), Renames);
		}

		// Every object in the package: Blueprint CDO and component templates, graph nodes,
		// map actors and their components alike.
		TArray<UObject*> Objects;
		ForEachObjectWithPackage(Package, [&Objects](UObject* Object)
		{
			Objects.Add(Object);
			return true;
		}, /*bIncludeNestedObjects=*/true, RF_NoFlags, EInternalObjectFlags::Garbage);
		for (UObject* Object : Objects)
		{
			if (Object->IsA<UClass>() || Object->IsA<UFunction>())
			{
				continue; // reflection metadata, not authored data
			}
			Rewriter.RewriteObject(Object);
			if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
			{
				Rewriter.RewriteNodePins(Node);
			}
		}

		if (Rewriter.Changes == 0)
		{
			continue;
		}
		TotalChanges += Rewriter.Changes;
		ChangedPackages.Add(FString::Printf(TEXT("%s (%d)"), *Package->GetName(), Rewriter.Changes));
		UE_LOG(LogHVPRenameStates, Display, TEXT("  %d change(s) in %s"), Rewriter.Changes, *Package->GetName());
		if (bDryRun)
		{
			continue;
		}

		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			const int32 Errors = CompileAndReportRenames(Blueprint);
			TotalErrors += Errors;
			UE_LOG(LogHVPRenameStates, Log, TEXT("  compile: %d error(s)"), Errors);
		}
		Package->MarkPackageDirty();
		if (!SavePackageChecked(Package, Asset))
		{
			++TotalErrors;
		}
	}

	UE_LOG(LogHVPRenameStates, Display, TEXT("HVPRenameStates finished: %d change(s) in %d package(s), %d error(s)%s"),
		TotalChanges, ChangedPackages.Num(), TotalErrors, bDryRun ? TEXT(" [DRY RUN — nothing saved]") : TEXT(""));
	for (const FString& Changed : ChangedPackages)
	{
		UE_LOG(LogHVPRenameStates, Display, TEXT("  %s"), *Changed);
	}
	return TotalErrors == 0 ? 0 : 1;
}
