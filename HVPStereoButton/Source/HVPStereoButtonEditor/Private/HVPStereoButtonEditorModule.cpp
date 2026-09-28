#include "StereoButtonBaseDetails.h"
#include "StereoButtonBase.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

class FHVPStereoButtonEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyModule.RegisterCustomClassLayout(
			AStereoButtonBase::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FStereoButtonBaseDetails::MakeInstance));

		PropertyModule.NotifyCustomizationModuleChanged();
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyModule =
				FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

			PropertyModule.UnregisterCustomClassLayout(
				AStereoButtonBase::StaticClass()->GetFName());

			PropertyModule.NotifyCustomizationModuleChanged();
		}
	}
};

IMPLEMENT_MODULE(FHVPStereoButtonEditorModule, HVPStereoButtonEditor)
