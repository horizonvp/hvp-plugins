#include "PrimitiveDataLegendFactory.h"

#include "AssetTypeCategories.h"
#include "PrimitiveDataLegend.h"

#define LOCTEXT_NAMESPACE "PrimitiveDataLegendFactory"

UPrimitiveDataLegendFactory::UPrimitiveDataLegendFactory()
{
	SupportedClass = UPrimitiveDataLegend::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UPrimitiveDataLegendFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UPrimitiveDataLegend>(InParent, InClass, InName, Flags | RF_Transactional);
}

uint32 UPrimitiveDataLegendFactory::GetMenuCategories() const
{
	// Beside materials: a legend is half of a material's interface.
	return EAssetTypeCategories::Materials;
}

FText UPrimitiveDataLegendFactory::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Primitive Data Legend");
}

FText UPrimitiveDataLegendFactory::GetToolTip() const
{
	return LOCTEXT("Tooltip",
		"Name a layout of custom primitive data once. Bound materials get their slot numbers written for them, "
		"and Set Named Primitive Data picks parameters by name.");
}

#undef LOCTEXT_NAMESPACE
