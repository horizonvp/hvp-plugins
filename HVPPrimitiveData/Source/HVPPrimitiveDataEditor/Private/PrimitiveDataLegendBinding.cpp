#include "PrimitiveDataLegendBinding.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Logging/MessageLog.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialFunction.h"
#include "Misc/UObjectToken.h"
#include "PrimitiveDataLegend.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "PrimitiveDataLegendBinding"

const FName FPrimitiveDataLegendBinding::LogName(TEXT("HVPPrimitiveData"));

namespace PrimitiveDataBinding
{
	FText TypeText(EPrimitiveDataParameterType Type)
	{
		return Type == EPrimitiveDataParameterType::Vector
			? LOCTEXT("Vector", "vector")
			: LOCTEXT("Scalar", "scalar");
	}

	/** A parameter node this binding manages, read into one shape so scalar and vector share a path. */
	struct FParameterNode
	{
		UMaterialExpression* Expression = nullptr;
		FName* Name = nullptr;
		bool* bUseCustomPrimitiveData = nullptr;
		uint8* Slot = nullptr;
		EPrimitiveDataParameterType Type = EPrimitiveDataParameterType::Scalar;
	};

	/**
	 * Exact classes only. Curve Atlas Row and Channel Mask parameters derive from Scalar and Vector
	 * Parameter and inherit the custom primitive data fields, but neither means anything read from
	 * primitive data, and a name clash with one should not quietly repurpose it.
	 */
	bool Read(UMaterialExpression* Expression, FParameterNode& Out)
	{
		if (!Expression)
		{
			return false;
		}
		if (Expression->GetClass() == UMaterialExpressionScalarParameter::StaticClass())
		{
			UMaterialExpressionScalarParameter* Scalar = CastChecked<UMaterialExpressionScalarParameter>(Expression);
			Out = { Expression, &Scalar->ParameterName, &Scalar->bUseCustomPrimitiveData, &Scalar->PrimitiveDataIndex,
				EPrimitiveDataParameterType::Scalar };
			return true;
		}
		if (Expression->GetClass() == UMaterialExpressionVectorParameter::StaticClass())
		{
			UMaterialExpressionVectorParameter* Vector = CastChecked<UMaterialExpressionVectorParameter>(Expression);
			Out = { Expression, &Vector->ParameterName, &Vector->bUseCustomPrimitiveData, &Vector->PrimitiveDataIndex,
				EPrimitiveDataParameterType::Vector };
			return true;
		}
		return false;
	}

	void Report(FMessageLog& Log, const UPrimitiveDataLegend& Legend, UObject* Material, const FPrimitiveDataBindingResult& Result)
	{
		for (const FText& Error : Result.Errors)
		{
			Log.Error()
				->AddToken(FUObjectToken::Create(Material))
				->AddToken(FTextToken::Create(Error))
				->AddToken(FTextToken::Create(LOCTEXT("Via", "  (legend:")))
				->AddToken(FUObjectToken::Create(&Legend))
				->AddToken(FTextToken::Create(LOCTEXT("Close", ")")));
		}
		for (const FText& Warning : Result.Warnings)
		{
			Log.Warning()
				->AddToken(FUObjectToken::Create(Material))
				->AddToken(FTextToken::Create(Warning))
				->AddToken(FTextToken::Create(LOCTEXT("Via", "  (legend:")))
				->AddToken(FUObjectToken::Create(&Legend))
				->AddToken(FTextToken::Create(LOCTEXT("Close", ")")));
		}
		if (!Result.SkippedReason.IsEmpty())
		{
			Log.Warning()
				->AddToken(FUObjectToken::Create(Material))
				->AddToken(FTextToken::Create(Result.SkippedReason));
		}
	}
}

FPrimitiveDataBindingResult FPrimitiveDataLegendBinding::Sync(const UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction,
	bool bApply, const TMap<FName, FName>& Renames)
{
	using namespace PrimitiveDataBinding;

	FPrimitiveDataBindingResult Result;

	UMaterial* Material = Cast<UMaterial>(MaterialOrFunction);
	UMaterialFunction* Function = Cast<UMaterialFunction>(MaterialOrFunction);
	if (!Material && !Function)
	{
		Result.Errors.Add(LOCTEXT("NotAMaterial",
			"is not a material or material function. Material instances cannot be bound - the custom "
			"primitive data setting lives on the parent's parameter node, so bind the parent."));
		return Result;
	}

	const TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions =
		Material ? Material->GetExpressions() : Function->GetExpressions();

	// Names already in the material. A rename only follows if the new name is free - otherwise the
	// material already has a parameter by that name, and relabelling a second one onto it would merge
	// two parameters that were separate.
	TSet<FName> NamesInMaterial;
	for (UMaterialExpression* Expression : Expressions)
	{
		FParameterNode Node;
		if (Read(Expression, Node))
		{
			NamesInMaterial.Add(*Node.Name);
		}
	}

	struct FChange
	{
		FParameterNode Node;
		FName NewName;
		uint8 NewSlot = 0;
	};
	TArray<FChange> Changes;
	TSet<FName> Found;

	for (UMaterialExpression* Expression : Expressions)
	{
		FParameterNode Node;
		if (!Read(Expression, Node))
		{
			continue;
		}

		FName Name = *Node.Name;
		if (const FName* Renamed = Renames.Find(Name))
		{
			if (!NamesInMaterial.Contains(*Renamed))
			{
				Name = *Renamed;
			}
		}

		const FPrimitiveDataLegendEntry* Entry = Legend.FindParameter(Name);
		if (!Entry)
		{
			// Not ours - an ordinary parameter, unless it reads primitive data, which is the one thing a
			// bound material must not do outside its legend.
			if (*Node.bUseCustomPrimitiveData)
			{
				Result.Errors.Add(FText::Format(
					LOCTEXT("Stray", "'{0}' reads custom primitive data (slot {1}), but the legend has no parameter by that name. "
						"Add it to the legend, or turn off Use Custom Primitive Data on the node."),
					FText::FromName(*Node.Name), FText::AsNumber(*Node.Slot)));
			}
			continue;
		}

		Found.Add(Entry->Name);

		if (Entry->Type != Node.Type)
		{
			Result.Errors.Add(FText::Format(
				LOCTEXT("WrongType", "'{0}' is a {1} parameter here but a {2} in the legend."),
				FText::FromName(Entry->Name), TypeText(Node.Type), TypeText(Entry->Type)));
			continue;
		}
		if (!Entry->HasSlot())
		{
			Result.Errors.Add(FText::Format(
				LOCTEXT("NoSlot", "'{0}' has no slot - the legend is out of room."), FText::FromName(Entry->Name)));
			continue;
		}

		const bool bSlotWrong = !*Node.bUseCustomPrimitiveData || *Node.Slot != Entry->Slot;
		const bool bNameWrong = *Node.Name != Entry->Name;
		if (bSlotWrong || bNameWrong)
		{
			Changes.Add({ Node, Entry->Name, static_cast<uint8>(Entry->Slot) });
		}
	}

	for (const FPrimitiveDataLegendEntry& Entry : Legend.Parameters)
	{
		if (Found.Contains(Entry.Name))
		{
			continue;
		}
		const FText Missing = FText::Format(
			LOCTEXT("Missing", "has no {0} parameter named '{1}'."), TypeText(Entry.Type), FText::FromName(Entry.Name));
		(Legend.bRequireAllParameters ? Result.Errors : Result.Warnings).Add(Missing);
	}

	if (Changes.Num() == 0)
	{
		return Result;
	}

	if (!bApply)
	{
		// A check, not a sync: say what a sync would do.
		for (const FChange& Change : Changes)
		{
			Result.Warnings.Add(FText::Format(
				LOCTEXT("OutOfStep", "'{0}' is not laid out on its slot ({1}) yet. Right-click the legend > Sync Bound Materials."),
				FText::FromName(Change.NewName), FText::AsNumber(Change.NewSlot)));
		}
		return Result;
	}

	// The material editor edits a COPY and writes it back over the original on Apply. Changing the
	// original underneath it would be silently undone the next time anyone presses Apply there.
	if (GEditor)
	{
		if (UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			if (Editors->FindEditorForAsset(MaterialOrFunction, /*bFocusIfOpen*/ false))
			{
				Result.SkippedReason = FText::Format(
					LOCTEXT("OpenInEditor", "is open in the material editor, which works on a copy and would overwrite "
						"these changes on Apply. {0} parameter(s) still need laying out - close it and sync again."),
					FText::AsNumber(Changes.Num()));
				return Result;
			}
		}
	}

	MaterialOrFunction->Modify();
	for (const FChange& Change : Changes)
	{
		Change.Node.Expression->Modify();
		if (*Change.Node.Name != Change.NewName)
		{
			*Change.Node.Name = Change.NewName;
			++Result.ParametersRenamed;
		}
		if (!*Change.Node.bUseCustomPrimitiveData || *Change.Node.Slot != Change.NewSlot)
		{
			*Change.Node.bUseCustomPrimitiveData = true;
			*Change.Node.Slot = Change.NewSlot;
			++Result.ParametersWritten;
		}
	}

	if (Material)
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
	}
	else
	{
		UMaterialEditingLibrary::UpdateMaterialFunction(Function, nullptr);
	}
	MaterialOrFunction->MarkPackageDirty();

	return Result;
}

void FPrimitiveDataLegendBinding::SyncAll(UPrimitiveDataLegend& Legend, bool bApply, const TMap<FName, FName>& Renames)
{
	FMessageLog Log(LogName);
	bool bProblems = false;
	int32 Written = 0;
	int32 Renamed = 0;
	int32 Checked = 0;

	for (const TSoftObjectPtr<UObject>& Bound : Legend.BoundMaterials)
	{
		if (Bound.IsNull())
		{
			continue;
		}

		UObject* Material = Bound.LoadSynchronous();
		if (!Material)
		{
			Log.Warning()
				->AddToken(FUObjectToken::Create(&Legend))
				->AddToken(FTextToken::Create(FText::Format(
					LOCTEXT("Unloadable", "binds {0}, which could not be loaded. Remove it from Bound Materials if it was deleted."),
					FText::FromString(Bound.ToString()))));
			bProblems = true;
			continue;
		}

		++Checked;
		const FPrimitiveDataBindingResult Result = Sync(Legend, Material, bApply, Renames);
		Written += Result.ParametersWritten;
		Renamed += Result.ParametersRenamed;
		if (!Result.IsClean())
		{
			PrimitiveDataBinding::Report(Log, Legend, Material, Result);
			bProblems = true;
		}
	}

	if (bProblems)
	{
		Log.Notify(FText::Format(LOCTEXT("Problems", "{0}: some bound materials need attention"),
			FText::FromString(Legend.GetName())), EMessageSeverity::Warning, /*bForce*/ true);
		return;
	}

	// Clean. Say so only when something actually happened - a no-op sync on every legend edit would
	// otherwise put a toast up for typing a name.
	if (Written > 0 || Renamed > 0 || !bApply)
	{
		FNotificationInfo Info(bApply
			? FText::Format(LOCTEXT("Synced", "{0}: laid out {1} parameter(s), renamed {2}, across {3} material(s)"),
				FText::FromString(Legend.GetName()), FText::AsNumber(Written), FText::AsNumber(Renamed), FText::AsNumber(Checked))
			: FText::Format(LOCTEXT("Checked", "{0}: all {1} bound material(s) match"),
				FText::FromString(Legend.GetName()), FText::AsNumber(Checked)));
		Info.ExpireDuration = 4.f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

TArray<UPrimitiveDataLegend*> FPrimitiveDataLegendBinding::FindLegendsBinding(const UObject* MaterialOrFunction)
{
	TArray<UPrimitiveDataLegend*> Out;
	if (!MaterialOrFunction)
	{
		return Out;
	}

	const FSoftObjectPath Path(MaterialOrFunction);
	TArray<FAssetData> Assets;
	FAssetRegistryModule::GetRegistry().GetAssetsByClass(UPrimitiveDataLegend::StaticClass()->GetClassPathName(), Assets);

	// Legends are a handful of small assets, so loading them to read the binding list is cheaper than
	// maintaining a searchable tag for it.
	for (const FAssetData& Asset : Assets)
	{
		UPrimitiveDataLegend* Legend = Cast<UPrimitiveDataLegend>(Asset.GetAsset());
		if (Legend && Legend->BoundMaterials.ContainsByPredicate(
			[&Path](const TSoftObjectPtr<UObject>& Bound) { return Bound.ToSoftObjectPath() == Path; }))
		{
			Out.Add(Legend);
		}
	}
	return Out;
}

void FPrimitiveDataLegendBinding::Bind(UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction)
{
	if (!MaterialOrFunction)
	{
		return;
	}

	for (UPrimitiveDataLegend* Other : FindLegendsBinding(MaterialOrFunction))
	{
		if (Other != &Legend)
		{
			Unbind(*Other, MaterialOrFunction);
		}
	}

	const FSoftObjectPath Path(MaterialOrFunction);
	if (Legend.BoundMaterials.ContainsByPredicate(
		[&Path](const TSoftObjectPtr<UObject>& Bound) { return Bound.ToSoftObjectPath() == Path; }))
	{
		SyncAll(Legend, /*bApply*/ true);
		return;
	}

	Legend.Modify();
	Legend.BoundMaterials.Add(TSoftObjectPtr<UObject>(MaterialOrFunction));
	// Through PostEditChange, not a direct sync: the legend notices its own change, re-snapshots, and
	// its listener syncs - the same path a details-panel edit takes, so there is only one.
	Legend.PostEditChange();
}

void FPrimitiveDataLegendBinding::Unbind(UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction)
{
	const FSoftObjectPath Path(MaterialOrFunction);
	Legend.Modify();
	const int32 Removed = Legend.BoundMaterials.RemoveAll(
		[&Path](const TSoftObjectPtr<UObject>& Bound) { return Bound.ToSoftObjectPath() == Path; });
	if (Removed > 0)
	{
		// The material keeps its layout. Unbinding stops the legend managing it; it does not undo
		// what the legend wrote, which would change how the material renders.
		Legend.PostEditChange();
	}
}

void FPrimitiveDataLegendBinding::ReportTo(FMessageLog& Log, const UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction,
	const FPrimitiveDataBindingResult& Result)
{
	PrimitiveDataBinding::Report(Log, Legend, MaterialOrFunction, Result);
}

#undef LOCTEXT_NAMESPACE
