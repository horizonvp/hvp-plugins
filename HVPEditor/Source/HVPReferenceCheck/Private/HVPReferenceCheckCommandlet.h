#pragma once

#include "Commandlets/Commandlet.h"
#include "HVPReferenceCheckCommandlet.generated.h"

/**
 * Headless sweep of every project plugin's content for references it may not have.
 * Exits non-zero when anything is found, so a git hook or CI step can fail on it.
 *
 * Run (editor closed):
 *   UnrealEditor-Cmd.exe <project> -run=HVPReferenceCheck -nohmd [-Plugins=HVPStereoButton,Foo]
 */
UCLASS()
class UHVPReferenceCheckCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UHVPReferenceCheckCommandlet();
	virtual int32 Main(const FString& Params) override;
};
