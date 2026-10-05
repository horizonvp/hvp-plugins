#pragma once

#include "CoreMinimal.h"
#include "Kismet/KismetMathLibrary.h"
#include "StructUtils/PropertyBag.h"

#include "CodeAnimationWebTypes.generated.h"

class UCurveFloat;

namespace CodeAnimWeb
{
	/** Variable metadata marking a Web's variable as an Animation Output. Set from the variable's Details panel. */
	inline const FName OutputMetaKey(TEXT("CodeAnimOutput"));

	/** Variable metadata holding the output's ECodeAnimLerpPolicy, by enumerator name. */
	inline const FName LerpMetaKey(TEXT("CodeAnimLerp"));

	/**
	 * Metadata on an event dispatcher the Web made for an output, holding that output's variable GUID.
	 * How the Web recognises its own dispatchers, and keeps each one matched to its output through renames.
	 */
	inline const FName EventMetaKey(TEXT("CodeAnimOutputEvent"));

	/**
	 * EventMetaKey's value on the one dispatcher that is not any single output's: On Outputs Changed,
	 * fired once per update with every output that changed.
	 */
	inline const TCHAR* AllOutputsEventKey = TEXT("AllOutputs");

	/**
	 * Inputs of the graphs a Web calls, by position: a state graph gets TimeInState; transition and
	 * Custom Lerp graphs get Alpha (eased) then Progress (linear, 0-1).
	 */
	inline const FName TimeInStateParam(TEXT("TimeInState"));
	inline const FName AlphaParam(TEXT("Alpha"));
	inline const FName ProgressParam(TEXT("Progress"));
}

/** How an output travels between two values during a transition. */
UENUM(BlueprintType)
enum class ECodeAnimLerpPolicy : uint8
{
	/**
	 * Lerp numbers, vectors, rotators, colours and transforms. Anything that cannot be lerped - meshes,
	 * bools, enums - switches halfway through.
	 */
	Auto			UMETA(DisplayName = "Auto"),
	/** Switch to the new value as soon as the transition starts. */
	SnapAtStart		UMETA(DisplayName = "Snap at Start"),
	/** Switch when the transition's alpha reaches 0.5. */
	SnapAtHalf		UMETA(DisplayName = "Snap at Half"),
	/** Keep the old value until the transition finishes. */
	SnapAtEnd		UMETA(DisplayName = "Snap at End"),
	/**
	 * Blend as Auto, then run this output's Custom Lerp graph, which can overwrite this output and any
	 * other. Custom Lerps run last, after any transition graph.
	 */
	Custom			UMETA(DisplayName = "Custom"),
};

/** What Transition Traversal Only minimises when it picks a route between two states. */
UENUM(BlueprintType)
enum class ECodeAnimWebPathCost : uint8
{
	/** The fewest transitions; the shortest total time between routes that tie. */
	FewestTransitions	UMETA(DisplayName = "Fewest Transitions"),
	/** The shortest total duration; the fewest transitions between routes that tie. */
	ShortestTime		UMETA(DisplayName = "Shortest Time"),
};

/**
 * One Animation Output, as baked from its variable's metadata when the Web compiles. Cooked builds
 * have no metadata, so this is what they run from.
 */
USTRUCT()
struct HVPCODEANIMWEB_API FCodeAnimOutputSpec
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	/** The variable's Blueprint GUID: stable across renames, so per-state overrides follow a renamed output. */
	UPROPERTY()
	FGuid ID;

	UPROPERTY()
	ECodeAnimLerpPolicy Lerp = ECodeAnimLerpPolicy::Auto;

	/** The output's On <Output> Changed dispatcher, fired with the Web and the new value. */
	UPROPERTY()
	FName EventName;
};

/**
 * A state of a Web, by the enumerator's internal name ("NewEnumerator3" for a Blueprint enum).
 * Internal names survive renaming and reordering in the enum editor, where display names and values do not.
 */
USTRUCT(BlueprintType)
struct HVPCODEANIMWEB_API FCodeAnimWebStateRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "State")
	FName Key;

	bool IsSet() const { return !Key.IsNone(); }
	bool operator==(const FCodeAnimWebStateRef& Other) const { return Key == Other.Key; }
};

/** Duration and shape of a transition. */
USTRUCT(BlueprintType)
struct HVPCODEANIMWEB_API FCodeAnimWebTransitionTiming
{
	GENERATED_BODY()

	/** Seconds of game time. Zero switches instantly. */
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", Units = "s"))
	float Duration = 0.25f;

	/** Shape of the alpha. Ignored when a Curve is set. */
	UPROPERTY(EditAnywhere, Category = "Timing")
	TEnumAsByte<EEasingFunc::Type> Easing = EEasingFunc::EaseInOut;

	/** Exponent for the EaseIn/EaseOut/EaseInOut shapes. */
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0.01"))
	float BlendExponent = 2.0f;

	/**
	 * Optional alpha curve over 0-1 time, replacing Easing. Values outside 0-1 overshoot: a curve that
	 * peaks at 1.2 before settling at 1 gives every lerped output a pulse past its target.
	 */
	UPROPERTY(EditAnywhere, Category = "Timing")
	TObjectPtr<UCurveFloat> Curve;

	/** Alpha for linear progress Progress (0-1). */
	double AlphaAt(double Progress) const;
};

/** An authored transition between two states. Pairs without one use the Web's Default Transition. */
USTRUCT(BlueprintType)
struct HVPCODEANIMWEB_API FCodeAnimWebTransition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Transition")
	FCodeAnimWebStateRef From;

	UPROPERTY(EditAnywhere, Category = "Transition")
	FCodeAnimWebStateRef To;

	/**
	 * Also covers To -> From, played in reverse: the same motion run backwards, so an ease-in going
	 * out becomes an ease-out coming back.
	 */
	UPROPERTY(EditAnywhere, Category = "Transition")
	bool bTwoWay = false;

	UPROPERTY(EditAnywhere, Category = "Transition", meta = (ShowOnlyInnerProperties))
	FCodeAnimWebTransitionTiming Timing;

	/** The transition graph's function, if it has one. Resolved from GraphGuid on compile. */
	UPROPERTY()
	FName GraphFunction;

	/** The graph itself, by GUID, so renaming the function in My Blueprint does not unlink it. */
	UPROPERTY()
	FGuid GraphGuid;
};

/** An output's Custom Lerp graph. */
USTRUCT()
struct HVPCODEANIMWEB_API FCodeAnimCustomLerp
{
	GENERATED_BODY()

	/** FCodeAnimOutputSpec::ID of the output. */
	UPROPERTY()
	FGuid Output;

	UPROPERTY()
	FName GraphFunction;

	UPROPERTY()
	FGuid GraphGuid;
};

/**
 * What one state does to the outputs. Every state covers every output: an output left unticked holds
 * its variable default in this state, one ticked holds the value set here.
 */
USTRUCT()
struct HVPCODEANIMWEB_API FCodeAnimWebStateEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "State")
	FName Key;

	/** The enumerator's display name when last synced; for the editor only. */
	UPROPERTY(VisibleAnywhere, Category = "State")
	FText DisplayName;

	/**
	 * The enumerator is gone from the enum. Its values are kept rather than silently discarded, in
	 * case the removal was a mistake; Remove Orphaned States deletes them for good.
	 */
	UPROPERTY(VisibleAnywhere, Category = "State")
	bool bOrphaned = false;

	/** One property per output. Only the ones in OverriddenOutputs mean anything. */
	UPROPERTY(EditAnywhere, Category = "State")
	FInstancedPropertyBag Values;

	/** IDs (FCodeAnimOutputSpec::ID) of the outputs this state sets. */
	UPROPERTY()
	TArray<FGuid> OverriddenOutputs;

	/**
	 * The state graph's function, if it has one: run every frame this state is in play, after the
	 * values above are applied, to set outputs from TimeInState. Resolved from GraphGuid on compile.
	 */
	UPROPERTY()
	FName GraphFunction;

	UPROPERTY()
	FGuid GraphGuid;

#if WITH_EDITORONLY_DATA
	/** Where this state sits in the Web Graph. */
	UPROPERTY()
	FVector2D EditorPosition = FVector2D::ZeroVector;

	UPROPERTY()
	bool bHasEditorPosition = false;
#endif
};
