# Milestone 3: read-only Skyrim world telemetry

Implementation status: complete on 2026-10-01. Automated Debug/Release validation
and the pinned Shipwright Release build pass. Real Skyrim sampling and the
coordinate measurement matrix remain to be performed; no unobserved game-runtime
result is claimed.

## Boundary and authority

The Skyrim endpoint is the sole writer and source of truth for Skyrim world
telemetry. OOT and the passive monitor only copy and validate it. This milestone
does not move actors, forward input, exchange collision, render assets, control a
camera, or change gameplay. Skyrim retains authority over its world, NPCs, quests,
interactions, and rendering; OOT retains authority over Link-specific gameplay.

No source or installed file under `../SkyCraft`, `../oot`, the installed Skyrim
directory, or the installed Shipwright distribution was changed. No user save was
opened or modified. All code and build integration changes are in Zelrim.

## Protocol v3

The mapping family remains `Local\Zelrim_v1`; version and exact byte size determine
compatibility. Version 3 retains the 64-byte base header and 120-byte Link record
at their v2 offsets, then appends a 96-byte, 8-byte-aligned Skyrim record for 280
bytes total. Compile-time assertions fix all material offsets and sizes.

Each Skyrim attachment creates a new opaque 128-bit session. Every record advances
a session-local 64-bit sequence and carries capture uptime plus matching begin/end
publication markers. Clean detach clears the Skyrim presence and record; heartbeat
expiry rejects retained bytes after a crash. Abandoned-mutex recovery discards the
opposite writer's possibly incomplete record before publishing a new complete one.

The record carries raw player position and rotation components, player/cell/runtime
worldspace form IDs, validity bits, and an invalidation reason. Worldspace ID zero
is meaningful for an interior/no-worldspace cell. Runtime form IDs identify forms
under the current load order; they are not portable persistent identifiers.

A record is usable only when the Skyrim process is live, the publication is
complete, the sample is under 1000 ms old, all floats are finite, and context says
playable/player/cell present with no load, pause, or transition. A valid zero
position remains distinct from invalid data.

## Capture and lifecycle

The pinned Skyrim 1.7.104 / SKSE 2.3.1 plugin reads named fields on the existing
bounded game-thread task: `g_thePlayer`, `TESObjectREFR::pos`, `rot`, `parentCell`,
`TESForm::formID`, and `TESObjectCELL::worldSpace`. It retains no engine pointer.
SKSE load messages drive a testable context state machine. Menu open/close events
track menus whose pinned `IMenu` flags say they pause the game.

Contract behavior is:

- Startup/main menu is `NoPlayableContext`.
- Pre-load is invalid `Loading`.
- Successful load/new game requires one invalid settling publication after a
  player and cell first appear, then may become usable.
- Failed load, missing player, and missing cell remain separately invalid.
- A cell or worldspace identity change emits one invalid transition barrier before
  the first usable record in the new identity.
- A detected pause emits invalid `Paused`. If Skyrim stops servicing tasks, the
  prior sample instead becomes stale and then the Skyrim heartbeat times out.
- Disconnect/restart cannot reuse cached data because liveness, freshness, and a
  new session are all required.

The standalone Skyrim endpoint publishes a changing coherent synthetic pattern.
The passive monitor reports Skyrim and Link sessions, sequences, ages, validity,
reasons, identities, positions, and orientations in the same copied header.

## OOT and Skyrim coordinate conventions

### Verified from pinned source and wire contracts

- OOT position is copied without conversion from `Player.actor.world.pos.x/y/z`.
  Its scale is raw OOT scene units.
- OOT world yaw and shape yaw are distinct signed 16-bit binary angles. One full
  turn is 65536 units. The implementation does not infer pitch or roll.
- OOT context is identified by scene and room; gameplay frame is not a spatial
  identity or publication order.
- Skyrim position and orientation are copied without conversion from the pinned
  `TESObjectREFR::pos.x/y/z` and `rot.x/y/z` named float members.
- Skyrim spatial context is identified by parent cell and optional worldspace
  runtime form IDs. A cell/worldspace change is an explicit discontinuity barrier.
- Neither wire record contains an origin, cell transform, worldspace north
  rotation, or cross-game scale. The protocol cannot justify a cross-game transform.

### Measurements completed in this work

Automated tests verify binary preservation of positive, negative, and zero float
components; valid interior worldspace ID zero; session/sequence ordering; 100
changing six-component patterns without mixed-field reads; and cell/worldspace
transition barriers. These are transport/coherence measurements, not measurements
of either game's physical scale, axis direction, or origin.

No real-game axis or unit measurement was performed in this session. In
particular, the work does **not** claim which horizontal component is north/east,
the sign of yaw, the zero-facing direction, the handedness of either basis, a
Skyrim-units-to-OOT-units ratio, or equivalence of Euler and binary-angle axes.

### Assumptions deliberately not adopted

Common informal claims such as a fixed real-world length per Skyrim unit, a
particular Skyrim `Z`-up basis, or an OOT axis/yaw convention are hypotheses until
reproduced against these pinned runtimes. Even if their axis directions and unit
ratios are measured, absolute coordinates must not be aligned directly:

- OOT scenes/rooms can have unrelated local origins.
- Skyrim interior cells need not share an exterior worldspace origin.
- Separate Skyrim worldspaces may use unrelated frames or north rotations.
- Cell/scene transitions can introduce discontinuities independently in either
  engine.

Future movement work therefore needs a context-keyed calibration/transform (and
possibly portal/transition mappings), not one global affine transform.

## Validation performed

The implementation was configured and built with VS 2026 / MSVC 19.51 and Windows
SDK 10.0.26100.0 against the pinned manifests.

- Debug build: succeeded; all 4 CTest suites passed in 53.95 seconds.
- Release build: succeeded; all 4 CTest suites passed in 53.89 seconds.
- Pinned Shipwright Release `soh` target: compiled and linked successfully with
  protocol v3; its post-build asset copy completed successfully.
- The actual SKSE plugin DLL was loaded by the test host. Tests cover API/runtime
  rejection, bounded scheduling, no publication before a game-thread task, stall
  timeout/recovery, and clean shutdown. A test-only explicit gate prevents runtime
  relocation reads outside Skyrim.
- Bridge tests cover exact ABI assertions, mixed/malformed protocol rejection,
  writer ownership, initial invalidation, finite-value rejection, raw identity and
  value preservation, coherent changing patterns, load/settle/pause/cell
  transitions, liveness, restart sessions, contention, abandonment, and existing
  Link telemetry regressions.
- A Release standalone run launched both synthetic endpoints and observed three
  successive monitor samples. Both telemetry records were usable; independent
  sessions and sequences advanced, Link's component offsets remained `(n,n+1,n+2)`,
  and Skyrim's remained `(n,n+10,n+20)` with stable player/cell/worldspace IDs.

## Remaining validation and limitations

Run the release DLL in the pinned Skyrim 1.7.104 runtime with a disposable
development save and compare the same monitor/plugin sequence while stationary,
moving along controlled axes, moving vertically, rotating in quarter turns,
opening/closing pausing menus, loading, crossing exterior cells, entering an
interior, changing worldspaces, quitting, crashing, and restarting. Record raw
deltas with cell/worldspace identities. Repeat corresponding controlled OOT
measurements per scene/room before proposing any conversion.

The production player/menu relocation access has compiled against the pinned
headers but was not exercised in the real executable during this session. Pause
events that occur while Skyrim services no game-thread tasks can only be observed
as stale/timeout, not as an immediately published pause reason. Form IDs can change
with load order. No coordinate conversion exists, by design.
