#pragma once

#include "CoreMinimal.h"
#include "ConnectionDrawingPolicy.h"
#include "SGraphNode.h"
#include "SGraphPin.h"

class UCodeAnimWebGraphEntryNode;
class UCodeAnimWebGraphStateNode;
class UCodeAnimWebGraphTransitionNode;

/**
 * A state: a rounded box with the state's name, what it sets, and a graph icon if it has a state graph.
 * The border all round is its output pin, as in an animation state machine: drag from the edge onto
 * another state to add a transition.
 */
class SCodeAnimWebStateNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SCodeAnimWebStateNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UCodeAnimWebGraphStateNode* InNode);

	virtual void UpdateGraphNode() override;
	virtual void CreatePinWidgets() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	virtual void MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	/**
	 * A transition (or the Entry arrow) let go anywhere on this state - edge or middle, it means the
	 * same thing - so the drop is handled here rather than by whichever pin happens to be underneath.
	 */
	FReply HandleConnectionDrop();

private:
	/** Whether the arrow being dragged could land here; the whole state lights up while it can. */
	bool CanAcceptDrop() const;

	TSharedPtr<SOverlay> PinOverlay;
	bool bDropTarget = false;
};

/** The Entry node: a small box whose edge, like a state's, is dragged onto the state to start in. */
class SCodeAnimWebEntryNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SCodeAnimWebEntryNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UCodeAnimWebGraphEntryNode* InNode);

	virtual void UpdateGraphNode() override;
	virtual void CreatePinWidgets() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	virtual void MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;

private:
	TSharedPtr<SOverlay> PinOverlay;
};

/** A transition: a small disc riding on its arrow, showing direction; placed by the states it joins. */
class SCodeAnimWebTransitionNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SCodeAnimWebTransitionNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UCodeAnimWebGraphTransitionNode* InNode);

	virtual void UpdateGraphNode() override;
	virtual bool RequiresSecondPassLayout() const override { return true; }
	virtual void PerformSecondPassLayout(const TMap<UObject*, TSharedRef<SNode>>& NodeToWidgetLookup) const override;

private:
	FSlateColor GetColor() const;
};

/** Straight arrows from state edge to state edge, doubled for two-way transitions. */
class FCodeAnimWebConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
	FCodeAnimWebConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor,
		const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements);

	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params) override;
	virtual void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& PinGeometries, FArrangedChildren& ArrangedNodes) override;
	virtual void DetermineLinkGeometry(FArrangedChildren& ArrangedNodes, TSharedRef<SWidget>& OutputPinWidget,
		UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FArrangedWidget*& StartWidgetGeometry, FArrangedWidget*& EndWidgetGeometry) override;
	virtual void DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params) override;
	virtual void DrawSplineWithArrow(const FVector2f& StartPoint, const FVector2f& EndPoint, const FConnectionParams& Params) override;
	virtual void DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2f& StartPoint, const FVector2f& EndPoint, UEdGraphPin* Pin) override;
	virtual FVector2f ComputeSplineTangent(const FVector2f& Start, const FVector2f& End) const override;

private:
	void DrawLineWithArrow(const FVector2f& Start, const FVector2f& End, const FConnectionParams& Params);

	TMap<UEdGraphNode*, int32> NodeWidgetMap;
};
