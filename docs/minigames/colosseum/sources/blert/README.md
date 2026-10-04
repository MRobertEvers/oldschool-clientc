# Blert code pinned for the Fortis Colosseum (challenge type 4)

Copied 2026-10-04 from shallow clones kept in `build/corpus_tmp/` (not in git): `blert_plugin/`,
`blert/` (cloned by the Inferno pass, same commit, re-checked) and `protos/`. Files are copied
verbatim, licence headers intact. **Read `PROVENANCE.md` first.** The Inferno copy of the shared
files is under `docs/minigames/inferno/sources/blert/`; this tree repeats the shared ones so the
Colosseum's line numbers stay self-contained.

| Repo | URL | Commit | Licence |
|---|---|---|---|
| plugin (RuneLite plugin that records) | https://github.com/blert-io/plugin | `0efb39800b9e1f73e01c0116b962ccc93f0f8f6f` (2026-10-01) | MIT, `plugin/LICENSE` |
| blert (server, web, processing) | https://github.com/blert-io/blert | `7c7750cf01b23e5d223623d7c34c5ef10ca327fd` (2026-10-01) | README states MIT; the clone at this commit has no `LICENSE` file, so none is copied. Source headers are unchanged |
| protos (event and storage schema, submodule `proto/` of blert, empty in the clone) | https://github.com/blert-io/protos | `450112dffc33e1e51b7ec201e1c8a45bf2f903c9` (2026-09-30) | MIT, `protos/LICENSE` |

## Files (path under this directory; what each encodes)

### plugin/src/main/java/io/blert/
* `challenges/colosseum/ColosseumChallenge.java` the run: regions 7216 (arena) and 7316 (lobby), the arena `WorldArea(1806, 3088, 38, 38)`, Minimus npc 12808 spawn/despawn = wave advance, reward chest 50741, handicap script 4931 and varbit 9788, end chat regex.
* `challenges/colosseum/WaveDataTracker.java` one wave: start on `Wave: N` / the Sol jump message, npc spawn/despawn, attack-by-animation, hitsplats, healing-totem projectile 2687, reentry pool objects 50743/50744, Sol dust/laser/pool graphics, grapple chat lines, wave end chat.
* `challenges/colosseum/ColosseumNpc.java` the npc table: ids 12810-12826, plugin hitpoints, animation ids to attack kinds.
* `challenges/colosseum/Manticore.java` loading/attack animations 10868/10869, style spotanims 2681/2683/2685, the three-attack burst.
* `challenges/colosseum/Handicap.java` the 14 handicap ids (0-13).
* `events/colosseum/*.java` the eight Colosseum-only events (200-207).
* `events/EventType.java`, `events/NpcEvent.java`, `events/NpcAttackEvent.java`, `events/StageUpdateEvent.java`, `events/ChallengeStartEvent.java`, `events/ChallengeEndEvent.java` the shared events.
* `core/DataTracker.java` per-tick machinery: `getTick()`, `sendNpcUpdate`, hitsplat hitpoints, player attack detection, stage completion and the in-game-ticks check.
* `core/NpcAttack.java` (Colosseum = 100-115), `core/Stage.java` (waves = stages 100-111), `core/Challenge.java` (COLOSSEUM = 4), `core/Hitpoints.java`, `core/BasicTrackedNpc.java`, `core/TrackedNpc.java`, `core/RecordableChallenge.java`.
* `util/Tick.java` (600 ms a tick, time-string parsing), `util/Location.java`, `util/DeferredTask.java`.
* `json/Event.java` the JSON event shape the Colosseum fields use.

### blert/
* `web/app/utils/spawn-index.ts` the arena base (1808, 3123), 34 x 34, the 12 spawn tiles, the four indexed npc types.
* `challenge-harder/src/processing/colosseum.rs` the server processor: the `WAVES` priors (four types only, see `PROVENANCE.md`), `HANDICAP_LEVEL_INCREMENT` = 30, Dynamic Duo's extra shockwave.
* `challenge-harder/src/processing/spawn_index.rs` the spawn index packing.
* `challenge-server/event-processing/colosseum.ts` the older TypeScript processor.
* `common/protocol/json-schemas.ts` the event JSON schema.
* `web/app/components/colosseum-handicap/colosseum-handicap.tsx` handicap names and levels as the web shows them.
* `web/app/api/v1/challenges/colosseum/[id]/route.ts` and `.../events/route.ts` the API endpoints (stage 100..111).

### protos/
`event.proto` (stages, events, the handicap enum, attack numbers), `challenge_storage.proto` (the wave record, splits 150-173), `npc_definitions.json`, `attack_definitions.json`, `LICENSE`.

## Not copied
`blert/web/**` pages that only draw images; the Rust tests; every file the Colosseum does not use.
`blert/proto` is an empty submodule directory in the clone: the schema comes from the protos clone above.
