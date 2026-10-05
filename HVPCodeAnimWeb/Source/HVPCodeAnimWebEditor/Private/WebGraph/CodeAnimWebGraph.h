#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"

#include "CodeAnimWebGraph.generated.h"

class UBlueprint;
class UCodeAnimationWeb;
class UCodeAnimWebGraphEntryNode;
class UCodeAnimWebGraphStateNode;

/**
 * The Web Graph: a picture of a Web's states and transitions, and a way to edit them.
 *
 * It is only a view. The Web's own data - the enum, State Values and Transitions in Class Defaults -
 * stays the one source of truth; this graph is rebuilt from it whenever it changes, and every edit made
 * here (a new transition, a deleted one, a moved state) is written straight back to it. So the graph is
 * transient: nothing of it is saved except where each state sits.
 */
UCLASS(Transient)
class UCodeAnimWebEdGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UBlueprint> Blueprint;

	/** The Web's class defaults, fetched fresh: a compile replaces them. */
	UCodeAnimationWeb* GetDefaults() const;

	void Rebuild();

	UCodeAnimWebGraphStateNode* FindState(FName Key) const;
	UCodeAnimWebGraphEntryNode* FindEntry() const;

	/** Broadcast after this graph changed the Web's data, so the editor rebuilds it. */
	FSimpleMulticastDelegate OnDataChanged;
};

/**
 * Where the Web starts: an arrow from here into the initial state, as in an animation state machine.
 * Drag it onto another state to start there instead.
 */
UCLASS(Transient)
class UCodeAnimWebGraphEntryNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	UEdGraphPin* GetOutputPin() const { return Pins[0]; }

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual bool CanDuplicateNode() const override { return false; }
	virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;
};

/** A state. Its whole border is the handle to drag a new transition from. */
UCLASS(Transient)
class UCodeAnimWebGraphStateNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	FName StateKey;
	FText DisplayName;
	bool bHasGraph = false;
	int32 NumSet = 0;
	int32 NumOutputs = 0;

	UEdGraphPin* GetInputPin() const { return Pins[0]; }
	UEdGraphPin* GetOutputPin() const { return Pins[1]; }

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual bool CanDuplicateNode() const override { return false; }
	virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;
};

/** A transition, drawn as an arrow between its states with this node riding on it. */
UCLASS(Transient)
class UCodeAnimWebGraphTransitionNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	int32 TransitionIndex = INDEX_NONE;
	bool bTwoWay = false;
	bool bHasGraph = false;
	FText Summary;

	/** Direction of the arrow, for the icon; set by the widget as it lays itself out. */
	FVector2f CachedDirection = FVector2f(1.0f, 0.0f);

	UEdGraphPin* GetInputPin() const { return Pins[0]; }
	UEdGraphPin* GetOutputPin() const { return Pins[1]; }
	UCodeAnimWebGraphStateNode* GetFromState() const;
	UCodeAnimWebGraphStateNode* GetToState() const;

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanDuplicateNode() const override { return false; }
	virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;
};

UCLASS(Transient)
class UCodeAnimWebGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	static const FName PC_Transition;

	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;
	virtual bool SupportsDropPinOnNode(UEdGraphNode* InTargetNode, const FEdGraphPinType& InSourcePinType,
		EEdGraphPinDirection InSourcePinDirection, FText& OutErrorMessage) const override;
	virtual UEdGraphPin* DropPinOnNode(UEdGraphNode* InTargetNode, const FName& InSourcePinName,
		const FEdGraphPinType& InSourcePinType, EEdGraphPinDirection InSourcePinDirection) const override;
	virtual void GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual class FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID,
		float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements,
		class UEdGraph* InGraphObj) const override;

	// Links are the picture of the data, not the data: breaking one by hand would only lie about it.
	virtual void BreakNodeLinks(UEdGraphNode& TargetNode) const override {}
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const override {}
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override {}
};

/**
 * What the Blueprint editor's Details panel shows while something is selected in the Web Graph: one
 * state's values, one transition, or the Web's own settings. It holds no data itself - its Details
 * are the Web's real class-default properties, so editing there is editing Class Defaults, with undo.
 */
UCLASS(Transient)
class UCodeAnimWebGraphSelection : public UObject
{
	GENERATED_BODY()

public:
	enum class EKind : uint8 { Web, State, Transition };

	TWeakObjectPtr<UBlueprint> Blueprint;
	EKind Kind = EKind::Web;
	FName StateKey;
	int32 TransitionIndex = INDEX_NONE;

	UCodeAnimationWeb* GetDefaults() const;
};

/** Every change the Web Graph makes to a Web, each undoable. */
namespace CodeAnimWebGraphEdits
{
	void AddTransition(UCodeAnimWebEdGraph* Graph, FName From, FName To);
	void RemoveTransitions(UCodeAnimWebEdGraph* Graph, TArray<int32> Indices);
	void SetTwoWay(UCodeAnimWebEdGraph* Graph, int32 Index, bool bTwoWay);
	void Reverse(UCodeAnimWebEdGraph* Graph, int32 Index);
	void SetInitialState(UCodeAnimWebEdGraph* Graph, FName Key);

	/** Called as a state is dragged; the panel's own Move transaction covers it. */
	void SetStatePosition(UCodeAnimWebEdGraph* Graph, FName Key, const FVector2D& Position);
	void SetEntryPosition(UCodeAnimWebEdGraph* Graph, const FVector2D& Position);

	/** Double-click: a state's or transition's graph, created if it has none. */
	void OpenGraphFor(UEdGraphNode* Node);

	/**
	 * The node an arrow is being dragged out of. The engine's drag keeps its pins to itself, so the edge
	 * pin notes where a drag began; the schema and the states read it back.
	 */
	void SetDragSource(UEdGraphNode* Node);
	UEdGraphPin* GetDragSourcePin();
}
