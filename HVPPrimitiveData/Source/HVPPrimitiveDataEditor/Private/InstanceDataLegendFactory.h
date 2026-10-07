#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "InstanceDataLegendFactory.generated.h"

/** Content Browser > Add > Materials > Instance Data Legend. */
UCLASS()
class UInstanceDataLegendFactory : public UFactory
{
	GENERATED_BODY()

public:
	UInstanceDataLegendFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories() const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTip() const override;
};
