#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "CodeAnimationWebFactory.generated.h"

/** Add > Blueprint > Code Animation Web: a Blueprint of UCodeAnimationWeb, without the class picker. */
UCLASS()
class UCodeAnimationWebFactory : public UFactory
{
	GENERATED_BODY()

public:
	UCodeAnimationWebFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTip() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual FString GetDefaultNewAssetName() const override;
};
