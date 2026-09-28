#include "PrimitiveDataIndexFactory.h"

#include "AssetTypeCategories.h"
#include "PrimitiveDataIndex.h"

#define LOCTEXT_NAMESPACE "PrimitiveDataIndexFactory"

UPrimitiveDataIndexFactory::UPrimitiveDataIndexFactory()
{
	SupportedClass = UPrimitiveDataIndex::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UPrimitiveDataIndexFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UPrimitiveDataIndex>(InParent, InClass, InName, Flags | RF_Transactional);
}

uint32 UPrimitiveDataIndexFactory::GetMenuCategories() const
{
	// Beside materials: an index is half of a material's interface.
	return EAssetTypeCategories::Materials;
}

FText UPrimitiveDataIndexFactory::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Primitive Data Index");
}

FText UPrimitiveDataIndexFactory::GetToolTip() const
{
	return LOCTEXT("Tooltip",
		"Name a layout of custom primitive data once. Bound materials get their slot numbers written for them, "
		"and Set Indexed Primitive Data picks parameters by name.");
}

#undef LOCTEXT_NAMESPACE
