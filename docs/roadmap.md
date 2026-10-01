# Zelrim roadmap and context for future chats

Updated: 2026-10-01. This is the proposed project sequence, not authorization to
implement every milestone. Plan and implement one milestone at a time; revise
later milestones as experiments establish what works.

## Project boundaries

Zelrim is an experimental Windows interoperability project between Skyrim Special
Edition and Ocarina of Time, using Ship of Harkinian (Shipwright) as the OOT runtime.

- All project code, integration adapters, patches, and documentation belong in
  `../zelrim`. Check the working directory: a chat may start in `../SkyCraft`.
- `../SkyCraft` and `../oot` are reference repositories. Do not modify them unless
  explicitly instructed. Access to a reference folder is not permission to edit it.
- The installed `../Shipwright` distribution is separate from the development
  source checkout under Zelrim's ignored `external/Shipwright` directory.
  Keep reproducible integration changes in Zelrim and stage test runtimes under
  ignored `build/`; preserve installed game files and user saves.
- Skyrim remains authoritative over its world, NPCs, quests, interactions, and
  rendering. OOT will be authoritative over Link's movement, actions, combat,
  targeting, inventory, and other Link-specific gameplay.
- The bridge translates data and intent between the engines. It must not recreate
  OOT mechanics in Skyrim or duplicate Skyrim's world simulation in OOT.

## Current state

**Milestone 1: Windows transport foundation is complete.** The shared-memory
bridge carries protocol identification, PIDs, and heartbeat timestamps through
`Local\Zelrim_v1`. Standalone hosts, a passive monitor, and real SKSE/Shipwright
heartbeat integrations have been validated. No gameplay interoperability exists.

Read these documents for implementation details and observed limitations:

- [Milestone 1 protocol and lifecycle](milestone-01.md)
- [Completed transport build and runtime evidence](transport-foundation.md)
- [Transport foundation implementation plan](transport-foundation-plan.md)
- [Shipwright heartbeat integration](shipwright-heartbeat.md)

**Milestone 2 is complete.** Its
[implementation record](milestone-02.md) and detailed
[read-only Link telemetry plan](milestone-02-plan.md) distinguish automated/build
evidence from the remaining in-game matrix. The milestones below continue numbering from the existing milestone files;
the earlier conversational list of next steps was not a milestone numbering scheme.

## Milestone 2: Read-only Link telemetry

Implementation completed 2026-10-01; see `milestone-02.md`.

Publish coherent OOT position, world/shape orientation, and existing state flags.
Read them through the standalone monitor and Skyrim logs without affecting either
game's behavior. Distinguish process liveness, sample freshness, and context validity.

Follow the detailed plan for protocol v2, nonblocking publication, session identity,
title/loading/pause handling, lifecycle invalidation, and synthetic/runtime tests.
Completion requires matching source and consumer observations and reliable stale
data rejection. No coordinate conversion or Skyrim movement belongs in this step.

## Milestone 3: Read-only Skyrim world telemetry

Implementation completed 2026-10-01; automated validation and the pinned
Shipwright build pass. Real Skyrim sampling and coordinate measurements remain;
see [implementation record](milestone-03.md) and [plan](milestone-03-plan.md).

Expose the Skyrim context needed by future Link integration, starting with player
location, cell/worldspace identity, and transition state. Establish and document
the relationship between OOT and Skyrim coordinate systems, including units,
axes, orientation, and origins, using measurements rather than assumptions.

Completion requires independently verified samples and unambiguous handling of
cell changes and coordinate discontinuities. This step observes the world; it
does not move actors or exchange collision geometry yet.

## Milestone 4: Movement authority proof

In an isolated Skyrim test area, let OOT drive a minimal proxy using OOT's own
movement simulation. Establish update timing, authority, pause behavior,
coordinate application, and reconnect behavior before expanding the experiment.

Define the smallest necessary input routing in this milestone's detailed plan;
do not implement a second Link controller inside Skyrim. Completion means the
proxy demonstrably follows OOT-authoritative state with explicit failure behavior.
A simple proxy is sufficient; Link assets and full presentation are deferred.

## Milestone 5: World collision exchange

Experiment with supplying Skyrim world geometry and/or spatial query results to
OOT so OOT can resolve Link's movement against the Skyrim world. Determine which
engine-side interfaces make this possible and what information must cross the
bridge. The mechanism is an open design question, not an established solution.

Start with a small static test environment. Assess correctness, latency, geometry
lifetime, transitions, and the implications of dynamic objects before broadening
scope. Skyrim remains the source of world data; OOT retains Link movement rules.

**Milestones 4 and 5 are feasibility gates.** Their results may require changes
to transport, scheduling, or the later roadmap. Do not treat subsequent steps as
fully designed or promise a general solution before these experiments succeed.

## Milestone 6: Link presentation

Render Link in Skyrim and synchronize presentation with OOT's authoritative
simulation. Plan assets, animation state translation, camera behavior, and visual
timing after movement and collision behavior are understood.

Skyrim owns rendering; animation and camera presentation must not become a second
authority for Link gameplay. Completion requires a stable visual representation
with documented handling of transitions and missing or stale simulation data.

## Milestone 7: Interactions and targeting

Translate OOT targeting and interaction intent into Skyrim-owned interactions
with objects and NPCs. Define identity mapping, object lifetime, range/context
validation, and results returned to OOT. Skyrim remains authoritative over NPC,
quest, and world interaction outcomes.

Start with one narrowly defined interaction and expand only after its ownership
and lifecycle rules are demonstrated. Do not replace Skyrim quest or dialogue logic.

## Milestone 8: Combat and inventory interoperability

Define explicit rules for translating Link's combat and inventory gameplay into
interactions with Skyrim actors, damage, equipment, and quest-related state.
Resolve ownership and result reporting before implementing individual mechanics.

OOT owns Link-specific rules; Skyrim owns the affected world actors and systems.
The exact reconciliation model remains to be designed. Begin with one controlled
combat case and a small inventory mapping rather than attempting full parity.

## Milestone 9: Persistence and hardening

Design coordinated save/load, cross-engine state identity, transitions, and
recovery. Expand compatibility, performance testing, installation, diagnostics,
and packaging once the gameplay boundary is proven.

Persistence needs consideration in earlier designs, and every milestone needs
its own failure handling and tests. This final milestone consolidates those
pieces; it is not a reason to postpone lifecycle correctness or preserve unsafe
intermediate state. Never silently alter existing user saves during experiments.

## Working across chats

Use one chat per milestone, keeping its detailed planning and implementation
together. Use a separate roadmap discussion when cross-milestone decisions need
revision. Repository documents, source, and test evidence are the durable context;
do not assume a new chat can recover decisions from prior conversations.

At the start of a milestone:

1. Confirm the Zelrim working directory, Git status, applicable repository
   instructions, and actual implementation state.
2. Read this roadmap, the milestone's detailed plan, and relevant completed
   milestone documentation. Check pinned integration revisions before editing.
3. For milestones beyond 2, create a focused `docs/milestone-NN-plan.md` with
   scope, authority boundaries, protocol implications, validation, and completion
   criteria. Keep distant milestones at roadmap detail until evidence supports more.
4. Work within the requested milestone. Record new decisions, runtime evidence,
   limitations, and remaining work in the repository before handing off.
5. Update this roadmap's status when a milestone is actually complete. Verify
   Git commit/push state directly rather than assuming earlier chat actions succeeded.

Suggested next-chat prompt:

> Work in `../zelrim`. Read `docs/roadmap.md`,
> `docs/transport-foundation.md`, and `docs/milestone-02-plan.md`, along with any
> applicable repository instructions. Implement milestone 2 only. Keep reference
> repositories unchanged, preserve Skyrim world authority and OOT Link authority,
> and document validation and remaining limitations.
