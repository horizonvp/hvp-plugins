#include "AssetDefinition_PrimitiveDataIndex.h"

#include "PrimitiveDataIndex.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_PrimitiveDataIndex"

FText UAssetDefinition_PrimitiveDataIndex::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "Primitive Data Index");
}

FLinearColor UAssetDefinition_PrimitiveDataIndex::GetAssetColor() const
{
	// Teal: related to materials' green without being mistaken for one.
	return FLinearColor(FColor(48, 190, 170));
}

TSoftClassPtr<UObject> UAssetDefinition_PrimitiveDataIndex::GetAssetClass() const
{
	return UPrimitiveDataIndex::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_PrimitiveDataIndex::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { EAssetCategoryPaths::Material };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
