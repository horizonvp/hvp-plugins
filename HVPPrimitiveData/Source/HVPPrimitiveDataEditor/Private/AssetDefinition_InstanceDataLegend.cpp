#include "AssetDefinition_InstanceDataLegend.h"

#include "InstanceDataLegend.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_InstanceDataLegend"

FText UAssetDefinition_InstanceDataLegend::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "Instance Data Legend");
}

FLinearColor UAssetDefinition_InstanceDataLegend::GetAssetColor() const
{
	// A bluer teal than the Primitive Data Legend's: the same family, told apart at a glance.
	return FLinearColor(FColor(48, 150, 200));
}

TSoftClassPtr<UObject> UAssetDefinition_InstanceDataLegend::GetAssetClass() const
{
	return UInstanceDataLegend::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_InstanceDataLegend::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { EAssetCategoryPaths::Material };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
