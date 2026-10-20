#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/Interface.h"
#include "HandGrabSubsystem.generated.h"

class UPrimitiveComponent;
class USkinnedMeshComponent;
class APawn;

HVPHANDGRAB_API DECLARE_LOG_CATEGORY_EXTERN(LogHVPHandGrab, Log, All);

// A grab/release moment. Component is the registered grabbable; HandIndex identifies which
// tracked hand (stable within a pawn's lifetime, reshuffled if the pawn or its hand meshes
// change); PinchLocation is the world-space pinch midpoint at the moment of the event.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FHandGrabEvent, UPrimitiveComponent*, Component, int32, HandIndex, FVector, PinchLocation);

// Hover affordance edges. Fired when a component gains its first hovering hand and when it
// loses its last one, so a consumer can toggle a "you can pick this up" visual without
// caring how many hands are near.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FHandGrabHoverEvent, UPrimitiveComponent*, Component);

// Per-grabbable tuning, supplied at registration.
USTRUCT(BlueprintType)
struct FHandGrabbableParams
{
	GENERATED_BODY()

	// How close (cm) a pinch must start to this component's COLLISION SURFACE to grab it
	// — any point inside the collision volume counts as distance 0, so the whole body is
	// directly grabbable and this radius is the extra reach beyond it. Also the open-hand
	// hover radius. Components without usable collision fall back to center-of-volume
	// (bounds center) distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hand Grab",
		meta=(ClampMin="1"))
	float GrabRadius = 14.f;

	// While held, the subsystem chases the component toward the pinch midpoint at this
	// exponential interp speed. Lower = floatier; higher = glued to the fingers.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hand Grab",
		meta=(ClampMin="0.5"))
	float FollowSpeed = 18.f;

	// When false the subsystem never moves the component — it only reports grab/release,
	// and the consumer drives the motion itself (e.g. constrained sliders, levers).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hand Grab")
	bool bFollow = true;

	// Hold the item at EXACTLY the offset it had from the pinch at the moment of the grab,
	// instead of reeling it in until its grabbed surface point meets the fingers.
	//
	// The default (false) anchors on the nearest point of the collision body. Since GrabRadius
	// is reach BEYOND that surface — and hover-memory reaches further still — most grabs close
	// outside the body, so the item travels to the hand. On a large item held near its surface
	// that reads as taking hold of it; on a small one it reads as the item snapping into the
	// middle of your hand, which is what this flag exists to stop.
	//
	// Costs the reel-in: an item grabbed at arm's length keeps hanging at arm's length. Best
	// for small items the player pinches directly rather than plucks from a distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hand Grab")
	bool bPreserveGrabOffset = false;

	// Move the item to the follow target outright instead of easing toward it at FollowSpeed.
	//
	// FollowSpeed is an exponential interp, so it lags by its own time constant (1/FollowSpeed
	// seconds — at the default 18 that is ~55 ms, about four frames at 72 Hz) whenever the hand
	// is moving. That softness is wanted when the item is being reeled in, and reads as drag
	// when the item is supposed to already be in your hand. Pair with bPreserveGrabOffset for
	// an item that stays exactly where you took hold of it.
	//
	// Does NOT remove the one frame of latency inherent in reading the hand's pose (see
	// Tick) — it removes the smoothing on top of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hand Grab")
	bool bRigidFollow = false;

	// Turn the held item WITH the hand, relative to their poses at the moment of the grab, so
	// pickup never snaps it to a canonical alignment and it rolls naturally with the wrist.
	//
	// Owned here, next to the position follow, ON PURPOSE. A consumer can read GetHandRotation
	// and write the rotation from its own tick, but actor ticks run in TG_PrePhysics and this
	// subsystem runs after TG_PostPhysics — so that rotation is always a frame staler than the
	// position, the grip lever swings by one frame of wrist motion every frame, and the item
	// visibly slides through the fingers while the hand moves. Doing both here, rotation then
	// position, from one read of the hand, is what keeps the grip on the pinch.
	//
	// Eases at FollowSpeed unless bRigidFollow, matching the position channel either way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hand Grab")
	bool bFollowRotation = false;

	// Take this only with a pinch at least this tight: thumb to index/middle within this many cm
	// (reference-hand cm, like PinchStartDistance, so it scales with the hand). 0 = any pinch that
	// closes the gesture. For small things, where the subsystem's 3 cm close would grab from a loose,
	// half-formed pinch. A hand that closed in reach but too loose takes it the moment it tightens
	// past this, no further squeeze needed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hand Grab", meta=(ClampMin="0"))
	float MaxPinchDistance = 0.f;

	// Let a closed fist (or a controller grip squeeze) take this. Off: pinches only.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hand Grab")
	bool bAllowFistGrab = true;
};

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UHandGrabItemSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Many grabbables that are not components: instances, particles, entries in a consumer's own
 * array. The source owns the items' shapes and poses; the subsystem runs the same gesture,
 * forgiveness and follow on them that it runs on registered components, and addresses each
 * item by an index the source chooses (keep it stable while the item may be held or hovered —
 * call ReleaseItems before reshuffling).
 *
 * Exists so a few thousand small things can be grabbable without a component (and a collision
 * body) each: the source answers "nearest item to this point" however suits its data, rather
 * than the subsystem scanning every item. Register with RegisterItemSource. C++ only.
 */
class HVPHANDGRAB_API IHandGrabItemSource
{
	GENERATED_BODY()

public:
	// The nearest item whose SURFACE lies within MaxDistance (cm) of Probe (distance 0 inside
	// it, as for a component's collision), skipping items IsAvailable rejects (held ones).
	// INDEX_NONE if none; OutDistance receives its distance.
	virtual int32 FindGrabItem(const FVector& Probe, float MaxDistance,
		TFunctionRef<bool(int32)> IsAvailable, float& OutDistance) const = 0;

	// Surface distance from Probe to Item (0 inside) and the nearest surface point (Probe itself
	// when inside). Negative if the item no longer exists.
	virtual float GetGrabItemDistance(int32 Item, const FVector& Probe, FVector& OutClosest) const = 0;

	// World pose of Item. False if it no longer exists (a held item is then let go).
	virtual bool GetGrabItemPose(int32 Item, FVector& OutLocation, FQuat& OutRotation) const = 0;

	// Called from the subsystem's tick (after TG_PostPhysics) while Item is held with bFollow:
	// show the item at this pose THIS frame — deferring it to the next tick of the source adds a
	// frame of lag behind the hand.
	virtual void SetGrabItemPose(int32 Item, const FVector& Location, const FQuat& Rotation) = 0;

	// The item counterparts of OnGrabbed / OnReleased / OnHoverBegin / OnHoverEnd.
	virtual void OnGrabItemGrabbed(int32 Item, int32 HandIndex, const FVector& PinchLocation) {}
	virtual void OnGrabItemReleased(int32 Item, int32 HandIndex, const FVector& PinchLocation) {}
	virtual void OnGrabItemHover(int32 Item, bool bHovered) {}
};

// What a hand holds or hovers: a registered component, or one item of a registered item source.
struct FHandGrabTarget
{
	TWeakObjectPtr<UPrimitiveComponent> Comp;
	TWeakObjectPtr<UObject> Source;
	int32 Item = INDEX_NONE;

	static FHandGrabTarget OfComponent(UPrimitiveComponent* InComp)
	{
		FHandGrabTarget T;
		T.Comp = InComp;
		return T;
	}

	static FHandGrabTarget OfItem(UObject* InSource, int32 InItem)
	{
		FHandGrabTarget T;
		T.Source = InSource;
		T.Item = InItem;
		return T;
	}

	bool IsItem() const { return Item != INDEX_NONE; }

	// Still refers to something (the component, or the source, is alive).
	bool IsValid() const { return IsItem() ? Source.IsValid() : Comp.IsValid(); }

	void Reset() { *this = FHandGrabTarget(); }

	bool operator==(const FHandGrabTarget& Other) const
	{
		return Comp == Other.Comp && Source == Other.Source && Item == Other.Item;
	}

	friend uint32 GetTypeHash(const FHandGrabTarget& T)
	{
		return HashCombine(HashCombine(GetTypeHash(T.Comp), GetTypeHash(T.Source)), ::GetTypeHash(T.Item));
	}
};

// Where a hand's carry point is measured from. The live choice tracks the gesture each frame;
// the choice in force at the moment of a grab is FROZEN for that hold (see FHandGrabHand).
enum class EHandGrabCarry : uint8
{
	PinchIndex,   // thumb-tip / index-tip midpoint
	PinchMiddle,  // thumb-tip / middle-tip midpoint (thumb nearer the middle finger)
	Palm,         // palm-centre marker (fist)
};

// One tracked VR hand: a skinned mesh on the player pawn carrying both a thumb-tip and an
// index-tip bone. Plain struct — transient, rebuilt whenever the pawn or its meshes change.
// One digit's tip on a hand mesh: a marker bone where the mesh has one, else estimated from the
// digit's last three joints (some meshes ship without the markers).
struct FHandGrabDigit
{
	FName Tip;                                  // *_tip / *_null marker; NAME_None = estimate
	FName Joint1, Joint2, Joint3;               // the last three joints, for the estimate
	float DistalRatio = 0.9f;                   // tip distance past Joint3, in Joint2->Joint3 lengths
	bool IsValid() const { return !Tip.IsNone() || !Joint3.IsNone(); }
};

struct FHandGrabHand
{
	TWeakObjectPtr<USkinnedMeshComponent> Mesh;
	// Thumb, index, middle, ring, pinky.
	FHandGrabDigit Digits[5];
	FName ThumbTipBone;
	FName IndexTipBone;
	FName MiddleTipBone;           // NAME_None = thumb-middle pinch / middle curl not checked
	FName ThumbPadBone;            // finger-pad markers (Meta hand meshes carry them); NAME_None = tips only
	FName IndexPadBone;
	FName MiddlePadBone;
	FName MiddleKnuckleBone;       // wrist -> this is the palm length the thresholds scale by
	FName PalmBone;                // palm-center marker, the fist grab point; NAME_None = wrist/fingertip midpoint
	FName WristBone;               // NAME_None disables fist detection for this hand
	bool bPinchClosed = false;     // thumb-to-index/middle pinch, with hysteresis
	bool bFistClosed = false;      // curled fist (index/middle tip near wrist), with hysteresis
	bool bPinching = false;        // overall "grab gesture closed" (pinch OR fist), release-debounced
	FVector PinchMid = FVector::ZeroVector;   // current grab point (pinch mid / palm)
	// Offset from the held component's ORIGIN to the point actually grabbed, expressed in the
	// component's rotation frame and deliberately UNSCALED — the follow anchors it to the pinch.
	// Rotation-relative so the item pivots about the grip; scale-free so a consumer animating
	// the item's scale while it is held (a seat-to-carry pose blend, a hover pop) doesn't drag
	// the grip across the mesh and lurch the item through the player's fingers.
	FVector HeldGrabLocal = FVector::ZeroVector;
	// bFollowRotation reference frame: the item's and the hand's world rotations at the grab.
	// Each tick the item is set to (hand now * hand then^-1) * item then.
	FQuat HeldGrabItemQuat = FQuat::Identity;
	FQuat HeldGrabHandQuat = FQuat::Identity;
	bool bHeldGrabRotValid = false; // hand rotation was readable at the grab
	// The pose the follow set on Held this tick (GetGrabbedTransform). Cleared each tick before
	// the follow runs, so it is never a stale pose from an earlier frame or an earlier hold.
	FTransform FollowPose = FTransform::Identity;
	bool bHasFollowPose = false;
	// Carry-point source: LiveCarry is re-chosen every frame from the gesture; HeldCarry is
	// LiveCarry as it stood when the grab closed, and is what PinchMid is measured from for
	// as long as Held is valid. Measured on device: a holding hand's fist metric sits inside
	// the fist hysteresis band, so the gate flips as the fingers settle and the live carry
	// point hops between the fingertips and the palm — ~5 cm, in one frame, about a second
	// into most holds. The item follows it and visibly snaps in the hand. The gates still
	// decide WHETHER the grip is closed; while something is held they no longer move WHERE
	// it hangs.
	EHandGrabCarry LiveCarry = EHandGrabCarry::PinchIndex;
	EHandGrabCarry HeldCarry = EHandGrabCarry::PinchIndex;
	FHandGrabTarget Held;
	FHandGrabTarget Hovered;

	// Live hand-size factor (palm length / reference palm length) the cm thresholds scale by.
	float SizeScale = 1.f;

	// This frame's gesture measurements (cm, already divided by SizeScale — i.e. in
	// reference-hand units, directly comparable to the thresholds).
	float PinchMetric = TNumericLimits<float>::Max();
	float FistMetric = TNumericLimits<float>::Max();

	// Probe points for the grab search: every place the item might actually be touching.
	FVector ThumbTip = FVector::ZeroVector;
	FVector IndexTip = FVector::ZeroVector;
	FVector MiddleTip = FVector::ZeroVector;
	FVector PalmPoint = FVector::ZeroVector;
	bool bHasMiddleTip = false;

	// --- Forgiveness bookkeeping ---

	// Seconds since the gesture closed (only meaningful while bPinching).
	float TimeSinceClose = 0.f;

	// Seconds the raw gesture has read OPEN while bPinching is still latched (release debounce).
	float TimeOpen = 0.f;

	// The last grabbable this hand hovered while open, and how long ago — a grab that closes
	// a moment after the hand drifted (closing the fingers moves the grab point) still takes it.
	FHandGrabTarget RecentHover;
	float TimeSinceRecentHover = TNumericLimits<float>::Max();

	// Closed-but-empty "squeeze" re-arm: a hand that arrives already half-closed (so the close
	// edge happened out of reach) grabs when it tightens further while an item is in reach.
	// Tightest reference-unit metrics seen since the close edge / since coming into reach.
	float SqueezeRefPinch = TNumericLimits<float>::Max();
	float SqueezeRefFist = TNumericLimits<float>::Max();
	bool bSqueezeArmed = false;
	// Closed with something in reach that its registration refused only for the gesture (too loose a
	// pinch, a fist where pinches are asked for): grab it once the gesture qualifies.
	bool bTightenPending = false;

	float LogCooldown = 0.f;
};

// One registered grabbable.
struct FHandGrabbable
{
	TWeakObjectPtr<UPrimitiveComponent> Comp;
	FHandGrabbableParams Params;
	bool bEnabled = true;
};

// One registered item source; its Params apply to every one of its items.
struct FHandGrabItemSourceEntry
{
	TWeakObjectPtr<UObject> Object;
	IHandGrabItemSource* Interface = nullptr;  // Object's implementation; valid while Object is
	FHandGrabbableParams Params;
	bool bEnabled = true;
};

/**
 * World-level pinch-grab: the HVP hand-interaction primitive extracted from BiogenLupus's
 * LupusCycle actor.
 *
 * The subsystem discovers the player pawn's hands itself — any USkinnedMeshComponent on the
 * pawn carrying both a *thumb*tip* and an *index*tip* bone is a hand (the same bone
 * discovery HVPButtonBase uses for its fingertip pressers) — so grabbing needs no
 * per-hand or per-pawn setup, and works for tracked hands and controller-animated hand
 * meshes alike. The grab gesture is a PINCH (thumb tip or pad meeting the index OR middle
 * tip or pad — covers tip pinches, pad pinches and the "fingertips gathered" grip) or a
 * closed FIST (index or middle tip curled to the wrist), each with hysteresis so tracking
 * jitter can't machine-gun grab/drop. By default only one grabbable can be held at a time
 * across all hands (bAllowMultiGrab).
 *
 * Forgiveness, aimed at slow and deliberate grabs (see Docs/HandGrab.md):
 *  - Thresholds are in cm for the reference hand and scale with the live palm length, so
 *    large and small (calibrated) hands close the gesture at the same pose.
 *  - The grab search probes the fingertips and palm as well as the grab point, since
 *    closing the fingers moves the grab point away from where the item is.
 *  - A grab is still accepted for GrabGraceSeconds after the gesture closes, and a hand that
 *    was hovering an item within HoverMemorySeconds takes it even if the close drifted.
 *  - A hand that arrives already half-closed grabs when it squeezes further in reach.
 *  - Release waits for the gesture to read open for ReleaseDebounceSeconds, so a dropped
 *    tracking frame mid-carry doesn't drop the item.
 *
 * Consumers REGISTER primitive components as grabbables and bind the events:
 *
 *   auto* Grab = GetWorld()->GetSubsystem<UHandGrabSubsystem>();
 *   Grab->RegisterGrabbable(MeshComp, Params);          // radius, follow speed
 *   Grab->OnGrabbed.AddDynamic(this, &AMyActor::HandleGrabbed);
 *   Grab->OnReleased.AddDynamic(this, &AMyActor::HandleReleased);  // do your snap logic here
 *
 * While held (and bFollow), the subsystem chases the component toward the pinch midpoint;
 * on pinch-open it broadcasts OnReleased and lets go — what happens next (snap, return,
 * drop) is the consumer's business. OnHoverBegin/End bracket the open-hand-in-reach
 * affordance. Each hand holds at most one component; a held component can't be taken by
 * the other hand.
 *
 * Grabbables that are not components (instances, particles) come from an IHandGrabItemSource
 * registered with RegisterItemSource: the same gesture, forgiveness and follow, with the
 * source's own nearest-item search and pose read/write in place of collision and transforms.
 *
 * Game worlds only; ticks itself (no consumer ticking required).
 */
UCLASS()
class HVPHANDGRAB_API UHandGrabSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- Pinch tuning (global — the gesture is a property of the player, not of any one
	// grabbable). Defaults suit Quest hand tracking; adjust from Blueprint/C++ if needed. ---

	// Pinch distance (cm, reference-size hand) at which a pinch STARTS. The pinch distance is
	// the CLOSEST of thumb tip/pad to index tip/pad and thumb tip/pad to middle tip/pad (see
	// bPinchWithMiddle / bUsePadMarkers), so a pad-to-pad pinch — whose fingertip ENDS stay
	// 2-3 cm apart — registers too. Was 2.5 on tips only.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float PinchStartDistance = 3.f;

	// Distance (cm) at which a held pinch RELEASES. Kept wider than the start threshold
	// (hysteresis) so tracking jitter can't machine-gun grab/drop.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float PinchReleaseDistance = 5.5f;

	// Count the thumb meeting the MIDDLE finger as a pinch as well as the index — the
	// "fingertips gathered" grip people use for small objects often lands the thumb on the
	// middle finger or between the two.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	bool bPinchWithMiddle = true;

	// Measure pinches pad-to-pad as well as tip-to-tip when the hand mesh has finger-pad
	// markers (the Meta hand meshes do). The closer of the two wins.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	bool bUsePadMarkers = true;

	// Also treat a closed FIST as the grab gesture (index or middle tip curled in toward the
	// wrist). Covers whole-hand grabs in hand tracking, and means a controller grip squeeze —
	// which animates the hand mesh into a fist — grabs too.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	bool bEnableFistGrab = true;

	// Fingertip-to-wrist distance (cm, reference-size hand; closer of index and middle) at
	// which a fist CLOSES. The reference hand reads ~19 cm open, ~14-16 relaxed, ~8 in a fist.
	// Was 10 on the index only.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float FistCloseDistance = 11.f;

	// Fingertip-to-wrist distance (cm) at which a held fist OPENS (hysteresis, like the
	// pinch thresholds).
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float FistOpenDistance = 13.5f;

	// --- Size normalization ---

	// Scale the cm thresholds by the live hand size: wrist-to-middle-knuckle length divided by
	// ReferencePalmLength. Hand-size calibration makes a big hand's mesh bigger, and without
	// this its pinch has to close proportionally tighter to count.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	bool bScaleThresholdsByHandSize = true;

	// Wrist-to-middle-knuckle length (cm) the thresholds above were chosen for — the
	// project's Hand_*_SKM at scale 1 (Docs/HandSizing.md: 9.57).
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float ReferencePalmLength = 9.57f;

	// Clamp on the size factor, so a glitched bone reading can't blow the thresholds up.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float MinHandSizeScale = 0.75f;

	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float MaxHandSizeScale = 1.4f;

	// --- Forgiveness (timing) ---

	// After the gesture closes without finding anything, keep looking for this long (s) —
	// covers closing a beat before the hand arrives, and tracking lag on slow reaches. Short
	// on purpose: a closed hand sweeping through items afterwards must not pick them up.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float GrabGraceSeconds = 0.3f;

	// A component the OPEN hand hovered within this many seconds before the gesture closed is
	// still grabbed if nothing is in reach at the close — closing the fingers moves the grab
	// point, often just out of range of the item the player was aiming at.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float HoverMemorySeconds = 0.35f;

	// ...as long as it is still within its GrabRadius times this.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float HoverMemoryReachScale = 1.5f;

	// A hand that is ALREADY closed (edge happened out of reach, grace expired, nothing held)
	// grabs an in-reach item when the pinch tightens by this much (cm, reference hand)...
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float SqueezePinchDelta = 0.75f;

	// ...or the fist tightens by this much (cm, reference hand). <= 0 disables squeeze grabs.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float SqueezeFistDelta = 1.5f;

	// The gesture must read open continuously for this long (s) before a held item is
	// released. Hand tracking drops or mis-reads single frames during slow, careful motion.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float ReleaseDebounceSeconds = 0.1f;

	// When false (the default), only ONE grabbable can be held at a time across all hands —
	// a second hand can't pick anything up (and shows no hover affordance) until the first
	// lets go. Set true to let each hand hold its own grabbable.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	bool bAllowMultiGrab = false;

	// Two tracked "hands" whose pinch points sit closer than this (cm) are the SAME
	// physical hand seen through duplicate skinned meshes (tracked-hand mesh +
	// controller-animated copy, both carrying the finger bones). However many meshes
	// represent it, a physical hand holds at most ONE item — without this gate,
	// bAllowMultiGrab would let each duplicate take its own grabbable into one fist.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	float SamePhysicalHandDistance = 8.f;

	// Draw pinch points (green while pinching), fingertip probes, and grab radii.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	bool bDrawDebug = false;

	// Log each hand's gesture metrics (reference-hand cm) a few times a second while a
	// grabbable is within reach, plus every grab/release with the reason — for tuning the
	// thresholds on device. Also switchable at runtime: HandGrab.LogGestures 1.
	UPROPERTY(BlueprintReadWrite, Category="Hand Grab")
	bool bLogGestures = false;

	// --- Events ---

	// A registered component was grabbed (pinch closed within its GrabRadius).
	UPROPERTY(BlueprintAssignable, Category="Hand Grab")
	FHandGrabEvent OnGrabbed;

	// A held component was let go (pinch opened, or its hand/pawn went away). NOT fired by
	// ForceRelease — a consumer calling that is taking control and needs no echo.
	UPROPERTY(BlueprintAssignable, Category="Hand Grab")
	FHandGrabEvent OnReleased;

	// First open hand came within GrabRadius of a grabbable / last one left.
	UPROPERTY(BlueprintAssignable, Category="Hand Grab")
	FHandGrabHoverEvent OnHoverBegin;

	UPROPERTY(BlueprintAssignable, Category="Hand Grab")
	FHandGrabHoverEvent OnHoverEnd;

	// --- Registration ---

	// Make Component pinch-grabbable. Re-registering an already-registered component just
	// updates its params (and keeps its enabled state).
	UFUNCTION(BlueprintCallable, Category="Hand Grab")
	void RegisterGrabbable(UPrimitiveComponent* Component, const FHandGrabbableParams& Params);

	// Remove Component from the system. If a hand is holding it, it is silently let go
	// (no OnReleased — unregistering is the consumer taking control).
	UFUNCTION(BlueprintCallable, Category="Hand Grab")
	void UnregisterGrabbable(UPrimitiveComponent* Component);

	// Gate a grabbable without unregistering it — e.g. disable while it animates into a
	// slot. Disabling does not release a hand already holding it (use ForceRelease).
	UFUNCTION(BlueprintCallable, Category="Hand Grab")
	void SetGrabbableEnabled(UPrimitiveComponent* Component, bool bEnabled);

	// Pry Component out of whatever hand holds it, WITHOUT broadcasting OnReleased. No-op
	// if nothing holds it.
	UFUNCTION(BlueprintCallable, Category="Hand Grab")
	void ForceRelease(UPrimitiveComponent* Component);

	// Make every item of Source grabbable (see IHandGrabItemSource), with one set of params for
	// all of them. Source must implement IHandGrabItemSource. Re-registering updates the params.
	void RegisterItemSource(UObject* Source, const FHandGrabbableParams& Params);

	// Remove Source; any of its items held are let go silently (as UnregisterGrabbable).
	void UnregisterItemSource(UObject* Source);

	// Gate every item of Source without unregistering it (does not release held ones).
	void SetItemSourceEnabled(UObject* Source, bool bEnabled);

	// Let go of (and stop hovering) every item of Source WITHOUT broadcasting — for a source
	// about to rebuild or renumber its items.
	void ReleaseItems(UObject* Source);

	// Pry item Item of Source out of whatever hand holds it, WITHOUT broadcasting — the item
	// counterpart of ForceRelease, for a source taking one item over (snapping it somewhere, say).
	// Other items stay held. No-op if nothing holds it.
	void ForceReleaseItem(UObject* Source, int32 Item);

	// The world positions of a hand's fingertips: thumb, index, middle, ring, pinky (those the mesh
	// has). Marker bones where the mesh carries them; otherwise estimated from each digit's last
	// joints, as the pinch measure is. False for an unknown hand.
	UFUNCTION(BlueprintPure, Category="Hand Grab")
	bool GetFingertips(int32 HandIndex, TArray<FVector>& OutTips) const;

	// True while any hand holds item Item of Source.
	bool IsItemHeld(const UObject* Source, int32 Item) const;

	// --- Queries ---

	UFUNCTION(BlueprintPure, Category="Hand Grab")
	bool IsHeld(const UPrimitiveComponent* Component) const;

	// Number of tracked hands (0 until a pawn with hand meshes exists).
	UFUNCTION(BlueprintPure, Category="Hand Grab")
	int32 GetHandCount() const { return Hands.Num(); }

	UFUNCTION(BlueprintPure, Category="Hand Grab")
	bool IsHandPinching(int32 HandIndex) const;

	// World-space pinch midpoint of a hand. False if the hand doesn't exist.
	UFUNCTION(BlueprintPure, Category="Hand Grab")
	bool GetPinchLocation(int32 HandIndex, FVector& OutLocation) const;

	// World-space orientation of a hand: the wrist bone's rotation when the hand has one,
	// else the hand mesh component's. False if the hand doesn't exist (or its mesh went
	// away). Lets consumers make a held object rotate WITH the hand instead of staying
	// locked to its grab-time orientation.
	UFUNCTION(BlueprintPure, Category="Hand Grab")
	bool GetHandRotation(int32 HandIndex, FRotator& OutRotation) const;

	// True while gesture logging is on (bLogGestures or HandGrab.LogGestures 1), so consumers
	// can emit their own per-frame diagnostics alongside the subsystem's under one switch.
	UFUNCTION(BlueprintPure, Category="Hand Grab")
	bool IsLoggingGestures() const { return ShouldLog(); }

	// What a hand is holding, or null.
	UFUNCTION(BlueprintPure, Category="Hand Grab")
	UPrimitiveComponent* GetHeldComponent(int32 HandIndex) const;

	// The skinned mesh a hand was found on, or null if the hand doesn't exist. On device this is
	// the tracking-source mesh (only those grab; see ResolveHands), not necessarily the copy the
	// player sees, but its transform and bones are live.
	UFUNCTION(BlueprintPure, Category="Hand Grab")
	USkinnedMeshComponent* GetHandMesh(int32 HandIndex) const;

	// The world transform the follow set on this hand's held item during this frame's tick:
	// location and rotation as written, scale the component's own (1 for an item source's item;
	// the subsystem never scales). False if the hand holds nothing or holds a bFollow = false
	// registration. The subsystem ticks between TG_PostPhysics and TG_PostUpdateWork: read in
	// TG_PostUpdateWork for this frame's pose; an ordinary (pre-physics) Tick gets last frame's.
	UFUNCTION(BlueprintPure, Category="Hand Grab")
	bool GetGrabbedTransform(int32 HandIndex, FTransform& OutTransform) const;

	// --- UTickableWorldSubsystem ---

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	// Rebuilds Hands from the local pawn's skinned meshes when the pawn changes or a
	// cached mesh goes stale. Anything held by a vanishing hand is released (broadcast).
	void ResolveHands();

	// Pinch edges + held follow + hover for one hand.
	void TickHand(FHandGrabHand& Hand, int32 HandIndex, float DeltaTime);

	// Reads the hand's bones: probe points, size factor and the reference-unit pinch/fist
	// metrics. False if the mesh is gone.
	bool MeasureHand(FHandGrabHand& Hand) const;

	// The hand's world orientation (wrist bone, else the mesh component). Shared by the
	// public GetHandRotation and the rotation follow so both read the identical source.
	static bool GetHandQuat(const FHandGrabHand& Hand, FQuat& OutQuat);

	// Nearest enabled, unheld grabbable (component or item) within its own GrabRadius (times
	// ReachScale) of WorldPos; an unset target if none. OutDistance receives its distance.
	// GateHand: skip what that (closed) hand's gesture does not qualify for (PassesGestureGate).
	FHandGrabTarget FindGrabCandidate(const FVector& WorldPos, float ReachScale, float& OutDistance,
		const FHandGrabHand* GateHand = nullptr) const;

	// Nearest candidate across all of a hand's probe points (grab point, fingertips, palm).
	FHandGrabTarget FindGrabCandidateForHand(const FHandGrabHand& Hand, bool bGated = true) const;

	// Where a digit's tip is now: its marker bone, or the estimate from its last joints.
	FVector DigitTip(const USkinnedMeshComponent* Mesh, const FHandGrabDigit& Digit) const;

	// Whether a hand's gesture qualifies for a registration: an open hand always (hovering asks
	// nothing); a closed one by a pinch at least MaxPinchDistance tight, or a fist if bAllowFistGrab.
	bool PassesGestureGate(const FHandGrabbableParams& Params, const FHandGrabHand& Hand) const;

	// Takes Candidate into Hand: sets Held and the grab anchor, broadcasts OnGrabbed.
	void GrabCandidate(FHandGrabHand& Hand, int32 HandIndex, const FHandGrabTarget& Candidate, const TCHAR* Reason);

	// Lets go of whatever Hand holds and broadcasts the release.
	void ReleaseHeld(FHandGrabHand& Hand, int32 HandIndex);

	// Single-grab / one-item-per-physical-hand gates.
	bool CanHandGrab(const FHandGrabHand& Hand) const;

	bool ShouldLog() const;

	// True if any hand currently holds Target.
	bool IsHeldByAnyHand(const FHandGrabTarget& Target) const;

	// True if any hand holds anything (the bAllowMultiGrab gate).
	bool IsAnythingHeld() const;

	// Index into Grabbables for Comp, or INDEX_NONE.
	int32 FindGrabbable(const UPrimitiveComponent* Comp) const;

	// Index into ItemSources for Source, or INDEX_NONE.
	int32 FindItemSource(const UObject* Source) const;

	// --- Component/item dispatch: everything the grab logic needs to know about a target ---

	// The target's params, or null if it is no longer registered. bOutEnabled: its enabled gate.
	const FHandGrabbableParams* GetTargetParams(const FHandGrabTarget& Target, bool* bOutEnabled = nullptr) const;

	// Surface distance from Probe (0 inside) and the nearest surface point; negative when the
	// target has no usable shape (a component without collision; OutClosest is then Probe).
	float GetTargetSurfaceDistance(const FHandGrabTarget& Target, const FVector& Probe, FVector& OutClosest) const;

	// Distance used for reach tests: the surface distance, else (component without collision)
	// the distance to its bounds centre.
	float GetTargetReachDistance(const FHandGrabTarget& Target, const FVector& Probe) const;

	bool GetTargetPose(const FHandGrabTarget& Target, FVector& OutLocation, FQuat& OutRotation) const;

	// Moves the target; bRotate also applies Rotation (components keep their own otherwise).
	void SetTargetPose(const FHandGrabTarget& Target, const FVector& Location, const FQuat& Rotation, bool bRotate);

	FString GetTargetName(const FHandGrabTarget& Target) const;

	void BroadcastGrabbed(const FHandGrabTarget& Target, int32 HandIndex, const FVector& Pinch);
	void BroadcastReleased(const FHandGrabTarget& Target, int32 HandIndex, const FVector& Pinch);

	// Hover-refcount transitions: called when a hand's hovered target changes.
	void SetHandHover(FHandGrabHand& Hand, const FHandGrabTarget& NewHover);

	// Drops stale (destroyed-component) grabbables and fixes hover counts.
	void PruneStale();

	TArray<FHandGrabbable> Grabbables;
	TArray<FHandGrabItemSourceEntry> ItemSources;
	TArray<FHandGrabHand> Hands;
	TWeakObjectPtr<APawn> CachedPawn;

	// Per-target count of hands currently hovering it, for the begin/end edge events.
	TMap<FHandGrabTarget, int32> HoverCounts;
};
