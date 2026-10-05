#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "PrimitiveDataLegendFactory.generated.h"

/** Content Browser > Add > Materials > Primitive Data Legend. */
UCLASS()
class UPrimitiveDataLegendFactory : public UFactory
{
	GENERATED_BODY()

public:
	UPrimitiveDataLegendFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories() const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTip() const override;
};
