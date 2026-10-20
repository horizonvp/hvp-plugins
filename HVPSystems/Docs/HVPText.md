# HVPText (HVPSystems)

Letters as data, drawn in one batch per frame. `UTextRenderSubsystem` (module `HVPText`) holds
every letter in the world in named groups and draws each distinct glyph of each font as one
instanced component. Setting a letter only records the change. Late in the frame the subsystem
sends everything that changed: one instance update per changed glyph, however many letters
moved. It came out of the Letter Lab experiments in TextWork, where it drew 1000 moving letters at
72 FPS on Quest 3.

The plugin ships no fonts and no materials. The font tools in `Tools/HVPText` make both from
TTFs in the project.

## Setting it up in a project

1. **Make the fonts.** From the editor: right-click a **Font Face** in the Content Browser >
   **Make HVPText Font...**. The dialog asks for:

   | Field | Default |
   |---|---|
   | Font id | `<Family>-<weight>`: the family from the asset name (`Gotham_Medium_FF`), the weight from the font's OS/2 table (500) |
   | Folder | `<the font face's folder>/<Family>/<id>` |
   | Materials folder | Where the project's Glyph Mesh Material is, else `/Game/HVPText/Materials`. The two materials are made only if missing, so edits to them survive |
   | As MSDF, As meshes | Both |

   Make writes the font face's data to `Saved/HVPText/<id>`, runs `Tools/HVPText/make_font.py` (the
   three scripts below, in the editor's own Python) and fills in any of the settings in step 2 that
   are still empty. The tools' Python packages are this plugin's `PythonRequirements`, which the
   editor installs at startup (Windows; it needs network access once).

   By hand, for many fonts at once: put TTFs under `Art/Fonts` (any depth, one file per weight,
   named by the font id: `Cinzel-700.ttf`). Then, from the project root, with the tools' Python
   requirements installed (`pip install -r Plugins/HVP/HVPSystems/Tools/HVPText/requirements.txt`):

   ```
   python Plugins/HVP/HVPSystems/Tools/HVPText/build_fonts.py                (--metrics-only: no MSDF)
   python Plugins/HVP/HVPSystems/Tools/HVPText/build_glyph_meshes.py      (optional: glyph meshes)
   UnrealEditor-Cmd <Project>.uproject -run=pythonscript -nohmd -script="Plugins/HVP/HVPSystems/Tools/HVPText/import_fonts.py --fonts-folder=/Game/<Where>/Fonts --materials-folder=/Game/<Where>/Materials"
   ```

   `build_fonts.py` uses **msdf-atlas-gen** v1.4 (MIT, Viktor Chlumsky), committed for Windows in
   `Tools/HVPText/bin/msdf-atlas-gen/` with its licence; `MSDF_ATLAS_GEN` or `--msdf` picks another.
   Every script takes `--fonts`, `--out` (or the importer's `--generated`) and `--only <Font> ...`;
   their headers list the rest.

2. **Point the settings at them.** Project Settings > Plugins > **HVP Text**:

   | Setting | Use |
   |---|---|
   | Default Font | A group's font until its style names one. The importer prints a path. |
   | Default Technique, Default Cap Height Cm | A new group's style. |
   | Num Custom Data Floats, Default Custom Data | What every letter carries (below). 3 floats, white. |
   | Glyph Mesh Material | `M_LetterMesh` from the importer. MSDF quads use their font's own material instance. |
   | Quad Mesh | The plane MSDF quads are drawn on: the engine's `/Engine/BasicShapes/Plane`. |
   | Translucent Sort Priority | MSDF letters draw after other translucency (10). |

3. **Draw something.** From any Blueprint: `Get TextRenderSubsystem` > `Set Group Text` with a group
   name, a string and a location. Or add a **Text Group** component and type into its Text.

Game worlds only: the subsystem does not exist in the editor world, so text shows when playing.

## How it works

**Groups.** Every letter belongs to a group: a name the caller chooses, with the group's own
indices 0..N-1. Systems that share the subsystem need nothing but a unique group name; adding to,
re-spelling or removing from one group never touches another's indices. A group exists from its
first letter, style or frame until `RemoveGroup`.

**Styles.** A group's style is its font, technique and cap height (`FTextGroupStyle`). Letters of
every group that share a font and technique share one instanced component per glyph, whatever
their cap height or position, so there is never more than one component for a glyph. A new font
or technique moves the group's letters to other components; a new cap height only resizes them.

| Technique | Draws | Needs |
|---|---|---|
| MSDF | Flat distance-field quads, sharp at any size, with a soft glow | The font's atlas and material instance |
| Glyph Mesh | Extruded glyphs with 3 LODs: real thickness when a letter turns | `build_glyph_meshes.py`, and the Glyph Mesh Material |

**Letter indices.** Each letter also has a world-wide index, stable while it lives: what the grab
bridge, `GetLetterGroup` and the per-letter functions use. Removing a letter frees its slot and
fires `OnLetterRemoved`; the next letter added may reuse it, preferring a slot that showed the same
character (which then needs no new instance). `ClearAll` removes everything and fires
`OnLettersReset`.

**Frames.** A group's letter transforms are relative to its frame: the world, or any scene
component (`SetGroupFrame`). When the component moves, the group follows it.

- **Shared (the default).** The group is drawn with everyone else's letters. A moved frame means
  its letters are re-sent with the next batch: fine for text that moves now and then, a cost per
  letter per frame for text that moves all the time.
- **Own instances** (`bOwnInstances`). The group gets components of its own, attached to the
  frame, so moving it costs nothing at all. The price is those components: one per glyph for this
  group alone, instead of sharing.

A group whose frame is destroyed is removed.

**The batch.** Setters only record. A transient actor the subsystem spawns (`HVPTextPool`, the
shared components' root, at the origin) ticks in `TG_PostUpdateWork`, after HandGrab has moved
anything held, and flushes: letters whose glyph changed move to their new component (the old
instance is hidden and kept for reuse), then every component holding a changed letter gets one
batched transform update and one custom data update. `FlushChanges` does it now, for a measurement
or a log that must match the screen. `Rebuild` recreates every component, dropping the empty ones a
font change can leave behind.

**Looks are per instance custom data.** Letters carry no colour of their own. Each carries
`NumCustomDataFloats` floats, read by its material's `PerInstanceCustomData` nodes; the stock
materials read a colour from floats 0-2. Floats a letter does not set come from Default Custom
Data. MSDF's glyph rectangle is not in there: it is each component's custom primitive data 0-3, so
a custom MSDF material reads it as four scalar parameters with Use Custom Primitive Data, indices
0-3 (left, top, right, bottom), as `M_LetterMSDF` does.

## Blueprint

On the subsystem (`Get TextRenderSubsystem`), under **Text**:

| Area | Functions |
|---|---|
| Group style | `Set Group Style`, `Set Group Font`, `Set Group Technique`, `Set Group Cap Height`, `Get Group Style`, `Get Group Font` |
| Group frame | `Set Group Frame (Group, Frame, Own Instances)`, `Get Group Frame`, `Get Group Frame Transform` |
| Group letters | `Add Group Letter(s)`, `Set Group Letters`, `Remove Group Letter`, `Remove Group`, `Has Group`, `Get Group Names`, `Get Group Letter Count`, `Get Group Letter(s)`, `Get Group Letter Index` / `Indices`, `Set` / `Get Group Letter World Transform(s)`, `Set Group Letter Visible`, `Set Group Visible`, `Set Group Letter Character` |
| Custom data | Scalar, vector and masked writes for a group letter or a whole group; scalar, vector, run and masked writes for a letter by index; getters |
| Layout | `Set Group Text`, `Measure Text`, `Get Letter Spacing` |
| Letters by index | `Is Valid Letter`, `Get Letter Count`, `Get Letter Group`, `Get Letter`, `Remove Letter`, `Set Letter Character`, `Set` / `Get Letter Transform`, `Set` / `Get Letter World Transform`, `Set Letter Location`, `Set Letter Visible`, `Get Letter Bounds` |
| Everything | `Set Num Custom Data Floats`, `Clear All`, `Flush Changes`, `Rebuild`, `Get Render Component Count`, `Get Render Note` |
| Events | `On Letters Rebuilt`, `On Letters Reset`, `On Letter Removed (Letter)` |

**Text layout.** `Set Group Text (Group, Text, Location, Scale, Anchor, Rotation)` spells a string
as the group's letters, reusing its slots, and keeps each kept letter's custom data. Spacing is each
glyph's advance plus the pair's kerning, from the font's GPOS table. Spaces and `\n` only move the
pen, so group indices follow the visible characters. `TopLeft` puts the top-left of the ink at
Location, lines left-aligned; `Center` puts the block's middle there, lines centred. At zero
Rotation the text faces the frame's +X and reads toward its -Y. Tracking and Line Spacing are
advanced pins. `Get Letter Spacing` gives a pair's centre-to-centre distance for placing letters by
hand; `Measure Text` gives a block's size.

**What setting data costs** is the caller's own loop. From C++ it is negligible. From Blueprint,
a few nodes per letter for hundreds of letters every frame adds up to milliseconds on Quest: move
many letters with one `Set Group Letter World Transforms` call, or from C++.

## The Text Group component

`UTextGroupComponent` is one group placed in the world: its Style, its Text (spelled at Begin Play,
centred on the component and facing its +X), its layout, and **Own Instances** (off by default).
The group is the component's frame, made at Begin Play and removed at End Play; `Get Group Name`
gives the name for everything else. Its Group Name defaults to `<Owner>.<Component>`.
`Set` / `Get Letter Relative Transform(s)` move its letters relative to the component, by index in the
group; the subsystem's group functions work in world space.

## Grabbing letters

`UTextGrabBridge` (an actor component) hands letters to HandGrab as an item source: no collision,
no component per letter, a box test against each letter's glyph. Nothing is grabbable until it is
submitted: `Submit Group`, `Submit Groups`, `Submit Grabbables (letter indices)` or `Submit All
Letters`; each replaces the last submission. Letters added later need a new submission; removed ones
drop out by themselves. A held letter's pose goes back into the subsystem as data.

Events: `On Letter Grabbed` and `On Letter Released` (Letter, Group, Index In Group, Hand Index, Hand
mesh, Pinch Location), `On Letter Hover Begin` and `On Letter Hover End`. Highlighting, putting
letters back, or keeping other motion off held letters (`Is Letter Held`) are the listener's. See
[HandGrab.md](HandGrab.md) for how the item source path works.

## Tests

`HVP.Text.Batching` and `HVP.Text.Frames` (HVPSystemsEditor) build a game world and check shared
components across groups and sizes, fonts, slot reuse, rebuilds, frames in the shared pool and in
own instances, and groups whose frame is destroyed:

```
UnrealEditor-Cmd <Project>.uproject -ExecCmds="Automation RunTests HVP.Text;Quit" -unattended -nullrhi -nohmd -testexit="Automation Test Queue Empty"
```
