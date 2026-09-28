#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"

#include "AssetDefinition_PrimitiveDataIndex.generated.h"

/** Name, colour and Content Browser category for Primitive Data Index assets. */
UCLASS()
class UAssetDefinition_PrimitiveDataIndex : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};
