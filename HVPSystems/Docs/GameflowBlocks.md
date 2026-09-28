# Gameflow queue blocks (`All` / `Any`)

How the gameflow transition queue expresses "visit every one of these, in any order" and
"pick one of these" without leaving the flat list of state strings authored on the
**Gameflow Config** actor. Plugin: `Plugins/HVPSystems/Source/HVPGameflow`, rules in
`GameflowQueueRules.h`.

## The idea

States are strings, and the queue is a flat array of them. Structure is carried by the
paths, not by the container. There are exactly two operators:

- `/` — containment. `AILibrary/Curated/Reveal` is inside `AILibrary/Curated`. This is
  what level bindings, CodeAnimation `ActiveStates` and receptacle gates already match on.
- A reserved segment, **`All`** or **`Any`** — the entries that share the prefix before it
  form a **block** of branches around an implicit **hub**, which is that prefix.

```
AILibrary/Curated/Intro
AILibrary/Curated/Pick                       <- explicit hub entry (optional, recommended)
AILibrary/Curated/Pick/All/PlayA_Video       <- block: hub = AILibrary/Curated/Pick
AILibrary/Curated/Pick/All/PlayB_Video
AILibrary/Curated/Pick/All/PlayC_Video
AILibrary/Curated/Pick/All/PlayD_Video
AILibrary/Curated/Pick/All/PlayE_Video
AILibrary/Curated/Overview
```

The marker names what it takes to leave the block:

| | Entering a branch | Next state from the branch | Block is done when |
|---|---|---|---|
| **All** | removes only that entry | the hub, while siblings remain | it is empty (the join) |
| **Any** | removes the whole block | whatever follows the block | one branch was entered |

## The rules

The controller asks the rules two things: *what is next* (Continue / Snap) and *this
transition was accepted* (advance). Everything else is unchanged.

**Next state** (`GetNextQueuedState`, used by `ContinueToNextState` and `SnapToNextState`):

- Plain front entry → that entry.
- Block at the front, current state outside the hub → the hub. It does not need to be
  listed; it is derived from the front entry's path.
- At the hub → the first remaining branch. A skip-walk still visits every branch in order.
- In a branch of an `All` block → the hub. (Siblings remain by construction: if none did,
  the block would already be gone and the front would be whatever follows.)

**Advance** (once a request or demand is accepted):

- The first entry the target lands on is found: an exact match, or a block whose hub
  subtree contains the target. Entries before it were vaulted over and are dropped, as with
  any jump-ahead demand today.
- A plain entry (an explicitly listed hub included) is popped.
- The hub itself, or a substate of it that is not a branch, leaves the block untouched.
- A branch of an `All` block removes the entries it lands on; a branch of an `Any` block
  removes the whole block.
- A target that lands nowhere clears the queue on a demand and leaves it alone on a request,
  exactly as before.

**Visited state is queue membership.** There is no separate visited set and nothing to
reset. `TransitionQueue` (Blueprint-readable, as before) shows exactly the branches that
remain; `GetRemainingBranches` returns them for a "3 of 5" callout.

## The Curated section, end to end

1. `Curated/Intro` plays, the selection actor pulls the five books into the row, and the
   VO auto-continues. Next state is `Curated/Pick`.
2. The flow rests at `Pick`. The podium receptacle accepts drops because its
   `ActiveStates` is `{AILibrary/Curated/Pick}`.
3. The visitor places any book. `Slots_BP` (the podium) requests the placed book's
   `ItemID` state, e.g. `AILibrary/Curated/Pick/All/PlayC_Video`. The advance rule sees a
   branch of the front block and removes just that entry. The video screen and VO row
   keyed on that state play.
4. The video's auto-continue asks for the next state: the hub. The flow returns to `Pick`.
5. Repeat in any order. During a video the receptacle is closed: a hub entry in its
   `ActiveStates` admits the hub's resting state only, not its branches
   (`bHubStatesExcludeBranches`, default on).
6. After the fifth video the block is empty, so the next state is `Curated/Overview`.

The skip button and the portal demand section roots by name, and a demand to a state
further down the queue drops the block with everything else it vaults over. Snap from the
hub walks the remaining branches in authored order.

## Authoring notes

- List the hub explicitly before its block. It is not required (the controller derives it),
  but it matches the parent-before-children habit the list already uses and makes the
  resting state visible to a reader.
- List the hub again **after** the block for a closing beat at the hub before moving on.
- Branches are leaves in the list. What happens inside a branch belongs to consumers through
  subtree matching (a screen keyed on its `PlayX_Video` state), not to further entries.
  Nested markers are not supported and warn.
- `StartTransitionQueue` logs a warning for a marker as the first or last segment, nested
  markers, and a block whose branches are not contiguous. Read the log after changing the
  list.
- Returning to the hub is a move *within* the hub's subtree. A consumer whose `ActiveStates`
  is `{Curated/Pick}` is entered once and left once for the whole picking phase. The
  per-round boundary is the **block** subtree, `Curated/Pick/All`: entering it means a
  pick was made, leaving it means a video ended.
- `Any` gives a one-of choice: a menu that leads to exactly one of several demos before
  the flow rejoins.

## Renaming states across assets

States are spelled out in more places than the config: Blueprint defaults, component
templates, map actors, graph literals and Switch-on-String cases, and the VO lines
DataTable's row names. `HVPRenameStates` (in the HVPMigration editor module) rewrites
exact, case-insensitive matches in all of them and saves only what changed:

```powershell
& "E:\EpicGames\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
    "<repo>\LenovoAILibrary.uproject" -run=HVPRenameStates -nohmd -xrtrackingsystem=None `
    -Paths=/Game/LenovoAILibrary `
    -Renames="AILibrary/Curated/PickA=AILibrary/Curated/Pick;AILibrary/Curated/PickB=;AILibrary/Curated/PlayA_Video=AILibrary/Curated/Pick/All/PlayA_Video" `
    -DryRun
```

An empty right-hand side deletes the entry from arrays and sets. Map keys, switch cases and
DataTable rows mapped to delete are reported and left for a hand edit. Run with `-DryRun`
first and read the per-change log; drop the flag to save. Editor closed, as with every
headless run in this project.

## Tests

`UnrealEditor-Cmd.exe <project> -run=HVPGameflowTest -nohmd -xrtrackingsystem=None`
covers the rules as pure functions (parse, out-of-order visits, implicit hub, jumps into
and past a block, the closing-beat hub, `Any`, validation) and a controller walk of an
`All` block through Continue, Request and Snap.
