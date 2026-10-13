#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HandGrabSubsystem.h"
#include "TextGrabBridge.generated.h"

class UTextRenderSubsystem;

// Letter is the world-wide letter index. Group and IndexInGroup locate it in its group, current at
// the moment of the event. Hand is the skinned mesh HandGrab found the hand on (HandGrabSubsystem's
// GetHandMesh); PinchLocation is the world-space grab point.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(FTextGrabEvent, int32, Letter, FName, Group, int32, IndexInGroup, int32, HandIndex, USkinnedMeshComponent*, Hand, FVector, PinchLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FTextGrabHoverEvent, int32, Letter, FName, Group, int32, IndexInGroup);

/**
 * Hands letters of the text subsystem to HandGrab. It does nothing until SubmitGrabbables is
 * called: add the component, then submit the letters that should be grabbable. While submitted, the
 * hands pick letters up by their glyph boxes (no collision, no component per letter), and a held
 * letter's pose is written back into the subsystem as data.
 *
 * The bridge decides nothing about what a grab means: it reports grabs, releases and hovers
 * through its events, and anything else (highlighting, letting motion skip held letters, putting
 * letters back) belongs to whoever listens.
 */
UCLASS(ClassGroup = (HVP), meta = (BlueprintSpawnableComponent))
class HVPTEXT_API UTextGrabBridge : public UActorComponent, public IHandGrabItemSource
{
	GENERATED_BODY()

public:
	UTextGrabBridge();

	/** HandGrab params for every submitted letter. GrabRadius is reach beyond the letter's box.
	    Defaults are HandGrab's recipe for small things pinched directly: held where pinched,
	    rigidly, turning with the wrist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Grab")
	FHandGrabbableParams GrabParams;

	/** On submit, let each hand hold its own letter (sets HandGrab's bAllowMultiGrab). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Grab")
	bool bAllowMultiGrab = true;

	/** Makes these letters grabbable, replacing any earlier submission. Letters already held stay
	    held if they are submitted again. */
	UFUNCTION(BlueprintCallable, Category = "Text Grab")
	void SubmitGrabbables(const TArray<int32>& Letters);

	/** SubmitGrabbables with every letter there is now. */
	UFUNCTION(BlueprintCallable, Category = "Text Grab")
	void SubmitAllLetters();

	/** SubmitGrabbables with every letter Group has now. Letters added to the group later are not
	    included until it is submitted again; removed ones drop out by themselves. */
	UFUNCTION(BlueprintCallable, Category = "Text Grab")
	void SubmitGroup(FName Group);

	/** SubmitGroup for several groups at once. */
	UFUNCTION(BlueprintCallable, Category = "Text Grab")
	void SubmitGroups(const TArray<FName>& Groups);

	/** Releases anything held and makes nothing grabbable. */
	UFUNCTION(BlueprintCallable, Category = "Text Grab")
	void ClearGrabbables();

	UFUNCTION(BlueprintPure, Category = "Text Grab")
	TArray<int32> GetGrabbables() const;

	UFUNCTION(BlueprintPure, Category = "Text Grab")
	bool IsLetterHeld(int32 Letter) const;

	UPROPERTY(BlueprintAssignable, Category = "Text Grab")
	FTextGrabEvent OnLetterGrabbed;

	UPROPERTY(BlueprintAssignable, Category = "Text Grab")
	FTextGrabEvent OnLetterReleased;

	/** First open hand came within reach of a letter. */
	UPROPERTY(BlueprintAssignable, Category = "Text Grab")
	FTextGrabHoverEvent OnLetterHoverBegin;

	/** Last hand left a letter's reach. */
	UPROPERTY(BlueprintAssignable, Category = "Text Grab")
	FTextGrabHoverEvent OnLetterHoverEnd;

	// IHandGrabItemSource
	virtual int32 FindGrabItem(const FVector& Probe, float MaxDistance,
		TFunctionRef<bool(int32)> IsAvailable, float& OutDistance) const override;
	virtual float GetGrabItemDistance(int32 Item, const FVector& Probe, FVector& OutClosest) const override;
	virtual bool GetGrabItemPose(int32 Item, FVector& OutLocation, FQuat& OutRotation) const override;
	virtual void SetGrabItemPose(int32 Item, const FVector& Location, const FQuat& Rotation) override;
	virtual void OnGrabItemGrabbed(int32 Item, int32 HandIndex, const FVector& PinchLocation) override;
	virtual void OnGrabItemReleased(int32 Item, int32 HandIndex, const FVector& PinchLocation) override;
	virtual void OnGrabItemHover(int32 Item, bool bHovered) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleLettersRebuilt();

	UFUNCTION()
	void HandleLettersReset();

	UFUNCTION()
	void HandleLetterRemoved(int32 Letter);

	void BroadcastGrab(const FTextGrabEvent& Event, int32 Item, int32 HandIndex, const FVector& PinchLocation);

	void Bind(bool bBind);
	void RefreshBoxes();
	bool IsItemUsable(int32 Item) const;
	/** Surface distance from a world point to an item's box (0 inside), cm; OutClosest receives the
	    nearest point on it. */
	float ItemBoxDistance(int32 Item, const FTransform& LetterWorld, const FVector& Point, FVector* OutClosest = nullptr) const;
	UHandGrabSubsystem* GetGrab() const;
	UTextRenderSubsystem* GetText() const;

	bool bBound = false;

	/** Item index -> letter index; INDEX_NONE once that letter was removed. Items are never
	    renumbered, so removing a letter does not disturb the hands holding others. */
	TArray<int32> Grabbables;

	/** Per item: the letter's box in its own frame (scale 1), and the radius around the frame
	    origin that contains it, for a cheap reject before the box test. */
	TArray<FBox> Boxes;
	TArray<float> Radii;
};
