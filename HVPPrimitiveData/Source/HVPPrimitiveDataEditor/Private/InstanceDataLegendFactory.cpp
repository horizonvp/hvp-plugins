#include "InstanceDataLegendFactory.h"

#include "AssetTypeCategories.h"
#include "InstanceDataLegend.h"

#define LOCTEXT_NAMESPACE "InstanceDataLegendFactory"

UInstanceDataLegendFactory::UInstanceDataLegendFactory()
{
	SupportedClass = UInstanceDataLegend::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UInstanceDataLegendFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UInstanceDataLegend>(InParent, InClass, InName, Flags | RF_Transactional);
}

uint32 UInstanceDataLegendFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Materials;
}

FText UInstanceDataLegendFactory::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Instance Data Legend");
}

FText UInstanceDataLegendFactory::GetToolTip() const
{
	return LOCTEXT("Tooltip",
		"Name a layout of per instance custom data once. Bound materials get their PerInstanceCustomData nodes' "
		"indices written for them, and Set Named Instance Data picks parameters by name.");
}

#undef LOCTEXT_NAMESPACE
