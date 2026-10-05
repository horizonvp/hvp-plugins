# HVP Naming Conventions

Editor-only plugin that keeps assets named and filed correctly as you work: it appends the
project's type suffix (`_T`, `_SM`, `_BP`, …) to new assets, and warns when something lands
in the wrong folder. It uses the same rules as `Tools/Conventions`, so the editor and the
git hooks agree.

It reduces friction. It does **not** replace the git hooks — those still run on commit and
push, and they're the only thing that catches a machine without the plugin installed.

---

# How it works

## The flow

```
asset created / renamed / moved   folder created
              │                         │
              ▼                         ▼
      registry hooks  ─────────────────────────►  filter
                                                    │  only /Game, not exempt,
                                                    │  not a world / redirector / _BuiltData
                                                    ▼
                                              debounce queue  (0.4s of quiet)
                                                    │
                                                    ▼
                                          rename  (one batched call)
                                                    │
                                                    ▼
                                          placement check
                                                    │
                                                    ▼
                                          sticky warning
```

Renaming happens before the placement check, because the audio rules key off the *final*
name. The debounce matters because a single FBX emits a mesh, skeleton, physics asset,
materials and textures across several frames — waiting for quiet turns that into one
batched rename instead of a race against the importer.

## Which hooks, and why

The obvious hook — `UImportSubsystem::OnAssetPostImport` — is **useless for the headline
case on UE 5.7**. Textures, meshes, materials and audio all route through Interchange by
default, and Interchange never broadcasts it, so a plugin built on it silently does nothing
when you drag in a PNG. (Details in "Why it is not built on the import hook".)

`IAssetRegistry::OnAssetAdded` is the backbone instead — it fires whatever created the
asset. Four more hooks fill the gaps:

| Hook | Catches |
| --- | --- |
| `OnAssetAdded` | any new asset, whatever created it |
| `OnAssetRenamed` | renames and moves, **including already-saved assets** |
| `OnPathAdded` | folder creation — an empty folder has no asset to inspect |
| `OnAssetPostImport` | only tags an asset as *imported*, for the scope toggles |
| `OnAssetPostRename` | Content Browser inline rename |

## Two independent rule sets

**Naming** decides the type suffix. It keys off the resulting **UClass**, never the file
extension — one FBX yields a mesh, a skeleton, a physics asset, materials and textures,
which extension logic cannot express. Ambiguity is settled by inheritance depth, so `_RT`
beats `_T` automatically with no hand-maintained ordering.

**Placement** decides whether the folder is legal — six rules, four of them mirroring
`check_conventions.py` exactly. Nothing is ever moved automatically.

The two are independent: turning naming off in `conventions.json` leaves placement warnings
running, and vice versa.

## The warning

A **sticky Slate notification, not a modal dialog** — and that's load-bearing, not
cosmetic. A modal freezes the editor, so you could not move the offending asset while it
was up, and moving it is the one action that should dismiss it. Instead the notification is
non-blocking, will not go away on its own, and retracts itself the moment the asset or
folder is somewhere valid.

## Principles

- **Never second-guess the author.** A texture you named `Foo_SM` keeps that name. Only the
  *case* of a suffix is corrected.
- **Never move anything.** A move rewrites references and can strand redirectors. The
  warning tells you; you decide.
- **Never guess.** A type with no rule is left alone rather than given a best-effort suffix.
- **Never disagree with the hook.** Shared rules are evaluated in the same path space, using
  the same config file.

## Source layout

```
Plugins/HVPNamingConventions/
├── HVPNamingConventions.uplugin
├── README.md
├── Tests/
│   ├── test_asset_naming.py                     <- naming suite, see "Testing"
│   └── test_placement.py                        <- placement suite
└── Source/HVPNamingConventions/
    ├── HVPNamingConventions.Build.cs
    └── Private/
        ├── HVPNamingConventionsModule.cpp   <- module + console commands
        ├── AssetNamingSettings.h                <- Project Settings page
        ├── AssetNamingConfig.h/.cpp             <- reads Tools/Conventions/conventions.json
        ├── AssetSuffixResolver.h/.cpp           <- UClass -> suffix
        ├── AssetNamingWatcher.h/.cpp            <- hooks, debounce queue, renaming
        ├── AssetPlacementRules.h/.cpp           <- placement rules, assets and folders
        ├── PlacementWarning.h/.cpp              <- the sticky warning + self-clearing
        └── HVPNamingLibrary.h/.cpp          <- Python / Blueprint access
```

The module is `HVPNamingConventions`; the internal classes keep `AssetNaming*` names
because that's what they do.

---

# Deployment and portability

## It never ships in a build

The plugin is editor-only, twice over:

- `HVPNamingConventions.uplugin` declares its single module as `"Type": "Editor"`, so
  UBT does not compile or stage it for Game / Client / Server targets.
- The `.uproject` entry adds `"TargetAllowList": ["Editor"]` as a second gate.

On top of that, `StartupModule` returns immediately unless `GIsEditor` is true and
`IsRunningCommandlet()` is false, so even a cooked-editor or commandlet run does nothing.
Nothing it does can reach a packaged build.

## Dropping it into another project

**Use the portable snapshot at `Tools/HVPNamingConventions/`** — it is this folder with
`Tests/`, `Binaries/` and `Intermediate/` already stripped, plus an `INSTALL.md`. That
mirrors how `Tools/Conventions/` is packaged for reuse.

Copy its `HVPNamingConventions.uplugin` and `Source/` into the target project as
`Plugins/HVPNamingConventions/`, add it to that project's `.uproject` `Plugins` array
(project plugins are auto-discovered, but the explicit entry is what carries the
`TargetAllowList`), and rebuild the editor target.

**Never copy `Binaries/` or `Intermediate/`.** Those are per-engine-version build output; a
stale DLL from a different engine will fail to load.

**This folder is the source of truth** — it is the copy that gets compiled and tested. The
snapshot is generated from it by `Tools/HVPNamingConventions/sync-from-project.ps1`; run
that after changing the plugin, or the snapshot drifts from what you shipped.

There is no project-specific state in the C++ — `BiogenCD38` appears only in comments. What
adapts automatically:

| | How |
| --- | --- |
| Project name / content root | From the new project's `.uproject`, or `ProjectNameOverride` |
| Suffix table, exempt paths, severities | From the new project's `conventions.json`, if present |
| No `conventions.json` at all | Falls back to the built-in HVP defaults |
| Disabled engine plugins | Those suffixes are skipped, with a startup log line |

The one thing that *is* project-specific: `Tests/*.py` hard-code `BiogenCD38` paths, so
they'd need their fixture paths updating to be meaningful elsewhere.

## Engine versions

Built and fully tested on **5.7**. Every API it uses was verified present and
signature-identical in **5.5** and **5.6** — the registry hooks (`OnAssetAdded`,
`OnAssetRenamed`, `OnPathAdded`, `OnPathRemoved`, `PathExists`), `IAssetTools`
(`RenameAssets`, `CreateUniqueAssetName`, `OnAssetPostRename`), `UClass::TryFindTypeSlow`,
`UImportSubsystem::OnAssetPostImport`, `IContentBrowserSingleton`, `FNotificationButtonInfo`
and `GIsSavingPackage` — as were all the modules behind the resolved class paths. It should
compile and run unchanged on 5.5 and 5.6, though only 5.7 has actually been built and tested.

**5.8 cannot be verified** — it isn't released. The design limits the blast radius of engine
churn: class paths are resolved by string at runtime, so a class that moves or disappears
costs one suffix and a log line, not a failed load or a crash. That's exactly what happened
across the 5.4→5.5 boundary, when `UUserDefinedStruct` moved from `Engine` to `CoreUObject`;
both paths are now listed, and a suffix is only reported unavailable when *no* path for it
resolves. Add an alternate path the same way if a future version moves something else.

The `.uplugin` deliberately sets no `EngineVersion`, so it isn't pinned to one release.

---

# Settings

**Project Settings → Editor → HVP Naming Conventions** (saved to
`Config/DefaultEditor.ini`, so it's shared by the team).

| Setting | Default | Notes |
| --- | --- | --- |
| `bEnabled` | on | Master switch. |
| `bDryRun` | off | Log every rename without performing it. |
| `bApplyToImportedAssets` | on | png → `_T`, fbx → `_SM`, wav → `_SW`. |
| `bApplyToCreatedAssets` | on | Add menu, duplicate, Blueprint child. |
| `bRepairDuplicateSuffix` | on | Ctrl+W leaves `Foo_T1`; this makes it `Foo1_T`. |
| `bNormaliseSuffixCase` | on | `Foo_bp` / `Foo_Bp` → `Foo_BP` instead of `Foo_bp_BP`. |
| `bFixPascalCase` | **off** | Capitalises each `_`-separated segment. Off because it rewrites names typed deliberately. |
| `PlacementWarning` | Sticky | Off / Log only / Toast / Sticky. See "Placement rules". |
| `ProjectNameOverride` | *(empty)* | The `<ProjectName>` in `Content/<ProjectName>/Modules/`. Empty = the `.uproject` file name. See below. |
| `DebounceSeconds` | 0.4 | Quiet period before a batch renames. |
| `ConventionsConfigPath` | `Tools/Conventions/conventions.json` | |

Rule severities in `conventions.json` govern both tools. `severity.asset-suffix: "off"`
keeps the renamer idle; setting any of `content-project-folder`, `content-toplevel`,
`module-structure` or `audio-placement` to `"off"` drops that rule from the in-editor
warning as well as from the hook.

`conventions.json` additions the plugin understands (both optional, both plugin-only):
`requireLibrarySubfolders` (default `true`) and `discouragedFolderNames`
(default `["Textures", "Materials"]`).

## The project name

Everything about placement hangs off one value: the `<ProjectName>` in
`Content/<ProjectName>/`. It is resolved highest-priority-first:

1. **`ProjectNameOverride`** — this plugin's Project Settings entry
2. **`projectName`** — `conventions.json`
3. **`FApp::GetProjectName()`** — the `.uproject` file name *(the default)*

Leave the override empty and you get the `.uproject` name, which is also what
`check_conventions.py` auto-detects — so the editor and the hook agree with no configuration
at all. Set it only when the content folder is deliberately named something other than the
`.uproject`.

Setting the override also recomputes the content root, deliberately ignoring any explicit
`contentRoot` in the JSON: the override is the more specific instruction, and a stale
content root would point every placement rule at the wrong folder.

**If the override disagrees with what the checker would compute, the plugin logs a warning
at startup** naming both values — because the hook will still use its own, and the two would
silently enforce different layouts. To keep them in step, set `projectName` in
`conventions.json` to the same value. The resolved root is logged on every boot:

```
LogAssetNaming: Content root: Content/BiogenCD38/  (project name 'BiogenCD38')
```

# Console commands

| Command | Effect |
| --- | --- |
| `HVPNaming.Audit` | Log every `/Game` asset lacking a recognised suffix. Read-only; only loads assets whose name already looks wrong. |
| `HVPNaming.FixSelected` | Apply suffixes to the Content Browser selection. Ignores the imported/created toggles — invoking it is explicit intent. |
| `HVPNaming.Flush` | Drain the queue now instead of waiting out the debounce. |
| `HVPNaming.RecheckPlacement` | Re-run the folder rules over anything still flagged. |

---

# Placement rules

Create a Blueprint straight into `Content/` and a sticky warning appears naming the asset,
what's wrong, and where it should go. It **will not go away until the asset is somewhere
valid** — moving it re-runs the rules automatically and the warning retracts itself.

| Rule | Catches | Also in the hook? |
| --- | --- | --- |
| `content-project-folder` | anything under `Content/` but outside `Content/BiogenCD38/` | yes |
| `content-toplevel` | loose files at the content root, or folders other than App/Library/Systems/Modules | yes |
| `module-structure` | files sitting directly in `Modules/`. Inside a module folder anything goes: `Modules/MainMenu/Thing_BP` is fine, subfolders are optional | yes |
| `audio-placement` | `VO_*` outside `Audio/VO/`, `_SW`/`_SC` outside `Audio/` | yes |
| `library-structure` | a loose asset at `Library/` root instead of in a subfolder | **no — plugin only** |
| `type-folder` | folders named `Textures` or `Materials`, which group by asset type instead of by subject — checked on assets **and on the folders themselves** | **no — plugin only** |

The first four are evaluated in **repo-path space**
(`Content/BiogenCD38/Modules/AMR/Thing_BP.uasset`) rather than package-path space, so the
comparisons are the same string operations `check_conventions.py` performs and the two can't
quietly disagree.

The last two have **no equivalent in `check_conventions.py`**, so they are caught in the
editor but *not* at commit or push. If you want them enforced on the hook too, they'd need
adding to the Python.

`type-folder` matches a whole path segment, case-insensitively — so `Library/Materials/` is
flagged but `Library/MasterMaterials/` (the location `CLAUDE.md` prescribes) is not. Its
hint is the guidance verbatim: *"Assets should be grouped into folders based on what it is.
Example, a material used for a cell A, should be in a folder Cell/A/."* Both folder names
are configurable via `discouragedFolderNames` in `conventions.json`.

## Folders are checked too, not just assets

An empty folder contains no asset, so the asset-level rules never see it — creating a folder
called `Textures` would go unflagged until something was put in it. So
`IAssetRegistry::OnPathAdded` is hooked as well, and folders are checked in their own right
against `type-folder`, `content-toplevel` and `content-project-folder`.

Folders can't be tracked as weak object pointers the way assets are — a folder isn't a
`UObject` — so they're held by path and re-validated against the asset registry, dropping off
the list when `PathExists` goes false (renamed or deleted).

Two consequences worth knowing:

- A folder flagged as `type-folder` makes every asset inside it report the identical
  message. Duplicates are collapsed in the popup, and folders are evaluated first, so the
  surviving entry is the folder — the more actionable one.
- **Moving an asset out of a bad folder leaves the bad folder behind**, and an empty
  misplaced folder is still a misplaced folder, so the warning persists until you delete it
  too. That is intentional, and the test suite asserts it.

## The sticky warning

A modal dialog freezes the editor — you couldn't move the offending asset while it was up,
and moving it is the one action that should dismiss the warning. So it's a persistent Slate
notification instead: non-blocking, with a *Show in Content Browser* link and a *Re-check*
button. Moving, renaming or deleting an asset re-checks automatically via
`IAssetRegistry::OnAssetRenamed`; the button is there for impatience and for changes the
registry doesn't report.

Misplaced assets are tracked as **weak object pointers, not paths** — moving an asset in
Unreal renames the same `UObject` into a new package, so the pointer survives the move and
re-checking is just re-running the rules on the same objects.

One warning covers the whole batch, so a 50-file import can't bury the editor. It's
suppressed automatically when the editor runs unattended (`FApp::IsUnattended()`), which is
also what keeps it from hanging the test harness.

`PlacementWarning` in Project Settings: `Off` / `Log only` / `Toast (auto-dismiss)` /
`Sticky — clears only when fixed` (default).

## Nothing is ever moved automatically

The warning tells you; you decide. A move rewrites references and can strand a redirector in
a module that later gets deleted. And one rule in `CLAUDE.md` is deliberately **not** checked
at all — "shared assets used by 2+ modules belong in `Library/`" is a judgement about how an
asset will be *used*, which is unknowable at the moment it's created.

`map-sublevel` is also not checked: `maps.parentMaps` is empty in this project, so the rule
is off in the checker too.

## Scripting

`UHVPNamingLibrary` exposes the rules to Python and Blueprint:

```python
unreal.HVPNamingLibrary.check_asset_placement(asset)                  # -> [str]
unreal.HVPNamingLibrary.check_placement_by_path(package, asset_name)  # -> [str]
unreal.HVPNamingLibrary.check_folder_placement(package_path)          # -> [str]
unreal.HVPNamingLibrary.recheck_placement()
unreal.HVPNamingLibrary.has_outstanding_placement_warnings()          # -> bool
```

---

# Naming rules

## How each asset type is resolved

Suffixes key off the resulting **UClass**, never the source file extension. One FBX produces
a static mesh, a skeleton, a physics asset, materials and textures; extension logic cannot
express that, class logic gets it for free.

Ambiguity is settled by **inheritance depth**, not a hand-maintained ordering. Both
`UTexture2D` and `UTextureRenderTarget2D` match a `UTexture` rule, but the render target is
deeper, so `_RT` beats `_T`. The same falls out automatically for `_ABP` / `_WBP` / `_CS`
over `_BP`, and `_IA` over `_DA`.

Six suffixes share the *exact* class `UBlueprint`, so depth cannot separate them. They are
told apart by `BlueprintType` and `ParentClass` instead:

| Condition | Suffix |
| --- | --- |
| `BlueprintType == BPTYPE_Interface` | `_BI` |
| `BlueprintType == BPTYPE_FunctionLibrary` | `_BFL` |
| `BlueprintType == BPTYPE_MacroLibrary` | `_BML` |
| `ParentClass` derives `UStaticMeshComponent` | `_SMC` |
| `ParentClass` derives `UActorComponent` | `_AC` |
| otherwise | `_BP` |

All 39 suffixes in `check_conventions.py` resolve deterministically. Nothing is guessed: a
type with no rule is left alone rather than given a best-effort suffix.

Two suffixes are newer than `check_conventions.py`'s list: `_BML` (above) and `_PDL` for a
`HVPPrimitiveData` Primitive Data Legend. `_PDL` is resolved by class path like the rest, so a
project without that plugin simply skips it. A new suffix needs two entries in the source: one
in `AssetSuffixResolver.cpp` (which type gets it) and one in `AssetNamingConfig.cpp`'s
`BuiltInSuffixes` (so a name already carrying it is recognised).

Classes are looked up from **path strings at runtime**, not `#include`d. That keeps the
module's dependencies to engine essentials and lets it load cleanly where Niagara, PCG,
Control Rig or Enhanced Input are disabled — those suffixes are simply skipped, and the
startup log says which. A suffix may list more than one class path — alternates for
different engine versions, or genuinely distinct classes (`_MS` covers both MetaSound
types) — and is only reported unavailable when none of them resolve. In this project 34 of
35 suffixes resolve; `_PCGG` is unavailable because the PCG plugin is off.

## Behaviour

- **Idempotent.** A name that already carries a recognised suffix is returned unchanged, so
  reimport never produces `Foo_T_T`.
- **It will not second-guess you.** A texture you named `Foo_SM` keeps that name. Silently
  rewriting a deliberate name is a worse failure than leaving it for the Claude pass on push
  to raise.
- **Longest-match suffix detection**, mirroring `split_suffix()` in the checker. This is
  load-bearing: `Plasma_ATT` must resolve to `_ATT`, not `_T`.
- **Wrong-case suffixes are corrected, not doubled.** The checker is case-sensitive, so
  `Foo_bp` genuinely fails it — but the author clearly meant a suffix, so the case is fixed
  (`Foo_BP`) rather than a second suffix appended (`Foo_bp_BP`). Only the *case* changes: a
  material named `Foo_sm` becomes `Foo_SM`, not `Foo_M`, for the same reason an exact
  `Foo_SM` is left alone. Longest-match still applies, so `Foo_att` → `Foo_ATT`.
- **Mid-name tags work.** `_REF`, `_WIP`, `_TEMP` and `_OLD` aren't suffixes, so `Foo_REF`
  reads as unsuffixed and becomes `Foo_REF_T` — exactly what the checker wants.
- **New assets, plus renames and moves.** For *creation*, "already on disk" is the test that
  separates a brand-new asset from one merely being discovered (a branch switch, a
  directory-watcher pickup) — new assets live only in memory until saved. A rename or move is
  different: it's an explicit user action, usually on an asset that *is* already saved, so
  `IAssetRegistry::OnAssetRenamed` re-runs both the naming and placement rules with that test
  waived. Rename `Foo_M` to `Bar` and it becomes `Bar_M`; drag an asset into an invalid
  folder and the placement warning fires.
  - One consequence worth knowing: correcting the name of an asset that was already saved
    leaves a redirector at the intermediate name, on top of the one your own rename left. Run
    *Fix Up Redirectors* on the folder afterwards.
- **Debounced.** A single FBX can emit a mesh, skeleton, physics asset, materials and
  textures across several frames. The queue waits for the import to go quiet (0.4s by
  default) and then renames the batch in one `IAssetTools::RenameAssets` call, rather than
  racing the importer. Renames are also deferred out of async loading, GC and package saving.
- **Collision-safe.** Names are made unique against both disk and the rest of the batch.
- **Exempt paths are honoured**, read from `conventions.json`: `Content/Developers/**`,
  `ThirdParty`, `HoudiniEngine`, `UsdAssets`, `Collections`. `Content/Movies` is exempt
  unconditionally — the engine requires media to live there, so no `exemptPaths` override may
  turn it into a violation. An exemption covers the folder itself as well as its contents.
  Only `/Game` is touched — engine
  and other plugins' content never is. Worlds, redirectors, `_BuiltData` and
  `__ExternalActors__` / `__ExternalObjects__` are never renamed.
- **Content Browser inline rename is handled.** Creating an asset from the Add menu drops it
  in as `NewBlueprint` and immediately opens inline rename. Suffixing the placeholder under
  the open text box would just be overwritten by what you type, so the plugin waits for
  `OnAssetPostRename` and appends the suffix to the name you committed.

## Why it is not built on the import hook

The obvious hook is `UImportSubsystem::OnAssetPostImport`. On UE 5.7 it is useless for the
headline case.

Textures, meshes, materials and audio all route through **Interchange** by default —
`BaseEngine.ini` defines `InterchangeProjectSettings` pipeline stacks for `Textures`,
`Materials`, `Audio` and `Assets`. Interchange never broadcasts the import subsystem's
delegates (grep `Source/Runtime/Interchange/` and `Plugins/Interchange/` for
`ImportSubsystem`: no hits). So a plugin built on `OnAssetPostImport` silently does nothing
when you drag a PNG into the Content Browser.

`IAssetRegistry::OnAssetAdded` is therefore the backbone. It fires for every new asset
regardless of whether it came from Interchange, a legacy factory, or the Add menu.
`OnAssetPostImport` is still hooked, but only as a signal that a given asset was *imported*,
so the imported/created scope toggles can be honoured.

---

# Config sharing, and the one duplicated thing

The plugin reads `Tools/Conventions/conventions.json` for `exemptPaths`, `midNameTags`,
`suffixes`, `contentDir` / `projectName` tokens and `severity`. It understands the file's
`//` comments, which `FJsonSerializer` cannot.

One thing is deliberately duplicated. `conventions.json` ships with `"suffixes": {}` because
the Python checker treats that key as an **overlay** on its own built-in table rather than a
replacement — so reading the JSON alone yields an empty table. The plugin therefore mirrors
the same built-in baseline and merges the JSON overlay on top, exactly as
`check_conventions.py` does.

The practical consequence: **add new suffixes to `conventions.json`'s `suffixes` overlay, not
to the C++**. Both tools pick those up and cannot drift. Only the frozen 39-entry baseline
lives in two places, and it also needs a class-path entry in `AssetSuffixResolver.cpp` for
the plugin to act on it.

# Testing

Two suites, both run the same way.

`Tests/test_placement.py` covers 39 cases: every placement rule in both its failing and
passing form for assets *and* for folders (including that `MasterMaterials/` does not trip
the `Materials` rule), the exempt paths, that creating an **empty** `Textures` folder warns
and deleting it clears, that a misplaced asset raises a warning and that **moving it to a
valid folder retracts that warning by itself**, and that renaming or moving an
**already-saved** asset re-runs both sets of rules. All 39 pass.

It wipes its own scratch folders on entry — the editor flushes created assets to disk on
shutdown, and leftovers from a previous run collide and make renames fail.

`Tests/test_asset_naming.py` covers 22 cases: plain class→suffix mappings, the `_RT` vs `_T`
depth case, the `_ATT` longest-match case, all three `UBlueprint` disambiguations, mid-name
tags, idempotency, refusal to rewrite a wrong-but-recognised suffix, suffix case
normalisation (`_m` / `_Bp` / `_bp` / `_sm` / `_att`), PascalCase staying off, and the
`Developers/` exemption. All 22 pass.

Delete `Content/__AssetNamingTest/` between runs — leftovers from a previous run collide and
`CreateUniqueAssetName` appends `1` to everything, which reads as a mass failure.

Both must run in a full editor boot, not a commandlet — the plugin no-ops under
`IsRunningCommandlet()`, and the debounce needs real editor ticks:

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
    "D:\HorizonProjects\BiogenCD38\BiogenCD38.uproject" `
    -ExecutePythonScript="D:\HorizonProjects\BiogenCD38\Plugins\HVPNamingConventions\Tests\test_asset_naming.py" `
    -unattended -nosplash -nopause -stdout -ForceLogFlush
```

Results land in `Intermediate/AssetNamingTest/`. The editor flushes the created assets to
disk on shutdown, so **delete `Content/__AssetNamingTest/` afterwards**.

Note that the build fails with `OtherCompilationError` while the editor is open — that's
Live Coding holding the mutex, not a compile error. Close the editor first.

# Known limits

- Only helps machines with the plugin built. The git hooks remain the enforcement backstop.
- Editor-only (`Type: Editor`), so it cannot affect packaged builds.
- Types with no rule are skipped, not guessed at — `_PCGG` while PCG is disabled, anim
  montages, and any vendor asset type the convention doesn't name.
- It never repairs *existing* content automatically. Use `HVPNaming.Audit` to see what's
  outstanding and `HVPNaming.FixSelected` to fix it deliberately. `Audit` reports naming
  only, not placement.
- Placement problems are flagged, never auto-fixed.
- A case-only fix on an asset that is *already saved* (`HVPNaming.FixSelected` on
  `Foo_bp.uasset`) is a case-only file rename, which Windows' case-insensitive filesystem and
  Git both handle awkwardly. The automatic path is unaffected because it only ever touches
  unsaved, newly created assets. If a case-only rename on committed content misbehaves,
  rename it to something else and back.

# A note on this file's location

`CLAUDE.md` says markdown belongs in `Docs/`, so `check_conventions.py` raises a
`markdown-location` **warning** (not an error) for this README. That's deliberate: the plugin
is meant to be copied whole into other projects, the way `Tools/Conventions` is, and docs
that live outside the folder don't travel with it. The vendor plugins (`HoudiniEngine`,
`OculusHandTools`) already carry the same warning.

To silence it for every plugin, add `Plugins/*/README.md` to `exemptPaths` in
`conventions.json`.
