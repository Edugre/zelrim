# Milestone 4: movement authority proof

Implementation status: the narrow movement-authority feasibility gate was verified
in a controlled two-game run on 2026-10-02. This is not movement against Skyrim
terrain; collision exchange and general presentation remain out of scope.

## What was implemented

The protocol remains version 3. Existing post-player-update Link telemetry is the
authority input. `MovementAuthority` pairs the first usable OOT sample with the
current usable Skyrim player sample, then maps OOT displacement to an absolute
proxy pose. It never generates velocity, integrates input, runs pathfinding, or
reimplements Link movement.

The opt-in SKSE path is enabled only by `ZELRIM_MOVEMENT_PROOF=1` plus explicit
`ZELRIM_PROXY_BASE_FORM_ID` and `ZELRIM_EXPECTED_CELL_FORM_ID`. It uses pinned
Skyrim 1.7.104 native relocations to
place one nonpersistent reference and move it on Skyrim's game thread. NPC/actor
bases are rejected. A static vanilla visual with no controller or useful physics
is required, because direct pose application must have no competing Skyrim AI,
character controller, or force simulation.

The returned runtime form ID is logged and is the proxy identity. The plugin never
adopts a pre-existing reference. On a safety reset it parks the reference 10,000
Skyrim units below the stationary player when still in the same cell, logs the
reason, and relinquishes it. The reference is not force-persistent, but the hard
cleanup boundary is still a disposable run that exits without saving: the pinned
public API did not establish a verified deletion call. Do not use a user save.

## Coordinates and timing

The runtime parameters are deliberately hypotheses:

* paired origins: first OOT position and current Skyrim player position;
* an optional fixed Skyrim X offset for a visible test proxy beside the player;
* candidate basis: OOT `(x,y,z)` to Skyrim `(x,z,y)`;
* configurable positive scale in Skyrim units per OOT unit;
* Skyrim anchor `rot.z` plus configurable-sign delta from initial OOT shape yaw;
* signed binary-angle wrap before conversion to radians.

These choices are not promoted to general coordinate facts. The final live
matrix below verifies the local horizontal basis, scale, and applied yaw for
this disposable run; it does not establish a terrain or collision transform.

Proof mode schedules at 16 ms with at most one pending SKSE task. Each task performs
one zero-wait bridge copy and at most one placement or move. Duplicate Link
sequences do no work. Normal read-only mode retains its 250 ms schedule. Skyrim
world telemetry still publishes at 250 ms in proof mode; no sample queue,
interpolation, extrapolation, or catch-up exists.

Pause/loading, stale or invalid telemetry, disconnect, OOT session restart,
scene/room change, Skyrim cell/worldspace change, sequence regression, nonfinite
output, and a configured per-sample jump limit all fail closed. The calibration
is discarded and recovery begins from a new paired origin. A zero-wait mutex miss
does no movement work and is retried by the next bounded task.

## Input and reproducible launch

Assign one controller only to Shipwright. Use keyboard only to place Skyrim in a
quiet isolated test cell, leave the Skyrim player stationary, and avoid Skyrim
movement bindings. Disable Steam Input/remappers that expose the same controller
to Skyrim. If Skyrim pauses while unfocused, use the already documented temporary
`bAlwaysActive=1` procedure and restore the original file byte-for-byte afterward.

After copying the matching Release DLL with the established installer, launch the
opt-in Skyrim side with:

```powershell
.\tools\Start-MovementProof.ps1 `
  -SkyrimDirectory 'C:\path\to\Skyrim Special Edition' `
  -ProxyBaseFormId '0x0010C0E3' -ExpectedCellFormId '0x00009CC3' `
  -Scale 0.1 -YawSign 1
```

The script refuses a mismatched installed DLL and writes the test log under
ignored `build/movement-proof`. Launch staged Shipwright from
`build/runtime-shipwright` so its settings and saves remain separate from the
installed game. Exit Skyrim without saving.

The `0x0010C0E3` example is `Barrel02Static`, read directly from the installed
`Skyrim.esm` `STAT` record with model `Clutter\Barrel02.nif`. The script defaults to a 120-unit positive Skyrim X
visual offset so the barrel is not placed inside the player. This offset is a
presentation choice for the test, not a cross-game coordinate measurement.
The expected cell ID is required; the adapter disables proof mode if the player
leaves it. Replace the example cell ID with the disposable test save's actual
cell identity rather than treating `0x00009CC3` as a universal test area.

## Automated and build evidence

MSVC 19.51 / Windows SDK 10.0.26100.0 built the Debug and Release bridge, hosts,
SKSE DLL, and movement state machine. All four CTest suites passed in both
configurations: Debug 53.94 seconds and Release 53.85 seconds. After adding the
visual offset, all four Release suites passed again in 53.84 seconds.

New checks cover paired-origin calibration, the candidate axis/scale transform,
binary-yaw wrap, duplicate suppression, nonmonotonic sequences, OOT scene and
Skyrim context barriers, OOT session restart, implausible jumps, stale Link data,
and invalid Skyrim context. Existing lifecycle, timeout, abandoned mutex, bounded
SKSE scheduling, and both-adapter tests continue to pass.

The pinned Shipwright Release `soh` target compiled and linked successfully with
the unchanged telemetry publication path and completed its asset-copy step.
`../SkyCraft` and `../oot` were not modified.

## Live runtime evidence and gate decision

The first opt-in run used provisional gold form `0x0000000F` at scale 1. Skyrim
1.7.104 created runtime reference `0xff000f1c` in cell `0x00009cc3` and applied
OOT-authoritative samples. The logged pose changed from approximately
`(-35120,-97914,158), yaw 6.08` at Link sequence 261082 to
`(-35145,-98686,58), yaw 6.49` by sequence 262200, with later movement and
turning through sequence 263700. The user could not see this tiny proxy.
This proves native placement/move calls executed in a live Skyrim process and
that the logged target pose responded to OOT telemetry; it does not prove visual
agreement or stable object physics.

When Skyrim lost focus, its game-thread heartbeat timed out while OOT telemetry
continued. A later windowed run used temporary `bAlwaysActive=1` in Skyrim.ini,
`bFull Screen=0`, `bBorderless=0`, `960x900` in SkyrimPrefs.ini, and
`bGamepadEnable=0` to prevent the controller from also affecting Skyrim. Both
heartbeats then remained live simultaneously. The original INI files are backed
up byte-for-byte under ignored `build/movement-proof/ini-backup-20261001` and
must be restored after testing.

The gold run at scale 0.1 created reference `0xff000f20`; logged position and
yaw changed repeatedly with Link sequences 270240–270540 while the Skyrim player
position remained stable. The user still could not see gold. The subsequent
static-barrel run used base `0x0010C0E3` and a 120-unit X offset. The user briefly
saw a barrel but could not establish it was the Zelrim reference. Logs showed
retirement/recreation on OOT invalidation and Skyrim pause; the Skyrim player
also moved substantially during that run, contaminating the paired origin.

A diagnostic rebuild recorded both requested and actual `TESObjectREFR` pose.
For example, at Link sequence 119700 the target and actual position were both
`(-35039.609,-97950.914,158.378)` with yaw `3.778480`, but `loaded3d=0`.
That remained zero through many subsequent position and yaw changes. The pinned
native placement call therefore produced a reference with mutable coordinates,
but no confirmed loaded visual mesh in this path. The adapter now requires an
explicit test cell (`0x00009CC3` in this disposable run) and disables proof mode
if the player leaves it; the prior build had incorrectly recreated a proxy in
another cell.

After the run, the original installed Zelrim DLL and both Skyrim INI files were
restored byte-for-byte from their backups. The staged development executables
and runtime logs remain under ignored `build/`.

On 2026-10-02 a second controlled run used the pinned-SKSE call convention.
Both peers were live, Skyrim telemetry reported player `0x14` in the expected
cell `0x00009cc3`, and the plugin created owned barrel reference `0xff000eeb`.
At Link sequence 1217 the target and actual pose matched
`(-35000.168,-97913.742,158.211), yaw 6.083190`, but `loaded3d=0`; the user
reported no visible barrel. Subsequent stationary OOT samples caused repeated
identical `MoveRefrToPosition` calls (for example sequences 1260–1800), still
without a loaded node. This rules out the null-registry/zero-handle mismatch as
a sufficient fix. It does not isolate whether the repeated moves themselves
prevented 3D attachment. The user closed Skyrim without saving, and original
installed DLL and INIs were restored and hash-verified. No new Skyrim save file
was written.

The next candidate suppresses pose application for fresh OOT sequences whose
position and shape yaw are unchanged, while still consuming those sequences
for ordering and stale-data checks. Turning-only samples still apply. A
dedicated movement-only check and all five Debug/Release CTest suites pass
(Debug 53.85 seconds; Release 53.82 seconds). The final bounded 3D-attachment
transition log was rebuilt in both configurations and its movement-only test
passed in both configurations.

The subsequent 2026-10-02 no-redundant-move live run created reference
`0xff000eea` at Link sequence 726 in the configured test cell. Its initial
readback matched the target at `(-35000.168,-97913.742,158.218), yaw
6.083190`; shortly afterward the plugin observed that same reference gain a
loaded 3D node. The user located the barrel and reported that it moved against
fixed Skyrim scenery when Link moved in OOT. In the initial motion interval,
OOT position changed from `(-80.493,0,210.323)` to approximately
`(-510.561,0,-201.243)` while Skyrim player telemetry was fixed at
`(-35120.168,-97913.742,159.094)`. The user later walked the Skyrim player
about 83 units while looking around; that later interval is not clean
cross-game movement evidence. The loaded 3D transition after redundant moves
were suppressed is strong evidence for that fix, but does not prove causality
in isolation.

When the user paused OOT, its adapter published an invalid sample. Skyrim's
game-thread heartbeat was temporarily starved in the background, so retirement
was delayed until Skyrim ran again; it then retired `0xff000eea` with
`unusable-link`. OOT resume recalibrated into new reference `0xff000eef`,
which gained loaded 3D. OOT exit retired that reference and disconnected the
peer. Relaunching OOT produced a new PID/session and Skyrim created and loaded
new reference `0xff000ef0` rather than reusing an old one. This demonstrates
safe recovery on OOT pause/disconnect/restart when Skyrim's game thread runs,
not an unconditional wall-clock retirement bound while that thread is stopped.

The user exited Skyrim without saving. The original installed Zelrim DLL and
both INI files were restored from preserved backups and hash-verified; no new
Skyrim save file was written. The roadmap remains unchanged.

The former `sequence % 60` applied-pose log filter missed this run's movement
samples. It has been replaced with first-applied-pose logging and a one-second
rate limit for later applications; this diagnostic change needs the next live
run to supply measured proxy deltas and yaw. After this final diagnostic build,
all five CTest suites passed again in Release (53.82 seconds) and Debug (53.80
seconds), with both games closed to avoid live-endpoint contention.

An earlier launch attempt exposed an OOT window/startup problem: a process existed
without a targetable desktop window, and a separate launch from the wrong
working directory showed SoH's file-permissions error. A correctly staged
launch made an OOT window visible, but a Windows application-error dialog was
also present. This attempt did not reach playable two-game validation. Both
games subsequently closed; the original installed DLL and both INI files were
again restored and their hashes verified. No Skyrim save was created by Zelrim.

Source inspection after the failed visual run found that the pinned SKSE
`PapyrusSpawnerTask` supplies a real VM class registry to `PlaceAtMe_Native`
and uses `*g_invalidRefHandle` with `MoveRefrToPosition`. The first proof build
had supplied `nullptr` and zero. The adapter now follows the pinned call
convention and disables proof mode if the registry is unavailable. The updated
Debug and Release DLLs built and all four then-existing CTest suites passed in
each configuration (Debug 53.93 seconds; Release 53.88 seconds). The second
controlled run described above showed that this call-convention correction
alone did not render the proxy; the no-redundant-move change was also present
in the successful run.

## Final controlled matrix, 2026-10-02

The final run kept both games visible, disabled Skyrim's gamepad input, and put
`bAlwaysActive=1` in the original `[General]` section of the temporary Skyrim.ini.
Putting that setting in a second `[General]` section had not kept the Skyrim game
thread active in the background; the corrected setting did. The Skyrim player
remained at `(-35120.168,-97913.742,159.094)` throughout the clean movement
measurements. Only OOT received movement input. The user's earlier Skyrim walking
was excluded from this matrix.

The paired OOT origin was `(-82.595,0,109.463)` and the Skyrim proxy origin,
including the intentional +120 X visual offset, was
`(-35000.168,-97913.742,159.094)`. During the straight movement, OOT reached
`(-150.724,0,-259.288)`, a displacement of `(-68.129,0,-368.751)`. The
configured `(x,z,y)` basis at scale 0.1 therefore predicts proxy displacement
`(-6.813,-36.875,0)` and endpoint near
`(-35006.981,-97950.617,159.094)`. Applied-pose log readbacks matched their
targets (for example Link sequence 7462 at
`(-35006.176,-97946.266,159.094)`), with the Skyrim player stationary.
The user independently observed the barrel move against a fixed patch of Skyrim
scenery in the prior visual run. In this faster straight segment it was not
continuously visible, but was found again after stopping and moving only the
Skyrim camera. Some move-time readbacks reported `loaded3d=0`; this is a
presentation/streaming limitation, not evidence of competing proxy physics.

A user-requested turn changed OOT shape yaw from about -30861 to -3738 binary
angle units (about 149 degrees, larger than a strict quarter-turn). Proxy target
and actual yaw changed together from 6.083190 through
7.653987 to 8.683576 radians, consistent with `YawSign=+1`; the latter is an
unwrapped angle accepted by the pinned Skyrim move call. The barrel's nearly
rotationally symmetric mesh limits visual judgment of heading, so the yaw claim
rests on OOT telemetry plus Skyrim target/actual readback. Signed-yaw wrap has
automated coverage. Moving forward along the new heading changed OOT position
approximately from `(-171.878,0,-228.819)` to `(-255.764,0,-9.573)` and the
proxy moved in the corresponding second Skyrim direction, with player position
fixed. With Link, Skyrim player, and camera still for ten seconds, the barrel
held its place; no drift or independent controller behavior was observed.

The proof task was scheduled every 16 ms with at most one pending task and one
latest-sample read per invocation. Live OOT sequence numbers advanced around
20 per second in the unfocused, side-by-side setup, while the read-only Skyrim
world sample advanced around four per second as configured. Applied-pose logs
were rate-limited to one per second after the first, so these observations do
not establish per-frame visual latency or a precise end-to-end distribution.
No stale pose was integrated or extrapolated between fresh OOT samples.

Skyrim exit and relaunch changed its PID and session (from PID 27448 to 13172)
and produced a newly placed, 3D-loaded proxy rather than adopting the old
reference. During a later temporary OOT heartbeat timeout, the current proxy
`0xff000ee9` retired with `unusable-link`; when OOT samples resumed, a new
reference `0xff000eee` calibrated and loaded 3D. On `coc qasmoke`, Skyrim
telemetry became unusable during loading, so that proxy retired with
`unusable-skyrim`. In the new interior cell `0x00032ae7` (worldspace zero),
the proof disabled itself because the configured test cell was `0x00009cc3`;
it created no proxy there. Pause, OOT disconnect, and OOT restart were covered
in the preceding controlled run. Retirement is bounded by Skyrim game-thread
execution, not wall-clock time when that thread is starved.

The user exited Skyrim without saving. No Skyrim save file was created during
this run. The pre-test installed DLL and both INI files were restored
byte-for-byte and SHA-256 verified against the preserved backups. The static
reference is only parked and relinquished, not deleted through a verified
engine API, so the no-save/disposable-session rule remains essential.
With both games closed, all five CTest suites passed in Debug (53.64 seconds)
and Release (53.81 seconds), including movement authority, bridge lifecycle,
both adapters, and bounded SKSE task scheduling.

This evidence completes the deliberately narrow milestone-4 gate: OOT's own
movement samples drive a Skyrim-owned visual proxy, direct pose application
matches logged coordinates/yaw without an independent Skyrim Link controller,
and safety resets recover across pauses, stale data, cell changes, and both
process restarts. It supports beginning the *experiment* in milestone 5, not
claiming that OOT can yet move against arbitrary Skyrim terrain. Before any
broader use, investigate the transient unloaded 3D during moves, measure
end-to-end visual latency, and establish a verified deletion/persistence policy.
