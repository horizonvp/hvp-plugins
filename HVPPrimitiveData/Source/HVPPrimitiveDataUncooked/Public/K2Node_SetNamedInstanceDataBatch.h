#pragma once

#include "CoreMinimal.h"
#include "InstanceDataLegend.h"
#include "K2Node.h"
#include "K2Node_SetNamedPrimitiveData.h"

#include "K2Node_SetNamedInstanceDataBatch.generated.h"

/** One parameter ticked on the batch node, and whether it takes one value or one per instance. */
USTRUCT()
struct FNamedInstanceDataBatchSelection
{
	GENERATED_BODY()

	/** The legend entry. Its pin is named after this, so a relabelled parameter keeps its wire. */
	UPROPERTY()
	FGuid Id;

	UPROPERTY()
	FName Name;

	/** None while the legend no longer has it: no pin, and a compile error saying so. */
	UPROPERTY()
	ENamedPrimitiveDataValueKind Kind = ENamedPrimitiveDataValueKind::None;

	/** An array pin, one value per entry of Instance Indices, rather than one value for them all. */
	UPROPERTY()
	bool bPerInstance = false;
};

/**
 * Set Named Instance Data (Batch): set parameters of an Instance Data Legend on MANY instances of an
 * instanced static mesh in one call - for updating hundreds or thousands of instances a frame without a
 * Blueprint loop.
 *
 * Wire an array of instance indices. Tick parameters in the Details panel, and for each choose whether it
 * takes one value for every instance or, ticking Per Instance, an array with a value per instance (the
 * k-th value goes to the k-th index). It compiles to one UInstanceDataSetLibrary::SetInstanceCustomDataBatch
 * call per execution, which loops in C++: consecutive ascending indices are written with the engine's
 * ranged SetCustomData as one block, any other order row by row. Floats the node does not set keep their
 * values, and an instance past the end of a per instance array keeps that parameter as it is.
 */
UCLASS()
class HVPPRIMITIVEDATAUNCOOKED_API UK2Node_SetNamedInstanceDataBatch : public UK2Node
{
	GENERATED_BODY()

public:
	static const FName TargetPinName;
	static const FName InstanceIndicesPinName;
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
	void SetParameterSelected(const FGuid& Id, bool bSelected);

	bool IsParameterPerInstance(const FGuid& Id) const;
	/** Switch a ticked parameter's pin between one value and an array. Its wire goes, as the type changes. */
	void SetParameterPerInstance(const FGuid& Id, bool bPerInstance);

	/** The ticked parameters, in legend order. */
	const TArray<FNamedInstanceDataBatchSelection>& GetSelection() const { return Selection; }

	UEdGraphPin* FindValuePin(const FGuid& Id) const;

	/** As on the multiple node: first slot, count, and which floats are this node's (bit i = slot First + i). */
	bool GetWriteRange(int32& OutFirst, int32& OutCount, uint64& OutMask) const;

	/** Fired when the parameters on offer change, so the Details panel can rebuild. */
	FSimpleMulticastDelegate OnParametersOffered;

private:
	static FName PinNameFor(const FGuid& Id);
	void CreateValuePin(const FNamedInstanceDataBatchSelection& Selected);
	UEdGraphPin* FindPinIn(FName Name, const TArray<UEdGraphPin*>* InPins) const;
	const FInstanceDataLegendEntry* Resolve(const UInstanceDataLegend* Legend, const FNamedInstanceDataBatchSelection& Selected) const;
	void RefreshSelection(const TArray<UEdGraphPin*>* InPins);
	void DropValuePin(const FGuid& Id);
	void Rebuild();

	UPROPERTY()
	TArray<FNamedInstanceDataBatchSelection> Selection;
};
