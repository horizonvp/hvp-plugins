#include "HandGrabSubsystem.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogHVPHandGrab);

namespace
{
	TAutoConsoleVariable<int32> CVarHandGrabLogGestures(
		TEXT("HandGrab.LogGestures"), 0,
		TEXT("1 = log hand-grab gesture metrics near grabbables and every grab/release with its reason "
			 "(same as UHandGrabSubsystem::bLogGestures)."));

	// Seconds between metric log lines per hand while bLogGestures is on.
	constexpr float GestureLogInterval = 0.25f;

	// Secondary probe points (fingertips, palm) pay this much (cm) against the grab point when
	// two grabbables compete, so the item BETWEEN the fingers beats one a fingertip brushes.
	constexpr float SecondaryProbePenalty = 1.f;

	const float NoMetric = TNumericLimits<float>::Max();

	// The named tip bone on a hand mesh. Prefers a bone whose name contains both Digit and
	// "tip" (the Meta meshes' *_finger_tip_marker), then Digit and "null" (b_*_<digit>_null,
	// also at the tip), falling back to the last Digit bone — the same resolution
	// HVPButtonBase uses for the index tip, generalized to any digit.
	FName FindTipBone(const USkinnedMeshComponent* Mesh, const TCHAR* Digit)
	{
		if (!Mesh)
		{
			return NAME_None;
		}
		FName Tip = NAME_None;
		FName Null = NAME_None;
		FName Last = NAME_None;
		const int32 NumBones = Mesh->GetNumBones();
		for (int32 i = 0; i < NumBones; ++i)
		{
			const FName BoneName = Mesh->GetBoneName(i);
			const FString Name = BoneName.ToString().ToLower();
			if (!Name.Contains(Digit))
			{
				continue;
			}
			Last = BoneName;
			if (Name.Contains(TEXT("tip")))
			{
				Tip = BoneName;
			}
			else if (Name.Contains(TEXT("null")))
			{
				Null = BoneName;
			}
		}
		return !Tip.IsNone() ? Tip : (!Null.IsNone() ? Null : Last);
	}

	// First bone whose lower-cased name contains every fragment; NAME_None if none.
	FName FindBoneContaining(const USkinnedMeshComponent* Mesh,
		std::initializer_list<const TCHAR*> Fragments)
	{
		if (!Mesh)
		{
			return NAME_None;
		}
		const int32 NumBones = Mesh->GetNumBones();
		for (int32 i = 0; i < NumBones; ++i)
		{
			const FString Name = Mesh->GetBoneName(i).ToString().ToLower();
			bool bAll = true;
			for (const TCHAR* Fragment : Fragments)
			{
				if (!Name.Contains(Fragment))
				{
					bAll = false;
					break;
				}
			}
			if (bAll)
			{
				return Mesh->GetBoneName(i);
			}
		}
		return NAME_None;
	}

	// The wrist bone, for fist detection. Prefers a bone named *wrist*; falls back to the
	// root bone, which on the Quest hand meshes IS the wrist (see HandSizing.md's bone
	// map: 0 wrist, 1 forearm stub, 2-5 thumb, ...).
	FName FindWristBone(const USkinnedMeshComponent* Mesh)
	{
		if (!Mesh || Mesh->GetNumBones() == 0)
		{
			return NAME_None;
		}
		const FName Wrist = FindBoneContaining(Mesh, {TEXT("wrist")});
		return Wrist.IsNone() ? Mesh->GetBoneName(0) : Wrist;
	}

	FVector BoneWorld(const USkinnedMeshComponent* Mesh, FName Bone)
	{
		return Mesh->GetBoneLocation(Bone, EBoneSpaces::WorldSpace);
	}
}

bool UHandGrabSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Hands only exist at runtime — stay out of editor preview worlds.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UHandGrabSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHandGrabSubsystem, STATGROUP_Tickables);
}

void UHandGrabSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	PruneStale();
	ResolveHands();

	for (int32 i = 0; i < Hands.Num(); ++i)
	{
		TickHand(Hands[i], i, DeltaTime);
	}
}

// --- Registration ------------------------------------------------------------------------

void UHandGrabSubsystem::RegisterGrabbable(UPrimitiveComponent* Component,
	const FHandGrabbableParams& Params)
{
	if (!Component)
	{
		return;
	}
	const int32 Existing = FindGrabbable(Component);
	if (Existing != INDEX_NONE)
	{
		Grabbables[Existing].Params = Params;
		return;
	}
	FHandGrabbable G;
	G.Comp = Component;
	G.Params = Params;
	Grabbables.Add(G);
}

void UHandGrabSubsystem::UnregisterGrabbable(UPrimitiveComponent* Component)
{
	const int32 Index = FindGrabbable(Component);
	if (Index == INDEX_NONE)
	{
		return;
	}
	ForceRelease(Component);
	for (FHandGrabHand& Hand : Hands)
	{
		if (Hand.Hovered.Get() == Component)
		{
			SetHandHover(Hand, nullptr);
		}
		if (Hand.RecentHover.Get() == Component)
		{
			Hand.RecentHover = nullptr;
		}
	}
	Grabbables.RemoveAt(Index);
}

void UHandGrabSubsystem::SetGrabbableEnabled(UPrimitiveComponent* Component, bool bEnabled)
{
	const int32 Index = FindGrabbable(Component);
	if (Index != INDEX_NONE)
	{
		Grabbables[Index].bEnabled = bEnabled;
	}
}

void UHandGrabSubsystem::ForceRelease(UPrimitiveComponent* Component)
{
	for (FHandGrabHand& Hand : Hands)
	{
		if (Hand.Held.Get() == Component)
		{
			Hand.Held = nullptr;
		}
	}
}

// --- Queries -----------------------------------------------------------------------------

bool UHandGrabSubsystem::IsHeld(const UPrimitiveComponent* Component) const
{
	return Component && IsHeldByAnyHand(Component);
}

bool UHandGrabSubsystem::IsHandPinching(int32 HandIndex) const
{
	return Hands.IsValidIndex(HandIndex) && Hands[HandIndex].bPinching;
}

bool UHandGrabSubsystem::GetPinchLocation(int32 HandIndex, FVector& OutLocation) const
{
	if (!Hands.IsValidIndex(HandIndex))
	{
		return false;
	}
	OutLocation = Hands[HandIndex].PinchMid;
	return true;
}

bool UHandGrabSubsystem::GetHandRotation(int32 HandIndex, FRotator& OutRotation) const
{
	if (!Hands.IsValidIndex(HandIndex))
	{
		return false;
	}
	FQuat Q;
	if (!GetHandQuat(Hands[HandIndex], Q))
	{
		return false;
	}
	OutRotation = FRotator(Q);
	return true;
}

bool UHandGrabSubsystem::GetHandQuat(const FHandGrabHand& Hand, FQuat& OutQuat)
{
	const USkinnedMeshComponent* Mesh = Hand.Mesh.Get();
	if (!Mesh)
	{
		return false;
	}
	// The wrist is the natural "hand orientation"; hands resolved without a wrist bone
	// fall back to the mesh component (still tracks a controller-driven hand actor).
	OutQuat = (Hand.WristBone != NAME_None)
		? Mesh->GetBoneQuaternion(Hand.WristBone)
		: Mesh->GetComponentQuat();
	return true;
}

UPrimitiveComponent* UHandGrabSubsystem::GetHeldComponent(int32 HandIndex) const
{
	return Hands.IsValidIndex(HandIndex) ? Hands[HandIndex].Held.Get() : nullptr;
}

// --- Internals ---------------------------------------------------------------------------

void UHandGrabSubsystem::ResolveHands()
{
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

	bool bNeedResolve = (CachedPawn.Get() != Pawn);
	if (!bNeedResolve)
	{
		for (const FHandGrabHand& Hand : Hands)
		{
			if (!Hand.Mesh.IsValid())
			{
				bNeedResolve = true;
				break;
			}
		}
	}
	if (!bNeedResolve)
	{
		return;
	}

	// The hand set is changing under us — release (with broadcast: the consumer must know
	// its item is no longer held) and clear hover.
	for (int32 i = 0; i < Hands.Num(); ++i)
	{
		if (UPrimitiveComponent* Held = Hands[i].Held.Get())
		{
			Hands[i].Held = nullptr;
			OnReleased.Broadcast(Held, i, Hands[i].PinchMid);
		}
		SetHandHover(Hands[i], nullptr);
	}
	Hands.Reset();
	CachedPawn = Pawn;
	if (!Pawn)
	{
		return;
	}

	// A "hand" is any skinned mesh on the pawn carrying both a thumb tip and an index tip.
	TArray<USkinnedMeshComponent*> Meshes;
	Pawn->GetComponents(Meshes);
	for (USkinnedMeshComponent* Mesh : Meshes)
	{
		if (!Mesh)
		{
			continue;
		}
		const FName Thumb = FindTipBone(Mesh, TEXT("thumb"));
		const FName Index = FindTipBone(Mesh, TEXT("index"));
		if (Thumb.IsNone() || Index.IsNone())
		{
			continue;
		}
		FHandGrabHand Hand;
		Hand.Mesh = Mesh;
		Hand.ThumbTipBone = Thumb;
		Hand.IndexTipBone = Index;
		Hand.MiddleTipBone = FindTipBone(Mesh, TEXT("middle"));
		Hand.ThumbPadBone = FindBoneContaining(Mesh, {TEXT("thumb"), TEXT("finger_pad")});
		Hand.IndexPadBone = FindBoneContaining(Mesh, {TEXT("index"), TEXT("finger_pad")});
		Hand.MiddlePadBone = FindBoneContaining(Mesh, {TEXT("middle"), TEXT("finger_pad")});
		Hand.MiddleKnuckleBone = FindBoneContaining(Mesh, {TEXT("middle1")});
		Hand.PalmBone = FindBoneContaining(Mesh, {TEXT("palm_center")});
		Hand.WristBone = FindWristBone(Mesh);
		Hands.Add(Hand);
	}

	// Registration order is the pawn's component order — deliberately NOT re-sorted. An
	// attempt to prefer the presentation meshes (the plain USkeletalMeshComponents that replay
	// the tracked pose, and are what the player sees) over the tracking-source meshes turned
	// out to be built on a false premise: measured on device, the replay meshes report their
	// tip and wrist bones collapsed to a point (pinch 0.00, fist 0.00), so they read as
	// permanently latched closed and never produce a grab edge. Only the source meshes grab.
	// Sorting the replays first merely moved dead hands to indices 0/1; dropping the sources
	// removed grabbing from the app entirely. Why the replay bones read collapsed here while
	// rendering correctly is unresolved — until it is, hand order stays as registered.
	for (int32 i = 0; i < Hands.Num(); ++i)
	{
		const FHandGrabHand& Hand = Hands[i];
		UE_LOG(LogHVPHandGrab, Log,
			TEXT("Hand %d = %s: thumb=%s index=%s middle=%s pads=%s/%s/%s knuckle=%s palm=%s wrist=%s"),
			i, *GetNameSafe(Hand.Mesh.Get()), *Hand.ThumbTipBone.ToString(),
			*Hand.IndexTipBone.ToString(), *Hand.MiddleTipBone.ToString(),
			*Hand.ThumbPadBone.ToString(), *Hand.IndexPadBone.ToString(),
			*Hand.MiddlePadBone.ToString(), *Hand.MiddleKnuckleBone.ToString(),
			*Hand.PalmBone.ToString(), *Hand.WristBone.ToString());
	}
}

bool UHandGrabSubsystem::MeasureHand(FHandGrabHand& Hand) const
{
	const USkinnedMeshComponent* Mesh = Hand.Mesh.Get();
	if (!Mesh)
	{
		return false;
	}

	Hand.ThumbTip = BoneWorld(Mesh, Hand.ThumbTipBone);
	Hand.IndexTip = BoneWorld(Mesh, Hand.IndexTipBone);
	Hand.bHasMiddleTip = !Hand.MiddleTipBone.IsNone();
	Hand.MiddleTip = Hand.bHasMiddleTip ? BoneWorld(Mesh, Hand.MiddleTipBone) : Hand.IndexTip;

	const bool bHasWrist = !Hand.WristBone.IsNone();
	const FVector Wrist = bHasWrist ? BoneWorld(Mesh, Hand.WristBone) : Hand.IndexTip;

	// Size factor: the palm (wrist -> middle knuckle) is rigid, so its live length tracks the
	// calibrated hand size regardless of pose.
	Hand.SizeScale = 1.f;
	if (bScaleThresholdsByHandSize && bHasWrist && !Hand.MiddleKnuckleBone.IsNone()
		&& ReferencePalmLength > KINDA_SMALL_NUMBER)
	{
		const float Palm = FVector::Dist(Wrist, BoneWorld(Mesh, Hand.MiddleKnuckleBone));
		if (Palm > KINDA_SMALL_NUMBER)
		{
			Hand.SizeScale = FMath::Clamp(Palm / ReferencePalmLength,
				FMath::Min(MinHandSizeScale, MaxHandSizeScale),
				FMath::Max(MinHandSizeScale, MaxHandSizeScale));
		}
	}

	// Pinch: closest thumb point to closest finger point. Tips always; pads when the mesh has
	// them (a pad-to-pad pinch leaves the tip ENDS 2-3 cm apart); middle finger optionally.
	TArray<FVector, TInlineAllocator<2>> ThumbPoints;
	ThumbPoints.Add(Hand.ThumbTip);
	TArray<FVector, TInlineAllocator<4>> FingerPoints;
	FingerPoints.Add(Hand.IndexTip);
	if (bPinchWithMiddle && Hand.bHasMiddleTip)
	{
		FingerPoints.Add(Hand.MiddleTip);
	}
	if (bUsePadMarkers)
	{
		if (!Hand.ThumbPadBone.IsNone())
		{
			ThumbPoints.Add(BoneWorld(Mesh, Hand.ThumbPadBone));
		}
		if (!Hand.IndexPadBone.IsNone())
		{
			FingerPoints.Add(BoneWorld(Mesh, Hand.IndexPadBone));
		}
		if (bPinchWithMiddle && !Hand.MiddlePadBone.IsNone())
		{
			FingerPoints.Add(BoneWorld(Mesh, Hand.MiddlePadBone));
		}
	}
	float BestPinch = NoMetric;
	for (const FVector& T : ThumbPoints)
	{
		for (const FVector& F : FingerPoints)
		{
			BestPinch = FMath::Min(BestPinch, FVector::Dist(T, F));
		}
	}
	Hand.PinchMetric = BestPinch / Hand.SizeScale;

	// Fist: the closer of the index and middle tips to the wrist.
	Hand.FistMetric = NoMetric;
	if (bEnableFistGrab && bHasWrist)
	{
		float Curl = FVector::Dist(Hand.IndexTip, Wrist);
		if (Hand.bHasMiddleTip)
		{
			Curl = FMath::Min(Curl, FVector::Dist(Hand.MiddleTip, Wrist));
		}
		Hand.FistMetric = Curl / Hand.SizeScale;
	}

	Hand.PalmPoint = !Hand.PalmBone.IsNone()
		? BoneWorld(Mesh, Hand.PalmBone)
		: (Wrist + Hand.IndexTip) * 0.5f;
	return true;
}

void UHandGrabSubsystem::TickHand(FHandGrabHand& Hand, int32 HandIndex, float DeltaTime)
{
	if (!MeasureHand(Hand))
	{
		// ResolveHands rebuilds next tick; just release cleanly now.
		if (UPrimitiveComponent* Held = Hand.Held.Get())
		{
			Hand.Held = nullptr;
			OnReleased.Broadcast(Held, HandIndex, Hand.PinchMid);
		}
		Hand.bPinching = false;
		Hand.bPinchClosed = false;
		Hand.bFistClosed = false;
		SetHandHover(Hand, nullptr);
		return;
	}

	// Each gesture keeps its own hysteresis (close tight, open wide) so tracking jitter at
	// a threshold can't machine-gun grab/drop. Metrics are already in reference-hand cm.
	if (!Hand.bPinchClosed)
	{
		Hand.bPinchClosed = Hand.PinchMetric < PinchStartDistance;
	}
	else
	{
		Hand.bPinchClosed = Hand.PinchMetric <= FMath::Max(PinchReleaseDistance, PinchStartDistance);
	}

	if (Hand.FistMetric < NoMetric)
	{
		if (!Hand.bFistClosed)
		{
			Hand.bFistClosed = Hand.FistMetric < FistCloseDistance;
		}
		else
		{
			Hand.bFistClosed = Hand.FistMetric <= FMath::Max(FistOpenDistance, FistCloseDistance);
		}
	}
	else
	{
		Hand.bFistClosed = false;
	}

	// Grab point: the palm for a fist (the pinch midpoint of a fist sits oddly on the
	// curled fingers), the thumb-index midpoint otherwise — with a middle-finger pinch, the
	// midpoint to whichever of index/middle the thumb is nearer.
	Hand.LiveCarry = EHandGrabCarry::PinchIndex;
	if (Hand.bFistClosed)
	{
		Hand.LiveCarry = EHandGrabCarry::Palm;
	}
	else if (bPinchWithMiddle && Hand.bHasMiddleTip
		&& FVector::DistSquared(Hand.ThumbTip, Hand.MiddleTip)
			< FVector::DistSquared(Hand.ThumbTip, Hand.IndexTip))
	{
		Hand.LiveCarry = EHandGrabCarry::PinchMiddle;
	}

	// While something is held, measure from the source that was in force at the grab — the
	// live choice keeps updating (it seeds the NEXT grab) but must not move a held item.
	const EHandGrabCarry Carry = Hand.Held.IsValid() ? Hand.HeldCarry : Hand.LiveCarry;
	switch (Carry)
	{
	case EHandGrabCarry::Palm:
		Hand.PinchMid = Hand.PalmPoint;
		break;
	case EHandGrabCarry::PinchMiddle:
		Hand.PinchMid = Hand.bHasMiddleTip
			? (Hand.ThumbTip + Hand.MiddleTip) * 0.5f
			: (Hand.ThumbTip + Hand.IndexTip) * 0.5f;
		break;
	default:
		Hand.PinchMid = (Hand.ThumbTip + Hand.IndexTip) * 0.5f;
		break;
	}

	Hand.TimeSinceRecentHover += DeltaTime;
	const bool bGestureClosed = Hand.bPinchClosed || Hand.bFistClosed;

	if (!Hand.bPinching && bGestureClosed)
	{
		// --- Close edge ---
		Hand.bPinching = true;
		Hand.TimeSinceClose = 0.f;
		Hand.TimeOpen = 0.f;
		Hand.bSqueezeArmed = false;
		Hand.SqueezeRefPinch = Hand.PinchMetric;
		Hand.SqueezeRefFist = Hand.FistMetric;

		UPrimitiveComponent* Remembered =
			(Hand.TimeSinceRecentHover <= HoverMemorySeconds) ? Hand.RecentHover.Get() : nullptr;
		SetHandHover(Hand, nullptr);

		if (CanHandGrab(Hand))
		{
			int32 Candidate = FindGrabCandidateForHand(Hand);
			const TCHAR* Reason = TEXT("close");
			if (Candidate == INDEX_NONE && Remembered)
			{
				// The open hand was aiming at this a moment ago; closing drifted the grab
				// point off it. Take it if it's still roughly in reach of any probe.
				const int32 RememberedIndex = FindGrabbable(Remembered);
				if (RememberedIndex != INDEX_NONE && Grabbables[RememberedIndex].bEnabled
					&& !IsHeldByAnyHand(Remembered))
				{
					const FVector Probes[] = {Hand.PinchMid, Hand.ThumbTip, Hand.IndexTip,
						Hand.MiddleTip, Hand.PalmPoint};
					for (const FVector& Probe : Probes)
					{
						FVector OnBody;
						float D = Remembered->GetClosestPointOnCollision(Probe, OnBody);
						if (D < 0.f)
						{
							D = FVector::Dist(Probe, Remembered->Bounds.Origin);
						}
						if (D < Grabbables[RememberedIndex].Params.GrabRadius * HoverMemoryReachScale)
						{
							Candidate = RememberedIndex;
							Reason = TEXT("hover-memory");
							break;
						}
					}
				}
			}
			if (Candidate != INDEX_NONE)
			{
				GrabCandidate(Hand, HandIndex, Candidate, Reason);
			}
		}
	}
	else if (Hand.bPinching && !bGestureClosed)
	{
		// --- Gesture reads open ---
		Hand.TimeOpen += DeltaTime;
		const bool bHolding = Hand.Held.IsValid();
		// Only a HELD item waits out the debounce: an empty hand re-arms at once, so a quick
		// re-close is a fresh attempt.
		if (!bHolding || Hand.TimeOpen >= ReleaseDebounceSeconds)
		{
			Hand.bPinching = false;
			Hand.TimeOpen = 0.f;
			Hand.bSqueezeArmed = false;
			if (UPrimitiveComponent* Held = Hand.Held.Get())
			{
				Hand.Held = nullptr;
				if (ShouldLog())
				{
					UE_LOG(LogHVPHandGrab, Log,
						TEXT("Hand %d RELEASE %s (pinch %.2f fist %.2f scale %.2f)"),
						HandIndex, *GetNameSafe(Held->GetOwner()), Hand.PinchMetric,
						Hand.FistMetric, Hand.SizeScale);
				}
				OnReleased.Broadcast(Held, HandIndex, Hand.PinchMid);
			}
		}
	}
	else if (Hand.bPinching)
	{
		// --- Held closed ---
		Hand.TimeOpen = 0.f;
		Hand.TimeSinceClose += DeltaTime;

		if (!Hand.Held.IsValid() && CanHandGrab(Hand))
		{
			const int32 Candidate = FindGrabCandidateForHand(Hand);
			if (Candidate != INDEX_NONE && Hand.TimeSinceClose <= GrabGraceSeconds)
			{
				// Closed a beat before arriving (or tracking lagged the reach).
				GrabCandidate(Hand, HandIndex, Candidate, TEXT("grace"));
			}
			else if (Candidate == INDEX_NONE)
			{
				// Out of reach: follow the hand's current closure, so arriving compares
				// against the pose it arrived in.
				Hand.bSqueezeArmed = false;
				Hand.SqueezeRefPinch = Hand.PinchMetric;
				Hand.SqueezeRefFist = Hand.FistMetric;
			}
			else if (!Hand.bSqueezeArmed)
			{
				// Arrived already closed: remember the arrival pose, grab on a further squeeze.
				Hand.bSqueezeArmed = true;
				Hand.SqueezeRefPinch = Hand.PinchMetric;
				Hand.SqueezeRefFist = Hand.FistMetric;
			}
			else
			{
				// Loosest pose since arrival is the baseline; a squeeze past it grabs.
				Hand.SqueezeRefPinch = FMath::Max(Hand.SqueezeRefPinch, Hand.PinchMetric);
				Hand.SqueezeRefFist = FMath::Max(Hand.SqueezeRefFist, Hand.FistMetric);
				const bool bPinchSqueeze = SqueezePinchDelta > 0.f && Hand.PinchMetric < NoMetric
					&& Hand.PinchMetric <= Hand.SqueezeRefPinch - SqueezePinchDelta;
				const bool bFistSqueeze = SqueezeFistDelta > 0.f && Hand.FistMetric < NoMetric
					&& Hand.FistMetric <= Hand.SqueezeRefFist - SqueezeFistDelta;
				if (bPinchSqueeze || bFistSqueeze)
				{
					GrabCandidate(Hand, HandIndex, Candidate, TEXT("squeeze"));
				}
			}
		}
	}

	// Held component chases the pinch midpoint (when its registration says to).
	if (UPrimitiveComponent* Held = Hand.Held.Get())
	{
		const int32 GrabIndex = FindGrabbable(Held);
		if (GrabIndex != INDEX_NONE && Grabbables[GrabIndex].Params.bFollow)
		{
			const FHandGrabbableParams& P = Grabbables[GrabIndex].Params;

			// Rotation FIRST, then position, both from this tick's one read of the hand. The
			// position solve below anchors the grip through the component's CURRENT rotation,
			// so turning the item here and then re-solving lands the grip on the pinch within
			// the same frame — the ordering a consumer's own tick can never get (see the
			// bFollowRotation comment in the header).
			FQuat HandQuat;
			if (P.bFollowRotation && Hand.bHeldGrabRotValid && GetHandQuat(Hand, HandQuat))
			{
				const FQuat TargetQuat =
					(HandQuat * Hand.HeldGrabHandQuat.Inverse()) * Hand.HeldGrabItemQuat;
				Held->SetWorldRotation(P.bRigidFollow
					? TargetQuat
					: FMath::QInterpTo(Held->GetComponentQuat(), TargetQuat, DeltaTime, P.FollowSpeed));
			}

			// Chase so the GRABBED POINT stays at the pinch — the item hangs from
			// wherever you took hold of it (no snap to any center), and rotation pivots
			// about that grip, since the anchor is re-solved from the component's current
			// transform every tick.
			const FVector WorldGrabPoint = Held->GetComponentLocation()
				+ Held->GetComponentQuat().RotateVector(Hand.HeldGrabLocal);
			const FVector Target =
				Held->GetComponentLocation() + (Hand.PinchMid - WorldGrabPoint);
			const FVector NewPos = Grabbables[GrabIndex].Params.bRigidFollow
				? Target
				: FMath::VInterpTo(Held->GetComponentLocation(),
					Target, DeltaTime, Grabbables[GrabIndex].Params.FollowSpeed);
			Held->SetWorldLocation(NewPos);

			if (ShouldLog())
			{
				// The one number that matters for "did it move in my hand": the item's origin
				// expressed in the HAND's frame. Rigid in hand = constant. A snap = a jump.
				// gripErr is how far the grip sat off the pinch BEFORE this solve (the error the
				// solve had to absorb this frame — nonzero means something else moved it).
				FQuat LogHandQuat = FQuat::Identity;
				GetHandQuat(Hand, LogHandQuat);
				const FVector OffsetInHand = LogHandQuat.UnrotateVector(NewPos - Hand.PinchMid);
				UE_LOG(LogHVPHandGrab, Log,
					TEXT("Hand %d FOLLOW %s carry=%d/%d pinch=%s pos=%s gripErr=%.2f offsetInHand=%s rot=%s scale=%s"),
					HandIndex, *GetNameSafe(Held->GetOwner()),
					static_cast<int32>(Hand.HeldCarry), static_cast<int32>(Hand.LiveCarry),
					*Hand.PinchMid.ToString(),
					*NewPos.ToString(), FVector::Dist(WorldGrabPoint, Hand.PinchMid),
					*OffsetInHand.ToString(), *Held->GetComponentRotation().ToString(),
					*Held->GetComponentScale().ToString());
			}
		}
	}

	// Open-hand hover affordance. Suppressed while the grab gates would refuse the grab
	// anyway — no "you can pick this up" hint on things you can't.
	if (!Hand.bPinching)
	{
		const int32 Candidate = CanHandGrab(Hand) ? FindGrabCandidateForHand(Hand) : INDEX_NONE;
		UPrimitiveComponent* NewHover =
			Candidate != INDEX_NONE ? Grabbables[Candidate].Comp.Get() : nullptr;
		SetHandHover(Hand, NewHover);
		if (NewHover)
		{
			Hand.RecentHover = NewHover;
			Hand.TimeSinceRecentHover = 0.f;
		}
	}

	if (ShouldLog())
	{
		Hand.LogCooldown -= DeltaTime;
		const bool bInteresting = Hand.Hovered.IsValid() || Hand.bPinching
			|| Hand.TimeSinceRecentHover <= HoverMemorySeconds;
		if (bInteresting && Hand.LogCooldown <= 0.f)
		{
			Hand.LogCooldown = GestureLogInterval;
			UE_LOG(LogHVPHandGrab, Log,
				TEXT("Hand %d: pinch %.2f (start %.2f/release %.2f) fist %.2f (close %.2f/open %.2f) "
					 "scale %.2f closed=%d/%d latched=%d held=%s hover=%s"),
				HandIndex, Hand.PinchMetric, PinchStartDistance, PinchReleaseDistance,
				Hand.FistMetric < NoMetric ? Hand.FistMetric : -1.f, FistCloseDistance,
				FistOpenDistance, Hand.SizeScale, Hand.bPinchClosed ? 1 : 0,
				Hand.bFistClosed ? 1 : 0, Hand.bPinching ? 1 : 0,
				*GetNameSafe(Hand.Held.IsValid() ? Hand.Held->GetOwner() : nullptr),
				*GetNameSafe(Hand.Hovered.IsValid() ? Hand.Hovered->GetOwner() : nullptr));
		}
	}

	if (bDrawDebug)
	{
		UWorld* World = GetWorld();
		DrawDebugSphere(World, Hand.PinchMid, 1.f, 8,
			Hand.bPinching ? FColor::Green : FColor::Silver, false, -1.f, 0, 0.15f);
		const FColor ProbeColor = Hand.bPinchClosed ? FColor::Cyan
			: (Hand.bFistClosed ? FColor::Orange : FColor(90, 90, 90));
		DrawDebugPoint(World, Hand.ThumbTip, 6.f, ProbeColor);
		DrawDebugPoint(World, Hand.IndexTip, 6.f, ProbeColor);
		if (Hand.bHasMiddleTip)
		{
			DrawDebugPoint(World, Hand.MiddleTip, 6.f, ProbeColor);
		}
		DrawDebugPoint(World, Hand.PalmPoint, 6.f, ProbeColor);
		for (const FHandGrabbable& G : Grabbables)
		{
			if (const UPrimitiveComponent* Comp = G.Comp.Get())
			{
				DrawDebugSphere(World, Comp->Bounds.Origin, G.Params.GrabRadius,
					12, G.bEnabled ? FColor(64, 64, 64) : FColor(96, 32, 32),
					false, -1.f, 0, 0.1f);
			}
		}
	}
}

bool UHandGrabSubsystem::CanHandGrab(const FHandGrabHand& Hand) const
{
	// Single-grab policy: while anything is held, other hands can't take a second one.
	if (!bAllowMultiGrab && IsAnythingHeld())
	{
		return false;
	}
	// One item per PHYSICAL hand: if a DIFFERENT tracked hand already holds something from
	// (essentially) the same grab point, it is this same physical hand seen through a
	// duplicate mesh — don't stack a second item into it.
	for (const FHandGrabHand& Other : Hands)
	{
		if (&Other != &Hand && Other.Held.IsValid()
			&& FVector::DistSquared(Other.PinchMid, Hand.PinchMid)
				< FMath::Square(SamePhysicalHandDistance))
		{
			return false;
		}
	}
	return true;
}

void UHandGrabSubsystem::GrabCandidate(FHandGrabHand& Hand, int32 HandIndex, int32 Candidate,
	const TCHAR* Reason)
{
	UPrimitiveComponent* Comp = Grabbables.IsValidIndex(Candidate)
		? Grabbables[Candidate].Comp.Get() : nullptr;
	if (!Comp)
	{
		return;
	}
	Hand.Held = Comp;
	Hand.bSqueezeArmed = false;
	// Freeze the carry-point source for this hold (see FHandGrabHand::HeldCarry).
	Hand.HeldCarry = Hand.LiveCarry;
	// Stick to the point you actually took hold of: the nearest point on the collision body
	// (the pinch itself when it's inside the volume), stored component-local so it rides the
	// item's motion and rotation.
	// bPreserveGrabOffset anchors on the pinch itself, so the follow target resolves to the
	// item's current position and it never travels to the hand at all.
	FVector GrabPoint = Hand.PinchMid;
	if (!Grabbables[Candidate].Params.bPreserveGrabOffset)
	{
		FVector OnBody;
		if (Comp->GetClosestPointOnCollision(Hand.PinchMid, OnBody) > 0.f)
		{
			GrabPoint = OnBody;
		}
	}
	Hand.HeldGrabLocal = Comp->GetComponentQuat().UnrotateVector(
		GrabPoint - Comp->GetComponentLocation());

	// Rotation reference frame for bFollowRotation. A hand whose rotation can't be read
	// simply leaves the item at its grab-time orientation.
	Hand.HeldGrabItemQuat = Comp->GetComponentQuat();
	Hand.bHeldGrabRotValid = GetHandQuat(Hand, Hand.HeldGrabHandQuat);

	if (ShouldLog())
	{
		UE_LOG(LogHVPHandGrab, Log,
			TEXT("Hand %d GRAB %s via %s (pinch %.2f fist %.2f scale %.2f, %.2fs after close)"),
			HandIndex, *GetNameSafe(Comp->GetOwner()), Reason, Hand.PinchMetric,
			Hand.FistMetric < NoMetric ? Hand.FistMetric : -1.f, Hand.SizeScale,
			Hand.TimeSinceClose);
		UE_LOG(LogHVPHandGrab, Log,
			TEXT("Hand %d GRAB-REF pinch=%s pos=%s grabLocal=%s itemRot=%s handRotValid=%d preserve=%d rigid=%d"),
			HandIndex, *Hand.PinchMid.ToString(), *Comp->GetComponentLocation().ToString(),
			*Hand.HeldGrabLocal.ToString(), *Hand.HeldGrabItemQuat.Rotator().ToString(),
			Hand.bHeldGrabRotValid ? 1 : 0,
			Grabbables[Candidate].Params.bPreserveGrabOffset ? 1 : 0,
			Grabbables[Candidate].Params.bRigidFollow ? 1 : 0);
	}
	OnGrabbed.Broadcast(Comp, HandIndex, Hand.PinchMid);
}

int32 UHandGrabSubsystem::FindGrabCandidate(const FVector& WorldPos, float ReachScale) const
{
	float Unused = 0.f;
	return FindGrabCandidate(WorldPos, ReachScale, Unused);
}

int32 UHandGrabSubsystem::FindGrabCandidate(const FVector& WorldPos, float ReachScale,
	float& OutDistance) const
{
	int32 Best = INDEX_NONE;
	float BestDist = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Grabbables.Num(); ++i)
	{
		const UPrimitiveComponent* Comp = Grabbables[i].Comp.Get();
		if (!Comp || !Grabbables[i].bEnabled || IsHeldByAnyHand(Comp))
		{
			continue;
		}
		// Distance to the collision SURFACE — 0 anywhere inside the volume, so the whole
		// body is grabbable and GrabRadius is reach beyond it. Falls back to the center
		// of volume (bounds center) when the component has no usable collision.
		FVector OnBody;
		float Dist = Comp->GetClosestPointOnCollision(WorldPos, OnBody);
		if (Dist < 0.f)
		{
			Dist = FVector::Dist(WorldPos, Comp->Bounds.Origin);
		}
		if (Dist < Grabbables[i].Params.GrabRadius * ReachScale && Dist < BestDist)
		{
			BestDist = Dist;
			Best = i;
		}
	}
	OutDistance = BestDist;
	return Best;
}

int32 UHandGrabSubsystem::FindGrabCandidateForHand(const FHandGrabHand& Hand) const
{
	// The grab point first; the fingertips and palm as backups, since the item the player is
	// reaching for is often touching a fingertip while the grab point sits a few cm off it.
	float BestScore = TNumericLimits<float>::Max();
	int32 Best = INDEX_NONE;

	auto Consider = [&](const FVector& Probe, float Penalty)
	{
		float Dist = 0.f;
		const int32 Candidate = FindGrabCandidate(Probe, 1.f, Dist);
		if (Candidate != INDEX_NONE && Dist + Penalty < BestScore)
		{
			BestScore = Dist + Penalty;
			Best = Candidate;
		}
	};

	Consider(Hand.PinchMid, 0.f);
	Consider(Hand.ThumbTip, SecondaryProbePenalty);
	Consider(Hand.IndexTip, SecondaryProbePenalty);
	if (Hand.bHasMiddleTip)
	{
		Consider(Hand.MiddleTip, SecondaryProbePenalty);
	}
	Consider(Hand.PalmPoint, SecondaryProbePenalty);
	return Best;
}

bool UHandGrabSubsystem::ShouldLog() const
{
	return bLogGestures || CVarHandGrabLogGestures.GetValueOnGameThread() != 0;
}

bool UHandGrabSubsystem::IsHeldByAnyHand(const UPrimitiveComponent* Comp) const
{
	for (const FHandGrabHand& Hand : Hands)
	{
		if (Hand.Held.Get() == Comp)
		{
			return true;
		}
	}
	return false;
}

bool UHandGrabSubsystem::IsAnythingHeld() const
{
	for (const FHandGrabHand& Hand : Hands)
	{
		if (Hand.Held.IsValid())
		{
			return true;
		}
	}
	return false;
}

int32 UHandGrabSubsystem::FindGrabbable(const UPrimitiveComponent* Comp) const
{
	return Grabbables.IndexOfByPredicate(
		[Comp](const FHandGrabbable& G) { return G.Comp.Get() == Comp; });
}

void UHandGrabSubsystem::SetHandHover(FHandGrabHand& Hand, UPrimitiveComponent* NewHover)
{
	UPrimitiveComponent* OldHover = Hand.Hovered.Get();
	if (OldHover == NewHover)
	{
		return;
	}
	Hand.Hovered = NewHover;

	// Refcount per component so Begin fires on the FIRST hovering hand and End on the LAST.
	if (OldHover)
	{
		int32& Count = HoverCounts.FindOrAdd(OldHover);
		Count = FMath::Max(0, Count - 1);
		if (Count == 0)
		{
			HoverCounts.Remove(OldHover);
			OnHoverEnd.Broadcast(OldHover);
		}
	}
	if (NewHover)
	{
		int32& Count = HoverCounts.FindOrAdd(NewHover);
		if (Count++ == 0)
		{
			OnHoverBegin.Broadcast(NewHover);
		}
	}
}

void UHandGrabSubsystem::PruneStale()
{
	for (int32 i = Grabbables.Num() - 1; i >= 0; --i)
	{
		if (!Grabbables[i].Comp.IsValid())
		{
			Grabbables.RemoveAt(i);
		}
	}
	for (auto It = HoverCounts.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
