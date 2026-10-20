#include "HVPTextFontMaker.h"
#include "Modules/ModuleManager.h"

class FHVPSystemsEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		HVPTextFontMaker::Register();
	}

	virtual void ShutdownModule() override
	{
		HVPTextFontMaker::Unregister();
	}
};

IMPLEMENT_MODULE(FHVPSystemsEditorModule, HVPSystemsEditor)
