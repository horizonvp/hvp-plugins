#pragma once

#include "CoreMinimal.h"
#include "CodeAnimationWebTypes.h"
#include "IDetailCustomization.h"

class IBlueprintEditor;
class UBlueprint;

/**
 * Adds "Animation Output" and its lerp policy to the Details of a Web's own variables.
 *
 * Both are stored as variable metadata, which reaches the compiled class; the Web bakes them into its
 * output list when it compiles, since cooked builds have no metadata to read.
 */
class FCodeAnimOutputVariableDetails : public IDetailCustomization
{
public:
	/** Null for anything but a Code Animation Web, so other Blueprints' variables are left alone. */
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> BlueprintEditor);

	explicit FCodeAnimOutputVariableDetails(UBlueprint* InBlueprint) : Blueprint(InBlueprint) {}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

private:
	bool IsOutput() const;
	void SetOutput(bool bOutput);

	ECodeAnimLerpPolicy GetLerp() const;
	void SetLerp(ECodeAnimLerpPolicy Lerp);

	TWeakObjectPtr<UBlueprint> Blueprint;
	FName VariableName;
};
