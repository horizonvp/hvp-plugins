#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphPin.h"
#include "Widgets/Input/SComboBox.h"

class UK2Node_SetIndexedPrimitiveData;

/**
 * The Parameter pin on Set Indexed Primitive Data: a dropdown of the chosen index's parameters, each
 * labelled with its type and slot.
 *
 * The option list is rebuilt every time the dropdown opens rather than once at construction, so an
 * index edited while the Blueprint is open is reflected the next time you look - no change
 * notification to subscribe to, and nothing to go stale. The shown value reads the pin every frame
 * for the same reason: the node can rewrite it (a new index, a rename) without this widget knowing.
 */
class SGraphPinPrimitiveDataParameter : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinPrimitiveDataParameter) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	UK2Node_SetIndexedPrimitiveData* GetNode() const;
	void RebuildOptions();
	FText LabelFor(FName Name) const;
	void OnSelected(TSharedPtr<FName> Item, ESelectInfo::Type Info);

	TArray<TSharedPtr<FName>> Options;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> Combo;
};

/** Hands the Parameter pin of Set Indexed Primitive Data the widget above. */
class FPrimitiveDataParameterPinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override;
};
