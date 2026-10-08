# Package and Install to Headset

**Platforms > Package and Install to Headset** packages the project for Android (ASTC), installs it
on the connected headset, and follows the run in the **Package and Install** window. The entry sits
just above Package Project. The console command `HVP.PackageAndInstall` does the same.

The entry is greyed out unless the Platforms menu is set to package for Android with the ASTC
flavour: **Package Project > Android > ASTC**. Its tooltip says why when it's greyed out.

## The window

When a run starts, the window opens as a small floating window centred on the editor. When the
run ends, it comes back to the front, centred again, and flashes. If you've docked it, the tab
flashes instead. It's a regular editor tab, so it can be docked anywhere and reopened from
**Window > Tools > Package and Install**.

On the left is a timeline of the steps: **Build, Cook, Package, Install, Done**. Each step has a
circle:

| Circle | Meaning |
|---|---|
| Hollow ring | Waiting, or skipped (the build is skipped when packaging doesn't compile) |
| Blue, breathing | In progress |
| Green with a check | Done |
| Red with a cross | Failed here |
| Yellow with a cross | Canceled here |

The line down to the next step turns green once the step is behind the run. To the right of each
circle are the step's name, its state and time, and what it's doing:

- **Build:** UBT's compile progress.
- **Cook:** packages cooked out of the total.
- **Package:** staging, building the APK, archiving.
- **Install:** the result, or the likely cause of a failure.

At the bottom is the overall result, coloured by outcome, with the total time. Under it is the
report: where the package went, or the errors. The text is selectable, so it can be copied. The
buttons open the Output Log (the full UAT output is under `UATHelper`), open the package folder,
or run again.

## What it does

1. Saves unsaved work, as Package Project does.
2. Packages with the same BuildCookRun as Package Project, with the editor's own progress toast,
   Cancel button and Output Log. It uses the Package Project settings: configuration (the menu's,
   or the project's, and Shipping for distribution), build target, pak, IoStore, compression and
   full rebuild. The steps follow UAT's own phase markers (`BUILD`, `COOK`, `STAGE`, `PACKAGE`
   and `ARCHIVE`, each `COMMAND STARTED` / `COMPLETED`).
3. Runs the install script that this package produced for that configuration. UAT names it after
   the APK, so each configuration has its own:

   | Configuration | Script |
   |---|---|
   | Development | `Install_<Target>-arm64.bat` |
   | DebugGame, Test, Shipping | `Install_<Target>-Android-<Config>-arm64.bat` |

   It looks in the archive folder (`<archive>/Android_ASTC/`), then in `Binaries/Android`. It only
   accepts a script written during this run, so it never installs an older build.

   The script's input comes from `NUL`, because its error path ends in `pause` and would otherwise
   wait for a key forever. Success is judged by the script's own words ("Installation successful",
   with no "There was an error"), not its exit code: the success path exits with whatever its last
   cleanup command returned, which is 255 when there was nothing to delete.

## Settings

**Editor Preferences > Plugins > HVP Package and Install**. These are saved per user, because the
paths belong to the machine:

| Setting | Use |
|---|---|
| Android Home | The Android SDK, given to packaging and to the install script as `ANDROID_HOME`. Set it when the machine-wide `ANDROID_HOME` points somewhere else; the install script trusts it over everything except an `adb` already on the PATH. |
| Java Home | A JDK for packaging (Gradle needs 17 or later), as `JAVA_HOME`. |
| Archive Directory | Where the package goes. Empty: Package Project's last folder, else `<Project>/Packaged`. |
| Device Serial | Install to this adb device. Empty: the only one connected. |

The two environment overrides apply only to the processes this tool starts. The editor's own
environment is restored as soon as UAT has launched.
