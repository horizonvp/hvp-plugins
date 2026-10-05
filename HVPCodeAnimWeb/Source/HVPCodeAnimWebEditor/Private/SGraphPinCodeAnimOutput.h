#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphPin.h"
#include "Widgets/Input/SComboBox.h"

/**
 * The Output pin on Bind Web Output Changed and Get From / To Output: a dropdown of the Web's Animation
 * Outputs, each labelled with its type. Rebuilt whenever it opens, so outputs added since show up.
 */
class SGraphPinCodeAnimOutput : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinCodeAnimOutput) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	void RebuildOptions();
	FText LabelFor(FName Name) const;
	void OnSelected(TSharedPtr<FName> Item, ESelectInfo::Type Info);

	TArray<TSharedPtr<FName>> Options;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> Combo;
};

class FCodeAnimOutputPinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override;
};
