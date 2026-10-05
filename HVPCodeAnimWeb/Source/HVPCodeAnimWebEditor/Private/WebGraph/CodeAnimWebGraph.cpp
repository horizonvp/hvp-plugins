#include "WebGraph/CodeAnimWebGraph.h"

#include "CodeAnimationWeb.h"
#include "CodeAnimWebGraphs.h"
#include "Engine/Blueprint.h"
#include "Curves/CurveFloat.h"
#include "ScopedTransaction.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "WebGraph/SCodeAnimWebGraphNodes.h"

#define LOCTEXT_NAMESPACE "CodeAnimWebGraph"

const FName UCodeAnimWebGraphSchema::PC_Transition(TEXT("Transition"));

namespace CodeAnimWebGraph
{
	static FText StateName(const TArray<TPair<FName, FText>>& States, FName Key)
	{
		for (const TPair<FName, FText>& State : States)
		{
			if (State.Key == Key)
			{
				return State.Value;
			}
		}
		return FText::FromName(Key);
	}

	static FText TimingSummary(const FCodeAnimWebTransitionTiming& Timing)
	{
		if (Timing.Curve)
		{
			return FText::Format(LOCTEXT("CurveTiming", "{0}s, {1}"), FText::AsNumber(Timing.Duration), FText::FromString(Timing.Curve->GetName()));
		}
		const UEnum* Easing = StaticEnum<EEasingFunc::Type>();
		return FText::Format(LOCTEXT("EasedTiming", "{0}s, {1}"), FText::AsNumber(Timing.Duration),
			Easing ? Easing->GetDisplayNameTextByValue(Timing.Easing) : FText::GetEmpty());
	}
}

// ---------------------------------------------------------------------------
// Graph
// ---------------------------------------------------------------------------

UCodeAnimationWeb* UCodeAnimWebEdGraph::GetDefaults() const
{
	const UBlueprint* BP = Blueprint.Get();
	return BP && BP->GeneratedClass ? Cast<UCodeAnimationWeb>(BP->GeneratedClass->GetDefaultObject()) : nullptr;
}

UCodeAnimWebGraphEntryNode* UCodeAnimWebEdGraph::FindEntry() const
{
	for (UEdGraphNode* Node : Nodes)
	{
		if (UCodeAnimWebGraphEntryNode* Entry = Cast<UCodeAnimWebGraphEntryNode>(Node))
		{
			return Entry;
		}
	}
	return nullptr;
}

UCodeAnimationWeb* UCodeAnimWebGraphSelection::GetDefaults() const
{
	const UBlueprint* BP = Blueprint.Get();
	return BP && BP->GeneratedClass ? Cast<UCodeAnimationWeb>(BP->GeneratedClass->GetDefaultObject()) : nullptr;
}

UCodeAnimWebGraphStateNode* UCodeAnimWebEdGraph::FindState(FName Key) const
{
	for (UEdGraphNode* Node : Nodes)
	{
		UCodeAnimWebGraphStateNode* State = Cast<UCodeAnimWebGraphStateNode>(Node);
		if (State && State->StateKey == Key)
		{
			return State;
		}
	}
	return nullptr;
}

void UCodeAnimWebEdGraph::Rebuild()
{
	for (UEdGraphNode* Node : Nodes)
	{
		if (Node)
		{
			Node->BreakAllNodeLinks();
			Node->MarkAsGarbage();
		}
	}
	Nodes.Reset();

	const UCodeAnimationWeb* Defaults = GetDefaults();
	if (!Defaults)
	{
		NotifyGraphChanged();
		return;
	}

	const UBlueprint* BP = Blueprint.Get();
	TArray<TPair<FName, FText>> States;
	Defaults->GetStateList(States);
	const FName Initial = Defaults->InitialState.IsSet() ? Defaults->InitialState.Key : (States.Num() > 0 ? States[0].Key : NAME_None);
	const int32 NumOutputs = Defaults->GetOutputNames().Num();

	for (int32 Index = 0; Index < States.Num(); ++Index)
	{
		const FName Key = States[Index].Key;
		const FCodeAnimWebStateEntry* Entry = Defaults->States.FindByPredicate(
			[Key](const FCodeAnimWebStateEntry& E) { return E.Key == Key; });

		UCodeAnimWebGraphStateNode* Node = NewObject<UCodeAnimWebGraphStateNode>(this);
		Node->CreateNewGuid();
		Node->StateKey = Key;
		Node->DisplayName = States[Index].Value;
		Node->bHasGraph = Entry && CodeAnimWebGraphs::FindGraph(BP, Entry->GraphGuid) != nullptr;
		Node->NumSet = Entry ? Entry->OverriddenOutputs.Num() : 0;
		Node->NumOutputs = NumOutputs;
		Node->AllocateDefaultPins();

		if (Entry && Entry->bHasEditorPosition)
		{
			Node->NodePosX = FMath::RoundToInt(Entry->EditorPosition.X);
			Node->NodePosY = FMath::RoundToInt(Entry->EditorPosition.Y);
		}
		else
		{
			// Never placed: a loose grid, in enum order.
			Node->NodePosX = (Index % 4) * 320;
			Node->NodePosY = (Index / 4) * 220;
		}
		AddNode(Node, /*bFromUI*/ false, /*bSelectNewNode*/ false);
	}

	if (States.Num() > 0)
	{
		UCodeAnimWebGraphEntryNode* Entry = NewObject<UCodeAnimWebGraphEntryNode>(this);
		Entry->CreateNewGuid();
		Entry->AllocateDefaultPins();
		if (Defaults->bHasWebGraphEntryPosition)
		{
			Entry->NodePosX = FMath::RoundToInt(Defaults->WebGraphEntryPosition.X);
			Entry->NodePosY = FMath::RoundToInt(Defaults->WebGraphEntryPosition.Y);
		}
		else if (const UCodeAnimWebGraphStateNode* First = FindState(Initial))
		{
			Entry->NodePosX = First->NodePosX - 240;
			Entry->NodePosY = First->NodePosY;
		}
		AddNode(Entry, false, false);
		if (UCodeAnimWebGraphStateNode* InitialNode = FindState(Initial))
		{
			Entry->GetOutputPin()->MakeLinkTo(InitialNode->GetInputPin());
		}
	}

	for (int32 Index = 0; Index < Defaults->Transitions.Num(); ++Index)
	{
		const FCodeAnimWebTransition& Transition = Defaults->Transitions[Index];
		UCodeAnimWebGraphStateNode* From = FindState(Transition.From.Key);
		UCodeAnimWebGraphStateNode* To = FindState(Transition.To.Key);
		if (!From || !To || From == To)
		{
			continue;	// unset or stale ends: fixed from the Details panel, not drawable meanwhile
		}

		UCodeAnimWebGraphTransitionNode* Node = NewObject<UCodeAnimWebGraphTransitionNode>(this);
		Node->CreateNewGuid();
		Node->TransitionIndex = Index;
		Node->bTwoWay = Transition.bTwoWay;
		Node->bHasGraph = CodeAnimWebGraphs::FindGraph(BP, Transition.GraphGuid) != nullptr;
		Node->Summary = CodeAnimWebGraph::TimingSummary(Transition.Timing);
		Node->AllocateDefaultPins();
		Node->NodePosX = (From->NodePosX + To->NodePosX) / 2;
		Node->NodePosY = (From->NodePosY + To->NodePosY) / 2;
		AddNode(Node, false, false);

		From->GetOutputPin()->MakeLinkTo(Node->GetInputPin());
		Node->GetOutputPin()->MakeLinkTo(To->GetInputPin());
	}

	NotifyGraphChanged();
}

// ---------------------------------------------------------------------------
// Nodes
// ---------------------------------------------------------------------------

void UCodeAnimWebGraphEntryNode::AllocateDefaultPins()
{
	CreatePin(EGPD_Output, UCodeAnimWebGraphSchema::PC_Transition, TEXT("Out"));
}

FText UCodeAnimWebGraphEntryNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Entry", "Entry");
}

FText UCodeAnimWebGraphEntryNode::GetTooltipText() const
{
	return LOCTEXT("EntryTooltip", "Where the Web starts. Drag from here onto a state to make it the initial state.");
}

TSharedPtr<SGraphNode> UCodeAnimWebGraphEntryNode::CreateVisualWidget()
{
	return SNew(SCodeAnimWebEntryNode, this);
}

void UCodeAnimWebGraphStateNode::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UCodeAnimWebGraphSchema::PC_Transition, TEXT("In"));
	CreatePin(EGPD_Output, UCodeAnimWebGraphSchema::PC_Transition, TEXT("Out"));
}

FText UCodeAnimWebGraphStateNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return DisplayName;
}

FText UCodeAnimWebGraphStateNode::GetTooltipText() const
{
	return LOCTEXT("StateTooltip",
		"A state of the Web. Drag from its edge to another state to add a transition; double-click for its state graph.");
}

TSharedPtr<SGraphNode> UCodeAnimWebGraphStateNode::CreateVisualWidget()
{
	return SNew(SCodeAnimWebStateNode, this);
}

void UCodeAnimWebGraphTransitionNode::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UCodeAnimWebGraphSchema::PC_Transition, TEXT("In"));
	CreatePin(EGPD_Output, UCodeAnimWebGraphSchema::PC_Transition, TEXT("Out"));
}

UCodeAnimWebGraphStateNode* UCodeAnimWebGraphTransitionNode::GetFromState() const
{
	const UEdGraphPin* In = Pins.IsValidIndex(0) ? Pins[0] : nullptr;
	return In && In->LinkedTo.Num() > 0 ? Cast<UCodeAnimWebGraphStateNode>(In->LinkedTo[0]->GetOwningNode()) : nullptr;
}

UCodeAnimWebGraphStateNode* UCodeAnimWebGraphTransitionNode::GetToState() const
{
	const UEdGraphPin* Out = Pins.IsValidIndex(1) ? Pins[1] : nullptr;
	return Out && Out->LinkedTo.Num() > 0 ? Cast<UCodeAnimWebGraphStateNode>(Out->LinkedTo[0]->GetOwningNode()) : nullptr;
}

FText UCodeAnimWebGraphTransitionNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const UCodeAnimWebGraphStateNode* From = GetFromState();
	const UCodeAnimWebGraphStateNode* To = GetToState();
	if (!From || !To)
	{
		return LOCTEXT("Transition", "Transition");
	}
	return FText::Format(bTwoWay ? LOCTEXT("TwoWayTitle", "{0} ↔ {1}") : LOCTEXT("OneWayTitle", "{0} → {1}"),
		From->DisplayName, To->DisplayName);
}

FText UCodeAnimWebGraphTransitionNode::GetTooltipText() const
{
	return FText::Format(LOCTEXT("TransitionTooltip", "{0}\n{1}{2}\n\nDouble-click for its transition graph."),
		GetNodeTitle(ENodeTitleType::FullTitle), Summary,
		bHasGraph ? LOCTEXT("WithGraph", ", with a transition graph") : FText::GetEmpty());
}

TSharedPtr<SGraphNode> UCodeAnimWebGraphTransitionNode::CreateVisualWidget()
{
	return SNew(SCodeAnimWebTransitionNode, this);
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

namespace CodeAnimWebGraph
{
	/** The pin a drag started from: the output end. Either argument order is accepted. */
	static const UEdGraphPin* SourceOf(const UEdGraphPin* A, const UEdGraphPin* B)
	{
		return A && A->Direction == EGPD_Output ? A : B;
	}

	/**
	 * The two states a drag would join, source first. The target is whichever state the other pin belongs
	 * to - its hidden input or its edge, it is the state that is meant.
	 */
	static bool StatesFor(const UEdGraphPin* A, const UEdGraphPin* B,
		const UCodeAnimWebGraphStateNode*& OutFrom, const UCodeAnimWebGraphStateNode*& OutTo)
	{
		const UEdGraphPin* Source = SourceOf(A, B);
		const UEdGraphPin* Target = Source == A ? B : A;
		OutFrom = Source ? Cast<UCodeAnimWebGraphStateNode>(Source->GetOwningNode()) : nullptr;
		OutTo = Target ? Cast<UCodeAnimWebGraphStateNode>(Target->GetOwningNode()) : nullptr;
		return OutFrom && OutTo;
	}

	/** A drag from the Entry node onto a state (any of its pins): the state it would make initial, or null. */
	static const UCodeAnimWebGraphStateNode* EntryTarget(const UEdGraphPin* A, const UEdGraphPin* B)
	{
		const UEdGraphPin* Source = SourceOf(A, B);
		const UEdGraphPin* Target = Source == A ? B : A;
		const bool bFromEntry = Source && Cast<UCodeAnimWebGraphEntryNode>(Source->GetOwningNode());
		return bFromEntry && Target ? Cast<UCodeAnimWebGraphStateNode>(Target->GetOwningNode()) : nullptr;
	}
}

const FPinConnectionResponse UCodeAnimWebGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	if (const UCodeAnimWebGraphStateNode* Target = CodeAnimWebGraph::EntryTarget(A, B))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE,
			FText::Format(LOCTEXT("StartIn", "Start in {0}"), Target->DisplayName));
	}

	const UCodeAnimWebGraphStateNode* From = nullptr;
	const UCodeAnimWebGraphStateNode* To = nullptr;
	if (!CodeAnimWebGraph::StatesFor(A, B, From, To))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("StatesOnly", "Transitions go from one state to another"));
	}
	if (From == To)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("SameState", "A state is already where it is"));
	}

	const UCodeAnimWebEdGraph* Graph = Cast<UCodeAnimWebEdGraph>(From->GetGraph());
	const UCodeAnimationWeb* Defaults = Graph ? Graph->GetDefaults() : nullptr;
	if (Defaults)
	{
		for (const FCodeAnimWebTransition& T : Defaults->Transitions)
		{
			const bool bSame = T.From.Key == From->StateKey && T.To.Key == To->StateKey;
			const bool bCovered = T.bTwoWay && T.From.Key == To->StateKey && T.To.Key == From->StateKey;
			if (bSame || bCovered)
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("Exists", "That transition already exists"));
			}
		}
	}
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE,
		FText::Format(LOCTEXT("Add", "Add {0} → {1}"), From->DisplayName, To->DisplayName));
}

bool UCodeAnimWebGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
	if (const UCodeAnimWebGraphStateNode* Target = CodeAnimWebGraph::EntryTarget(A, B))
	{
		CodeAnimWebGraphEdits::SetInitialState(Cast<UCodeAnimWebEdGraph>(Target->GetGraph()), Target->StateKey);
		return true;
	}
	if (CanCreateConnection(A, B).Response == CONNECT_RESPONSE_DISALLOW)
	{
		return false;
	}
	const UCodeAnimWebGraphStateNode* From = nullptr;
	const UCodeAnimWebGraphStateNode* To = nullptr;
	CodeAnimWebGraph::StatesFor(A, B, From, To);
	CodeAnimWebGraphEdits::AddTransition(Cast<UCodeAnimWebEdGraph>(From->GetGraph()), From->StateKey, To->StateKey);
	return true;
}

bool UCodeAnimWebGraphSchema::SupportsDropPinOnNode(UEdGraphNode* InTargetNode, const FEdGraphPinType& InSourcePinType,
	EEdGraphPinDirection InSourcePinDirection, FText& OutErrorMessage) const
{
	// Asked while an arrow hovers the middle of a node, for the message under the cursor. Answered as
	// hovering the edge is: the same rules, the same "Add Idle -> Blocked", or the same refusal.
	const UCodeAnimWebGraphStateNode* State = Cast<UCodeAnimWebGraphStateNode>(InTargetNode);
	UEdGraphPin* Source = CodeAnimWebGraphEdits::GetDragSourcePin();
	if (!State || !Source || InSourcePinDirection != EGPD_Output)
	{
		return false;
	}
	const FPinConnectionResponse Response = CanCreateConnection(Source, State->GetInputPin());
	OutErrorMessage = Response.Message;
	return Response.Response != CONNECT_RESPONSE_DISALLOW;
}

UEdGraphPin* UCodeAnimWebGraphSchema::DropPinOnNode(UEdGraphNode* InTargetNode, const FName& InSourcePinName,
	const FEdGraphPinType& InSourcePinType, EEdGraphPinDirection InSourcePinDirection) const
{
	const UCodeAnimWebGraphStateNode* State = Cast<UCodeAnimWebGraphStateNode>(InTargetNode);
	return State ? State->GetInputPin() : nullptr;
}

FLinearColor UCodeAnimWebGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return FLinearColor::White;
}

FConnectionDrawingPolicy* UCodeAnimWebGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID,
	float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	return new FCodeAnimWebConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements);
}

void UCodeAnimWebGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	UEdGraphNode* Node = Context ? const_cast<UEdGraphNode*>(Context->Node.Get()) : nullptr;
	UCodeAnimWebEdGraph* Graph = Node ? Cast<UCodeAnimWebEdGraph>(Node->GetGraph()) : nullptr;
	if (!Graph)
	{
		return;
	}

	FToolMenuSection& Section = Menu->AddSection(TEXT("CodeAnimWeb"), LOCTEXT("Section", "Code Animation Web"));
	if (UCodeAnimWebGraphStateNode* State = Cast<UCodeAnimWebGraphStateNode>(Node))
	{
		Section.AddMenuEntry(TEXT("OpenStateGraph"),
			State->bHasGraph ? LOCTEXT("OpenStateGraph", "Open State Graph") : LOCTEXT("AddStateGraph", "Add State Graph"),
			LOCTEXT("StateGraphTip", "A graph run every frame this state is in play, for motion like a wiggle."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([State]() { CodeAnimWebGraphEdits::OpenGraphFor(State); })));
		Section.AddMenuEntry(TEXT("SetInitial"), LOCTEXT("SetInitial", "Set as Initial State"),
			LOCTEXT("SetInitialTip", "The state the Web starts in."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Graph, Key = State->StateKey]() { CodeAnimWebGraphEdits::SetInitialState(Graph, Key); })));
	}
	else if (UCodeAnimWebGraphTransitionNode* Transition = Cast<UCodeAnimWebGraphTransitionNode>(Node))
	{
		const int32 Index = Transition->TransitionIndex;
		Section.AddMenuEntry(TEXT("OpenTransitionGraph"),
			Transition->bHasGraph ? LOCTEXT("OpenTransitionGraph", "Open Transition Graph") : LOCTEXT("AddTransitionGraph", "Add Transition Graph"),
			LOCTEXT("TransitionGraphTip", "A graph run every frame of this transition, after the automatic blend."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Transition]() { CodeAnimWebGraphEdits::OpenGraphFor(Transition); })));
		Section.AddMenuEntry(TEXT("TwoWay"),
			Transition->bTwoWay ? LOCTEXT("MakeOneWay", "Make One-Way") : LOCTEXT("MakeTwoWay", "Make Two-Way"),
			LOCTEXT("TwoWayTip", "A two-way transition also covers the way back, played in reverse."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Graph, Index, bTwoWay = Transition->bTwoWay]() { CodeAnimWebGraphEdits::SetTwoWay(Graph, Index, !bTwoWay); })));
		Section.AddMenuEntry(TEXT("Reverse"), LOCTEXT("Reverse", "Reverse Direction"),
			LOCTEXT("ReverseTip", "Swap From and To."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Graph, Index]() { CodeAnimWebGraphEdits::Reverse(Graph, Index); })));
		Section.AddMenuEntry(TEXT("Delete"), LOCTEXT("Delete", "Delete Transition"),
			LOCTEXT("DeleteTip", "The pair falls back to the Default Transition. Its graph, if it has one, is deleted with it."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Graph, Index]() { CodeAnimWebGraphEdits::RemoveTransitions(Graph, { Index }); })));
	}
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

namespace CodeAnimWebGraphEdits
{
	/** Runs Edit on the Web's defaults inside an undoable transaction, then has the graph rebuilt. */
	template <typename TEdit>
	static void Apply(UCodeAnimWebEdGraph* Graph, const FText& Description, TEdit&& Edit)
	{
		UCodeAnimationWeb* Defaults = Graph ? Graph->GetDefaults() : nullptr;
		if (!Defaults)
		{
			return;
		}
		{
			const FScopedTransaction Transaction(Description);
			Defaults->Modify();
			Edit(*Defaults);
			// The graphs follow in the same transaction, so one undo puts everything back: a deleted
			// transition's graph goes, a reversed or redirected one is renamed to match.
			UBlueprint* Blueprint = Defaults->GetWebBlueprint();
			CodeAnimWebGraphs::RemoveUnusedGraphs(Blueprint);
			CodeAnimWebGraphs::RenameGraphsToMatch(Blueprint);
		}
		Graph->OnDataChanged.Broadcast();
	}
}

void CodeAnimWebGraphEdits::AddTransition(UCodeAnimWebEdGraph* Graph, FName From, FName To)
{
	Apply(Graph, LOCTEXT("AddTransition", "Add Transition"), [From, To](UCodeAnimationWeb& Web)
	{
		FCodeAnimWebTransition& Transition = Web.Transitions.AddDefaulted_GetRef();
		Transition.From.Key = From;
		Transition.To.Key = To;
		// Starts as whatever the pair did before it had its own row.
		Transition.Timing = Web.DefaultTransition;
	});
}

void CodeAnimWebGraphEdits::RemoveTransitions(UCodeAnimWebEdGraph* Graph, TArray<int32> Indices)
{
	Indices.Sort([](int32 A, int32 B) { return A > B; });
	Apply(Graph, LOCTEXT("RemoveTransitions", "Delete Transition"), [&Indices](UCodeAnimationWeb& Web)
	{
		for (const int32 Index : Indices)
		{
			if (Web.Transitions.IsValidIndex(Index))
			{
				Web.Transitions.RemoveAt(Index);
			}
		}
	});
}

void CodeAnimWebGraphEdits::SetTwoWay(UCodeAnimWebEdGraph* Graph, int32 Index, bool bTwoWay)
{
	Apply(Graph, LOCTEXT("SetTwoWay", "Change Transition Direction"), [Index, bTwoWay](UCodeAnimationWeb& Web)
	{
		if (Web.Transitions.IsValidIndex(Index))
		{
			Web.Transitions[Index].bTwoWay = bTwoWay;
		}
	});
}

void CodeAnimWebGraphEdits::Reverse(UCodeAnimWebEdGraph* Graph, int32 Index)
{
	Apply(Graph, LOCTEXT("Reverse", "Reverse Transition"), [Index](UCodeAnimationWeb& Web)
	{
		if (Web.Transitions.IsValidIndex(Index))
		{
			Swap(Web.Transitions[Index].From, Web.Transitions[Index].To);
		}
	});
}

void CodeAnimWebGraphEdits::SetInitialState(UCodeAnimWebEdGraph* Graph, FName Key)
{
	Apply(Graph, LOCTEXT("SetInitial", "Set Initial State"), [Key](UCodeAnimationWeb& Web)
	{
		Web.InitialState.Key = Key;
	});
}

void CodeAnimWebGraphEdits::SetStatePosition(UCodeAnimWebEdGraph* Graph, FName Key, const FVector2D& Position)
{
	UCodeAnimationWeb* Defaults = Graph ? Graph->GetDefaults() : nullptr;
	FCodeAnimWebStateEntry* Entry = Defaults
		? Defaults->States.FindByPredicate([Key](const FCodeAnimWebStateEntry& E) { return E.Key == Key; })
		: nullptr;
	if (Entry && (!Entry->bHasEditorPosition || !Entry->EditorPosition.Equals(Position)))
	{
		Defaults->Modify();
		Entry->EditorPosition = Position;
		Entry->bHasEditorPosition = true;
	}
}

void CodeAnimWebGraphEdits::SetEntryPosition(UCodeAnimWebEdGraph* Graph, const FVector2D& Position)
{
	UCodeAnimationWeb* Defaults = Graph ? Graph->GetDefaults() : nullptr;
	if (Defaults && (!Defaults->bHasWebGraphEntryPosition || !Defaults->WebGraphEntryPosition.Equals(Position)))
	{
		Defaults->Modify();
		Defaults->WebGraphEntryPosition = Position;
		Defaults->bHasWebGraphEntryPosition = true;
	}
}

namespace CodeAnimWebGraphEdits
{
	static TWeakObjectPtr<UEdGraphNode> DragSource;
}

void CodeAnimWebGraphEdits::SetDragSource(UEdGraphNode* Node)
{
	DragSource = Node;
}

UEdGraphPin* CodeAnimWebGraphEdits::GetDragSourcePin()
{
	UEdGraphNode* Source = DragSource.Get();
	if (!Source)
	{
		return nullptr;
	}
	for (UEdGraphPin* Pin : Source->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output)
		{
			return Pin;
		}
	}
	return nullptr;
}

void CodeAnimWebGraphEdits::OpenGraphFor(UEdGraphNode* Node)
{
	UCodeAnimWebEdGraph* Graph = Node ? Cast<UCodeAnimWebEdGraph>(Node->GetGraph()) : nullptr;
	UCodeAnimationWeb* Defaults = Graph ? Graph->GetDefaults() : nullptr;
	if (!Defaults)
	{
		return;
	}
	bool bCreated = false;
	if (const UCodeAnimWebGraphStateNode* State = Cast<UCodeAnimWebGraphStateNode>(Node))
	{
		bCreated = !State->bHasGraph;
		CodeAnimWebGraphs::OpenOrCreateStateGraph(Defaults, State->StateKey);
	}
	else if (const UCodeAnimWebGraphTransitionNode* Transition = Cast<UCodeAnimWebGraphTransitionNode>(Node))
	{
		bCreated = !Transition->bHasGraph;
		CodeAnimWebGraphs::OpenOrCreateTransitionGraph(Defaults, Transition->TransitionIndex);
	}
	if (bCreated)
	{
		Graph->OnDataChanged.Broadcast();
	}
}

#undef LOCTEXT_NAMESPACE
