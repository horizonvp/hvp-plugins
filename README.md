# HVP Plugins

Horizon VP's shared Unreal Engine plugins, in one repository, consumed by projects as a git
subtree at `Plugins/HVP`. This is the single source of truth for the code that used to be copied
between projects by hand.

| Plugin | What it is | Ships in builds | Content |
|---|---|---|---|
| `HVPSystems` | Runtime systems: AnimatedGameflow, CodeAnimation, HandGrab. Editor module with the gameflow smoke test and the state-rename commandlet. | yes | no |
| `HVPStereoButton` | Hand-pressable VR button drawn through a depth-composited stereo layer. | yes | yes |
| `HVPEditor` | Stateless editor tools: naming conventions, graph select, palette generator, reference check. Safe to enable or disable per project. | no | no |
| `HVPPrimitiveData` | Named custom primitive data and per instance custom data: the Primitive Data Legend and Instance Data Legend assets, and the Set Named Primitive Data and Set Named Instance Data nodes (single, multiple, and a batch node for many instances in one call). Once a project uses a node its assets depend on the plugin, so it stays separate from `HVPEditor`. | three functions (the multiple nodes' single writes and the instance batch write) | no |
| `HVPBlueprintUtils` | Small general-purpose Blueprint tools: the Switch on Float node, and the Linear Oscillate, Fold Float Over and Macro Timeline macros. | only `MacroTimelineDirection_E` (the node and macros expand away at compile time) | yes |
| `HVPCodeAnimWeb` | Code Animation Web: a state enum, a set of animation outputs, and how each state drives them, with mid-transition state changes. Blueprint nodes and a graph view in the asset editor. | yes | no |

`HVPHost/` is a build fixture, not a template. It exists so every tag compiles and the smoke tests
run against a project that contains nothing but the plugins. New projects are cloned from the most
recently shipped real project, never from this one.

Engine: **5.8**. Tags carry the engine they were built with (`v1.0.0` is 5.8). A project on an
older engine takes the last tag built for it.

## Using the plugins in a project

To most people the project is one git repository. The plugins are ordinary files under
`Plugins/HVP`, they clone, pull, branch and commit like everything else, and nothing needs setting up.

The one thing to know: **an edit inside `Plugins/HVP` is an edit to shared code.** Keep such edits in
their own commits, without project changes mixed in, so they can be sent upstream cleanly.

## Moving changes between projects

`hvp.ps1` wraps git subtree. It adds the `hvp` remote itself the first time it runs.

```
Plugins\HVP\hvp.ps1 status                  which release is here, and what has been edited locally
Plugins\HVP\hvp.ps1 pull v1.2.0             bring a release in, when you choose to
Plugins\HVP\hvp.ps1 push fix/pinch-grab     send local plugin commits to a branch here for review
```

Pulling records the release in `Plugins/HVP.version`. Pushing extracts only the `Plugins/HVP` part of
the project's history, so it works even for edits that were never intended to leave the project.

A single upstream commit can also be taken without the rest of a release:

```
git fetch hvp
git cherry-pick -X subtree=Plugins/HVP <sha>
```

### First-time adoption of a project that has the old copies

```
hvp.ps1 add v1.0.0 -Project C:\dev\biogen-lupus -RemoveLegacy -ApplyRedirects
```

This removes `Plugins/HVPSystems`, `Plugins/HVPCodeAnimWeb` and `Plugins/Horizon*` in one commit, adds the subtree in the next,
then rewrites the `.uproject` plugin list, appends the rename redirects to `Config/DefaultEngine.ini`
and renames the settings sections in `Config/DefaultEditor.ini`. Rebuild, open the project, resave
the assets that referenced the old names, and delete the redirect block.

The redirects cover the plugin renames from `HorizonStereoButton` and `HorizonPrimitiveData`, and the
stock face material's rename from `LenovoButtonFace_M` to `StereoButtonFace_M`. `HVPSystems` kept its
name and its module names, so gameflow, code-animation and hand-grab assets need nothing.

## Rules the plugins live by

- **A plugin never references the project.** No `/Game` content, no project module, no other plugin
  it does not declare in its `.uplugin`. `HVPEditor`'s reference check enforces this on save, on
  demand (`HVPReferences.Audit` in the console) and headless (`-run=HVPReferenceCheck`, non-zero exit
  on any leak). The stock stereo button face carries no font override and so renders with the
  engine's default Roboto for exactly this reason; projects set their own font on their own face
  widgets.
- **Plugin content that ships must be light.** Example maps and fixtures belong here only when a
  test loads them. A heavy showcase goes in `HVPHost/Content`, outside the plugins.
- **Module names are serialised into assets.** Renaming a runtime or uncooked module means a
  package redirect in every consuming project. Editor-only stateless modules can be renamed freely.
- **Source is LF, binary content is LFS.** `.gitattributes` here must stay in step with the projects'.

## Building and testing here

```
Engine\Build\BatchFiles\Build.bat HVPHostEditor Win64 Development -Project=<repo>\HVPHost\HVPHost.uproject
Engine\Binaries\Win64\UnrealEditor-Cmd.exe <repo>\HVPHost\HVPHost.uproject -run=HVPGameflowTest -nohmd
Engine\Binaries\Win64\UnrealEditor-Cmd.exe <repo>\HVPHost\HVPHost.uproject -run=HVPReferenceCheck -nohmd
```

Both commandlets exit non-zero on failure. A tag is not cut until all three pass.

## Releasing

1. Merge to `main`, build and run the commandlets above.
2. Bump `VersionName` in the changed `.uplugin` files.
3. Tag `vMAJOR.MINOR.PATCH`. Bump MAJOR for an asset-breaking change (a module rename, a removed
   class), MINOR for features, PATCH for fixes.
4. Projects pull the tag when they choose to. Projects close to release usually do not.

## History

The plugins were reconciled from four per-project copies in September 2026. `HVPSystems` came from
lenovo-ailibrary (the superset of gameflow features) with the hand-grab work from biogen-lupus,
which was a direct descendant. The one-shot `HVPMigrate` commandlet that reparented the old
`Content/HVP` Blueprints was dropped; every project had already run it. The three xiq-only editor
plugins became modules of `HVPEditor`. Docs that lived in the projects' `Docs/` folders now travel
with the plugin they describe, under each plugin's `Docs/`.
