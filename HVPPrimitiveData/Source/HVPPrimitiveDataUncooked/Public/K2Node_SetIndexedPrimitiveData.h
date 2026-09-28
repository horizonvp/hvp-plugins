#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "PrimitiveDataIndex.h"

#include "K2Node_SetIndexedPrimitiveData.generated.h"

/** What the Value pin currently is. Stored on the node so AllocateDefaultPins can build it without an index. */
UENUM()
enum class EIndexedPrimitiveDataValueKind : uint8
{
	None,
	Scalar,
	Vector,
};

/**
 * Set Indexed Primitive Data: set one custom primitive data parameter on a mesh, chosen by name from a
 * Primitive Data Index.
 *
 * Pick the index, then the parameter from a dropdown; the Value pin becomes a float or a Linear Color
 * to match. At compile time it expands to UPrimitiveComponent::SetCustomPrimitiveDataFloat or
 * SetCustomPrimitiveDataVector4 with the slot as a LITERAL - there is no name lookup at runtime and no
 * reference to the index in a cooked build, so it costs exactly what the hand-wired call does.
 *
 * The index and the parameter are choices made on the node, not data: both pins refuse connections.
 * That is not a limitation to work around - a slot resolved at runtime would need the index at
 * runtime, and the whole design is that it does not.
 *
 * The parameter is held by the index entry's GUID, not its name, so relabelling a parameter in the
 * index never orphans the nodes that use it; the pin just shows the new name.
 */
UCLASS()
class HVPPRIMITIVEDATAUNCOOKED_API UK2Node_SetIndexedPrimitiveData : public UK2Node
{
	GENERATED_BODY()

public:
	static const FName TargetPinName;
	static const FName IndexPinName;
	static const FName ParameterPinName;
	static const FName ValuePinName;

	//~ UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;

	//~ UK2Node
	virtual bool IsNodePure() const override { return false; }

	/**
	 * Several meshes wired into Target, or an array, run the set once per mesh - the same behaviour as
	 * any engine function node. None of the looping is done here: ExpandNode hands Target's links to
	 * an intermediate SetCustomPrimitiveData* call, and UK2Node_CallFunction::ExpandNode already turns
	 * multiple or array selfs into one call each. This only tells the schema to allow the wiring.
	 */
	virtual bool AllowMultipleSelfs(bool bInputAsArray) const override { return true; }

	/** Nodes placed before Target became a self pin keep their Target wires. */
	virtual ERedirectType DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex,
		const UEdGraphPin* OldPin, int32 OldPinIndex) const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual FText GetKeywords() const override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void PostReconstructNode() override;
	virtual void PreloadRequiredAssets() override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	/** The index chosen on the node, or null. Reads OldPins during reconstruction, when the live pins have no defaults yet. */
	UPrimitiveDataIndex* GetIndex(const TArray<UEdGraphPin*>* InPins = nullptr) const;

	/** The chosen parameter: by GUID first, then by the name on the pin (a node from before it had one). */
	const FPrimitiveDataIndexEntry* ResolveParameter(const TArray<UEdGraphPin*>* InPins = nullptr) const;

private:
	UEdGraphPin* FindPinIn(FName Name, const TArray<UEdGraphPin*>* InPins) const;
	void CreateValuePin();

	/** Re-read the selection from the index: GUID, name and value kind. */
	void RefreshSelection(const TArray<UEdGraphPin*>* InPins);

	/** Swap the Value pin for one of the new kind in place, breaking its links only if the type changes. */
	void SetValueKind(EIndexedPrimitiveDataValueKind NewKind);

	static EIndexedPrimitiveDataValueKind KindOf(const FPrimitiveDataIndexEntry* Entry);

	/** Identity of the chosen parameter in the index. */
	UPROPERTY()
	FGuid ParameterId;

	/** The chosen parameter's current name, written back to the pin after reconstruction. */
	UPROPERTY()
	FName ParameterName;

	UPROPERTY()
	EIndexedPrimitiveDataValueKind ValueKind = EIndexedPrimitiveDataValueKind::None;
};
