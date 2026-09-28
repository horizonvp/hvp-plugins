#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "PrimitiveDataIndexFactory.generated.h"

/** Content Browser > Add > Materials > Primitive Data Index. */
UCLASS()
class UPrimitiveDataIndexFactory : public UFactory
{
	GENERATED_BODY()

public:
	UPrimitiveDataIndexFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories() const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTip() const override;
};
