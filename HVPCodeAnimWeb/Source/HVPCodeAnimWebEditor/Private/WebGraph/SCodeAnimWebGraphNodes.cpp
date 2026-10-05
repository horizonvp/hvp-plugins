#include "WebGraph/SCodeAnimWebGraphNodes.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditorDragDropAction.h"
#include "SGraphPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "WebGraph/CodeAnimWebGraph.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SCodeAnimWebGraphNodes"

namespace CodeAnimWebGraphWidgets
{
	/** A state's whole edge: grab it to drag out a new transition. */
	class SStateEdgePin : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(SStateEdgePin) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
		{
			SetCursor(EMouseCursor::Default);
			bShowLabel = true;
			GraphPinObj = InPin;
			SBorder::Construct(SBorder::FArguments()
				.BorderImage(this, &SStateEdgePin::GetEdgeBrush)
				.BorderBackgroundColor(this, &SStateEdgePin::GetPinColor)
				.OnMouseButtonDown(this, &SStateEdgePin::OnEdgeMouseDown)
				.Cursor(this, &SStateEdgePin::GetPinCursor));
		}

		virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
		{
			// Let go on a state's edge: the state takes it, exactly as if it were dropped in the middle.
			const TSharedPtr<SGraphNode> Owner = OwnerNodePtr.Pin();
			if (Owner.IsValid() && Cast<UCodeAnimWebGraphStateNode>(Owner->GetNodeObj()))
			{
				return StaticCastSharedPtr<SCodeAnimWebStateNode>(Owner)->HandleConnectionDrop();
			}
			return SGraphPin::OnDrop(MyGeometry, DragDropEvent);
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override { return SNew(STextBlock); }

	private:
		FReply OnEdgeMouseDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
		{
			CodeAnimWebGraphEdits::SetDragSource(GraphPinObj ? GraphPinObj->GetOwningNode() : nullptr);
			return OnPinMouseDown(MyGeometry, MouseEvent);
		}

		const FSlateBrush* GetEdgeBrush() const
		{
			// The edge lights up as the place to start a drag - not while one is being dropped, when it is
			// the whole state that lights up instead.
			return IsHovered() && !FSlateApplication::Get().IsDragDropping()
				? FAppStyle::GetBrush(TEXT("Graph.AnimStateNode.Pin.BackgroundHovered"))
				: FAppStyle::GetBrush(TEXT("Graph.AnimStateNode.Pin.Background"));
		}
	};

	/** Where transitions arrive. Invisible: a drag lands on the whole state instead (see the schema). */
	class SStateInputPin : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(SStateInputPin) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
		{
			bShowLabel = false;
			GraphPinObj = InPin;
			SetVisibility(EVisibility::HitTestInvisible);
			SBorder::Construct(SBorder::FArguments().BorderImage(nullptr));
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override { return SNullWidget::NullWidget; }
	};

	/** The mark for "has its own graph", on states and transitions alike. */
	static TSharedRef<SWidget> GraphIcon(const FText& Tooltip, float Size)
	{
		return SNew(SBox)
			.WidthOverride(Size)
			.HeightOverride(Size)
			.ToolTipText(Tooltip)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(TEXT("Kismet.AllClasses.FunctionIcon")))
				.ColorAndOpacity(FStyleColors::AccentBlue)
			];
	}

	/** The outline a state shows while an arrow that can land on it is dragged over it. */
	static const FSlateBrush* DropTargetBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent, 10.0f, FStyleColors::AccentOrange.GetSpecifiedColor(), 2.0f);
		return &Brush;
	}

	static EVisibility SelectionVisibility(const TWeakPtr<SGraphPanel>& Panel, const UEdGraphNode* Node)
	{
		const TSharedPtr<SGraphPanel> Owner = Panel.Pin();
		return Owner.IsValid() && Owner->SelectionManager.IsNodeSelected(const_cast<UEdGraphNode*>(Node))
			? EVisibility::HitTestInvisible
			: EVisibility::Hidden;
	}
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void SCodeAnimWebStateNode::Construct(const FArguments& InArgs, UCodeAnimWebGraphStateNode* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SCodeAnimWebStateNode::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	const UCodeAnimWebGraphStateNode* State = CastChecked<UCodeAnimWebGraphStateNode>(GraphNode);

	const FText Detail = State->NumSet == 0
		? LOCTEXT("AllDefault", "all outputs at default")
		: FText::Format(LOCTEXT("SomeSet", "sets {0} of {1}"), State->NumSet, State->NumOutputs);
	const FLinearColor Body(0.08f, 0.08f, 0.08f);

	SetVisibility(EVisibility::SelfHitTestInvisible);
	GetOrAddSlot(ENodeZone::Center)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		.Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.AnimStateNode.Body")))
			.BorderBackgroundColor(Body)
			.Padding(0.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(PinOverlay, SOverlay)
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(12.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush(TEXT("Graph.AnimStateNode.ColorSpill")))
					.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f))
					.Padding(FMargin(10.0f, 4.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								State->bHasGraph
									? CodeAnimWebGraphWidgets::GraphIcon(LOCTEXT("StateGraphIcon", "Has a state graph - double-click to open it."), 14.0f)
									: SNullWidget::NullWidget
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(STextBlock)
								.TextStyle(FAppStyle::Get(), TEXT("Graph.StateNode.NodeTitle"))
								.Text(State->DisplayName)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(Detail)
							.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
				]
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.AnimStateNode.Selection")))
			.Padding(0.0f)
			.Visibility_Lambda([this]() { return CodeAnimWebGraphWidgets::SelectionVisibility(OwnerGraphPanelPtr, GraphNode); })
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(CodeAnimWebGraphWidgets::DropTargetBrush())
			.Padding(0.0f)
			.Visibility_Lambda([this]() { return bDropTarget ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
		]
	];

	CreatePinWidgets();
}

void SCodeAnimWebStateNode::CreatePinWidgets()
{
	UCodeAnimWebGraphStateNode* State = CastChecked<UCodeAnimWebGraphStateNode>(GraphNode);
	AddPin(SNew(CodeAnimWebGraphWidgets::SStateEdgePin, State->GetOutputPin()));
	AddPin(SNew(CodeAnimWebGraphWidgets::SStateInputPin, State->GetInputPin()));
}

void SCodeAnimWebStateNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	PinToAdd->SetOwner(SharedThis(this));
	PinOverlay->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		PinToAdd
	];
	(PinToAdd->GetPinObj()->Direction == EGPD_Input ? InputPins : OutputPins).Add(PinToAdd);
}

void SCodeAnimWebStateNode::MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);

	// Where states sit is the one thing the Web Graph saves; the panel's Move transaction covers it.
	UCodeAnimWebGraphStateNode* State = CastChecked<UCodeAnimWebGraphStateNode>(GraphNode);
	CodeAnimWebGraphEdits::SetStatePosition(Cast<UCodeAnimWebEdGraph>(State->GetGraph()), State->StateKey,
		FVector2D(State->NodePosX, State->NodePosY));
}

bool SCodeAnimWebStateNode::CanAcceptDrop() const
{
	const UCodeAnimWebGraphStateNode* State = CastChecked<UCodeAnimWebGraphStateNode>(GraphNode);
	UEdGraphPin* SourcePin = CodeAnimWebGraphEdits::GetDragSourcePin();
	return SourcePin && SourcePin->GetOwningNode() != State
		&& State->GetSchema()->CanCreateConnection(SourcePin, State->GetInputPin()).Response != CONNECT_RESPONSE_DISALLOW;
}

void SCodeAnimWebStateNode::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SGraphNode::OnDragEnter(MyGeometry, DragDropEvent);
	const TSharedPtr<FGraphEditorDragDropAction> Operation = DragDropEvent.GetOperationAs<FGraphEditorDragDropAction>();
	bDropTarget = Operation.IsValid() && CanAcceptDrop();
}

void SCodeAnimWebStateNode::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SGraphNode::OnDragLeave(DragDropEvent);
	bDropTarget = false;
}

FReply SCodeAnimWebStateNode::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (DragDropEvent.GetOperationAs<FGraphEditorDragDropAction>().IsValid())
	{
		return HandleConnectionDrop();
	}
	return SGraphNode::OnDrop(MyGeometry, DragDropEvent);
}

FReply SCodeAnimWebStateNode::HandleConnectionDrop()
{
	bDropTarget = false;
	UCodeAnimWebGraphStateNode* State = CastChecked<UCodeAnimWebGraphStateNode>(GraphNode);
	UEdGraphPin* SourcePin = CodeAnimWebGraphEdits::GetDragSourcePin();
	CodeAnimWebGraphEdits::SetDragSource(nullptr);
	if (SourcePin && SourcePin->GetOwningNode() != State)
	{
		// The schema turns this into a transition (or, from Entry, the initial state), refusing what it must.
		State->GetSchema()->TryCreateConnection(SourcePin, State->GetInputPin());
	}
	// Handled either way: an arrow let go on a state never means "open the add-node menu".
	return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------

void SCodeAnimWebEntryNode::Construct(const FArguments& InArgs, UCodeAnimWebGraphEntryNode* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SCodeAnimWebEntryNode::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	SetVisibility(EVisibility::SelfHitTestInvisible);
	GetOrAddSlot(ENodeZone::Center)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		.Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.AnimStateNode.Body")))
			.BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f))
			.Padding(0.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(PinOverlay, SOverlay)
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(10.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Entry", "Entry"))
					.TextStyle(FAppStyle::Get(), TEXT("Graph.StateNode.NodeTitle"))
				]
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.AnimStateNode.Selection")))
			.Padding(0.0f)
			.Visibility_Lambda([this]() { return CodeAnimWebGraphWidgets::SelectionVisibility(OwnerGraphPanelPtr, GraphNode); })
		]
	];

	CreatePinWidgets();
}

void SCodeAnimWebEntryNode::CreatePinWidgets()
{
	UCodeAnimWebGraphEntryNode* Entry = CastChecked<UCodeAnimWebGraphEntryNode>(GraphNode);
	AddPin(SNew(CodeAnimWebGraphWidgets::SStateEdgePin, Entry->GetOutputPin()));
}

void SCodeAnimWebEntryNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	PinToAdd->SetOwner(SharedThis(this));
	PinOverlay->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		PinToAdd
	];
	OutputPins.Add(PinToAdd);
}

void SCodeAnimWebEntryNode::MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);
	CodeAnimWebGraphEdits::SetEntryPosition(Cast<UCodeAnimWebEdGraph>(GraphNode->GetGraph()),
		FVector2D(GraphNode->NodePosX, GraphNode->NodePosY));
}

// ---------------------------------------------------------------------------
// Transition
// ---------------------------------------------------------------------------

void SCodeAnimWebTransitionNode::Construct(const FArguments& InArgs, UCodeAnimWebGraphTransitionNode* InNode)
{
	GraphNode = InNode;
	UpdateGraphNode();
}

FSlateColor SCodeAnimWebTransitionNode::GetColor() const
{
	const UCodeAnimWebGraphTransitionNode* Transition = CastChecked<UCodeAnimWebGraphTransitionNode>(GraphNode);
	if (IsHovered())
	{
		return FStyleColors::AccentOrange;
	}
	// A transition with its own graph is marked out, as a state with one is.
	return Transition->bHasGraph ? FStyleColors::AccentBlue : FStyleColors::Foreground;
}

void SCodeAnimWebTransitionNode::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	const UCodeAnimWebGraphTransitionNode* Transition = CastChecked<UCodeAnimWebGraphTransitionNode>(GraphNode);

	const FSlateBrush* IconBrush = FAppStyle::GetBrush(TEXT("Graph.AnimTransitionNode.Icon"));
	TSharedRef<SImage> Direction = SNew(SImage)
		.Image(IconBrush)
		.ColorAndOpacity(FStyleColors::Background);
	if (!Transition->bTwoWay)
	{
		Direction->SetRenderTransform(MakeAttributeLambda([this]() -> TOptional<FSlateRenderTransform>
		{
			const UCodeAnimWebGraphTransitionNode* Node = CastChecked<UCodeAnimWebGraphTransitionNode>(GraphNode);
			return FSlateRenderTransform(FQuat2D(FVector2D(Node->CachedDirection)));
		}));
		Direction->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	}

	GetOrAddSlot(ENodeZone::Center)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.Padding(2.0f)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(TEXT("Graph.AnimTransitionNode.ColorSpill")))
			.ColorAndOpacity(this, &SCodeAnimWebTransitionNode::GetColor)
		]
		+ SOverlay::Slot()
		[
			SNew(SBox)
			.Padding(4.0f)
			[
				// One size either way: the two-way glyph sits in a box the size of the one-way icon.
				SNew(SBox)
				.WidthOverride(IconBrush->ImageSize.X)
				.HeightOverride(IconBrush->ImageSize.Y)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					Transition->bTwoWay
						? StaticCastSharedRef<SWidget>(SNew(STextBlock)
							.Text(FText::FromString(TEXT("\u2194")))
							.Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
							.ColorAndOpacity(FStyleColors::Background))
						: StaticCastSharedRef<SWidget>(Direction)
				]
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		[
			// Shifted off the disc (a render transform, so the disc stays centred on its arrow): the icon
			// is the disc's own blue, and on top of it would disappear.
			SNew(SBox)
			.RenderTransform(FSlateRenderTransform(FVector2f(11.0f, -5.0f)))
			[
				Transition->bHasGraph
					? CodeAnimWebGraphWidgets::GraphIcon(LOCTEXT("TransitionGraphIcon", "Has a transition graph - double-click to open it."), 13.0f)
					: SNullWidget::NullWidget
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.AnimTransitionNode.Selection")))
			.Padding(0.0f)
			.Visibility_Lambda([this]() { return CodeAnimWebGraphWidgets::SelectionVisibility(OwnerGraphPanelPtr, GraphNode); })
		]
	];
}

void SCodeAnimWebTransitionNode::PerformSecondPassLayout(const TMap<UObject*, TSharedRef<SNode>>& NodeToWidgetLookup) const
{
	UCodeAnimWebGraphTransitionNode* Transition = CastChecked<UCodeAnimWebGraphTransitionNode>(GraphNode);
	const UCodeAnimWebGraphStateNode* From = Transition->GetFromState();
	const UCodeAnimWebGraphStateNode* To = Transition->GetToState();
	const TSharedRef<SNode>* FromWidget = From ? NodeToWidgetLookup.Find(From) : nullptr;
	const TSharedRef<SNode>* ToWidget = To ? NodeToWidgetLookup.Find(To) : nullptr;
	if (!FromWidget || !ToWidget)
	{
		return;
	}

	const FGeometry StartGeom(FVector2D(From->NodePosX, From->NodePosY), FVector2D::ZeroVector, (*FromWidget)->GetDesiredSize(), 1.0f);
	const FGeometry EndGeom(FVector2D(To->NodePosX, To->NodePosY), FVector2D::ZeroVector, (*ToWidget)->GetDesiredSize(), 1.0f);

	// Halfway along the line between the two states' edges, lifted off it to the side the arrow is
	// drawn on - so a pair of one-way transitions in opposite directions sit on their own arrows.
	const FVector2f SeedPoint = (FGeometryHelper::CenterOf(StartGeom) + FGeometryHelper::CenterOf(EndGeom)) * 0.5f;
	const FVector2f StartAnchor = FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint);
	const FVector2f EndAnchor = FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint);
	FVector2f Delta = EndAnchor - StartAnchor;
	if (Delta.IsNearlyZero())
	{
		Delta = FVector2f(10.0f, 0.0f);
	}
	const FVector2f Normal = FVector2f(Delta.Y, -Delta.X).GetSafeNormal();
	const float Lift = Transition->bTwoWay ? 0.0f : 6.0f;
	const FVector2f Center = StartAnchor + Delta * 0.5f + Normal * Lift;
	const FVector2f Corner = Center - FVector2f(GetDesiredSize()) * 0.5f;

	Transition->NodePosX = FMath::RoundToInt(Corner.X);
	Transition->NodePosY = FMath::RoundToInt(Corner.Y);
	Transition->CachedDirection = Delta.GetSafeNormal();
}

// ---------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------

FCodeAnimWebConnectionDrawingPolicy::FCodeAnimWebConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor,
	const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements)
{
	ArrowImage = FAppStyle::GetBrush(TEXT("Graph.AnimStateNode.ConnectionArrow"));
	ArrowRadius = ArrowImage->ImageSize * ZoomFactor * 0.5f;
}

void FCodeAnimWebConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params)
{
	Params.AssociatedPin1 = OutputPin;
	Params.AssociatedPin2 = InputPin;
	Params.WireThickness = 1.5f;
	Params.WireColor = FStyleColors::Foreground.GetSpecifiedColor();

	if (const UCodeAnimWebGraphTransitionNode* Transition = InputPin ? Cast<UCodeAnimWebGraphTransitionNode>(InputPin->GetOwningNode()) : nullptr)
	{
		Params.bUserFlag1 = Transition->bTwoWay;
		if (Transition->bHasGraph)
		{
			Params.WireColor = FStyleColors::AccentBlue.GetSpecifiedColor();
		}
	}
	if (HoveredPins.Num() > 0 && !Params.bUserFlag2)
	{
		ApplyHoverDeemphasis(OutputPin, InputPin, Params.WireThickness, Params.WireColor);
	}
}

void FCodeAnimWebConnectionDrawingPolicy::Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes)
{
	NodeWidgetMap.Reset();
	for (int32 Index = 0; Index < ArrangedNodes.Num(); ++Index)
	{
		const TSharedRef<SGraphNode> Node = StaticCastSharedRef<SGraphNode>(ArrangedNodes[Index].Widget);
		NodeWidgetMap.Add(Node->GetNodeObj(), Index);
	}
	FConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
}

void FCodeAnimWebConnectionDrawingPolicy::DetermineLinkGeometry(FArrangedChildren& ArrangedNodes, TSharedRef<SWidget>& OutputPinWidget,
	UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FArrangedWidget*& StartWidgetGeometry, FArrangedWidget*& EndWidgetGeometry)
{
	// The Entry's arrow: from the Entry box to the initial state.
	if (Cast<UCodeAnimWebGraphEntryNode>(OutputPin->GetOwningNode()))
	{
		const int32* EntryIndex = NodeWidgetMap.Find(OutputPin->GetOwningNode());
		const int32* StateIndex = NodeWidgetMap.Find(InputPin->GetOwningNode());
		if (EntryIndex && StateIndex)
		{
			StartWidgetGeometry = &ArrangedNodes[*EntryIndex];
			EndWidgetGeometry = &ArrangedNodes[*StateIndex];
		}
		return;
	}

	// Each transition is drawn once, whole, state to state, from its incoming link. Its outgoing link
	// (transition -> state) is left undrawn.
	const UCodeAnimWebGraphTransitionNode* Transition = Cast<UCodeAnimWebGraphTransitionNode>(InputPin->GetOwningNode());
	const UCodeAnimWebGraphStateNode* From = Transition ? Transition->GetFromState() : nullptr;
	const UCodeAnimWebGraphStateNode* To = Transition ? Transition->GetToState() : nullptr;
	const int32* FromIndex = From ? NodeWidgetMap.Find(const_cast<UCodeAnimWebGraphStateNode*>(From)) : nullptr;
	const int32* ToIndex = To ? NodeWidgetMap.Find(const_cast<UCodeAnimWebGraphStateNode*>(To)) : nullptr;
	if (FromIndex && ToIndex)
	{
		StartWidgetGeometry = &ArrangedNodes[*FromIndex];
		EndWidgetGeometry = &ArrangedNodes[*ToIndex];
	}
}

void FCodeAnimWebConnectionDrawingPolicy::DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params)
{
	const FVector2f SeedPoint = (FGeometryHelper::CenterOf(StartGeom) + FGeometryHelper::CenterOf(EndGeom)) * 0.5f;
	DrawSplineWithArrow(FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint),
		FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint), Params);
}

void FCodeAnimWebConnectionDrawingPolicy::DrawSplineWithArrow(const FVector2f& StartPoint, const FVector2f& EndPoint, const FConnectionParams& Params)
{
	DrawLineWithArrow(StartPoint, EndPoint, Params);
	if (Params.bUserFlag1)
	{
		DrawLineWithArrow(EndPoint, StartPoint, Params);
	}
}

void FCodeAnimWebConnectionDrawingPolicy::DrawLineWithArrow(const FVector2f& StartAnchor, const FVector2f& EndAnchor, const FConnectionParams& Params)
{
	const FVector2f Delta = EndAnchor - StartAnchor;
	const FVector2f Unit = Delta.GetSafeNormal();
	const FVector2f Normal = FVector2f(Delta.Y, -Delta.X).GetSafeNormal();

	// Offset to one side, so two transitions between the same pair (one each way) do not overlap.
	const FVector2f Side = Normal * (Params.bUserFlag1 || Params.bUserFlag2 ? 0.0f : 6.0f * ZoomFactor);
	const FVector2f Radius(ArrowRadius);
	const FVector2f Length = Radius.X * Unit;
	const FVector2f Start = StartAnchor + Side + Length;
	const FVector2f End = EndAnchor + Side - Length;

	DrawConnection(WireLayerID, Start, End - Length * 0.8f, Params);

	FSlateDrawElement::MakeRotatedBox(DrawElementsList, ArrowLayerID,
		FPaintGeometry(End - Radius, ArrowImage->ImageSize * ZoomFactor, ZoomFactor),
		ArrowImage, ESlateDrawEffect::None, static_cast<float>(FMath::Atan2(Delta.Y, Delta.X)),
		TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement, Params.WireColor);
}

void FCodeAnimWebConnectionDrawingPolicy::DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2f& StartPoint,
	const FVector2f& EndPoint, UEdGraphPin* Pin)
{
	// While dragging out a new transition: an arrow from the state's edge to the cursor.
	FConnectionParams Params;
	Params.bUserFlag2 = true;
	Params.WireThickness = 1.5f;
	Params.WireColor = FStyleColors::Foreground.GetSpecifiedColor();
	Params.WireColor.A = 0.6f;
	const FVector2f EdgePoint = FGeometryHelper::FindClosestPointOnGeom(PinGeometry, EndPoint);
	DrawLineWithArrow(EdgePoint, EndPoint, Params);
}

FVector2f FCodeAnimWebConnectionDrawingPolicy::ComputeSplineTangent(const FVector2f& Start, const FVector2f& End) const
{
	// Straight lines: a tangent along the line makes DrawConnection's spline one.
	return (End - Start).GetSafeNormal();
}

#undef LOCTEXT_NAMESPACE
