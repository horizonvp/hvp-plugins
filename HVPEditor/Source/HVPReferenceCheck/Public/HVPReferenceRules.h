#pragma once

#include "CoreMinimal.h"

class IPlugin;

/** One illegal reference: a package inside a checked plugin depends on something it may not. */
struct HVPREFERENCECHECK_API FHVPReferenceViolation
{
	/** The plugin whose content leaks. */
	FString PluginName;
	/** The package inside that plugin. */
	FName Package;
	/** What it references. */
	FName Dependency;
	/** Why that is not allowed, in one sentence. */
	FString Reason;

	FString ToString() const;
};

/**
 * The rule set, usable from the editor, the console command and the commandlet alike.
 *
 * A package is "checked" when it lives under the mount point of a content plugin loaded from the
 * project (not the engine) and not listed as exempt. For each such package every dependency
 * recorded by the asset registry, hard and soft, is classified:
 *
 *   /Engine/...           always allowed
 *   /Script/<Module>      allowed if the module is an engine module, belongs to the same plugin, or
 *                         belongs to a plugin declared in the .uplugin; a project module is a leak
 *   /Game/...             a leak, unless under an AllowedRoots entry
 *   /<OtherPlugin>/...    allowed if it is the same plugin or a declared dependency
 *
 * Only the asset registry's on-disk dependency data is consulted, so the check is cheap and needs
 * nothing loaded. On save the registry is refreshed for that one file first.
 */
class HVPREFERENCECHECK_API FHVPReferenceRules
{
public:
	/** True when PackageName lives inside a plugin this check enforces. OutPlugin is that plugin. */
	static bool IsCheckedPackage(FName PackageName, TSharedPtr<IPlugin>& OutPlugin);

	/** Check one package. Returns the number of violations appended to Out. */
	static int32 CheckPackage(FName PackageName, TArray<FHVPReferenceViolation>& Out);

	/**
	 * Check every package of every enforced plugin. PluginFilter, when non-empty, restricts the
	 * sweep to those plugin names. Returns the number of packages examined.
	 */
	static int32 AuditAll(const TArray<FString>& PluginFilter, TArray<FHVPReferenceViolation>& Out);

	/** The plugin that owns a content mount point ("/Foo/Bar" -> plugin Foo), or null. */
	static TSharedPtr<IPlugin> FindPluginForPackage(FName PackageName);

	/** The plugin whose descriptor lists ModuleName, or null for engine and project modules. */
	static TSharedPtr<IPlugin> FindPluginForModule(FName ModuleName);

	/** True if ModuleName is one of the project's own modules (from the .uproject). */
	static bool IsProjectModule(FName ModuleName);

private:
	static bool IsDeclaredDependency(const IPlugin& From, const IPlugin& To);
	static bool IsAllowedRoot(const FString& PackagePath);
};
