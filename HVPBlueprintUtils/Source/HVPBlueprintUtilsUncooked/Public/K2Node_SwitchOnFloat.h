#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"

#include "K2Node_SwitchOnFloat.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;

/**
 * One output pin's interval.
 *
 * Both bounds carry their own inclusivity because the useful default is half-open - [0, 1), [1, 2),
 * [2, 3) - which tiles a number line without a value ever landing in two bands by accident. Closed
 * on both ends is available and is exactly how you get deliberate overlap.
 */
USTRUCT()
struct FHVPFloatSwitchRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Range")
	double Min = 0.0;

	UPROPERTY(EditAnywhere, Category = "Range")
	double Max = 1.0;

	/** Value == Min counts as inside. */
	UPROPERTY(EditAnywhere, Category = "Range")
	bool bIncludeMin = true;

	/** Value == Max counts as inside. Off by default so adjacent ranges do not both claim the seam. */
	UPROPERTY(EditAnywhere, Category = "Range")
	bool bIncludeMax = false;

	/** Shown on the pin instead of the interval. Leave empty for the interval itself. */
	UPROPERTY(EditAnywhere, Category = "Range")
	FString Label;
};

/**
 * Switch on Float - one exec output per numeric range, edited in the details panel.
 *
 * NOT ACTUALLY A SWITCH, and the difference is deliberate: every range containing the value fires,
 * top to bottom, not just the first. Overlapping ranges are a feature here rather than an
 * ambiguity to resolve, which is why this expands to a sequence of guarded branches rather than to
 * the chain of exclusive comparisons a real switch uses.
 *
 * Ranges are edited as an array, so the details panel's own add/remove/reorder is the pin editing
 * UI - there is nothing to reimplement. Pins are named by INDEX, so changing a range's bounds keeps
 * its wiring; inserting one in the middle shifts everything after it, exactly as reordering Switch
 * on String's names does.
 *
 * Step Alpha reports where the value sat inside whichever range fired - 0 at that range's Min, 1
 * at its Max - which is the number you actually want for driving a blend once a band has been
 * picked. It assumes one match at a time: with overlapping ranges it holds whichever fired LAST,
 * since each match overwrites it. Nothing matched leaves it at 0.
 *
 * The expansion is Engine nodes only - UKismetMathLibrary::InRange_FloatFloat, Branch, Sequence -
 * so a blueprint using this compiles and cooks with no dependency on this module beyond edit time.
 */
UCLASS()
class UK2Node_SwitchOnFloat : public UK2Node
{
	GENERATED_BODY()

public:
	UK2Node_SwitchOnFloat();

	/** One exec output each, in order. */
	UPROPERTY(EditAnywhere, Category = "Switch On Float")
	TArray<FHVPFloatSwitchRange> Ranges;

	/** An extra output that fires only when no range matched at all. */
	UPROPERTY(EditAnywhere, Category = "Switch On Float")
	bool bHasDefaultPin = true;

	//~ UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	//~ UK2Node
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual bool ShouldShowNodeProperties() const override { return true; }

	UEdGraphPin* GetSelectionPin() const;
	UEdGraphPin* GetDefaultPin() const;
	UEdGraphPin* GetAlphaPin() const;
	UEdGraphPin* GetRangePin(int32 Index) const;

private:
	/** Stable across bound edits - which is what keeps a pin's connections when a range is retuned. */
	static FName RangePinName(int32 Index);

	/** "[0.0, 1.0)" unless the range carries a label. */
	FText RangePinFriendlyName(int32 Index) const;
};
