#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CodeAnimWebTestFixture.h"

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

#endif
