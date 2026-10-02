# Milestone 4 plan: movement authority proof

Status: implemented on 2026-10-01; partial live two-game validation on
2026-10-02, with the completion gate still open. See
[the implementation and evidence record](milestone-04.md). This is a feasibility
experiment, not a general Skyrim traversal system.

## Outcome and boundary

In one deliberately isolated, disposable Skyrim session, create one minimal
vanilla visual reference and apply fresh OOT Link poses to it. Link's position
and yaw come only from Shipwright's existing post-player-update telemetry; the
Skyrim integration performs a coordinate transform and pose application, not a
second movement controller. Skyrim continues to own its world, NPCs, quests,
interactions, and rendering.

This milestone does not exchange collision, claim movement against arbitrary
Skyrim terrain, add Link assets or animation, change either camera, implement
combat/inventory/interactions, or persist cross-game state. Milestone 5 owns
collision exchange.

## Verified source basis and unresolved assumptions

The pins remain Shipwright `ff0209e76f01cf816806cd1d8521373744d1dc8e`
and SKSE `25b72352adb6543fa6d0bd3795780672b2e238e0` for Skyrim 1.7.104.
Milestone 3 verified the raw fields and encodings but explicitly did **not**
measure a cross-game origin, axis mapping, scale, yaw sign, or common terrain.

The pinned SKSE declarations expose `PlaceAtMe_Native`, reference handles, and
`MoveRefrToPosition`; they are game-thread operations. The pinned Shipwright
hook captures `Player.actor.world.pos` and yaw after OOT's own player update.
Those facts justify a pose follower. They do not justify a universal transform.

For this proof the transform is local and resettable:

* The first usable OOT sample and current Skyrim player pose define paired
  origins, with an optional fixed Skyrim X offset to keep the test object in
  view beside the player. Absolute coordinates are never equated.
* The candidate basis is OOT `(x, y, z)` to Skyrim `(x, z, y)`, with a configured
  scalar number of Skyrim units per OOT unit. OOT `y` and Skyrim `z` are treated
  as the candidate vertical axes only for the controlled test.
* Proxy heading is the Skyrim anchor heading plus the signed delta from OOT's
  initial shape yaw. Binary-angle wrap is handled before converting one turn to
  `2*pi`; the candidate sign is explicit and testable.
* Runtime evidence must verify axis direction, scale usefulness, and yaw sign
  before any of these choices are reused. A failed measurement rejects the
  candidate; it does not become a roadmap fact.

## Proxy lifecycle

The feature is off unless an explicit test configuration enables it and names a
vanilla base-form ID. On a playable, unpaused Skyrim game-thread task with a
fresh usable OOT sample, the plugin places exactly one **nonpersistent** reference
at the Skyrim player through the pinned native placement function. The returned
reference pointer and runtime form ID are its identity; the plugin never adopts
or moves an arbitrary pre-existing reference.

The configured base must be a simple non-actor visual object. Actors and objects
with their own character controllers are rejected because Skyrim AI, animation,
or character physics would compete for authority. Each accepted OOT sample is
applied as an absolute pose with `MoveRefrToPosition`; no Skyrim velocity,
pathfinding, forces, or controller input are authored.

The proxy is owned only for the current process/session and is never force-persistent.
On disable, load, cell/worldspace change, stale/invalid OOT data, peer loss, or
shutdown, pose application stops immediately and the owned reference is retired.
Runtime validation loads an existing save only as a disposable starting point
and exits without saving. This is the hard safety boundary because the pinned
public API does not establish a verified general-purpose delete call. Zelrim
does not create a test save or write to a user save.

## Timing and failure behavior

Shipwright continues publishing from `OnPlayerUpdate` with a zero-wait mutex and
one latest pending sample. Skyrim schedules at most one game-thread task. A task
copies bridge state with a zero-wait lock, validates it, and performs at most one
placement or pose application. Logging and formatting occur after the copy.
There is no queue, interpolation, extrapolation, or catch-up burst.

The follower accepts only a live peer and a complete, fresh, playable Link sample.
It additionally requires usable Skyrim context and an unchanged Skyrim
cell/worldspace plus unchanged OOT scene/room. Sequence must advance within the
same publisher session. Duplicate samples do no work; out-of-order samples,
non-finite results, and implausible one-sample jumps fail closed.

Pause, loading, either spatial-context change, stale telemetry, disconnect, and
either process restart disarm the calibration and retire the proxy. Recovery
requires a new usable sample and a new local origin; old bytes or a reused PID
cannot resume movement. Skyrim task starvation naturally becomes stale and then
timed-out. OOT pause publishes invalid context. Neither condition freezes an old
pose as authoritative.

## Input arrangement

Use one controller assigned only to Shipwright. Skyrim remains in a quiet,
isolated test cell with its window kept active only as needed for game-thread
tasks; do not bind or press Skyrim movement controls. Prefer distinct devices
(controller for OOT, keyboard only for Skyrim setup) and disable Steam Input or
other remappers that duplicate the controller into Skyrim. The Skyrim player is
only an origin/cell host and must remain stationary during a measurement run.

## Delivery and validation

1. Add a protocol-independent, deterministic movement-proof state machine and
   tests for calibration, movement, turn wrap, duplicate/out-of-order samples,
   pause/load/cell/scene barriers, stale/disconnect behavior, jumps, and sessions.
2. Wire the opt-in SKSE placement/move path on the bounded game-thread task and
   log creation, identity, pose sequence/age, reset reason, and retirement.
3. Preserve protocol v3: existing telemetry is sufficient and adding a command
   channel would broaden the proof unnecessarily.
4. Build and run Debug and Release suites, then rebuild the pinned Shipwright
   target and stage only Zelrim-owned artifacts.
5. In a disposable save and isolated cell, record matching OOT sequence and proxy
   observations for forward/lateral movement, quarter turns and wrap, pauses,
   loading/cell barriers, stale data, and restarting each process. Restore any
   temporary focus/input settings byte-for-byte and do not save.

Completion requires automated checks plus controlled real-runtime evidence that
movement and turning are OOT-authored, work is bounded, and every failure path
stops safely. Only then may the roadmap mark milestone 4 complete and recommend
milestone 5.
