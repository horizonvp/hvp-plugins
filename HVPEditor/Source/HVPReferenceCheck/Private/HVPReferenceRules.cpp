#include "HVPReferenceRules.h"

#include "HVPReferenceCheckLog.h"
#include "HVPReferenceCheckSettings.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"

FString FHVPReferenceViolation::ToString() const
{
	return FString::Printf(TEXT("%s: %s -> %s (%s)"),
		*PluginName, *Package.ToString(), *Dependency.ToString(), *Reason);
}

namespace
{
	/** "/Foo/Bar/Baz" -> "Foo". Empty when the path has no root. */
	FString RootOf(const FString& PackagePath)
	{
		if (!PackagePath.StartsWith(TEXT("/")))
		{
			return FString();
		}
		const int32 Slash = PackagePath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
		return Slash == INDEX_NONE ? PackagePath.Mid(1) : PackagePath.Mid(1, Slash - 1);
	}

	bool IsReservedRoot(const FString& Root)
	{
		return Root == TEXT("Game") || Root == TEXT("Engine") || Root == TEXT("Script")
			|| Root == TEXT("Temp") || Root == TEXT("Memory") || Root == TEXT("Config")
			|| Root.IsEmpty();
	}
}

TSharedPtr<IPlugin> FHVPReferenceRules::FindPluginForPackage(FName PackageName)
{
	const FString Root = RootOf(PackageName.ToString());
	if (IsReservedRoot(Root))
	{
		return nullptr;
	}
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Root);
	if (Plugin.IsValid() && Plugin->CanContainContent())
	{
		return Plugin;
	}
	return nullptr;
}

TSharedPtr<IPlugin> FHVPReferenceRules::FindPluginForModule(FName ModuleName)
{
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
		{
			if (Module.Name == ModuleName)
			{
				return Plugin;
			}
		}
	}
	return nullptr;
}

bool FHVPReferenceRules::IsProjectModule(FName ModuleName)
{
	const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
	if (!Project)
	{
		return false;
	}
	for (const FModuleDescriptor& Module : Project->Modules)
	{
		if (Module.Name == ModuleName)
		{
			return true;
		}
	}
	return false;
}

bool FHVPReferenceRules::IsCheckedPackage(FName PackageName, TSharedPtr<IPlugin>& OutPlugin)
{
	OutPlugin = FindPluginForPackage(PackageName);
	if (!OutPlugin.IsValid())
	{
		return false;
	}
	if (OutPlugin->GetLoadedFrom() != EPluginLoadedFrom::Project)
	{
		return false;
	}
	return !UHVPReferenceCheckSettings::Get().ExemptPlugins.Contains(OutPlugin->GetName());
}

bool FHVPReferenceRules::IsDeclaredDependency(const IPlugin& From, const IPlugin& To)
{
	for (const FPluginReferenceDescriptor& Ref : From.GetDescriptor().Plugins)
	{
		if (Ref.bEnabled && Ref.Name == To.GetName())
		{
			return true;
		}
	}
	return false;
}

bool FHVPReferenceRules::IsAllowedRoot(const FString& PackagePath)
{
	for (const FString& Root : UHVPReferenceCheckSettings::Get().AllowedRoots)
	{
		if (!Root.IsEmpty() && PackagePath.StartsWith(Root))
		{
			return true;
		}
	}
	return false;
}

int32 FHVPReferenceRules::CheckPackage(FName PackageName, TArray<FHVPReferenceViolation>& Out)
{
	TSharedPtr<IPlugin> Owner;
	if (!IsCheckedPackage(PackageName, Owner))
	{
		return 0;
	}

	TArray<FName> Dependencies;
	IAssetRegistry::GetChecked().GetDependencies(PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);

	const bool bRequireEnginePlugins = UHVPReferenceCheckSettings::Get().bRequireDeclaredEnginePlugins;
	int32 Count = 0;

	auto Report = [&](FName Dependency, FString Reason)
	{
		FHVPReferenceViolation& V = Out.AddDefaulted_GetRef();
		V.PluginName = Owner->GetName();
		V.Package = PackageName;
		V.Dependency = Dependency;
		V.Reason = MoveTemp(Reason);
		++Count;
	};

	for (const FName& Dependency : Dependencies)
	{
		const FString Path = Dependency.ToString();
		const FString Root = RootOf(Path);

		if (Root == TEXT("Engine") || Root == TEXT("Temp") || Root == TEXT("Memory"))
		{
			continue;
		}

		if (Root == TEXT("Script"))
		{
			const FName Module(*Path.Mid(8)); // strip "/Script/"
			TSharedPtr<IPlugin> ModuleOwner = FindPluginForModule(Module);
			if (ModuleOwner.IsValid())
			{
				if (ModuleOwner == Owner || IsDeclaredDependency(*Owner, *ModuleOwner))
				{
					continue;
				}
				if (ModuleOwner->GetLoadedFrom() == EPluginLoadedFrom::Engine && !bRequireEnginePlugins)
				{
					continue;
				}
				Report(Dependency, FString::Printf(TEXT("module of plugin %s, which %s.uplugin does not declare"),
					*ModuleOwner->GetName(), *Owner->GetName()));
			}
			else if (IsProjectModule(Module))
			{
				Report(Dependency, TEXT("project module; a plugin cannot depend on the project that hosts it"));
			}
			// Otherwise an engine module: always allowed.
			continue;
		}

		if (Root == TEXT("Game"))
		{
			if (!IsAllowedRoot(Path))
			{
				Report(Dependency, TEXT("project content; the plugin only works in projects that have this asset"));
			}
			continue;
		}

		TSharedPtr<IPlugin> Other = FindPluginForPackage(Dependency);
		if (!Other.IsValid())
		{
			// An unknown mount point (a virtual path, a disabled plugin). Not ours to judge.
			UE_LOG(LogHVPReferenceCheck, Verbose, TEXT("%s -> %s: unknown mount point, skipped"),
				*PackageName.ToString(), *Path);
			continue;
		}
		if (Other == Owner || IsDeclaredDependency(*Owner, *Other))
		{
			continue;
		}
		if (Other->GetLoadedFrom() == EPluginLoadedFrom::Engine && !bRequireEnginePlugins)
		{
			continue;
		}
		Report(Dependency, FString::Printf(TEXT("content of plugin %s, which %s.uplugin does not declare"),
			*Other->GetName(), *Owner->GetName()));
	}

	return Count;
}

int32 FHVPReferenceRules::AuditAll(const TArray<FString>& PluginFilter, TArray<FHVPReferenceViolation>& Out)
{
	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	int32 Examined = 0;

	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
	{
		if (Plugin->GetLoadedFrom() != EPluginLoadedFrom::Project)
		{
			continue;
		}
		if (PluginFilter.Num() > 0 && !PluginFilter.Contains(Plugin->GetName()))
		{
			continue;
		}
		if (UHVPReferenceCheckSettings::Get().ExemptPlugins.Contains(Plugin->GetName()))
		{
			continue;
		}

		FString Mount = Plugin->GetMountedAssetPath();
		Mount.RemoveFromEnd(TEXT("/"));

		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(FName(*Mount), Assets, /*bRecursive*/ true, /*bIncludeOnlyOnDiskAssets*/ true);

		TSet<FName> Packages;
		for (const FAssetData& Asset : Assets)
		{
			Packages.Add(Asset.PackageName);
		}
		for (const FName& Package : Packages)
		{
			CheckPackage(Package, Out);
			++Examined;
		}
	}
	return Examined;
}
