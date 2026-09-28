#include "StereoButtonHandTracker.h"

#include "Components/SkinnedMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogStereoButtonHands, Log, All);

const FVector FStereoButtonHandTracker::InvalidTip(TNumericLimits<float>::Max());

namespace
{
	// The index-fingertip bone: prefers the last bone named both "index" and "tip" (the distal
	// marker), falling back to the last "index" bone — the same rule AHorizonButtonBase and
	// UHandWidgetInteractionComponent use, so every pressable surface reads the same tip.
	FName FindIndexTipBone(const USkinnedMeshComponent* Mesh)
	{
		FName Result = NAME_None;
		const int32 Num = Mesh->GetNumBones();
		for (int32 i = 0; i < Num; ++i)
		{
			const FString Name = Mesh->GetBoneName(i).ToString().ToLower();
			if (Name.Contains(TEXT("index")) && Name.Contains(TEXT("tip")))
			{
				Result = Mesh->GetBoneName(i);
			}
		}
		if (Result.IsNone())
		{
			for (int32 i = 0; i < Num; ++i)
			{
				if (Mesh->GetBoneName(i).ToString().Contains(TEXT("index"), ESearchCase::IgnoreCase))
				{
					Result = Mesh->GetBoneName(i);
				}
			}
		}
		return Result;
	}
}

bool FStereoButtonHandTracker::AnyValid(const TArray<FVector>& Tips)
{
	return Tips.ContainsByPredicate([](const FVector& T) { return IsValidTip(T); });
}

void FStereoButtonHandTracker::Gather(UWorld* World, FName MeshClassFilter, float MergeDistanceCm, TArray<FVector>& OutTips)
{
	OutTips.Reset();
	APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!Pawn)
	{
		return;
	}

	bool bNeedResolve = CachedPawn.Get() != Pawn || Meshes.Num() == 0;
	if (!bNeedResolve)
	{
		bNeedResolve = Meshes.ContainsByPredicate([](const TWeakObjectPtr<USkinnedMeshComponent>& M) { return !M.IsValid(); });
	}
	if (bNeedResolve)
	{
		Resolve(Pawn, MeshClassFilter);
	}

	for (int32 i = 0; i < Meshes.Num(); ++i)
	{
		USkinnedMeshComponent* Mesh = Meshes[i].Get();
		OutTips.Add(Mesh ? Mesh->GetBoneLocation(Bones[i], EBoneSpaces::WorldSpace) : InvalidTip);
	}

	// Collapse tips that are really the same finger. The LOWEST slot wins so the surviving index
	// stays stable; merged slots are blanked, not removed.
	const float MergeSq = FMath::Square(FMath::Max(MergeDistanceCm, 0.f));
	if (MergeSq > 0.f)
	{
		for (int32 i = 0; i < OutTips.Num(); ++i)
		{
			if (!IsValidTip(OutTips[i])) continue;
			for (int32 j = i + 1; j < OutTips.Num(); ++j)
			{
				if (IsValidTip(OutTips[j]) && FVector::DistSquared(OutTips[i], OutTips[j]) <= MergeSq)
				{
					OutTips[j] = InvalidTip;
				}
			}
		}
	}
}

void FStereoButtonHandTracker::Resolve(APawn* Pawn, FName MeshClassFilter)
{
	CachedPawn = Pawn;
	Meshes.Reset();
	Bones.Reset();

	TArray<USkinnedMeshComponent*> All;
	Pawn->GetComponents(All);

	const FString ClassFilter = MeshClassFilter.IsNone() ? FString() : MeshClassFilter.ToString();

	struct FCandidate { USkinnedMeshComponent* Mesh; FName Tip; bool bTracked; };
	TArray<FCandidate, TInlineAllocator<4>> Candidates;
	bool bAnyTracked = false;
	for (USkinnedMeshComponent* Mesh : All)
	{
		if (!Mesh) continue;
		const FName Tip = FindIndexTipBone(Mesh);
		if (Tip.IsNone()) continue;
		const bool bTracked = !ClassFilter.IsEmpty()
			&& Mesh->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase);
		bAnyTracked |= bTracked;
		Candidates.Add({ Mesh, Tip, bTracked });
	}

	// Keep only the tracked rigs when the pawn has any. The filter can only narrow a pawn it
	// recognises — never blank out one it doesn't.
	FString Used, Skipped;
	for (const FCandidate& C : Candidates)
	{
		if (bAnyTracked && !C.bTracked)
		{
			Skipped += FString::Printf(TEXT("%s%s"), Skipped.IsEmpty() ? TEXT("") : TEXT(", "), *C.Mesh->GetName());
			continue;
		}
		Meshes.Add(C.Mesh);
		Bones.Add(C.Tip);
		Used += FString::Printf(TEXT("%s%s:%s"), Used.IsEmpty() ? TEXT("") : TEXT(", "), *C.Mesh->GetName(), *C.Tip.ToString());
	}

	UE_LOG(LogStereoButtonHands, Log, TEXT("Fingertips resolved on %s: using %d [%s]; skipped %d non-'%s' rig(s) [%s]%s"),
		*Pawn->GetName(), Meshes.Num(), *Used, Candidates.Num() - Meshes.Num(), *ClassFilter, *Skipped,
		Meshes.Num() > 2 ? TEXT(" -- MORE THAN 2: widen FingertipMeshClassFilter or rely on FingertipMergeDistanceCm") : TEXT(""));
}
