# Milestone 4: movement authority proof

Implementation status: code-complete on 2026-10-01; the live two-game movement
matrix remains pending. The feasibility gate is therefore **not yet complete**
and `docs/roadmap.md` has not been advanced.

## What was implemented

The protocol remains version 3. Existing post-player-update Link telemetry is the
authority input. `MovementAuthority` pairs the first usable OOT sample with the
current usable Skyrim player sample, then maps OOT displacement to an absolute
proxy pose. It never generates velocity, integrates input, runs pathfinding, or
reimplements Link movement.

The opt-in SKSE path is enabled only by `ZELRIM_MOVEMENT_PROOF=1` plus an explicit
`ZELRIM_PROXY_BASE_FORM_ID`. It uses pinned Skyrim 1.7.104 native relocations to
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
* candidate basis: OOT `(x,y,z)` to Skyrim `(x,z,y)`;
* configurable positive scale in Skyrim units per OOT unit;
* Skyrim anchor `rot.z` plus configurable-sign delta from initial OOT shape yaw;
* signed binary-angle wrap before conversion to radians.

These choices are not promoted to general coordinate facts. The first live matrix
must verify them and may reverse the yaw sign or replace the basis.

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
  -ProxyBaseFormId '0xFORMID' -Scale 1 -YawSign 1
```

The script refuses a mismatched installed DLL and writes the test log under
ignored `build/movement-proof`. Launch staged Shipwright from
`build/runtime-shipwright` so its settings and saves remain separate from the
installed game. Exit Skyrim without saving.

## Automated and build evidence

MSVC 19.51 / Windows SDK 10.0.26100.0 built the Debug and Release bridge, hosts,
SKSE DLL, and movement state machine. All four CTest suites passed in both
configurations: Debug 53.94 seconds and Release 53.85 seconds.

New checks cover paired-origin calibration, the candidate axis/scale transform,
binary-yaw wrap, duplicate suppression, nonmonotonic sequences, OOT scene and
Skyrim context barriers, OOT session restart, implausible jumps, stale Link data,
and invalid Skyrim context. Existing lifecycle, timeout, abandoned mutex, bounded
SKSE scheduling, and both-adapter tests continue to pass.

The pinned Shipwright Release `soh` target compiled and linked successfully with
the unchanged telemetry publication path and completed its asset-copy step.
`../SkyCraft` and `../oot` were not modified.

## Required live evidence and gate decision

No real Skyrim proxy was created or moved in this run, so no claim is made that
the pinned placement/move relocations, visual orientation, scale, or physics
behavior work in the game executable. The following remains mandatory:

1. Observe proxy creation and record its logged runtime form ID in an isolated
   cell using a disposable no-save Skyrim run.
2. Match OOT publication sequences to forward/lateral/vertical proxy deltas and
   verify the candidate basis and scale.
3. Turn through quarter turns and signed-yaw wrap; verify or reverse `YawSign`.
4. Pause each game, force stale telemetry, load/change cells, disconnect, and
   restart each process; confirm immediate stop/retirement and fresh calibration.
5. Confirm the selected static proxy does not drift or fight absolute poses, and
   record observed update cadence/drop behavior.

Until those observations pass, milestone 4 does not support proceeding to
milestone 5. Automated evidence supports the design and failure semantics only.
