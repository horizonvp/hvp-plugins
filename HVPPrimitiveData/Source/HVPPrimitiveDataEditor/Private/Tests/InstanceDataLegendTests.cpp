#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/InstancedStaticMeshComponent.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "InstanceDataLegend.h"
#include "InstanceDataLegendBinding.h"
#include "InstanceDataSetLibrary.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Self.h"
#include "K2Node_SetNamedInstanceData.h"
#include "K2Node_SetNamedInstanceDataBatch.h"
#include "K2Node_SetNamedInstanceDataMulti.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// Its own namespace: the Primitive Data Legend tests define helpers of the same names, and both files can
// land in one unity translation unit.
namespace InstanceDataLegendTests
{
	using EType = EInstanceDataParameterType;

	FInstanceDataLegendEntry& Add(UInstanceDataLegend* Legend, const TCHAR* Name, EType Type)
	{
		FInstanceDataLegendEntry& Entry = Legend->Parameters.AddDefaulted_GetRef();
		Entry.Name = Name;
		Entry.Type = Type;
		return Entry;
	}

	int32 SlotOf(const UInstanceDataLegend* Legend, const TCHAR* Name)
	{
		const FInstanceDataLegendEntry* Entry = Legend->FindParameter(FName(Name));
		return Entry ? Entry->Slot : -2;
	}

	/** The engine does not export the per instance custom data expression classes; find them as the binding does. */
	UMaterialExpression* AddDataNode(UMaterial* Material, bool bVector, const TCHAR* Description, uint32 DataIndex)
	{
		UClass* Class = FindObject<UClass>(nullptr, bVector
			? TEXT("/Script/Engine.MaterialExpressionPerInstanceCustomData3Vector")
			: TEXT("/Script/Engine.MaterialExpressionPerInstanceCustomData"));
		UMaterialExpression* Expression = UMaterialEditingLibrary::CreateMaterialExpression(Material, Class);
		Expression->Desc = Description;
		if (bVector)
		{
			static_cast<UMaterialExpressionPerInstanceCustomData3Vector*>(Expression)->DataIndex = DataIndex;
		}
		else
		{
			static_cast<UMaterialExpressionPerInstanceCustomData*>(Expression)->DataIndex = DataIndex;
		}
		return Expression;
	}

	uint32 DataIndexOf(const UMaterialExpression* Expression)
	{
		return Expression->GetClass()->GetName().EndsWith(TEXT("3Vector"))
			? static_cast<const UMaterialExpressionPerInstanceCustomData3Vector*>(Expression)->DataIndex
			: static_cast<const UMaterialExpressionPerInstanceCustomData*>(Expression)->DataIndex;
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceDataLegendSlotTest, "HVP.PrimitiveData.Instance.SlotAllocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInstanceDataLegendSlotTest::RunTest(const FString& Parameters)
{
	using namespace InstanceDataLegendTests;

	const TStrongObjectPtr<UInstanceDataLegend> Owner(NewObject<UInstanceDataLegend>(GetTransientPackage()));
	UInstanceDataLegend* Legend = Owner.Get();

	Add(Legend, TEXT("A"), EType::Scalar);
	Add(Legend, TEXT("B"), EType::Vector);
	Add(Legend, TEXT("C"), EType::Scalar);
	Legend->PostEditChange();

	TestEqual(TEXT("A packs first"), SlotOf(Legend, TEXT("A")), 0);
	TestEqual(TEXT("B takes the next three"), SlotOf(Legend, TEXT("B")), 1);
	TestEqual(TEXT("C follows B"), SlotOf(Legend, TEXT("C")), 4);
	TestEqual(TEXT("Floats per instance"), Legend->FloatsPerInstance, 5);

	// Only the entry that changed moves: A cannot grow to three floats at 0 (B owns 1-3).
	Legend->Parameters[0].Type = EType::Vector;
	Legend->PostEditChange();
	TestEqual(TEXT("B unmoved when A grows"), SlotOf(Legend, TEXT("B")), 1);
	TestEqual(TEXT("C unmoved when A grows"), SlotOf(Legend, TEXT("C")), 4);
	TestEqual(TEXT("A relocated to the first free run of three"), SlotOf(Legend, TEXT("A")), 5);
	TestEqual(TEXT("Floats per instance follows the highest slot"), Legend->FloatsPerInstance, 8);

	// Removing never moves anyone; the gap is filled by the next entry that fits.
	Legend->Parameters.RemoveAt(1); // B
	Legend->PostEditChange();
	TestEqual(TEXT("C unmoved by removal"), SlotOf(Legend, TEXT("C")), 4);
	Add(Legend, TEXT("D"), EType::Vector);
	Legend->PostEditChange();
	TestEqual(TEXT("D fills the freed run"), SlotOf(Legend, TEXT("D")), 0);

	// Overflow: a legend lays out at most its capacity, never overlapping, and leaves the rest unslotted.
	for (int32 i = 0; i < 30; ++i)
	{
		Add(Legend, *FString::Printf(TEXT("V%d"), i), EType::Vector);
	}
	Legend->PostEditChange();
	TestTrue(TEXT("Never over capacity"), Legend->FloatsPerInstance <= UInstanceDataLegend::GetCapacity());
	TBitArray<> Claimed(false, UInstanceDataLegend::GetCapacity());
	bool bOverlap = false;
	int32 Unslotted = 0;
	for (const FInstanceDataLegendEntry& Entry : Legend->Parameters)
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
	TestTrue(TEXT("Overflowing entries are left unslotted"), Unslotted > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceDataLegendBindingTest, "HVP.PrimitiveData.Instance.MaterialBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInstanceDataLegendBindingTest::RunTest(const FString& Parameters)
{
	using namespace InstanceDataLegendTests;

	const TStrongObjectPtr<UInstanceDataLegend> LegendOwner(NewObject<UInstanceDataLegend>(GetTransientPackage()));
	UInstanceDataLegend* Legend = LegendOwner.Get();
	Add(Legend, TEXT("Wind"), EType::Scalar);   // 0
	Add(Legend, TEXT("Tint"), EType::Vector);   // 1-3
	Add(Legend, TEXT("Glow"), EType::Scalar);   // 4 - no node for it: missing
	Legend->bRequireAllParameters = true;
	Legend->PostEditChange();

	UPackage* Package = CreatePackage(TEXT("/Temp/HVPPrimitiveDataTest/M_InstanceBindingTest"));
	const TStrongObjectPtr<UMaterial> MaterialOwner(
		NewObject<UMaterial>(Package, TEXT("M_InstanceBindingTest"), RF_Public | RF_Standalone | RF_Transactional));
	UMaterial* Material = MaterialOwner.Get();
	UMaterialExpression* Wind = AddDataNode(Material, false, TEXT("Wind"), 7);
	UMaterialExpression* WindAgain = AddDataNode(Material, false, TEXT(" Wind "), 9); // same parameter read twice, padded
	UMaterialExpression* Tint = AddDataNode(Material, true, TEXT("Tint"), 0);
	UMaterialExpression* Unnamed = AddDataNode(Material, false, TEXT(""), 12);
	UMaterialExpression* Stray = AddDataNode(Material, false, TEXT("Leftover"), 13);

	const FPrimitiveDataBindingResult Check = FInstanceDataLegendBinding::Sync(*Legend, Material, /*bApply*/ false);
	TestEqual(TEXT("Check does not write"), DataIndexOf(Wind), 7u);
	TestEqual(TEXT("Check writes nothing"), Check.ParametersWritten, 0);

	const FPrimitiveDataBindingResult Result = FInstanceDataLegendBinding::Sync(*Legend, Material, /*bApply*/ true);
	TestEqual(TEXT("Wind on its slot"), static_cast<int32>(DataIndexOf(Wind)), SlotOf(Legend, TEXT("Wind")));
	TestEqual(TEXT("Every node with the name gets the slot"), static_cast<int32>(DataIndexOf(WindAgain)), SlotOf(Legend, TEXT("Wind")));
	TestEqual(TEXT("Tint on its slot"), static_cast<int32>(DataIndexOf(Tint)), SlotOf(Legend, TEXT("Tint")));
	TestEqual(TEXT("Three nodes written"), Result.ParametersWritten, 3);
	TestEqual(TEXT("An unnamed node is left alone"), DataIndexOf(Unnamed), 12u);
	TestEqual(TEXT("A stray node is left alone"), DataIndexOf(Stray), 13u);

	// Unnamed, stray, and Glow missing (required).
	TestEqual(TEXT("Unnamed + stray + missing reported as errors"), Result.Errors.Num(), 3);

	const FPrimitiveDataBindingResult Again = FInstanceDataLegendBinding::Sync(*Legend, Material, /*bApply*/ true);
	TestEqual(TEXT("Second sync is a no-op"), Again.ParametersWritten, 0);

	Legend->bRequireAllParameters = false;
	const FPrimitiveDataBindingResult Relaxed = FInstanceDataLegendBinding::Sync(*Legend, Material, /*bApply*/ false);
	TestEqual(TEXT("Missing becomes a warning"), Relaxed.Warnings.Num(), 1);
	TestEqual(TEXT("Unnamed and stray stay errors"), Relaxed.Errors.Num(), 2);

	// A rename in the legend relabels the node's Description.
	TMap<FName, FName> Renames;
	Renames.Add(TEXT("Tint"), TEXT("BaseTint"));
	Legend->Parameters[1].Name = TEXT("BaseTint");
	const FPrimitiveDataBindingResult Renamed = FInstanceDataLegendBinding::Sync(*Legend, Material, /*bApply*/ true, Renames);
	TestEqual(TEXT("Node description relabelled"), Tint->Desc, FString(TEXT("BaseTint")));
	TestEqual(TEXT("One rename counted"), Renamed.ParametersRenamed, 1);

	// A type mismatch is reported, never written.
	UMaterialExpression* WrongType = AddDataNode(Material, true, TEXT("Wind"), 20);
	const FPrimitiveDataBindingResult Mismatch = FInstanceDataLegendBinding::Sync(*Legend, Material, /*bApply*/ true);
	TestEqual(TEXT("Vector node described like a scalar entry is not rewritten"), DataIndexOf(WrongType), 20u);
	TestTrue(TEXT("Type mismatch reported"), Mismatch.Errors.ContainsByPredicate(
		[](const FText& Error) { return Error.ToString().Contains(TEXT("PerInstanceCustomData3Vector node here")); }));

	Material->ClearFlags(RF_Standalone | RF_Public);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSetNamedInstanceDataNodesTest, "HVP.PrimitiveData.Instance.NodesCompileAndRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Both instance nodes end to end: a Blueprint of an instanced static mesh compiles, and running it writes
 * the right floats of the right instance. The single node's scalar and vector (three engine calls, or the
 * plugin's function when Target is an array), the multiple node gapped across a vector and a parameter it
 * leaves alone, Target unwired and from an array, and a mesh with too few floats per instance.
 */
bool FSetNamedInstanceDataNodesTest::RunTest(const FString& Parameters)
{
	using namespace InstanceDataLegendTests;

	const TStrongObjectPtr<UInstanceDataLegend> LegendOwner(NewObject<UInstanceDataLegend>(GetTransientPackage()));
	UInstanceDataLegend* Legend = LegendOwner.Get();
	Add(Legend, TEXT("Pad"), EType::Scalar);    // 0
	Add(Legend, TEXT("Wind"), EType::Scalar);   // 1
	Add(Legend, TEXT("Tint"), EType::Vector);   // 2-4
	Add(Legend, TEXT("Burst"), EType::Scalar);  // 5 - never set: must survive every multiple write
	Add(Legend, TEXT("Glow"), EType::Scalar);   // 6
	Legend->PostEditChange();
	TestEqual(TEXT("Glow at 6"), SlotOf(Legend, TEXT("Glow")), 6);
	auto IdOf = [Legend](const TCHAR* Name) { return Legend->FindParameter(FName(Name))->Id; };

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UPackage* Package = CreatePackage(TEXT("/Temp/HVPPrimitiveDataTest/BP_InstanceNodes"));
	const TStrongObjectPtr<UBlueprint> BlueprintOwner(FKismetEditorUtilities::CreateBlueprint(
		UInstancedStaticMeshComponent::StaticClass(), Package, TEXT("BP_InstanceNodes"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	UBlueprint* Blueprint = BlueprintOwner.Get();
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("Event graph"), Graph))
	{
		return false;
	}

	auto AddEvent = [Graph](const TCHAR* Name)
	{
		UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
		Event->CustomFunctionName = Name;
		Graph->AddNode(Event, false, false);
		Event->CreateNewGuid();
		Event->AllocateDefaultPins();
		return Event;
	};
	auto Then = [](UEdGraphNode* Node) { return Node->FindPinChecked(UEdGraphSchema_K2::PN_Then); };
	auto AddSingle = [Graph, Schema, Legend](const TCHAR* Parameter, const TCHAR* Instance, const TCHAR* Value)
	{
		UK2Node_SetNamedInstanceData* Node = Place<UK2Node_SetNamedInstanceData>(Graph);
		Schema->TrySetDefaultObject(*Node->FindPinChecked(UK2Node_SetNamedInstanceData::LegendPinName), Legend);
		Schema->TrySetDefaultValue(*Node->FindPinChecked(UK2Node_SetNamedInstanceData::ParameterPinName), Parameter);
		Schema->TrySetDefaultValue(*Node->FindPinChecked(UK2Node_SetNamedInstanceData::InstanceIndexPinName), Instance);
		if (UEdGraphPin* ValuePin = Node->FindPin(UK2Node_SetNamedInstanceData::ValuePinName))
		{
			Schema->TrySetDefaultValue(*ValuePin, Value);
		}
		return Node;
	};
	auto AddMulti = [this, Graph, Schema, Legend](const TCHAR* Instance, std::initializer_list<TPair<FGuid, const TCHAR*>> Values)
	{
		UK2Node_SetNamedInstanceDataMulti* Node = Place<UK2Node_SetNamedInstanceDataMulti>(Graph);
		Schema->TrySetDefaultObject(*Node->FindPinChecked(UK2Node_SetNamedInstanceDataMulti::LegendPinName), Legend);
		Schema->TrySetDefaultValue(*Node->FindPinChecked(UK2Node_SetNamedInstanceDataMulti::InstanceIndexPinName), Instance);
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

	// Single: a scalar then a vector on instance 1, Target unwired (self).
	UK2Node_SetNamedInstanceData* Scalar = AddSingle(TEXT("Wind"), TEXT("1"), TEXT("0.625"));
	UK2Node_SetNamedInstanceData* Vector = AddSingle(TEXT("Tint"), TEXT("1"), TEXT("(R=0.100000,G=0.200000,B=0.300000,A=0.400000)"));
	{
		const UEdGraphPin* VectorValue = Vector->FindPin(UK2Node_SetNamedInstanceData::ValuePinName);
		TestTrue(TEXT("Vector Value pin is a Linear Color"),
			VectorValue && VectorValue->PinType.PinSubCategoryObject == TBaseStructure<FLinearColor>::Get());
		// The morphed Value pin sits above the advanced Mark Render State Dirty pin, not after it.
		const int32 ValueIndex = Vector->Pins.IndexOfByPredicate([](const UEdGraphPin* Pin) { return Pin->PinName == UK2Node_SetNamedInstanceData::ValuePinName; });
		const int32 DirtyIndex = Vector->Pins.IndexOfByPredicate([](const UEdGraphPin* Pin) { return Pin->PinName == UK2Node_SetNamedInstanceData::MarkRenderStateDirtyPinName; });
		TestTrue(TEXT("Value pin before the advanced pin"), ValueIndex != INDEX_NONE && ValueIndex < DirtyIndex);
	}
	bool bWired = Schema->TryCreateConnection(Then(AddEvent(TEXT("RunSingle"))), Scalar->GetExecPin());
	bWired &= Schema->TryCreateConnection(Then(Scalar), Vector->GetExecPin());

	// Single, Target from an array: a scalar then a vector on instance 0. The engine call cannot loop over
	// targets, so these take the plugin's function instead.
	auto WireArrayTarget = [Graph, Schema](UEdGraphPin* TargetPin)
	{
		UK2Node_MakeArray* MakeArray = Place<UK2Node_MakeArray>(Graph);
		bool bOk = Schema->TryCreateConnection(Place<UK2Node_Self>(Graph)->FindPinChecked(UEdGraphSchema_K2::PN_Self), MakeArray->FindPinChecked(TEXT("[0]")));
		bOk &= Schema->TryCreateConnection(MakeArray->GetOutputPin(), TargetPin);
		return bOk;
	};
	UK2Node_SetNamedInstanceData* FromArray = AddSingle(TEXT("Pad"), TEXT("0"), TEXT("0.5"));
	UK2Node_SetNamedInstanceData* VectorFromArray = AddSingle(TEXT("Tint"), TEXT("0"), TEXT("(R=0.400000,G=0.300000,B=0.200000,A=1.000000)"));
	bWired &= Schema->TryCreateConnection(Then(AddEvent(TEXT("RunArray"))), FromArray->GetExecPin());
	bWired &= Schema->TryCreateConnection(Then(FromArray), VectorFromArray->GetExecPin());
	bWired &= WireArrayTarget(FromArray->FindPinChecked(UK2Node_SetNamedInstanceData::TargetPinName));
	bWired &= WireArrayTarget(VectorFromArray->FindPinChecked(UK2Node_SetNamedInstanceData::TargetPinName));

	// Multiple: Wind, Tint and Glow on instance 1 - Burst between them must be kept. Ticked out of order.
	UK2Node_SetNamedInstanceDataMulti* Multi = AddMulti(TEXT("1"), {
		{ IdOf(TEXT("Glow")), TEXT("0.75") },
		{ IdOf(TEXT("Wind")), TEXT("0.25") },
		{ IdOf(TEXT("Tint")), TEXT("(R=0.500000,G=0.600000,B=0.700000,A=1.000000)") } });
	{
		const TArray<FNamedPrimitiveDataSelection>& Selection = Multi->GetSelection();
		TestTrue(TEXT("Pins in legend order"), Selection.Num() == 3
			&& Selection[0].Id == IdOf(TEXT("Wind")) && Selection[1].Id == IdOf(TEXT("Tint")) && Selection[2].Id == IdOf(TEXT("Glow")));
		int32 First = 0;
		int32 Count = 0;
		uint64 Mask = 0;
		TestTrue(TEXT("Range resolves"), Multi->GetWriteRange(First, Count, Mask));
		TestTrue(TEXT("Slots 1-6, owning all but Burst"), First == 1 && Count == 6 && Mask == 0b101111);
	}
	bWired &= Schema->TryCreateConnection(Then(AddEvent(TEXT("RunMulti"))), Multi->GetExecPin());

	// Multiple, Target from an array, instance 0.
	UK2Node_SetNamedInstanceDataMulti* MultiArray = AddMulti(TEXT("0"), { { IdOf(TEXT("Pad")), TEXT("0.125") }, { IdOf(TEXT("Glow")), TEXT("0.375") } });
	bWired &= Schema->TryCreateConnection(Then(AddEvent(TEXT("RunMultiArray"))), MultiArray->GetExecPin());
	bWired &= WireArrayTarget(MultiArray->FindPinChecked(UK2Node_SetNamedInstanceDataMulti::TargetPinName));
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
	TestTrue(TEXT("The multiple node compiles to the masked write"), Calls(Blueprint->GeneratedClass,
		UInstanceDataSetLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UInstanceDataSetLibrary, SetInstanceCustomDataMasked))));

	// Run each event on a fresh mesh with two instances, every float pre-filled with 9, and read back.
	constexpr int32 Floats = 8;
	auto Run = [this, Blueprint](const TCHAR* Event, int32 NumFloats)
	{
		UInstancedStaticMeshComponent* Mesh = NewObject<UInstancedStaticMeshComponent>(GetTransientPackage(), Blueprint->GeneratedClass);
		Mesh->SetNumCustomDataFloats(NumFloats);
		TArray<float> Fill;
		Fill.Init(9.f, NumFloats);
		for (int32 Instance = 0; Instance < 2; ++Instance)
		{
			Mesh->AddInstance(FTransform::Identity);
			Mesh->SetCustomData(Instance, TArrayView<const float>(Fill));
		}
		if (UFunction* Function = Mesh->FindFunction(Event))
		{
			Mesh->ProcessEvent(Function, nullptr);
		}
		else
		{
			AddError(FString::Printf(TEXT("No event %s"), Event));
		}
		return Mesh->PerInstanceSMCustomData;
	};
	auto Expect = [this](const TCHAR* What, const TArray<float>& Data, int32 Instance, int32 NumFloats, std::initializer_list<float> Expected)
	{
		int32 Slot = 0;
		for (const float Value : Expected)
		{
			const int32 Index = Instance * NumFloats + Slot;
			TestTrue(FString::Printf(TEXT("%s: instance %d slot %d exists"), What, Instance, Slot), Data.IsValidIndex(Index));
			if (Data.IsValidIndex(Index))
			{
				TestEqual(FString::Printf(TEXT("%s: instance %d slot %d"), What, Instance, Slot), Data[Index], Value, KINDA_SMALL_NUMBER);
			}
			++Slot;
		}
	};
	const std::initializer_list<float> Untouched = { 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f };

	const TArray<float> Single = Run(TEXT("RunSingle"), Floats);
	Expect(TEXT("Single"), Single, 0, Floats, Untouched);
	Expect(TEXT("Single"), Single, 1, Floats, { 9.f, 0.625f, 0.1f, 0.2f, 0.3f, 9.f, 9.f, 9.f });

	const TArray<float> Array = Run(TEXT("RunArray"), Floats);
	Expect(TEXT("Array"), Array, 0, Floats, { 0.5f, 9.f, 0.4f, 0.3f, 0.2f, 9.f, 9.f, 9.f });
	Expect(TEXT("Array"), Array, 1, Floats, Untouched);

	const TArray<float> Gapped = Run(TEXT("RunMulti"), Floats);
	Expect(TEXT("Multiple"), Gapped, 0, Floats, Untouched);
	Expect(TEXT("Multiple"), Gapped, 1, Floats, { 9.f, 0.25f, 0.5f, 0.6f, 0.7f, 9.f, 0.75f, 9.f });

	const TArray<float> MultiFromArray = Run(TEXT("RunMultiArray"), Floats);
	Expect(TEXT("Multiple array"), MultiFromArray, 0, Floats, { 0.125f, 9.f, 9.f, 9.f, 9.f, 9.f, 0.375f, 9.f });
	Expect(TEXT("Multiple array"), MultiFromArray, 1, Floats, Untouched);

	// Too few floats per instance: what fits is written, the rest dropped, and it says so once.
	AddExpectedMessage(TEXT("custom data floats per instance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	const TArray<float> Short = Run(TEXT("RunMulti"), 3);
	Expect(TEXT("Short"), Short, 1, 3, { 9.f, 0.25f, 0.5f });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSetNamedInstanceDataBatchTest, "HVP.PrimitiveData.Instance.BatchCompilesAndRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The batch node end to end, on a mesh with four instances: uniform values over consecutive indices (the
 * one-block ranged write), and over indices out of order with per instance arrays - floats and colours,
 * one array shorter than the index list - and Target from an array.
 */
bool FSetNamedInstanceDataBatchTest::RunTest(const FString& Parameters)
{
	using namespace InstanceDataLegendTests;

	const TStrongObjectPtr<UInstanceDataLegend> LegendOwner(NewObject<UInstanceDataLegend>(GetTransientPackage()));
	UInstanceDataLegend* Legend = LegendOwner.Get();
	Add(Legend, TEXT("Pad"), EType::Scalar);    // 0
	Add(Legend, TEXT("Wind"), EType::Scalar);   // 1
	Add(Legend, TEXT("Tint"), EType::Vector);   // 2-4
	Add(Legend, TEXT("Burst"), EType::Scalar);  // 5 - never set
	Add(Legend, TEXT("Glow"), EType::Scalar);   // 6
	Legend->PostEditChange();
	auto IdOf = [Legend](const TCHAR* Name) { return Legend->FindParameter(FName(Name))->Id; };

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UPackage* Package = CreatePackage(TEXT("/Temp/HVPPrimitiveDataTest/BP_InstanceBatch"));
	const TStrongObjectPtr<UBlueprint> BlueprintOwner(FKismetEditorUtilities::CreateBlueprint(
		UInstancedStaticMeshComponent::StaticClass(), Package, TEXT("BP_InstanceBatch"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	UBlueprint* Blueprint = BlueprintOwner.Get();
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("Event graph"), Graph))
	{
		return false;
	}

	auto AddEvent = [Graph](const TCHAR* Name)
	{
		UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
		Event->CustomFunctionName = Name;
		Graph->AddNode(Event, false, false);
		Event->CreateNewGuid();
		Event->AllocateDefaultPins();
		return Event;
	};
	// A literal array into Pin: a Make Array typed by the connection, one element per value.
	auto Literal = [Graph, Schema](UEdGraphPin* Pin, std::initializer_list<const TCHAR*> Values)
	{
		UK2Node_MakeArray* MakeArray = Place<UK2Node_MakeArray>(Graph);
		bool bOk = Schema->TryCreateConnection(MakeArray->GetOutputPin(), Pin);
		for (int32 Index = 1; Index < int32(Values.size()); ++Index)
		{
			MakeArray->AddInputPin();
		}
		int32 Index = 0;
		for (const TCHAR* Value : Values)
		{
			Schema->TrySetDefaultValue(*MakeArray->FindPinChecked(MakeArray->GetPinName(Index++)), Value);
		}
		return bOk;
	};
	auto AddBatch = [Graph, Schema, Legend]()
	{
		UK2Node_SetNamedInstanceDataBatch* Node = Place<UK2Node_SetNamedInstanceDataBatch>(Graph);
		Schema->TrySetDefaultObject(*Node->FindPinChecked(UK2Node_SetNamedInstanceDataBatch::LegendPinName), Legend);
		return Node;
	};
	auto Then = [](UEdGraphNode* Node) { return Node->FindPinChecked(UEdGraphSchema_K2::PN_Then); };

	// Uniform over instances 1 and 2 (consecutive): Wind and Tint, Target unwired (self).
	UK2Node_SetNamedInstanceDataBatch* Uniform = AddBatch();
	Uniform->SetParameterSelected(IdOf(TEXT("Wind")), true);
	Uniform->SetParameterSelected(IdOf(TEXT("Tint")), true);
	Schema->TrySetDefaultValue(*Uniform->FindValuePin(IdOf(TEXT("Wind"))), TEXT("0.25"));
	Schema->TrySetDefaultValue(*Uniform->FindValuePin(IdOf(TEXT("Tint"))), TEXT("(R=0.500000,G=0.600000,B=0.700000,A=1.000000)"));
	bool bWired = Literal(Uniform->FindPinChecked(UK2Node_SetNamedInstanceDataBatch::InstanceIndicesPinName), { TEXT("1"), TEXT("2") });
	bWired &= Schema->TryCreateConnection(Then(AddEvent(TEXT("RunUniform"))), Uniform->GetExecPin());

	// Instances 3 then 0: Wind uniform, Glow and Tint per instance - Tint's array covers only the first.
	UK2Node_SetNamedInstanceDataBatch* PerInstance = AddBatch();
	PerInstance->SetParameterSelected(IdOf(TEXT("Glow")), true);
	PerInstance->SetParameterSelected(IdOf(TEXT("Wind")), true);
	PerInstance->SetParameterSelected(IdOf(TEXT("Tint")), true);
	PerInstance->SetParameterPerInstance(IdOf(TEXT("Glow")), true);
	PerInstance->SetParameterPerInstance(IdOf(TEXT("Tint")), true);
	{
		const UEdGraphPin* GlowPin = PerInstance->FindValuePin(IdOf(TEXT("Glow")));
		TestTrue(TEXT("Per instance pin is an array"), GlowPin && GlowPin->PinType.IsArray());
		const UEdGraphPin* WindPin = PerInstance->FindValuePin(IdOf(TEXT("Wind")));
		TestTrue(TEXT("Uniform pin is a single value"), WindPin && !WindPin->PinType.IsArray());
	}
	Schema->TrySetDefaultValue(*PerInstance->FindValuePin(IdOf(TEXT("Wind"))), TEXT("0.125"));
	bWired &= Literal(PerInstance->FindValuePin(IdOf(TEXT("Glow"))), { TEXT("0.1"), TEXT("0.2") });
	bWired &= Literal(PerInstance->FindValuePin(IdOf(TEXT("Tint"))), { TEXT("(R=0.300000,G=0.400000,B=0.500000,A=1.000000)") });
	bWired &= Literal(PerInstance->FindPinChecked(UK2Node_SetNamedInstanceDataBatch::InstanceIndicesPinName), { TEXT("3"), TEXT("0") });
	{
		UK2Node_MakeArray* Meshes = Place<UK2Node_MakeArray>(Graph);
		bWired &= Schema->TryCreateConnection(Place<UK2Node_Self>(Graph)->FindPinChecked(UEdGraphSchema_K2::PN_Self), Meshes->FindPinChecked(TEXT("[0]")));
		bWired &= Schema->TryCreateConnection(Meshes->GetOutputPin(), PerInstance->FindPinChecked(UK2Node_SetNamedInstanceDataBatch::TargetPinName));
	}
	bWired &= Schema->TryCreateConnection(Then(AddEvent(TEXT("RunPerInstance"))), PerInstance->GetExecPin());
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
	TestTrue(TEXT("Compiles to the batch write"), Calls(Blueprint->GeneratedClass,
		UInstanceDataSetLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UInstanceDataSetLibrary, SetInstanceCustomDataBatch))));

	constexpr int32 Floats = 8;
	auto Run = [this, Blueprint](const TCHAR* Event)
	{
		UInstancedStaticMeshComponent* Mesh = NewObject<UInstancedStaticMeshComponent>(GetTransientPackage(), Blueprint->GeneratedClass);
		Mesh->SetNumCustomDataFloats(Floats);
		TArray<float> Fill;
		Fill.Init(9.f, Floats);
		for (int32 Instance = 0; Instance < 4; ++Instance)
		{
			Mesh->AddInstance(FTransform::Identity);
			Mesh->SetCustomData(Instance, TArrayView<const float>(Fill));
		}
		if (UFunction* Function = Mesh->FindFunction(Event))
		{
			Mesh->ProcessEvent(Function, nullptr);
		}
		else
		{
			AddError(FString::Printf(TEXT("No event %s"), Event));
		}
		return Mesh->PerInstanceSMCustomData;
	};
	auto Expect = [this](const TCHAR* What, const TArray<float>& Data, int32 Instance, std::initializer_list<float> Expected)
	{
		int32 Slot = 0;
		for (const float Value : Expected)
		{
			const int32 Index = Instance * Floats + Slot;
			if (TestTrue(FString::Printf(TEXT("%s: instance %d slot %d exists"), What, Instance, Slot), Data.IsValidIndex(Index)))
			{
				TestEqual(FString::Printf(TEXT("%s: instance %d slot %d"), What, Instance, Slot), Data[Index], Value, KINDA_SMALL_NUMBER);
			}
			++Slot;
		}
	};
	const std::initializer_list<float> Untouched = { 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f };

	const TArray<float> UniformData = Run(TEXT("RunUniform"));
	Expect(TEXT("Uniform"), UniformData, 0, Untouched);
	Expect(TEXT("Uniform"), UniformData, 1, { 9.f, 0.25f, 0.5f, 0.6f, 0.7f, 9.f, 9.f, 9.f });
	Expect(TEXT("Uniform"), UniformData, 2, { 9.f, 0.25f, 0.5f, 0.6f, 0.7f, 9.f, 9.f, 9.f });
	Expect(TEXT("Uniform"), UniformData, 3, Untouched);

	const TArray<float> PerInstanceData = Run(TEXT("RunPerInstance"));
	Expect(TEXT("Per instance"), PerInstanceData, 3, { 9.f, 0.125f, 0.3f, 0.4f, 0.5f, 9.f, 0.1f, 9.f });
	Expect(TEXT("Per instance"), PerInstanceData, 0, { 9.f, 0.125f, 9.f, 9.f, 9.f, 9.f, 0.2f, 9.f });
	Expect(TEXT("Per instance"), PerInstanceData, 1, Untouched);
	Expect(TEXT("Per instance"), PerInstanceData, 2, Untouched);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSetNamedInstanceDataBatchTimingTest, "HVP.PrimitiveData.Instance.BatchTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * What the batch node is for, measured: the same two parameters set on 10,000 instances by a Blueprint
 * ForLoop around Set Named Instance Data (Multiple), and by one Set Named Instance Data (Batch) - uniform,
 * and with one parameter per instance. Reports the times; checks only that all three agree, as timings
 * depend on the machine.
 */
bool FSetNamedInstanceDataBatchTimingTest::RunTest(const FString& Parameters)
{
	using namespace InstanceDataLegendTests;

	constexpr int32 NumInstances = 10000;
	constexpr int32 Floats = 8;

	const TStrongObjectPtr<UInstanceDataLegend> LegendOwner(NewObject<UInstanceDataLegend>(GetTransientPackage()));
	UInstanceDataLegend* Legend = LegendOwner.Get();
	Add(Legend, TEXT("Wind"), EType::Scalar);   // 0
	Add(Legend, TEXT("Tint"), EType::Vector);   // 1-3, never set: kept by every write
	Add(Legend, TEXT("Glow"), EType::Scalar);   // 4
	Legend->PostEditChange();
	const FGuid Wind = Legend->FindParameter(FName(TEXT("Wind")))->Id;
	const FGuid Glow = Legend->FindParameter(FName(TEXT("Glow")))->Id;

	UBlueprint* StandardMacros = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
	const TObjectPtr<UEdGraph>* ForLoopGraph = StandardMacros
		? StandardMacros->MacroGraphs.FindByPredicate([](const UEdGraph* Graph) { return Graph->GetFName() == TEXT("ForLoop"); })
		: nullptr;
	if (!TestNotNull(TEXT("ForLoop macro"), ForLoopGraph))
	{
		return false;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UPackage* Package = CreatePackage(TEXT("/Temp/HVPPrimitiveDataTest/BP_InstanceBatchTiming"));
	const TStrongObjectPtr<UBlueprint> BlueprintOwner(FKismetEditorUtilities::CreateBlueprint(
		UInstancedStaticMeshComponent::StaticClass(), Package, TEXT("BP_InstanceBatchTiming"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	UBlueprint* Blueprint = BlueprintOwner.Get();
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);

	auto AddEvent = [Graph](const TCHAR* Name, std::initializer_list<TPair<const TCHAR*, FEdGraphPinType>> Inputs)
	{
		UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
		Event->CustomFunctionName = Name;
		Graph->AddNode(Event, false, false);
		Event->CreateNewGuid();
		Event->AllocateDefaultPins();
		for (const TPair<const TCHAR*, FEdGraphPinType>& Input : Inputs)
		{
			Event->CreateUserDefinedPin(Input.Key, Input.Value, EGPD_Output, false);
		}
		return Event;
	};
	auto ArrayOf = [](FName Category, FName SubCategory)
	{
		FEdGraphPinType Type;
		Type.PinCategory = Category;
		Type.PinSubCategory = SubCategory;
		Type.ContainerType = EPinContainerType::Array;
		return Type;
	};
	FEdGraphPinType IntType;
	IntType.PinCategory = UEdGraphSchema_K2::PC_Int;
	const FEdGraphPinType IntArray = ArrayOf(UEdGraphSchema_K2::PC_Int, NAME_None);
	const FEdGraphPinType FloatArray = ArrayOf(UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Float);
	auto Then = [](UEdGraphNode* Node) { return Node->FindPinChecked(UEdGraphSchema_K2::PN_Then); };
	bool bWired = true;

	// 1. A Blueprint loop: ForLoop 0..Last, the multiple node on each index.
	{
		UK2Node_CustomEvent* Event = AddEvent(TEXT("RunLoop"), { { TEXT("Last"), IntType } });
		UK2Node_MacroInstance* Loop = NewObject<UK2Node_MacroInstance>(Graph);
		Loop->SetMacroGraph(*ForLoopGraph);
		Graph->AddNode(Loop, false, false);
		Loop->CreateNewGuid();
		Loop->AllocateDefaultPins();

		UK2Node_SetNamedInstanceDataMulti* Multi = Place<UK2Node_SetNamedInstanceDataMulti>(Graph);
		Schema->TrySetDefaultObject(*Multi->FindPinChecked(UK2Node_SetNamedInstanceDataMulti::LegendPinName), Legend);
		Multi->SetParameterSelected(Wind, true);
		Multi->SetParameterSelected(Glow, true);
		Schema->TrySetDefaultValue(*Multi->FindValuePin(Wind), TEXT("0.25"));
		Schema->TrySetDefaultValue(*Multi->FindValuePin(Glow), TEXT("0.75"));

		UEdGraphPin* LoopExec = nullptr;
		for (UEdGraphPin* Pin : Loop->Pins)
		{
			if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				LoopExec = Pin;
				break;
			}
		}
		bWired &= LoopExec && Schema->TryCreateConnection(Then(Event), LoopExec);
		Schema->TrySetDefaultValue(*Loop->FindPinChecked(TEXT("FirstIndex")), TEXT("0"));
		bWired &= Schema->TryCreateConnection(Event->FindPinChecked(TEXT("Last")), Loop->FindPinChecked(TEXT("LastIndex")));
		bWired &= Schema->TryCreateConnection(Loop->FindPinChecked(TEXT("LoopBody")), Multi->GetExecPin());
		bWired &= Schema->TryCreateConnection(Loop->FindPinChecked(TEXT("Index")),
			Multi->FindPinChecked(UK2Node_SetNamedInstanceDataMulti::InstanceIndexPinName));
	}

	// 2. One batch node, the same values for every instance.
	{
		UK2Node_CustomEvent* Event = AddEvent(TEXT("RunBatch"), { { TEXT("Indices"), IntArray } });
		UK2Node_SetNamedInstanceDataBatch* Batch = Place<UK2Node_SetNamedInstanceDataBatch>(Graph);
		Schema->TrySetDefaultObject(*Batch->FindPinChecked(UK2Node_SetNamedInstanceDataBatch::LegendPinName), Legend);
		Batch->SetParameterSelected(Wind, true);
		Batch->SetParameterSelected(Glow, true);
		Schema->TrySetDefaultValue(*Batch->FindValuePin(Wind), TEXT("0.25"));
		Schema->TrySetDefaultValue(*Batch->FindValuePin(Glow), TEXT("0.75"));
		bWired &= Schema->TryCreateConnection(Then(Event), Batch->GetExecPin());
		bWired &= Schema->TryCreateConnection(Event->FindPinChecked(TEXT("Indices")),
			Batch->FindPinChecked(UK2Node_SetNamedInstanceDataBatch::InstanceIndicesPinName));
	}

	// 3. One batch node, Glow per instance.
	{
		UK2Node_CustomEvent* Event = AddEvent(TEXT("RunBatchPerInstance"), { { TEXT("Indices"), IntArray }, { TEXT("GlowValues"), FloatArray } });
		UK2Node_SetNamedInstanceDataBatch* Batch = Place<UK2Node_SetNamedInstanceDataBatch>(Graph);
		Schema->TrySetDefaultObject(*Batch->FindPinChecked(UK2Node_SetNamedInstanceDataBatch::LegendPinName), Legend);
		Batch->SetParameterSelected(Wind, true);
		Batch->SetParameterSelected(Glow, true);
		Batch->SetParameterPerInstance(Glow, true);
		Schema->TrySetDefaultValue(*Batch->FindValuePin(Wind), TEXT("0.25"));
		bWired &= Schema->TryCreateConnection(Then(Event), Batch->GetExecPin());
		bWired &= Schema->TryCreateConnection(Event->FindPinChecked(TEXT("Indices")),
			Batch->FindPinChecked(UK2Node_SetNamedInstanceDataBatch::InstanceIndicesPinName));
		bWired &= Schema->TryCreateConnection(Event->FindPinChecked(TEXT("GlowValues")), Batch->FindValuePin(Glow));
	}
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

	const TStrongObjectPtr<UInstancedStaticMeshComponent> Mesh(
		NewObject<UInstancedStaticMeshComponent>(GetTransientPackage(), Blueprint->GeneratedClass));
	Mesh->SetNumCustomDataFloats(Floats);
	TArray<FTransform> Transforms;
	Transforms.Init(FTransform::Identity, NumInstances);
	Mesh->AddInstances(Transforms, /*bShouldReturnIndices*/ false);

	TArray<int32> Indices;
	TArray<float> GlowValues;
	for (int32 Index = 0; Index < NumInstances; ++Index)
	{
		Indices.Add(Index);
		GlowValues.Add(0.75f);
	}

	// Best of five, after a reset to 9s each time; returns milliseconds.
	auto Time = [&Mesh](const TCHAR* Event, void* Params)
	{
		UFunction* Function = Mesh->FindFunction(Event);
		double Best = TNumericLimits<double>::Max();
		for (int32 Pass = 0; Pass < 5 && Function; ++Pass)
		{
			for (float& Value : Mesh->PerInstanceSMCustomData)
			{
				Value = 9.f;
			}
			const double Start = FPlatformTime::Seconds();
			Mesh->ProcessEvent(Function, Params);
			Best = FMath::Min(Best, (FPlatformTime::Seconds() - Start) * 1000.0);
		}
		return Function ? Best : -1.0;
	};
	auto Check = [this, &Mesh](const TCHAR* What)
	{
		bool bAll = Mesh->PerInstanceSMCustomData.Num() == NumInstances * Floats;
		for (int32 Instance = 0; Instance < NumInstances && bAll; ++Instance)
		{
			const float* Row = &Mesh->PerInstanceSMCustomData[Instance * Floats];
			bAll = Row[0] == 0.25f && Row[1] == 9.f && Row[3] == 9.f && Row[4] == 0.75f && Row[5] == 9.f;
		}
		TestTrue(FString::Printf(TEXT("%s: every instance set, and only the ticked floats"), What), bAll);
	};

	int32 Last = NumInstances - 1;
	const double LoopMs = Time(TEXT("RunLoop"), &Last);
	Check(TEXT("Blueprint loop"));

	const double BatchMs = Time(TEXT("RunBatch"), &Indices);
	Check(TEXT("Batch"));

	struct
	{
		TArray<int32> Indices;
		TArray<float> GlowValues;
	} PerInstanceParams{ Indices, GlowValues };
	const double PerInstanceMs = Time(TEXT("RunBatchPerInstance"), &PerInstanceParams);
	Check(TEXT("Batch per instance"));

	AddInfo(FString::Printf(TEXT("%d instances, 2 parameters. Blueprint ForLoop + Multiple: %.3f ms. Batch: %.3f ms (%.0fx). Batch, one per instance: %.3f ms (%.0fx)."),
		NumInstances, LoopMs, BatchMs, LoopMs / FMath::Max(BatchMs, 1e-6), PerInstanceMs, LoopMs / FMath::Max(PerInstanceMs, 1e-6)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
