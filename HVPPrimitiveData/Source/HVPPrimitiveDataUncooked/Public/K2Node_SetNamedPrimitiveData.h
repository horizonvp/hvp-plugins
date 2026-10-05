#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "PrimitiveDataLegend.h"

#include "K2Node_SetNamedPrimitiveData.generated.h"

/** What the Value pin currently is. Stored on the node so AllocateDefaultPins can build it without a legend. */
UENUM()
enum class ENamedPrimitiveDataValueKind : uint8
{
	None,
	Scalar,
	Vector,
};

/**
 * Set Named Primitive Data: set one custom primitive data parameter on a mesh, chosen by name from a
 * Primitive Data Legend.
 *
 * Pick the legend, then the parameter from a dropdown; the Value pin becomes a float or a Linear Color
 * to match. At compile time it expands to UPrimitiveComponent::SetCustomPrimitiveDataFloat or
 * SetCustomPrimitiveDataVector4 with the slot as a LITERAL - there is no name lookup at runtime and no
 * reference to the legend in a cooked build, so it costs exactly what the hand-wired call does.
 *
 * The legend and the parameter are choices made on the node, not data: both pins refuse connections.
 * That is not a limitation to work around - a slot resolved at runtime would need the legend at
 * runtime, and the whole design is that it does not.
 *
 * The parameter is held by the legend entry's GUID, not its name, so relabelling a parameter in the
 * legend never orphans the nodes that use it; the pin just shows the new name.
 */
UCLASS()
class HVPPRIMITIVEDATAUNCOOKED_API UK2Node_SetNamedPrimitiveData : public UK2Node
{
	GENERATED_BODY()

public:
	static const FName TargetPinName;
	static const FName LegendPinName;
	static const FName ParameterPinName;
	static const FName ValuePinName;

	//~ UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;

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

	//~ UK2Node
	virtual bool IsNodePure() const override { return false; }
	/** For Write Defaults, which lives in the Details panel. */
	virtual bool ShouldShowNodeProperties() const override { return true; }

	/**
	 * Several meshes wired into Target, or an array, run the set once per mesh - the same behaviour as
	 * any engine function node. None of the looping is done here: ExpandNode hands Target's links to
	 * an intermediate SetCustomPrimitiveData* call, and UK2Node_CallFunction::ExpandNode already turns
	 * multiple or array selfs into one call each. This only tells the schema to allow the wiring.
	 */
	virtual bool AllowMultipleSelfs(bool bInputAsArray) const override { return true; }

	/**
	 * Maps old pins onto new ones: Target (before it became a self pin) and Index (before the asset was
	 * renamed Legend), so Blueprints saved with either keep their wires and their chosen legend.
	 */
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

	/** The chosen parameter: by GUID first, then by the name on the pin (a node from before it had one). */
	const FPrimitiveDataLegendEntry* ResolveParameter(const TArray<UEdGraphPin*>* InPins = nullptr) const;

private:
	UEdGraphPin* FindPinIn(FName Name, const TArray<UEdGraphPin*>* InPins) const;
	void CreateValuePin();

	/** Re-read the selection from the legend: GUID, name and value kind. */
	void RefreshSelection(const TArray<UEdGraphPin*>* InPins);

	/** Swap the Value pin for one of the new kind in place, breaking its links only if the type changes. */
	void SetValueKind(ENamedPrimitiveDataValueKind NewKind);

	static ENamedPrimitiveDataValueKind KindOf(const FPrimitiveDataLegendEntry* Entry);

	/** Identity of the chosen parameter in the legend. */
	UPROPERTY()
	FGuid ParameterId;

	/** The chosen parameter's current name, written back to the pin after reconstruction. */
	UPROPERTY()
	FName ParameterName;

	UPROPERTY()
	ENamedPrimitiveDataValueKind ValueKind = ENamedPrimitiveDataValueKind::None;
};
