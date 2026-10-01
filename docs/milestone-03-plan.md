# Milestone 3 plan: read-only Skyrim world telemetry

Status: implemented 2026-10-01. This plan was written before implementation; see
[the implementation and validation record](milestone-03.md).

## Outcome and boundary

The Skyrim endpoint publishes coherent, read-only snapshots of the player
reference: raw position, raw Euler orientation, parent cell form ID, parent
worldspace form ID when present, and explicit context/transition validity. The
standalone monitor reports both Skyrim and Link telemetry without changing either
game.

Skyrim remains authoritative over its world, NPCs, quests, interactions, and
rendering. OOT remains authoritative over Link-specific gameplay. This milestone
adds no actor movement, input forwarding, collision exchange, rendering, camera
control, quest or interaction calls, coordinate conversion, or gameplay changes.
No installed game file or user save is modified by the implementation or tests.

## Pinned source basis

Implementation targets Skyrim runtime 1.7.104 and SKSE 2.3.1 at commit
`25b72352adb6543fa6d0bd3795780672b2e238e0`, as recorded in
`integrations/skse/upstream.json`.

The pinned public SKSE declarations provide the named fields used by capture:

- `g_thePlayer` in `skse64/GameAPI.h` is the runtime player pointer.
- `TESObjectREFR::pos` and `TESObjectREFR::rot` in
  `skse64/GameReferences.h` are three-component float values.
- `TESObjectREFR::parentCell` identifies the current cell.
- `TESForm::formID` supplies stable runtime form identity within the current
  load order.
- `TESObjectCELL::worldSpace` supplies the exterior worldspace when present.
- `MenuOpenCloseEvent` and `IMenu::kFlag_PausesGame` provide pause context.
- SKSE `PreLoadGame`, `PostLoadGame`, and `NewGame` messages delimit load/new-game
  context. `DataLoaded` only means forms are available; it is not a playable save.

All engine reads occur in the existing SKSE game-thread task. Engine pointers are
copied into a local value and never retained. Integration-specific source and any
required upstream build shim remain in Zelrim.

## Protocol v3

Keep `Local\Zelrim_v1` as the mapping-family name and preserve the v2 header and
Link record offsets. Advance the protocol version to **3** and append one fixed,
8-byte-aligned Skyrim telemetry record. Version and exact byte size continue to
control compatibility; mixed versions fail closed.

The Skyrim record contains:

- a new opaque 128-bit session ID for each Skyrim bridge attachment;
- a session-local 64-bit publication sequence and capture uptime;
- matching begin/end publication markers;
- context bits and an invalidation reason;
- player reference form ID, parent cell form ID, and parent worldspace form ID
  (`0` explicitly means no worldspace, as for an interior cell);
- raw `pos.x/y/z` and raw `rot.x/y/z` floats.

No pointers, names, plugin filenames, native enums, containers, or inferred global
coordinates cross IPC. Runtime form IDs are load-order-dependent identities, not
portable persistent IDs.

Only the Skyrim endpoint writes this region. It publishes while holding the
existing data mutex in its bounded game-thread heartbeat task. Readers copy the
entire mapping under the mutex and classify the local copy afterward. Matching
markers protect against incomplete/abandoned publications, as for Link telemetry.

## Context and lifecycle contract

A usable sample requires all of the following independently:

1. the Skyrim heartbeat is live (less than the existing 3-second timeout);
2. the Skyrim record is complete and newer than any abandoned publication;
3. capture age is below the 1-second telemetry freshness threshold;
4. a playable load/new-game context is established;
5. the player and parent cell are present;
6. loading, a cell/worldspace transition barrier, and pause are absent;
7. all position and rotation values are finite.

Behavior is explicit:

- **Startup/main menu:** publish a new session with invalid `NoPlayableContext`.
- **Pre-load/new game:** publish invalid `Loading` and clear cached identity.
- **Post-load success:** enter a settling state; do not claim valid context until a
  game-thread capture finds a player and parent cell.
- **Post-load failure:** remain invalid `NoPlayableContext`.
- **Player/cell absent:** publish invalid `NoPlayer` or `NoCell` as applicable.
- **Cell/worldspace identity change:** publish one invalid
  `CellOrWorldspaceChanged` barrier before any valid sample with the new identity.
  This makes coordinate discontinuities and interior/exterior changes observable.
- **Pause:** publish invalid paused context when the task runs. If the engine stops
  servicing tasks, the last record becomes stale and the heartbeat can time out;
  readers still fail closed and never relabel the last position as current.
- **Clean disconnect:** clear Skyrim presence and its telemetry record together.
- **Crash/forced exit:** retained bytes are unusable after heartbeat expiry.
- **Restart:** a new session begins invalid; sequence/PID reuse cannot revive data.

Context changes supersede ordinary valid samples. Readers clear cached usable data
on session change, invalid context, staleness, disconnect, timeout, protocol error,
or abandoned publication. There is no interpolation or extrapolation.

## Coordinate-convention investigation

The implementation transports raw engine values only. Documentation will keep
three evidence classes separate:

1. **Verified from pinned source/contract:** field meanings and encodings directly
   established by named declarations or code.
2. **Measured in pinned runtimes:** observations recorded from controlled movements
   and rotations in disposable development saves/runtimes.
3. **Assumptions requiring validation:** common community conventions or candidate
   transforms that have not been reproduced here.

Measurements will record stationary baselines and controlled positive-axis moves,
vertical moves, and quarter-turns in at least one Skyrim exterior, one Skyrim
interior, and representative OOT scenes. They will compare deltas, not absolute
positions, and record the relevant cell/worldspace or scene/room identity. Unit
ratios, handedness, yaw direction/zero, pitch/roll ordering, and any north rotation
will be reported only if measured.

No shared origin is assumed. Skyrim interior cells and OOT scenes may each have
local origins; exterior worldspaces may have distinct frames. A single global
OOT-to-Skyrim transform is explicitly out of scope and must not be inferred from
one location.

## Delivery sequence

1. Extend the wire layout and bridge with Skyrim-only coherent publication,
   session identity, classification, and lifecycle clearing.
2. Add a testable Skyrim capture state machine for load, pause, missing context,
   cell/worldspace barriers, and finite-value validation.
3. Wire pinned SKSE player/menu/message sources to that state machine on the game
   thread without hooks, writes, or retained engine pointers.
4. Extend the standalone Skyrim endpoint and monitor with a changing synthetic
   pattern and simultaneous Link/Skyrim status output.
5. Add ABI, ownership, coherence, invalidation, freshness, pause, transition,
   disconnect, restart, contention, and regression tests.
6. Build and run Debug and Release suites, build the pinned Release integration,
   and document automated versus real-runtime evidence and remaining limitations.

## Acceptance criteria

- Monitor samples identify coherent Skyrim session/sequence, position, rotation,
  cell, and worldspace values while independently reporting liveness, freshness,
  and context validity.
- Loading, pause, cell/worldspace changes, disconnect, and restart cannot expose an
  old position as usable.
- Synthetic tests exercise changing values and identity transitions without torn
  mixed-field records; existing Link telemetry and lifecycle suites still pass.
- The pinned SKSE plugin builds and uses only read-only game-thread access.
- Coordinate documentation clearly separates verified source facts, reproduced
  measurements, and assumptions, and rejects an unproven shared origin/global
  transform.
- Project code and reproducible integration changes remain in Zelrim; `../SkyCraft`,
  `../oot`, installed game files, and user saves remain unchanged.
