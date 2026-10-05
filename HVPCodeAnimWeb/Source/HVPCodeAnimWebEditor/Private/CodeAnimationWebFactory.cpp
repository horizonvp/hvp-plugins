#include "CodeAnimationWebFactory.h"

#include "AssetTypeCategories.h"
#include "CodeAnimationWeb.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "CodeAnimationWebFactory"

UCodeAnimationWebFactory::UCodeAnimationWebFactory()
{
	SupportedClass = UBlueprint::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UCodeAnimationWebFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
	UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	return FKismetEditorUtilities::CreateBlueprint(UCodeAnimationWeb::StaticClass(), InParent, Name, BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), CallingContext);
}

FText UCodeAnimationWebFactory::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Code Animation Web");
}

FText UCodeAnimationWebFactory::GetToolTip() const
{
	return LOCTEXT("Tooltip", "A set of states, the Animation Outputs they drive, and how to travel between them.");
}

uint32 UCodeAnimationWebFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Blueprint;
}

FString UCodeAnimationWebFactory::GetDefaultNewAssetName() const
{
	return TEXT("NewCodeAnimationWeb");
}

#undef LOCTEXT_NAMESPACE
