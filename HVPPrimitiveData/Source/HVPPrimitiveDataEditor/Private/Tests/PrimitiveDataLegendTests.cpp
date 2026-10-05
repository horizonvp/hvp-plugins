#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Self.h"
#include "K2Node_SetNamedPrimitiveData.h"
#include "K2Node_SetNamedPrimitiveDataMulti.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "PrimitiveDataLegend.h"
#include "PrimitiveDataLegendBinding.h"
#include "PrimitiveDataSetLibrary.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace PrimitiveDataLegendTests
{
	FPrimitiveDataLegendEntry& Add(UPrimitiveDataLegend* Legend, const TCHAR* Name, EPrimitiveDataParameterType Type)
	{
		FPrimitiveDataLegendEntry& Entry = Legend->Parameters.AddDefaulted_GetRef();
		Entry.Name = Name;
		Entry.Type = Type;
		return Entry;
	}

	int32 SlotOf(const UPrimitiveDataLegend* Legend, const TCHAR* Name)
	{
		const FPrimitiveDataLegendEntry* Entry = Legend->FindParameter(FName(Name));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrimitiveDataLegendSlotTest, "HVP.PrimitiveData.SlotAllocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPrimitiveDataLegendSlotTest::RunTest(const FString& Parameters)
{
	using namespace PrimitiveDataLegendTests;
	using EType = EPrimitiveDataParameterType;

	// Strong refs: nothing else holds these, and the engine is free to collect garbage between steps.
	const TStrongObjectPtr<UPrimitiveDataLegend> Owner(NewObject<UPrimitiveDataLegend>(GetTransientPackage()));
	UPrimitiveDataLegend* Legend = Owner.Get();

	Add(Legend, TEXT("A"), EType::Scalar);
	Add(Legend, TEXT("B"), EType::Vector);
	Add(Legend, TEXT("C"), EType::Scalar);
	Legend->PostEditChange();

	TestEqual(TEXT("A packs first"), SlotOf(Legend, TEXT("A")), 0);
	TestEqual(TEXT("B takes the next four"), SlotOf(Legend, TEXT("B")), 1);
	TestEqual(TEXT("C follows B"), SlotOf(Legend, TEXT("C")), 5);
	TestEqual(TEXT("Used floats"), Legend->GetUsedFloats(), 6);

	// The entry that CHANGED moves; its neighbours do not. A at 0 cannot grow to four floats there
	// (B owns 1-4), so A must relocate - B and C must stay exactly where they were.
	Legend->Parameters[0].Type = EType::Vector;
	Legend->PostEditChange();
	TestEqual(TEXT("B unmoved when A grows"), SlotOf(Legend, TEXT("B")), 1);
	TestEqual(TEXT("C unmoved when A grows"), SlotOf(Legend, TEXT("C")), 5);
	TestEqual(TEXT("A relocated to first free run of four"), SlotOf(Legend, TEXT("A")), 6);

	// Removing and reordering never move anyone.
	Legend->Parameters.RemoveAt(1); // B
	Legend->Parameters.Swap(0, 1);  // C before A
	Legend->PostEditChange();
	TestEqual(TEXT("C unmoved by removal and reorder"), SlotOf(Legend, TEXT("C")), 5);
	TestEqual(TEXT("A unmoved by removal and reorder"), SlotOf(Legend, TEXT("A")), 6);

	// A new vector takes the first free run - the gap B left.
	Add(Legend, TEXT("D"), EType::Vector);
	Legend->PostEditChange();
	TestEqual(TEXT("D fills the freed run"), SlotOf(Legend, TEXT("D")), 0);

	// A duplicated element keeps the original in place and moves the copy, with a fresh identity.
	const FGuid OriginalId = Legend->FindParameter(FName(TEXT("C")))->Id;
	FPrimitiveDataLegendEntry Copy = *Legend->FindParameter(FName(TEXT("C")));
	Copy.Name = TEXT("C2");
	Legend->Parameters.Add(Copy);
	Legend->PostEditChange();
	TestEqual(TEXT("Original keeps its slot"), SlotOf(Legend, TEXT("C")), 5);
	TestNotEqual(TEXT("Copy moved"), SlotOf(Legend, TEXT("C2")), 5);
	TestNotEqual(TEXT("Copy has its own identity"), Legend->FindParameter(FName(TEXT("C2")))->Id, OriginalId);

	// Nameless new entries get a name a dropdown can show.
	Legend->Parameters.AddDefaulted();
	Legend->PostEditChange();
	TestFalse(TEXT("New entry named"), Legend->Parameters.Last().Name.IsNone());

	// Overflow: fill well past 36 floats. Every entry that fits gets a slot; nothing overlaps.
	for (int32 i = 0; i < 12; ++i)
	{
		Add(Legend, *FString::Printf(TEXT("V%d"), i), EType::Vector);
	}
	Legend->PostEditChange();
	TestTrue(TEXT("Never over capacity"), Legend->GetUsedFloats() <= UPrimitiveDataLegend::GetCapacity());

	TBitArray<> Claimed(false, UPrimitiveDataLegend::GetCapacity());
	bool bOverlap = false;
	int32 Unslotted = 0;
	for (const FPrimitiveDataLegendEntry& Entry : Legend->Parameters)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrimitiveDataLegendBindingTest, "HVP.PrimitiveData.MaterialBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPrimitiveDataLegendBindingTest::RunTest(const FString& Parameters)
{
	using namespace PrimitiveDataLegendTests;
	using EType = EPrimitiveDataParameterType;

	// Strong refs: recompiling a material can let the garbage collector run, and nothing else holds
	// these. (Real bindings only ever touch saved assets, which stay loaded.)
	const TStrongObjectPtr<UPrimitiveDataLegend> LegendOwner(NewObject<UPrimitiveDataLegend>(GetTransientPackage()));
	UPrimitiveDataLegend* Legend = LegendOwner.Get();
	Add(Legend, TEXT("Roughness"), EType::Scalar);
	Add(Legend, TEXT("Tint"), EType::Vector);
	Add(Legend, TEXT("Glow"), EType::Scalar);
	Legend->bRequireAllParameters = true;
	Legend->PostEditChange();

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
	const FPrimitiveDataBindingResult Check = FPrimitiveDataLegendBinding::Sync(*Legend, Material, /*bApply*/ false);
	TestFalse(TEXT("Check does not write"), Roughness->bUseCustomPrimitiveData);
	TestEqual(TEXT("Check writes nothing"), Check.ParametersWritten, 0);

	const FPrimitiveDataBindingResult Result = FPrimitiveDataLegendBinding::Sync(*Legend, Material, /*bApply*/ true);

	TestTrue(TEXT("Roughness switched to CPD"), Roughness->bUseCustomPrimitiveData);
	TestEqual(TEXT("Roughness on its slot"), static_cast<int32>(Roughness->PrimitiveDataIndex), SlotOf(Legend, TEXT("Roughness")));
	TestTrue(TEXT("Tint switched to CPD"), Tint->bUseCustomPrimitiveData);
	TestEqual(TEXT("Tint on its slot"), static_cast<int32>(Tint->PrimitiveDataIndex), SlotOf(Legend, TEXT("Tint")));
	TestEqual(TEXT("Two parameters written"), Result.ParametersWritten, 2);

	TestFalse(TEXT("An ordinary parameter is left alone"), Ordinary->bUseCustomPrimitiveData);
	TestTrue(TEXT("A stray CPD parameter is NOT switched off"), Stray->bUseCustomPrimitiveData);

	// Exactly two errors: the stray ("only") and the missing Glow ("all", required).
	TestEqual(TEXT("Stray + missing reported as errors"), Result.Errors.Num(), 2);

	// Idempotent: a second sync writes nothing.
	const FPrimitiveDataBindingResult Again = FPrimitiveDataLegendBinding::Sync(*Legend, Material, /*bApply*/ true);
	TestEqual(TEXT("Second sync is a no-op"), Again.ParametersWritten, 0);

	// With "all" not required, the missing parameter drops to a warning.
	Legend->bRequireAllParameters = false;
	const FPrimitiveDataBindingResult Relaxed = FPrimitiveDataLegendBinding::Sync(*Legend, Material, /*bApply*/ false);
	TestEqual(TEXT("Only the stray remains an error"), Relaxed.Errors.Num(), 1);
	TestEqual(TEXT("Missing becomes a warning"), Relaxed.Warnings.Num(), 1);

	// A rename in the legend follows into the material.
	TMap<FName, FName> Renames;
	Renames.Add(TEXT("Tint"), TEXT("BaseTint"));
	Legend->Parameters[1].Name = TEXT("BaseTint");
	const FPrimitiveDataBindingResult Renamed = FPrimitiveDataLegendBinding::Sync(*Legend, Material, /*bApply*/ true, Renames);
	TestEqual(TEXT("Material parameter relabelled"), Tint->ParameterName, FName(TEXT("BaseTint")));
	TestEqual(TEXT("One rename counted"), Renamed.ParametersRenamed, 1);

	// A type mismatch is reported, never written.
	UMaterialExpressionVectorParameter* WrongType = AddParameter<UMaterialExpressionVectorParameter>(Material, TEXT("Roughness"));
	const FPrimitiveDataBindingResult Mismatch = FPrimitiveDataLegendBinding::Sync(*Legend, Material, /*bApply*/ true);
	TestFalse(TEXT("Vector named like a scalar entry is not switched"), WrongType->bUseCustomPrimitiveData);
	TestTrue(TEXT("Type mismatch reported"), Mismatch.Errors.ContainsByPredicate(
		[](const FText& Error) { return Error.ToString().Contains(TEXT("vector parameter here")); }));

	Material->ClearFlags(RF_Standalone | RF_Public);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSetNamedPrimitiveDataNodeTest, "HVP.PrimitiveData.NodeCompilesAndRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * End to end: a Blueprint using the node compiles, and running it writes the right floats into the
 * right slots. This is the only way to prove the expansion - a wrong intermediate pin name compiles
 * fine in C++ and only fails when a Blueprint does.
 */
bool FSetNamedPrimitiveDataNodeTest::RunTest(const FString& Parameters)
{
	using namespace PrimitiveDataLegendTests;
	using EType = EPrimitiveDataParameterType;

	const TStrongObjectPtr<UPrimitiveDataLegend> LegendOwner(NewObject<UPrimitiveDataLegend>(GetTransientPackage()));
	UPrimitiveDataLegend* Legend = LegendOwner.Get();
	Add(Legend, TEXT("Pad"), EType::Scalar);        // slot 0 - so nothing lands at 0 by accident
	Add(Legend, TEXT("Roughness"), EType::Scalar);  // slot 1
	Add(Legend, TEXT("Tint"), EType::Vector);       // slots 2-5
	Legend->PostEditChange();
	const int32 RoughnessSlot = SlotOf(Legend, TEXT("Roughness"));
	const int32 TintSlot = SlotOf(Legend, TEXT("Tint"));

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
		UK2Node_SetNamedPrimitiveData* Node = NewObject<UK2Node_SetNamedPrimitiveData>(Graph);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		Node->AllocateDefaultPins();
		return Node;
	};

	// Scalar. Choosing the legend auto-selects its first parameter; then pick Roughness.
	UK2Node_SetNamedPrimitiveData* SetScalar = AddSetNode();
	TestNull(TEXT("No Value pin before a parameter is chosen"), SetScalar->FindPin(UK2Node_SetNamedPrimitiveData::ValuePinName));
	Schema->TrySetDefaultObject(*SetScalar->FindPinChecked(UK2Node_SetNamedPrimitiveData::LegendPinName), Legend);
	TestEqual(TEXT("Choosing a legend selects its first parameter"),
		SetScalar->FindPinChecked(UK2Node_SetNamedPrimitiveData::ParameterPinName)->DefaultValue, FString(TEXT("Pad")));
	Schema->TrySetDefaultValue(*SetScalar->FindPinChecked(UK2Node_SetNamedPrimitiveData::ParameterPinName), TEXT("Roughness"));
	UEdGraphPin* ScalarValue = SetScalar->FindPin(UK2Node_SetNamedPrimitiveData::ValuePinName);
	if (!TestNotNull(TEXT("Scalar Value pin"), ScalarValue))
	{
		return false;
	}
	TestEqual(TEXT("Scalar Value pin is a float"), ScalarValue->PinType.PinCategory, UEdGraphSchema_K2::PC_Real);
	Schema->TrySetDefaultValue(*ScalarValue, TEXT("0.625"));

	// Vector. Switching the parameter morphs the Value pin to a Linear Color.
	UK2Node_SetNamedPrimitiveData* SetVector = AddSetNode();
	Schema->TrySetDefaultObject(*SetVector->FindPinChecked(UK2Node_SetNamedPrimitiveData::LegendPinName), Legend);
	Schema->TrySetDefaultValue(*SetVector->FindPinChecked(UK2Node_SetNamedPrimitiveData::ParameterPinName), TEXT("Tint"));
	UEdGraphPin* VectorValue = SetVector->FindPin(UK2Node_SetNamedPrimitiveData::ValuePinName);
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
	UK2Node_SetNamedPrimitiveData* SetViaArray = AddSetNode();
	Schema->TrySetDefaultObject(*SetViaArray->FindPinChecked(UK2Node_SetNamedPrimitiveData::LegendPinName), Legend);
	Schema->TrySetDefaultValue(*SetViaArray->FindPinChecked(UK2Node_SetNamedPrimitiveData::ParameterPinName), TEXT("Pad"));
	Schema->TrySetDefaultValue(*SetViaArray->FindPinChecked(UK2Node_SetNamedPrimitiveData::ValuePinName), TEXT("0.5"));

	UK2Node_MakeArray* MakeArray = NewObject<UK2Node_MakeArray>(Graph);
	Graph->AddNode(MakeArray, false, false);
	MakeArray->CreateNewGuid();
	MakeArray->AllocateDefaultPins();

	// Event -> SetScalar -> SetVector -> SetViaArray, Self into the first two Targets directly.
	bool bWired = true;
	bWired &= Schema->TryCreateConnection(Event->FindPinChecked(UEdGraphSchema_K2::PN_Then), SetScalar->GetExecPin());
	bWired &= Schema->TryCreateConnection(SetScalar->FindPinChecked(UEdGraphSchema_K2::PN_Then), SetVector->GetExecPin());
	bWired &= Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self),
		SetScalar->FindPinChecked(UK2Node_SetNamedPrimitiveData::TargetPinName));
	bWired &= Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self),
		SetVector->FindPinChecked(UK2Node_SetNamedPrimitiveData::TargetPinName));
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
		SetViaArray->FindPinChecked(UK2Node_SetNamedPrimitiveData::TargetPinName));
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


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSetNamedPrimitiveDataMultiNodeTest, "HVP.PrimitiveData.MultiNodeCompilesAndRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PrimitiveDataLegendTests
{
	template <typename T>
	T* Place(UEdGraph* Graph)
	{
		T* Node = NewObject<T>(Graph);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		Node->AllocateDefaultPins();
		return Node;
	}

	/** Whether any function of Class calls Function: its pointer appears in the compiled script. */
	bool Calls(const UClass* Class, const UFunction* Function)
	{
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const TArray<uint8>& Script = It->Script;
			for (int32 Offset = 0; Offset + int32(sizeof(Function)) <= Script.Num(); ++Offset)
			{
				if (FMemory::Memcmp(Script.GetData() + Offset, &Function, sizeof(Function)) == 0)
				{
					return true;
				}
			}
		}
		return false;
	}
}

/**
 * The multiple node, end to end: ticked parameters become pins in legend order; side-by-side ones
 * compile to the engine call alone, gapped ones to the masked write that leaves the floats in between
 * as they were - with Target unwired, wired to an array, and wired twice. Then a rename in the legend.
 */
bool FSetNamedPrimitiveDataMultiNodeTest::RunTest(const FString& Parameters)
{
	using namespace PrimitiveDataLegendTests;
	using EType = EPrimitiveDataParameterType;

	const TStrongObjectPtr<UPrimitiveDataLegend> LegendOwner(NewObject<UPrimitiveDataLegend>(GetTransientPackage()));
	UPrimitiveDataLegend* Legend = LegendOwner.Get();
	Add(Legend, TEXT("Pad"), EType::Scalar);        // 0
	Add(Legend, TEXT("Roughness"), EType::Scalar);  // 1
	Add(Legend, TEXT("Tint"), EType::Vector);       // 2-5
	Add(Legend, TEXT("Burst"), EType::Scalar);      // 6 - never set here: must survive every gapped write
	Add(Legend, TEXT("Glow"), EType::Scalar);       // 7
	Legend->PostEditChange();
	auto IdOf = [Legend](const TCHAR* Name) { return Legend->FindParameter(FName(Name))->Id; };
	const FGuid Pad = IdOf(TEXT("Pad"));
	const FGuid Roughness = IdOf(TEXT("Roughness"));
	const FGuid Tint = IdOf(TEXT("Tint"));
	const FGuid Glow = IdOf(TEXT("Glow"));
	TestEqual(TEXT("Glow at 7"), SlotOf(Legend, TEXT("Glow")), 7);

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	const UFunction* Masked = UPrimitiveDataSetLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UPrimitiveDataSetLibrary, SetCustomPrimitiveDataMasked));

	auto MakeBlueprint = [](const TCHAR* Name)
	{
		UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HVPPrimitiveDataTest/%s"), Name));
		return TStrongObjectPtr<UBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UStaticMeshComponent::StaticClass(), Package, Name, BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	};
	auto AddEvent = [](UEdGraph* Graph, const TCHAR* Name)
	{
		UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
		Event->CustomFunctionName = Name;
		Graph->AddNode(Event, false, false);
		Event->CreateNewGuid();
		Event->AllocateDefaultPins();
		return Event;
	};
	auto AddSet = [this, Schema, Legend](UEdGraph* Graph, std::initializer_list<TPair<FGuid, const TCHAR*>> Values)
	{
		UK2Node_SetNamedPrimitiveDataMulti* Node = Place<UK2Node_SetNamedPrimitiveDataMulti>(Graph);
		Schema->TrySetDefaultObject(*Node->FindPinChecked(UK2Node_SetNamedPrimitiveDataMulti::LegendPinName), Legend);
		for (const TPair<FGuid, const TCHAR*>& Value : Values)
		{
			Node->SetParameterSelected(Value.Key, true);
		}
		for (const TPair<FGuid, const TCHAR*>& Value : Values)
		{
			UEdGraphPin* Pin = Node->FindValuePin(Value.Key);
			if (TestNotNull(TEXT("Value pin for a ticked parameter"), Pin))
			{
				Schema->TrySetDefaultValue(*Pin, Value.Value);
			}
		}
		return Node;
	};
	auto Compile = [this](UBlueprint* Blueprint)
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			if (Message->GetSeverity() == EMessageSeverity::Error)
			{
				AddError(FString::Printf(TEXT("Compile %s: %s"), *Blueprint->GetName(), *Message->ToText().ToString()));
			}
		}
		return Results.NumErrors == 0 && Blueprint->GeneratedClass;
	};
	auto Then = [](UEdGraphNode* Node) { return Node->FindPinChecked(UEdGraphSchema_K2::PN_Then); };
	auto SelfOf = [](UEdGraphNode* Node) { return Node->FindPinChecked(UEdGraphSchema_K2::PN_Self); };

	// --- Side by side: Roughness + Tint, slots 1-5. Ticked out of order on purpose.
	const TStrongObjectPtr<UBlueprint> ContiguousOwner = MakeBlueprint(TEXT("BP_MultiContiguous"));
	UEdGraph* ContiguousGraph = FBlueprintEditorUtils::FindEventGraph(ContiguousOwner.Get());
	UK2Node_SetNamedPrimitiveDataMulti* Contiguous = AddSet(ContiguousGraph,
		{ { Tint, TEXT("(R=0.100000,G=0.200000,B=0.300000,A=0.400000)") }, { Roughness, TEXT("0.625") } });
	{
		const TArray<FNamedPrimitiveDataSelection>& Selection = Contiguous->GetSelection();
		TestTrue(TEXT("Pins in legend order, not ticking order"),
			Selection.Num() == 2 && Selection[0].Id == Roughness && Selection[1].Id == Tint);
		int32 First = 0;
		int32 Count = 0;
		uint64 Mask = 0;
		TestTrue(TEXT("Contiguous range resolves"), Contiguous->GetWriteRange(First, Count, Mask));
		TestTrue(TEXT("Slots 1-5, all owned"), First == 1 && Count == 5 && Mask == 0x1F);
		const UEdGraphPin* TintPin = Contiguous->FindValuePin(Tint);
		TestTrue(TEXT("Tint pin is a Linear Color"), TintPin && TintPin->PinType.PinSubCategoryObject == TBaseStructure<FLinearColor>::Get());
	}
	bool bWired = Schema->TryCreateConnection(Then(AddEvent(ContiguousGraph, TEXT("Run"))), Contiguous->GetExecPin());
	bWired &= Schema->TryCreateConnection(SelfOf(Place<UK2Node_Self>(ContiguousGraph)),
		Contiguous->FindPinChecked(UK2Node_SetNamedPrimitiveDataMulti::TargetPinName));
	TestTrue(TEXT("Contiguous graph wired"), bWired);

	// --- Gapped: three nodes setting a low slot and Glow, with Tint and Burst in between left alone.
	const TStrongObjectPtr<UBlueprint> GappedOwner = MakeBlueprint(TEXT("BP_MultiGapped"));
	UEdGraph* GappedGraph = FBlueprintEditorUtils::FindEventGraph(GappedOwner.Get());

	// Target unwired: self.
	UK2Node_SetNamedPrimitiveDataMulti* Unwired = AddSet(GappedGraph, { { Roughness, TEXT("0.25") }, { Glow, TEXT("0.75") } });
	{
		int32 First = 0;
		int32 Count = 0;
		uint64 Mask = 0;
		Unwired->GetWriteRange(First, Count, Mask);
		TestTrue(TEXT("Gapped range: slots 1-7, owning 1 and 7"), First == 1 && Count == 7 && Mask == ((1ull << 0) | (1ull << 6)));
	}
	bWired = Schema->TryCreateConnection(Then(AddEvent(GappedGraph, TEXT("RunUnwired"))), Unwired->GetExecPin());

	// Target from an array.
	UK2Node_SetNamedPrimitiveDataMulti* FromArray = AddSet(GappedGraph, { { Pad, TEXT("0.5") }, { Glow, TEXT("0.875") } });
	UK2Node_MakeArray* MakeArray = Place<UK2Node_MakeArray>(GappedGraph);
	bWired &= Schema->TryCreateConnection(Then(AddEvent(GappedGraph, TEXT("RunArray"))), FromArray->GetExecPin());
	bWired &= Schema->TryCreateConnection(SelfOf(Place<UK2Node_Self>(GappedGraph)), MakeArray->FindPinChecked(TEXT("[0]")));
	bWired &= Schema->TryCreateConnection(MakeArray->GetOutputPin(), FromArray->FindPinChecked(UK2Node_SetNamedPrimitiveDataMulti::TargetPinName));

	// Target wired twice.
	UK2Node_SetNamedPrimitiveDataMulti* TwoWires = AddSet(GappedGraph, { { Pad, TEXT("0.125") }, { Glow, TEXT("0.375") } });
	bWired &= Schema->TryCreateConnection(Then(AddEvent(GappedGraph, TEXT("RunTwoWires"))), TwoWires->GetExecPin());
	for (int32 Index = 0; Index < 2; ++Index)
	{
		bWired &= Schema->TryCreateConnection(SelfOf(Place<UK2Node_Self>(GappedGraph)),
			TwoWires->FindPinChecked(UK2Node_SetNamedPrimitiveDataMulti::TargetPinName));
	}
	TestTrue(TEXT("Gapped graph wired"), bWired);

	const bool bContiguousCompiled = Compile(ContiguousOwner.Get());
	const bool bGappedCompiled = Compile(GappedOwner.Get());
	if (!TestTrue(TEXT("Both compile"), bContiguousCompiled && bGappedCompiled))
	{
		return false;
	}
	TestFalse(TEXT("Side by side compiles to the engine call alone"), Calls(ContiguousOwner->GeneratedClass, Masked));
	TestTrue(TEXT("Gapped compiles to the masked write"), Calls(GappedOwner->GeneratedClass, Masked));

	// Run each event on a component pre-filled with 9s, and read back.
	auto Run = [this](UBlueprint* Blueprint, const TCHAR* Event)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(GetTransientPackage(), Blueprint->GeneratedClass);
		TArray<float> Sentinel;
		Sentinel.Init(9.f, 8);
		Component->SetCustomPrimitiveDataFloatArray(0, Sentinel);
		if (UFunction* Function = Component->FindFunction(Event))
		{
			Component->ProcessEvent(Function, nullptr);
		}
		else
		{
			AddError(FString::Printf(TEXT("No event %s"), Event));
		}
		TArray<float> Data = Component->GetCustomPrimitiveData().Data;
		Data.SetNumZeroed(8);
		return Data;
	};
	auto Expect = [this](const TCHAR* What, const TArray<float>& Data, std::initializer_list<float> Expected)
	{
		int32 Slot = 0;
		for (const float Value : Expected)
		{
			TestEqual(FString::Printf(TEXT("%s: slot %d"), What, Slot), Data[Slot], Value, KINDA_SMALL_NUMBER);
			++Slot;
		}
	};

	Expect(TEXT("Contiguous"), Run(ContiguousOwner.Get(), TEXT("Run")),     { 9.f, 0.625f, 0.1f, 0.2f, 0.3f, 0.4f, 9.f, 9.f });
	Expect(TEXT("Unwired"),    Run(GappedOwner.Get(), TEXT("RunUnwired")),  { 9.f, 0.25f, 9.f, 9.f, 9.f, 9.f, 9.f, 0.75f });
	Expect(TEXT("Array"),      Run(GappedOwner.Get(), TEXT("RunArray")),    { 0.5f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 0.875f });
	Expect(TEXT("Two wires"),  Run(GappedOwner.Get(), TEXT("RunTwoWires")), { 0.125f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 0.375f });

	// A rename in the legend: same pin, new label, value kept.
	Legend->Parameters.FindByPredicate([&Roughness](const FPrimitiveDataLegendEntry& Entry) { return Entry.Id == Roughness; })->Name = TEXT("Rough");
	Legend->PostEditChange();
	Contiguous->ReconstructNode();
	const UEdGraphPin* Renamed = Contiguous->FindValuePin(Roughness);
	if (TestNotNull(TEXT("Pin survives a rename"), Renamed))
	{
		TestEqual(TEXT("Pin shows the new name"), Renamed->PinFriendlyName.ToString(), FString(TEXT("Rough")));
		TestEqual(TEXT("Value kept across the rename"), Renamed->DefaultValue, FString(TEXT("0.625")));
	}

	// Unticking removes the pin.
	Contiguous->SetParameterSelected(Tint, false);
	TestNull(TEXT("Unticked parameter loses its pin"), Contiguous->FindValuePin(Tint));

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSetNamedPrimitiveDataDefaultsTest, "HVP.PrimitiveData.WriteDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Write Defaults on both nodes, through every path - the single node, the multiple node side by side
 * (engine call) and gapped (masked write): the values land in the saved defaults, so they survive the
 * runtime data being reset to them, which is what loading the level does. Floats in the gaps keep
 * their old defaults.
 */
bool FSetNamedPrimitiveDataDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace PrimitiveDataLegendTests;
	using EType = EPrimitiveDataParameterType;

	const TStrongObjectPtr<UPrimitiveDataLegend> LegendOwner(NewObject<UPrimitiveDataLegend>(GetTransientPackage()));
	UPrimitiveDataLegend* Legend = LegendOwner.Get();
	Add(Legend, TEXT("Pad"), EType::Scalar);        // 0
	Add(Legend, TEXT("Roughness"), EType::Scalar);  // 1
	Add(Legend, TEXT("Tint"), EType::Vector);       // 2-5
	Add(Legend, TEXT("Burst"), EType::Scalar);      // 6
	Add(Legend, TEXT("Glow"), EType::Scalar);       // 7
	Legend->PostEditChange();
	auto IdOf = [Legend](const TCHAR* Name) { return Legend->FindParameter(FName(Name))->Id; };

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UPackage* Package = CreatePackage(TEXT("/Temp/HVPPrimitiveDataTest/BP_WriteDefaults"));
	const TStrongObjectPtr<UBlueprint> BlueprintOwner(FKismetEditorUtilities::CreateBlueprint(
		UStaticMeshComponent::StaticClass(), Package, TEXT("BP_WriteDefaults"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	UBlueprint* Blueprint = BlueprintOwner.Get();
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);

	auto AddEvent = [Graph](const TCHAR* Name)
	{
		UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
		Event->CustomFunctionName = Name;
		Graph->AddNode(Event, false, false);
		Event->CreateNewGuid();
		Event->AllocateDefaultPins();
		return Event;
	};
	auto AddMulti = [this, Graph, Schema, Legend](std::initializer_list<TPair<FGuid, const TCHAR*>> Values)
	{
		UK2Node_SetNamedPrimitiveDataMulti* Node = Place<UK2Node_SetNamedPrimitiveDataMulti>(Graph);
		Node->bWriteDefaults = true;
		Schema->TrySetDefaultObject(*Node->FindPinChecked(UK2Node_SetNamedPrimitiveDataMulti::LegendPinName), Legend);
		for (const TPair<FGuid, const TCHAR*>& Value : Values)
		{
			Node->SetParameterSelected(Value.Key, true);
		}
		for (const TPair<FGuid, const TCHAR*>& Value : Values)
		{
			if (UEdGraphPin* Pin = Node->FindValuePin(Value.Key))
			{
				Schema->TrySetDefaultValue(*Pin, Value.Value);
			}
			else
			{
				AddError(TEXT("No value pin for a ticked parameter"));
			}
		}
		return Node;
	};
	auto Then = [](UEdGraphNode* Node) { return Node->FindPinChecked(UEdGraphSchema_K2::PN_Then); };

	// Single node, unwired Target (self).
	UK2Node_SetNamedPrimitiveData* Single = Place<UK2Node_SetNamedPrimitiveData>(Graph);
	Single->bWriteDefaults = true;
	Schema->TrySetDefaultObject(*Single->FindPinChecked(UK2Node_SetNamedPrimitiveData::LegendPinName), Legend);
	Schema->TrySetDefaultValue(*Single->FindPinChecked(UK2Node_SetNamedPrimitiveData::ParameterPinName), TEXT("Glow"));
	Schema->TrySetDefaultValue(*Single->FindPinChecked(UK2Node_SetNamedPrimitiveData::ValuePinName), TEXT("0.5"));
	TestTrue(TEXT("Single node title says defaults"), Single->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(TEXT("Default")));

	UK2Node_SetNamedPrimitiveDataMulti* Contiguous = AddMulti(
		{ { IdOf(TEXT("Roughness")), TEXT("0.625") }, { IdOf(TEXT("Tint")), TEXT("(R=0.100000,G=0.200000,B=0.300000,A=0.400000)") } });
	UK2Node_SetNamedPrimitiveDataMulti* Gapped = AddMulti({ { IdOf(TEXT("Pad")), TEXT("0.25") }, { IdOf(TEXT("Glow")), TEXT("0.75") } });

	bool bWired = Schema->TryCreateConnection(Then(AddEvent(TEXT("RunSingle"))), Single->GetExecPin());
	bWired &= Schema->TryCreateConnection(Then(AddEvent(TEXT("RunContiguous"))), Contiguous->GetExecPin());
	bWired &= Schema->TryCreateConnection(Then(AddEvent(TEXT("RunGapped"))), Gapped->GetExecPin());
	TestTrue(TEXT("Graph wired"), bWired);

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
	TestTrue(TEXT("Gapped defaults compile to the masked defaults write"), Calls(Blueprint->GeneratedClass,
		UPrimitiveDataSetLibrary::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UPrimitiveDataSetLibrary, SetDefaultCustomPrimitiveDataMasked))));
	TestFalse(TEXT("Nothing compiles to the runtime masked write"), Calls(Blueprint->GeneratedClass,
		UPrimitiveDataSetLibrary::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UPrimitiveDataSetLibrary, SetCustomPrimitiveDataMasked))));

	// Defaults pre-filled with 9s, runtime data then scribbled over with -1s (as a construction script's
	// runtime writes would be), the event run, and finally the runtime data reset to the defaults - what
	// a level load does. Whatever survives that came from the defaults.
	auto Run = [this, Blueprint](const TCHAR* Event)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(GetTransientPackage(), Blueprint->GeneratedClass);
		TArray<float> Fill;
		Fill.Init(9.f, 8);
		Component->SetDefaultCustomPrimitiveDataFloatArray(0, Fill);
		if (UFunction* Function = Component->FindFunction(Event))
		{
			Component->ProcessEvent(Function, nullptr);
		}
		else
		{
			AddError(FString::Printf(TEXT("No event %s"), Event));
		}
		Fill.Init(-1.f, 8);
		Component->SetCustomPrimitiveDataFloatArray(0, Fill);
		Component->ResetCustomPrimitiveData();
		TArray<float> Data = Component->GetCustomPrimitiveData().Data;
		Data.SetNumZeroed(8);
		return Data;
	};
	auto Expect = [this](const TCHAR* What, const TArray<float>& Data, std::initializer_list<float> Expected)
	{
		int32 Slot = 0;
		for (const float Value : Expected)
		{
			TestEqual(FString::Printf(TEXT("%s: slot %d"), What, Slot), Data[Slot], Value, KINDA_SMALL_NUMBER);
			++Slot;
		}
	};

	Expect(TEXT("Single"),     Run(TEXT("RunSingle")),     { 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 0.5f });
	Expect(TEXT("Contiguous"), Run(TEXT("RunContiguous")), { 9.f, 0.625f, 0.1f, 0.2f, 0.3f, 0.4f, 9.f, 9.f });
	Expect(TEXT("Gapped"),     Run(TEXT("RunGapped")),     { 0.25f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 0.75f });
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
