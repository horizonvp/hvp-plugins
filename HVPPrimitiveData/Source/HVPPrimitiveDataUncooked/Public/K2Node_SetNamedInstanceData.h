#pragma once

#include "CoreMinimal.h"
#include "InstanceDataLegend.h"
#include "K2Node.h"
#include "K2Node_SetNamedPrimitiveData.h"

#include "K2Node_SetNamedInstanceData.generated.h"

/**
 * Set Named Instance Data: set one per instance custom data parameter on one instance of an instanced
 * static mesh, chosen by name from an Instance Data Legend.
 *
 * The Set Named Primitive Data node's twin. Pick the legend, then the parameter from a dropdown; the
 * Value pin becomes a float or a Linear Color to match. At compile time it expands to the engine's
 * UInstancedStaticMeshComponent::SetCustomDataValue with the slot as a LITERAL - three of them, one per
 * channel, for a vector - so there is no name lookup at runtime and nothing from this plugin in a cooked
 * build. The exception is several meshes or an array wired into Target: SetCustomDataValue cannot loop
 * over targets (it returns a value), so that case calls UInstanceDataSetLibrary::SetInstanceCustomDataMasked,
 * the multiple node's function, instead.
 *
 * The legend and the parameter are choices made on the node, not data: both pins refuse connections.
 * The parameter is held by the legend entry's GUID, so relabelling it in the legend never orphans the node.
 */
UCLASS()
class HVPPRIMITIVEDATAUNCOOKED_API UK2Node_SetNamedInstanceData : public UK2Node
{
	GENERATED_BODY()

public:
	static const FName TargetPinName;
	static const FName InstanceIndexPinName;
	static const FName LegendPinName;
	static const FName ParameterPinName;
	static const FName ValuePinName;
	static const FName MarkRenderStateDirtyPinName;

	//~ UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;

	//~ UK2Node
	virtual bool IsNodePure() const override { return false; }
	/** Several instanced meshes wired into Target, or an array, set the same instance index on each. */
	virtual bool AllowMultipleSelfs(bool bInputAsArray) const override { return true; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual FText GetKeywords() const override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void PostReconstructNode() override;
	virtual void PreloadRequiredAssets() override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	/** The legend chosen on the node, or null. Reads OldPins during reconstruction, when the live pins have no defaults yet. */
	UInstanceDataLegend* GetLegend(const TArray<UEdGraphPin*>* InPins = nullptr) const;

	/** The chosen parameter: by GUID first, then by the name on the pin. */
	const FInstanceDataLegendEntry* ResolveParameter(const TArray<UEdGraphPin*>* InPins = nullptr) const;

private:
	UEdGraphPin* FindPinIn(FName Name, const TArray<UEdGraphPin*>* InPins) const;
	void CreateValuePin();

	/** Re-read the selection from the legend: GUID, name and value kind. */
	void RefreshSelection(const TArray<UEdGraphPin*>* InPins);

	/** Swap the Value pin for one of the new kind, in its place among the pins. */
	void SetValueKind(ENamedPrimitiveDataValueKind NewKind);

	static ENamedPrimitiveDataValueKind KindOf(const FInstanceDataLegendEntry* Entry);

	UPROPERTY()
	FGuid ParameterId;

	UPROPERTY()
	FName ParameterName;

	UPROPERTY()
	ENamedPrimitiveDataValueKind ValueKind = ENamedPrimitiveDataValueKind::None;
};
