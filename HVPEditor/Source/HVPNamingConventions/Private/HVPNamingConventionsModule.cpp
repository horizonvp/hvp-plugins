#include "AssetNamingLog.h"
#include "AssetNamingWatcher.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAssetNaming);

class FHVPNamingConventionsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Editor-only module, but a commandlet or -game run can still load editor modules;
		// there is nothing to watch in either case.
		if (!GIsEditor || IsRunningCommandlet())
		{
			return;
		}

		Watcher = MakeUnique<FAssetNamingWatcher>();
		Watcher->Startup();

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("HVPNaming.Audit"),
			TEXT("Log every /Game asset whose name lacks a recognised type suffix. Read-only."),
			FConsoleCommandDelegate::CreateLambda([this]
			{
				if (Watcher) { Watcher->AuditProject(); }
			}),
			ECVF_Default));

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("HVPNaming.Flush"),
			TEXT("Rename everything queued right now, without waiting out the debounce."),
			FConsoleCommandDelegate::CreateLambda([this]
			{
				if (Watcher) { Watcher->FlushNow(); }
			}),
			ECVF_Default));

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("HVPNaming.RecheckPlacement"),
			TEXT("Re-run the folder rules over anything still flagged; clears the warning if all pass."),
			FConsoleCommandDelegate::CreateLambda([this]
			{
				if (Watcher) { Watcher->RecheckPlacement(); }
			}),
			ECVF_Default));

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("HVPNaming.FixSelected"),
			TEXT("Apply the type suffix to the current Content Browser selection."),
			FConsoleCommandDelegate::CreateLambda([this]
			{
				if (Watcher) { Watcher->FixSelection(); }
			}),
			ECVF_Default));
	}

	virtual void ShutdownModule() override
	{
		for (IConsoleObject* Command : ConsoleCommands)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Command);
		}
		ConsoleCommands.Empty();

		if (Watcher)
		{
			Watcher->Shutdown();
			Watcher.Reset();
		}
	}

private:
	TUniquePtr<FAssetNamingWatcher> Watcher;
	TArray<IConsoleObject*> ConsoleCommands;
};

IMPLEMENT_MODULE(FHVPNamingConventionsModule, HVPNamingConventions)
