#include "HVPPackageInstaller.h"
#include "SHVPPackageInstallPanel.h"

#include "Framework/Docking/TabManager.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "HVPPackageInstall"

/**
 * Package and Install to Headset: the Platforms-menu entry (just above Package Project), the
 * HVP.PackageAndInstall console command, and the Package and Install window (Window > Tools), which
 * opens as a small floating window when a run starts and comes back to the front when it ends.
 */
class FHVPPackageInstallModule : public IModuleInterface
{
public:
	static inline const FName TabId = TEXT("HVPPackageInstall");

	virtual void StartupModule() override
	{
		if (!GIsEditor || IsRunningCommandlet())
		{
			return;
		}
		Installer = MakeShared<FHVPPackageInstaller>();
		Installer->OnRunStarted.AddRaw(this, &FHVPPackageInstallModule::ShowWindow, false);
		Installer->OnRunFinished.AddRaw(this, &FHVPPackageInstallModule::ShowWindow, true);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId, FOnSpawnTab::CreateRaw(this, &FHVPPackageInstallModule::SpawnTab))
			.SetDisplayName(LOCTEXT("TabName", "Package and Install"))
			.SetTooltipText(LOCTEXT("TabTip", "Progress and result of Package and Install to Headset."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "MainFrame.PackageProject"));

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FHVPPackageInstallModule::RegisterMenu));

		// The same as the menu entry, for scripts and keyboard people.
		Command = IConsoleManager::Get().RegisterConsoleCommand(TEXT("HVP.PackageAndInstall"),
			TEXT("Package for Android (ASTC) with the Package Project settings and install on the connected headset."),
			FConsoleCommandDelegate::CreateLambda([Weak = TWeakPtr<FHVPPackageInstaller>(Installer)]()
			{
				if (TSharedPtr<FHVPPackageInstaller> Pinned = Weak.Pin())
				{
					Pinned->Start();
				}
			}));
	}

	virtual void ShutdownModule() override
	{
		if (Command)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Command);
			Command = nullptr;
		}
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
		}
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		Installer.Reset();
	}

private:
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs&)
	{
		bJustSpawned = true;
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SHVPPackageInstallPanel, Installer)
			];
	}

	/** Opens the window (or brings it back), centred on the editor; flashes it when a run ends. */
	void ShowWindow(bool bFinished)
	{
		bJustSpawned = false;
		const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(TabId);
		if (!Tab)
		{
			return;
		}
		// The tab manager lays a newly spawned floating window out after this returns, overriding any
		// size or place set now: arrange it on the next tick instead.
		const bool bSize = bJustSpawned;
		TWeakPtr<SDockTab> WeakTab = Tab;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, WeakTab, bSize, bFinished](float)
		{
			if (const TSharedPtr<SDockTab> Pinned = WeakTab.Pin())
			{
				ArrangeWindow(Pinned.ToSharedRef(), bSize, bFinished);
			}
			return false;  // once
		}));
	}

	void ArrangeWindow(const TSharedRef<SDockTab>& Tab, bool bSize, bool bFinished)
	{
		const TSharedPtr<SWindow> Window = Tab->GetParentWindow();
		const TSharedPtr<SWindow> Root = FGlobalTabmanager::Get()->GetRootWindow();
		if (!Window || Window == Root)
		{
			// Docked into the main window: draw the eye to the tab instead.
			Tab->FlashTab();
			return;
		}
		if (bSize)
		{
			Window->Resize(FVector2D(560.f, 760.f));
		}
		if (Root)
		{
			const FVector2D Centre = Root->GetPositionInScreen() + (Root->GetSizeInScreen() - Window->GetSizeInScreen()) * 0.5f;
			Window->MoveWindowTo(Centre);
		}
		Window->BringToFront(/*bForce*/ true);
		if (bFinished)
		{
			Window->FlashWindow();
		}
	}

	void RegisterMenu()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Platforms");
		FToolMenuSection& Section = Menu->FindOrAddSection("ContentManagementSection");

		TWeakPtr<FHVPPackageInstaller> Weak = Installer;
		FToolMenuEntry Entry = FToolMenuEntry::InitMenuEntry(
			"HVPPackageAndInstall",
			LOCTEXT("Label", "Package and Install to Headset"),
			// The tooltip says why when the entry is greyed out.
			TAttribute<FText>::CreateLambda([Weak]()
			{
				FText Why;
				if (TSharedPtr<FHVPPackageInstaller> Pinned = Weak.Pin())
				{
					Pinned->CanStart(&Why);
				}
				return Why;
			}),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "MainFrame.PackageProject"),
			FUIAction(
				FExecuteAction::CreateLambda([Weak]()
				{
					if (TSharedPtr<FHVPPackageInstaller> Pinned = Weak.Pin())
					{
						Pinned->Start();
					}
				}),
				FCanExecuteAction::CreateLambda([Weak]()
				{
					const TSharedPtr<FHVPPackageInstaller> Pinned = Weak.Pin();
					return Pinned && Pinned->CanStart();
				})));
		Entry.InsertPosition = FToolMenuInsert("PackageSubMenu", EToolMenuInsertType::Before);
		Section.AddEntry(Entry);
	}

	TSharedPtr<FHVPPackageInstaller> Installer;
	IConsoleCommand* Command = nullptr;
	/** The tab was just created, so its floating window still has the default size. */
	bool bJustSpawned = false;
};

IMPLEMENT_MODULE(FHVPPackageInstallModule, HVPPackageInstall);

#undef LOCTEXT_NAMESPACE
