#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Templates/SharedPointer.h"

class SNotificationItem;
class FHVPUatOutputCapture;

DECLARE_LOG_CATEGORY_EXTERN(LogHVPPackageInstall, Log, All);

enum class EHVPInstallStepState : uint8
{
	Pending,
	Running,
	Succeeded,
	Failed,
	Canceled,
	/** Not part of this run (the build, when packaging does not compile). */
	Skipped,
};

/** One step of a run, as the window shows it. */
struct FHVPInstallStep
{
	FText Name;
	EHVPInstallStepState State = EHVPInstallStepState::Pending;
	/** What it is doing or did: "Compiling 120/450", "617 of 835 packages cooked"... */
	FString Detail;
	double StartSeconds = 0.0;
	double EndSeconds = 0.0;
};

enum class EHVPInstallOutcome : uint8 { None, Running, Succeeded, Failed, Canceled };

/** Everything the window shows about the current or last run. */
struct FHVPInstallStatus
{
	enum EStep : int32 { Build, Cook, Package, Install, Done, Num };

	TArray<FHVPInstallStep> Steps;
	EHVPInstallOutcome Outcome = EHVPInstallOutcome::None;
	FText Headline;
	/** The final report: where the build went, or what went wrong. */
	FString Report;
	FString Configuration;
	FString ArchiveDirectory;
	double StartSeconds = 0.0;
	double EndSeconds = 0.0;
};

/**
 * Package and Install to Headset, start to finish: save, package for Android ASTC through UAT (the
 * same BuildCookRun the Platforms menu's Package Project runs, with its progress toast), run the
 * install script that package produced for the chosen configuration, and keep a step-by-step
 * status for the Package and Install window. One run at a time; everything here is game thread.
 */
class FHVPPackageInstaller : public TSharedFromThis<FHVPPackageInstaller>
{
public:
	FHVPPackageInstaller();
	~FHVPPackageInstaller();

	/** Whether a run can start now; when not, OutWhyNot says why (shown as the menu's tooltip). */
	bool CanStart(FText* OutWhyNot = nullptr) const;
	void Start();

	const FHVPInstallStatus& GetStatus() const { return Status; }

	/** A run started or finished: the window comes forward. */
	FSimpleMulticastDelegate OnRunStarted;
	FSimpleMulticastDelegate OnRunFinished;

private:
	enum class EStage : uint8 { Idle, Packaging, Installing };

	/** What one run packages, decided when it starts. */
	struct FRun
	{
		FString ExecutableName;  // TextWork, or TextWork-Android-Shipping outside Development
		FString Architecture;    // arm64
		FDateTime StartedUtc;
		bool bBuilds = false;
	};

	void ResetStatus();
	/** Applies UAT's phase markers and progress, collected off-thread, to the steps. */
	bool Tick(float DeltaTime);
	void HandleUatLine(const FString& Line);
	void BeginStep(int32 Step, const FString& Detail = FString());
	void EndStep(int32 Step, EHVPInstallStepState State, const FString& Detail = FString());
	int32 GetRunningStep() const;

	void OnPackaged(const FString& Result);
	void Install();
	void OnInstalled(int32 ReturnCode, const FString& Output);
	void Finish(EHVPInstallOutcome Outcome, const FText& Headline, const FString& Report);

	/** The install script UAT wrote for this run's configuration, newer than the run; empty if none. */
	FString FindInstallScript(FString& OutLooked) const;

	/** ANDROID_HOME and JAVA_HOME for the editor process while UAT starts (it inherits them). */
	void ApplyEnvironment();
	void RestoreEnvironment();

	EStage Stage = EStage::Idle;
	FRun Run;
	FHVPInstallStatus Status;
	TUniquePtr<FHVPUatOutputCapture> Capture;
	FTSTicker::FDelegateHandle TickHandle;
	TArray<TPair<FString, FString>> SavedEnvironment;
	TWeakPtr<SNotificationItem> InstallToast;
};
