#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StereoButtonFloatSpring.h"
#include "StereoButtonHandTracker.h"
#include "StereoButtonPressModel.h"
#include "StereoButtonBase.generated.h"

class UBoxComponent;
class UMaterialInterface;
class USoundBase;
class UStereoButtonLayerComponent;
class UTexture;
class UUserWidget;
class UWidgetComponent;
class AStereoButtonBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FStereoButtonPressed, int32, ButtonIndex, AStereoButtonBase*, Button);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FStereoButtonReleased, int32, ButtonIndex, AStereoButtonBase*, Button);

/** How the face is displayed. */
UENUM(BlueprintType)
enum class EStereoButtonSurface : uint8
{
	/** Stereo layer whenever the XR runtime offers one; the flat widget plane otherwise (editor, PIE without a headset). */
	Auto,
	/** Always the stereo layer. Invisible anywhere without an XR compositor. */
	StereoLayer,
	/** Always the flat widget plane. Useful to A/B the two on device. */
	WidgetPlane,
};

/**
 * A hand-pressable VR button whose face is a UMG widget.
 *
 * On device the widget is shown through a depth-composited stereo layer, so the text is as sharp
 * as the compositor can make it, yet hands and scene geometry still occlude it. In the editor the
 * same widget shows on a flat widget plane, so the button can be authored and previewed without a
 * headset. The whole face floats on a spring: a finger sinks and tilts it around the contact point,
 * and it eases back when released.
 *
 * Frame: the face is the local YZ plane at X = 0, normal +X toward the user. Fingers press along -X.
 *
 * Press detection is the presser latch ported from AHorizonButtonBase ("one poke = one press",
 * Docs/HorizonButton.md), kept in FStereoButtonPressModel. The visible motion is entirely separate
 * (FStereoButtonFloatSpring), so nothing about the feel of the physics can change what fires.
 *
 * Subclass in Blueprint, set FaceWidgetClass, bind OnButtonPressed. See Docs/StereoButton.md.
 */
UCLASS(Blueprintable, ClassGroup=(Interaction),
	meta=(DisplayName="Stereo Button Base",
	      PrioritizeCategories="\"Stereo Button|Debug\" \"Stereo Button\" \"Stereo Button|Face\" \"Stereo Button|Motion\" \"Stereo Button|Float\" \"Stereo Button|Audio\""))
class HVPSTEREOBUTTON_API AStereoButtonBase : public AActor
{
	GENERATED_BODY()

public:
	AStereoButtonBase();

	// --- Components -----------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> Root;

	/** The floating plate. Everything visible hangs off this; the spring moves it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> FloatRoot;

	/** Hosts the face widget and renders it to the render target the stereo layer shows. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UWidgetComponent> WidgetComp;

	/** The compositor layer. Same transform as WidgetComp, so what you see is where you press. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStereoButtonLayerComponent> StereoLayer;

	/** Overlap volume for the grab-sphere fallback presser. Sized to the face automatically. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UBoxComponent> PressVolume;

	// --- Identification -------------------------------------------------------------------

	/** Passed to both dispatchers so listeners can route by id. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button")
	int32 ButtonIndex = -1;

	// --- Face -----------------------------------------------------------------------------

	/** The widget drawn on the face. Implement IStereoButtonWidgetInterface on it to react to presses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Face")
	TSubclassOf<UUserWidget> FaceWidgetClass;

	/** Render-target size in pixels. The face is this divided by PixelsPerCm in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Face", meta=(ClampMin="16", ClampMax="4096"))
	FIntPoint FaceSizePx = FIntPoint(512, 192);

	/** Pixel density of the face. 512 px at 16 px/cm is a 32 cm wide button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Face", meta=(ClampMin="1", ClampMax="128"))
	float PixelsPerCm = 16.f;

	/**
	 * Render-target resolution multiplier: 2 = four times the texels at the same physical size.
	 * The widget component's draw size is BOTH its render target and its layout box, so the face
	 * widget lays out at FaceSizePx * this and has to be authored for that box - raising the
	 * supersample makes the widget's own space bigger, so fixed font sizes look smaller against it.
	 * The material is unaffected: it works in 0..1 UVs, so the pill keeps its shape and its corner
	 * radius stays the same fraction of the face whatever FacePx says.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Face", meta=(ClampMin="1", ClampMax="4"))
	float FaceSupersample = 2.f;

	/** Pushed to the widget via SetButtonLabel (or a TextBlock named "LabelText" as a fallback). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Face", meta=(MultiLine="true"))
	FText LabelText;

	/** Pushed to the widget via SetButtonColor. The widget decides what it means. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Face")
	FLinearColor LabelColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Face")
	EStereoButtonSurface Surface = EStereoButtonSurface::Auto;

	/**
	 * Applied to the widget plane while the stereo layer is showing the face, so the plane is not
	 * seen twice. Must be a translucent material with zero opacity — the plane has to keep
	 * rendering or its render target stops updating. Null = the plane stays visible (you will see
	 * a slightly blurrier ghost behind the layer; a warning is logged).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Face")
	TObjectPtr<UMaterialInterface> HiddenPlaneMaterial;

	/** Stereo layer priority. Higher draws over lower among layers; irrelevant to scene occlusion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Face")
	int32 LayerPriority = 0;

	// --- Motion ---------------------------------------------------------------------------

	/**
	 * Finger travel (cm) for a full press. Also the denominator that turns penetration into the
	 * 0..1 press amount, so it sets how hard the button is to actuate: required bone travel is
	 * PressThreshold * PressDepth - FingertipRadius. 4.0 is the deploy-tested value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(ClampMin="0.1"))
	float PressDepth = 4.f;

	/** Exponential decay of the press amount back to zero once the presser has left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(ClampMin="0"))
	float ReturnSpeed = 12.f;

	/** Press amount (0..1) at which OnButtonPressed fires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(ClampMin="0", ClampMax="1"))
	float PressThreshold = 0.85f;

	/** Press amount the channel must fall back below before OnButtonReleased fires (hysteresis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(ClampMin="0", ClampMax="1"))
	float ReleaseThreshold = 0.4f;

	/** Minimum seconds between two presses, button-wide. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(ClampMin="0"))
	float PressCooldownSeconds = 0.15f;

	/**
	 * How far IN FRONT of the face (and laterally past its edge) a latched presser must come back
	 * out before the contact breaks. The only way out: there is no back wall, so a finger punched
	 * clean through stays latched however deep it goes. That asymmetry is what killed the double press.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(ClampMin="0"))
	float ReArmClearanceCm = 2.f;

	/** Seconds the latched presser must be continuously absent before the channel unlatches. Absorbs dropped tracking frames. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(ClampMin="0"))
	float UnlatchGraceSeconds = 0.12f;

	/** Pressers that may be latched at once. 1 = first finger owns the button; 2 lets both hands work it independently. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(ClampMin="1", ClampMax="8"))
	int32 MaxConcurrentPressers = 1;

	/** Press from the player's real index fingertip, read off the pawn's hand mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion")
	bool bPressWithFingertips = true;

	/** Radius (cm) of the finger pad ahead of the tracked tip bone. Keep equal to the hand pointer's value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(EditCondition="bPressWithFingertips", ClampMin="0"))
	float FingertipRadius = 1.2f;

	/**
	 * Substring of the hand mesh CLASS name that marks the tracked rig. The pawn carries a tracked
	 * and a controller-posed rig per side; without this both supply a tip and one poke can double-press.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(EditCondition="bPressWithFingertips"))
	FName FingertipMeshClassFilter = FName(TEXT("OculusXRHand"));

	/** Two tips closer than this are one finger. Backstop for rigs the class filter doesn't know. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(EditCondition="bPressWithFingertips", ClampMin="0"))
	float FingertipMergeDistanceCm = 2.f;

	/**
	 * Fallback presser when no fingertip is available: an overlapping component whose name contains
	 * this (or carries it as a tag). Rejects the pawn capsule and world triggers. None = any primitive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion")
	FName PresserComponentFilter = FName(TEXT("HandGrabArea"));

	/** Extra margin (cm) on the press volume's face extents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(ClampMin="0"))
	float PressVolumePaddingCm = 0.f;

	/** Seconds after BeginPlay before presses register. The plate still reacts to touch meanwhile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion", meta=(ClampMin="0"))
	float EngageDelaySeconds = 0.f;

	/** Refuse to fire while this actor or any actor it is attached to is hidden in game. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion")
	bool bBlockPressWhenHidden = true;

	/** The first button in this group to press locks every button in it (itself included) until UnlockExclusiveGroup. None = off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Motion")
	FName ExclusiveGroup = NAME_None;

	// --- Float ----------------------------------------------------------------------------

	/** Off = the plate stays rigid at neutral. Press detection is unaffected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Float")
	bool bFloatEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Float", meta=(ShowOnlyInnerProperties))
	FStereoButtonFloatSettings FloatSettings;

	// --- Audio ----------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Audio")
	TObjectPtr<USoundBase> PressSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stereo Button|Audio")
	TObjectPtr<USoundBase> ReleaseSound;

	// --- Dispatchers ----------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category="Stereo Button")
	FStereoButtonPressed OnButtonPressed;

	UPROPERTY(BlueprintAssignable, Category="Stereo Button")
	FStereoButtonReleased OnButtonReleased;

	// --- API ------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category="Stereo Button")
	void SetLabelText(const FText& NewText);

	UFUNCTION(BlueprintCallable, Category="Stereo Button")
	void SetLabelColor(const FLinearColor& NewColor);

	/** Drops every latch, unlocks input, and rests the plate. */
	UFUNCTION(BlueprintCallable, Category="Stereo Button")
	void ResetButton();

	/** A full press + release from code, as if poked in the middle. Honors the lock, hidden and engage gates. */
	UFUNCTION(BlueprintCallable, Category="Stereo Button")
	void TriggerButton();

	/** Editor test button: press + release with no gates and a visible kick to the plate. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category="Stereo Button|Debug")
	void DebugFirePress();

	UFUNCTION(BlueprintPure, Category="Stereo Button")
	float GetPressedAmount() const { return PressModel.GetPressedAmount(); }

	/** True while any presser channel is between its press and release edges. */
	UFUNCTION(BlueprintPure, Category="Stereo Button")
	bool IsPressed() const { return PressModel.IsPressed(); }

	/** True while any presser is touching the face, pressed or not. */
	UFUNCTION(BlueprintPure, Category="Stereo Button")
	bool IsInContact() const { return PressModel.GetLiveChannelCount() > 0; }

	UFUNCTION(BlueprintPure, Category="Stereo Button")
	int32 GetActivePresserCount() const { return PressModel.GetLiveChannelCount(); }

	/** True once EngageDelaySeconds has elapsed since BeginPlay. Always true in the editor. */
	UFUNCTION(BlueprintPure, Category="Stereo Button")
	bool IsEngaged() const;

	UFUNCTION(BlueprintCallable, Category="Stereo Button")
	void SetInputLocked(bool bLocked) { bInputLocked = bLocked; }

	UFUNCTION(BlueprintPure, Category="Stereo Button")
	bool IsInputLocked() const { return bInputLocked; }

	/** Clears the lock on every button sharing this button's ExclusiveGroup, this one included. */
	UFUNCTION(BlueprintCallable, Category="Stereo Button")
	void UnlockExclusiveGroup();

	/** The live face widget, or null before it exists. */
	UFUNCTION(BlueprintPure, Category="Stereo Button")
	UUserWidget* GetFaceWidget() const;

	/** Face size in centimetres. */
	UFUNCTION(BlueprintPure, Category="Stereo Button")
	FVector2D GetFaceSizeCm() const;

	/** True while the stereo layer (not the widget plane) is showing the face. */
	UFUNCTION(BlueprintPure, Category="Stereo Button")
	bool IsUsingStereoLayer() const { return bStereoLayerLive; }

	/** Seconds since a presser last touched the face. */
	UFUNCTION(BlueprintPure, Category="Stereo Button")
	float GetIdleSeconds() const { return PressModel.GetTimeSinceInteraction(); }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

	/**
	 * May one continuous contact fire again at its release edge? Base: no — the "one poke = one
	 * press" guarantee. Override to true only where an occasional extra press is harmless.
	 */
	virtual bool ShouldRearmOnRelease() const { return false; }

	/** Re-lays the widget plane, the layer and the press volume from FaceSizePx / PixelsPerCm. */
	void ApplyLayout();

private:
	void SamplePressers(TArray<FStereoButtonPresserSample>& OutSamples);
	void FireEdges(const TArray<FStereoButtonPressEdge>& Edges);
	void StepFloat(float DeltaSeconds, const TArray<FStereoButtonPresserSample>& Samples);
	void ReconcileSurface();
	void PushLabel();
	void PushColor();
	void PushWidgetState();
	/**
	 * Pushes ViewOffset / PressAmount straight into the face material, so a widget needs no
	 * Blueprint to get parallax and press feedback: an Image named "FaceImage" gets a dynamic
	 * material instance with vector "ViewOffset" and scalar "PressAmount" set every tick.
	 */
	void PushFaceMaterialParams();
	/** Viewer position relative to the (tilted) plate as view-angle tangents; zero when unknown. */
	FVector2D ComputeViewOffset() const;
	void PlayOneShot(USoundBase* Cue) const;
	void LockExclusiveGroupPeers();
	bool IsEffectivelyHidden() const;
	bool WantsStereoLayer() const;

	FStereoButtonPressModel PressModel;
	FStereoButtonHandTracker HandTracker;
	FStereoButtonFloatSpring FloatSpring;

	/** Reused every tick so the common case stops allocating after the first frame. */
	TArray<FStereoButtonPresserSample> ScratchSamples;
	TArray<FStereoButtonPressEdge> ScratchEdges;
	TArray<FVector> ScratchTips;

	/** What the layer currently shows; compared by pointer so SetTexture is called only on change. */
	TWeakObjectPtr<UTexture> LayerTexture;

	float TimeSinceBeginPlay = 0.f;
	FVector2D ViewOffset = FVector2D::ZeroVector;
	bool bInputLocked = false;
	bool bStereoLayerLive = false;
	bool bWarnedNoHiddenMaterial = false;
	bool bLastPressed = false;
};
