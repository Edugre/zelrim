# Completed transport foundation

## Boundary

This implements Windows presence/liveness IPC between real Skyrim and Shipwright
processes using `Local\Zelrim_v1`. It carries only protocol identification, PIDs,
and heartbeat timestamps. No movement, collision, combat, targeting, inventory,
input forwarding, or rendering integration is included. Skyrim retains authority
over its world/rendering; OOT will retain authority over Link's gameplay.

See [milestone 1](milestone-01.md) for the unchanged fixed 64-byte wire layout.

## Components

* `bridge/`: reusable Windows mapping, role reservation, synchronized heartbeat,
  compatibility validation, and bounded disconnect handling.
* `integrations/shipwright/`: opt-in start/frame/shutdown adapter, exact upstream
  pins, and a reproducible two-file patch. No gameplay mechanics are changed.
* `integrations/skse/`: `Zelrim.dll`, using the official public SKSE interfaces.
  It deliberately supports **Skyrim 1.7.104 / SKSE 2.3.1** only. Older runtimes
  have not been validated and are rejected, rather than advertised as compatible.
* `bridge_monitor.exe`: passive observer. It claims neither endpoint role and
  writes no shared fields. It samples once per second; `--seconds N` controls
  duration. Exit code 0 means both sides were observed live at least once, 1
  means they were not, and 2 means invalid arguments. Inspect all samples to
  establish sustained liveness or a disconnect transition.

## Skyrim adapter lifetime

On plugin load, validate the runtime and required SKSE task/messaging interfaces,
register for `DataLoaded`, and prepare a Windows thread-pool timer. `DataLoaded`
starts that timer at 250 ms intervals. It only schedules tasks: **only a task
executed on Skyrim's game thread creates the mapping or publishes a heartbeat**.
At most one task can be pending, preventing an unbounded queue during pauses.

The plugin uses no engine structures, Address Library offsets, trampolines,
input hooks, rendering hooks, or CommonLib dependency. Its minimal TaskDelegate
declaration matches the pinned official SKSE header. Bridge failures are caught
and disable transport for that process; peer loss alone does not disable it.
The DLL is pinned for process lifetime so queued callbacks cannot target unloaded
code. A diagnostic `Zelrim_IsRunning` export reports whether scheduling remains
enabled; it is not proof of a live peer.

`Zelrim_Shutdown`, called on the game thread by the test host, cancels and drains
timer callbacks before disconnecting. Queued SKSE tasks subsequently become
no-ops and dispose themselves. SKSE has no public process-shutdown message.
For an ordinary DLL process-detach notification, cleanup attempts a **zero-wait**
disconnect: no thread joins, logging, or waiting under loader lock. This path is
best-effort; if unavailable, heartbeat expiration remains authoritative.

**Observed runtime behavior:** quitting Skyrim from its main menu left the last
PID/timestamp in shared memory. Shipwright reported timed-out after heartbeat
expiry. Do not assume Skyrim's normal quit calls DLL detach or clears its slot.
The standalone host does exercise successful explicit and process-detach clears.
Shipwright's normal window close runs `DeinitOTR` and clears its PID immediately.

Logs default to `%LOCALAPPDATA%\Zelrim\ZelrimSKSE.log`; set `ZELRIM_LOG_PATH` on
the launching process to choose a test log. Shipwright logs through its own logger.

## Build and deployment

The tested toolchain is VS 2026 / MSVC 19.51, Windows SDK 10.0.26100.0, x64.
Shipwright's documented v143 toolset was not installed; the VS 2026 build succeeded.
Source revisions live in the two integration `upstream.json` manifests. The full
game build used vcpkg revision `a42757564758c60651ebc616d9b000b7b91249ef`.
Upstream vcpkg bootstrapping updates dependencies, so this is a recorded build,
not a promise of bit-for-bit dependency reproducibility.

From a developer PowerShell in Zelrim, with no bridge/game processes running:

```powershell
# Fetch pinned source if absent, build the plugin/hosts, and run the release tests.
.\tools\Build-Transport.ps1 -SkipShipwright

# Also build and stage the full game, using your existing extracted OOT archive.
.\tools\Build-Transport.ps1 -OotArchive 'C:\path\to\oot.o2r'

# Install only the new DLL; refuses to overwrite a different existing DLL.
.\tools\Install-SksePlugin.ps1 -SkyrimDirectory 'C:\path\to\Skyrim Special Edition'
```

Pass `-CMake 'C:\path\to\cmake.exe'` if automatic discovery is unsuitable.
The full game is staged in `build/runtime-shipwright`, with its generated
`soh.o2r` and required `assets/` directory. The Windows port uses its working
directory for portable runtime files. Launch from that directory, keeping it
separate from the user's installed game and saves:

```powershell
Push-Location build/runtime-shipwright
.\soh.exe
Pop-Location
```

Launch Skyrim using its existing `skse64_loader.exe`, then observe from Zelrim:

```powershell
.\build\Release\bridge_monitor.exe --seconds 15
```

For the initial deployment on this machine, only `Data/SKSE/Plugins/Zelrim.dll`
was added to the installed Skyrim directory. To uninstall, exit Skyrim and remove
that file. No ESP, save changes, or additional runtime files are required by the
plugin. The installed `../Shipwright` distribution is unchanged; the development
game and its generated settings/saves are entirely under ignored `build/`.

Skyrim normally pauses its game loop when unfocused/minimized, which correctly
causes this adapter's heartbeat to expire. For simultaneous runtime testing we
temporarily set `[General] bAlwaysActive=1` in Skyrim.ini, backed up the original,
then restored it **byte-for-byte** after closing Skyrim. Future simultaneous
tests need that setting or equivalent focus management; transport does not
change it automatically. Long loads and debugger pauses may also time out.

## Validation evidence (2026-09-30)

The instrumented full Shipwright executable and resource archive built successfully.
Skyrim loaded the release DLL through real SKSE and reached its main menu.
Shipwright reached its title/attract screen using a private copy of the existing
OOT archive. No user save was loaded.

| Check | Evidence / outcome |
| --- | --- |
| Both real processes live | Skyrim 18888 + Shipwright 12888; both timestamps advanced over successive samples |
| OOT normal close | OOT PID and heartbeat became zero; Skyrim reported disconnected |
| OOT restart | New PID 26116 attached to Skyrim's surviving mapping |
| OOT forced termination | Timestamp stopped at 342037093; at 342040187, `ootAlive=0` |
| OOT crash recovery | New PID 4160 attached and both heartbeats advanced |
| Skyrim menu quit | User quit from main menu; Shipwright reported timed-out, with stale PID 18888 retained |
| Skyrim restart | New PID 6072 attached to surviving Shipwright 4160; both timestamps advanced |
| Skyrim forced termination | Timestamp stopped at 342327000; at 342330062, `skyrimAlive=0` |
| Final cleanup | Both game processes closed; original Skyrim.ini hash matched its backup |

All timestamps in the table are host uptime milliseconds, not wall-clock dates.
Raw monitor logs are in ignored `build/runtime-evidence/`; build output is in
`build/shipwright-build.log`. Standalone tests additionally cover both startup
orders, duplicate roles, version/magic/size mismatch, mutex contention/abandonment,
stalls, restarts, and endpoint cleanup. SKSE-specific tests load the actual DLL,
reject incompatible API/runtime inputs, prove that a timer cannot publish without
the game thread, bound the pending queue, and verify shutdown with a queued task.

The bridge test, Shipwright adapter test, SKSE task test, and combined adapter
lifecycle test passed in both x64 Debug (4/4) and Release (4/4) builds. The final
Release run took 50.85 seconds and is recorded in
`build/transport-release-validation.log`. These checks establish the transport
foundation, not gameplay interoperability or frame-time guarantees.
