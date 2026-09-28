#include "CodeAnimationsComponent.h"
#include "CodeAnimationComponent.h"
#include "ShellDelegateUtil.h"
#include "GameFramework/Actor.h"

void UCodeAnimationsComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	for (const TPair<FString, FAnimationSteps>& Pair : Animations)
	{
		const FName ComponentName = MakeUniqueObjectName(Owner, UCodeAnimationComponent::StaticClass(), FName(*Pair.Key));
		UCodeAnimationComponent* Child = NewObject<UCodeAnimationComponent>(Owner, ComponentName);
		Child->AnimationSteps = Pair.Value.Steps;
		// Manager children never self-start off the gameflow; the manager (or its owner's graph)
		// decides when each named animation plays.
		Child->bPlayOnEnter = false;
		Child->SetForwardTarget(this, Pair.Key);
		Child->RegisterComponent();
		Components.Add(Pair.Key, Child);
	}
	UE_LOG(LogHVPCodeAnimation, Log, TEXT("%s: spawned %d named animations"), *GetReadableName(), Components.Num());
}

void UCodeAnimationsComponent::PlayAnimation(const FString& Name, bool ResetIfPlaying, bool Reverse)
{
	if (UCodeAnimationComponent* Child = GetAnimationComponent(Name))
	{
		Child->PlayAnimation(Reverse, ResetIfPlaying);
	}
	else
	{
		UE_LOG(LogHVPCodeAnimation, Warning, TEXT("PlayAnimation: no animation named '%s' on %s"),
			*Name, *GetReadableName());
	}
}

UCodeAnimationComponent* UCodeAnimationsComponent::GetAnimationComponent(const FString& Animation) const
{
	const TObjectPtr<UCodeAnimationComponent>* Found = Components.Find(Animation);
	return Found ? Found->Get() : nullptr;
}

void UCodeAnimationsComponent::NotifyStart(const FString& Animation)
{
	StartAnimationEvent.Broadcast(Animation);
	HVPShellDelegates::Fire(this, TEXT("Start Animation"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Animation"), Animation);
	});
}

void UCodeAnimationsComponent::NotifyEnd(const FString& Animation, bool bInterrupted)
{
	EndAnimationEvent.Broadcast(Animation, bInterrupted);
	HVPShellDelegates::Fire(this, TEXT("End Animation"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Animation"), Animation);
		HVPShellDelegates::SetBool(Sig, Parms, TEXT("Inturrupted"), bInterrupted);
	});
}

void UCodeAnimationsComponent::NotifyStepStart(const FString& Animation, const FString& Step)
{
	StartAnimationStep.Broadcast(Animation, Step);
	HVPShellDelegates::Fire(this, TEXT("Start Animation Step"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Animation"), Animation);
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Step"), Step);
	});
}

void UCodeAnimationsComponent::NotifyStepTick(const FString& Animation, const FString& Step, double Alpha, double ScaledAlpha)
{
	TickAnimationStep.Broadcast(Animation, Step, Alpha, ScaledAlpha);
	HVPShellDelegates::Fire(this, TEXT("Tick Animation Step"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Animation"), Animation);
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Step"), Step);
		HVPShellDelegates::SetDouble(Sig, Parms, TEXT("Alpha"), Alpha);
		HVPShellDelegates::SetDouble(Sig, Parms, TEXT("Scaled Alpha"), ScaledAlpha);
	});
}

void UCodeAnimationsComponent::NotifyStepEnd(const FString& Animation, const FString& Step)
{
	EndAnimationStep.Broadcast(Animation, Step);
	HVPShellDelegates::Fire(this, TEXT("End Animation Step"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Animation"), Animation);
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Step"), Step);
	});
}
