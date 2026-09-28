#pragma once

#include "CoreMinimal.h"
#include "StereoButtonFloatSpring.generated.h"

/**
 * Tuning for the floating plate. Springs are specified as frequency + damping ratio rather than
 * raw stiffness so the numbers mean something: 2 Hz at ratio 0.3 is a plate that overshoots once
 * or twice and settles in about a second. Ratio 1.0 is critically damped (no overshoot).
 */
USTRUCT(BlueprintType)
struct FStereoButtonFloatSettings
{
	GENERATED_BODY()

	/** How fast the plate returns along its press axis. Higher = snappier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float", meta=(ClampMin="0.1", ClampMax="20"))
	float PressSpringHz = 2.2f;

	/** 0..1. Below 1 the plate overshoots and floats back; 1 stops dead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float", meta=(ClampMin="0.02", ClampMax="2"))
	float PressDampingRatio = 0.28f;

	/** How fast a tilt rights itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float", meta=(ClampMin="0.1", ClampMax="20"))
	float TiltSpringHz = 1.8f;

	/** 0..1. Lower wobbles longer after a tap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float", meta=(ClampMin="0.02", ClampMax="2"))
	float TiltDampingRatio = 0.22f;

	/**
	 * How hard a finger pushes the plate per cm it is buried past the plate surface (1/s²). Higher
	 * = the plate hugs the fingertip more tightly; too high and it starts to buzz. 1500-4000 is
	 * the useful range at the default sub-step.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float", meta=(ClampMin="0", ClampMax="20000"))
	float ContactStiffness = 2500.f;

	/** Velocity damping applied only while a finger is in contact, so the plate doesn't ring against the finger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float", meta=(ClampMin="0", ClampMax="200"))
	float ContactDamping = 25.f;

	/**
	 * How far off-centre a contact must be for full leverage. Torque scales with the offset up to
	 * this distance, in cm. Smaller = a light touch near the edge tilts more.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float", meta=(ClampMin="0.1"))
	float LeverArmCm = 6.f;

	/** Largest tilt the plate is allowed, degrees, either axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float", meta=(ClampMin="0", ClampMax="60"))
	float MaxTiltDegrees = 12.f;

	/** How far past neutral the plate may sink visually, cm. Usually PressDepth plus a little. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float", meta=(ClampMin="0"))
	float MaxSinkCm = 5.f;

	/** How far the plate may float forward of neutral on an overshoot, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float", meta=(ClampMin="0"))
	float MaxRiseCm = 1.2f;

	/** Gentle resting bob along the press axis, cm. 0 = perfectly still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float|Idle", meta=(ClampMin="0"))
	float IdleBobAmplitudeCm = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float|Idle", meta=(ClampMin="0"))
	float IdleBobHz = 0.35f;

	/** Gentle resting sway, degrees. 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float|Idle", meta=(ClampMin="0"))
	float IdleSwayDegrees = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float|Idle", meta=(ClampMin="0"))
	float IdleSwayHz = 0.23f;

	/** Physics sub-step, seconds. The contact spring is stiff; keep this at or below 1/240. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Float|Advanced", meta=(ClampMin="0.001", ClampMax="0.02"))
	float SubStepSeconds = 1.f / 240.f;
};

/** One thing touching the plate this frame, in the plate's neutral local frame. */
struct FStereoButtonContact
{
	/** Contact position on the face plane, cm: Y right, Z up. */
	FVector2D Local = FVector2D::ZeroVector;
	/** Where the presser's leading surface is along the press axis, cm. Negative = past the neutral face. */
	float PadX = 0.f;
};

/**
 * A rigid plate on a centre spring with torsional springs on its two in-plane axes, pushed by
 * penalty forces at the contact points. Unit mass, unit inertia — everything is expressed in
 * cm and radians per second.
 *
 * Frame: the face is the local YZ plane at X = 0, normal +X toward the user. Fingers come in
 * along -X. Sink is measured positive INTO the button (-X). Tilt angles are right-handed
 * rotations about the local Y and Z axes; ToRotator() converts them to UE's rotator handedness.
 */
class HVPSTEREOBUTTON_API FStereoButtonFloatSpring
{
public:
	/** Advances the plate. Contacts are applied as penalty forces every sub-step. */
	void Step(const FStereoButtonFloatSettings& S, float DeltaSeconds, TArrayView<const FStereoButtonContact> Contacts);

	/** Snaps the plate to neutral and clears velocities. */
	void Reset();

	/** Kicks the plate as if tapped at Local with the given impulse, cm/s along -X. For synthetic presses. */
	void Impulse(const FStereoButtonFloatSettings& S, const FVector2D& Local, float SpeedCmPerSec);

	/** Plate offset along local X, cm (negative = sunk in). Includes the idle bob. */
	float GetOffsetX() const { return -Sink + IdleBob; }

	/** Plate tilt as a UE rotator (pitch/yaw in degrees), including the idle sway. */
	FRotator ToRotator() const;

	float GetSinkCm() const { return Sink; }
	float GetTiltYRad() const { return TiltY; }
	float GetTiltZRad() const { return TiltZ; }

	/** Plate surface X at a point on the face, given the current sink and tilt (small-angle). */
	float SurfaceXAt(const FVector2D& Local) const { return -Sink - Local.X * TiltZ + Local.Y * TiltY; }

private:
	float Sink = 0.f;        // cm into the button (positive = pressed in)
	float SinkVel = 0.f;
	float TiltY = 0.f;       // radians, right-handed about local Y
	float TiltZ = 0.f;       // radians, right-handed about local Z
	float TiltYVel = 0.f;
	float TiltZVel = 0.f;
	float IdleTime = 0.f;
	float IdleBob = 0.f;     // cm, along +X
	float IdleSwayY = 0.f;   // radians
	float IdleSwayZ = 0.f;
	float Accumulator = 0.f;
};
