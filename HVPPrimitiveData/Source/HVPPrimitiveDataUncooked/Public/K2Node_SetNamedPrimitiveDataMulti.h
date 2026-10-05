#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_SetNamedPrimitiveData.h"
#include "PrimitiveDataLegend.h"

#include "K2Node_SetNamedPrimitiveDataMulti.generated.h"

/** One parameter ticked on the node. Enough to build its pin without the legend loaded. */
USTRUCT()
struct FNamedPrimitiveDataSelection
{
	GENERATED_BODY()

	/** The legend entry. Its pin is named after this, so a relabelled parameter keeps its wire. */
	UPROPERTY()
	FGuid Id;

	/** Its current name, shown on the pin and used to find it again in a different legend. */
	UPROPERTY()
	FName Name;

	/** None while the legend no longer has it: no pin, and a compile error saying so. */
	UPROPERTY()
	ENamedPrimitiveDataValueKind Kind = ENamedPrimitiveDataValueKind::None;
};

/**
 * Set Named Primitive Data (Multiple): set several parameters of a Primitive Data Legend on a mesh in
 * ONE custom primitive data update, rather than one update per parameter.
 *
 * Every Set Custom Primitive Data call on a mesh that changes something sends the renderer an update
 * of its own. Animating four parameters with four nodes is four of them per mesh per frame - which
 * across a few hundred meshes is a measurable cost. This node sends one.
 *
 * Pick the legend on the node and tick parameters in the Details panel; each gets a pin, a float or a
 * Linear Color. At compile time the ticked floats are laid out from the lowest slot to the highest:
 *
 *   - if they are side by side, it becomes the engine's SetCustomPrimitiveDataFloatArray with the
 *     first slot as a literal - nothing from this plugin at runtime, like the single node;
 *   - if other parameters sit between them, it becomes UPrimitiveDataSetLibrary::
 *     SetCustomPrimitiveDataMasked, which reads the floats in between back off the mesh and writes them
 *     over themselves, so they are left as they were - still a single update.
 *
 * Several meshes wired into Target, or an array, set each of them, as on the single node.
 */
UCLASS()
class HVPPRIMITIVEDATAUNCOOKED_API UK2Node_SetNamedPrimitiveDataMulti : public UK2Node
{
	GENERATED_BODY()

public:
	static const FName TargetPinName;
	static const FName LegendPinName;

	/**
	 * Write the mesh's Custom Primitive Data Defaults rather than its runtime data. For construction
	 * scripts: runtime data set there is not saved, and resets to the defaults when the level loads.
	 * Rebuilds the mesh's render state on every call, so not for every frame.
	 */
	UPROPERTY(EditAnywhere, Category = "Primitive Data")
	bool bWriteDefaults = false;

	//~ UObject
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

	//~ UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;

	//~ UK2Node
	virtual bool IsNodePure() const override { return false; }
	/** The Blueprint editor only hands a node to the Details panel when it asks; the parameter checkboxes live there. */
	virtual bool ShouldShowNodeProperties() const override { return true; }
	virtual bool AllowMultipleSelfs(bool bInputAsArray) const override { return true; }
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

	/** The legend chosen on the node, or null. Reads OldPins during reconstruction, when the live pins have no defaults yet. */
	UPrimitiveDataLegend* GetLegend(const TArray<UEdGraphPin*>* InPins = nullptr) const;

	/** Whether the legend's parameter with this id is ticked. */
	bool IsParameterSelected(const FGuid& Id) const;

	/** Tick or untick one of the legend's parameters: adds or removes its pin, keeping every other wire. */
	void SetParameterSelected(const FGuid& Id, bool bSelected);

	/** The ticked parameters, in legend order. */
	const TArray<FNamedPrimitiveDataSelection>& GetSelection() const { return Selection; }

	/** The pin carrying a ticked parameter's value, or null if it has none. */
	UEdGraphPin* FindValuePin(const FGuid& Id) const;

	/**
	 * The floats this node would write, as of the current legend: first slot, count, and which of them
	 * are its own (bit i = slot First + i). False if nothing ticked resolves to a slot.
	 */
	bool GetWriteRange(int32& OutFirst, int32& OutCount, uint64& OutMask) const;

	/**
	 * Fired when the parameters on offer change - a different legend chosen, or the legend edited - so
	 * the Details panel can rebuild its checkbox list. Not fired for ticking, which changes no list.
	 */
	FSimpleMulticastDelegate OnParametersOffered;

private:
	static FName PinNameFor(const FGuid& Id);
	void CreateValuePin(const FNamedPrimitiveDataSelection& Selected);
	UEdGraphPin* FindPinIn(FName Name, const TArray<UEdGraphPin*>* InPins) const;
	const FPrimitiveDataLegendEntry* Resolve(const UPrimitiveDataLegend* Legend, const FNamedPrimitiveDataSelection& Selected) const;

	/** Re-read every ticked parameter's name and kind from the legend, and put them in legend order. */
	void RefreshSelection(const TArray<UEdGraphPin*>* InPins);

	/** Remove a parameter's pin now, wires and all, rather than leave it to reconstruction. */
	void DropValuePin(const FGuid& Id);

	/** Rebuild the pins after the selection changed, and tell the Blueprint. */
	void Rebuild();

	UPROPERTY()
	TArray<FNamedPrimitiveDataSelection> Selection;
};
