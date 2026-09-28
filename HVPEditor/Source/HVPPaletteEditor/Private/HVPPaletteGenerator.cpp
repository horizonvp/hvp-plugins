#include "HVPPaletteGenerator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Factories/BlueprintFunctionLibraryFactory.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "Factories/Texture2dFactoryNew.h"
#include "HVPPaletteSettings.h"
#include "Math/Float16Color.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialParameterCollection.h"
#include "FileHelpers.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "ToolMenus.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "HVPPaletteGenerator"

DEFINE_LOG_CATEGORY_STATIC(LogHVPPalette, Log, All);

namespace HVPPaletteGen
{
	// "GlobalColors_MPC" -> "GlobalColors". Keeps the project's suffix convention intact: the
	// generated assets get their own suffix rather than inheriting the collection's.
	static FString BaseNameOf(const UMaterialParameterCollection& Collection)
	{
		FString Name = Collection.GetName();
		Name.RemoveFromEnd(TEXT("_MPC"));
		Name.RemoveFromStart(TEXT("MPC_"));
		return Name;
	}

	static FString FolderOf(const UMaterialParameterCollection& Collection)
	{
		return FPackageName::GetLongPackagePath(Collection.GetOutermost()->GetName());
	}
}

void FHVPPaletteGenerator::RegisterMenus()
{
	// Extending the MPC asset menu specifically, so the entry cannot appear on assets it would be
	// meaningless for. The name is the class name - UToolMenus builds one menu per asset type.
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(
		TEXT("ContentBrowser.AssetContextMenu.MaterialParameterCollection"));
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GetAssetActions"));
	Section.AddMenuEntry(
		TEXT("HVPGeneratePaletteAccessors"),
		LOCTEXT("GenerateLabel", "Generate Palette Accessors"),
		LOCTEXT("GenerateTooltip",
			"Create or refresh a material function and a Blueprint function library exposing every "
			"parameter in this collection as a named output pin, plus an 8x8 swatch texture that "
			"addresses those same entries by index. Which of the three are produced is set under "
			"Project Settings -> Plugins -> HVP Palette. Existing assets are updated in place, "
			"so anything already wired to them keeps working."),
		FSlateIcon(),
		FToolUIActionChoice(FToolMenuExecuteAction::CreateLambda(
			[](const FToolMenuContext& Context)
			{
				const UContentBrowserAssetContextMenuContext* CBContext =
					Context.FindContext<UContentBrowserAssetContextMenuContext>();
				if (!CBContext)
				{
					return;
				}

				// Whatever is enabled is generated TOGETHER, in one pass over the collection: the
				// outputs all describe the same palette, and refreshing one without the others is
				// how they drift apart. The texture is the sharp case - a stale cell hands back the
				// wrong colour rather than failing - so switching one off here has to mean it is not
				// this project's concern at all, never that it may lag behind the rest.
				const UHVPPaletteSettings& Settings = *GetDefault<UHVPPaletteSettings>();
				if (!Settings.bGenerateMaterialFunction && !Settings.bGenerateBlueprintLibrary
					&& !Settings.bGeneratePaletteTexture)
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("NothingEnabled",
							"Nothing is switched on to generate. Tick at least one output under "
							"Project Settings -> Plugins -> HVP Palette."));
					return;
				}

				int32 Made = 0;
				for (UMaterialParameterCollection* Collection :
					 CBContext->LoadSelectedObjects<UMaterialParameterCollection>())
				{
					if (!Collection)
					{
						continue;
					}
					bool bAny = false;
					if (Settings.bGenerateMaterialFunction)
					{
						bAny |= GenerateMaterialFunction(Collection) != nullptr;
					}
					if (Settings.bGenerateBlueprintLibrary)
					{
						bAny |= GenerateBlueprintLibrary(Collection) != nullptr;
					}
					if (Settings.bGeneratePaletteTexture)
					{
						bAny |= GeneratePaletteTexture(Collection) != nullptr;
					}
					if (bAny)
					{
						++Made;
					}
				}

				if (Made == 0)
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("NothingGenerated",
							"Nothing was generated. The selected collection has no scalar or vector "
							"parameters to expose."));
				}
			})));
}

namespace HVPPaletteGen
{
	// Generated assets are written, not authored, so leaving them dirty just means the next Save All
	// prompt asks about work nobody did by hand. Saved immediately instead.
	static void SaveGeneratedAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return;
		}
		TArray<UPackage*> Packages;
		Packages.Add(Asset->GetOutermost());
		UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty*/ false);
	}
}
using HVPPaletteGen::SaveGeneratedAsset;

UMaterialFunction* FHVPPaletteGenerator::GenerateMaterialFunction(UMaterialParameterCollection* Collection)
{
	if (!Collection)
	{
		return nullptr;
	}

	const TArray<FName> VectorNames = Collection->GetVectorParameterNames();
	const TArray<FName> ScalarNames = Collection->GetScalarParameterNames();
	if (VectorNames.Num() == 0 && ScalarNames.Num() == 0)
	{
		UE_LOG(LogHVPPalette, Warning, TEXT("'%s' has no parameters - nothing to generate."),
			*Collection->GetName());
		return nullptr;
	}

	const FString AssetName = HVPPaletteGen::BaseNameOf(*Collection) + TEXT("_MF");
	const FString Folder = HVPPaletteGen::FolderOf(*Collection);
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Folder, *AssetName, *AssetName);

	// Update in place when it already exists. Creating a fresh asset instead would orphan every
	// material already wired to the old one, which is exactly the breakage this tool is meant to
	// prevent - regenerating after adding a colour must never cost you your existing hookups.
	UMaterialFunction* Function = LoadObject<UMaterialFunction>(nullptr, *ObjectPath);
	const bool bExisted = (Function != nullptr);
	if (bExisted)
	{
		// NOT DeleteAllMaterialExpressionsInFunction. That helper iterates the expression collection
		// while deleting out of it, so every removal shifts the array under the loop and the next
		// element is skipped - roughly half the old nodes survive, and regenerating leaves a drift of
		// orphans piling up behind the new ones. Deleting from our own copy sidesteps it.
		for (UMaterialExpression* Stale : TArray<UMaterialExpression*>(Function->GetExpressions()))
		{
			UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Function, Stale);
		}
	}
	else
	{
		IAssetTools& AssetTools =
			FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
		Function = Cast<UMaterialFunction>(
			AssetTools.CreateAsset(AssetName, Folder, UMaterialFunction::StaticClass(), Factory));
	}

	if (!Function)
	{
		UE_LOG(LogHVPPalette, Error, TEXT("Could not create or load '%s'."), *ObjectPath);
		return nullptr;
	}

	Function->SetMaterialFunctionUsage(EMaterialFunctionUsage::Default);
	Function->Description = FString::Printf(
		TEXT("Generated from %s. Each output reads that parameter live from the collection, so ")
		TEXT("changing the collection changes every material using this. Do not hand-edit - ")
		TEXT("re-run 'Generate Palette Accessors' on the collection instead."),
		*Collection->GetName());

	// Laid out in a column so the generated graph is readable if someone opens it: the parameter
	// node on the left, its named output immediately to the right.
	int32 Row = 0;
	auto AddEntry = [&](FName ParameterName)
	{
		UMaterialExpressionCollectionParameter* Param =
			Cast<UMaterialExpressionCollectionParameter>(
				UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
					Function, UMaterialExpressionCollectionParameter::StaticClass(), -420, Row * 140));
		if (!Param)
		{
			return;
		}
		Param->Collection = Collection;
		Param->ParameterName = ParameterName;
		// ParameterId is the REAL link, not the name. Compilation resolves the value through
		// GetParameterIndex(ParameterId, ...), and PostLoad re-derives ParameterName FROM the id -
		// so an expression with a name but an empty GUID loses the name on the next load and reads
		// as "None" in the dropdown. This is the same assignment the engine's own
		// PostEditChangeProperty makes when you pick a name in the editor.
		Param->ParameterId = Collection->GetParameterId(ParameterName);

		UMaterialExpressionFunctionOutput* Output =
			Cast<UMaterialExpressionFunctionOutput>(
				UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
					Function, UMaterialExpressionFunctionOutput::StaticClass(), -80, Row * 140));
		if (!Output)
		{
			return;
		}
		Output->OutputName = ParameterName;
		// Sort priority mirrors declaration order, so the pins on the function node come out in the
		// same order as the parameters in the collection rather than alphabetised.
		Output->SortPriority = Row;

		UMaterialEditingLibrary::ConnectMaterialExpressions(Param, FString(), Output, FString());
		++Row;
	};

	for (const FName& Name : VectorNames)
	{
		AddEntry(Name);
	}
	for (const FName& Name : ScalarNames)
	{
		AddEntry(Name);
	}

	UMaterialEditingLibrary::UpdateMaterialFunction(Function);

	Function->MarkPackageDirty();
	if (!bExisted)
	{
		FAssetRegistryModule::AssetCreated(Function);
	}
	SaveGeneratedAsset(Function);

	UE_LOG(LogHVPPalette, Log, TEXT("%s '%s' with %d output(s) from '%s'."),
		bExisted ? TEXT("Refreshed") : TEXT("Created"), *AssetName, Row, *Collection->GetName());

	return Function;
}

UBlueprint* FHVPPaletteGenerator::GenerateBlueprintLibrary(UMaterialParameterCollection* Collection)
{
	if (!Collection)
	{
		return nullptr;
	}

	const TArray<FName> VectorNames = Collection->GetVectorParameterNames();
	const TArray<FName> ScalarNames = Collection->GetScalarParameterNames();
	if (VectorNames.Num() == 0 && ScalarNames.Num() == 0)
	{
		return nullptr;
	}

	const FString Base = HVPPaletteGen::BaseNameOf(*Collection);
	const FString AssetName = Base + TEXT("_BFL");
	const FString Folder = HVPPaletteGen::FolderOf(*Collection);
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Folder, *AssetName, *AssetName);
	// "GetPaletteGlobalColors" - renders in a graph as "Get Palette Global Colors". The name
	// carries the palette so two collections never produce clashing nodes, and the Category and
	// ToolTip set on the entry below mark it as generated rather than hand-written.
	const FName FunctionName(*(TEXT("GetPalette") + Base));

	// Same in-place rule as the material function: keep the asset, replace only the graph, so every
	// Blueprint already calling this keeps its wiring when the palette gains an entry.
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	const bool bExisted = (Blueprint != nullptr);
	if (!bExisted)
	{
		IAssetTools& AssetTools =
			FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		// The DEDICATED factory, not the general UBlueprintFactory. A function library is a distinct
		// blueprint TYPE (BPTYPE_FunctionLibrary), not merely a blueprint with a different parent -
		// the general factory rejects the parent outright with "Cannot create a blueprint based on
		// the class BlueprintFunctionLibrary".
		UBlueprintFunctionLibraryFactory* Factory = NewObject<UBlueprintFunctionLibraryFactory>();
		Factory->ParentClass = UBlueprintFunctionLibrary::StaticClass();
		Blueprint = Cast<UBlueprint>(
			AssetTools.CreateAsset(AssetName, Folder, UBlueprint::StaticClass(), Factory));
	}
	if (!Blueprint)
	{
		UE_LOG(LogHVPPalette, Error, TEXT("Could not create or load '%s'."), *ObjectPath);
		return nullptr;
	}

	// Drop our own previous graph (regeneration) and the empty "NewFunction" the factory seeds a
	// fresh library with - otherwise opening the asset for the first time presents a blank function
	// alongside the generated one.
	for (UEdGraph* Existing : TArray<UEdGraph*>(Blueprint->FunctionGraphs))
	{
		if (Existing && (Existing->GetFName() == FunctionName
			|| Existing->GetFName() == TEXT("NewFunction")))
		{
			FBlueprintEditorUtils::RemoveGraph(Blueprint, Existing);
		}
	}

	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FunctionName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);

	// Pure, so it drops anywhere a colour is wanted without an execution wire. The calls inside are
	// impure Engine nodes, which is fine: a pure function may run whatever logic it likes internally,
	// it simply runs on demand rather than at a fixed point in the caller's execution. Safe here
	// because those calls only READ the collection - nothing in this graph has a side effect.
	TArray<UK2Node_FunctionEntry*> Entries;
	Graph->GetNodesOfClass(Entries);
	if (Entries.Num() > 0)
	{
		Entries[0]->AddExtraFlags(FUNC_BlueprintPure);
		Entries[0]->NodePosX = -400;
		Entries[0]->MetaData.Category = FText::FromString(TEXT("HVP Palette|Generated"));
		Entries[0]->MetaData.Keywords = FText::FromString(
			FString::Printf(TEXT("palette colour color generated horizon %s"), *Base));
		Entries[0]->MetaData.ToolTip = FText::FromString(FString::Printf(
			TEXT("Generated from %s. Every output reads that parameter live from the collection.\n\n")
			TEXT("Built on Engine nodes only, so this asset works in projects without the ")
			TEXT("HVPPalette plugin - only the collection travels with it.\n\n")
			TEXT("Do not edit by hand - re-run 'Generate Palette Accessors' on the collection, which ")
			TEXT("rebuilds this graph in place and keeps existing call sites wired."),
			*Collection->GetName()));
	}

	UK2Node_FunctionResult* Result = nullptr;
	TArray<UK2Node_FunctionResult*> Results;
	Graph->GetNodesOfClass(Results);
	if (Results.Num() > 0)
	{
		Result = Results[0];
	}
	else
	{
		FGraphNodeCreator<UK2Node_FunctionResult> Creator(*Graph);
		Result = Creator.CreateNode();
		Creator.Finalize();
	}
	Result->NodePosX = 600;

	// The hidden member every Blueprint Function Library function carries. Reading it and feeding it
	// to the calls is what makes the generated function work at construction-script time, where a
	// world resolved from engine globals is not yet the right one. One node, shared by every call.
	UK2Node_VariableGet* WorldContextNode = nullptr;
	{
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
		WorldContextNode = Creator.CreateNode();
		WorldContextNode->VariableReference.SetLocalMember(
			TEXT("__WorldContext"), FunctionName.ToString(), FGuid());
		WorldContextNode->NodePosX = -150;
		WorldContextNode->NodePosY = -160;
		Creator.Finalize();
	}
	UEdGraphPin* WorldContextPin =
		WorldContextNode ? WorldContextNode->FindPin(TEXT("__WorldContext"), EGPD_Output) : nullptr;

	// ENGINE functions, deliberately - not this plugin's wrapper.
	//
	// A generated asset must not depend on the tool that generated it. Routing through a wrapper
	// in this plugin would bake a plugin class into the Blueprint, so handing the library to someone
	// without HVPPalette installed would break every node in it. Built on UKismetMaterialLibrary,
	// the generated assets reference nothing but Engine and the collection itself, and travel with
	// ordinary project content. (The material function was already free of this - it is Engine
	// expressions throughout.)
	//
	// The cost is that these two are BlueprintCallable rather than pure, so the graph needs an exec
	// chain through them. That stays hidden: the generated function is still marked pure, and a pure
	// function is free to run impure logic internally - it just runs whenever its output is asked
	// for rather than at a fixed point in the caller's execution.
	UFunction* ColorFn = UKismetMaterialLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetMaterialLibrary, GetVectorParameterValue));
	UFunction* ScalarFn = UKismetMaterialLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetMaterialLibrary, GetScalarParameterValue));

	// Threaded through every call in turn, then into the result node. Impure nodes that are never
	// reached simply do not run, so a missing link here would leave outputs silently at their
	// defaults rather than failing loudly.
	UEdGraphPin* PrevExec = (Entries.Num() > 0)
		? Entries[0]->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output)
		: nullptr;

	int32 Row = 0;
	auto AddEntry = [&](FName ParameterName, bool bVector)
	{
		UFunction* Fn = bVector ? ColorFn : ScalarFn;
		if (!Fn || !Result)
		{
			return;
		}

		UK2Node_CallFunction* Call = nullptr;
		{
			FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
			Call = Creator.CreateNode();
			Call->SetFromFunction(Fn);
			Call->NodePosX = 150;
			Call->NodePosY = Row * 130;
			Creator.Finalize();
		}

		// Both arguments are baked in, which is the whole point: the caller picks a pin, never a
		// collection asset or a parameter string.
		if (PrevExec)
		{
			if (UEdGraphPin* CallExecIn = Call->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input))
			{
				PrevExec->MakeLinkTo(CallExecIn);
				PrevExec = Call->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			}
		}

		if (WorldContextPin)
		{
			if (UEdGraphPin* WCPin = Call->FindPin(TEXT("WorldContextObject")))
			{
				WorldContextPin->MakeLinkTo(WCPin);
			}
		}
		if (UEdGraphPin* CollectionPin = Call->FindPin(TEXT("Collection")))
		{
			CollectionPin->DefaultObject = Collection;
		}
		if (UEdGraphPin* NamePin = Call->FindPin(TEXT("ParameterName")))
		{
			NamePin->DefaultValue = ParameterName.ToString();
		}

		FEdGraphPinType PinType;
		if (bVector)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
		}
		else
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		}

		// A function's OUTPUTS are inputs on its result node - hence EGPD_Input here.
		if (UEdGraphPin* OutPin = Result->CreateUserDefinedPin(ParameterName, PinType, EGPD_Input))
		{
			if (UEdGraphPin* ReturnPin = Call->GetReturnValuePin())
			{
				OutPin->MakeLinkTo(ReturnPin);
			}
		}
		++Row;
	};

	for (const FName& Name : VectorNames) { AddEntry(Name, true); }
	for (const FName& Name : ScalarNames) { AddEntry(Name, false); }

	// Close the chain into the result node. PrevExec is the last call's Then pin by now, or the
	// entry's own if the collection somehow produced no calls at all.
	if (PrevExec && Result)
	{
		if (UEdGraphPin* ResultExec = Result->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input))
		{
			PrevExec->MakeLinkTo(ResultExec);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// The asset already HAS its function, so it must not look newly created any more. The Blueprint
	// editor reads bIsNewlyCreated on first open and, for a library, seeds an empty "NewFunction"
	// and drops straight into renaming it - which is where that stray blank function was coming
	// from. It clears the flag itself on open, so this only ever suppresses that one-time setup.
	Blueprint->bIsNewlyCreated = false;

	Blueprint->MarkPackageDirty();
	if (!bExisted)
	{
		FAssetRegistryModule::AssetCreated(Blueprint);
	}
	SaveGeneratedAsset(Blueprint);

	UE_LOG(LogHVPPalette, Log, TEXT("%s '%s::%s' with %d output pin(s)."),
		bExisted ? TEXT("Refreshed") : TEXT("Created"), *AssetName, *FunctionName.ToString(), Row);

	return Blueprint;
}

namespace HVPPaletteGen
{
	// 8x8 = 64 cells, which is the whole point: one scalar in 0-63 addresses any entry, and the
	// material side turns that number into a UV with a modulo and a divide.
	static constexpr int32 GridCells = 8;
	static constexpr int32 MaxEntries = GridCells * GridCells;

	// Each swatch is a BLOCK of pixels rather than a single texel. The sampling maths does not care
	// - a cell centre is a cell centre at any resolution - but 8x8 is unreadable as a thumbnail,
	// and a block gives an incoming UV a whole cell of slack instead of one texel's, so an index
	// that arrives slightly off still lands on the right colour. 64x64 RGBA16F is 32 KB.
	static constexpr int32 CellPixels = 8;
	static constexpr int32 TextureSize = GridCells * CellPixels;
}

UTexture2D* FHVPPaletteGenerator::GeneratePaletteTexture(UMaterialParameterCollection* Collection)
{
	using namespace HVPPaletteGen;

	if (!Collection)
	{
		return nullptr;
	}

	// Deliberately the same two calls in the same order that build the output pins above. Cell N
	// and pin N have to be the same palette entry, or the texture is lying about what it holds.
	const TArray<FName> VectorNames = Collection->GetVectorParameterNames();
	const TArray<FName> ScalarNames = Collection->GetScalarParameterNames();
	if (VectorNames.Num() == 0 && ScalarNames.Num() == 0)
	{
		return nullptr;
	}

	TArray<TPair<FName, FLinearColor>> Entries;
	Entries.Reserve(VectorNames.Num() + ScalarNames.Num());
	for (const FName& Name : VectorNames)
	{
		bool bFound = false;
		Entries.Emplace(Name, Collection->GetVectorParameterDefaultValue(Name, bFound));
	}
	for (const FName& Name : ScalarNames)
	{
		// Scalars ride along as grey so ONE index scheme covers the whole collection. Skipping them
		// would work too, right up until someone reorders the collection and the two halves of the
		// palette disagree about which entry index 12 is. Alpha 1, so a scalar read out of the RGB
		// channels is the value and nothing else.
		bool bFound = false;
		const float Value = Collection->GetScalarParameterDefaultValue(Name, bFound);
		Entries.Emplace(Name, FLinearColor(Value, Value, Value, 1.0f));
	}

	if (Entries.Num() > MaxEntries)
	{
		// Truncating rather than growing the grid. The grid size is a contract with whatever maps
		// index to UV on the material side; quietly making it 16x16 would move every index that
		// already works. Losing the tail is visible in the log - moving all of them is not.
		UE_LOG(LogHVPPalette, Warning,
			TEXT("'%s' has %d parameters but the 8x8 palette holds %d. Entries after '%s' are not in ")
			TEXT("the texture - the generated pins still cover every one of them."),
			*Collection->GetName(), Entries.Num(), MaxEntries,
			*Entries[MaxEntries - 1].Key.ToString());
		Entries.SetNum(MaxEntries);
	}

	// Zeroed, so an unfilled cell is transparent black - which an authored black, carrying alpha 1,
	// is not. Sampling past the end of the palette is therefore something you can test for.
	TArray<FFloat16Color> Pixels;
	Pixels.SetNumZeroed(TextureSize * TextureSize);
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FFloat16Color Swatch(Entries[Index].Value);
		const int32 OriginX = (Index % GridCells) * CellPixels;
		const int32 OriginY = (Index / GridCells) * CellPixels;
		for (int32 Y = 0; Y < CellPixels; ++Y)
		{
			FFloat16Color* Row = Pixels.GetData() + (OriginY + Y) * TextureSize + OriginX;
			for (int32 X = 0; X < CellPixels; ++X)
			{
				Row[X] = Swatch;
			}
		}
	}

	const FString AssetName = HVPPaletteGen::BaseNameOf(*Collection) + TEXT("_T");
	const FString Folder = HVPPaletteGen::FolderOf(*Collection);
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Folder, *AssetName, *AssetName);

	// In place, for the same reason as the other two: adding a colour to the palette must not cost
	// a material its texture reference.
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
	const bool bExisted = (Texture != nullptr);
	if (!bExisted)
	{
		IAssetTools& AssetTools =
			FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UTexture2DFactoryNew* Factory = NewObject<UTexture2DFactoryNew>();
		// The factory refuses non-power-of-two dimensions and seeds a white BGRA8 mip chain that the
		// Init below replaces outright. Only the asset shell is wanted from it.
		Factory->Width = TextureSize;
		Factory->Height = TextureSize;
		Texture = Cast<UTexture2D>(
			AssetTools.CreateAsset(AssetName, Folder, UTexture2D::StaticClass(), Factory));
	}
	if (!Texture)
	{
		UE_LOG(LogHVPPalette, Error, TEXT("Could not create or load '%s'."), *ObjectPath);
		return nullptr;
	}

	// Source.Init has to run BETWEEN these two - it rehashes the source GUID, and the texture only
	// throws away its built platform data and rebuilds on the PostEditChange. Setting the source
	// outside that window leaves the old pixels on screen until something else touches the asset.
	Texture->PreEditChange(nullptr);

	Texture->Source.Init(TextureSize, TextureSize, /*Slices*/ 1, /*Mips*/ 1, TSF_RGBA16F,
		reinterpret_cast<const uint8*>(Pixels.GetData()));

	// Uncompressed RGBA16F with sRGB off - that is what TC_HDR is - so a sample returns the
	// collection's FLinearColor as authored, values above 1 included. An 8-bit sRGB texture would
	// round every swatch and clamp the emissive ones, which is the wrong trade for something meant
	// to hand back the same colour the pins do.
	Texture->CompressionSettings = TC_HDR;
	Texture->SRGB = false;
	// No mips, nearest filtering. One mip down is already four swatches averaged into one, and
	// bilinear across a cell boundary blends two colours that were never meant to meet.
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->Filter = TF_Nearest;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	// The LUT group and NeverStream keep it at full resolution on every platform. A downscaled or
	// half-streamed palette does not look like a missing texture - it hands back a neighbouring
	// colour, and reads as a bug in whatever sampled it.
	Texture->LODGroup = TEXTUREGROUP_ColorLookupTable;
	Texture->NeverStream = true;

	Texture->PostEditChange();

	Texture->MarkPackageDirty();
	if (!bExisted)
	{
		FAssetRegistryModule::AssetCreated(Texture);
	}
	SaveGeneratedAsset(Texture);

	// The index IS the interface here, and unlike a pin it is a bare number with no name on it, so
	// the map goes to the log on every run - where it can be checked against what the material does.
	UE_LOG(LogHVPPalette, Log,
		TEXT("%s '%s' - %dx%d, %d of %d cells filled from '%s'. Index map:"),
		bExisted ? TEXT("Refreshed") : TEXT("Created"), *AssetName, TextureSize, TextureSize,
		Entries.Num(), MaxEntries, *Collection->GetName());
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		UE_LOG(LogHVPPalette, Log, TEXT("    %2d  (col %d, row %d)  %s"),
			Index, Index % GridCells, Index / GridCells, *Entries[Index].Key.ToString());
	}

	return Texture;
}

#undef LOCTEXT_NAMESPACE
