#include "HVPReferenceCheckCommandlet.h"

#include "HVPReferenceCheckLog.h"
#include "HVPReferenceRules.h"

#include "AssetRegistry/IAssetRegistry.h"

UHVPReferenceCheckCommandlet::UHVPReferenceCheckCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UHVPReferenceCheckCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> Values;
	ParseCommandLine(*Params, Tokens, Switches, Values);

	TArray<FString> PluginFilter;
	if (const FString* Plugins = Values.Find(TEXT("Plugins")))
	{
		Plugins->ParseIntoArray(PluginFilter, TEXT(","), true);
	}

	// A commandlet starts with an empty registry: scan synchronously before asking it anything.
	IAssetRegistry::GetChecked().SearchAllAssets(/*bSynchronousSearch*/ true);

	TArray<FHVPReferenceViolation> Violations;
	const int32 Examined = FHVPReferenceRules::AuditAll(PluginFilter, Violations);

	for (const FHVPReferenceViolation& V : Violations)
	{
		UE_LOG(LogHVPReferenceCheck, Error, TEXT("%s"), *V.ToString());
	}
	UE_LOG(LogHVPReferenceCheck, Display, TEXT("Checked %d plugin packages: %d violation(s)."), Examined, Violations.Num());

	return Violations.Num() > 0 ? 1 : 0;
}
