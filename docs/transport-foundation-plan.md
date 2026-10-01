# Transport foundation execution plan

Status: completed. See [results and evidence](transport-foundation.md).

Scope: presence and heartbeat transport only. Skyrim owns its world and rendering;
OOT owns future Link gameplay. No movement, assets, combat, collision or input work.

1. Build the pinned Shipwright source with the prepared adapter and generate its
   own runtime assets. Keep development settings/saves separate from the installed game.
2. Build a minimal Skyrim SKSE plugin against the public SKSE API. Publish
   heartbeats from game-thread tasks, with bounded work and no engine hooks.
3. Test the actual plugin DLL in a small SKSE API host, including shutdown and
   failure handling. Retain the existing bridge and Shipwright adapter suites.
4. Stage and validate the development game and SKSE plugin against installed
   runtimes. Verify live PIDs/heartbeats, normal exit, forced exit, and reconnect.
5. Record exact build commands, tested versions, deployment/rollback instructions,
   and evidence. Distinguish host-level coverage from real-game validation.

The installed machine has Skyrim 1.7.104 and matching SKSE. VS 2026 is available;
the documented Shipwright v143 toolset is absent, so the build uses the installed
VS 2026 compiler. All project implementation stays in Zelrim.
