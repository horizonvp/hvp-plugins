#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/WeakObjectPtr.h"

/**
 * Where a sampled presser came from. The button latches onto ONE presser for the whole
 * contact, so the source is part of its identity — a fingertip and a grab sphere at the same
 * depth are not the same presser.
 */
enum class EStereoButtonPresserSource : uint8
{
	None,
	Fingertip,   // an index-tip bone read off the pawn's hand mesh
	Component,   // an overlapping primitive (the grab-sphere fallback)
};

/** One presser seen during a single tick, in the button's neutral local frame. */
struct FStereoButtonPresserSample
{
	EStereoButtonPresserSource Source = EStereoButtonPresserSource::None;

	/** Index into the gathered fingertip array. Stable frame to frame (see FStereoButtonHandTracker). */
	int32 FingertipIndex = INDEX_NONE;

	/** The overlapping primitive for the Component source. */
	TWeakObjectPtr<UPrimitiveComponent> Comp;

	/** 0..1 press amount this presser is asking for (penetration / PressDepth). */
	float Amount = 0.f;

	/** Signed penetration past the neutral face in cm. Positive = inside the button. */
	float PenetrationCm = 0.f;

	/** Where on the face plane the contact is, in button-local cm (local Y, local Z; +Y is the viewer's left). Drives the tilt. */
	FVector2D ContactLocal = FVector2D::ZeroVector;

	/**
	 * True when this presser is deep enough to START a new contact. Samples are emitted for the
	 * wider RETAIN region, so a presser can keep a latch alive without being able to open one.
	 */
	bool bCanAcquire = false;

	bool Matches(EStereoButtonPresserSource InSource, int32 InFingertipIndex, const UPrimitiveComponent* InComp) const
	{
		if (Source != InSource) return false;
		if (Source == EStereoButtonPresserSource::Fingertip) return FingertipIndex == InFingertipIndex;
		if (Source == EStereoButtonPresserSource::Component) return Comp.Get() == InComp;
		return false;
	}
};

/**
 * One presser the button is bound to — an INDEPENDENT press channel with its own depth, its own
 * Do Once and its own press/release edges. Two hands on one button are two channels that never
 * see each other.
 */
struct FStereoButtonPressChannel
{
	EStereoButtonPresserSource Source = EStereoButtonPresserSource::None;
	int32 FingertipIndex = INDEX_NONE;
	TWeakObjectPtr<UPrimitiveComponent> Comp;

	/** This presser's own 0..1 depth. Held while missing inside the grace window, decayed once detached. */
	float Amount = 0.f;
	float PenetrationCm = 0.f;
	FVector2D ContactLocal = FVector2D::ZeroVector;

	/** Do Once, scoped to this contact. Cleared when the channel closes (or at release if re-arm-on-release). */
	bool bFired = false;

	/** True between this channel's own press and release edges. */
	bool bPressed = false;

	/** Seconds since this presser was last seen. At UnlatchGraceSeconds the channel detaches. */
	float TimeSinceSeen = 0.f;

	/**
	 * The presser is gone and the channel is winding down: Amount decays so the head returns
	 * smoothly and an outstanding release still fires. A detached channel can never press, does
	 * not occupy a slot, and is dropped once it has both released and reached zero. If its presser
	 * returns first it re-attaches rather than a fresh one opening.
	 */
	bool bDetached = false;
};

/** Tuning the press model reads every update. Owned by the actor as UPROPERTYs; copied in here. */
struct FStereoButtonPressSettings
{
	float PressThreshold = 0.85f;
	float ReleaseThreshold = 0.4f;
	float ReturnSpeed = 12.f;
	float UnlatchGraceSeconds = 0.12f;
	float PressCooldownSeconds = 0.15f;
	int32 MaxConcurrentPressers = 1;
	/** Clear the Do Once at the release edge, so one continuous contact can fire again (spam buttons). */
	bool bRearmOnRelease = false;
	/** False suppresses new presses (locked / hidden / not yet engaged). The head still moves. */
	bool bMayFire = true;
};

/** A press or release edge produced by one update, in the order they happened. */
struct FStereoButtonPressEdge
{
	bool bPressed = true;
	int32 ChannelIndex = INDEX_NONE;
};

/**
 * The "one poke = one press" latch, kept free of any UObject so it can be reasoned about (and
 * unit-tested) on its own. Ported from AHorizonButtonBase, whose long history is in
 * Docs/HorizonButton.md — the design points that matter are preserved verbatim:
 *
 *  - Latch on contact: when a slot is free, the deepest ACQUIRE-eligible presser claims it.
 *  - One press per latch (bFired). Nothing the finger does inside one contact fires twice.
 *  - Unlatch is the only re-arm, and needs the presser continuously absent for the grace window,
 *    because presence is sampled and a single dropped tracking frame must not re-arm.
 *  - The head follows the deepest channel; a detached channel winds down at ReturnSpeed.
 *  - Cooldown is a button-wide debounce backstop.
 *
 * The caller decides what a "presser" is (fingertip vs grab sphere), what the contact regions
 * are, and what to do with the edges. This class only decides who owns the button.
 */
class HVPSTEREOBUTTON_API FStereoButtonPressModel
{
public:
	/** Advances the latch one tick. Edges are appended to OutEdges in the order they fired. */
	void Update(const TArray<FStereoButtonPresserSample>& Samples, const FStereoButtonPressSettings& Settings,
		float DeltaSeconds, TArray<FStereoButtonPressEdge>& OutEdges);

	/** Drops every channel and the cooldown, re-arming for fresh contacts. */
	void Reset();

	/** Deepest channel's amount, or the decaying remainder once every channel is gone. */
	float GetPressedAmount() const { return PressedAmount; }

	/** True while ANY channel is between its press and release edges. */
	bool IsPressed() const;

	/** Channels currently latched and live (not winding down). */
	int32 GetLiveChannelCount() const;

	/** Seconds since a presser was last in contact, for idle affordances. */
	float GetTimeSinceInteraction() const { return TimeSinceInteraction; }
	void ResetInteractionTimer() { TimeSinceInteraction = 0.f; }

	const TArray<FStereoButtonPressChannel, TInlineAllocator<2>>& GetChannels() const { return Channels; }

	/** Starts the cooldown as though a press had just fired (used by synthetic presses). */
	void StartCooldown(float Seconds) { PressCooldownRemaining = FMath::Max(Seconds, 0.f); }

private:
	TArray<FStereoButtonPressChannel, TInlineAllocator<2>> Channels;
	float PressedAmount = 0.f;
	float PressCooldownRemaining = 0.f;
	float TimeSinceInteraction = 0.f;
};
