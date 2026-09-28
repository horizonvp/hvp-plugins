# Stereo Button (plugin)

`AStereoButtonBase` — a hand-pressable VR button whose face is a UMG widget shown through a
**depth-composited stereo layer**, floating on a spring that fingers can tilt. Shipped as the
`HVPStereoButton` plugin (`Plugins/HVPStereoButton`), not as project source, and with no
Python bootstrap scripts: the C++ is the plugin and the assets are built in the editor over the MCP.

Successor to [HVPButton](HorizonButton.md), whose press model it keeps and whose mesh-plus-baked-
label rendering it replaces. `ButtonBase_BP` / `AHorizonButtonBase` stay untouched; the two coexist.

> **Status (2026-09-15):** copied here from `lenovo-xiq`, where it runs on a Quest 3 as the
> main-menu start button in a Shipping build: depth occlusion, the press latch, the float spring and
> the face shader are all device-verified there. In this project it is **built but not yet placed** —
> nothing references it until you drop a `StereoButton_BP` into a level.

## Files

| File | Purpose |
|---|---|
| [Plugins/HVPStereoButton/HVPStereoButton.uplugin](../Plugins/HVPStereoButton/HVPStereoButton.uplugin) | Runtime module, `CanContainContent` (the Blueprint and materials live under `/HVPStereoButton/`) |
| [Source/.../Public/StereoButtonBase.h](../Plugins/HVPStereoButton/Source/HVPStereoButton/Public/StereoButtonBase.h) / [.cpp](../Plugins/HVPStereoButton/Source/HVPStereoButton/Private/StereoButtonBase.cpp) | The actor: components, properties, tick, dispatchers |
| [Public/StereoButtonPressModel.h](../Plugins/HVPStereoButton/Source/HVPStereoButton/Public/StereoButtonPressModel.h) / [.cpp](../Plugins/HVPStereoButton/Source/HVPStereoButton/Private/StereoButtonPressModel.cpp) | The presser latch, UObject-free. "One poke = one press" lives here and nowhere else |
| [Public/StereoButtonFloatSpring.h](../Plugins/HVPStereoButton/Source/HVPStereoButton/Public/StereoButtonFloatSpring.h) / [.cpp](../Plugins/HVPStereoButton/Source/HVPStereoButton/Private/StereoButtonFloatSpring.cpp) | The floating plate: springs, contact forces, idle bob. `FStereoButtonFloatSettings` is the tuning struct |
| [Public/StereoButtonHandTracker.h](../Plugins/HVPStereoButton/Source/HVPStereoButton/Public/StereoButtonHandTracker.h) / [.cpp](../Plugins/HVPStereoButton/Source/HVPStereoButton/Private/StereoButtonHandTracker.cpp) | Index fingertips off the pawn's hand meshes, positionally stable |
| [Public/StereoButtonLayerComponent.h](../Plugins/HVPStereoButton/Source/HVPStereoButton/Public/StereoButtonLayerComponent.h) | `UStereoLayerComponent` subclass: world-locked, depth on, live texture |
| [Public/StereoButtonWidgetInterface.h](../Plugins/HVPStereoButton/Source/HVPStereoButton/Public/StereoButtonWidgetInterface.h) | Optional interface the face widget implements to react to presses |

## Why a stereo layer, and can it be occluded?

Yes — **"Supports Depth" is on, and it works on this project's settings.** Checked against the
MetaXR source rather than assumed, because a stereo layer that always draws on top would be
unusable for a button a hand has to reach into:

| Path | Where | What happens with `bSupportsDepth = true` |
|---|---|---|
| Quest, `Composite Depth (Mobile)` **off** — *the current project setting* (`bCompositeDepthMobile=False`) | [OculusXRHMD_Layer.cpp:169-215](../Plugins/MetaXR/Source/OculusXRHMD/Private/OculusXRHMD_Layer.cpp#L169-L215), [OculusXRHMD_Layer.h:182-205](../Plugins/MetaXR/Source/OculusXRHMD/Private/OculusXRHMD_Layer.h#L182-L205) | **Poke-a-hole underlay.** The layer is submitted *beneath* the eye buffer (sort priority −1 vs the eye layer's 0) and MetaXR spawns a procedural quad at the layer's transform with `/OculusXR/Materials/PokeAHoleMaterial` — translucent, unlit, `bWriteOnlyAlpha`. Wherever that quad passes the scene depth test it writes alpha 0 into the eye buffer and the layer shows through; wherever a hand or wall is in front, the eye buffer stays opaque and hides it. The hole quad follows the layer transform every frame, so the tilting plate keeps its hole. |
| Quest, `Composite Depth (Mobile)` **on** | [OculusXRHMD_Layer.cpp:1318-1321](../Plugins/MetaXR/Source/OculusXRHMD/Private/OculusXRHMD_Layer.cpp#L1318-L1321) | The eye depth buffer is submitted and the compositor depth-tests the layer per pixel. Same result, costs a depth resolve per frame; the layer stays an overlay. |
| PC / Link | [OculusXRHMD_Layer.cpp:133-138](../Plugins/MetaXR/Source/OculusXRHMD/Private/OculusXRHMD_Layer.cpp#L133-L138) | MetaXR forces depth support on every non-passthrough layer anyway (`bCompositesDepth=True` is set). |
| Editor viewport / PIE without a headset | — | No compositor. The actor shows the **widget plane** instead (see [Surface](#surface)). |

Two consequences of the poke-a-hole path worth knowing before judging it on device:

- **A finger pressing into the button disappears behind the face**, as it physically should — the
  hole quad is in front of the sunk fingertip, so the eye buffer's hand pixels are punched out.
- **The hole quad is drawn at 99% of the layer size** (`QuadScale = 0.99` in `BuildPokeAHoleMesh`),
  so the outer 0.5% of the face is never visible. Keep a transparent margin in the widget.

> **Device result (2026-09-15): the poke-a-hole path is unusable for a button with transparent corners.**
> The hole mesh is the full rectangle, so everywhere the widget is transparent (the rounded corners,
> the margin) the eye buffer is punched out and nothing sits behind the underlay but **passthrough**.
> The project therefore runs `Composite Depth (Mobile)` **on** (`bCompositeDepthMobile=True` in
> [DefaultEngine.ini](../Config/DefaultEngine.ini)): the eye depth is submitted, `NeedsPokeAHole()` is
> false, and the layer is a normal depth-tested overlay whose alpha blends over the scene. Cost: a
> depth resolve per frame ([OculusXRHMD.cpp:3787-3795](../Plugins/MetaXR/Source/OculusXRHMD/Private/OculusXRHMD.cpp#L3787-L3795)).

Nothing in the poke-a-hole path is affected by `r.Mobile.PropagateAlpha=False`
([DefaultEngine.ini](../Config/DefaultEngine.ini)) — the hole material's own blend state writes the
alpha directly to the swapchain. **Still: verify on a Quest 3 before building UI on it.** If the hole
does not appear (button always on top), flip `Composite Depth (Mobile)` on in Project Settings →
MetaXR and it becomes a compositor depth test instead; both are one setting away and the button code
needs no change.

Why not a `UWidgetComponent` alone: the layer is the whole point — the compositor resamples the
render target per frame at its own resolution, so text stays sharp and stable under head motion.
Why not the old three-mesh button: everything about its look is a material chain that only Python
could build; here the look is a widget the designer owns.

## Components

All attached under `FloatRoot`, the plate the spring moves. Nothing is a mesh.

| Component | Class | Role |
|---|---|---|
| `Root` | `USceneComponent` | Neutral frame. Press detection is measured here, never on the moving plate |
| `FloatRoot` | `USceneComponent` | The plate. Sinks along −X and tilts about Y/Z |
| `WidgetComp` | `UWidgetComponent` | Hosts `FaceWidgetClass`, draws it to a render target. `World` space, no collision, ticks off-screen |
| `StereoLayer` | `UStereoButtonLayerComponent` | Shows that render target. Same transform as `WidgetComp` |
| `PressVolume` | `UBoxComponent` | Overlap volume for the grab-sphere fallback presser |

**Frame:** the face is the local **YZ plane at X = 0**, normal **+X toward the user**. Fingers press
along −X. This matches `UWidgetComponent`; the **stereo-layer quad faces the other way**, so
`StereoLayer` carries a relative yaw of 180° (found on device 2026-09-15: with identity rotation the
button faced away from the player and read mirrored from behind — the callout popup's stereo quad
has the same flip). With that, the visible face and the pressable face coincide.

## Surface

`Surface` (Face) chooses what shows the widget:

| Mode | Behaviour |
|---|---|
| `Auto` (default) | Stereo layer whenever `GEngine->StereoRenderingDevice` is stereo-enabled and offers `IStereoLayers`; the widget plane otherwise. Re-evaluated every tick, so it follows `stereo on` |
| `StereoLayer` | Always the layer. Invisible without a compositor |
| `WidgetPlane` | Always the plane. For A/B on device |

Switching to the layer applies **`HiddenPlaneMaterial`** to the widget plane — the plane must keep
*rendering* (that is what refreshes its render target) but must not be *seen*. The material has to
be translucent with zero opacity; `InvisableWidget_M` in `Systems/Popups/TextPopup` is exactly that
and is a fine default until the plugin ships its own. Left null, the plane stays visible behind the
layer and a warning is logged once.

The layer's texture is **reconciled** each tick, not pushed once: a `UWidgetComponent`'s render
target does not exist until the widget has painted, so the first frames have nothing to show.
`SetTexture` / `SetQuadSize` are called only when the value actually differs (each marks the layer
dirty).

## Press detection

The presser latch from [HVPButton](HorizonButton.md#one-poke--one-press--the-presser-latch),
ported into `FStereoButtonPressModel` with every behavioural rule intact and none of the actor
around it. Read that section for the history; the short form:

- **Fingertips first.** `FStereoButtonHandTracker` reads the index-tip bone off every hand mesh on
  the pawn, keeps only the tracked rig (`FingertipMeshClassFilter`, default `OculusXRHand`), merges
  co-located tips (`FingertipMergeDistanceCm`), and holds slots positionally stable — a tip's index
  is its identity to the latch.
- **Grab sphere only as fallback**, when no tip could be read (`PresserComponentFilter`).
- **Contact region bounded at the front, open at the back.** A tip is *retained* while
  `|y| ≤ HalfW + ReArmClearanceCm`, `|z| ≤ HalfH + ReArmClearanceCm`, `penetration > −ReArmClearanceCm`;
  it can *acquire* a fresh latch only squarely inside the face with `penetration > 0`.
- **One press per latch**, cooldown, grace window, per-channel edges, `MaxConcurrentPressers` —
  all as before. `ShouldRearmOnRelease()` is the same protected hook for a spam-button subclass.
- **Penetration is measured against the neutral face** (`Root`), not the moving plate. The physics
  can never change what fires.

Each sample also carries `ContactLocal` (Y, Z on the face, cm) and `PenetrationCm`, which is what
the float spring consumes.

`PressDepth` defaults to **4.0**, the deploy-tested value, and means the same thing as before:
required finger-bone travel is `PressThreshold * PressDepth − FingertipRadius` (≈2.2 cm).

## Floating

`FStereoButtonFloatSpring` is a rigid plate on a centre spring with torsional springs on its two
in-plane axes, driven by **penalty forces at the contact points**:

```
for each presser:
    depth = plateSurfaceX(contact.yz) − fingerPadX        // > 0 when the pad is past the plate
    F     = ContactStiffness * depth                       // pushes the plate in at that point
    torque = r × F   →  τy = −z·F,  τz = +y·F              // off-centre pushes tilt it
springs pull sink and both tilts back to zero
semi-implicit Euler at SubStepSeconds (1/240 s), hard-clamped to MaxSinkCm / MaxRiseCm / MaxTiltDegrees
```

So a gentle tap on the left edge tips the left edge in, the plate overshoots on the way back and
settles — the "floaty" return is simply an underdamped spring. Everything is in
`FStereoButtonFloatSettings` (Float category):

| Property | Default | Feel |
|---|---|---|
| `PressSpringHz` / `PressDampingRatio` | 2.2 / 0.28 | Return along the press axis. Lower ratio = more overshoot |
| `TiltSpringHz` / `TiltDampingRatio` | 1.8 / 0.22 | How a tilt rights itself. Lower ratio = longer wobble |
| `ContactStiffness` | 2500 | How tightly the plate hugs the fingertip (1/s²). Too high buzzes |
| `ContactDamping` | 25 | Kills ringing against a finger that is holding still |
| `LeverArmCm` | 6 | Offset at which a contact gets full leverage. Smaller = edge taps tilt more |
| `MaxTiltDegrees` | 12 | Hard tilt limit |
| `MaxSinkCm` / `MaxRiseCm` | 5 / 1.2 | Hard travel limits (sink ≥ `PressDepth` looks right) |
| `IdleBobAmplitudeCm` / `IdleBobHz` | 0.25 / 0.35 | Resting hover. 0 = still |
| `IdleSwayDegrees` / `IdleSwayHz` | 0.6 / 0.23 | Resting sway. 0 = still |

The idle motion is layered on top of the physics rather than fed through it, so it can never pump
energy into the springs. `bFloatEnabled = false` pins the plate at neutral without touching press
detection. `TriggerButton` / `DebugFirePress` kick the plate with an impulse so a synthetic press is
visible too.

Angles are right-handed about local Y and Z internally; `ToRotator()` converts (UE's pitch is the
opposite hand to a right-handed rotation about Y, so `Pitch = −θy`, `Yaw = +θz`).

## The face widget

The actor owns nothing about the look. It talks to the widget through
`IStereoButtonWidgetInterface` (every function optional):

| Function | When |
|---|---|
| `SetButtonLabel(Text)` | On construction and on `SetLabelText` |
| `SetButtonColor(Color)` | On construction and on `SetLabelColor` |
| `UpdateButtonState(PressedAmount, bInContact, bPressed, IdleSeconds)` | Every tick — drive a glow, a press-darken, an idle shimmer |
| `OnButtonEdge(bPressed)` | The press / release moment |

A widget that implements none of it still works: `LabelText` is also written into a `TextBlock`
named **`LabelText`** if one exists.

Sizing: `FaceSizePx` is the widget's authored size and `PixelsPerCm` its density — 512×192 at 16 px/cm
is a 32×12 cm face. `FaceSupersample` (default 2) multiplies the render target and shrinks the plane by
the same factor, so the face keeps its centimetres at four times the texels. **The widget component's
draw size is both its render target AND its layout box**, so the widget lays out at
`FaceSizePx * FaceSupersample` and must be authored for that box — raising the supersample enlarges the
space the widget lays out in, and fixed font sizes then look smaller against the face. (The first
version tried to keep the layout at `FaceSizePx` by render-scaling the widget up to fill the larger
target; the content already filled it, so only its top-left quarter survived the clip. Found on a Quest
on 2026-09-15.) The material is unaffected — it works in 0..1 UVs, so `FacePx` only sets how the corner
radius and margin read as a fraction of the face. Both the widget plane scale and the layer quad size
derive from these, so they can never disagree. Leave a transparent margin (see the 99% note above), and remember the material
chain a stereo layer sees: Slate → RT → MetaXR premultiply → compositor, so a UI material with soft
edges wants **AlphaComposite**, not Translucent, or its edges darken twice
([VideoAdditiveStereoLayer.md](VideoAdditiveStereoLayer.md#why-alphacomposite-and-not-translucent)).

## Public API

```cpp
UPROPERTY(BlueprintAssignable) FStereoButtonPressed  OnButtonPressed;    // (Index, Button)
UPROPERTY(BlueprintAssignable) FStereoButtonReleased OnButtonReleased;   // (Index, Button)

void  SetLabelText(const FText&);      void  SetLabelColor(const FLinearColor&);
void  ResetButton();                   void  TriggerButton();
void  DebugFirePress();                // CallInEditor
float GetPressedAmount() const;        bool  IsPressed() const;       bool IsInContact() const;
int32 GetActivePresserCount() const;   bool  IsEngaged() const;       float GetIdleSeconds() const;
void  SetInputLocked(bool);            bool  IsInputLocked() const;   void UnlockExclusiveGroup();
UUserWidget* GetFaceWidget() const;    FVector2D GetFaceSizeCm() const; bool IsUsingStereoLayer() const;
```

`ExclusiveGroup`, `EngageDelaySeconds`, `bBlockPressWhenHidden`, `PressSound` / `ReleaseSound`
behave exactly as on HVPButton.

## Assets (plugin content)

| Asset | What |
|---|---|
| `/HVPStereoButton/StereoButton_BP` | Child of `StereoButtonBase`, with `HiddenPlaneMaterial` and `FaceWidgetClass` set. Place this, not the C++ class |
| `/HVPStereoButton/StereoButtonFace_WBP` | The face widget: the material in an `Image` named `FaceImage` filling the canvas, with the label on top |
| `/HVPStereoButton/LenovoButtonFace_M` | The face shader — see below |
| `/HVPStereoButton/HiddenPlane_M` | Translucent, unlit, two-sided, opacity 0 — applied to the widget plane while the layer shows the face |

`FacePx` in the material must match the actor's `FaceSizePx`, or the rounded rectangle is the wrong
shape — the material works in pixels because Slate hands it 0..1 UVs with no size.

The face widget's label font is named by the **xiq** content path, so
`Config/DefaultEngine.ini` carries a `PackageRedirects` line mapping
`/Game/LenovoXIQ/Library/Font/Gotham/Gotham_F` onto this project's copy. Re-point the font in the
widget and the redirect can go.

## Using it in this project

Nothing here uses it yet. To add one:

1. Drag `/HVPStereoButton/StereoButton_BP` into the level and face its **local +X** at the
   player — that is the face normal (width is local +Y, height local +Z).
2. Set `FaceSizePx` and the material's `FacePx` to the same numbers, `PixelsPerCm` for the physical
   size, and `LabelText`.
3. Bind `OnButtonPressed` (or `OnButtonReleased`) — both hand `(int32 ButtonIndex,
   AStereoButtonBase* Button)` — from whatever owns the button, the same way `ButtonBase_BP`'s
   dispatcher is bound today. See [HVPButton](HorizonButton.md) for the pattern this replaces.
4. Give the actor a `PressSound` if it should click.

In `lenovo-xiq` the button lives under an invisible controller Blueprint that holds the reference,
binds the dispatcher on BeginPlay and scales the button in with a timeline — a good shape to copy if
a menu here needs the same reveal.

**Zero-scale guard.** Hiding a button by scaling it to zero is safe: at zero scale
`InverseTransformPosition` would collapse every world point onto the origin and read as a fingertip
dead-centre on the face, so `SamplePressers` returns nothing while the actor's scale is degenerate.

## The face shader: `LenovoButtonFace_M`

`/HVPStereoButton/LenovoButtonFace_M` (2026-09-15) — the "AI Recommendation" reference: a rounded
red pill with a vertical gradient, a glassy highlight along the top, a shadow along the bottom, a
tilted sheen band that sweeps across every few seconds, and a little parallax. `MD_UI`,
**`AlphaComposite`**, one `Custom` HLSL node (`LenovoButtonFace`, node `MaterialExpressionCustom_1`)
fed by parameters, its `rgb` to Emissive and `a` to Opacity. (The first version melted three reds
through domain-warped noise; the user asked for the gradient + sheen instead. The noise macros are in
git history if ever wanted back.)

| Parameter | Default | Does |
|---|---|---|
| `FacePx` | (416, 160) | Must equal the button's `FaceSizePx` — the SDF works in pixels |
| `CornerPx` / `MarginPx` | 18 / 4 | Corner radius and the transparent border (keep ≥ 3 px for the compositor's 99 % hole) |
| `ColorTop` / `ColorBottom` | crimson / maroon | The vertical gradient |
| `GradientTop` / `GradientBottom` | 0 / 1 | Where the gradient starts and ends, 0 = top edge, 1 = bottom edge. Pure `ColorTop` above the start, pure `ColorBottom` below the end |
| `SheenColor` / `SheenStrength` | pale pink / 0.35 | The band's tint and how much it adds |
| `SheenPeriod` / `SheenSweepSeconds` | 3.0 / 0.8 | One sweep starts every period and takes this long (ease in/out); the rest of the period is quiet |
| `SheenWidth` / `SheenTilt` | 0.08 / 0.35 | Band width as a fraction of the face, and its lean (x shift per unit of height) |
| `Parallax` | 0.08 | How far the sheen layer shifts per unit of view angle; the gradient moves 35 % of it, the gloss 25 % |
| `GlossStrength` / `GlossHeight` | 0.5 / 0.45 | Top glass: a soft cap over the top part plus a thin bright line just under the edge |
| `ShadowStrength` / `ShadowHeight` | 0.45 / 0.35 | Bottom darkening |
| `EdgeDarken` | 0.25 | Bevel darkening at the rim |
| `Opacity` | 1 | Scales coverage |
| `ViewOffset` | (0, 0) | **Pushed by the actor every tick** — see below |
| `PressAmount` | 0 | **Pushed by the actor every tick** — brightens the face while pressed |

### Parallax plumbing

`AStereoButtonBase::ComputeViewOffset` puts the player camera into the **plate's** frame
(`FloatRoot`, so the spring's tilt reads as parallax too) and returns `(Y/X, Z/X)`: the tangents of
the view angle, zero when looking straight on. Every tick the actor:

1. calls `UpdateButtonView(ViewOffset)` on the face widget if it implements the interface, and
2. finds an `Image` named **`FaceImage`** in the widget, takes its dynamic material (created once
   from the brush material) and sets the vector `ViewOffset` and scalar `PressAmount` on it.

So a widget needs **no Blueprint at all** for parallax and press feedback: name the background
image `FaceImage` and give it the material. Nothing is pushed in the editor viewport (no camera), so
the material previews at `ViewOffset = 0`.

Custom-node gotchas met here: the code is pasted into a function body, so a function definition is
a compile error (use macros); `line` is a reserved word in HLSL; `MaterialTools.recompile` does not
raise on HLSL errors — grep the log for `Failed to compile Material` after every recompile; and a
rolled-back batch can leave an orphan `Custom_0` beside the wired `Custom_1` — check which node you
edited. Renaming a Custom input keeps its link; growing the `Inputs` array needs the two-step
grow-then-rename write. The HLSL source is also kept at
`Plugins/HVPStereoButton/Source/Shaders/LenovoButtonFace.hlsl` for diffing; the Custom node's
`Code` is the copy that runs.

## What is left here

1. **Place one** and check it in the headset. This project's MetaXR settings already have
   `Composite Depth (Mobile)` on, which is what makes the layer occlude properly.
2. **Hand tracking frequency** is `LOW` in `Config/DefaultEngine.ini` (xiq runs `HIGH`, with
   `SuggestedCpuPerfLevel=SustainedHigh`). The poke will feel less crisp until that is raised —
   a project-wide perf call, so it was left alone.
3. **Restyle the face.** `StereoButtonFace_WBP` and `LenovoButtonFace_M` came over as authored for
   xiq's red pill; the parameters in the table above are the fast way to re-colour it.
