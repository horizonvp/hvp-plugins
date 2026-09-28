#include "StereoButtonPressModel.h"

void FStereoButtonPressModel::Update(const TArray<FStereoButtonPresserSample>& Samples,
	const FStereoButtonPressSettings& Settings, float DeltaSeconds, TArray<FStereoButtonPressEdge>& OutEdges)
{
	TimeSinceInteraction += DeltaSeconds;
	if (PressCooldownRemaining > 0.f)
	{
		PressCooldownRemaining = FMath::Max(0.f, PressCooldownRemaining - DeltaSeconds);
	}
	const float Grace = FMath::Max(Settings.UnlatchGraceSeconds, 0.f);

	// --- 1. Refresh the open channels ------------------------------------------------------
	// Identity matters, not just presence: a sample only refreshes the channel it belongs to, so
	// a second finger arriving can neither take over an existing channel nor contribute to it.
	for (int32 i = Channels.Num() - 1; i >= 0; --i)
	{
		FStereoButtonPressChannel& C = Channels[i];
		const FStereoButtonPresserSample* Found = Samples.FindByPredicate(
			[&C](const FStereoButtonPresserSample& S) { return S.Matches(C.Source, C.FingertipIndex, C.Comp.Get()); });

		if (Found)
		{
			if (C.bDetached)
			{
				// Back before the wind-down finished: re-attach rather than open a fresh channel.
				// bPressed staying set is what stops a finger that flickers out and back from
				// skipping its release and banking an extra press.
				C.bDetached = false;
				C.bFired = false;
			}
			C.TimeSinceSeen = 0.f;
			C.Amount = Found->Amount;
			C.PenetrationCm = Found->PenetrationCm;
			C.ContactLocal = Found->ContactLocal;
			TimeSinceInteraction = 0.f;
		}
		else
		{
			// Gone this frame — hold the depth through the grace window so a dropped tracking
			// frame doesn't read as the finger leaving.
			C.TimeSinceSeen += DeltaSeconds;
			if (C.TimeSinceSeen >= Grace)
			{
				C.bDetached = true;
			}
		}

		if (C.bDetached)
		{
			C.Amount = FMath::FInterpTo(C.Amount, 0.f, DeltaSeconds, Settings.ReturnSpeed);
			C.PenetrationCm = FMath::FInterpTo(C.PenetrationCm, 0.f, DeltaSeconds, Settings.ReturnSpeed);
			if (!C.bPressed && C.Amount <= KINDA_SMALL_NUMBER)
			{
				Channels.RemoveAt(i);
			}
		}
	}

	// --- 2. Acquire new channels -----------------------------------------------------------
	// The ONLY path that arms a press. Requires a real contact (bCanAcquire), never a presser
	// lingering in the exit hysteresis band on its way out.
	const int32 MaxPressers = FMath::Max(Settings.MaxConcurrentPressers, 1);
	for (;;)
	{
		if (GetLiveChannelCount() >= MaxPressers)
		{
			break;
		}

		const FStereoButtonPresserSample* Best = nullptr;
		for (const FStereoButtonPresserSample& S : Samples)
		{
			if (!S.bCanAcquire)
			{
				continue;
			}
			const bool bBound = Channels.ContainsByPredicate([&S](const FStereoButtonPressChannel& C)
			{
				return S.Matches(C.Source, C.FingertipIndex, C.Comp.Get());
			});
			if (bBound)
			{
				continue;
			}
			if (!Best || S.Amount > Best->Amount)
			{
				Best = &S;
			}
		}
		if (!Best)
		{
			break;
		}

		FStereoButtonPressChannel& New = Channels.AddDefaulted_GetRef();
		New.Source = Best->Source;
		New.FingertipIndex = Best->FingertipIndex;
		New.Comp = Best->Comp;
		New.Amount = Best->Amount;
		New.PenetrationCm = Best->PenetrationCm;
		New.ContactLocal = Best->ContactLocal;
		TimeSinceInteraction = 0.f;
	}

	// --- 3. Head amount: the deepest channel ---------------------------------------------
	if (Channels.Num() > 0)
	{
		float Deepest = 0.f;
		for (const FStereoButtonPressChannel& C : Channels)
		{
			Deepest = FMath::Max(Deepest, C.Amount);
		}
		PressedAmount = Deepest;
	}
	else
	{
		PressedAmount = FMath::FInterpTo(PressedAmount, 0.f, DeltaSeconds, Settings.ReturnSpeed);
	}

	// --- 4. Press / release edges, per channel ---------------------------------------------
	for (int32 i = 0; i < Channels.Num(); ++i)
	{
		FStereoButtonPressChannel& C = Channels[i];
		if (Settings.bMayFire && !C.bPressed && !C.bFired && !C.bDetached
			&& PressCooldownRemaining <= 0.f && C.Amount >= Settings.PressThreshold)
		{
			C.bPressed = true;
			C.bFired = true;
			// Button-wide: with two hands this delays the second press by a frame or two rather
			// than dropping it, since the gate is re-tested every tick while that presser stays deep.
			PressCooldownRemaining = FMath::Max(Settings.PressCooldownSeconds, 0.f);
			OutEdges.Add({ true, i });
		}
		else if (C.bPressed && C.Amount <= Settings.ReleaseThreshold)
		{
			C.bPressed = false;
			if (Settings.bRearmOnRelease)
			{
				C.bFired = false;
			}
			OutEdges.Add({ false, i });
		}
	}
}

void FStereoButtonPressModel::Reset()
{
	Channels.Reset();
	PressedAmount = 0.f;
	PressCooldownRemaining = 0.f;
	TimeSinceInteraction = 0.f;
}

bool FStereoButtonPressModel::IsPressed() const
{
	return Channels.ContainsByPredicate([](const FStereoButtonPressChannel& C) { return C.bPressed; });
}

int32 FStereoButtonPressModel::GetLiveChannelCount() const
{
	int32 Count = 0;
	for (const FStereoButtonPressChannel& C : Channels)
	{
		Count += C.bDetached ? 0 : 1;
	}
	return Count;
}
