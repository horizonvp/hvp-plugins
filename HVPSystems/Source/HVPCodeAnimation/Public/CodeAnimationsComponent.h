#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CodeAnimationTypes.h"
#include "CodeAnimationsComponent.generated.h"

class UCodeAnimationComponent;

// Signatures mirror the original Blueprint dispatchers (String names, double alphas).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCodeAnimationsStart, const FString&, Animation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodeAnimationsEnd, const FString&, Animation, bool, Interrupted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodeAnimationsStepEvent, const FString&, Animation, const FString&, Step);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FCodeAnimationsStepTick, const FString&, Animation, const FString&, Step, double, Alpha, double, ScaledAlpha);

/**
 * A named collection of CodeAnimation timelines on one actor.
 *
 * Author the Animations map (name -> steps) in defaults; on BeginPlay one child
 * UCodeAnimationComponent is spawned per entry (gameflow autoplay off — the manager, or its owner's
 * graph, decides when things play) and every child's events are re-broadcast here with the
 * animation's name prepended, so one set of bindings serves any number of timelines.
 *
 * Events fire on this class's typed delegates AND, when the runtime class is the reparented
 * Content/HVP Blueprint shell, into the shell's original dispatchers via reflection.
 *
 * Native replacement for /Game/HVP/Systems/CodeAnimation/CodeAnimations.
 */
UCLASS(ClassGroup = (HVP), meta = (BlueprintSpawnableComponent), BlueprintType, Blueprintable)
class HVPCODEANIMATION_API UCodeAnimationsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** The timelines to create, keyed by animation name (String key, matching the Blueprint's map). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	TMap<FString, FAnimationSteps> Animations;

	/** The spawned child components, keyed by animation name. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Code Animation")
	TMap<FString, TObjectPtr<UCodeAnimationComponent>> Components;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationsStart StartAnimationEvent;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationsEnd EndAnimationEvent;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationsStepEvent StartAnimationStep;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationsStepTick TickAnimationStep;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationsStepEvent EndAnimationStep;

	UFUNCTION(BlueprintCallable, Category = "Code Animation")
	void PlayAnimation(const FString& Name, bool ResetIfPlaying = false, bool Reverse = false);

	UFUNCTION(BlueprintPure, Category = "Code Animation")
	UCodeAnimationComponent* GetAnimationComponent(const FString& Animation) const;

	virtual void BeginPlay() override;

	// Child -> manager forwarding (called by UCodeAnimationComponent when a forward target is set).
	void NotifyStart(const FString& Animation);
	void NotifyEnd(const FString& Animation, bool bInterrupted);
	void NotifyStepStart(const FString& Animation, const FString& Step);
	void NotifyStepTick(const FString& Animation, const FString& Step, double Alpha, double ScaledAlpha);
	void NotifyStepEnd(const FString& Animation, const FString& Step);
};
