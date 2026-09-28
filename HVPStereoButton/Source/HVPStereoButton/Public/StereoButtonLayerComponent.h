#pragma once

#include "CoreMinimal.h"
#include "Components/StereoLayerComponent.h"
#include "StereoButtonLayerComponent.generated.h"

/**
 * UStereoLayerComponent with the button's defaults baked in.
 *
 * Exists because UStereoLayerComponent::StereoLayerType is protected with no public setter — a
 * subclass is the only way to get SLT_WorldLocked from code. The engine default, SLT_FaceLocked,
 * interprets the transform in view space and puts a world-placed quad centimetres from the
 * viewer's face in an arbitrary direction (see Docs/StereoQuadActor.md for the day that cost).
 *
 * bSupportsDepth is ON. That is what lets the button be occluded by hands and scene geometry
 * instead of always drawing on top — see Docs/StereoButton.md, "Depth".
 */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent))
class HVPSTEREOBUTTON_API UStereoButtonLayerComponent : public UStereoLayerComponent
{
	GENERATED_BODY()

public:
	UStereoButtonLayerComponent();

	/** Re-asserts world-locked. Protected on the base, so this is the only route from outside. */
	void EnsureWorldLocked();
};
