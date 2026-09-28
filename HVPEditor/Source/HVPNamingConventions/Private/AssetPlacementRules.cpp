#include "AssetPlacementRules.h"

#include "AssetNamingConfig.h"
#include "AssetSuffixResolver.h"
#include "Misc/Paths.h"
#include "UObject/Object.h"
#include "UObject/Package.h"

namespace
{
	/** check_conventions.py's under(): equal to, or nested inside, the prefix. */
	bool Under(const FString& Path, const FString& Prefix)
	{
		if (Prefix.IsEmpty())
		{
			return false;
		}
		return Path.Equals(Prefix, ESearchCase::IgnoreCase)
			|| Path.StartsWith(Prefix + TEXT("/"), ESearchCase::IgnoreCase);
	}

	FString FirstSegment(const FString& Path)
	{
		int32 Slash = INDEX_NONE;
		return Path.FindChar(TEXT('/'), Slash) ? Path.Left(Slash) : Path;
	}

	/**
	 * Discouraged folder names among these path segments. Whole-segment and
	 * case-insensitive, so Library/MasterMaterials/ is unaffected by a "Materials" entry.
	 *
	 * Shared by the asset-level and folder-level checks so the two can't word the same
	 * finding differently.
	 */
	TArray<FString> FindDiscouragedFolders(const TArray<FString>& Segments,
		const FAssetNamingConfig& Config)
	{
		TArray<FString> Offenders;
		for (const FString& Segment : Segments)
		{
			const bool bDiscouraged = Config.DiscouragedFolderNames.ContainsByPredicate(
				[&Segment](const FString& Name) { return Name.Equals(Segment, ESearchCase::IgnoreCase); });

			if (bDiscouraged)
			{
				Offenders.AddUnique(Segment);
			}
		}
		return Offenders;
	}

	FString DiscouragedFolderMessage(const TArray<FString>& Offenders)
	{
		const FString Names = TEXT("'") + FString::Join(Offenders, TEXT("', '")) + TEXT("'");
		return Offenders.Num() == 1
			? FString::Printf(TEXT("%s is not a valid folder name. Please rename it to a valid name."), *Names)
			: FString::Printf(TEXT("%s are not valid folder names. Please rename them to valid names."), *Names);
	}

	const TCHAR* DiscouragedFolderHint()
	{
		return TEXT("Assets should be grouped into folders based on what it is. ")
			   TEXT("Example, a material used for a cell A, should be in a folder Cell/A/.");
	}
}

TArray<FPlacementViolation> AssetPlacementRules::CheckPath(const FString& PackageName,
	const FString& AssetName, const FAssetNamingConfig& Config)
{
	TArray<FPlacementViolation> Violations;

	const FString RepoPath = Config.PackageToRepoPath(PackageName);
	if (RepoPath.IsEmpty())
	{
		return Violations;   // not /Game content
	}

	auto Add = [&](const TCHAR* Rule, FString Message, FString Hint)
	{
		if (!Config.IsRuleEnabled(Rule))
		{
			return;
		}
		FPlacementViolation V;
		V.Rule = Rule;
		V.PackageName = PackageName;
		V.AssetName = AssetName;
		V.Message = MoveTemp(Message);
		V.Hint = MoveTemp(Hint);
		Violations.Add(MoveTemp(V));
	};

	// 1. All game content belongs under Content/<ProjectName>/.
	if (Config.bRequireProjectContentFolder
		&& Under(RepoPath, Config.ContentDir)
		&& !Under(RepoPath, Config.ContentRoot))
	{
		const FString Rel = RepoPath.RightChop(Config.ContentDir.Len() + 1);
		const FString Stray = FirstSegment(Rel);
		Add(TEXT("content-project-folder"),
			FString::Printf(TEXT("'%s/%s' is outside the project content folder"), *Config.ContentDir, *Stray),
			FString::Printf(TEXT("game content belongs under %s/"), *Config.ContentRoot));
		return Violations;   // mirrors the Python's early return
	}

	// 2. Anything under the content root must sit in an allowed top-level folder.
	if (Under(RepoPath, Config.ContentRoot) && Config.ContentTopLevelFolders.Num() > 0)
	{
		const FString Rel = RepoPath.RightChop(Config.ContentRoot.Len() + 1);
		int32 Slash = INDEX_NONE;
		if (!Rel.FindChar(TEXT('/'), Slash))
		{
			Add(TEXT("content-toplevel"),
				FString::Printf(TEXT("loose file in %s/"), *Config.ContentRoot),
				TEXT("allowed folders: ") + FString::Join(Config.ContentTopLevelFolders, TEXT(", ")));
			return Violations;
		}

		const FString Top = Rel.Left(Slash);
		const bool bAllowed = Config.ContentTopLevelFolders.ContainsByPredicate(
			[&Top](const FString& Folder) { return Folder.Equals(Top, ESearchCase::IgnoreCase); });

		if (!bAllowed)
		{
			Add(TEXT("content-toplevel"),
				FString::Printf(TEXT("'%s/' is not an allowed top-level folder under %s/"), *Top, *Config.ContentRoot),
				TEXT("allowed folders: ") + FString::Join(Config.ContentTopLevelFolders, TEXT(", ")));
		}
	}

	// 3. Library/ must organise its content into subfolders.
	const FString LibraryRoot = Config.ContentRoot / Config.LibraryDir;
	if (Config.bRequireLibrarySubfolders && Under(RepoPath, LibraryRoot))
	{
		const FString Rel = RepoPath.RightChop(LibraryRoot.Len() + 1);
		int32 Slash = INDEX_NONE;
		if (!Rel.FindChar(TEXT('/'), Slash))
		{
			Add(TEXT("library-structure"),
				FString::Printf(TEXT("sits loose in %s/"), *LibraryRoot),
				FString::Printf(TEXT("move it into a subfolder, e.g. %s/<Group>/"), *LibraryRoot));
		}
	}

	// 4. Folders that group by asset type rather than by what the asset is for.
	if (Config.DiscouragedFolderNames.Num() > 0 && Under(RepoPath, Config.ContentRoot))
	{
		const FString Rel = RepoPath.RightChop(Config.ContentRoot.Len() + 1);

		TArray<FString> Segments;
		Rel.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty*/ true);
		if (Segments.Num() > 0)
		{
			Segments.Pop();   // drop the asset filename; only folders matter
		}

		const TArray<FString> Offenders = FindDiscouragedFolders(Segments, Config);
		if (Offenders.Num() > 0)
		{
			Add(TEXT("type-folder"), DiscouragedFolderMessage(Offenders), DiscouragedFolderHint());
		}
	}

	// 5. Module layout. Only the Modules/ folder itself has to be empty of assets — once
	// something is inside a module folder, how deep it sits there is the module's business.
	const FString ModuleRoot = Config.ContentRoot / Config.ModulesDir;
	if (Under(RepoPath, ModuleRoot))
	{
		const FString Rel = RepoPath.RightChop(ModuleRoot.Len() + 1);
		TArray<FString> Parts;
		Rel.ParseIntoArray(Parts, TEXT("/"), /*InCullEmpty*/ true);

		if (Parts.Num() == 1 && !Config.bAllowLooseFilesInModulesDir)
		{
			Add(TEXT("module-structure"),
				FString::Printf(TEXT("sits directly in %s/ instead of inside a module folder"), *ModuleRoot),
				FString::Printf(TEXT("move it into %s/<ModuleName>/"), *ModuleRoot));
		}

		// 6. Audio placement, inside a module.
		if (Config.bAudioCheckEnabled && Parts.Num() >= 2)
		{
			TArray<FString> InnerParts(Parts);
			const FString ModuleName = InnerParts[0];
			InnerParts.RemoveAt(0);
			const FString Inner = FString::Join(InnerParts, TEXT("/"));

			const bool bIsVO = Config.VoPrefixes.ContainsByPredicate(
				[&AssetName](const FString& Prefix)
				{ return AssetName.StartsWith(Prefix, ESearchCase::CaseSensitive); });

			const bool bIsAudio = Config.AudioSuffixes.ContainsByPredicate(
				[&AssetName](const FString& Suffix)
				{ return AssetName.EndsWith(Suffix, ESearchCase::CaseSensitive); });

			if (bIsVO && !Under(Inner, Config.VoDir))
			{
				Add(TEXT("audio-placement"),
					TEXT("voice-over asset outside the VO folder"),
					FString::Printf(TEXT("expected %s/%s/%s/"), *ModuleRoot, *ModuleName, *Config.VoDir));
			}
			else if (bIsAudio && !Under(Inner, Config.AudioDir))
			{
				Add(TEXT("audio-placement"),
					TEXT("sound asset outside the module Audio folder"),
					FString::Printf(TEXT("expected %s/%s/%s/"), *ModuleRoot, *ModuleName, *Config.AudioDir));
			}
		}
	}

	return Violations;
}

TArray<FPlacementViolation> AssetPlacementRules::CheckFolder(const FString& PackagePath,
	const FAssetNamingConfig& Config)
{
	TArray<FPlacementViolation> Violations;

	if (Config.IsExemptPackage(PackagePath))
	{
		return Violations;
	}

	// PackageToRepoPath appends ".uasset"; a folder has no extension.
	FString RepoPath = Config.PackageToRepoPath(PackagePath);
	if (RepoPath.IsEmpty())
	{
		return Violations;
	}
	RepoPath.RemoveFromEnd(TEXT(".uasset"));

	const FString FolderName = FPaths::GetCleanFilename(RepoPath);

	auto Add = [&](const TCHAR* Rule, FString Message, FString Hint)
	{
		if (!Config.IsRuleEnabled(Rule))
		{
			return;
		}
		FPlacementViolation V;
		V.Rule = Rule;
		V.PackageName = PackagePath;
		V.AssetName = FolderName + TEXT("/");
		V.Message = MoveTemp(Message);
		V.Hint = MoveTemp(Hint);
		Violations.Add(MoveTemp(V));
	};

	// A folder created under Content/ but outside Content/<Project>/.
	if (Config.bRequireProjectContentFolder
		&& Under(RepoPath, Config.ContentDir)
		&& !Under(RepoPath, Config.ContentRoot))
	{
		const FString Rel = RepoPath.RightChop(Config.ContentDir.Len() + 1);
		Add(TEXT("content-project-folder"),
			FString::Printf(TEXT("'%s/%s' is outside the project content folder"),
				*Config.ContentDir, *FirstSegment(Rel)),
			FString::Printf(TEXT("game content belongs under %s/"), *Config.ContentRoot));
		return Violations;
	}

	if (!Under(RepoPath, Config.ContentRoot) || RepoPath.Len() <= Config.ContentRoot.Len())
	{
		return Violations;   // the content root itself, or outside it entirely
	}

	const FString Rel = RepoPath.RightChop(Config.ContentRoot.Len() + 1);

	TArray<FString> Segments;
	Rel.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty*/ true);
	if (Segments.Num() == 0)
	{
		return Violations;
	}

	// A new top-level folder under the content root must be one of the allowed ones.
	if (Config.ContentTopLevelFolders.Num() > 0)
	{
		const bool bAllowed = Config.ContentTopLevelFolders.ContainsByPredicate(
			[&Segments](const FString& Folder) { return Folder.Equals(Segments[0], ESearchCase::IgnoreCase); });

		if (!bAllowed)
		{
			Add(TEXT("content-toplevel"),
				FString::Printf(TEXT("'%s/' is not an allowed top-level folder under %s/"),
					*Segments[0], *Config.ContentRoot),
				TEXT("allowed folders: ") + FString::Join(Config.ContentTopLevelFolders, TEXT(", ")));
		}
	}

	// Folders that group by asset type rather than by what the asset is for.
	const TArray<FString> Offenders = FindDiscouragedFolders(Segments, Config);
	if (Offenders.Num() > 0)
	{
		Add(TEXT("type-folder"), DiscouragedFolderMessage(Offenders), DiscouragedFolderHint());
	}

	return Violations;
}

TArray<FPlacementViolation> AssetPlacementRules::Check(const UObject* Asset,
	const FAssetNamingConfig& Config)
{
	if (!Asset || FAssetSuffixResolver::IsNeverRenamed(Asset))
	{
		return {};
	}

	const FString PackageName = Asset->GetOutermost()->GetName();
	if (Config.IsExemptPackage(PackageName))
	{
		return {};
	}

	TArray<FPlacementViolation> Violations = CheckPath(PackageName, Asset->GetName(), Config);
	for (FPlacementViolation& Violation : Violations)
	{
		Violation.Asset = const_cast<UObject*>(Asset);
	}
	return Violations;
}
