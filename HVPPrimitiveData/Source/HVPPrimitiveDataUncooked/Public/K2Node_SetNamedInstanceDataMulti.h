#pragma once

#include "CoreMinimal.h"
#include "InstanceDataLegend.h"
#include "K2Node.h"
#include "K2Node_SetNamedPrimitiveDataMulti.h"

#include "K2Node_SetNamedInstanceDataMulti.generated.h"

/**
 * Set Named Instance Data (Multiple): set several parameters of an Instance Data Legend on one instance of
 * an instanced static mesh in a single write.
 *
 * Pick the legend on the node and tick parameters in the Details panel; each gets a pin, a float or a
 * Linear Color. It compiles to UInstanceDataSetLibrary::SetInstanceCustomDataMasked with the first slot
 * and the mask as literals: one call that reads the instance's current floats, lays the ticked ones over
 * them and writes the row back once - where the same in single nodes would be a SetCustomDataValue per
 * float, a vector being three.
 *
 * Several meshes wired into Target, or an array, set the same instance on each.
 */
UCLASS()
class HVPPRIMITIVEDATAUNCOOKED_API UK2Node_SetNamedInstanceDataMulti : public UK2Node
{
	GENERATED_BODY()

public:
	static const FName TargetPinName;
	static const FName InstanceIndexPinName;
	static const FName LegendPinName;
	static const FName MarkRenderStateDirtyPinName;

	//~ UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;

	//~ UK2Node
	virtual bool IsNodePure() const override { return false; }
	/** The parameter checkboxes live in the Details panel. */
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

	UInstanceDataLegend* GetLegend(const TArray<UEdGraphPin*>* InPins = nullptr) const;

	bool IsParameterSelected(const FGuid& Id) const;

	/** Tick or untick one of the legend's parameters: adds or removes its pin, keeping every other wire. */
	void SetParameterSelected(const FGuid& Id, bool bSelected);

	/** The ticked parameters, in legend order. */
	const TArray<FNamedPrimitiveDataSelection>& GetSelection() const { return Selection; }

	UEdGraphPin* FindValuePin(const FGuid& Id) const;

	/**
	 * The floats this node would write, as of the current legend: first slot, count, and which of them are
	 * its own (bit i = slot First + i). False if nothing ticked resolves to a slot.
	 */
	bool GetWriteRange(int32& OutFirst, int32& OutCount, uint64& OutMask) const;

	/** Fired when the parameters on offer change, so the Details panel can rebuild its checkbox list. */
	FSimpleMulticastDelegate OnParametersOffered;

private:
	static FName PinNameFor(const FGuid& Id);
	void CreateValuePin(const FNamedPrimitiveDataSelection& Selected);
	UEdGraphPin* FindPinIn(FName Name, const TArray<UEdGraphPin*>* InPins) const;
	const FInstanceDataLegendEntry* Resolve(const UInstanceDataLegend* Legend, const FNamedPrimitiveDataSelection& Selected) const;
	void RefreshSelection(const TArray<UEdGraphPin*>* InPins);
	void DropValuePin(const FGuid& Id);
	void Rebuild();

	UPROPERTY()
	TArray<FNamedPrimitiveDataSelection> Selection;
};
