# Milestone 1: Windows shared-memory bridge

## Scope and authority

Skyrim remains authoritative over its world, NPCs, quests, interactions, and
rendering. OOT will eventually own Link's movement, actions, combat, targeting,
inventory, and other Link-specific gameplay. The bridge translates data between
the engines; it must not recreate either engine's mechanics.

This milestone only establishes presence and liveness between two standalone
Windows processes. There is no rendering, collision, Link asset, combat, input,
SKSE, or OOT integration. `../oot` is an N64 decompilation, explicitly not a PC
port; the OOT endpoint here represents a future Windows host adapter, not an
N64 binary that can call Win32 APIs. Selecting that adapter is later work.

## Reference study

The reference repositories are read-only. SkyCraft's
`protocol/skycraft_protocol.h` defines fixed-width little-endian fields, a named
mapping, both PIDs and millisecond heartbeats. Its `skse/src/Link.cpp` makes Skyrim
the mapping creator, reuses mappings retained by a peer, and checks a 3000 ms
heartbeat timeout using `GetTickCount64`. `docs/DESIGN.md`, especially sections 1
and 10, establishes engine authority and the translation-only IPC boundary.

Zelrim adopts those lifecycle ideas with an independent protocol. SkyCraft's
large data regions, rings, seqlocks and rendering machinery are unnecessary here.
A bounded named mutex protects our entire small header, including initialization,
so readers cannot see torn 64-bit timestamps or a partially published header.

## Wire contract

`protocol/bridge_protocol.h` is the single source of truth. Mapping name:
`Local\Zelrim_v1`. Mapping size: exactly 64 requested bytes. Version: 1. All
integers are unsigned little-endian. Compile-time checks enforce every offset,
total size, alignment, and native little-endian execution. There are no pointers,
handles, compiler-dependent enums, or C++ atomic objects in shared memory.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Magic 0x4D524C5A (bytes `ZLRM`) |
| 4 | 4 | Protocol version, 1 |
| 8 | 4 | Byte size, 64 |
| 12 | 4 | Reserved, zero |
| 16 | 4 | Skyrim PID; zero when detached |
| 20 | 4 | OOT-side PID; zero when detached |
| 24 | 8 | Skyrim heartbeat, `GetTickCount64()` milliseconds |
| 32 | 8 | OOT heartbeat, same clock |
| 40 | 24 | Reserved, zero |

Timestamps are monotonic host uptime, not Unix time. PID zero is the presence
sentinel; timestamp zero can be valid at boot. A nonzero PID is connected only
while its timestamp is not in the future and is less than 3000 ms old. PIDs are
diagnostic identifiers, not proof of process identity or authority to take over.
Incompatible magic, version or size is an error and is never silently reset.
Changing the wire layout requires a version change and coordinated adapters.

## Synchronization and lifecycle

All participants must acquire `Local\Zelrim_v1_data` before any mapped access.
Each acquisition waits at most 100 ms. `WAIT_ABANDONED` grants ownership and the
header is validated before use. A partially initialized incompatible header is
rejected; close all holders before retrying with a fresh mapping.

Each endpoint reserves its side by creating a named mutex object:
`Local\Zelrim_v1_skyrim_owner` or `Local\Zelrim_v1_oot_owner`. These are lifetime
tokens, never acquired as locks. `ERROR_ALREADY_EXISTS` rejects duplicates,
including duplicates in the same process. Only the endpoint holds its token;
Windows destroys it after its last handle closes, including after process death.
A hung endpoint retains its role. Stale heartbeat alone never permits takeover.

1. Skyrim creates a page-file-backed mapping and initializes a new header under
   the data mutex. OOT opens the mapping, retrying if Skyrim has not started.
2. Both validate the header, then publish their own PID and initial timestamp.
   On reconnect, each replaces only its own fields. A surviving peer can retain
   the mapping across the other's restart.
3. The caller invokes `Bridge::tick()` periodically. Standalone programs use
   250 ms intervals. Each tick publishes a heartbeat and returns a consistent
   snapshot and peer state: connected, disconnected, timed-out, or unavailable.
   A mutex timeout is unavailable, never connected based on an old snapshot.
4. Normal shutdown clears only the local PID and timestamp, unmaps, and closes
   handles. The other side sees disconnected on its next successful poll.
   Ctrl+C/Ctrl+Break request this path; forced termination or console closure
   may instead require heartbeat timeout.
5. A crash or stall becomes timed-out after 3000 ms from the last heartbeat,
   plus polling/scheduling delay. A restarted or resumed peer becomes connected
   after its next heartbeat. Neither endpoint exits just because its peer leaves.
6. If shutdown cannot acquire the mutex, it closes handles anyway and lets the
   peer use the timeout path. Windows releases the mapping after all handles and
   views close. No persistent shared-memory file needs deletion.

`Bridge` is non-copyable and must be called serially by its owner. There is no
background worker. `connect()` returns false while waiting for the mapping or
data mutex; Win32 and compatibility failures throw. `disconnect()` is idempotent;
the role token lasts until object destruction. Integration should treat every
state except connected as unavailable and choose its own safe game behavior.
This milestone only reports state; it does not alter either game.

The `Local\` namespace restricts discovery to the Windows session. Default
Windows object security applies; run endpoints under the same user/session and
compatible elevation. This is cooperative local IPC, not a security boundary.

## Build and run

Use Windows with Visual Studio's C++ desktop tools and CMake 3.20 or newer.
From a developer PowerShell in `zelrim`:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

In two separate terminals, in either order:

```powershell
.\build\Debug\skyrim_bridge_test.exe
.\build\Debug\oot_bridge_test.exe
```

Use Ctrl+C to detach, or add `--seconds 10` to stop normally after ten seconds.
Output includes the local PID, peer-state transitions, both PIDs and heartbeat
snapshots. Start OOT first to observe unavailable until Skyrim creates the mapping.
Stop either side normally to observe disconnected; terminate it through Task
Manager to observe timed-out within roughly 3.25 seconds of its last heartbeat.
Restart it while the other remains running to observe connected again.

## Automated verification

Run CTest with no standalone endpoints running: tests intentionally use the real
fixed mapping name and reserve both roles. The test suite launches both supplied
standalone executables as separate hidden Windows processes. It checks both
startup orders, distinct published PIDs, advancing heartbeats, duplicate rejection,
clean disconnect, forced termination, restart with a surviving mapping, rejection
of incorrect magic/version/size without modifying the mapping, bounded lock
contention, abandoned-mutex recovery, and live-but-stalled heartbeat recovery.
The same header's static assertions verify the ABI during every build.

Verified on 2026-09-30 with MSVC 19.51, Windows SDK 10.0.26100.0, and an x64
Debug build: compilation succeeded and CTest passed (1/1, approximately 15 seconds).
Neither reference repository had working-tree changes after implementation.
