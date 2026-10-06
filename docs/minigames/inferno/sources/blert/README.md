# Blert code pinned for the Inferno (challenge type 5)

Copied 2026-10-03 from shallow clones kept in `build/corpus_tmp/` (not in git). Files are copied
verbatim, licence headers intact. **Read `PROVENANCE.md` first.**

| Repo | URL | Commit | Licence |
|---|---|---|---|
| plugin (RuneLite plugin that records) | https://github.com/blert-io/plugin | `0efb39800b9e1f73e01c0116b962ccc93f0f8f6f` | MIT, `plugin/LICENSE` |
| blert (server, web, processing) | https://github.com/blert-io/blert | `7c7750cf01b23e5d223623d7c34c5ef10ca327fd` | README says MIT (`README.md:220-222`); the clone at this commit has NO `LICENSE` file, so none is copied. Source headers in the copied files are unchanged |
| protos (event and storage schema, submodule `proto/` of blert) | https://github.com/blert-io/protos | `450112dffc33e1e51b7ec201e1c8a45bf2f903c9` | MIT, `protos/LICENSE` |

## Files (path under this directory; what each encodes)

### plugin/src/main/java/io/blert/
* `challenges/inferno/InfernoChallenge.java` the run: region ids, `Wave: 1` and `Duration:` chat parsing, the 10-tick wave-1 offset, pillar tracking, wave advance.
* `challenges/inferno/WaveDataTracker.java` one wave: tracker start on `Wave: N`, npc spawn/despawn, the attack-by-animation emitter, resurrected-hp rule, `Wave completed!`.
* `challenges/inferno/InfernoNpc.java` the npc table: ids 7691-7709, plugin hitpoints, animation ids -> attack kinds.
* `challenges/inferno/Pillar.java` the three pillar tiles.
* `events/inferno/InfernoWaveStartEvent.java` the one Inferno-only event (type 300).
* `events/EventType.java`, `events/NpcEvent.java`, `events/NpcAttackEvent.java`, `events/PlayerUpdateEvent.java` the shared events the Inferno uses.
* `core/DataTracker.java` the per-tick machinery: `getTick()`, `sendNpcUpdate`, hitsplat hitpoints, player attack/spell detection, stage completion.
* `core/NpcAttack.java` attack enum (70-88 are the Inferno's), `core/Stage.java` (waves = stages 200-268), `core/Challenge.java` (INFERNO = 5), `core/BasicTrackedNpc.java`, `core/TrackedNpc.java`.
* `util/Tick.java` (600 ms a tick, time-string parsing), `util/Location.java` (instance-aware world point).

### blert/
* `web/app/utils/spawn-index.ts` the spawn encoding (`type << 10 | x << 5 | y`, y inverted from the arena base), the arena base, the nine spawn tiles.
* `challenge-harder/src/processing/inferno.rs` the 69-row wave table, `WAVE_INTERVAL_TICKS`, pillar tiles, the stage-finished processing.
* `challenge-harder/src/processing/spawn_index.rs` how a wave's spawn is decided (the npcs on the first tick that sit on a spawn tile).
* `challenge-server/event-processing/inferno.ts` the older TypeScript twin of the processor.
* `common/protocol/json-schemas.ts` the JSON shape of an event as the API returns it.
* `web/app/api/v1/challenges/route.ts`, `query.ts`, `inferno/[id]/route.ts`, `inferno/[id]/events/route.ts` the API surface: listing filters, per-wave events, the per-wave record.

### protos/
* `event.proto` events and stage enums, `challenge_storage.proto` the per-wave record and split ids, `npc_definitions.json` the npc id table (Blert's names and sizes: `size` of Jad 5, mager 4), `attack_definitions.json` player attack and cooldown table (used to decide what counts as a player attack; included because PLAYER_ATTACK rows in the recordings depend on it).

Not copied: the web UI, the merger, the Colosseum/ToB/Mokhaiotl code.

The proto submodule is empty in the blert clone; `protos/` here is its own clone.
