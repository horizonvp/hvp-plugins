#include "HVPPackageInstaller.h"

#include "Async/Async.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HVPPackageInstallSettings.h"
#include "IDesktopPlatform.h"
#include "IUATHelperModule.h"
#include "Internationalization/Regex.h"
#include "Misc/App.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "PlatformInfo.h"
#include "Settings/PlatformsMenuSettings.h"
#include "Settings/ProjectPackagingSettings.h"
#include "Styling/AppStyle.h"
#include "UnrealEdMisc.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "HVPPackageInstall"

DEFINE_LOG_CATEGORY(LogHVPPackageInstall);

namespace HVPPackageInstall
{
	const FName Android(TEXT("Android"));
	const FName AndroidASTC(TEXT("Android_ASTC"));
	/** How many error lines the report lists before pointing at the log. */
	constexpr int32 MaxListedErrors = 15;

	FString FormatSeconds(double Seconds)
	{
		const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
	}
}

/**
 * Collects the UAT output lines the steps care about while a package runs. UAT's output reaches the
 * log through the UATHelper category from the process's own thread; the installer drains the lines
 * on the game thread.
 */
class FHVPUatOutputCapture : public FOutputDevice
{
public:
	FHVPUatOutputCapture() { GLog->AddOutputDevice(this); }
	virtual ~FHVPUatOutputCapture() override { GLog->RemoveOutputDevice(this); }

	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		static const FName UATHelper(TEXT("UATHelper"));
		if (Category != UATHelper)
		{
			return;
		}
		// "Packaging (Android (ASTC)): <line>": keep the line.
		FString Line(Message);
		const int32 Colon = Line.Find(TEXT("): "));
		if (Colon != INDEX_NONE)
		{
			Line.RightChopInline(Colon + 3);
		}
		Line.TrimStartAndEndInline();

		FScopeLock Lock(&Guard);
		if (Verbosity <= ELogVerbosity::Error || Line.Contains(TEXT("ERROR:")) || Line.Contains(TEXT("Error:")))
		{
			++ErrorCount;
			if (Errors.Num() < HVPPackageInstall::MaxListedErrors)
			{
				Errors.Add(Line);
			}
		}
		if (Line.Contains(TEXT("COMMAND STARTED")) || Line.Contains(TEXT("COMMAND COMPLETED"))
			|| Line.StartsWith(TEXT("[")) || Line.Contains(TEXT("Cooked packages")))
		{
			Pending.Add(MoveTemp(Line));
		}
	}

	TArray<FString> Drain()
	{
		FScopeLock Lock(&Guard);
		return MoveTemp(Pending);
	}

	FString DescribeErrors() const
	{
		FScopeLock Lock(&Guard);
		FString Text = FString::Join(Errors, TEXT("\n"));
		if (ErrorCount > Errors.Num())
		{
			Text += FString::Printf(TEXT("\n... and %d more."), ErrorCount - Errors.Num());
		}
		return Text;
	}

private:
	mutable FCriticalSection Guard;
	TArray<FString> Pending;
	TArray<FString> Errors;
	int32 ErrorCount = 0;
};

FHVPPackageInstaller::FHVPPackageInstaller()
{
	ResetStatus();
}

FHVPPackageInstaller::~FHVPPackageInstaller()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	RestoreEnvironment();
}

void FHVPPackageInstaller::ResetStatus()
{
	Status = FHVPInstallStatus();
	const FText Names[FHVPInstallStatus::Num] = {
		LOCTEXT("StepBuild", "Build"), LOCTEXT("StepCook", "Cook"), LOCTEXT("StepPackage", "Package"),
		LOCTEXT("StepInstall", "Install"), LOCTEXT("StepDone", "Done") };
	for (const FText& Name : Names)
	{
		Status.Steps.AddDefaulted_GetRef().Name = Name;
	}
}

bool FHVPPackageInstaller::CanStart(FText* OutWhyNot) const
{
	auto No = [OutWhyNot](const FText& Why)
	{
		if (OutWhyNot)
		{
			*OutWhyNot = Why;
		}
		return false;
	};
	if (Stage != EStage::Idle)
	{
		return No(LOCTEXT("Busy", "Already packaging or installing."));
	}
	const UPlatformsMenuSettings* Menu = GetDefault<UPlatformsMenuSettings>();
	if (Menu->PackagePlatform != HVPPackageInstall::Android || Menu->GetTargetFlavorForPlatform(HVPPackageInstall::Android) != HVPPackageInstall::AndroidASTC)
	{
		return No(LOCTEXT("NotASTC", "Only for Android (ASTC). Choose it under Package Project > Android > ASTC."));
	}
	if (!FPaths::IsProjectFilePathSet())
	{
		return No(LOCTEXT("NoProject", "No project is open."));
	}
	if (OutWhyNot)
	{
		*OutWhyNot = LOCTEXT("Ready", "Package the project for Android (ASTC) with the Package Project settings, install it on the connected headset, and follow it in the Package and Install window.");
	}
	return true;
}

// --- Packaging -------------------------------------------------------------------------------

void FHVPPackageInstaller::Start()
{
	if (!CanStart())
	{
		return;
	}
	// As Package Project does: unsaved work would not be in the package.
	if (!FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave*/ true, /*bSaveMapPackages*/ true, /*bSaveContentPackages*/ true))
	{
		return;
	}

	const UPlatformsMenuSettings* Menu = GetDefault<UPlatformsMenuSettings>();
	const UProjectPackagingSettings* Packaging = GetDefault<UProjectPackagingSettings>();
	const UHVPPackageInstallSettings* Settings = GetDefault<UHVPPackageInstallSettings>();
	const PlatformInfo::FTargetPlatformInfo* Flavor = PlatformInfo::FindPlatformInfo(HVPPackageInstall::AndroidASTC);
	const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

	// The configuration, as Package Project picks it: the menu's choice, else the project's, and
	// Shipping whenever packaging for distribution.
	EProjectPackagingBuildConfigurations BuildConfig = Menu->GetPackageBuildConfiguration();
	if (BuildConfig == EProjectPackagingBuildConfigurations::PPBC_MAX)
	{
		BuildConfig = Packaging->BuildConfiguration;
	}
	if (Packaging->ForDistribution)
	{
		BuildConfig = EProjectPackagingBuildConfigurations::PPBC_Shipping;
	}
	const EBuildConfiguration Configuration = UProjectPackagingSettings::ConfigurationInfo[(int32)BuildConfig].Configuration;

	// The target, and from it the executable name the APK and its install script are named after:
	// plain in Development, decorated with platform and configuration otherwise.
	bool bIsProjectTarget = false;
	const FTargetInfo* Target = Menu->GetBuildTargetInfo(Menu->PackageBuildTarget, bIsProjectTarget);
	const FString TargetName = Target && bIsProjectTarget ? Target->Name : FString(FApp::GetProjectName());

	ResetStatus();
	Run = FRun();
	Status.Configuration = LexToString(Configuration);
	Run.ExecutableName = Configuration == EBuildConfiguration::Development
		? TargetName : FString::Printf(TEXT("%s-Android-%s"), *TargetName, *Status.Configuration);
	Run.Architecture = Menu->GetArchitectureForPlatform(HVPPackageInstall::Android);
	if (Run.Architecture.IsEmpty())
	{
		Run.Architecture = TEXT("arm64");
	}
	Status.ArchiveDirectory = FPaths::ConvertRelativePathToFull(
		!Settings->ArchiveDirectory.Path.IsEmpty() ? Settings->ArchiveDirectory.Path
		: !Menu->StagingDirectory.Path.IsEmpty() ? Menu->StagingDirectory.Path
		: FPaths::ProjectDir() / TEXT("Packaged"));
	Run.StartedUtc = FDateTime::UtcNow();
	Run.bBuilds = Packaging->Build != EProjectPackagingBuild::Never && FPaths::DirectoryExists(FPaths::GameSourceDir());

	// The BuildCookRun Package Project runs, for this one platform flavour.
	FString Params = FString::Printf(TEXT("-ScriptsForProject=\"%s\" BuildCookRun -project=\"%s\" -nop4 -utf8output -nocompileeditor -skipbuildeditor -cook -stage -archive -package"),
		*ProjectPath, *ProjectPath);
	Params += FString::Printf(TEXT(" -unrealexe=\"%s\""), *FUnrealEdMisc::Get().GetExecutableForCommandlets());
	Params += TEXT(" -platform=Android");
	if (Flavor && !Flavor->UATCommandLine.IsEmpty())
	{
		Params += TEXT(" ") + Flavor->UATCommandLine;  // -cookflavor=ASTC
	}
	if (Target && bIsProjectTarget)
	{
		Params += FString::Printf(TEXT(" -target=%s"), *Target->Name);
	}
	if (Run.bBuilds)
	{
		Params += TEXT(" -build");
	}
	if (Packaging->FullRebuild)
	{
		Params += TEXT(" -clean");
	}
	if (Packaging->UsePakFile || Packaging->bUseIoStore)
	{
		Params += TEXT(" -pak");
		Params += Packaging->bUseIoStore ? TEXT(" -iostore") : TEXT("");
		Params += Packaging->bCompressed ? TEXT(" -compressed") : TEXT("");
	}
	if (Packaging->bSkipEditorContent)
	{
		Params += TEXT(" -SkipCookingEditorContent");
	}
	if (Packaging->ForDistribution)
	{
		Params += TEXT(" -distribution");
	}
	if (FApp::IsEngineInstalled())
	{
		Params += TEXT(" -installed");
	}
	Params += FString::Printf(TEXT(" -clientconfig=%s"), *Status.Configuration);
	if (Configuration == EBuildConfiguration::Shipping && !Packaging->IncludeDebugFiles)
	{
		Params += TEXT(" -nodebuginfo");
	}
	Params += FString::Printf(TEXT(" -archivedirectory=\"%s\""), *Status.ArchiveDirectory);
	Params += TEXT(" -SkipCookingErrorSummary -JsonStdOut");

	UE_LOG(LogHVPPackageInstall, Log, TEXT("Packaging %s for Android ASTC into %s"), *Status.Configuration, *Status.ArchiveDirectory);
	Stage = EStage::Packaging;
	Status.Outcome = EHVPInstallOutcome::Running;
	Status.StartSeconds = FPlatformTime::Seconds();
	Status.Headline = FText::Format(LOCTEXT("RunningHeadline", "Packaging {0} for Android ASTC..."), FText::FromString(Status.Configuration));
	if (!Run.bBuilds)
	{
		EndStep(FHVPInstallStatus::Build, EHVPInstallStepState::Skipped, TEXT("Packaging does not compile (Project Settings > Packaging > Build)."));
	}
	Capture = MakeUnique<FHVPUatOutputCapture>();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FHVPPackageInstaller::Tick), 0.1f);
	OnRunStarted.Broadcast();

	// UAT inherits the editor's environment when it starts, so the overrides are only in place
	// for that moment. "Packaging" as the short task name keeps the editor's cook-error handling.
	TWeakPtr<FHVPPackageInstaller> WeakThis = AsShared();
	ApplyEnvironment();
	IUATHelperModule::Get().CreateUatTask(Params, Flavor ? Flavor->DisplayName : FText::FromString(TEXT("Android (ASTC)")),
		LOCTEXT("TaskName", "Packaging for the headset"), LOCTEXT("TaskShortName", "Packaging"),
		FAppStyle::GetBrush(TEXT("MainFrame.PackageProject")), nullptr,
		[WeakThis](FString Result, double)
		{
			// Called from the process's thread; everything after this belongs on the game thread.
			AsyncTask(ENamedThreads::GameThread, [WeakThis, Result]()
			{
				if (TSharedPtr<FHVPPackageInstaller> This = WeakThis.Pin())
				{
					This->OnPackaged(Result);
				}
			});
		});
	RestoreEnvironment();
}

bool FHVPPackageInstaller::Tick(float)
{
	if (Capture)
	{
		for (const FString& Line : Capture->Drain())
		{
			HandleUatLine(Line);
		}
	}
	return true;
}

void FHVPPackageInstaller::HandleUatLine(const FString& Line)
{
	// UAT's phase markers: "********** COOK COMMAND STARTED **********" and so on.
	auto Marker = [&Line](const TCHAR* Phase, const TCHAR* Edge)
	{
		return Line.Contains(FString::Printf(TEXT("%s COMMAND %s"), Phase, Edge));
	};
	if (Marker(TEXT("BUILD"), TEXT("STARTED")))
	{
		BeginStep(FHVPInstallStatus::Build, TEXT("Compiling for Android..."));
	}
	else if (Marker(TEXT("BUILD"), TEXT("COMPLETED")))
	{
		EndStep(FHVPInstallStatus::Build, EHVPInstallStepState::Succeeded, TEXT("Compiled."));
	}
	else if (Marker(TEXT("COOK"), TEXT("STARTED")))
	{
		BeginStep(FHVPInstallStatus::Cook, TEXT("Starting the cooker..."));
	}
	else if (Marker(TEXT("COOK"), TEXT("COMPLETED")))
	{
		EndStep(FHVPInstallStatus::Cook, EHVPInstallStepState::Succeeded);
	}
	else if (Marker(TEXT("STAGE"), TEXT("STARTED")))
	{
		BeginStep(FHVPInstallStatus::Package, TEXT("Staging the cooked content..."));
	}
	else if (Marker(TEXT("PACKAGE"), TEXT("STARTED")))
	{
		BeginStep(FHVPInstallStatus::Package, TEXT("Building the APK..."));
	}
	else if (Marker(TEXT("ARCHIVE"), TEXT("STARTED")))
	{
		BeginStep(FHVPInstallStatus::Package, TEXT("Archiving..."));
	}
	else if (Marker(TEXT("ARCHIVE"), TEXT("COMPLETED")))
	{
		EndStep(FHVPInstallStatus::Package, EHVPInstallStepState::Succeeded,
			FString::Printf(TEXT("Archived to %s"), *(Status.ArchiveDirectory / TEXT("Android_ASTC"))));
	}
	else if (Line.Contains(TEXT("Cooked packages")))
	{
		// "LogCook: Display: Cooked packages 32 Packages Remain 803 Total 835"
		static const FRegexPattern Pattern(TEXT("Cooked packages (\\d+) Packages Remain \\d+ Total (\\d+)"));
		FRegexMatcher Match(Pattern, Line);
		if (Match.FindNext())
		{
			Status.Steps[FHVPInstallStatus::Cook].Detail = FString::Printf(TEXT("%s of %s packages cooked"), *Match.GetCaptureGroup(1), *Match.GetCaptureGroup(2));
		}
	}
	else if (Line.StartsWith(TEXT("[")))
	{
		// "[12/450] Compile Module.Core.cpp", or the packager's "[3/13] Adding ... to OBB".
		const int32 Running = GetRunningStep();
		if (Running == FHVPInstallStatus::Build || Running == FHVPInstallStatus::Package)
		{
			Status.Steps[Running].Detail = Line.Left(160);
		}
	}
}

int32 FHVPPackageInstaller::GetRunningStep() const
{
	return Status.Steps.IndexOfByPredicate([](const FHVPInstallStep& Step) { return Step.State == EHVPInstallStepState::Running; });
}

void FHVPPackageInstaller::BeginStep(int32 Step, const FString& Detail)
{
	// Starting a step settles any earlier one still marked running, or never reached.
	for (int32 Earlier = 0; Earlier < Step; ++Earlier)
	{
		const EHVPInstallStepState Before = Status.Steps[Earlier].State;
		if (Before == EHVPInstallStepState::Running)
		{
			EndStep(Earlier, EHVPInstallStepState::Succeeded);
		}
		else if (Before == EHVPInstallStepState::Pending)
		{
			EndStep(Earlier, EHVPInstallStepState::Skipped);
		}
	}
	FHVPInstallStep& This = Status.Steps[Step];
	if (This.State != EHVPInstallStepState::Running)
	{
		This.State = EHVPInstallStepState::Running;
		This.StartSeconds = FPlatformTime::Seconds();
	}
	This.Detail = Detail;
}

void FHVPPackageInstaller::EndStep(int32 Step, EHVPInstallStepState State, const FString& Detail)
{
	FHVPInstallStep& This = Status.Steps[Step];
	if (This.StartSeconds == 0.0)
	{
		This.StartSeconds = FPlatformTime::Seconds();
	}
	This.State = State;
	This.EndSeconds = FPlatformTime::Seconds();
	if (!Detail.IsEmpty())
	{
		This.Detail = Detail;
	}
}

void FHVPPackageInstaller::OnPackaged(const FString& Result)
{
	Tick(0.f);  // the last markers before the result
	const FString Errors = Capture ? Capture->DescribeErrors() : FString();
	Capture.Reset();

	const int32 Running = GetRunningStep();
	const int32 Stopped = Running != INDEX_NONE ? Running : FHVPInstallStatus::Build;
	if (Result == TEXT("Canceled"))
	{
		EndStep(Stopped, EHVPInstallStepState::Canceled, TEXT("Canceled."));
		Finish(EHVPInstallOutcome::Canceled, LOCTEXT("CanceledHeadline", "Canceled"), TEXT("Packaging was canceled; nothing was installed."));
		return;
	}
	if (Result != TEXT("Completed"))
	{
		EndStep(Stopped, EHVPInstallStepState::Failed, FString::Printf(TEXT("Failed (%s)."), *Result));
		Finish(EHVPInstallOutcome::Failed,
			FText::Format(LOCTEXT("PackageFailedHeadline", "{0} failed"), Status.Steps[Stopped].Name),
			FString::Printf(TEXT("Packaging %s for Android ASTC failed; nothing was installed.\n\n%s\n\nThe full log is in the Output Log (UATHelper)."),
				*Status.Configuration, Errors.IsEmpty() ? TEXT("UAT reported no error lines.") : *Errors));
		return;
	}
	// Anything UAT did not announce is done by now.
	for (int32 Step = FHVPInstallStatus::Build; Step <= FHVPInstallStatus::Package; ++Step)
	{
		const EHVPInstallStepState State = Status.Steps[Step].State;
		if (State == EHVPInstallStepState::Running || State == EHVPInstallStepState::Pending)
		{
			EndStep(Step, State == EHVPInstallStepState::Running ? EHVPInstallStepState::Succeeded : EHVPInstallStepState::Skipped);
		}
	}
	Install();
}

// --- Installing ------------------------------------------------------------------------------

FString FHVPPackageInstaller::FindInstallScript(FString& OutLooked) const
{
	// UAT names the script after the APK (Install_<APK name>.bat) and archives it with the APK in a
	// folder named after the flavour; the copy it builds from stays in Binaries/Android.
	const FString Script = FString::Printf(TEXT("Install_%s-%s.bat"), *Run.ExecutableName, *Run.Architecture);
	const FString Candidates[] = {
		Status.ArchiveDirectory / TEXT("Android_ASTC") / Script,
		Status.ArchiveDirectory / TEXT("Android") / Script,
		FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Binaries/Android") / Script),
	};
	// A script from an earlier package would install an earlier build: it must be from this run.
	const FDateTime Oldest = Run.StartedUtc - FTimespan::FromSeconds(5.0);
	for (const FString& Candidate : Candidates)
	{
		OutLooked += TEXT("\n  ") + Candidate;
		if (IFileManager::Get().FileExists(*Candidate) && IFileManager::Get().GetTimeStamp(*Candidate) >= Oldest)
		{
			return Candidate;
		}
	}
	return FString();
}

void FHVPPackageInstaller::Install()
{
	FString Looked;
	const FString Script = FindInstallScript(Looked);
	if (Script.IsEmpty())
	{
		EndStep(FHVPInstallStatus::Install, EHVPInstallStepState::Failed, TEXT("No install script from this run."));
		Finish(EHVPInstallOutcome::Failed, LOCTEXT("NoScriptHeadline", "Install script not found"),
			FString::Printf(TEXT("Packaging %s succeeded, but no install script from this run was found. Looked for:%s"), *Status.Configuration, *Looked));
		return;
	}
	Stage = EStage::Installing;
	const UHVPPackageInstallSettings* Settings = GetDefault<UHVPPackageInstallSettings>();
	BeginStep(FHVPInstallStatus::Install, FString::Printf(TEXT("%s: uninstalling the old build, installing, granting permissions%s..."),
		*FPaths::GetCleanFilename(Script), Settings->DeviceSerial.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" on %s"), *Settings->DeviceSerial)));
	Status.Headline = LOCTEXT("InstallingHeadline", "Installing on the headset...");
	UE_LOG(LogHVPPackageInstall, Log, TEXT("Installing with %s"), *Script);

	// Through cmd, from the script's folder (it names the APK relatively). Input comes from NUL:
	// the script's error path ends in pause, which would otherwise wait for a key forever.
	FString Inner;
	if (!Settings->AndroidHome.Path.IsEmpty())
	{
		Inner += FString::Printf(TEXT("set \"ANDROID_HOME=%s\" && "), *FPaths::ConvertRelativePathToFull(Settings->AndroidHome.Path).Replace(TEXT("/"), TEXT("\\")));
	}
	Inner += FString::Printf(TEXT("call \"%s\""), *Script.Replace(TEXT("/"), TEXT("\\")));
	if (!Settings->DeviceSerial.IsEmpty())
	{
		Inner += TEXT(" ") + Settings->DeviceSerial;
	}
	Inner += TEXT(" < NUL");
	const FString Params = FString::Printf(TEXT("/d /s /c \"%s\""), *Inner);
	const FString WorkingDirectory = FPaths::GetPath(Script);

	TWeakPtr<FHVPPackageInstaller> WeakThis = AsShared();
	Async(EAsyncExecution::Thread, [WeakThis, Params, WorkingDirectory]()
	{
		int32 ReturnCode = -1;
		FString StdOut, StdErr;
		FPlatformProcess::ExecProcess(TEXT("cmd.exe"), *Params, &ReturnCode, &StdOut, &StdErr, *WorkingDirectory);
		const FString Output = StdErr.IsEmpty() ? StdOut : StdOut + TEXT("\n") + StdErr;
		AsyncTask(ENamedThreads::GameThread, [WeakThis, ReturnCode, Output]()
		{
			if (TSharedPtr<FHVPPackageInstaller> This = WeakThis.Pin())
			{
				This->OnInstalled(ReturnCode, Output);
			}
		});
	});
}

void FHVPPackageInstaller::OnInstalled(int32 ReturnCode, const FString& Output)
{
	UE_LOG(LogHVPPackageInstall, Log, TEXT("Install script exited with %d:\n%s"), ReturnCode, *Output);

	// The script's own verdict, not its exit code: the success path ends "Installation successful"
	// but exits with whatever its last cleanup command returned (255 when there was nothing to
	// delete), and the error path says "There was an error" and exits 0 after the pause.
	const bool bSucceeded = Output.Contains(TEXT("Installation successful")) && !Output.Contains(TEXT("There was an error"));
	if (bSucceeded)
	{
		EndStep(FHVPInstallStatus::Install, EHVPInstallStepState::Succeeded, TEXT("Installed, with storage permissions granted."));
		Finish(EHVPInstallOutcome::Succeeded, LOCTEXT("DoneHeadline", "Installed on the headset"),
			FString::Printf(TEXT("%s, Android ASTC, in %s.\nThe package is in %s."), *Status.Configuration,
				*HVPPackageInstall::FormatSeconds(FPlatformTime::Seconds() - Status.StartSeconds), *(Status.ArchiveDirectory / TEXT("Android_ASTC"))));
		return;
	}
	// The likely cause first, then the tail of the script's output, where adb says what went wrong.
	FString Cause = TEXT("The install script reported an error.");
	if (Output.Contains(TEXT("no devices/emulators found")))
	{
		Cause = TEXT("No headset found: is it plugged in, awake, and allowing USB debugging for this PC?");
	}
	else if (Output.Contains(TEXT("more than one device")))
	{
		Cause = TEXT("More than one device on adb: set a Device Serial in Editor Preferences > HVP Package and Install.");
	}
	else if (Output.Contains(TEXT("unauthorized")))
	{
		Cause = TEXT("The headset has not authorised this PC: accept the USB debugging prompt in the headset.");
	}
	else if (Output.Contains(TEXT("Unable to locate local Android SDK")))
	{
		Cause = TEXT("The install script could not find the Android SDK: set Android Home in Editor Preferences > HVP Package and Install.");
	}
	TArray<FString> Lines;
	Output.ParseIntoArrayLines(Lines);
	const int32 First = FMath::Max(0, Lines.Num() - 20);
	const FString Tail = FString::Join(TArray<FString>(Lines.GetData() + First, Lines.Num() - First), TEXT("\n"));
	EndStep(FHVPInstallStatus::Install, EHVPInstallStepState::Failed, Cause);
	Finish(EHVPInstallOutcome::Failed, LOCTEXT("InstallFailedHeadline", "Install failed"),
		FString::Printf(TEXT("Packaging %s succeeded, but installing on the headset did not.\n%s\n\n%s"), *Status.Configuration, *Cause, *Tail));
}

void FHVPPackageInstaller::Finish(EHVPInstallOutcome Outcome, const FText& Headline, const FString& Report)
{
	Stage = EStage::Idle;
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();

	Status.Outcome = Outcome;
	Status.Headline = Headline;
	Status.Report = Report;
	Status.EndSeconds = FPlatformTime::Seconds();
	const FString Total = FString::Printf(TEXT("Total %s"), *HVPPackageInstall::FormatSeconds(Status.EndSeconds - Status.StartSeconds));
	EndStep(FHVPInstallStatus::Done,
		Outcome == EHVPInstallOutcome::Succeeded ? EHVPInstallStepState::Succeeded
		: Outcome == EHVPInstallOutcome::Canceled ? EHVPInstallStepState::Canceled : EHVPInstallStepState::Failed,
		Total);
	UE_LOG(LogHVPPackageInstall, Log, TEXT("%s: %s"), *Headline.ToString(), *Report);

	// A notification as well, for whoever has the window tucked away.
	FNotificationInfo Info(Headline);
	Info.ExpireDuration = 5.f;
	if (TSharedPtr<SNotificationItem> Toast = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Toast->SetCompletionState(Outcome == EHVPInstallOutcome::Succeeded ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
	OnRunFinished.Broadcast();
}

// --- Environment -----------------------------------------------------------------------------

void FHVPPackageInstaller::ApplyEnvironment()
{
	const UHVPPackageInstallSettings* Settings = GetDefault<UHVPPackageInstallSettings>();
	const TPair<const TCHAR*, FString> Overrides[] = {
		{ TEXT("ANDROID_HOME"), Settings->AndroidHome.Path },
		{ TEXT("JAVA_HOME"), Settings->JavaHome.Path },
	};
	for (const TPair<const TCHAR*, FString>& Override : Overrides)
	{
		if (!Override.Value.IsEmpty())
		{
			SavedEnvironment.Emplace(Override.Key, FPlatformMisc::GetEnvironmentVariable(Override.Key));
			FPlatformMisc::SetEnvironmentVar(Override.Key, *FPaths::ConvertRelativePathToFull(Override.Value).Replace(TEXT("/"), TEXT("\\")));
		}
	}
}

void FHVPPackageInstaller::RestoreEnvironment()
{
	for (const TPair<FString, FString>& Saved : SavedEnvironment)
	{
		FPlatformMisc::SetEnvironmentVar(*Saved.Key, *Saved.Value);
	}
	SavedEnvironment.Reset();
}

#undef LOCTEXT_NAMESPACE
