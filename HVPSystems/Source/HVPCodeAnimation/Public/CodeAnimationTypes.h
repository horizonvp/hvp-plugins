#pragma once

#include "CoreMinimal.h"
#include "CodeAnimationTypes.generated.h"

class UFileMediaSource;
class UMediaPlayer;
class USoundBase;

/**
 * One named sub-range of a CodeAnimation's 0-1 timeline. While the playthrough alpha is inside
 * [StartPercent, EndPercent] the step ticks with its own re-normalised (and eased) 0-1 alpha.
 *
 * Native replacement for the AnimationStep user-defined struct.
 */
USTRUCT(BlueprintType)
struct HVPCODEANIMATION_API FAnimationStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double StartPercent = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double EndPercent = 1.0;
};

/**
 * A named list of animation steps — the value type of the CodeAnimations manager's Animations map.
 * Native replacement for the AnimationSteps user-defined struct.
 */
USTRUCT(BlueprintType)
struct HVPCODEANIMATION_API FAnimationSteps
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	TArray<FAnimationStep> Steps;
};

/**
 * A movie cued at a point on the timeline: when the playthrough alpha reaches StartPositionPercent,
 * Movie is opened on Player and the StartMovie event fires.
 *
 * Native replacement for the MovieStep user-defined struct.
 */
USTRUCT(BlueprintType)
struct HVPCODEANIMATION_API FMovieStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	FName Name = TEXT("Default Movie");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	TObjectPtr<UFileMediaSource> Movie = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	TObjectPtr<UMediaPlayer> Player = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double StartPositionPercent = 0.0;
};

/**
 * A sound cued at a point on the timeline: when the playthrough alpha reaches StartPositionPercent,
 * Sound is set on the AudioComponent tagged AudioComponentTag (on Player, or the owner when Player
 * is unset), played, and the StartSound event fires.
 *
 * Native replacement for the SoundStep user-defined struct.
 */
USTRUCT(BlueprintType)
struct HVPCODEANIMATION_API FSoundStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	FName Name = TEXT("Default Sound");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	TObjectPtr<USoundBase> Sound = nullptr;

	/** Component tag identifying which AudioComponent on the target actor plays this sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	FName AudioComponentTag;

	/** Actor whose tagged AudioComponent plays the sound; the owning actor when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	TObjectPtr<AActor> Player = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double StartPositionPercent = 0.0;
};
