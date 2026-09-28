# HandGrab (HVPSystems)

`UHandGrabSubsystem` (Plugins/HVPSystems/Source/HVPHandGrab/) is the HVP pinch-grab
primitive: a world subsystem that turns "the player pinched near this mesh, carried it, and
let go" into events any actor can consume. Extracted from a drag-and-place actor so every
drag/carry interaction shares one gesture implementation.

## What it does

- **Finds the hands itself.** Any `USkinnedMeshComponent` on the player pawn carrying both
  a `*thumb*tip*` and an `*index*tip*` bone is a hand — the same bone discovery
  `AHorizonButtonBase` uses for fingertip presses. No per-hand components, no pawn
  Blueprint changes, and it works for tracked hands and controller-animated hand meshes
  alike (squeezing grip closes the mesh's tips past the pinch threshold).
- **Pinch OR fist, each with hysteresis.** The pinch distance is the *closest* of the thumb
  tip/pad to the index tip/pad and to the middle tip/pad (`bPinchWithMiddle`,
  `bUsePadMarkers`), so tip pinches, pad-to-pad pinches and the "fingertips gathered" grip
  all count. It closes at `PinchStartDistance` (3 cm) and releases at
  `PinchReleaseDistance` (5.5 cm). A closed fist grabs too (`bEnableFistGrab`, on by
  default): the index *or* middle tip curling to within `FistCloseDistance` (11 cm) of the
  wrist, opening at `FistOpenDistance` (13.5 cm) — which also makes a controller grip
  squeeze (it animates the hand mesh into a fist) grab naturally. The hysteresis on both
  means tracking jitter can't machine-gun grab/drop. The carry point is the thumb-to-finger
  midpoint for a pinch, the palm-center marker for a fist.
- **Sized to the hand.** All the cm thresholds are for the reference hand (`Hand_*_SKM` at
  scale 1) and scale with the live wrist-to-middle-knuckle length over
  `ReferencePalmLength` (9.57 cm), clamped to `MinHandSizeScale`/`MaxHandSizeScale`. A
  calibrated large hand closes the gesture at the same *pose* as a small one.
- **Grabs registered components.** When the gesture closes, the nearest *enabled,
  unheld* grabbable within its own `GrabRadius` is grabbed. The search probes the grab point
  first and then the thumb, index and middle tips and the palm (with a 1 cm handicap, so the
  item between the fingers beats one a fingertip brushes) — closing the fingers moves the
  grab point, often off the item the player was aiming at. **By default only one thing can
  be held at a time across all hands** — the second hand gets no grab and no hover
  affordance until the first releases. Set `bAllowMultiGrab` for one-per-hand behavior; a
  held component can never be stolen by the other hand either way.
- **Forgives slow, deliberate grabs.** Beyond the close edge itself:
  - *Grace* — for `GrabGraceSeconds` (0.3 s) after closing, a still-empty hand keeps
    looking, so closing a beat before arriving still grabs.
  - *Hover memory* — if nothing is in reach at the close, an item the open hand hovered
    within `HoverMemorySeconds` (0.35 s) is taken if it's within
    `GrabRadius × HoverMemoryReachScale` (1.5) of any probe.
  - *Squeeze* — a hand that arrives already closed (edge and grace spent out of reach)
    grabs an in-reach item when it tightens by `SqueezePinchDelta` (0.75 cm) or
    `SqueezeFistDelta` (1.5 cm) past the loosest pose since arriving. Simply sweeping a
    closed hand through items grabs nothing.
  - *Release debounce* — a held item is only released after the gesture has read open for
    `ReleaseDebounceSeconds` (0.1 s), so a mis-tracked frame mid-carry doesn't drop it. An
    empty hand re-arms immediately.
- **Carries it.** While held (and the registration's `bFollow` is true, the default) the
  subsystem chases the component toward the pinch midpoint at the registration's
  `FollowSpeed`. Register with `bFollow = false` to drive constrained motion yourself
  (sliders, levers, dials) off the grab/release events and `GetPinchLocation`.
- **Tells you what happened.** `OnGrabbed` / `OnReleased` fire with the component, the hand
  index, and the pinch location. What a release *means* — snap, return, drop, throw — is
  the consumer's business. `OnHoverBegin` / `OnHoverEnd` bracket "an open hand is in reach"
  for affordance visuals, refcounted across hands so you get exactly one begin/end pair.

Game worlds only (it doesn't exist in editor preview worlds); ticks itself.

## Using it

```cpp
#include "HandGrabSubsystem.h"   // module: HVPHandGrab

void AMyActor::BeginPlay()
{
    Super::BeginPlay();
    if (UHandGrabSubsystem* Grab = GetWorld()->GetSubsystem<UHandGrabSubsystem>())
    {
        FHandGrabbableParams Params;
        Params.GrabRadius  = 14.f;   // cm from pinch to component center
        Params.FollowSpeed = 18.f;   // held chase speed (exp interp)
        Grab->RegisterGrabbable(MyMeshComponent, Params);

        Grab->OnGrabbed.AddUniqueDynamic(this, &AMyActor::HandleGrabbed);
        Grab->OnReleased.AddUniqueDynamic(this, &AMyActor::HandleReleased);
        Grab->OnHoverBegin.AddUniqueDynamic(this, &AMyActor::HandleHoverBegin);
        Grab->OnHoverEnd.AddUniqueDynamic(this, &AMyActor::HandleHoverEnd);
    }
}
```

The same API is Blueprint-callable (Get HandGrabSubsystem → Register Grabbable / bind the
events). Handlers receive every consumer's components — filter to your own (compare the
`Component` argument against what you registered) before acting.

Lifecycle rules:

- `UnregisterGrabbable` in `EndPlay` (and whenever you destroy a registered component).
  Stale components are also pruned automatically, but explicit is tidier.
- `SetGrabbableEnabled(Comp, false)` gates a grabbable without unregistering — e.g. while
  it animates into a slot, or when game state says hands off. Disabling does not release a
  hand already holding it.
- `ForceRelease(Comp)` pries a component out of a hand **without** broadcasting
  `OnReleased` — for resets/scripted takeovers where you don't want your own release logic
  to run. A release caused by the pawn or hand meshes disappearing DOES broadcast, so
  consumers can put their item somewhere sane.

Queries: `IsHeld(Comp)`, `GetHandCount()`, `IsHandPinching(i)`, `GetPinchLocation(i)`,
`GetHeldComponent(i)`.

Tuning: the gesture thresholds and forgiveness timings are subsystem-level (the gesture
belongs to the player, not to any one interaction); per-object feel lives in the
registration params. `bDrawDebug` draws pinch points (green while pinching), the fingertip
and palm probes (cyan = pinch closed, orange = fist closed) and every grabbable's radius
(dark gray enabled, dark red disabled).

To tune on device, turn on `bLogGestures` or run the console command
`HandGrab.LogGestures 1`. While a hand is near a grabbable or closed, `LogHVPHandGrab`
prints its pinch and fist metrics (already in reference-hand cm, so directly comparable to
the thresholds) and the size scale a few times a second, and every grab and release logs
with its reason (`close`, `hover-memory`, `grace`, `squeeze`). Hand discovery logs the
bones it resolved once per pawn. If a pose that should grab is missed, its logged metric
tells you which threshold to move.

## Consumers

- **ALibraryLoadout** (Source/LenovoAILibrary/LibraryLoadout/) — registers its draggable
  items, disables them while they ease onto seats, and decides place-vs-return on
  `OnReleased`. See [LibraryLoadout.md](LibraryLoadout.md); it's the reference consumer.
