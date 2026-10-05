#include "WebGraph/SCodeAnimWebGraphEditor.h"

#include "BlueprintEditor.h"
#include "CodeAnimationWeb.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Engine/Blueprint.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "GraphEditor.h"
#include "IDetailPropertyRow.h"
#include "IDetailsView.h"
#include "SKismetInspector.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SCodeAnimWebGraphEditor"

// ---------------------------------------------------------------------------
// Details of the selection
// ---------------------------------------------------------------------------

void FCodeAnimWebGraphSelectionDetails::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	Builder.GetObjectsBeingCustomized(Objects);
	const UCodeAnimWebGraphSelection* Selection = Objects.Num() == 1 ? Cast<UCodeAnimWebGraphSelection>(Objects[0].Get()) : nullptr;
	UCodeAnimationWeb* Web = Selection ? Selection->GetDefaults() : nullptr;
	if (!Web)
	{
		return;
	}
	const TArray<UObject*> WebObjects{ Web };

	// The rows are the Web's own properties, added as external objects: editing them is editing Class
	// Defaults, customizations, undo and all. For one element of an array, the array is added hidden
	// and just that element shown.
	auto AddElement = [&Builder, &WebObjects](FName Category, const FText& Label, FName ArrayName, int32 Index)
	{
		IDetailCategoryBuilder& Section = Builder.EditCategory(Category, Label, ECategoryPriority::Important);
		IDetailPropertyRow* ArrayRow = Section.AddExternalObjectProperty(WebObjects, ArrayName);
		const TSharedPtr<IPropertyHandle> Array = ArrayRow ? ArrayRow->GetPropertyHandle() : nullptr;
		const TSharedPtr<IPropertyHandle> Element = Array.IsValid() ? Array->GetChildHandle(Index) : nullptr;
		if (!Element.IsValid())
		{
			return;
		}
		ArrayRow->Visibility(EVisibility::Collapsed);
		Section.AddProperty(Element.ToSharedRef()).ShouldAutoExpand(true);
	};

	switch (Selection->Kind)
	{
	case UCodeAnimWebGraphSelection::EKind::State:
	{
		const FName Key = Selection->StateKey;
		const int32 Index = Web->States.IndexOfByPredicate([Key](const FCodeAnimWebStateEntry& E) { return E.Key == Key; });
		if (Index != INDEX_NONE)
		{
			AddElement(TEXT("WebGraphState"), LOCTEXT("StateCategory", "State"), GET_MEMBER_NAME_CHECKED(UCodeAnimationWeb, States), Index);
		}
		break;
	}
	case UCodeAnimWebGraphSelection::EKind::Transition:
		if (Web->Transitions.IsValidIndex(Selection->TransitionIndex))
		{
			AddElement(TEXT("WebGraphTransition"), LOCTEXT("TransitionCategory", "Transition"),
				GET_MEMBER_NAME_CHECKED(UCodeAnimationWeb, Transitions), Selection->TransitionIndex);
		}
		break;
	default:
	{
		IDetailCategoryBuilder& Section = Builder.EditCategory(TEXT("WebGraphWeb"), LOCTEXT("WebCategory", "Code Animation Web"), ECategoryPriority::Important);
		for (const FName Name : {
			GET_MEMBER_NAME_CHECKED(UCodeAnimationWeb, StateEnum),
			GET_MEMBER_NAME_CHECKED(UCodeAnimationWeb, InitialState),
			GET_MEMBER_NAME_CHECKED(UCodeAnimationWeb, DefaultTransition),
			GET_MEMBER_NAME_CHECKED(UCodeAnimationWeb, InterruptTransition),
			GET_MEMBER_NAME_CHECKED(UCodeAnimationWeb, MaxBlendDepth),
			GET_MEMBER_NAME_CHECKED(UCodeAnimationWeb, bTransitionTraversalOnly),
			GET_MEMBER_NAME_CHECKED(UCodeAnimationWeb, PathCost) })
		{
			Section.AddExternalObjectProperty(WebObjects, Name);
		}
		break;
	}
	}
}

// ---------------------------------------------------------------------------
// The tab
// ---------------------------------------------------------------------------

void SCodeAnimWebGraphEditor::Construct(const FArguments& InArgs, UBlueprint* InBlueprint, TSharedPtr<FBlueprintEditor> InEditor)
{
	Blueprint = InBlueprint;
	Editor = InEditor;

	UCodeAnimWebEdGraph* NewGraph = NewObject<UCodeAnimWebEdGraph>(GetTransientPackage(), NAME_None, RF_Transient);
	NewGraph->Schema = UCodeAnimWebGraphSchema::StaticClass();
	NewGraph->Blueprint = InBlueprint;
	NewGraph->Rebuild();
	Graph.Reset(NewGraph);
	DataChangedHandle = NewGraph->OnDataChanged.AddSP(this, &SCodeAnimWebGraphEditor::RequestRebuild, true);

	UCodeAnimWebGraphSelection* NewSelection = NewObject<UCodeAnimWebGraphSelection>(GetTransientPackage(), NAME_None, RF_Transient);
	NewSelection->Blueprint = InBlueprint;
	Selection.Reset(NewSelection);

	Commands = MakeShared<FUICommandList>();
	Commands->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SCodeAnimWebGraphEditor::DeleteSelected),
		FCanExecuteAction::CreateSP(this, &SCodeAnimWebGraphEditor::CanDeleteSelected));
	Commands->MapAction(FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateLambda([this]() { GraphEditor->SelectAllNodes(); }));

	SGraphEditor::FGraphEditorEvents Events;
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &SCodeAnimWebGraphEditor::OnSelectionChanged);
	Events.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &SCodeAnimWebGraphEditor::OnNodeDoubleClicked);

	FGraphAppearanceInfo Appearance;
	Appearance.CornerText = LOCTEXT("CornerText", "WEB GRAPH");

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(GraphEditor, SGraphEditor)
			.GraphToEdit(NewGraph)
			.AdditionalCommands(Commands)
			.GraphEvents(Events)
			.Appearance(Appearance)
			.ShowGraphStateOverlay(false)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Visibility(this, &SCodeAnimWebGraphEditor::GetEmptyHintVisibility)
			.Text(LOCTEXT("EmptyHint", "Click the empty graph, then pick the Web's States enum in the Details panel."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
	];

	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SCodeAnimWebGraphEditor::OnObjectPropertyChanged);
	if (InBlueprint)
	{
		CompiledHandle = InBlueprint->OnCompiled().AddSP(this, &SCodeAnimWebGraphEditor::OnBlueprintCompiled);
	}
	ShowSelection(true);
}

SCodeAnimWebGraphEditor::~SCodeAnimWebGraphEditor()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	if (UBlueprint* BP = Blueprint.Get())
	{
		BP->OnCompiled().Remove(CompiledHandle);
	}
	if (Graph.IsValid())
	{
		Graph->OnDataChanged.Remove(DataChangedHandle);
	}
}

EVisibility SCodeAnimWebGraphEditor::GetEmptyHintVisibility() const
{
	const UCodeAnimationWeb* Defaults = Graph.IsValid() ? Graph->GetDefaults() : nullptr;
	return Defaults && Defaults->StateEnum ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
}

bool SCodeAnimWebGraphEditor::IsShowingSelection() const
{
	const TSharedPtr<FBlueprintEditor> BlueprintEditor = Editor.Pin();
	if (!BlueprintEditor.IsValid() || !Selection.IsValid())
	{
		return false;
	}
	const TSharedPtr<IDetailsView> View = BlueprintEditor->GetInspector()->GetPropertyView();
	return View.IsValid() && View->GetSelectedObjects().Num() == 1 && View->GetSelectedObjects()[0].Get() == Selection.Get();
}

void SCodeAnimWebGraphEditor::ShowSelection(bool bForceRefresh)
{
	const TSharedPtr<FBlueprintEditor> BlueprintEditor = Editor.Pin();
	if (!BlueprintEditor.IsValid())
	{
		return;
	}

	FText Title = LOCTEXT("WebTitle", "Code Animation Web");
	if (Selection->Kind == UCodeAnimWebGraphSelection::EKind::State)
	{
		if (const UCodeAnimWebGraphStateNode* State = Graph->FindState(Selection->StateKey))
		{
			Title = FText::Format(LOCTEXT("StateTitle", "{0} (state)"), State->DisplayName);
		}
	}
	else if (Selection->Kind == UCodeAnimWebGraphSelection::EKind::Transition)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UCodeAnimWebGraphTransitionNode* Transition = Cast<UCodeAnimWebGraphTransitionNode>(Node);
			if (Transition && Transition->TransitionIndex == Selection->TransitionIndex)
			{
				Title = Transition->GetNodeTitle(ENodeTitleType::FullTitle);
			}
		}
	}

	SKismetInspector::FShowDetailsOptions Options(Title, bForceRefresh);
	BlueprintEditor->GetInspector()->ShowDetailsForSingleObject(Selection.Get(), Options);
}

void SCodeAnimWebGraphEditor::RequestRebuild(bool bRefreshDetails)
{
	bRefreshDetailsPending |= bRefreshDetails;
	if (!bRebuildPending)
	{
		// Next frame: never rebuild the graph from inside one of its own callbacks (a drag, a menu).
		bRebuildPending = true;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SCodeAnimWebGraphEditor::DoRebuild));
	}
}

EActiveTimerReturnType SCodeAnimWebGraphEditor::DoRebuild(double InCurrentTime, float InDeltaTime)
{
	bRebuildPending = false;
	Graph->Rebuild();
	if (GraphEditor.IsValid())
	{
		GraphEditor->NotifyGraphChanged();
		RestoreSelection();
	}
	// Only if the Details panel is still ours: never take it back from something the user moved on to.
	if (bRefreshDetailsPending && IsShowingSelection())
	{
		ShowSelection(true);
	}
	bRefreshDetailsPending = false;
	return EActiveTimerReturnType::Stop;
}

void SCodeAnimWebGraphEditor::RestoreSelection()
{
	// The rebuilt graph has new node objects: reselect by what they stand for.
	TGuardValue<bool> Guard(bRestoringSelection, true);
	GraphEditor->ClearSelectionSet();
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		const UCodeAnimWebGraphStateNode* State = Cast<UCodeAnimWebGraphStateNode>(Node);
		const UCodeAnimWebGraphTransitionNode* Transition = Cast<UCodeAnimWebGraphTransitionNode>(Node);
		const bool bSelected =
			(State && Selection->Kind == UCodeAnimWebGraphSelection::EKind::State && State->StateKey == Selection->StateKey)
			|| (Transition && Selection->Kind == UCodeAnimWebGraphSelection::EKind::Transition && Transition->TransitionIndex == Selection->TransitionIndex);
		if (bSelected)
		{
			GraphEditor->SetNodeSelection(Node, true);
		}
	}
}

void SCodeAnimWebGraphEditor::OnSelectionChanged(const TSet<UObject*>& NewSelection)
{
	if (bRestoringSelection)
	{
		return;
	}

	UCodeAnimWebGraphSelection::EKind Kind = UCodeAnimWebGraphSelection::EKind::Web;
	FName Key;
	int32 Index = INDEX_NONE;
	if (NewSelection.Num() == 1)
	{
		UObject* Selected = *NewSelection.CreateConstIterator();
		if (const UCodeAnimWebGraphStateNode* State = Cast<UCodeAnimWebGraphStateNode>(Selected))
		{
			Kind = UCodeAnimWebGraphSelection::EKind::State;
			Key = State->StateKey;
		}
		else if (const UCodeAnimWebGraphTransitionNode* Transition = Cast<UCodeAnimWebGraphTransitionNode>(Selected))
		{
			Kind = UCodeAnimWebGraphSelection::EKind::Transition;
			Index = Transition->TransitionIndex;
		}
	}

	Selection->Kind = Kind;
	Selection->StateKey = Key;
	Selection->TransitionIndex = Index;
	ShowSelection(true);
}

void SCodeAnimWebGraphEditor::OnNodeDoubleClicked(UEdGraphNode* Node)
{
	CodeAnimWebGraphEdits::OpenGraphFor(Node);
}

bool SCodeAnimWebGraphEditor::CanDeleteSelected() const
{
	for (UObject* Selected : GraphEditor->GetSelectedNodes())
	{
		if (Cast<UCodeAnimWebGraphTransitionNode>(Selected))
		{
			return true;
		}
	}
	return false;
}

void SCodeAnimWebGraphEditor::DeleteSelected()
{
	// States come from the enum, so only transitions can go from here.
	TArray<int32> Indices;
	for (UObject* Selected : GraphEditor->GetSelectedNodes())
	{
		if (const UCodeAnimWebGraphTransitionNode* Transition = Cast<UCodeAnimWebGraphTransitionNode>(Selected))
		{
			Indices.Add(Transition->TransitionIndex);
		}
	}
	Selection->Kind = UCodeAnimWebGraphSelection::EKind::Web;
	Selection->TransitionIndex = INDEX_NONE;
	CodeAnimWebGraphEdits::RemoveTransitions(Graph.Get(), Indices);
}

void SCodeAnimWebGraphEditor::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	// An edit from a Details panel: the picture follows. The panel refreshes itself, so it is left alone -
	// rebuilding it mid-edit would drop the user's focus.
	if (Graph.IsValid() && Object && Object == Graph->GetDefaults())
	{
		RequestRebuild(false);
	}
}

void SCodeAnimWebGraphEditor::OnBlueprintCompiled(UBlueprint* Compiled)
{
	// A compile replaces the class defaults the Details rows point at.
	RequestRebuild(true);
}

#undef LOCTEXT_NAMESPACE
