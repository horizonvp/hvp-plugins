#include "AssetSuffixResolver.h"

#include "AssetNamingLog.h"
#include "Components/ActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "UObject/ObjectRedirector.h"

namespace
{
	/**
	 * Suffix -> class path. Order here is presentational only; resolution is by depth.
	 *
	 * _AC, _AW, _BFL, _BI, _BML and _SMC are absent on purpose: they're all plain UBlueprint and are
	 * handled by ResolveBlueprintSuffix below.
	 *
	 * A new suffix needs TWO entries: one here (or in ResolveBlueprintSuffix) so the watcher knows
	 * which type gets it, and one in AssetNamingConfig's BuiltInSuffixes so the name is recognised
	 * as already carrying a suffix. Either alone does nothing visible.
	 */
	const TCHAR* const ClassRules[][2] = {
		// Textures
		{ TEXT("_T"),     TEXT("/Script/Engine.Texture") },
		{ TEXT("_RT"),    TEXT("/Script/Engine.TextureRenderTarget") },

		// Geometry / animation
		{ TEXT("_SM"),    TEXT("/Script/Engine.StaticMesh") },
		{ TEXT("_SKM"),   TEXT("/Script/Engine.SkeletalMesh") },
		{ TEXT("_SKEL"),  TEXT("/Script/Engine.Skeleton") },
		{ TEXT("_PA"),    TEXT("/Script/Engine.PhysicsAsset") },
		{ TEXT("_A"),     TEXT("/Script/Engine.AnimSequence") },
		{ TEXT("_SMF"),   TEXT("/Script/Foliage.FoliageType_InstancedStaticMesh") },

		// Materials
		{ TEXT("_M"),     TEXT("/Script/Engine.Material") },
		{ TEXT("_MI"),    TEXT("/Script/Engine.MaterialInstanceConstant") },
		{ TEXT("_MF"),    TEXT("/Script/Engine.MaterialFunction") },
		{ TEXT("_MPC"),   TEXT("/Script/Engine.MaterialParameterCollection") },

		// Audio
		{ TEXT("_SW"),    TEXT("/Script/Engine.SoundWave") },
		{ TEXT("_SC"),    TEXT("/Script/Engine.SoundCue") },
		{ TEXT("_SCL"),   TEXT("/Script/Engine.SoundClass") },
		{ TEXT("_SMX"),   TEXT("/Script/Engine.SoundMix") },
		{ TEXT("_CNC"),   TEXT("/Script/Engine.SoundConcurrency") },
		{ TEXT("_ATT"),   TEXT("/Script/Engine.SoundAttenuation") },
		{ TEXT("_MS"),    TEXT("/Script/MetasoundEngine.MetaSoundSource") },
		{ TEXT("_MS"),    TEXT("/Script/MetasoundEngine.MetaSoundPatch") },

		// Data
		{ TEXT("_DT"),    TEXT("/Script/Engine.DataTable") },
		{ TEXT("_DA"),    TEXT("/Script/Engine.DataAsset") },
		{ TEXT("_CURVE"), TEXT("/Script/Engine.CurveBase") },
		{ TEXT("_E"),     TEXT("/Script/Engine.UserDefinedEnum") },
		// UUserDefinedStruct moved Engine -> CoreUObject in 5.5 (Engine/UserDefinedStruct.h
		// is now a deprecated forwarding stub). UUserDefinedEnum did not move. Both paths
		// are listed so the plugin covers 5.4 and earlier too — a suffix is only reported
		// as unavailable when NO path for it resolves.
		{ TEXT("_S"),     TEXT("/Script/CoreUObject.UserDefinedStruct") },
		{ TEXT("_S"),     TEXT("/Script/Engine.UserDefinedStruct") },

		// Blueprints (the plain-UBlueprint variants are disambiguated separately)
		{ TEXT("_BP"),    TEXT("/Script/Engine.Blueprint") },
		{ TEXT("_ABP"),   TEXT("/Script/Engine.AnimBlueprint") },
		{ TEXT("_WBP"),   TEXT("/Script/UMGEditor.WidgetBlueprint") },
		{ TEXT("_CS"),    TEXT("/Script/ControlRigDeveloper.ControlRigBlueprint") },

		// FX / sequencer / input / PCG
		{ TEXT("_NS"),    TEXT("/Script/Niagara.NiagaraSystem") },
		{ TEXT("_NE"),    TEXT("/Script/Niagara.NiagaraEmitter") },
		{ TEXT("_PS"),    TEXT("/Script/Engine.ParticleSystem") },
		{ TEXT("_PCGG"),  TEXT("/Script/PCG.PCGGraph") },
		{ TEXT("_LS"),    TEXT("/Script/LevelSequence.LevelSequence") },
		{ TEXT("_IA"),    TEXT("/Script/EnhancedInput.InputAction") },
		{ TEXT("_IMC"),   TEXT("/Script/EnhancedInput.InputMappingContext") },

		// HVP plugins. Resolved by path like everything else, so a project without the plugin
		// just skips the suffix. The class exists by the time Initialise runs because this plugin
		// loads at PostEngineInit and HVPPrimitiveData at Default.
		{ TEXT("_PDL"),   TEXT("/Script/HVPPrimitiveDataUncooked.PrimitiveDataLegend") },
		{ TEXT("_IDL"),   TEXT("/Script/HVPPrimitiveDataUncooked.InstanceDataLegend") },
	};

	int32 ComputeClassDepth(const UClass* Class)
	{
		int32 Depth = 0;
		for (const UStruct* Cursor = Class; Cursor; Cursor = Cursor->GetSuperStruct())
		{
			++Depth;
		}
		return Depth;
	}
}

void FAssetSuffixResolver::Initialise()
{
	Rules.Empty();

	// A suffix may list more than one class path — alternates for different engine versions
	// (_S moved module in 5.5) or genuinely distinct classes (_MS covers both MetaSound
	// types). So track resolution per SUFFIX, not per row.
	TSet<FString> ResolvedSuffixes;
	TMap<FString, TArray<FString>> UnresolvedBySuffix;

	for (const auto& Entry : ClassRules)
	{
		FRule Rule;
		Rule.Suffix = Entry[0];
		Rule.ClassPath = Entry[1];

		if (UClass* Found = UClass::TryFindTypeSlow<UClass>(Rule.ClassPath))
		{
			Rule.Class = Found;
			Rule.Depth = ComputeClassDepth(Found);
			ResolvedSuffixes.Add(Rule.Suffix);
			Rules.Add(MoveTemp(Rule));
		}
		else
		{
			UnresolvedBySuffix.FindOrAdd(Rule.Suffix).Add(Rule.ClassPath);
		}
	}

	// Only report a suffix as unavailable when nothing resolved it — an alternate path
	// failing is expected, not noteworthy.
	TArray<FString> TrulyMissing;
	for (const auto& Pair : UnresolvedBySuffix)
	{
		if (!ResolvedSuffixes.Contains(Pair.Key))
		{
			TrulyMissing.Add(FString::Printf(TEXT("%s (%s)"),
				*Pair.Key, *FString::Join(Pair.Value, TEXT(" | "))));
		}
	}

	UE_LOG(LogAssetNaming, Log, TEXT("Resolved %d of %d suffixes (%d class rules)."),
		ResolvedSuffixes.Num(), ResolvedSuffixes.Num() + TrulyMissing.Num(), Rules.Num());

	if (TrulyMissing.Num() > 0)
	{
		UE_LOG(LogAssetNaming, Log,
			TEXT("Unavailable (owning plugin disabled or engine version differs), these types will be skipped: %s"),
			*FString::Join(TrulyMissing, TEXT(", ")));
	}
}

bool FAssetSuffixResolver::IsNeverRenamed(const UObject* Asset)
{
	if (!Asset)
	{
		return true;
	}

	// Levels are exempt from the suffix rule (requireSuffixOnMaps is false); redirectors
	// and generated world-partition sidecars must never be touched at all.
	if (Asset->IsA<UWorld>() || Asset->IsA<UObjectRedirector>())
	{
		return true;
	}

	const FString PackageName = Asset->GetOutermost()->GetName();
	if (PackageName.Contains(TEXT("__ExternalActors__")) ||
	    PackageName.Contains(TEXT("__ExternalObjects__")) ||
	    PackageName.EndsWith(TEXT("_BuiltData")))
	{
		return true;
	}

	return false;
}

FString FAssetSuffixResolver::ResolveBlueprintSuffix(const UObject* Asset)
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (!Blueprint)
	{
		return TEXT("_BP");
	}

	if (Blueprint->BlueprintType == BPTYPE_Interface)
	{
		return TEXT("_BI");
	}
	if (Blueprint->BlueprintType == BPTYPE_FunctionLibrary)
	{
		return TEXT("_BFL");
	}
	if (Blueprint->BlueprintType == BPTYPE_MacroLibrary)
	{
		return TEXT("_BML");
	}

	if (const UClass* Parent = Blueprint->ParentClass)
	{
		// Most specific first: an Animation Web and a StaticMeshComponent are both ActorComponents.
		// The web's class is looked up by path, like _PDL's, so a project without HVPCodeAnimWeb
		// just never matches it.
		static const TCHAR* const AnimationWebClassPath = TEXT("/Script/HVPCodeAnimWeb.CodeAnimationWeb");
		if (const UClass* AnimationWeb = FindObject<UClass>(nullptr, AnimationWebClassPath);
			AnimationWeb && Parent->IsChildOf(AnimationWeb))
		{
			return TEXT("_AW");
		}
		if (Parent->IsChildOf(UStaticMeshComponent::StaticClass()))
		{
			return TEXT("_SMC");
		}
		if (Parent->IsChildOf(UActorComponent::StaticClass()))
		{
			return TEXT("_AC");
		}
	}

	return TEXT("_BP");
}

FString FAssetSuffixResolver::ResolveSuffix(const UObject* Asset) const
{
	if (!Asset || IsNeverRenamed(Asset))
	{
		return FString();
	}

	const UClass* AssetClass = Asset->GetClass();

	const FRule* Best = nullptr;
	for (const FRule& Rule : Rules)
	{
		const UClass* RuleClass = Rule.Class.Get();
		if (!RuleClass || !AssetClass->IsChildOf(RuleClass))
		{
			continue;
		}
		if (!Best || Rule.Depth > Best->Depth)
		{
			Best = &Rule;
		}
	}

	if (!Best)
	{
		return FString();
	}

	// Exactly UBlueprint means none of the derived blueprint rules matched, so the
	// distinction has to come from BlueprintType / ParentClass.
	if (Best->Class.Get() == UBlueprint::StaticClass())
	{
		return ResolveBlueprintSuffix(Asset);
	}

	return Best->Suffix;
}
