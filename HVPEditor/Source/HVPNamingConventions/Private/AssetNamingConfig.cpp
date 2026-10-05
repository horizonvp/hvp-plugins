#include "AssetNamingConfig.h"

#include "AssetNamingLog.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/**
	 * The frozen baseline, mirroring the "suffixes" block of check_conventions.py.
	 * Anything added from here on should go in conventions.json's "suffixes" overlay
	 * instead, so both tools stay in lockstep.
	 */
	const TCHAR* const BuiltInSuffixes[][2] = {
		{ TEXT("_BP"),    TEXT("Blueprint class") },
		{ TEXT("_BML"),   TEXT("Blueprint Macro Library") },
		{ TEXT("_PDL"),   TEXT("Primitive Data Legend") },
		{ TEXT("_ABP"),   TEXT("Animation Blueprint") },
		{ TEXT("_WBP"),   TEXT("Widget Blueprint") },
		{ TEXT("_A"),     TEXT("Animation Sequence") },
		{ TEXT("_AC"),    TEXT("ActorComponent") },
		{ TEXT("_BFL"),   TEXT("Blueprint Function Library") },
		{ TEXT("_BI"),    TEXT("Blueprint Interface") },
		{ TEXT("_E"),     TEXT("Enum") },
		{ TEXT("_S"),     TEXT("Struct") },
		{ TEXT("_DT"),    TEXT("DataTable") },
		{ TEXT("_DA"),    TEXT("Data Asset") },
		{ TEXT("_M"),     TEXT("Material") },
		{ TEXT("_MI"),    TEXT("Material Instance") },
		{ TEXT("_MF"),    TEXT("Material Function") },
		{ TEXT("_MPC"),   TEXT("Material Parameter Collection") },
		{ TEXT("_SM"),    TEXT("Static Mesh") },
		{ TEXT("_SMF"),   TEXT("Static Mesh Foliage (FoliageType)") },
		{ TEXT("_SMC"),   TEXT("Static Mesh Component") },
		{ TEXT("_SKM"),   TEXT("Skeletal Mesh") },
		{ TEXT("_SKEL"),  TEXT("Skeleton") },
		{ TEXT("_PA"),    TEXT("Physics Asset") },
		{ TEXT("_SW"),    TEXT("Sound Wave") },
		{ TEXT("_SC"),    TEXT("Sound Cue") },
		{ TEXT("_SCL"),   TEXT("Sound Class") },
		{ TEXT("_SMX"),   TEXT("Sound Mix") },
		{ TEXT("_CNC"),   TEXT("Sound Concurrency") },
		{ TEXT("_ATT"),   TEXT("Sound Attenuation") },
		{ TEXT("_MS"),    TEXT("MetaSound") },
		{ TEXT("_T"),     TEXT("Texture") },
		{ TEXT("_RT"),    TEXT("Render Target") },
		{ TEXT("_NS"),    TEXT("Niagara System") },
		{ TEXT("_NE"),    TEXT("Niagara Emitter") },
		{ TEXT("_PS"),    TEXT("Particle System") },
		{ TEXT("_PCGG"),  TEXT("PCG Generator") },
		{ TEXT("_LS"),    TEXT("Level Sequence") },
		{ TEXT("_CS"),    TEXT("Control Rig") },
		{ TEXT("_IA"),    TEXT("Input Action") },
		{ TEXT("_IMC"),   TEXT("Input Mapping Context") },
		{ TEXT("_CURVE"), TEXT("Curve") },
	};

	const TCHAR* const BuiltInMidNameTags[] = {
		TEXT("_REF"), TEXT("_WIP"), TEXT("_TEMP"), TEXT("_OLD"),
	};
}

FString StripJsonComments(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len());

	bool bInString = false;
	bool bEscaped = false;

	for (int32 i = 0; i < In.Len(); ++i)
	{
		const TCHAR C = In[i];
		const TCHAR Next = (i + 1 < In.Len()) ? In[i + 1] : TEXT('\0');

		if (bInString)
		{
			Out.AppendChar(C);
			if (bEscaped)          { bEscaped = false; }
			else if (C == TEXT('\\')) { bEscaped = true; }
			else if (C == TEXT('"'))  { bInString = false; }
			continue;
		}

		if (C == TEXT('"'))
		{
			bInString = true;
			Out.AppendChar(C);
			continue;
		}

		// Line comment: drop to end of line, but keep the newline so line numbers survive.
		if (C == TEXT('/') && Next == TEXT('/'))
		{
			while (i < In.Len() && In[i] != TEXT('\n')) { ++i; }
			if (i < In.Len()) { Out.AppendChar(TEXT('\n')); }
			continue;
		}

		// Block comment.
		if (C == TEXT('/') && Next == TEXT('*'))
		{
			i += 2;
			while (i + 1 < In.Len() && !(In[i] == TEXT('*') && In[i + 1] == TEXT('/'))) { ++i; }
			++i; // land on '/', loop's ++i steps past it
			continue;
		}

		Out.AppendChar(C);
	}

	// Commenting out the last entry of a block is the normal way to disable a rule in
	// conventions.json, which can leave a dangling comma behind. Strip those too.
	FString Cleaned;
	Cleaned.Reserve(Out.Len());
	for (int32 i = 0; i < Out.Len(); ++i)
	{
		if (Out[i] == TEXT(','))
		{
			int32 j = i + 1;
			while (j < Out.Len() && FChar::IsWhitespace(Out[j])) { ++j; }
			if (j < Out.Len() && (Out[j] == TEXT('}') || Out[j] == TEXT(']')))
			{
				continue; // drop the comma
			}
		}
		Cleaned.AppendChar(Out[i]);
	}

	return Cleaned;
}

void FAssetNamingConfig::ApplyBuiltInDefaults()
{
	Suffixes.Empty();
	for (const auto& Pair : BuiltInSuffixes)
	{
		Suffixes.Add(Pair[0], Pair[1]);
	}

	MidNameTags.Empty();
	for (const TCHAR* Tag : BuiltInMidNameTags)
	{
		MidNameTags.Add(Tag);
	}

	// Defaults matching conventions.json's exemptPaths. Only the Content/ entries can ever
	// contain assets; Houdini/, Config/ and friends hold no packages, so they need no /Game
	// equivalent.
	ExemptPackageWildcards.Empty();
	ExemptPackageWildcards.Add(TEXT("/Game/ThirdParty/*"));
	ExemptPackageWildcards.Add(TEXT("/Game/Developers/*"));
	// Content/Movies is where the engine requires media to live — it can't move under the
	// project content folder, so the placement rules must not ask it to.
	ExemptPackageWildcards.Add(TEXT("/Game/Movies/*"));
	ExemptPackageWildcards.Add(TEXT("/Game/HoudiniEngine/*"));
	ExemptPackageWildcards.Add(TEXT("/Game/UsdAssets/*"));
	ExemptPackageWildcards.Add(TEXT("/Game/Collections/*"));

	ProjectName = FApp::GetProjectName();
	ContentDir = TEXT("Content");
	ContentRoot = ContentDir / ProjectName;

	ContentTopLevelFolders = { TEXT("App"), TEXT("Library"), TEXT("Systems"), TEXT("Modules") };
	DiscouragedFolderNames = { TEXT("Textures"), TEXT("Materials") };
	VoPrefixes = { TEXT("VO_") };
	AudioSuffixes = { TEXT("_SW"), TEXT("_SC") };
}

bool FAssetNamingConfig::IsRuleEnabled(const FString& Rule) const
{
	const FString* Severity = Severities.Find(Rule);
	return !Severity || !Severity->Equals(TEXT("off"), ESearchCase::IgnoreCase);
}

FString FAssetNamingConfig::PackageToRepoPath(const FString& PackageName) const
{
	static const FString GameRoot = TEXT("/Game");
	if (!PackageName.StartsWith(GameRoot))
	{
		return FString();
	}
	// "/Game/Foo/Bar" -> "Content" + "/Foo/Bar" + ".uasset"
	return ContentDir + PackageName.RightChop(GameRoot.Len()) + TEXT(".uasset");
}

FAssetNamingConfig FAssetNamingConfig::Load(const FString& RelativeJsonPath,
	const FString& ProjectNameOverride)
{
	FAssetNamingConfig Config;
	Config.ApplyBuiltInDefaults();

	if (!RelativeJsonPath.IsEmpty())
	{
		const FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / RelativeJsonPath);

		FString JsonText;
		if (FFileHelper::LoadFileToString(JsonText, *FullPath))
		{
			Config.MergeJson(JsonText, FullPath);
		}
		else
		{
			UE_LOG(LogAssetNaming, Log,
				TEXT("No conventions.json at '%s' — using built-in defaults."), *FullPath);
		}
	}

	// Engine-required locations, re-added because an "exemptPaths" block in the JSON replaces
	// the defaults wholesale. Content/Movies can't be moved under the project content folder
	// without the engine losing it, so no config is allowed to make it a violation.
	Config.ExemptPackageWildcards.AddUnique(TEXT("/Game/Movies/*"));

	// Whatever the checker would compute, recorded before the plugin's override wins.
	Config.ProjectNameWithoutOverride = Config.ProjectName;

	if (!ProjectNameOverride.IsEmpty())
	{
		Config.ProjectName = ProjectNameOverride;
		// Recompute rather than honouring any explicit "contentRoot" from the JSON: the
		// override is the more specific instruction, and leaving a stale content root
		// would make every placement rule point at the wrong folder.
		Config.ContentRoot = Config.ContentDir / Config.ProjectName;
	}

	return Config;
}

void FAssetNamingConfig::MergeJson(const FString& JsonText, const FString& SourcePath)
{
	const FString Stripped = StripJsonComments(JsonText);

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Stripped);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogAssetNaming, Warning,
			TEXT("Could not parse '%s' — using built-in defaults."), *SourcePath);
		return;
	}

	LoadedFrom = SourcePath;

	// Tokens understood by conventions.json path patterns. Both keys are nullable in the
	// JSON, which is why HasTypedField<String> is used rather than TryGetStringField —
	// an explicit null must fall back to the auto-detected value, not to "".
	if (Root->HasTypedField<EJson::String>(TEXT("contentDir")))
	{
		ContentDir = Root->GetStringField(TEXT("contentDir"));
	}
	if (Root->HasTypedField<EJson::String>(TEXT("projectName")))
	{
		ProjectName = Root->GetStringField(TEXT("projectName"));
	}
	if (Root->HasTypedField<EJson::String>(TEXT("contentRoot")))
	{
		ContentRoot = Root->GetStringField(TEXT("contentRoot"));
	}
	else
	{
		ContentRoot = ContentDir / ProjectName;
	}

	Root->TryGetBoolField(TEXT("requireProjectContentFolder"), bRequireProjectContentFolder);
	Root->TryGetStringField(TEXT("modulesDir"), ModulesDir);

	const TArray<TSharedPtr<FJsonValue>>* TopLevelArray = nullptr;
	if (Root->TryGetArrayField(TEXT("contentTopLevelFolders"), TopLevelArray) && TopLevelArray)
	{
		ContentTopLevelFolders.Empty();
		for (const TSharedPtr<FJsonValue>& Value : *TopLevelArray)
		{
			FString Folder;
			if (Value.IsValid() && Value->TryGetString(Folder))
			{
				ContentTopLevelFolders.Add(Folder);
			}
		}
	}

	const TSharedPtr<FJsonObject>* ModulesObj = nullptr;
	if (Root->TryGetObjectField(TEXT("modules"), ModulesObj) && ModulesObj && ModulesObj->IsValid())
	{
		(*ModulesObj)->TryGetBoolField(TEXT("allowLooseFilesInModulesDir"), bAllowLooseFilesInModulesDir);
	}

	Root->TryGetBoolField(TEXT("requireLibrarySubfolders"), bRequireLibrarySubfolders);
	Root->TryGetStringField(TEXT("libraryDir"), LibraryDir);

	const TArray<TSharedPtr<FJsonValue>>* DiscouragedArray = nullptr;
	if (Root->TryGetArrayField(TEXT("discouragedFolderNames"), DiscouragedArray) && DiscouragedArray)
	{
		DiscouragedFolderNames.Empty();
		for (const TSharedPtr<FJsonValue>& Value : *DiscouragedArray)
		{
			FString Folder;
			if (Value.IsValid() && Value->TryGetString(Folder))
			{
				DiscouragedFolderNames.Add(Folder);
			}
		}
	}

	const TSharedPtr<FJsonObject>* AudioObj = nullptr;
	if (Root->TryGetObjectField(TEXT("audio"), AudioObj) && AudioObj && AudioObj->IsValid())
	{
		(*AudioObj)->TryGetBoolField(TEXT("enabled"), bAudioCheckEnabled);
		(*AudioObj)->TryGetStringField(TEXT("audioDir"), AudioDir);
		(*AudioObj)->TryGetStringField(TEXT("voDir"), VoDir);

		const TArray<TSharedPtr<FJsonValue>>* PrefixArray = nullptr;
		if ((*AudioObj)->TryGetArrayField(TEXT("voPrefixes"), PrefixArray) && PrefixArray)
		{
			VoPrefixes.Empty();
			for (const TSharedPtr<FJsonValue>& Value : *PrefixArray)
			{
				FString Prefix;
				if (Value.IsValid() && Value->TryGetString(Prefix)) { VoPrefixes.Add(Prefix); }
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* AudioSuffixArray = nullptr;
		if ((*AudioObj)->TryGetArrayField(TEXT("audioSuffixes"), AudioSuffixArray) && AudioSuffixArray)
		{
			AudioSuffixes.Empty();
			for (const TSharedPtr<FJsonValue>& Value : *AudioSuffixArray)
			{
				FString AudioSuffix;
				if (Value.IsValid() && Value->TryGetString(AudioSuffix)) { AudioSuffixes.Add(AudioSuffix); }
			}
		}
	}

	// "suffixes" merges into the built-in table rather than replacing it — same semantics
	// as check_conventions.py.
	const TSharedPtr<FJsonObject>* SuffixObj = nullptr;
	if (Root->TryGetObjectField(TEXT("suffixes"), SuffixObj) && SuffixObj && SuffixObj->IsValid())
	{
		for (const auto& Pair : (*SuffixObj)->Values)
		{
			FString Desc;
			if (Pair.Value.IsValid() && Pair.Value->TryGetString(Desc))
			{
				Suffixes.Add(FString(*Pair.Key), Desc);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* TagArray = nullptr;
	if (Root->TryGetArrayField(TEXT("midNameTags"), TagArray) && TagArray)
	{
		MidNameTags.Empty();
		for (const TSharedPtr<FJsonValue>& Value : *TagArray)
		{
			FString Tag;
			if (Value.IsValid() && Value->TryGetString(Tag))
			{
				MidNameTags.Add(Tag);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ExemptArray = nullptr;
	if (Root->TryGetArrayField(TEXT("exemptPaths"), ExemptArray) && ExemptArray)
	{
		ExemptPackageWildcards.Empty();
		for (const TSharedPtr<FJsonValue>& Value : *ExemptArray)
		{
			FString Pattern;
			if (!Value.IsValid() || !Value->TryGetString(Pattern))
			{
				continue;
			}

			Pattern = Pattern.Replace(TEXT("{contentRoot}"), *ContentRoot)
			                 .Replace(TEXT("{contentDir}"), *ContentDir)
			                 .Replace(TEXT("{projectName}"), *ProjectName);
			Pattern.ReplaceInline(TEXT("\\"), TEXT("/"));

			// Only repo paths under Content/ map onto package paths. Everything else
			// (Houdini/, Movies/, .github/) holds no assets, so it needs no translation.
			const FString Prefix = ContentDir + TEXT("/");
			if (!Pattern.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				continue;
			}

			FString PackageWildcard = TEXT("/Game/") + Pattern.RightChop(Prefix.Len());
			// FString::MatchesWildcard's '*' already spans '/', so ** and * are equivalent.
			PackageWildcard.ReplaceInline(TEXT("**"), TEXT("*"));
			// "Content/UsdAssetCache.uasset" -> a package path has no extension.
			if (PackageWildcard.EndsWith(TEXT(".uasset")) || PackageWildcard.EndsWith(TEXT(".umap")))
			{
				PackageWildcard = FPaths::GetBaseFilename(PackageWildcard, false);
			}

			ExemptPackageWildcards.AddUnique(PackageWildcard);
		}
	}

	// Respect the project turning individual rules off.
	const TSharedPtr<FJsonObject>* SeverityObj = nullptr;
	if (Root->TryGetObjectField(TEXT("severity"), SeverityObj) && SeverityObj && SeverityObj->IsValid())
	{
		for (const auto& Pair : (*SeverityObj)->Values)
		{
			FString Severity;
			if (Pair.Value.IsValid() && Pair.Value->TryGetString(Severity))
			{
				Severities.Add(FString(*Pair.Key), Severity);
			}
		}
	}
}

FString FAssetNamingConfig::FindTrailingSuffix(const FString& Stem, bool bCaseSensitive) const
{
	const ESearchCase::Type SearchCase =
		bCaseSensitive ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase;

	FString Best;
	for (const auto& Pair : Suffixes)
	{
		const FString& Suffix = Pair.Key;
		// len(stem) > len(suffix): a name that is *only* a suffix doesn't count as suffixed.
		if (Stem.Len() > Suffix.Len() && Stem.EndsWith(Suffix, SearchCase))
		{
			if (Suffix.Len() > Best.Len())
			{
				Best = Suffix;
			}
		}
	}
	return Best;
}

bool FAssetNamingConfig::EndsWithMidNameTag(const FString& Stem) const
{
	for (const FString& Tag : MidNameTags)
	{
		if (Stem.EndsWith(Tag, ESearchCase::CaseSensitive))
		{
			return true;
		}
	}
	return false;
}

bool FAssetNamingConfig::IsExemptPackage(const FString& PackagePath) const
{
	for (const FString& Wildcard : ExemptPackageWildcards)
	{
		if (PackagePath.MatchesWildcard(Wildcard, ESearchCase::IgnoreCase))
		{
			return true;
		}

		// "/Game/Movies/*" covers the contents but not "/Game/Movies" itself, because the
		// '*' can't match the missing slash. The folder-level rules are handed exactly that
		// path, so without this an exempt folder still gets flagged for existing.
		FString ExemptRoot = Wildcard;
		if (ExemptRoot.RemoveFromEnd(TEXT("/*")) && PackagePath.Equals(ExemptRoot, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}
