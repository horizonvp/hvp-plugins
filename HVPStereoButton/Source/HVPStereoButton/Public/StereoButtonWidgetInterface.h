#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "StereoButtonWidgetInterface.generated.h"

UINTERFACE(BlueprintType, Blueprintable, meta=(DisplayName="Stereo Button Widget"))
class HVPSTEREOBUTTON_API UStereoButtonWidgetInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implement this on the button's face widget to be told what the button is doing. Every function
 * is optional — an unimplemented one is a no-op. A widget that implements none of it still works:
 * the actor falls back to writing LabelText into a TextBlock named "LabelText" if there is one.
 */
class HVPSTEREOBUTTON_API IStereoButtonWidgetInterface
{
	GENERATED_BODY()

public:
	/** The label to show. Called on construction and whenever SetLabelText changes it. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Stereo Button")
	void SetButtonLabel(const FText& Label);

	/**
	 * Every tick. PressedAmount is 0..1 of PressDepth; bInContact is true while any finger is on
	 * the face; bPressed is true between the press and release edges; IdleSeconds counts up from
	 * the last contact (use it for a shimmer / attract state).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Stereo Button")
	void UpdateButtonState(float PressedAmount, bool bInContact, bool bPressed, float IdleSeconds);

	/** The press edge (true) or release edge (false) — the moment to flash, pulse, or click. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Stereo Button")
	void OnButtonEdge(bool bPressed);

	/**
	 * Every tick: where the viewer is relative to the face, for parallax. X is toward the viewer's
	 * left, Y is up, both as tangents of the view angle (0 = looking straight at the face, about
	 * 1 = 45 degrees off-axis). Follows the plate's tilt, so a tilted button parallaxes too.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Stereo Button")
	void UpdateButtonView(FVector2D ViewOffset);

	/** Live tint change requested through SetLabelColor. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Stereo Button")
	void SetButtonColor(const FLinearColor& Color);
};
