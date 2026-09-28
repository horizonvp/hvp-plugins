#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class APawn;
class USkinnedMeshComponent;
class UWorld;

/**
 * Reads the player's index fingertips off the pawn's hand meshes, positionally stable frame to
 * frame. Extracted from AHorizonButtonBase::GatherIndexFingertips so the actor stays readable.
 *
 * Output contract (the press latch relies on it): one slot per cached hand mesh, in cache order,
 * with an untracked or merged hand leaving its slot filled by an INVALID sentinel rather than
 * compacting the array. A tip's index IS its identity to the latch; compacting would silently
 * re-point a latch at the other hand.
 */
class HVPSTEREOBUTTON_API FStereoButtonHandTracker
{
public:
	/** Sentinel for a slot whose hand is not available this frame. */
	static const FVector InvalidTip;
	static FORCEINLINE bool IsValidTip(const FVector& Tip) { return Tip.X < InvalidTip.X; }

	/**
	 * @param MeshClassFilter  Substring matched against a mesh component's CLASS name. When any mesh
	 *                         matches, only matching meshes supply tips; when none does, every mesh
	 *                         with an index tip is used. Exists because a VR pawn carries a tracked rig
	 *                         and a controller-posed rig per side, ~1 cm apart — two tips per finger.
	 * @param MergeDistanceCm  Tips closer than this collapse onto the lowest slot (0 disables).
	 */
	void Gather(UWorld* World, FName MeshClassFilter, float MergeDistanceCm, TArray<FVector>& OutTips);

	/** True when at least one slot holds a real tip. */
	static bool AnyValid(const TArray<FVector>& Tips);

	void Invalidate() { CachedPawn = nullptr; Meshes.Reset(); Bones.Reset(); }

private:
	void Resolve(APawn* Pawn, FName MeshClassFilter);

	TWeakObjectPtr<APawn> CachedPawn;
	TArray<TWeakObjectPtr<USkinnedMeshComponent>> Meshes;
	TArray<FName> Bones;
};
