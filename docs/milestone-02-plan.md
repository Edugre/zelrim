# Milestone 2 plan: read-only Link telemetry

Status: planned; no telemetry implementation or gameplay integration yet.

## Outcome and scope

Shipwright publishes a coherent snapshot of Link after a player update. The
standalone monitor and Skyrim's existing SKSE task read that snapshot and report
it in logs. OOT remains the source of truth for every value. Skyrim continues to
own its world, NPCs, quests, interactions, and rendering.

This milestone adds no commands, input forwarding, Skyrim actor movement,
coordinate conversion, collision, assets, targeting, inventory, or combat.
Action diagnostics expose OOT's existing state flags; they do not reproduce an
action state machine. There is no overlay or rendering work.

## Source basis

Implementation targets the Shipwright revision in
`integrations/shipwright/upstream.json`:
`ff0209e76f01cf816806cd1d8521373744d1dc8e`. Paths below are relative to
the ignored `external/Shipwright` checkout:

- `soh/src/overlays/actors/ovl_player_actor/z_player.c:12322` invokes
  `GameInteractor_ExecuteOnPlayerUpdate` at the end of the player update.
- `soh/soh/Enhancements/game-interactor/GameInteractor_HookTable.h` exposes
  `OnPlayerUpdate`, `OnLoadGame`, `OnExitGame`, `OnSceneInit`, title hooks,
  and game-frame hooks. Use these existing extension points.
- `soh/include/z64actor.h` defines `Actor.world`, `velocity`, `speedXZ`,
  `bgCheckFlags`, and `shape`. Copy named members, never hard-coded offsets.
- `soh/include/z64player.h` defines `stateFlags1`, `stateFlags2`, and
  `actionFunc`. The last is a native function pointer and must never cross IPC.
  Its `linearVelocity` field can mean current or target speed depending on
  context, so it is excluded from the initial contract.
- `soh/include/z64.h` defines PlayState's scene, room, pause, frame, and
  transition context. `soh/src/code/game.c:357` dispatches the game-frame hook;
  that callback is not evidence that Link advanced one simulation step.

Before wiring hooks, trace their ordering through title/attract mode, save load,
scene transitions, player destruction, and exit. Presence of a Player alone is
insufficient to identify a playable save. Confirm the exact safe context checks
against this pinned source and test them; do not retain engine pointers between
callbacks. Integration implementation and reproducible patches stay in Zelrim.
The sibling SkyCraft and oot repositories remain untouched.

## Wire contract

Keep the required mapping name `Local\Zelrim_v1` and existing role/mutex names.
Advance the protocol version to **2**, preserve the 64-byte header's offsets,
and append one fixed-size Link snapshot in `protocol/bridge_protocol.h`.
The name identifies the mapping family; the header version and byte size govern
compatibility. Version 1 readers/writers must reject version 2 and vice versa.
Never repurpose reserved bytes silently or resize a live mapping. Upgrade all
endpoints and monitors together after closing every old mapping handle.

Define explicit offsets, padding, total size, and static assertions during the
contract step, before either adapter consumes it. All fields are little-endian
fixed-width integers or IEEE-754 binary32 floats, with compile-time assertions.
No pointers, handles, native enums, bools, containers, or packed unaligned atomics.

The initial snapshot carries:

- A 128-bit opaque publisher session ID, generated on each OOT adapter attachment;
  a 64-bit publication sequence; and a 64-bit capture uptime in milliseconds
  using `GetTickCount64`. Sequence is monotonic within a session and also advances
  for invalidation records. PID alone cannot identify a session because of reuse.
- Explicit context/validity bits and invalidation reason: no playable context,
  loading/transition, or no player. Record pause and cutscene context separately
  where verified. Unknown context is invalid, not guessed playable state.
- Signed scene and room IDs, OOT's gameplay frame counter, and Link age/form
  only if its source encoding can be documented unambiguously in the contract.
- Position XYZ from `actor.world.pos`; world yaw and shape yaw as separate
  signed 16-bit binary angles. One full turn is 65536 angle units. Positions
  stay in raw OOT scene units; no Skyrim scale, origin, or axis transform.
- Actor velocity XYZ and `speedXZ` as raw engine diagnostics, with their source
  semantics documented. Do not label them metres/second or derive displacement
  from them without verifying the engine's update/time conventions.
- Raw `stateFlags1`, `stateFlags2`, and `bgCheckFlags`. A log decoder may name
  verified constants from the pinned source. It must preserve unknown bits.
  A higher-level action enum and animation mapping are deferred.

Invalid records zero unavailable gameplay fields; validity always controls
interpretation. A valid origin position must remain distinguishable from invalid
data. Reject non-finite float payloads and unsupported validity combinations.
Frame counters may wrap or reset; they are not the publication sequence.

## Ownership, synchronization, and lifecycle

Skyrim initializes the whole mapping once. After initialization, only the OOT
endpoint writes the telemetry region. Skyrim and the monitor copy it under the
existing data mutex and process the local copy after releasing the mutex.
Do not log, allocate, call engine APIs, or format strings while holding it.

Build each snapshot from one game-thread callback. Attempt publication with a
zero-timeout mutex acquisition; skip a sample on contention. Retain only the
latest pending state, never a queue of old movement samples. Do not introduce
another 100 ms wait into each player update: the current heartbeat lock policy
must not be reused for per-update telemetry. Keep existing heartbeat behavior
separate and measure it during runtime validation.

On attachment, publish an invalid record with the new session before publishing
valid data. On scene/load/exit/player teardown, invalidate the local state before
any further capture and attempt immediate invalid publication. If contention
prevents it, retry from the game-frame hook; a pending invalidation supersedes
any older pending valid sample. Zero-wait publication cannot guarantee instantaneous
remote invalidation under contention, so freshness expiry is mandatory.

Clean detach clears telemetry validity together with the OOT presence fields
when the lock is available. On crash, the reader invalidates it through heartbeat
expiry. On mutex abandonment, treat telemetry as invalid until a subsequent
complete OOT publication; partial writes must never be accepted as a snapshot.
Add an explicit publication-complete marker if required to make this recovery
unambiguous, and test interruption during publication.

Readers independently track:

1. Process liveness using the existing 3-second heartbeat timeout.
2. Telemetry freshness using capture uptime, initially a documented 1-second
   threshold, adjustable for diagnostics without changing wire layout.
3. Context validity and session identity.

Only a live peer with a fresh, complete, valid record yields usable telemetry.
Never refresh capture time when rereading or republishing an old player sample.
Pause can leave the process alive while its last player sample becomes stale;
report that distinction. Clear cached usable data on session change, disconnect,
timeout, protocol error, or invalid context. No interpolation or extrapolation.

## Delivery sequence

1. **Contract and synthetic endpoints.** Finalize v2 layout and validity rules;
   extend Bridge with OOT-only publication and coherent snapshot reads. Extend
   standalone hosts and monitor first. Cover v1/v2 rejection in both directions,
   exact sizes/offsets, ownership, session changes, freshness, and abandoned writes.
2. **Shipwright capture.** Add a Zelrim-owned telemetry adapter and register the
   verified lifecycle/player hooks through the existing opt-in integration.
   Capture once per real player update, with bounded nonblocking publication.
   Keep engine access on the game thread and test context invalidation independently
   of the transport. Update the pinned reproducible patch as needed.
3. **Read-only consumers.** Extend the SKSE game-thread task and passive monitor.
   Log status transitions immediately, ordinary samples at most once per second.
   Include session, sequence, age, context, position, yaw, and raw flags. No new
   Skyrim engine structures or hooks are needed. Preserve bounded task scheduling.
4. **Automated validation.** Run Debug and Release suites, including existing
   lifecycle regressions. Add a synthetic changing pattern to detect torn records;
   contention/drop tests; pause/stale tests; invalid and non-finite payload tests;
   same-PID new-session tests; scene/frame reset tests; and crash/restart recovery.
   Test real capture-to-contract conversion rather than only duplicated fixtures.
5. **Real runtime verification and documentation.** Build both integrations and
   stage Shipwright privately. Use a disposable development save, preserving user
   saves. Compare captured source fields and consumer logs for the same sequence
   while stationary, moving, turning, airborne, paused, transitioning scenes,
   returning to title, and restarting either process. Test title attract mode
   explicitly. Record sample drops and callback timing under contention. Restore
   any temporary focus/settings changes and document evidence and limitations.

## Acceptance criteria

- Both standalone consumers and real Skyrim logs observe matching OOT position,
  both yaw values, and raw state flags from the same coherent snapshot.
- Invalid, stale, and disconnected states are visibly distinct; restart, scene
  change, and title/attract mode cannot silently reuse old usable telemetry.
- The telemetry path does not wait for the data mutex or accumulate samples.
  Existing transport lifecycle tests still pass in Debug and Release.
- The wire layout and source semantics are documented, with mixed-version
  rejection demonstrated. Real-game observations are recorded separately from
  synthetic test results.
- Skyrim's world and Link's mechanics are unchanged. This milestone supplies
  evidence for a later movement-authority design; it does not implement that design.
