#include "StereoButtonFloatSpring.h"

namespace
{
	FORCEINLINE float Stiffness(float Hz) { return FMath::Square(2.f * PI * Hz); }
	FORCEINLINE float Damping(float Hz, float Ratio) { return 2.f * Ratio * (2.f * PI * Hz); }
}

void FStereoButtonFloatSpring::Step(const FStereoButtonFloatSettings& S, float DeltaSeconds,
	TArrayView<const FStereoButtonContact> Contacts)
{
	// Idle motion is layered on top of the physics rather than fed through it, so it can never
	// pump energy into the springs and it costs nothing to tune.
	IdleTime += DeltaSeconds;
	IdleBob = S.IdleBobAmplitudeCm * FMath::Sin(2.f * PI * S.IdleBobHz * IdleTime);
	const float Sway = FMath::DegreesToRadians(S.IdleSwayDegrees);
	IdleSwayY = Sway * FMath::Sin(2.f * PI * S.IdleSwayHz * IdleTime);
	IdleSwayZ = Sway * FMath::Sin(2.f * PI * S.IdleSwayHz * 0.71f * IdleTime + 1.3f);

	const float Kp = Stiffness(S.PressSpringHz);
	const float Cp = Damping(S.PressSpringHz, S.PressDampingRatio);
	const float Kt = Stiffness(S.TiltSpringHz);
	const float Ct = Damping(S.TiltSpringHz, S.TiltDampingRatio);
	const float Lever = FMath::Max(S.LeverArmCm, 0.1f);
	const float MaxTilt = FMath::DegreesToRadians(S.MaxTiltDegrees);
	const float Dt = FMath::Clamp(S.SubStepSeconds, 0.001f, 0.02f);

	// Fixed sub-steps with a carried remainder; a hitch is capped so it can't dump a burst of
	// integration into one frame.
	Accumulator = FMath::Min(Accumulator + DeltaSeconds, 0.1f);
	while (Accumulator >= Dt)
	{
		Accumulator -= Dt;

		// Return springs toward neutral.
		float ForceX = -Kp * Sink - Cp * SinkVel;             // along +Sink (into the button)
		float TorqueY = -Kt * TiltY - Ct * TiltYVel;
		float TorqueZ = -Kt * TiltZ - Ct * TiltZVel;

		// Penalty contacts. A finger pad buried past the current plate surface pushes the plate in
		// (along -X) at that point; the lever arm turns that into a tilt. r x F with r = (0, y, z)
		// and F = (-f, 0, 0) gives torqueY = -z f, torqueZ = +y f.
		for (const FStereoButtonContact& C : Contacts)
		{
			const float Depth = SurfaceXAt(C.Local) - C.PadX;     // > 0 when the pad is past the surface
			if (Depth <= 0.f)
			{
				continue;
			}
			const float F = S.ContactStiffness * Depth;
			ForceX += F - S.ContactDamping * SinkVel;
			const float Ny = FMath::Clamp(C.Local.X / Lever, -1.f, 1.f);
			const float Nz = FMath::Clamp(C.Local.Y / Lever, -1.f, 1.f);
			// Torque is normalised by the lever arm so LeverArmCm is "how far out gives full tilt"
			// and MaxTiltDegrees bounds the answer, whatever the plate's physical size.
			TorqueY += -Nz * F * 0.02f - S.ContactDamping * 0.02f * TiltYVel;
			TorqueZ += Ny * F * 0.02f - S.ContactDamping * 0.02f * TiltZVel;
		}

		// Semi-implicit Euler: velocity first, then position, which keeps stiff springs stable.
		SinkVel += ForceX * Dt;
		Sink += SinkVel * Dt;
		TiltYVel += TorqueY * Dt;
		TiltY += TiltYVel * Dt;
		TiltZVel += TorqueZ * Dt;
		TiltZ += TiltZVel * Dt;

		// Hard limits. Hitting one kills the velocity into it so the plate doesn't wind up.
		if (Sink > S.MaxSinkCm) { Sink = S.MaxSinkCm; SinkVel = FMath::Min(SinkVel, 0.f); }
		if (Sink < -S.MaxRiseCm) { Sink = -S.MaxRiseCm; SinkVel = FMath::Max(SinkVel, 0.f); }
		if (TiltY > MaxTilt) { TiltY = MaxTilt; TiltYVel = FMath::Min(TiltYVel, 0.f); }
		if (TiltY < -MaxTilt) { TiltY = -MaxTilt; TiltYVel = FMath::Max(TiltYVel, 0.f); }
		if (TiltZ > MaxTilt) { TiltZ = MaxTilt; TiltZVel = FMath::Min(TiltZVel, 0.f); }
		if (TiltZ < -MaxTilt) { TiltZ = -MaxTilt; TiltZVel = FMath::Max(TiltZVel, 0.f); }
	}
}

void FStereoButtonFloatSpring::Reset()
{
	Sink = SinkVel = TiltY = TiltZ = TiltYVel = TiltZVel = 0.f;
	Accumulator = 0.f;
}

void FStereoButtonFloatSpring::Impulse(const FStereoButtonFloatSettings& S, const FVector2D& Local, float SpeedCmPerSec)
{
	const float Lever = FMath::Max(S.LeverArmCm, 0.1f);
	SinkVel += SpeedCmPerSec;
	TiltYVel += -FMath::Clamp(Local.Y / Lever, -1.f, 1.f) * SpeedCmPerSec * 0.02f;
	TiltZVel += FMath::Clamp(Local.X / Lever, -1.f, 1.f) * SpeedCmPerSec * 0.02f;
}

FRotator FStereoButtonFloatSpring::ToRotator() const
{
	// UE's pitch rotates the local up vector toward -X for a positive angle (the opposite hand to
	// a right-handed rotation about Y), while yaw IS right-handed about Z. So pitch = -TiltY.
	return FRotator(
		-FMath::RadiansToDegrees(TiltY + IdleSwayY),
		FMath::RadiansToDegrees(TiltZ + IdleSwayZ),
		0.f);
}
