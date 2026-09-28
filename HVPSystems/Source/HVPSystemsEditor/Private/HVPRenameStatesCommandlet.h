#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HVPRenameStatesCommandlet.generated.h"

/**
 * Renames (or deletes) gameflow state strings everywhere assets key on them.
 *
 * States are plain strings, so a rename has to reach every place one is spelled out: string
 * properties on Blueprint class defaults and component templates, on actors and components in
 * maps, string/name literal pins and Switch-on-String cases in Blueprint graphs, and DataTable
 * row names (the VO lines table is keyed by state). This walks every Blueprint, World and
 * DataTable under the given content paths and rewrites exact, case-insensitive matches inside
 * strings, string arrays, string sets and structs, then compiles and saves only what changed.
 *
 * Run (editor closed):
 *   UnrealEditor-Cmd.exe <project> -run=HVPRenameStates -nohmd -xrtrackingsystem=None
 *       -Paths=/Game/LenovoAILibrary
 *       -Renames="A/Old=A/New;A/Gone=;A/Other=A/Else"
 *       [-DryRun]
 *
 * An empty right-hand side deletes: the element is removed from arrays and sets, and a plain
 * string property is emptied with a warning. Map keys are never rewritten (reported instead).
 * -DryRun logs every change it would make and saves nothing.
 */
UCLASS()
class UHVPRenameStatesCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
