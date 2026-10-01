# Shipwright heartbeat adapter

The full game build and real-process validation are now complete. See
[transport foundation](transport-foundation.md) for the tested VS 2026 build,
runtime staging, Skyrim adapter, and validation evidence. The v143 commands below
remain the upstream-documented alternative when that toolset is installed.

The source checkout is `external/Shipwright`, separate from the user's installed
game at `../Shipwright`. `external/` is ignored by Zelrim Git; the integration
sources, upstream revision manifest, and reproducible patch live under
`integrations/shipwright/`. No changes are needed in SkyCraft or the OOT reference.

## Source revision and preparation

The initial checkout uses Shipwright commit
`ff0209e76f01cf816806cd1d8521373744d1dc8e` (project version 9.2.3), with its
libultraship and Torch submodules. `upstream.json` records all three exact commits.
The integration patch is already applied to this workspace's source checkout.

To reproduce from a fresh Zelrim checkout, run from a developer PowerShell:

```powershell
git clone --recurse-submodules https://github.com/HarbourMasters/Shipwright.git external/Shipwright
$pin = Get-Content integrations/shipwright/upstream.json -Raw | ConvertFrom-Json
git -C external/Shipwright checkout --detach $pin.commit
git -C external/Shipwright submodule update --init --recursive
.\tools\Prepare-Shipwright.ps1
```

The preparation script verifies the pinned revisions and applies the patch only
if it applies cleanly. Running it again recognizes the existing patch. It never
resets local changes. Updating Shipwright requires reviewing the hooks and pin.

## Lifecycle

The patch modifies only `soh/CMakeLists.txt` and `soh/soh/OTRGlobals.cpp`:

| Hook | Action |
| --- | --- |
| End of `InitOTR` | Reserve the OOT role and attempt the first heartbeat |
| Start of `Graph_StartFrame` | Retry attachment / publish heartbeat at most every 250 ms |
| Start of `DeinitOTR` | Clear OOT presence and release the bridge before logging teardown |

`Graph_StartFrame` is called by the main game-state loop, including its normal
non-gameplay states. Using this callback adds no rendering or input behavior.
There is no worker thread: if the loop stalls or stops being scheduled, its
heartbeat expires. Long loads, debugger pauses, and blocking dialogs can therefore
produce a timeout. Connected means the adapter is ticking, not that a save is
loaded or Link is ready for future gameplay commands.

The adapter shares the unchanged milestone-1 protocol (`Local\Zelrim_v1`). Its
OOT PID is the actual `soh.exe` process ID. Logs use Shipwright's `SPDLOG_INFO`
and include peer-state changes and PID changes. Missing Skyrim is retried.
Protocol, duplicate-role, and Win32 errors disable the adapter until restart;
exceptions do not escape into the game. Restart means a new game run, or explicit
`stop()` / `start()` calls by a future integration. No UI toggle is implemented.
Ordinary peer loss does not disable the adapter or alter gameplay.

Mapped operations retain milestone 1's bounded mutex waits (100 ms per lock,
up to two locks on initial connection). This is a minimal liveness integration,
not a guarantee of frame-time performance for future gameplay transport.

## Build the instrumented game

Follow the pinned checkout's `docs/BUILDING.md` prerequisites: Visual Studio 2022
C++ tools including v143, a Windows SDK, Python 3, Git, and CMake 3.26 or newer.
The upstream configuration downloads/builds dependencies through vcpkg and other
upstream build steps. Keep its vcpkg installation local to this build directory.

From the Zelrim root, with `cmake` on PATH:

```powershell
$adapter = (Resolve-Path integrations/shipwright/zelrim.cmake).Path
$vcpkg = [IO.Path]::GetFullPath((Join-Path $PWD 'build/shipwright/vcpkg'))
cmake -S external/Shipwright -B build/shipwright -G 'Visual Studio 17 2022' -T v143 -A x64 "-DZELRIM_INTEGRATION_FILE=$adapter" "-DVCPKG_ROOT=$vcpkg"
cmake --build build/shipwright --config Debug --target GenerateSohOtr
cmake --build build/shipwright --config Debug --target soh
```

Without `ZELRIM_INTEGRATION_FILE`, the patched checkout builds without the adapter.
With it, the adapter and bridge compile directly into `soh`, inheriting its CRT
and compiler settings. The supplied installed `soh.exe` has not been replaced.

Launch the newly built executable from its own build/runtime directory following
upstream's asset setup. Use a supported game dump to generate the appropriate
assets. Keep this development run's settings and saves separate from the installed
game. In another terminal start `build/Debug/skyrim_bridge_test.exe`. Do not run
`oot_bridge_test.exe` at the same time: it owns the same OOT role as the adapter.

The Skyrim test should report an OOT PID matching the instrumented game. Exit the
game to see disconnected, or terminate it to see timed-out. Restart either side
while the other remains running to verify reconnection. The installed unmodified
game cannot participate in this protocol.

## Verification and limits

The Zelrim build includes `shipwright_adapter_host.exe`, a small process that
calls the actual adapter's start/tick/stop functions through the same CMake module
used by the real game. CTest runs the bridge lifecycle suite against this host as
well as the original standalone OOT endpoint. This validates Windows IPC and
adapter behavior without requiring game assets.

Verified on 2026-09-30: MSVC x64 Debug build succeeded; both CTest lifecycle suites
passed (2/2, about 31 seconds total). The patch passes `git diff --check` and
reverse-application validation; rerunning the preparation script is idempotent.

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Subsequent full-build validation succeeded on VS 2026. The development executable
connected to the real Skyrim SKSE plugin, cleared its slot on normal window close,
and recovered after forced termination. Abnormal exits retain the heartbeat-timeout
fallback. See the transport foundation document for evidence and limits.
