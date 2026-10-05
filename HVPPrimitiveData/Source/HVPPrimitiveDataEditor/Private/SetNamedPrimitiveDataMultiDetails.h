#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class UK2Node_SetNamedPrimitiveDataMulti;

/**
 * Details panel of Set Named Primitive Data (Multiple): a checkbox per parameter of the chosen legend,
 * and a line saying what the node will write - one update of which slots, and whether the floats
 * between its parameters have to be read back to keep them.
 *
 * Everything reads the node live, so it stays right while the legend or the node changes underneath.
 */
class FSetNamedPrimitiveDataMultiDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual ~FSetNamedPrimitiveDataMultiDetails() override;

	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FText DescribeWrite() const;

	TWeakObjectPtr<UK2Node_SetNamedPrimitiveDataMulti> Node;
	TWeakPtr<IDetailLayoutBuilder> Builder;
	FDelegateHandle OfferedHandle;
};
