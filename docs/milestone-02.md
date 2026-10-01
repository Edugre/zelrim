# Milestone 2: read-only Link telemetry

Implementation status: complete on 2026-10-01. Automated validation and the full
pinned Shipwright Release build pass. The real-game observation matrix remains to
be run with a disposable development save; no unperformed runtime result is claimed.

## Boundary and authority

Shipwright is the sole writer and source of truth for Link telemetry. Skyrim and
the passive monitor only copy, validate, and log it. This milestone adds no
commands, input forwarding, coordinate conversion, Skyrim actor movement,
collision exchange, rendering, targeting, inventory, combat, or persistence.
Skyrim remains authoritative over its world; OOT remains authoritative over Link.

The sibling `SkyCraft` and `oot` repositories and the installed `Shipwright`
distribution were not changed. The ignored `external/Shipwright` checkout is the
pinned development source used to compile the reproducible Zelrim patch.

## Protocol v2

The mapping family remains `Local\Zelrim_v1`; compatibility is determined by the
header. Version 2 preserves every offset in the 64-byte v1 header and appends a
120-byte, 8-byte-aligned `LinkTelemetry` record, for 184 bytes total. Compile-time
assertions fix every material offset and size. Version and byte-size checks reject
mixed v1/v2 mappings.

Each OOT attachment creates a new opaque 128-bit session ID. Every publication,
including invalidation, advances a session-local 64-bit sequence and records
`GetTickCount64()` capture time. Matching begin/end markers make incomplete writes
unusable. A reader that observes an abandoned mutex rejects the current sequence
until OOT completes a newer publication.

A usable record requires a live OOT heartbeat, a complete publication, a known
validity combination, playable context plus player presence, no transition, finite
floats, no invalidation reason, and capture age below 1000 ms. Liveness, freshness,
and context validity are reported separately. Invalid records zero gameplay fields;
therefore a valid `(0,0,0)` position is not confused with missing data.

The payload contains scene and room IDs, gameplay frame, documented Link age
encoding, raw `Actor.world.pos`, separate world and shape yaw, raw actor velocity
and `speedXZ`, and raw `stateFlags1`, `stateFlags2`, and `bgCheckFlags`. Position
and velocity retain OOT engine units. Yaw is a signed binary angle with 65536 units
per turn. Frame counters may reset or wrap and are never used as publication order.

## Publication and lifecycle

The per-player-update path attempts the data mutex with a zero timeout. Contended
samples are dropped; the adapter retains at most one latest pending sample. A
pending invalidation cannot be displaced by a valid sample before it publishes.
Heartbeat publication remains on its separate 250 ms path.

Hooks are registered only in the pinned Shipwright runtime build. `OnLoadGame`
enables playable-save capture but first publishes a transition invalidation.
`OnPlayerUpdate` copies named engine members into one local value and submits it.
Title initialization, exit, scene initialization, play destruction, active
transition, and missing-player observations invalidate telemetry. `OnGameFrameUpdate`
retries pending publication and records a verified pause as invalid paused context,
but is never treated as evidence that Link advanced.
No engine pointer is retained between callbacks. Title/attract mode cannot become
valid because validity requires a preceding `OnLoadGame` event.

The monitor and SKSE task report unavailable, missing, invalid, stale, and usable
states. State/session changes log immediately; usable SKSE samples are limited to
once per second and include session, sequence, age, context, position, both yaw
values, and raw flags. Logging and formatting occur after the mutex-protected copy.

## Verification performed

Both x64 Debug and Release builds succeeded with MSVC 19.51 and Windows SDK
10.0.26100.0. All four CTest suites passed in each configuration. The final Debug
run completed in 53.90 seconds and the final Release run in 53.88 seconds.

Automated coverage includes existing endpoint/adaptor lifecycle behavior, v1/v2
version and size rejection, exact ABI assertions, attachment invalidation, valid
origin data, raw-field preservation, non-finite rejection, freshness expiry,
same-PID/new-session restart, zero-wait contention drops, abandoned-publication
rejection and recovery, disconnect/crash timeout, and bounded SKSE task scheduling.
Standalone OOT publishes a changing synthetic pattern for monitor/log observation.

The pinned Shipwright Release target compiled and linked successfully with
`telemetry.cpp`, `OTRGlobals.cpp`, the bridge, and heartbeat adapter. The preparation
script recognizes the updated reproducible patch as already applied. Reference
repository working trees were unchanged after the work.

## Remaining runtime verification

Run the development executable under `build/runtime-shipwright` with a disposable
save and the real SKSE plugin. For matching publication sequences, compare source
and consumer values while stationary, moving, turning, airborne, paused, changing
scenes, returning to title, and restarting each process. Exercise title attract
mode explicitly and record dropped samples/callback timing under forced mutex
contention. Preserve installed game files and user saves, and restore any temporary
focus settings byte-for-byte.

Until that matrix is recorded, the implementation is ready for runtime validation,
but milestone 3 should not start and real-game field agreement is not asserted.
