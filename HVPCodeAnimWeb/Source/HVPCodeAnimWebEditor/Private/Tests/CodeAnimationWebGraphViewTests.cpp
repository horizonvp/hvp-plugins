#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CodeAnimWebTestFixture.h"
#include "CodeAnimWebGraphs.h"

#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SGraphNode.h"
#include "WebGraph/CodeAnimWebGraph.h"
#include "WebGraph/SCodeAnimWebGraphEditor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebGraphViewTest, "HVP.CodeAnimWeb.WebGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodeAnimWebGraphViewTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;

	FFixture Fixture;
	UUserDefinedEnum* Enum = Fixture.Enum.Get();
	const FName Key0(Enum->GetNameStringByIndex(0));
	const FName Key1(Enum->GetNameStringByIndex(1));

	const TStrongObjectPtr<UCodeAnimWebEdGraph> Holder(NewObject<UCodeAnimWebEdGraph>(GetTransientPackage(), NAME_None, RF_Transient));
	UCodeAnimWebEdGraph* Graph = Holder.Get();
	Graph->Schema = UCodeAnimWebGraphSchema::StaticClass();
	Graph->Blueprint = Fixture.Blueprint.Get();
	Graph->Rebuild();

	auto CountOf = [Graph](UClass* Class)
	{
		return Graph->Nodes.FilterByPredicate([Class](const UEdGraphNode* Node) { return Node && Node->IsA(Class); }).Num();
	};
	TestEqual(TEXT("A node per state"), CountOf(UCodeAnimWebGraphStateNode::StaticClass()), 3);
	TestEqual(TEXT("No transitions yet"), CountOf(UCodeAnimWebGraphTransitionNode::StaticClass()), 0);
	auto InitialOf = [Graph]() -> FName
	{
		const UCodeAnimWebGraphEntryNode* Entry = Graph->FindEntry();
		const UEdGraphPin* Out = Entry ? Entry->GetOutputPin() : nullptr;
		const UCodeAnimWebGraphStateNode* Target = Out && Out->LinkedTo.Num() == 1
			? Cast<UCodeAnimWebGraphStateNode>(Out->LinkedTo[0]->GetOwningNode()) : nullptr;
		return Target ? Target->StateKey : NAME_None;
	};
	TestEqual(TEXT("Entry points at the first state"), InitialOf(), Key0);

	// Dragging from state 0's edge onto state 1 adds a transition.
	const UEdGraphSchema* Schema = Graph->GetSchema();
	UCodeAnimWebGraphStateNode* S0 = Graph->FindState(Key0);
	UCodeAnimWebGraphStateNode* S1 = Graph->FindState(Key1);
	FText Why;
	CodeAnimWebGraphEdits::SetDragSource(S0);
	TestTrue(TEXT("Drop onto a state is accepted"),
		Schema->SupportsDropPinOnNode(S1, S0->GetOutputPin()->PinType, EGPD_Output, Why));
	TestEqual(TEXT("...onto its input"), Schema->DropPinOnNode(S1, NAME_None, S0->GetOutputPin()->PinType, EGPD_Output), S1->GetInputPin());
	TestTrue(TEXT("Hovering the middle reads as the transition"), Why.ToString().Contains(TEXT("Add")));
	CodeAnimWebGraphEdits::SetDragSource(nullptr);
	TestTrue(TEXT("Connect adds a transition"), Schema->TryCreateConnection(S0->GetOutputPin(), S1->GetInputPin()));
	TestEqual(TEXT("Transitions row written"), Fixture.Defaults()->Transitions.Num(), 1);
	TestTrue(TEXT("...with the right ends"), Fixture.Defaults()->Transitions.Num() == 1
		&& Fixture.Defaults()->Transitions[0].From.Key == Key0 && Fixture.Defaults()->Transitions[0].To.Key == Key1);

	Graph->Rebuild();
	S0 = Graph->FindState(Key0);
	S1 = Graph->FindState(Key1);
	TestEqual(TEXT("Rebuilt with the transition"), CountOf(UCodeAnimWebGraphTransitionNode::StaticClass()), 1);
	TestEqual(TEXT("Same pair again is refused"),
		Schema->CanCreateConnection(S0->GetOutputPin(), S1->GetInputPin()).Response, CONNECT_RESPONSE_DISALLOW);
	TestTrue(TEXT("The way back is allowed while one-way"),
		Schema->CanCreateConnection(S1->GetOutputPin(), S0->GetInputPin()).Response != CONNECT_RESPONSE_DISALLOW);
	TestEqual(TEXT("A state cannot transition to itself"),
		Schema->CanCreateConnection(S0->GetOutputPin(), S0->GetInputPin()).Response, CONNECT_RESPONSE_DISALLOW);

	CodeAnimWebGraphEdits::SetTwoWay(Graph, 0, true);
	Graph->Rebuild();
	S0 = Graph->FindState(Key0);
	S1 = Graph->FindState(Key1);
	TestEqual(TEXT("Two-way covers the way back"),
		Schema->CanCreateConnection(S1->GetOutputPin(), S0->GetInputPin()).Response, CONNECT_RESPONSE_DISALLOW);

	// Dragging the Entry arrow onto another state makes it initial.
	TestTrue(TEXT("Entry drag accepted"), Schema->TryCreateConnection(Graph->FindEntry()->GetOutputPin(), Graph->FindState(Key1)->GetInputPin()));
	CodeAnimWebGraphEdits::SetStatePosition(Graph, Key1, FVector2D(640.0, 480.0));
	Graph->Rebuild();
	TestEqual(TEXT("Initial state moved"), InitialOf(), Key1);
	TestEqual(TEXT("...in the data"), Fixture.Defaults()->InitialState.Key, Key1);
	TestTrue(TEXT("Position saved and used"), Graph->FindState(Key1)->NodePosX == 640 && Graph->FindState(Key1)->NodePosY == 480);

	// The widgets build - the tab (graph + Details) and each node's own look.
	if (FSlateApplication::IsInitialized())
	{
		const TSharedRef<SCodeAnimWebGraphEditor> Editor = SNew(SCodeAnimWebGraphEditor, Fixture.Blueprint.Get(), nullptr);

		// The Details of each kind of selection build: the Web's rows, reached through the selection object.
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		const TSharedRef<IDetailsView> Details = PropertyEditor.CreateDetailView(FDetailsViewArgs());
		const TStrongObjectPtr<UCodeAnimWebGraphSelection> Selected(NewObject<UCodeAnimWebGraphSelection>(GetTransientPackage()));
		Selected->Blueprint = Fixture.Blueprint.Get();
		Selected->Kind = UCodeAnimWebGraphSelection::EKind::Web;
		Details->SetObject(Selected.Get(), true);
		Selected->Kind = UCodeAnimWebGraphSelection::EKind::State;
		Selected->StateKey = Key1;
		Details->SetObject(Selected.Get(), true);
		Selected->Kind = UCodeAnimWebGraphSelection::EKind::Transition;
		Selected->TransitionIndex = 0;
		Details->SetObject(Selected.Get(), true);
		TestTrue(TEXT("Selection Details built"), Details->GetSelectedObjects().Num() == 1);
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			TestTrue(FString::Printf(TEXT("%s has a widget"), *Node->GetName()), Node->CreateVisualWidget().IsValid());
		}
	}

	CodeAnimWebGraphEdits::RemoveTransitions(Graph, { 0 });
	Graph->Rebuild();
	TestEqual(TEXT("Delete removes the row"), Fixture.Defaults()->Transitions.Num(), 0);
	TestEqual(TEXT("...and the arrow"), CountOf(UCodeAnimWebGraphTransitionNode::StaticClass()), 0);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebGraphCleanupTest, "HVP.CodeAnimWeb.GraphCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Web's graphs go when their owners do - a transition deleted in the Web Graph, an output taken off
 * Custom, a state's graph unlinked - and a function made by hand never does, even named like one.
 */
bool FCodeAnimWebGraphCleanupTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;

	FFixture Fixture;
	UBlueprint* BP = Fixture.Blueprint.Get();
	UUserDefinedEnum* Enum = Fixture.Enum.Get();
	const FName Key0(Enum->GetNameStringByIndex(0));
	const FName Key1(Enum->GetNameStringByIndex(1));

	FCodeAnimWebTransition& Transition = Fixture.Defaults()->Transitions.AddDefaulted_GetRef();
	Transition.From.Key = Key0;
	Transition.To.Key = Key1;
	const FGuid TransitionGraph = CodeAnimWebGraphs::OpenOrCreateTransitionGraph(Fixture.Defaults(), 0, false)->GraphGuid;
	const FGuid StateGraph = CodeAnimWebGraphs::OpenOrCreateStateGraph(Fixture.Defaults(), Key1, false)->GraphGuid;
	FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, TEXT("Scale"), nullptr, CodeAnimWeb::LerpMetaKey, TEXT("Custom"));
	const FGuid LerpGraph = CodeAnimWebGraphs::OpenOrCreateCustomLerpGraph(BP, TEXT("Scale"), false)->GraphGuid;

	// Someone's own function, named as if it were a transition graph.
	UEdGraph* Mine = FBlueprintEditorUtils::CreateNewGraph(BP, TEXT("Transition_Mine"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, Mine, /*bIsUserCreated*/ true, nullptr);
	const FGuid MyGraph = Mine->GraphGuid;

	auto Has = [BP](const FGuid& Guid) { return CodeAnimWebGraphs::FindGraph(BP, Guid) != nullptr; };
	TestTrue(TEXT("All four graphs exist"), Has(TransitionGraph) && Has(StateGraph) && Has(LerpGraph) && Has(MyGraph));
	TestEqual(TEXT("The Web records the three it made"), Fixture.Defaults()->OwnedGraphs.Num(), 3);
	TestFalse(TEXT("Nothing to remove while every owner is there"), CodeAnimWebGraphs::RemoveUnusedGraphs(BP));

	// Deleting the transition in the Web Graph deletes its graph.
	const TStrongObjectPtr<UCodeAnimWebEdGraph> Holder(NewObject<UCodeAnimWebEdGraph>(GetTransientPackage(), NAME_None, RF_Transient));
	UCodeAnimWebEdGraph* Graph = Holder.Get();
	Graph->Schema = UCodeAnimWebGraphSchema::StaticClass();
	Graph->Blueprint = BP;
	Graph->Rebuild();
	CodeAnimWebGraphEdits::RemoveTransitions(Graph, { 0 });
	TestFalse(TEXT("Transition graph deleted with the transition"), Has(TransitionGraph));
	TestTrue(TEXT("The others stay"), Has(StateGraph) && Has(LerpGraph) && Has(MyGraph));

	// Taking the output off Custom deletes its Custom Lerp graph and entry.
	FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BP, TEXT("Scale"), nullptr, CodeAnimWeb::LerpMetaKey);
	TestTrue(TEXT("Off Custom: something removed"), CodeAnimWebGraphs::RemoveUnusedGraphs(BP));
	TestFalse(TEXT("Custom Lerp graph deleted"), Has(LerpGraph));
	TestEqual(TEXT("Custom Lerp entry gone"), Fixture.Defaults()->CustomLerps.Num(), 0);

	// A Web from before the record: graphs in use are adopted, then go with their owner like any other.
	Fixture.Defaults()->OwnedGraphs.Reset();
	TestFalse(TEXT("Adopting removes nothing"), CodeAnimWebGraphs::RemoveUnusedGraphs(BP));
	TestTrue(TEXT("The state graph adopted"), Fixture.Defaults()->OwnedGraphs.Contains(StateGraph));
	TestFalse(TEXT("A function it did not make is not adopted"), Fixture.Defaults()->OwnedGraphs.Contains(MyGraph));
	Fixture.Entry(1).GraphGuid = FGuid();
	TestTrue(TEXT("State graph unlinked: removed"), CodeAnimWebGraphs::RemoveUnusedGraphs(BP));
	TestFalse(TEXT("State graph deleted"), Has(StateGraph));

	TestTrue(TEXT("The hand-made function survives all of it"), Has(MyGraph));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebGraphNamesTest, "HVP.CodeAnimWeb.GraphNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A Web's graph names follow their owners - direction, ends, output name - unless renamed by hand. */
bool FCodeAnimWebGraphNamesTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;

	FFixture Fixture;
	UBlueprint* BP = Fixture.Blueprint.Get();
	UUserDefinedEnum* Enum = Fixture.Enum.Get();
	const FString S0 = Enum->GetDisplayNameTextByIndex(0).ToString();
	const FString S1 = Enum->GetDisplayNameTextByIndex(1).ToString();
	const FString S2 = Enum->GetDisplayNameTextByIndex(2).ToString();

	for (const int32 To : { 1, 2 })
	{
		FCodeAnimWebTransition& Transition = Fixture.Defaults()->Transitions.AddDefaulted_GetRef();
		Transition.From.Key = FName(Enum->GetNameStringByIndex(0));
		Transition.To.Key = FName(Enum->GetNameStringByIndex(To));
	}
	UEdGraph* First = CodeAnimWebGraphs::OpenOrCreateTransitionGraph(Fixture.Defaults(), 0, false);
	UEdGraph* Second = CodeAnimWebGraphs::OpenOrCreateTransitionGraph(Fixture.Defaults(), 1, false);
	TestEqual(TEXT("Named at creation"), First->GetName(), FString::Printf(TEXT("Transition_%s_To_%s"), *S0, *S1));

	const TStrongObjectPtr<UCodeAnimWebEdGraph> Holder(NewObject<UCodeAnimWebEdGraph>(GetTransientPackage(), NAME_None, RF_Transient));
	UCodeAnimWebEdGraph* Graph = Holder.Get();
	Graph->Schema = UCodeAnimWebGraphSchema::StaticClass();
	Graph->Blueprint = BP;
	Graph->Rebuild();

	// One-way to two-way, and back.
	CodeAnimWebGraphEdits::SetTwoWay(Graph, 0, true);
	TestEqual(TEXT("Two-way: renamed"), First->GetName(), FString::Printf(TEXT("Transition_%s_And_%s"), *S0, *S1));
	TestEqual(TEXT("The Web's record follows"), Fixture.Defaults()->Transitions[0].GraphFunction, First->GetFName());
	CodeAnimWebGraphEdits::SetTwoWay(Graph, 0, false);
	TestEqual(TEXT("One-way again"), First->GetName(), FString::Printf(TEXT("Transition_%s_To_%s"), *S0, *S1));

	// Reversed: the ends swap in the name.
	CodeAnimWebGraphEdits::Reverse(Graph, 0);
	TestEqual(TEXT("Reversed"), First->GetName(), FString::Printf(TEXT("Transition_%s_To_%s"), *S1, *S0));

	// Renamed by hand: left alone from then on.
	FBlueprintEditorUtils::RenameGraph(Second, TEXT("MySpecialSwoop"));
	CodeAnimWebGraphEdits::SetTwoWay(Graph, 1, true);
	TestEqual(TEXT("A hand-picked name stays"), Second->GetName(), FString(TEXT("MySpecialSwoop")));
	TestFalse(TEXT("Nothing left to rename"), CodeAnimWebGraphs::RenameGraphsToMatch(BP));

	// A Custom Lerp follows its output's name.
	FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, TEXT("Scale"), nullptr, CodeAnimWeb::LerpMetaKey, TEXT("Custom"));
	UEdGraph* Lerp = CodeAnimWebGraphs::OpenOrCreateCustomLerpGraph(BP, TEXT("Scale"), false);
	FBlueprintEditorUtils::RenameMemberVariable(BP, TEXT("Scale"), TEXT("Size"));
	TestTrue(TEXT("Output renamed: something to rename"), CodeAnimWebGraphs::RenameGraphsToMatch(BP));
	TestEqual(TEXT("Lerp graph follows the output"), Lerp->GetName(), FString(TEXT("Lerp_Size")));
	(void)S2;
	return true;
}

#endif
