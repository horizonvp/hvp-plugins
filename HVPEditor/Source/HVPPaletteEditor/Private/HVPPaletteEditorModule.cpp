#include "HVPPaletteFromTexture.h"
#include "HVPPaletteGenerator.h"

#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

class FHVPPaletteEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Deferred: ExtendMenu needs the menu to exist, and the content browser registers its asset
		// menus after modules start up. Registering directly here silently no-ops.
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda(
			[]
			{
				FHVPPaletteGenerator::RegisterMenus();
				FHVPPaletteFromTexture::RegisterMenus();
			}));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
};

IMPLEMENT_MODULE(FHVPPaletteEditorModule, HVPPaletteEditor);
