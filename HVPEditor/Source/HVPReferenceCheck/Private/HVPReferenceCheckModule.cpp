#include "HVPReferenceCheckLog.h"
#include "HVPReferenceCheckSettings.h"
#include "HVPReferenceRules.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY(LogHVPReferenceCheck);

/**
 * Hooks package saves so a leak is reported the moment it is introduced, while the offending
 * asset is still open in front of the person who made it. The commandlet covers everyone else.
 */
class FHVPReferenceCheckModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (!GIsEditor || IsRunningCommandlet())
		{
			return;
		}

		SavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FHVPReferenceCheckModule::OnPackageSaved);

		AuditCommand = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("HVPReferences.Audit"),
			TEXT("Log every plugin package that references project content, a project module, or an undeclared plugin."),
			FConsoleCommandDelegate::CreateRaw(this, &FHVPReferenceCheckModule::Audit),
			ECVF_Default);
	}

	virtual void ShutdownModule() override
	{
		if (SavedHandle.IsValid())
		{
			UPackage::PackageSavedWithContextEvent.Remove(SavedHandle);
		}
		if (AuditCommand)
		{
			IConsoleManager::Get().UnregisterConsoleObject(AuditCommand);
			AuditCommand = nullptr;
		}
	}

private:
	FDelegateHandle SavedHandle;
	IConsoleObject* AuditCommand = nullptr;

	void OnPackageSaved(const FString& PackageFileName, UPackage* Package, FObjectPostSaveContext Context)
	{
		const UHVPReferenceCheckSettings& Settings = UHVPReferenceCheckSettings::Get();
		if (!Settings.bEnabled || !Settings.bCheckOnSave || !Package || !Context.SaveSucceeded() || Context.IsProceduralSave())
		{
			return;
		}

		TSharedPtr<IPlugin> Owner;
		if (!FHVPReferenceRules::IsCheckedPackage(Package->GetFName(), Owner))
		{
			return;
		}

		// The registry's dependency list for this package comes from the file on disk; make sure it
		// reflects the save that just happened rather than the previous one.
		IAssetRegistry::GetChecked().ScanModifiedAssetFiles({ PackageFileName });

		TArray<FHVPReferenceViolation> Violations;
		if (FHVPReferenceRules::CheckPackage(Package->GetFName(), Violations) == 0)
		{
			return;
		}

		FString Lines;
		for (const FHVPReferenceViolation& V : Violations)
		{
			UE_LOG(LogHVPReferenceCheck, Warning, TEXT("%s"), *V.ToString());
			Lines += FString::Printf(TEXT("\n%s  (%s)"), *V.Dependency.ToString(), *V.Reason);
		}

		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("HVPReferenceCheck", "SaveLeak", "{0} references content it may not:{1}"),
			FText::FromString(Package->GetName()), FText::FromString(Lines)));
		Info.ExpireDuration = 12.0f;
		Info.bUseLargeFont = false;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}

	void Audit()
	{
		if (!UHVPReferenceCheckSettings::Get().bEnabled)
		{
			UE_LOG(LogHVPReferenceCheck, Display, TEXT("HVP Reference Check is disabled in Project Settings."));
			return;
		}
		TArray<FHVPReferenceViolation> Violations;
		const int32 Examined = FHVPReferenceRules::AuditAll({}, Violations);
		for (const FHVPReferenceViolation& V : Violations)
		{
			UE_LOG(LogHVPReferenceCheck, Warning, TEXT("%s"), *V.ToString());
		}
		UE_LOG(LogHVPReferenceCheck, Display, TEXT("Checked %d plugin packages: %d violation(s)."), Examined, Violations.Num());
	}
};

IMPLEMENT_MODULE(FHVPReferenceCheckModule, HVPReferenceCheck)
