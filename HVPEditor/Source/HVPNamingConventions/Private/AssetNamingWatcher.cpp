#include "AssetNamingWatcher.h"

#include "AssetNamingLog.h"
#include "AssetNamingSettings.h"
#include "AssetPlacementRules.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "AssetNamingConvention"

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

namespace
{
	FAssetNamingWatcher* GWatcher = nullptr;
}

FAssetNamingWatcher* FAssetNamingWatcher::Get()
{
	return GWatcher;
}

void FAssetNamingWatcher::Startup()
{
	GWatcher = this;

	const UAssetNamingSettings& Settings = UAssetNamingSettings::Get();

	Config = FAssetNamingConfig::Load(Settings.ConventionsConfigPath, Settings.ProjectNameOverride);
	Resolver.Initialise();
	PlacementWarning.Initialise(&Config);

	UE_LOG(LogAssetNaming, Log, TEXT("Content root: %s/  (project name '%s'%s)"),
		*Config.ContentRoot, *Config.ProjectName,
		Settings.ProjectNameOverride.IsEmpty() ? TEXT("") : TEXT(", from plugin override"));

	// A mismatch means the editor and the git hook are enforcing different layouts, which
	// is worth saying out loud rather than leaving to be discovered at push time.
	if (!Settings.ProjectNameOverride.IsEmpty()
		&& !Config.ProjectName.Equals(Config.ProjectNameWithoutOverride, ESearchCase::CaseSensitive))
	{
		UE_LOG(LogAssetNaming, Warning,
			TEXT("Project name override '%s' differs from '%s' (the .uproject name / conventions.json). ")
			TEXT("check_conventions.py will use '%s' on commit, so the two will disagree — set ")
			TEXT("\"projectName\" in conventions.json to match."),
			*Config.ProjectName, *Config.ProjectNameWithoutOverride, *Config.ProjectNameWithoutOverride);
	}

	if (!Config.LoadedFrom.IsEmpty())
	{
		UE_LOG(LogAssetNaming, Log, TEXT("Conventions loaded from '%s' (%d suffixes, %d exempt paths)."),
			*Config.LoadedFrom, Config.Suffixes.Num(), Config.ExemptPackageWildcards.Num());
	}

	if (!Config.IsRuleEnabled(TEXT("asset-suffix")))
	{
		UE_LOG(LogAssetNaming, Log,
			TEXT("severity.asset-suffix is 'off' in conventions.json — auto-suffixing stays idle."));
	}

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	bInitialScanComplete = !AssetRegistry.IsLoadingAssets();
	AssetAddedHandle = AssetRegistry.OnAssetAdded().AddRaw(this, &FAssetNamingWatcher::HandleAssetAdded);
	AssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddRaw(this, &FAssetNamingWatcher::HandleAssetRenamed);
	PathAddedHandle = AssetRegistry.OnPathAdded().AddRaw(this, &FAssetNamingWatcher::HandlePathAdded);
	FilesLoadedHandle = AssetRegistry.OnFilesLoaded().AddRaw(this, &FAssetNamingWatcher::HandleFilesLoaded);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	PostRenameHandle = AssetTools.OnAssetPostRename().AddRaw(this, &FAssetNamingWatcher::HandleAssetPostRename);

	if (GEditor)
	{
		if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			PostImportHandle =
				ImportSubsystem->OnAssetPostImport.AddRaw(this, &FAssetNamingWatcher::HandleAssetPostImport);
		}
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FAssetNamingWatcher::Tick), 0.0f);
}

void FAssetNamingWatcher::Shutdown()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
	{
		AssetRegistry->OnAssetAdded().Remove(AssetAddedHandle);
		AssetRegistry->OnAssetRenamed().Remove(AssetRenamedHandle);
		AssetRegistry->OnPathAdded().Remove(PathAddedHandle);
		AssetRegistry->OnFilesLoaded().Remove(FilesLoadedHandle);
	}

	if (FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		AssetToolsModule->Get().OnAssetPostRename().Remove(PostRenameHandle);
	}

	if (GEditor)
	{
		if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			ImportSubsystem->OnAssetPostImport.Remove(PostImportHandle);
		}
	}

	PlacementWarning.Shutdown();

	Pending.Empty();
	KnownImported.Empty();
	GWatcher = nullptr;
}

TArray<FPlacementViolation> FAssetNamingWatcher::CheckPlacement(const UObject* Asset) const
{
	return AssetPlacementRules::Check(Asset, Config);
}

TArray<FPlacementViolation> FAssetNamingWatcher::CheckPlacementByPath(const FString& PackageName,
	const FString& AssetName) const
{
	if (Config.IsExemptPackage(PackageName))
	{
		return {};
	}
	return AssetPlacementRules::CheckPath(PackageName, AssetName, Config);
}

TArray<FPlacementViolation> FAssetNamingWatcher::CheckFolderPlacement(const FString& PackagePath) const
{
	return AssetPlacementRules::CheckFolder(PackagePath, Config);
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void FAssetNamingWatcher::HandlePathAdded(const FString& Path)
{
	// An empty folder holds no asset, so the asset-level rules never see it. Without this
	// hook, creating a folder called "Textures" goes unflagged until something is put in it.
	const UAssetNamingSettings& Settings = UAssetNamingSettings::Get();
	if (!Settings.bEnabled
		|| Settings.PlacementWarning == EPlacementWarningStyle::Off
		|| bRenameInProgress
		|| !GEditor || IsRunningCommandlet()
		|| !bInitialScanComplete)          // the startup scan announces every folder in the project
	{
		return;
	}

	if (!Path.StartsWith(TEXT("/Game/")))
	{
		return;
	}

	PlacementWarning.ReportFolder(Path, AssetPlacementRules::CheckFolder(Path, Config));
}

void FAssetNamingWatcher::HandleFilesLoaded()
{
	bInitialScanComplete = true;
	// Anything queued during the startup scan was pre-existing content, not new work.
	Pending.Empty();
}

void FAssetNamingWatcher::HandleAssetPostImport(UFactory* /*Factory*/, UObject* CreatedObject)
{
	if (CreatedObject)
	{
		KnownImported.Add(FSoftObjectPath(CreatedObject));
	}
}

void FAssetNamingWatcher::HandleAssetAdded(const FAssetData& AssetData)
{
	Enqueue(AssetData);
}

void FAssetNamingWatcher::HandleAssetRenamed(const FAssetData& NewData, const FString& /*OldPath*/)
{
	// Renames and Content Browser moves both land here. The asset is usually already saved,
	// so this is the one path that must waive the "not yet on disk" test — otherwise
	// renaming Foo_M to Bar, or dragging an asset into the wrong folder, goes unchecked.
	if (!bRenameInProgress)
	{
		Enqueue(NewData, /*bAllowExisting*/ true);
	}
}

void FAssetNamingWatcher::HandleAssetPostRename(const TArray<FAssetRenameData>& RenameData)
{
	// This is what makes Content Browser creation work. Creating an asset from the Add
	// menu drops it in with a placeholder name ("NewBlueprint") and immediately opens
	// inline rename. Suffixing the placeholder underneath the open text box would just be
	// overwritten by whatever the user types. So we let them commit their name, then
	// append the suffix here. Our own rename re-enters this handler, but by then the name
	// already carries the suffix and BuildConventionalName returns empty — no loop.
	if (bRenameInProgress)
	{
		return;
	}

	for (const FAssetRenameData& Entry : RenameData)
	{
		if (const UObject* Asset = Entry.Asset.Get())
		{
			FAssetData Data(Asset);
			Enqueue(Data, /*bAllowExisting*/ true);
		}
	}
}

// ---------------------------------------------------------------------------
// Queue
// ---------------------------------------------------------------------------

void FAssetNamingWatcher::Enqueue(const FAssetData& AssetData, bool bAllowExisting)
{
	const UAssetNamingSettings& Settings = UAssetNamingSettings::Get();

	// Note: asset-suffix being switched off must NOT short-circuit here — placement
	// warnings are a separate rule and still need the asset queued.
	if (!Settings.bEnabled || bRenameInProgress)
	{
		return;
	}
	if (!GEditor || IsRunningCommandlet() || !bInitialScanComplete)
	{
		return;
	}
	if (AssetData.IsRedirector())
	{
		return;
	}

	const FString PackageName = AssetData.PackageName.ToString();

	// Only project content. Engine and plugin content belong to someone else, and
	// conventions.json exempts Plugins/*/Content/** explicitly.
	if (!PackageName.StartsWith(TEXT("/Game/")))
	{
		return;
	}
	if (Config.IsExemptPackage(PackageName))
	{
		return;
	}

	// Already on disk means this is existing content being discovered (a branch switch, a
	// directory-watcher pickup), not something just made. Freshly created and freshly
	// imported assets live only in memory until the user saves. Renames and moves waive
	// this — see the bAllowExisting comment on the declaration.
	if (!bAllowExisting && FPackageName::DoesPackageExist(PackageName))
	{
		return;
	}

	Pending.Add(AssetData.GetSoftObjectPath());
	LastEnqueueTime = FPlatformTime::Seconds();
}

bool FAssetNamingWatcher::Tick(float /*DeltaTime*/)
{
	if (Pending.IsEmpty())
	{
		return true;
	}

	const UAssetNamingSettings& Settings = UAssetNamingSettings::Get();
	if (FPlatformTime::Seconds() - LastEnqueueTime < Settings.DebounceSeconds)
	{
		return true;
	}

	// Never rename mid-load or mid-save; re-defer instead and pick it up next frame.
	if (IsAsyncLoading() || IsGarbageCollecting() || UE::IsSavingPackage())
	{
		return true;
	}

	ProcessPending();
	return true;
}

void FAssetNamingWatcher::ProcessPending()
{
	TArray<UObject*> Assets;
	Assets.Reserve(Pending.Num());

	for (const FSoftObjectPath& Path : Pending)
	{
		// ResolveObject only — a brand-new asset is in memory, and TryLoad on one that has
		// since been discarded would drag packages off disk for no reason.
		if (UObject* Asset = Path.ResolveObject())
		{
			Assets.Add(Asset);
		}
	}
	Pending.Empty();

	if (Assets.IsEmpty())
	{
		return;
	}

	TArray<FAssetRenameData> Renames;
	GatherRenames(Assets, Renames, /*bIgnoreScopeToggles*/ false);

	for (UObject* Asset : Assets)
	{
		KnownImported.Remove(FSoftObjectPath(Asset));
	}

	// GatherRenames already logged each one under dry run.
	if (Renames.Num() > 0 && !UAssetNamingSettings::Get().bDryRun)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		TGuardValue<bool> Guard(bRenameInProgress, true);
		if (!AssetTools.RenameAssets(Renames))
		{
			UE_LOG(LogAssetNaming, Warning, TEXT("Rename of %d asset(s) did not complete."), Renames.Num());
		}
	}

	// Placement runs after renaming, and unconditionally — the audio-placement rule keys
	// off the FINAL name (VO_ prefix, _SW/_SC suffix), and an asset that needed no rename
	// can still be sitting in the wrong folder.
	EvaluatePlacement(Assets);
}

void FAssetNamingWatcher::EvaluatePlacement(const TArray<UObject*>& Assets)
{
	if (UAssetNamingSettings::Get().PlacementWarning == EPlacementWarningStyle::Off)
	{
		return;
	}

	TArray<FPlacementViolation> Violations;
	for (UObject* Asset : Assets)
	{
		if (IsValid(Asset))
		{
			Violations.Append(AssetPlacementRules::Check(Asset, Config));
		}
	}

	PlacementWarning.Report(Violations);
}

// ---------------------------------------------------------------------------
// Naming
// ---------------------------------------------------------------------------

bool FAssetNamingWatcher::WasImported(const UObject* Asset)
{
	if (!Asset)
	{
		return false;
	}

	// Every importable type carries an AssetImportData object property — including
	// Interchange-created assets, which populate it with UInterchangeAssetImportData.
	// Finding it reflectively avoids depending on each asset type's header.
	for (TFieldIterator<FObjectProperty> It(Asset->GetClass()); It; ++It)
	{
		if (It->GetFName() == FName(TEXT("AssetImportData")))
		{
			return It->GetObjectPropertyValue_InContainer(Asset) != nullptr;
		}
	}
	return false;
}

FString FAssetNamingWatcher::BuildConventionalName(const FString& CurrentName, const FString& Suffix) const
{
	const UAssetNamingSettings& Settings = UAssetNamingSettings::Get();
	FString Name = CurrentName;

	// Ctrl+W turns Foo_T into Foo_T1, which puts the suffix mid-name. Move the number in
	// front of the suffix so the type stays the final segment.
	if (Settings.bRepairDuplicateSuffix)
	{
		int32 DigitStart = Name.Len();
		while (DigitStart > 0 && FChar::IsDigit(Name[DigitStart - 1]))
		{
			--DigitStart;
		}

		if (DigitStart < Name.Len() && DigitStart > 0)
		{
			const FString Stem = Name.Left(DigitStart);
			const FString Digits = Name.RightChop(DigitStart);

			FString StemSuffix = Config.FindTrailingSuffix(Stem, /*bCaseSensitive*/ true);
			bool bStemSuffixWasExact = !StemSuffix.IsEmpty();
			if (!bStemSuffixWasExact && Settings.bNormaliseSuffixCase)
			{
				StemSuffix = Config.FindTrailingSuffix(Stem, /*bCaseSensitive*/ false);
			}

			if (!StemSuffix.IsEmpty())
			{
				// FindTrailingSuffix returns the canonical spelling. Keep the author's
				// original casing unless we're normalising it anyway.
				const FString Tail = (bStemSuffixWasExact || Settings.bNormaliseSuffixCase)
					? StemSuffix
					: Stem.RightChop(Stem.Len() - StemSuffix.Len());
				Name = Stem.LeftChop(StemSuffix.Len()) + Digits + Tail;
			}
		}
	}

	// Does the name already carry a suffix, and in what spelling?
	//
	// A name ending in a mid-name tag (Foo_REF) counts as unsuffixed, so the type suffix
	// goes after the tag: Foo_REF -> Foo_REF_T.
	FString ExistingSuffix;   // canonical spelling of whatever is already on the name
	bool bHasSuffix = false;

	if (!Config.EndsWithMidNameTag(Name))
	{
		ExistingSuffix = Config.FindTrailingSuffix(Name, /*bCaseSensitive*/ true);
		bHasSuffix = !ExistingSuffix.IsEmpty();

		if (!bHasSuffix && Settings.bNormaliseSuffixCase)
		{
			// The checker is case-sensitive, so Foo_bp genuinely fails it. But the author
			// clearly meant a suffix, so fix the case instead of appending a second one
			// and producing Foo_bp_BP.
			const FString LooseMatch = Config.FindTrailingSuffix(Name, /*bCaseSensitive*/ false);
			if (!LooseMatch.IsEmpty())
			{
				Name = Name.LeftChop(LooseMatch.Len()) + LooseMatch;
				ExistingSuffix = LooseMatch;
				bHasSuffix = true;
			}
		}
	}

	if (Settings.bFixPascalCase)
	{
		// The checker's asset-case rule is per underscore-separated segment, so this only
		// capitalises each segment's first letter. Underscores are preserved, which keeps
		// deliberate names like Hand_L_CD38 intact.
		const FString CaseTarget = bHasSuffix ? Name.LeftChop(ExistingSuffix.Len()) : Name;

		TArray<FString> Segments;
		CaseTarget.ParseIntoArray(Segments, TEXT("_"), /*InCullEmpty*/ false);
		for (FString& Segment : Segments)
		{
			if (!Segment.IsEmpty() && FChar::IsLower(Segment[0]))
			{
				Segment[0] = FChar::ToUpper(Segment[0]);
			}
		}
		Name = FString::Join(Segments, TEXT("_")) + (bHasSuffix ? ExistingSuffix : FString());
	}

	// Idempotency, and the deliberate refusal to second-guess the author: a name that
	// already carries a recognised suffix keeps that suffix even when it's the "wrong" one
	// for the type. Silently rewriting Foo_SM to Foo_T on a texture would be a worse
	// failure than leaving it for the Claude pass on push to raise. Note this applies to
	// the case-normalised form too: a material named Foo_sm becomes Foo_SM, not Foo_M.
	if (!bHasSuffix)
	{
		Name += Suffix;
	}

	// Must be case-SENSITIVE. FString::operator== compares case-insensitively, so a
	// case-only fix (Romeo_m -> Romeo_M) would compare equal and be thrown away as a
	// no-op — which is exactly the change bNormaliseSuffixCase exists to make.
	return Name.Equals(CurrentName, ESearchCase::CaseSensitive) ? FString() : Name;
}

void FAssetNamingWatcher::GatherRenames(const TArray<UObject*>& Assets,
	TArray<FAssetRenameData>& OutRenames, bool bIgnoreScopeToggles) const
{
	if (!Config.IsRuleEnabled(TEXT("asset-suffix")))
	{
		return;   // the project turned naming off; placement is handled separately
	}

	const UAssetNamingSettings& Settings = UAssetNamingSettings::Get();
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Reserved so a batch that would collide with itself (two assets both wanting Foo_T)
	// still ends up with distinct names.
	TSet<FString> ClaimedPackages;

	for (UObject* Asset : Assets)
	{
		if (!IsValid(Asset) || FAssetSuffixResolver::IsNeverRenamed(Asset))
		{
			continue;
		}

		if (!bIgnoreScopeToggles)
		{
			const bool bImported =
				KnownImported.Contains(FSoftObjectPath(Asset)) || WasImported(Asset);
			if (bImported && !Settings.bApplyToImportedAssets)
			{
				continue;
			}
			if (!bImported && !Settings.bApplyToCreatedAssets)
			{
				continue;
			}
		}

		const FString Suffix = Resolver.ResolveSuffix(Asset);
		if (Suffix.IsEmpty())
		{
			continue; // Type not covered by the convention — leave it alone.
		}

		const FString CurrentName = Asset->GetName();
		const FString NewName = BuildConventionalName(CurrentName, Suffix);
		if (NewName.IsEmpty())
		{
			continue;
		}

		const FString PackagePath = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());

		// Resolve collisions against both disk and the rest of this batch.
		FString FinalName = NewName;
		FString CandidatePackage = PackagePath / FinalName;
		if (ClaimedPackages.Contains(CandidatePackage) ||
		    FPackageName::DoesPackageExist(CandidatePackage))
		{
			FString UniquePackage;
			FString UniqueName;
			AssetTools.CreateUniqueAssetName(PackagePath / NewName, FString(), UniquePackage, UniqueName);
			FinalName = UniqueName;
			CandidatePackage = UniquePackage;
		}
		ClaimedPackages.Add(CandidatePackage);

		UE_LOG(LogAssetNaming, Log, TEXT("%s%s  ->  %s   [%s]"),
			Settings.bDryRun ? TEXT("(dry run) ") : TEXT(""),
			*CurrentName, *FinalName, *Asset->GetClass()->GetName());

		if (!Settings.bDryRun)
		{
			OutRenames.Emplace(Asset, PackagePath, FinalName);
		}
	}
}

// ---------------------------------------------------------------------------
// Manual commands
// ---------------------------------------------------------------------------

void FAssetNamingWatcher::FlushNow()
{
	ProcessPending();
}

void FAssetNamingWatcher::AuditProject() const
{
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FAssetData> AllAssets;
	AssetRegistry.GetAssetsByPath(FName(TEXT("/Game")), AllAssets, /*bRecursive*/ true);

	int32 Flagged = 0;
	int32 Skipped = 0;

	for (const FAssetData& Data : AllAssets)
	{
		const FString PackageName = Data.PackageName.ToString();
		if (Data.IsRedirector() || Config.IsExemptPackage(PackageName))
		{
			++Skipped;
			continue;
		}

		const FString Stem = Data.AssetName.ToString();
		if (!Config.EndsWithMidNameTag(Stem) && !Config.FindTrailingSuffix(Stem).IsEmpty())
		{
			continue;
		}

		// Only load the asset when the name already looks wrong, so an audit of a large
		// project doesn't pull every package into memory.
		UObject* Asset = Data.GetAsset();
		if (!Asset || FAssetSuffixResolver::IsNeverRenamed(Asset))
		{
			continue;
		}

		const FString Suffix = Resolver.ResolveSuffix(Asset);
		if (Suffix.IsEmpty())
		{
			UE_LOG(LogAssetNaming, Warning, TEXT("%s  ->  no rule for type %s"),
				*PackageName, *Asset->GetClass()->GetName());
		}
		else
		{
			// Report exactly what the watcher would do, so an audit never disagrees with
			// HVPNaming.FixSelected.
			const FString Proposed = BuildConventionalName(Stem, Suffix);
			UE_LOG(LogAssetNaming, Warning, TEXT("%s  ->  %s   [%s]"),
				*PackageName, Proposed.IsEmpty() ? *Stem : *Proposed,
				*Asset->GetClass()->GetName());
		}
		++Flagged;
	}

	UE_LOG(LogAssetNaming, Warning,
		TEXT("Audit complete: %d asset(s) without a recognised suffix, %d exempt, %d scanned."),
		Flagged, Skipped, AllAssets.Num());
}

void FAssetNamingWatcher::FixSelection()
{
	FContentBrowserModule& ContentBrowser =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TArray<FAssetData> Selected;
	ContentBrowser.Get().GetSelectedAssets(Selected);

	if (Selected.IsEmpty())
	{
		UE_LOG(LogAssetNaming, Warning, TEXT("Nothing selected in the Content Browser."));
		return;
	}

	TArray<UObject*> Assets;
	for (const FAssetData& Data : Selected)
	{
		if (Config.IsExemptPackage(Data.PackageName.ToString()))
		{
			UE_LOG(LogAssetNaming, Log, TEXT("Skipping exempt path: %s"), *Data.PackageName.ToString());
			continue;
		}
		if (UObject* Asset = Data.GetAsset())
		{
			Assets.Add(Asset);
		}
	}

	TArray<FAssetRenameData> Renames;
	// Manual invocation is explicit intent, so the imported/created toggles don't apply.
	GatherRenames(Assets, Renames, /*bIgnoreScopeToggles*/ true);

	if (Renames.IsEmpty())
	{
		UE_LOG(LogAssetNaming, Log, TEXT("Selection already follows the convention."));
		return;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TGuardValue<bool> Guard(bRenameInProgress, true);
	AssetTools.RenameAssets(Renames);
}

#undef LOCTEXT_NAMESPACE
