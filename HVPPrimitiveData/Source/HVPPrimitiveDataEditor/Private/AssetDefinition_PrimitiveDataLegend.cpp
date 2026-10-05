#include "AssetDefinition_PrimitiveDataLegend.h"

#include "PrimitiveDataLegend.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_PrimitiveDataLegend"

FText UAssetDefinition_PrimitiveDataLegend::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "Primitive Data Legend");
}

FLinearColor UAssetDefinition_PrimitiveDataLegend::GetAssetColor() const
{
	// Teal: related to materials' green without being mistaken for one.
	return FLinearColor(FColor(48, 190, 170));
}

TSoftClassPtr<UObject> UAssetDefinition_PrimitiveDataLegend::GetAssetClass() const
{
	return UPrimitiveDataLegend::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_PrimitiveDataLegend::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { EAssetCategoryPaths::Material };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
