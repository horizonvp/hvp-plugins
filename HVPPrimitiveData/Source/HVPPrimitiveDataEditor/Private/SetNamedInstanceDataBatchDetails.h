#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class UK2Node_SetNamedInstanceDataBatch;

/**
 * Details panel of Set Named Instance Data (Batch): per parameter of the chosen legend, a checkbox to set
 * it and one to take a value per instance, and a line saying what the node will write.
 */
class FSetNamedInstanceDataBatchDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual ~FSetNamedInstanceDataBatchDetails() override;

	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FText DescribeWrite() const;

	TWeakObjectPtr<UK2Node_SetNamedInstanceDataBatch> Node;
	TWeakPtr<IDetailLayoutBuilder> Builder;
	FDelegateHandle OfferedHandle;
};
