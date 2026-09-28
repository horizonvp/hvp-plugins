#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Self.h"
#include "K2Node_SetIndexedPrimitiveData.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "PrimitiveDataIndex.h"
#include "PrimitiveDataIndexBinding.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace PrimitiveDataIndexTests
{
	FPrimitiveDataIndexEntry& Add(UPrimitiveDataIndex* Index, const TCHAR* Name, EPrimitiveDataParameterType Type)
	{
		FPrimitiveDataIndexEntry& Entry = Index->Parameters.AddDefaulted_GetRef();
		Entry.Name = Name;
		Entry.Type = Type;
		return Entry;
	}

	int32 SlotOf(const UPrimitiveDataIndex* Index, const TCHAR* Name)
	{
		const FPrimitiveDataIndexEntry* Entry = Index->FindParameter(FName(Name));
		return Entry ? Entry->Slot : -2;
	}

	template <typename T>
	T* AddParameter(UMaterial* Material, const TCHAR* Name)
	{
		T* Expression = Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(Material, T::StaticClass()));
		Expression->ParameterName = Name;
		return Expression;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrimitiveDataIndexSlotTest, "HVP.PrimitiveData.SlotAllocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPrimitiveDataIndexSlotTest::RunTest(const FString& Parameters)
{
	using namespace PrimitiveDataIndexTests;
	using EType = EPrimitiveDataParameterType;

	// Strong refs: nothing else holds these, and the engine is free to collect garbage between steps.
	const TStrongObjectPtr<UPrimitiveDataIndex> Owner(NewObject<UPrimitiveDataIndex>(GetTransientPackage()));
	UPrimitiveDataIndex* Index = Owner.Get();

	Add(Index, TEXT("A"), EType::Scalar);
	Add(Index, TEXT("B"), EType::Vector);
	Add(Index, TEXT("C"), EType::Scalar);
	Index->PostEditChange();

	TestEqual(TEXT("A packs first"), SlotOf(Index, TEXT("A")), 0);
	TestEqual(TEXT("B takes the next four"), SlotOf(Index, TEXT("B")), 1);
	TestEqual(TEXT("C follows B"), SlotOf(Index, TEXT("C")), 5);
	TestEqual(TEXT("Used floats"), Index->GetUsedFloats(), 6);

	// The entry that CHANGED moves; its neighbours do not. A at 0 cannot grow to four floats there
	// (B owns 1-4), so A must relocate - B and C must stay exactly where they were.
	Index->Parameters[0].Type = EType::Vector;
	Index->PostEditChange();
	TestEqual(TEXT("B unmoved when A grows"), SlotOf(Index, TEXT("B")), 1);
	TestEqual(TEXT("C unmoved when A grows"), SlotOf(Index, TEXT("C")), 5);
	TestEqual(TEXT("A relocated to first free run of four"), SlotOf(Index, TEXT("A")), 6);

	// Removing and reordering never move anyone.
	Index->Parameters.RemoveAt(1); // B
	Index->Parameters.Swap(0, 1);  // C before A
	Index->PostEditChange();
	TestEqual(TEXT("C unmoved by removal and reorder"), SlotOf(Index, TEXT("C")), 5);
	TestEqual(TEXT("A unmoved by removal and reorder"), SlotOf(Index, TEXT("A")), 6);

	// A new vector takes the first free run - the gap B left.
	Add(Index, TEXT("D"), EType::Vector);
	Index->PostEditChange();
	TestEqual(TEXT("D fills the freed run"), SlotOf(Index, TEXT("D")), 0);

	// A duplicated element keeps the original in place and moves the copy, with a fresh identity.
	const FGuid OriginalId = Index->FindParameter(FName(TEXT("C")))->Id;
	FPrimitiveDataIndexEntry Copy = *Index->FindParameter(FName(TEXT("C")));
	Copy.Name = TEXT("C2");
	Index->Parameters.Add(Copy);
	Index->PostEditChange();
	TestEqual(TEXT("Original keeps its slot"), SlotOf(Index, TEXT("C")), 5);
	TestNotEqual(TEXT("Copy moved"), SlotOf(Index, TEXT("C2")), 5);
	TestNotEqual(TEXT("Copy has its own identity"), Index->FindParameter(FName(TEXT("C2")))->Id, OriginalId);

	// Nameless new entries get a name a dropdown can show.
	Index->Parameters.AddDefaulted();
	Index->PostEditChange();
	TestFalse(TEXT("New entry named"), Index->Parameters.Last().Name.IsNone());

	// Overflow: fill well past 36 floats. Every entry that fits gets a slot; nothing overlaps.
	for (int32 i = 0; i < 12; ++i)
	{
		Add(Index, *FString::Printf(TEXT("V%d"), i), EType::Vector);
	}
	Index->PostEditChange();
	TestTrue(TEXT("Never over capacity"), Index->GetUsedFloats() <= UPrimitiveDataIndex::GetCapacity());

	TBitArray<> Claimed(false, UPrimitiveDataIndex::GetCapacity());
	bool bOverlap = false;
	int32 Unslotted = 0;
	for (const FPrimitiveDataIndexEntry& Entry : Index->Parameters)
	{
		if (!Entry.HasSlot())
		{
			++Unslotted;
			continue;
		}
		for (int32 i = Entry.Slot; i < Entry.Slot + Entry.GetWidth(); ++i)
		{
			bOverlap |= Claimed[i];
			Claimed[i] = true;
		}
	}
	TestFalse(TEXT("No two parameters share a float"), bOverlap);
	TestTrue(TEXT("Overflowing entries are left unslotted, not squeezed in"), Unslotted > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrimitiveDataIndexBindingTest, "HVP.PrimitiveData.MaterialBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPrimitiveDataIndexBindingTest::RunTest(const FString& Parameters)
{
	using namespace PrimitiveDataIndexTests;
	using EType = EPrimitiveDataParameterType;

	// Strong refs: recompiling a material can let the garbage collector run, and nothing else holds
	// these. (Real bindings only ever touch saved assets, which stay loaded.)
	const TStrongObjectPtr<UPrimitiveDataIndex> IndexOwner(NewObject<UPrimitiveDataIndex>(GetTransientPackage()));
	UPrimitiveDataIndex* Index = IndexOwner.Get();
	Add(Index, TEXT("Roughness"), EType::Scalar);
	Add(Index, TEXT("Tint"), EType::Vector);
	Add(Index, TEXT("Glow"), EType::Scalar);
	Index->bRequireAllParameters = true;
	Index->PostEditChange();

	// Shaped like a real asset - its own package, public and standalone - because that is what binding
	// is used on, and a bare transient material is not a fair stand-in for the recompile path.
	UPackage* Package = CreatePackage(TEXT("/Temp/HVPPrimitiveDataTest/M_BindingTest"));
	const TStrongObjectPtr<UMaterial> MaterialOwner(
		NewObject<UMaterial>(Package, TEXT("M_BindingTest"), RF_Public | RF_Standalone | RF_Transactional));
	UMaterial* Material = MaterialOwner.Get();
	UMaterialExpressionScalarParameter* Roughness = AddParameter<UMaterialExpressionScalarParameter>(Material, TEXT("Roughness"));
	UMaterialExpressionVectorParameter* Tint = AddParameter<UMaterialExpressionVectorParameter>(Material, TEXT("Tint"));
	UMaterialExpressionScalarParameter* Ordinary = AddParameter<UMaterialExpressionScalarParameter>(Material, TEXT("Metallic"));
	UMaterialExpressionScalarParameter* Stray = AddParameter<UMaterialExpressionScalarParameter>(Material, TEXT("Leftover"));
	Stray->bUseCustomPrimitiveData = true;
	Stray->PrimitiveDataIndex = 20;

	// A check first: nothing may change.
	const FPrimitiveDataBindingResult Check = FPrimitiveDataIndexBinding::Sync(*Index, Material, /*bApply*/ false);
	TestFalse(TEXT("Check does not write"), Roughness->bUseCustomPrimitiveData);
	TestEqual(TEXT("Check writes nothing"), Check.ParametersWritten, 0);

	const FPrimitiveDataBindingResult Result = FPrimitiveDataIndexBinding::Sync(*Index, Material, /*bApply*/ true);

	TestTrue(TEXT("Roughness switched to CPD"), Roughness->bUseCustomPrimitiveData);
	TestEqual(TEXT("Roughness on its slot"), static_cast<int32>(Roughness->PrimitiveDataIndex), SlotOf(Index, TEXT("Roughness")));
	TestTrue(TEXT("Tint switched to CPD"), Tint->bUseCustomPrimitiveData);
	TestEqual(TEXT("Tint on its slot"), static_cast<int32>(Tint->PrimitiveDataIndex), SlotOf(Index, TEXT("Tint")));
	TestEqual(TEXT("Two parameters written"), Result.ParametersWritten, 2);

	TestFalse(TEXT("An ordinary parameter is left alone"), Ordinary->bUseCustomPrimitiveData);
	TestTrue(TEXT("A stray CPD parameter is NOT switched off"), Stray->bUseCustomPrimitiveData);

	// Exactly two errors: the stray ("only") and the missing Glow ("all", required).
	TestEqual(TEXT("Stray + missing reported as errors"), Result.Errors.Num(), 2);

	// Idempotent: a second sync writes nothing.
	const FPrimitiveDataBindingResult Again = FPrimitiveDataIndexBinding::Sync(*Index, Material, /*bApply*/ true);
	TestEqual(TEXT("Second sync is a no-op"), Again.ParametersWritten, 0);

	// With "all" not required, the missing parameter drops to a warning.
	Index->bRequireAllParameters = false;
	const FPrimitiveDataBindingResult Relaxed = FPrimitiveDataIndexBinding::Sync(*Index, Material, /*bApply*/ false);
	TestEqual(TEXT("Only the stray remains an error"), Relaxed.Errors.Num(), 1);
	TestEqual(TEXT("Missing becomes a warning"), Relaxed.Warnings.Num(), 1);

	// A rename in the index follows into the material.
	TMap<FName, FName> Renames;
	Renames.Add(TEXT("Tint"), TEXT("BaseTint"));
	Index->Parameters[1].Name = TEXT("BaseTint");
	const FPrimitiveDataBindingResult Renamed = FPrimitiveDataIndexBinding::Sync(*Index, Material, /*bApply*/ true, Renames);
	TestEqual(TEXT("Material parameter relabelled"), Tint->ParameterName, FName(TEXT("BaseTint")));
	TestEqual(TEXT("One rename counted"), Renamed.ParametersRenamed, 1);

	// A type mismatch is reported, never written.
	UMaterialExpressionVectorParameter* WrongType = AddParameter<UMaterialExpressionVectorParameter>(Material, TEXT("Roughness"));
	const FPrimitiveDataBindingResult Mismatch = FPrimitiveDataIndexBinding::Sync(*Index, Material, /*bApply*/ true);
	TestFalse(TEXT("Vector named like a scalar entry is not switched"), WrongType->bUseCustomPrimitiveData);
	TestTrue(TEXT("Type mismatch reported"), Mismatch.Errors.ContainsByPredicate(
		[](const FText& Error) { return Error.ToString().Contains(TEXT("vector parameter here")); }));

	Material->ClearFlags(RF_Standalone | RF_Public);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSetIndexedPrimitiveDataNodeTest, "HVP.PrimitiveData.NodeCompilesAndRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * End to end: a Blueprint using the node compiles, and running it writes the right floats into the
 * right slots. This is the only way to prove the expansion - a wrong intermediate pin name compiles
 * fine in C++ and only fails when a Blueprint does.
 */
bool FSetIndexedPrimitiveDataNodeTest::RunTest(const FString& Parameters)
{
	using namespace PrimitiveDataIndexTests;
	using EType = EPrimitiveDataParameterType;

	const TStrongObjectPtr<UPrimitiveDataIndex> IndexOwner(NewObject<UPrimitiveDataIndex>(GetTransientPackage()));
	UPrimitiveDataIndex* Index = IndexOwner.Get();
	Add(Index, TEXT("Pad"), EType::Scalar);        // slot 0 - so nothing lands at 0 by accident
	Add(Index, TEXT("Roughness"), EType::Scalar);  // slot 1
	Add(Index, TEXT("Tint"), EType::Vector);       // slots 2-5
	Index->PostEditChange();
	const int32 RoughnessSlot = SlotOf(Index, TEXT("Roughness"));
	const int32 TintSlot = SlotOf(Index, TEXT("Tint"));

	// A Blueprint of a mesh component, so a Self node can feed Target.
	UPackage* Package = CreatePackage(TEXT("/Temp/HVPPrimitiveDataTest/BP_NodeTest"));
	const TStrongObjectPtr<UBlueprint> BlueprintOwner(FKismetEditorUtilities::CreateBlueprint(
		UStaticMeshComponent::StaticClass(), Package, TEXT("BP_NodeTest"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	UBlueprint* Blueprint = BlueprintOwner.Get();
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("Event graph"), Graph))
	{
		return false;
	}
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
	Event->CustomFunctionName = TEXT("RunTest");
	Graph->AddNode(Event, false, false);
	Event->CreateNewGuid();
	Event->AllocateDefaultPins();

	UK2Node_Self* Self = NewObject<UK2Node_Self>(Graph);
	Graph->AddNode(Self, false, false);
	Self->CreateNewGuid();
	Self->AllocateDefaultPins();

	auto AddSetNode = [Graph]()
	{
		UK2Node_SetIndexedPrimitiveData* Node = NewObject<UK2Node_SetIndexedPrimitiveData>(Graph);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		Node->AllocateDefaultPins();
		return Node;
	};

	// Scalar. Choosing the index auto-selects its first parameter; then pick Roughness.
	UK2Node_SetIndexedPrimitiveData* SetScalar = AddSetNode();
	TestNull(TEXT("No Value pin before a parameter is chosen"), SetScalar->FindPin(UK2Node_SetIndexedPrimitiveData::ValuePinName));
	Schema->TrySetDefaultObject(*SetScalar->FindPinChecked(UK2Node_SetIndexedPrimitiveData::IndexPinName), Index);
	TestEqual(TEXT("Choosing an index selects its first parameter"),
		SetScalar->FindPinChecked(UK2Node_SetIndexedPrimitiveData::ParameterPinName)->DefaultValue, FString(TEXT("Pad")));
	Schema->TrySetDefaultValue(*SetScalar->FindPinChecked(UK2Node_SetIndexedPrimitiveData::ParameterPinName), TEXT("Roughness"));
	UEdGraphPin* ScalarValue = SetScalar->FindPin(UK2Node_SetIndexedPrimitiveData::ValuePinName);
	if (!TestNotNull(TEXT("Scalar Value pin"), ScalarValue))
	{
		return false;
	}
	TestEqual(TEXT("Scalar Value pin is a float"), ScalarValue->PinType.PinCategory, UEdGraphSchema_K2::PC_Real);
	Schema->TrySetDefaultValue(*ScalarValue, TEXT("0.625"));

	// Vector. Switching the parameter morphs the Value pin to a Linear Color.
	UK2Node_SetIndexedPrimitiveData* SetVector = AddSetNode();
	Schema->TrySetDefaultObject(*SetVector->FindPinChecked(UK2Node_SetIndexedPrimitiveData::IndexPinName), Index);
	Schema->TrySetDefaultValue(*SetVector->FindPinChecked(UK2Node_SetIndexedPrimitiveData::ParameterPinName), TEXT("Tint"));
	UEdGraphPin* VectorValue = SetVector->FindPin(UK2Node_SetIndexedPrimitiveData::ValuePinName);
	if (!TestNotNull(TEXT("Vector Value pin"), VectorValue))
	{
		return false;
	}
	TestTrue(TEXT("Vector Value pin is a Linear Color"),
		VectorValue->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
		&& VectorValue->PinType.PinSubCategoryObject == TBaseStructure<FLinearColor>::Get());
	Schema->TrySetDefaultValue(*VectorValue, TEXT("(R=0.100000,G=0.200000,B=0.300000,A=0.400000)"));

	// Array target: Self through a Make Array into a third node. The schema must accept an array on
	// Target, and the engine's for-each expansion must reach the component.
	UK2Node_SetIndexedPrimitiveData* SetViaArray = AddSetNode();
	Schema->TrySetDefaultObject(*SetViaArray->FindPinChecked(UK2Node_SetIndexedPrimitiveData::IndexPinName), Index);
	Schema->TrySetDefaultValue(*SetViaArray->FindPinChecked(UK2Node_SetIndexedPrimitiveData::ParameterPinName), TEXT("Pad"));
	Schema->TrySetDefaultValue(*SetViaArray->FindPinChecked(UK2Node_SetIndexedPrimitiveData::ValuePinName), TEXT("0.5"));

	UK2Node_MakeArray* MakeArray = NewObject<UK2Node_MakeArray>(Graph);
	Graph->AddNode(MakeArray, false, false);
	MakeArray->CreateNewGuid();
	MakeArray->AllocateDefaultPins();

	// Event -> SetScalar -> SetVector -> SetViaArray, Self into the first two Targets directly.
	bool bWired = true;
	bWired &= Schema->TryCreateConnection(Event->FindPinChecked(UEdGraphSchema_K2::PN_Then), SetScalar->GetExecPin());
	bWired &= Schema->TryCreateConnection(SetScalar->FindPinChecked(UEdGraphSchema_K2::PN_Then), SetVector->GetExecPin());
	bWired &= Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self),
		SetScalar->FindPinChecked(UK2Node_SetIndexedPrimitiveData::TargetPinName));
	bWired &= Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self),
		SetVector->FindPinChecked(UK2Node_SetIndexedPrimitiveData::TargetPinName));
	bWired &= Schema->TryCreateConnection(SetVector->FindPinChecked(UEdGraphSchema_K2::PN_Then), SetViaArray->GetExecPin());
	TestTrue(TEXT("Graph wired"), bWired);

	UEdGraphPin* FirstElement = nullptr;
	for (UEdGraphPin* Pin : MakeArray->Pins)
	{
		if (Pin->Direction == EGPD_Input)
		{
			FirstElement = Pin;
			break;
		}
	}
	bool bArrayWired = FirstElement && Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self), FirstElement);
	bArrayWired &= Schema->TryCreateConnection(MakeArray->GetOutputPin(),
		SetViaArray->FindPinChecked(UK2Node_SetIndexedPrimitiveData::TargetPinName));
	TestTrue(TEXT("An array is accepted on Target"), bArrayWired);

	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		if (Message->GetSeverity() == EMessageSeverity::Error)
		{
			AddError(FString::Printf(TEXT("Compile: %s"), *Message->ToText().ToString()));
		}
	}
	if (!TestEqual(TEXT("Compiles without errors"), Results.NumErrors, 0) || !Blueprint->GeneratedClass)
	{
		return false;
	}

	// Run it on a real component and read the data back.
	const TStrongObjectPtr<UStaticMeshComponent> Component(
		NewObject<UStaticMeshComponent>(GetTransientPackage(), Blueprint->GeneratedClass));
	UFunction* Run = Component->FindFunction(TEXT("RunTest"));
	if (!TestNotNull(TEXT("Event compiled to a function"), Run))
	{
		return false;
	}
	Component->ProcessEvent(Run, nullptr);

	const TArray<float>& Data = Component->GetCustomPrimitiveData().Data;
	if (!TestTrue(TEXT("Data written far enough"), Data.Num() > TintSlot + 3))
	{
		return false;
	}
	TestEqual(TEXT("Scalar landed in its slot"), Data[RoughnessSlot], 0.625f);
	TestEqual(TEXT("Tint R"), Data[TintSlot + 0], 0.1f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Tint G"), Data[TintSlot + 1], 0.2f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Tint B"), Data[TintSlot + 2], 0.3f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Tint A"), Data[TintSlot + 3], 0.4f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Array target reached the component"), Data[0], 0.5f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
