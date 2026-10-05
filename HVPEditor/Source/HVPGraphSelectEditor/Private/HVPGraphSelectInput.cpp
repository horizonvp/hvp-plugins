#include "HVPGraphSelectInput.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditor.h"
#include "GraphEditorSettings.h"
#include "HVPGraphSelect.h"
#include "HVPGraphSelectSettings.h"
#include "Layout/WidgetPath.h"
#include "SGraphPanel.h"
#include "HVPDirectionalSelect.h"
#include "SHVPCutLine.h"
#include "SHVPRadialMenu.h"
#include "ScopedTransaction.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogHVPGraphSelect, Log, All);

namespace HVPGraphSelectInput
{
	static TSharedPtr<FHVPGraphSelectInput> Processor;

	/**
	 * True when the graph editor already pans with the configured gesture button - hands off.
	 *
	 * Only the two buttons panning can be bound to are worth asking about; any other gesture key is
	 * unclaimed by definition.
	 */
	static bool bGestureButtonIsPanning(const FKey& GestureKey)
	{
		const EGraphPanningMouseButton Panning = GetDefault<UGraphEditorSettings>()->PanningMouseButton;
		if (GestureKey == EKeys::MiddleMouseButton)
		{
			return Panning == EGraphPanningMouseButton::Middle || Panning == EGraphPanningMouseButton::Both;
		}
		if (GestureKey == EKeys::RightMouseButton)
		{
			return Panning == EGraphPanningMouseButton::Right || Panning == EGraphPanningMouseButton::Both;
		}
		return false;
	}

	/**
	 * The graph under the cursor, via the widget path.
	 *
	 * SGraphPanel rather than SGraphEditor, because SGraphPanel is a public header with a public
	 * GetGraphObj() and it declares itself through SLATE_DECLARE_WIDGET_API, so GetType() names it
	 * reliably. From the graph, FindGraphEditorForGraph gets back to the editor that owns it.
	 */
	static UEdGraph* FindGraphUnderCursor(const FVector2D& ScreenPosition,
		TSharedPtr<SWindow>& OutWindow, TSharedPtr<SGraphPanel>& OutPanel)
	{
		OutWindow.Reset();
		OutPanel.Reset();

		FSlateApplication& Slate = FSlateApplication::Get();
		const FWidgetPath Path =
			Slate.LocateWindowUnderMouse(ScreenPosition, Slate.GetInteractiveTopLevelWindows());
		if (!Path.IsValid())
		{
			return nullptr;
		}
		OutWindow = Path.GetWindow();

		static const FName GraphPanelType(TEXT("SGraphPanel"));
		for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
		{
			const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
			if (Widget->GetType() == GraphPanelType)
			{
				OutPanel = StaticCastSharedRef<SGraphPanel>(Widget);
				return OutPanel->GetGraphObj();
			}
		}
		return nullptr;
	}

	/**
	 * Puts an overlay on the window that actually hosts the panel, and returns that window.
	 *
	 * The panel's own window rather than the hit-test path's: the path starts at whichever window
	 * the OS reported under the cursor, which is not guaranteed to be the one the panel paints into
	 * once floating windows and monitors with different DPI are involved.
	 */
	static TSharedPtr<SWindow> AttachOverlay(const TSharedPtr<SGraphPanel>& Panel,
		const TSharedPtr<SWindow>& PathWindow, const TSharedRef<SWidget>& Overlay)
	{
		TSharedPtr<SWindow> Window = Panel.IsValid()
			? FSlateApplication::Get().FindWidgetWindow(Panel.ToSharedRef())
			: nullptr;
		if (!Window.IsValid())
		{
			Window = PathWindow;
		}
		if (!Window.IsValid())
		{
			UE_LOG(LogHVPGraphSelect, Warning, TEXT("No window found for the graph panel; overlay not shown."));
			return nullptr;
		}

		// Logged every time on purpose: when the overlay fails to show on one monitor, this line
		// says which window it went to and at what DPI.
		UE_LOG(LogHVPGraphSelect, Log,
			TEXT("Overlay -> window '%s' (path window '%s') at %s, DPI %.2f, overlay %s"),
			*Window->GetTitle().ToString(),
			PathWindow.IsValid() ? *PathWindow->GetTitle().ToString() : TEXT("none"),
			*Window->GetPositionInScreen().ToString(),
			Window->GetDPIScaleFactor(),
			Window->HasOverlay() ? TEXT("yes") : TEXT("NO"));

		Window->AddOverlaySlot(INDEX_NONE)[Overlay];
		return Window;
	}

	/** The single selected node in that graph, or null if the selection is empty or ambiguous. */
	static UEdGraphNode* SoleSelectedNode(UEdGraph* Graph)
	{
		TSharedPtr<SGraphEditor> Editor = SGraphEditor::FindGraphEditorForGraph(Graph);
		if (!Editor.IsValid())
		{
			return nullptr;
		}

		UEdGraphNode* Found = nullptr;
		for (UObject* Selected : Editor->GetSelectedNodes())
		{
			UEdGraphNode* Node = Cast<UEdGraphNode>(Selected);
			if (!Node)
			{
				continue;
			}
			if (Found)
			{
				// More than one: which node the gesture means is genuinely unclear, and guessing
				// would reselect around an arbitrary member of the set.
				return nullptr;
			}
			Found = Node;
		}
		return Found;
	}

	/** Screen-space drag to a mode. Y grows downward, hence the inverted up/down test. */
	static EHVPGraphSelectMode ModeForDelta(const FVector2D& Delta)
	{
		if (FMath::Abs(Delta.X) > FMath::Abs(Delta.Y))
		{
			return Delta.X > 0.0
				? EHVPGraphSelectMode::Downstream
				: EHVPGraphSelectMode::Upstream;
		}
		return Delta.Y < 0.0
			? EHVPGraphSelectMode::LocalBranch
			: EHVPGraphSelectMode::NodeAndContext;
	}
}

void FHVPGraphSelectInput::Register()
{
	using namespace HVPGraphSelectInput;

	if (!Processor.IsValid() && FSlateApplication::IsInitialized())
	{
		Processor = MakeShared<FHVPGraphSelectInput>();
		FSlateApplication::Get().RegisterInputPreProcessor(Processor);
	}
}

void FHVPGraphSelectInput::Unregister()
{
	using namespace HVPGraphSelectInput;

	if (Processor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
	}
	Processor.Reset();
}

void FHVPGraphSelectInput::Tick(
	const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	if (!bTracking && !bCutting)
	{
		return;
	}

	const UHVPGraphSelectSettings& Settings = *GetDefault<UHVPGraphSelectSettings>();

	if (bCutting)
	{
		// Polled rather than event-driven: pressing or releasing the move modifier produces no MOUSE
		// event, so a conversion between selecting and moving would otherwise not register until the
		// cursor happened to move again.
		UpdateCut(FVector2D(SlateApp.GetCursorPos()),
			UHVPGraphSelectSettings::IsModifierDown(
				Settings.MoveModifier, SlateApp.GetModifierKeys()));
	}

	// WATCHDOG. A preprocessor is only guaranteed the events the application actually receives, and
	// alt-tabbing away mid-drag means the mouse-up lands in another application - so the release
	// this gesture is waiting for never arrives and the overlay sits there forever, surviving even
	// the next press because that press sees bTracking already true. Polling the real button state
	// catches that and every other way an up-event can go missing.
	if (!SlateApp.GetPressedMouseButtons().Contains(Settings.GestureStartKey))
	{
		bTracking = false;
		PressedNode = nullptr;
		DismissRadial();
		if (bCutting)
		{
			// Abandoned rather than committed: a drag interrupted by losing focus never asked for
			// its nodes to stay where the cursor happened to leave them.
			FinishCut(/*bCommit*/ false, /*bCtrlHeld*/ false);
		}
	}
}


bool FHVPGraphSelectInput::BeginCut(const FPointerEvent& MouseEvent)
{
	using namespace HVPGraphSelectInput;

	TSharedPtr<SWindow> Window;
	TSharedPtr<SGraphPanel> Panel;
	UEdGraph* Graph = FindGraphUnderCursor(MouseEvent.GetScreenSpacePosition(), Window, Panel);
	if (!Graph || !Panel.IsValid())
	{
		return false;
	}

	// Empty selection only, as specified. Shift alone would disambiguate this from the radial, but
	// the tool REPLACES the selection, and doing that to a selection someone deliberately made is
	// the kind of surprise that costs more than the keystroke it saves.
	TSharedPtr<SGraphEditor> Editor = SGraphEditor::FindGraphEditorForGraph(Graph);
	if (!Editor.IsValid() || Editor->GetSelectedNodes().Num() > 0)
	{
		return false;
	}

	bCutting = true;
	bCutMoveActive = false;
	CutBankedOffset = FVector2D::ZeroVector;
	CutCardinal.Reset();
	CutPanel = Panel;
	PressPosition = MouseEvent.GetScreenSpacePosition();

	// Recorded in GRAPH space: the view can be panned or zoomed mid-drag and the cut still means
	// the same place in the graph rather than the same place on the monitor.
	const FVector2D PanelLocal(Panel->GetCachedGeometry().AbsoluteToLocal(PressPosition));
	CutGraphOrigin = FVector2D(Panel->PanelCoordToGraphCoord(PanelLocal));

	CutOverlay = SNew(SHVPCutLine);
	CutOverlay->SetOrigin(PressPosition);
	CutOverlay->SetGraphRect(Panel->GetCachedGeometry().GetLayoutBoundingRect());
	CutOverlay->SetMoveMode(false);
	CutOverlay->SetCardinal(TOptional<EHVPCardinal>());
	CutWindow = AttachOverlay(Panel, Window, CutOverlay.ToSharedRef());
	return true;
}

void FHVPGraphSelectInput::UpdateCut(const FVector2D& CurrentPosition, bool bCtrlDown)
{
	const UHVPGraphSelectSettings& Settings = *GetDefault<UHVPGraphSelectSettings>();
	TSharedPtr<SGraphPanel> Panel = CutPanel.Pin();
	if (!Panel.IsValid())
	{
		return;
	}

	const FVector2D Delta = CurrentPosition - PressPosition;

	// LOCKED ONCE. Re-deriving the direction every frame would let a diagonal wobble swap which half
	// of the graph is being moved, mid-move, with nodes already displaced.
	if (!CutCardinal.IsSet())
	{
		if (Delta.Size() < Settings.GestureDeadZone)
		{
			return;
		}
		CutCardinal = FHVPDirectionalSelect::CardinalForDelta(Delta);

		const TArray<UEdGraphNode*> Nodes =
			FHVPDirectionalSelect::NodesBeyond(*Panel, CutGraphOrigin, CutCardinal.GetValue());

		CutNodes.Reset();
		CutOriginalPositions.Reset();
		for (UEdGraphNode* Node : Nodes)
		{
			CutNodes.Add(Node);
			CutOriginalPositions.Add(FIntPoint(Node->NodePosX, Node->NodePosY));
		}

		if (CutOverlay.IsValid())
		{
			CutOverlay->SetCardinal(CutCardinal);
			CutOverlay->SetAffectedCount(CutNodes.Num());
		}
	}

	// Ctrl is a live toggle, not a mode chosen at press. Each press banks nothing and starts a new
	// run from the cursor's position AT THAT MOMENT, so picking up Ctrl halfway does not teleport
	// the nodes by however far the cursor had already travelled while merely selecting.
	if (bCtrlDown != bCutMoveActive)
	{
		const FVector2D Axis = FHVPDirectionalSelect::AxisOf(CutCardinal.GetValue());
		const double Zoom = FMath::Max(KINDA_SMALL_NUMBER, static_cast<double>(Panel->GetZoomAmount()));

		if (bCtrlDown)
		{
			CutMoveAnchor = CurrentPosition;
			if (!CutTransaction.IsValid() && CutNodes.Num() > 0)
			{
				// Opened on the first Ctrl rather than at lock: a drag that only ever selects should
				// not leave an entry in the undo stack at all.
				CutTransaction = MakeUnique<FScopedTransaction>(
					NSLOCTEXT("HVPGraphSelect", "MoveBeyondCut", "Move Nodes Past Cut"));
				for (const TWeakObjectPtr<UEdGraphNode>& Weak : CutNodes)
				{
					if (UEdGraphNode* Node = Weak.Get())
					{
						Node->Modify();
					}
				}
			}
		}
		else
		{
			// Releasing Ctrl banks where things ended up. They stay put; further cursor travel does
			// nothing until Ctrl comes back.
			const double Run = FMath::Max(0.0,
				FVector2D::DotProduct(CurrentPosition - CutMoveAnchor, Axis));
			CutBankedOffset += Axis * (Run / Zoom);
		}

		bCutMoveActive = bCtrlDown;
		if (CutOverlay.IsValid())
		{
			CutOverlay->SetMoveMode(bCutMoveActive);
		}
	}

	if (!bCutMoveActive && CutBankedOffset.IsNearlyZero())
	{
		return;	// pure selection so far: the overlay is the whole of the feedback until release
	}

	// ON RAILS. Only the component along the locked axis is kept, so the nodes slide straight even
	// when the hand does not.
	const FVector2D Axis = FHVPDirectionalSelect::AxisOf(CutCardinal.GetValue());
	const double Zoom = FMath::Max(KINDA_SMALL_NUMBER, static_cast<double>(Panel->GetZoomAmount()));
	const double LiveRun = bCutMoveActive
		? FMath::Max(0.0, FVector2D::DotProduct(CurrentPosition - CutMoveAnchor, Axis))
		: 0.0;
	const FVector2D GraphOffset = CutBankedOffset + Axis * (LiveRun / Zoom);

	for (int32 Index = 0; Index < CutNodes.Num(); ++Index)
	{
		UEdGraphNode* Node = CutNodes[Index].Get();
		if (!Node)
		{
			continue;
		}
		// Set from the captured original rather than accumulated per frame, so rounding cannot
		// creep and backtracking returns nodes exactly where they started.
		Node->NodePosX = CutOriginalPositions[Index].X + FMath::RoundToInt(GraphOffset.X);
		Node->NodePosY = CutOriginalPositions[Index].Y + FMath::RoundToInt(GraphOffset.Y);
	}
}

void FHVPGraphSelectInput::FinishCut(bool bCommit, bool bCtrlHeld)
{
	TSharedPtr<SGraphPanel> Panel = CutPanel.Pin();

	// Selection happens unless Ctrl is down at the moment of release - the gesture's last word on
	// whether this was a move or a selection, regardless of what it did in between.
	if (bCommit && !bCtrlHeld && CutCardinal.IsSet() && Panel.IsValid())
	{
		if (TSharedPtr<SGraphEditor> Editor = SGraphEditor::FindGraphEditorForGraph(Panel->GetGraphObj()))
		{
			Editor->ClearSelectionSet();
			for (const TWeakObjectPtr<UEdGraphNode>& Weak : CutNodes)
			{
				if (UEdGraphNode* Node = Weak.Get())
				{
					Editor->SetNodeSelection(Node, true);
				}
			}
		}
	}

	if (CutTransaction.IsValid())
	{
		if (!bCommit)
		{
			// Abandoned: put every node back before the transaction closes, so the undo entry does
			// not record a move that was cancelled.
			for (int32 Index = 0; Index < CutNodes.Num(); ++Index)
			{
				if (UEdGraphNode* Node = CutNodes[Index].Get())
				{
					Node->NodePosX = CutOriginalPositions[Index].X;
					Node->NodePosY = CutOriginalPositions[Index].Y;
				}
			}
		}
		CutTransaction.Reset();
	}

	if (TSharedPtr<SWindow> Window = CutWindow.Pin())
	{
		if (CutOverlay.IsValid())
		{
			Window->RemoveOverlaySlot(CutOverlay.ToSharedRef());
		}
	}
	CutOverlay.Reset();
	CutWindow.Reset();

	bCutting = false;
	bCutMoveActive = false;
	CutBankedOffset = FVector2D::ZeroVector;
	CutCardinal.Reset();
	CutPanel.Reset();
	CutNodes.Reset();
	CutOriginalPositions.Reset();
}

TOptional<EHVPGraphSelectMode> FHVPGraphSelectInput::ResolveMode(
	const FVector2D& CurrentPosition) const
{
	using namespace HVPGraphSelectInput;

	const UHVPGraphSelectSettings& Settings = *GetDefault<UHVPGraphSelectSettings>();
	const FVector2D Delta = CurrentPosition - PressPosition;
	if (Delta.Size() < Settings.GestureDeadZone)
	{
		return TOptional<EHVPGraphSelectMode>();
	}
	return ModeForDelta(Delta);
}

void FHVPGraphSelectInput::DismissRadial()
{
	if (TSharedPtr<SWindow> Window = RadialWindow.Pin())
	{
		if (Radial.IsValid())
		{
			Window->RemoveOverlaySlot(Radial.ToSharedRef());
		}
	}
	Radial.Reset();
	RadialWindow.Reset();
}

bool FHVPGraphSelectInput::HandleMouseButtonDownEvent(
	FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	using namespace HVPGraphSelectInput;

	// Belt and braces alongside the Tick watchdog: never start a gesture on top of a stale one.
	bTracking = false;
	PressedNode = nullptr;
	DismissRadial();
	if (bCutting)
	{
		FinishCut(/*bCommit*/ false, /*bCtrlHeld*/ false);
	}

	const UHVPGraphSelectSettings& Settings = *GetDefault<UHVPGraphSelectSettings>();
	if (MouseEvent.GetEffectingButton() != Settings.GestureStartKey)
	{
		return false;
	}
	if (bGestureButtonIsPanning(Settings.GestureStartKey))
	{
		return false;
	}

	// The modifier picks the tool. The radial needs one node selected and the cut needs none, so
	// they could in principle coexist without one - but sharing a button with no visible difference
	// between them is how a tool becomes unpredictable.
	//
	// Asked before the radial toggle, so disabling one tool never disables the other.
	if (UHVPGraphSelectSettings::IsModifierDown(Settings.CutModifier, MouseEvent))
	{
		return Settings.bEnableSelectionCut && BeginCut(MouseEvent);
	}

	if (!Settings.bEnableRadialMenu)
	{
		return false;
	}

	TSharedPtr<SWindow> Window;
	TSharedPtr<SGraphPanel> Panel;
	UEdGraph* Graph = FindGraphUnderCursor(MouseEvent.GetScreenSpacePosition(), Window, Panel);
	if (!Graph)
	{
		return false;	// not over a graph at all - leave the button alone everywhere else
	}

	UEdGraphNode* Node = SoleSelectedNode(Graph);
	if (!Node)
	{
		return false;	// nothing selected, or too much: not our gesture, so do not eat the click
	}

	bTracking = true;
	PressPosition = MouseEvent.GetScreenSpacePosition();
	PressedNode = Node;

	// High ZOrder so the ring sits over the graph and its nodes rather than behind them.
	Radial = SNew(SHVPRadialMenu);
	Radial->SetCenter(PressPosition);
	Radial->SetActiveMode(TOptional<EHVPGraphSelectMode>());
	RadialWindow = AttachOverlay(Panel, Window, Radial.ToSharedRef());

	return true;	// consumed: from here the drag belongs to us
}

bool FHVPGraphSelectInput::HandleMouseMoveEvent(
	FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (bCutting)
	{
		const UHVPGraphSelectSettings& Settings = *GetDefault<UHVPGraphSelectSettings>();
		UpdateCut(MouseEvent.GetScreenSpacePosition(),
			UHVPGraphSelectSettings::IsModifierDown(Settings.MoveModifier, MouseEvent));
		return true;
	}

	if (!bTracking)
	{
		return false;
	}

	// One source of truth: the highlight is whatever the release would fire, so the ring can never
	// show one answer while the mouse-up performs another.
	if (Radial.IsValid())
	{
		Radial->SetActiveMode(ResolveMode(MouseEvent.GetScreenSpacePosition()));
	}

	return true;	// swallowed, so nothing else interprets the drag
}

bool FHVPGraphSelectInput::HandleMouseButtonUpEvent(
	FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	using namespace HVPGraphSelectInput;

	const UHVPGraphSelectSettings& Settings = *GetDefault<UHVPGraphSelectSettings>();
	if (MouseEvent.GetEffectingButton() != Settings.GestureStartKey)
	{
		return false;
	}

	if (bCutting)
	{
		FinishCut(/*bCommit*/ true,
			UHVPGraphSelectSettings::IsModifierDown(Settings.MoveModifier, MouseEvent));
		return true;
	}

	if (!bTracking)
	{
		return false;
	}

	bTracking = false;
	const TOptional<EHVPGraphSelectMode> Mode = ResolveMode(MouseEvent.GetScreenSpacePosition());
	DismissRadial();

	UEdGraphNode* Node = PressedNode.Get();
	PressedNode = nullptr;

	if (!Node)
	{
		return true;	// the node went away mid-drag; still ours, just nothing to do
	}

	// Unset means the cursor never left the dead zone: a middle CLICK, not a drag. Doing nothing
	// is the right answer - a pie menu that fires on a twitch is worse than one needing a pull.
	if (!Mode.IsSet())
	{
		return true;
	}

	FHVPGraphSelect::SelectFrom(Node, Mode.GetValue());

	UE_LOG(LogHVPGraphSelect, Verbose, TEXT("Gesture from '%s' -> mode %d"),
		*Node->GetName(), static_cast<int32>(Mode.GetValue()));

	return true;
}
