#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class UK2Node_SetNamedInstanceDataMulti;

/**
 * Details panel of Set Named Instance Data (Multiple): a checkbox per parameter of the chosen legend, and
 * a line saying what the node will write and how many floats per instance the mesh needs for it.
 */
class FSetNamedInstanceDataMultiDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual ~FSetNamedInstanceDataMultiDetails() override;

	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FText DescribeWrite() const;

	TWeakObjectPtr<UK2Node_SetNamedInstanceDataMulti> Node;
	TWeakPtr<IDetailLayoutBuilder> Builder;
	FDelegateHandle OfferedHandle;
};
