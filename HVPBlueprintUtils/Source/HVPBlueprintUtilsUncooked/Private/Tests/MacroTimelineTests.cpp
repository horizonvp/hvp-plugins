#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Tunnel.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MacroTimelineTests
{
	const TCHAR* LibraryPath = TEXT("/HVPBlueprintUtils/BlueprintMacroTimeline_BP.BlueprintMacroTimeline_BP");
	const TCHAR* EnumPath = TEXT("/HVPBlueprintUtils/MacroTimelineDirection_E.MacroTimelineDirection_E");
	const TCHAR* MacroName = TEXT("Macro Timeline");

	UEdGraph* FindMacro(UBlueprint* Library)
	{
		for (UEdGraph* Graph : Library->MacroGraphs)
		{
			if (Graph && Graph->GetName() == MacroName)
			{
				return Graph;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMacroTimelineEnumTest, "HVP.BlueprintUtils.MacroTimeline.EnumReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The way Macro Timeline breaks when it moves between projects: its pins carry the direction enum as
 * a type object, and when the enum's path changes underneath them they keep pointing at the old one
 * (or at nothing). Every enum-typed pin and enum literal in the macro must resolve to the plugin's
 * own MacroTimelineDirection_E.
 */
bool FMacroTimelineEnumTest::RunTest(const FString& Parameters)
{
	using namespace MacroTimelineTests;

	UBlueprint* Library = LoadObject<UBlueprint>(nullptr, LibraryPath);
	UUserDefinedEnum* Direction = LoadObject<UUserDefinedEnum>(nullptr, EnumPath);
	if (!TestNotNull(TEXT("Macro library loads"), Library) || !TestNotNull(TEXT("Direction enum loads"), Direction))
	{
		return false;
	}
	UEdGraph* Macro = FindMacro(Library);
	if (!TestNotNull(TEXT("Library has a Macro Timeline graph"), Macro))
	{
		return false;
	}

	int32 EnumPins = 0;
	for (UEdGraphNode* Node : Macro->Nodes)
	{
		if (const UK2Node_EnumLiteral* Literal = Cast<UK2Node_EnumLiteral>(Node))
		{
			TestEqual(FString::Printf(TEXT("Enum literal %s uses the plugin enum"), *Node->GetName()),
				static_cast<UObject*>(Literal->Enum), static_cast<UObject*>(Direction));
		}
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			const bool bTunnel = Node->IsA<UK2Node_Tunnel>();
			UObject* TypeObject = Pin->PinType.PinSubCategoryObject.Get();
			if (bTunnel)
			{
				AddInfo(FString::Printf(TEXT("Interface %s %s: %s %s"),
					Pin->Direction == EGPD_Input ? TEXT("out") : TEXT("in"),
					*Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString(),
					TypeObject ? *TypeObject->GetPathName() : TEXT("-")));
			}
			if (const UEnum* Enum = Cast<UEnum>(TypeObject))
			{
				++EnumPins;
				TestEqual(FString::Printf(TEXT("%s.%s is typed by the plugin enum"), *Node->GetName(), *Pin->PinName.ToString()),
					static_cast<const UObject*>(Enum), static_cast<const UObject*>(Direction));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d enum-typed pins checked"), EnumPins));
	TestTrue(TEXT("The macro has enum-typed pins to check"), EnumPins > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMacroTimelineRunTest, "HVP.BlueprintUtils.MacroTimeline.CompilesAndRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * End to end, from a consumer's side: an actor Blueprint drops in Macro Timeline, stores its Position
 * and Direction in its own variables (Direction typed by the plugin enum, which is where a stale enum
 * shows up as a compile error), and plays forward then reverse in a ticking game world.
 */
bool FMacroTimelineRunTest::RunTest(const FString& Parameters)
{
	using namespace MacroTimelineTests;

	UBlueprint* Library = LoadObject<UBlueprint>(nullptr, LibraryPath);
	UUserDefinedEnum* Direction = LoadObject<UUserDefinedEnum>(nullptr, EnumPath);
	UEdGraph* Macro = Library ? FindMacro(Library) : nullptr;
	if (!TestNotNull(TEXT("Macro Timeline loads"), Macro) || !TestNotNull(TEXT("Direction enum loads"), Direction))
	{
		return false;
	}

	// --- Build the consumer Blueprint.
	UPackage* Package = CreatePackage(TEXT("/Temp/HVPBlueprintUtilsTest/BP_MacroTimelineTest"));
	const TStrongObjectPtr<UBlueprint> BlueprintOwner(FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), Package, TEXT("BP_MacroTimelineTest"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	UBlueprint* Blueprint = BlueprintOwner.Get();
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("Event graph"), Graph))
	{
		return false;
	}
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	FEdGraphPinType RealType(UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
	FEdGraphPinType DirectionType(UEdGraphSchema_K2::PC_Byte, NAME_None, Direction, EPinContainerType::None, false, FEdGraphTerminalType());
	FEdGraphPinType BoolType(UEdGraphSchema_K2::PC_Boolean, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("LastPosition"), RealType);
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("LastDirection"), DirectionType);
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("bFinished"), BoolType);

	auto AddEvent = [Graph](const TCHAR* Name)
	{
		UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
		Event->CustomFunctionName = Name;
		Graph->AddNode(Event, false, false);
		Event->CreateNewGuid();
		Event->AllocateDefaultPins();
		return Event;
	};
	auto AddSet = [Graph](const TCHAR* Name)
	{
		UK2Node_VariableSet* Set = NewObject<UK2Node_VariableSet>(Graph);
		Set->VariableReference.SetSelfMember(Name);
		Graph->AddNode(Set, false, false);
		Set->CreateNewGuid();
		Set->AllocateDefaultPins();
		return Set;
	};

	UK2Node_CustomEvent* Forward = AddEvent(TEXT("PlayForward"));
	UK2Node_CustomEvent* Backward = AddEvent(TEXT("PlayReverse"));

	UK2Node_MacroInstance* Timeline = NewObject<UK2Node_MacroInstance>(Graph);
	Timeline->SetMacroGraph(Macro);
	Graph->AddNode(Timeline, false, false);
	Timeline->CreateNewGuid();
	Timeline->AllocateDefaultPins();

	UEdGraphPin* TimelineDirection = Timeline->FindPin(TEXT("Direction"));
	if (!TestNotNull(TEXT("Instance has a Direction pin"), TimelineDirection))
	{
		return false;
	}
	TestEqual(TEXT("The instance's Direction pin is typed by the plugin enum"),
		TimelineDirection->PinType.PinSubCategoryObject.Get(), static_cast<UObject*>(Direction));

	UK2Node_VariableSet* SetPosition = AddSet(TEXT("LastPosition"));
	UK2Node_VariableSet* SetDirection = AddSet(TEXT("LastDirection"));
	UK2Node_VariableSet* SetFinished = AddSet(TEXT("bFinished"));

	bool bWired = true;
	bWired &= Schema->TryCreateConnection(Forward->FindPinChecked(UEdGraphSchema_K2::PN_Then), Timeline->FindPinChecked(TEXT("Play")));
	bWired &= Schema->TryCreateConnection(Backward->FindPinChecked(UEdGraphSchema_K2::PN_Then), Timeline->FindPinChecked(TEXT("Reverse from End")));
	bWired &= Schema->TryCreateConnection(Timeline->FindPinChecked(TEXT("Update")), SetPosition->GetExecPin());
	bWired &= Schema->TryCreateConnection(Timeline->FindPinChecked(TEXT("Position")), SetPosition->FindPinChecked(TEXT("LastPosition")));
	bWired &= Schema->TryCreateConnection(SetPosition->FindPinChecked(UEdGraphSchema_K2::PN_Then), SetDirection->GetExecPin());
	bWired &= Schema->TryCreateConnection(TimelineDirection, SetDirection->FindPinChecked(TEXT("LastDirection")));
	bWired &= Schema->TryCreateConnection(Timeline->FindPinChecked(TEXT("Finished")), SetFinished->GetExecPin());
	TestTrue(TEXT("Graph wired"), bWired);
	Schema->TrySetDefaultValue(*Timeline->FindPinChecked(TEXT("Length")), TEXT("0.25"));
	Schema->TrySetDefaultValue(*SetFinished->FindPinChecked(TEXT("bFinished")), TEXT("true"));

	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		if (Message->GetSeverity() <= EMessageSeverity::Warning)
		{
			AddError(FString::Printf(TEXT("Compile: %s"), *Message->ToText().ToString()));
		}
	}
	if (!TestEqual(TEXT("Compiles without errors"), Results.NumErrors, 0) || !Blueprint->GeneratedClass)
	{
		return false;
	}
	TestEqual(TEXT("Compiles without warnings"), Results.NumWarnings, 0);

	// --- Run it in a ticking game world.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MacroTimelineTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* Actor = World->SpawnActor<AActor>(Blueprint->GeneratedClass);
	if (!TestNotNull(TEXT("Actor spawns"), Actor))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	UClass* Class = Actor->GetClass();
	const FDoubleProperty* PositionProp = FindFProperty<FDoubleProperty>(Class, TEXT("LastPosition"));
	const FByteProperty* DirectionProp = FindFProperty<FByteProperty>(Class, TEXT("LastDirection"));
	const FBoolProperty* FinishedProp = FindFProperty<FBoolProperty>(Class, TEXT("bFinished"));
	if (!TestTrue(TEXT("Variables compiled"), PositionProp && DirectionProp && FinishedProp))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	auto Position = [&]() { return PositionProp->GetPropertyValue_InContainer(Actor); };
	auto DirectionName = [&]() { return Direction->GetDisplayNameTextByValue(DirectionProp->GetPropertyValue_InContainer(Actor)).ToString(); };
	auto Finished = [&]() { return FinishedProp->GetPropertyValue_InContainer(Actor); };
	auto Tick = [World](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		}
	};
	auto Call = [Actor](const TCHAR* Name)
	{
		Actor->ProcessEvent(Actor->FindFunction(Name), nullptr);
	};

	// Forward: 0.25 s at 60 fps. Part way, it is moving and not finished; well past, it has finished at Length.
	Call(TEXT("PlayForward"));
	Tick(6);
	AddInfo(FString::Printf(TEXT("Forward, 6 frames: position %.4f, direction %s, finished %d"), Position(), *DirectionName(), Finished()));
	TestTrue(TEXT("Forward: moving part way"), Position() > 0.0 && Position() < 0.25);
	TestFalse(TEXT("Forward: not finished part way"), Finished());
	TestEqual(TEXT("Forward: direction"), DirectionName(), FString(TEXT("Forward")));
	Tick(30);
	AddInfo(FString::Printf(TEXT("Forward, 36 frames: position %.4f, direction %s, finished %d"), Position(), *DirectionName(), Finished()));
	TestTrue(TEXT("Forward: finished"), Finished());
	TestEqual(TEXT("Forward: ends at Length"), Position(), 0.25, 1e-4);

	// Reverse from the end: back down to 0.
	FinishedProp->SetPropertyValue_InContainer(Actor, false);
	Call(TEXT("PlayReverse"));
	Tick(6);
	AddInfo(FString::Printf(TEXT("Reverse, 6 frames: position %.4f, direction %s, finished %d"), Position(), *DirectionName(), Finished()));
	TestTrue(TEXT("Reverse: moving part way"), Position() > 0.0 && Position() < 0.25);
	TestEqual(TEXT("Reverse: direction"), DirectionName(), FString(TEXT("Reverse")));
	Tick(30);
	AddInfo(FString::Printf(TEXT("Reverse, 36 frames: position %.4f, direction %s, finished %d"), Position(), *DirectionName(), Finished()));
	TestTrue(TEXT("Reverse: finished"), Finished());
	TestEqual(TEXT("Reverse: ends at 0"), Position(), 0.0, 1e-4);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
