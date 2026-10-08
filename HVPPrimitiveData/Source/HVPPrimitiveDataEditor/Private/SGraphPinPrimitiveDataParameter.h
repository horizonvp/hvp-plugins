#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphPin.h"
#include "Widgets/Input/SComboBox.h"

/**
 * Where a Parameter pin's dropdown gets its options: the chosen legend's parameter names in legend order
 * (false with no legend chosen), and how to label one. Lets one widget serve the Set Named Primitive Data
 * and Set Named Instance Data nodes.
 */
struct FNamedDataParameterSource
{
	TFunction<bool(TArray<FName>&)> GetNames;
	TFunction<FText(FName)> Label;
};

/**
 * The Parameter pin on the Set Named ... Data nodes: a dropdown of the chosen legend's parameters, each
 * labelled with its type and slot.
 *
 * The option list is rebuilt every time the dropdown opens rather than once at construction, so an
 * legend edited while the Blueprint is open is reflected the next time you look - no change
 * notification to subscribe to, and nothing to go stale. The shown value reads the pin every frame
 * for the same reason: the node can rewrite it (a new legend, a rename) without this widget knowing.
 */
class SGraphPinPrimitiveDataParameter : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinPrimitiveDataParameter) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin, FNamedDataParameterSource InSource);

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	void RebuildOptions();
	FText LabelFor(FName Name) const;
	void OnSelected(TSharedPtr<FName> Item, ESelectInfo::Type Info);

	FNamedDataParameterSource Source;
	TArray<TSharedPtr<FName>> Options;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> Combo;
};

/** Hands the Parameter pin of Set Named Primitive Data and Set Named Instance Data the widget above. */
class FPrimitiveDataParameterPinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override;
};
