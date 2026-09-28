#include "PlacementWarning.h"

#include "AssetNamingConfig.h"
#include "AssetNamingLog.h"
#include "AssetNamingSettings.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "HVPNamingConventions"

namespace
{
	/** Don't list fifty assets in a toast. */
	constexpr int32 MaxListedInWarning = 6;
}

void FPlacementWarningPresenter::Initialise(const FAssetNamingConfig* InConfig)
{
	Config = InConfig;

	IAssetRegistry& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	// Moving an asset in the Content Browser surfaces as a rename. This is what lets the
	// warning retract itself the instant the user does the right thing.
	AssetRenamedHandle = Registry.OnAssetRenamed().AddRaw(this, &FPlacementWarningPresenter::HandleAssetRenamed);
	AssetRemovedHandle = Registry.OnAssetRemoved().AddRaw(this, &FPlacementWarningPresenter::HandleAssetRemoved);
	// Renaming a folder surfaces as remove + add, so this is what retracts a folder warning.
	PathRemovedHandle = Registry.OnPathRemoved().AddRaw(this, &FPlacementWarningPresenter::HandlePathRemoved);
}

void FPlacementWarningPresenter::Shutdown()
{
	if (IAssetRegistry* Registry = IAssetRegistry::Get())
	{
		Registry->OnAssetRenamed().Remove(AssetRenamedHandle);
		Registry->OnAssetRemoved().Remove(AssetRemovedHandle);
		Registry->OnPathRemoved().Remove(PathRemovedHandle);
	}

	if (Notification.IsValid())
	{
		Notification->SetCompletionState(SNotificationItem::CS_None);
		Notification->ExpireAndFadeout();
		Notification.Reset();
	}
	Outstanding.Empty();
	OutstandingFolders.Empty();
	Config = nullptr;
}

void FPlacementWarningPresenter::HandlePathRemoved(const FString& /*Path*/)
{
	if (HasOutstanding())
	{
		Refresh();
	}
}

void FPlacementWarningPresenter::HandleAssetRenamed(const FAssetData& /*NewData*/, const FString& /*OldPath*/)
{
	if (HasOutstanding())
	{
		Refresh();
	}
}

void FPlacementWarningPresenter::HandleAssetRemoved(const FAssetData& /*Data*/)
{
	if (HasOutstanding())
	{
		Refresh();
	}
}

TArray<FPlacementViolation> FPlacementWarningPresenter::EvaluateOutstanding()
{
	TArray<FPlacementViolation> Live;
	if (!Config)
	{
		Outstanding.Empty();
		OutstandingFolders.Empty();
		return Live;
	}

	// Folders first. A folder that no longer exists was renamed or deleted, which is
	// exactly the fix we're asking for, so it simply drops off the list.
	IAssetRegistry* Registry = IAssetRegistry::Get();
	TArray<FString> StillBadFolders;
	for (const FString& Folder : OutstandingFolders)
	{
		if (Registry && !Registry->PathExists(Folder))
		{
			continue;
		}

		TArray<FPlacementViolation> Violations = AssetPlacementRules::CheckFolder(Folder, *Config);
		if (Violations.Num() > 0)
		{
			StillBadFolders.Add(Folder);
			Live.Append(MoveTemp(Violations));
		}
	}
	OutstandingFolders = MoveTemp(StillBadFolders);

	TArray<TWeakObjectPtr<UObject>> StillBad;
	for (const TWeakObjectPtr<UObject>& Weak : Outstanding)
	{
		UObject* Asset = Weak.Get();
		if (!Asset)
		{
			continue;   // deleted or GC'd — no longer our problem
		}

		TArray<FPlacementViolation> Violations = AssetPlacementRules::Check(Asset, *Config);
		if (Violations.Num() > 0)
		{
			StillBad.Add(Weak);
			Live.Append(MoveTemp(Violations));
		}
	}

	Outstanding = MoveTemp(StillBad);
	return Live;
}

void FPlacementWarningPresenter::Report(const TArray<FPlacementViolation>& NewViolations)
{
	if (NewViolations.Num() == 0)
	{
		return;
	}

	for (const FPlacementViolation& Violation : NewViolations)
	{
		UE_LOG(LogAssetNaming, Warning, TEXT("placement: %s  [%s]  %s"),
			*Violation.PackageName, *Violation.Rule, *Violation.Message);

		if (Violation.Asset.IsValid())
		{
			Outstanding.AddUnique(Violation.Asset);
		}
	}

	Refresh();
}

void FPlacementWarningPresenter::ReportFolder(const FString& PackagePath,
	const TArray<FPlacementViolation>& NewViolations)
{
	if (NewViolations.Num() == 0)
	{
		return;
	}

	for (const FPlacementViolation& Violation : NewViolations)
	{
		UE_LOG(LogAssetNaming, Warning, TEXT("placement: %s/  [%s]  %s"),
			*Violation.PackageName, *Violation.Rule, *Violation.Message);
	}

	OutstandingFolders.AddUnique(PackagePath);
	Refresh();
}

void FPlacementWarningPresenter::Recheck()
{
	Refresh();
}

void FPlacementWarningPresenter::Refresh()
{
	const UAssetNamingSettings& Settings = UAssetNamingSettings::Get();
	const TArray<FPlacementViolation> Live = EvaluateOutstanding();

	if (Live.Num() == 0)
	{
		Dismiss(/*bSuccess*/ true);
		return;
	}

	// Log-only mode, or no Slate to draw into (commandlet / -unattended test run).
	if (Settings.PlacementWarning == EPlacementWarningStyle::LogOnly
		|| Settings.PlacementWarning == EPlacementWarningStyle::Off
		|| FApp::IsUnattended()
		|| !FSlateApplication::IsInitialized())
	{
		return;
	}

	// Collapse duplicates. A folder flagged as type-folder makes every asset inside it
	// report the identical message, and listing both is noise. Folders are evaluated
	// first, so the surviving entry is the folder — the more actionable one.
	TArray<FString> Lines;
	TSet<FString> Seen;
	for (const FPlacementViolation& Violation : Live)
	{
		const FString Key = Violation.Rule + TEXT("|") + Violation.Message;
		if (!Seen.Contains(Key))
		{
			Seen.Add(Key);
			Lines.Add(Violation.ToLine());
		}
	}

	FTextBuilder Builder;
	Builder.AppendLine(Lines.Num() == 1
		? LOCTEXT("PlacementOne", "This is not in a valid location:")
		: FText::Format(LOCTEXT("PlacementMany", "{0} items are not in a valid location:"),
			FText::AsNumber(Lines.Num())));

	const int32 Listed = FMath::Min(Lines.Num(), MaxListedInWarning);
	for (int32 i = 0; i < Listed; ++i)
	{
		Builder.AppendLine(FText::FromString(TEXT("  • ") + Lines[i]));
	}
	if (Lines.Num() > Listed)
	{
		Builder.AppendLine(FText::Format(LOCTEXT("PlacementMore", "  ...and {0} more (see Output Log)"),
			FText::AsNumber(Lines.Num() - Listed)));
	}

	const bool bSticky = Settings.PlacementWarning == EPlacementWarningStyle::Persistent;
	if (bSticky)
	{
		Builder.AppendLine(LOCTEXT("PlacementFooter",
			"Move them somewhere valid — this clears itself as soon as they are."));
	}

	const FText Message = Builder.ToText();

	if (Notification.IsValid())
	{
		Notification->SetText(Message);
		return;   // already showing; just refresh the text
	}

	FNotificationInfo Info(Message);
	Info.bFireAndForget = !bSticky;
	Info.bUseSuccessFailIcons = true;
	Info.ExpireDuration = bSticky ? 0.0f : 6.0f;
	Info.FadeOutDuration = 0.5f;

	Info.Hyperlink = FSimpleDelegate::CreateRaw(this, &FPlacementWarningPresenter::ShowFirstOffender);
	Info.HyperlinkText = LOCTEXT("ShowAsset", "Show in Content Browser");

	if (bSticky)
	{
		// The only way out is to fix it, then press this. (Moving the asset re-checks
		// automatically via OnAssetRenamed; this button is for the impatient, and for
		// changes the registry doesn't report.)
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("Recheck", "Re-check"),
			LOCTEXT("RecheckTip", "Re-run the folder rules. This message clears once every asset is in a valid location."),
			FSimpleDelegate::CreateRaw(this, &FPlacementWarningPresenter::Recheck),
			SNotificationItem::CS_Fail));
	}

	Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

void FPlacementWarningPresenter::Dismiss(bool bSuccess)
{
	Outstanding.Empty();
	OutstandingFolders.Empty();

	if (!Notification.IsValid())
	{
		return;
	}

	Notification->SetText(LOCTEXT("PlacementResolved", "Placement fixed."));
	Notification->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_None);
	Notification->SetExpireDuration(2.0f);
	Notification->SetFadeOutDuration(0.5f);
	Notification->ExpireAndFadeout();
	Notification.Reset();
}

void FPlacementWarningPresenter::ShowFirstOffender()
{
	for (const TWeakObjectPtr<UObject>& Weak : Outstanding)
	{
		if (UObject* Asset = Weak.Get())
		{
			FContentBrowserModule& ContentBrowser =
				FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			ContentBrowser.Get().SyncBrowserToAssets(TArray<FAssetData>{ FAssetData(Asset) });
			return;
		}
	}
}

#undef LOCTEXT_NAMESPACE
