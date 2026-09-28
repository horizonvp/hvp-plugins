#include "HVPGraphSelect.h"
#include "HVPGraphSelectInput.h"

#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

class FHVPGraphSelectEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Deferred, for the same reason every other menu extension is: ExtendMenu needs the menu
		// system up, and graph node menus are registered lazily the first time one is opened.
		// Registering directly here silently no-ops.
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(
			&FHVPGraphSelect::RegisterMenus));

		// Not deferred: the preprocessor chain belongs to Slate, which is up well before the menu
		// system finishes, and registering early costs nothing.
		FHVPGraphSelectInput::Register();
	}

	virtual void ShutdownModule() override
	{
		// Before anything else: a live preprocessor whose code has been unloaded crashes on the
		// next mouse move.
		FHVPGraphSelectInput::Unregister();

		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
};

IMPLEMENT_MODULE(FHVPGraphSelectEditorModule, HVPGraphSelectEditor);
