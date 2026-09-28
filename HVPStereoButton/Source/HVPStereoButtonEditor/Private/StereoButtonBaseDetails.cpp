#include "StereoButtonBaseDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"

TSharedRef<IDetailCustomization> FStereoButtonBaseDetails::MakeInstance()
{
	return MakeShared<FStereoButtonBaseDetails>();
}

void FStereoButtonBaseDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// ECategoryPriority::Important places these right after Transform and above
	// all the default-priority component categories that get merged in.
	static const FName Cats[] =
	{
		TEXT("Stereo Button|Debug"),
		TEXT("Stereo Button"),
		TEXT("Stereo Button|Face"),
		TEXT("Stereo Button|Motion"),
		TEXT("Stereo Button|Float"),
		TEXT("Stereo Button|Audio"),
	};

	int32 Order = 0;
	for (const FName& Cat : Cats)
	{
		DetailBuilder
			.EditCategory(Cat, FText::GetEmpty(), ECategoryPriority::Important)
			.SetSortOrder(int32(ECategoryPriority::Important) * 1000 + Order++);
	}
}
