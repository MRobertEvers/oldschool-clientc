# LEDGER_code -- plugin, simulator and reference-server part of the Colosseum corpus

All fetches 2026-10-03 from github.com (git clone --depth 1, gh api, raw.githubusercontent.com). One host (GitHub); no wiki, no blert.io. Clones live in build/corpus_tmp/ (not in git). Notebook: build/spec_state/matthew-mbp-m4-waves-b1-spec-colosseum/corpus.code.progress.md.

| Date | URL | Revision | File(s) | For |
|---|---|---|---|---|
| 2026-10-03 | https://github.com/runelite/plugin-hub | fc877fc0 | plugins/fortis-colosseum, plugins/inf-colo-additions, plugins/combat-logger (manifests) | which plugins are in the hub (2841 plugins; name filters colosseum, sol, manti, fortis, wave, inferno, heredit, minotaur) |
| 2026-10-03 | https://github.com/LlemonDuck/fortis-colosseum | e8b26968 (hub pin b86cb480; mechanics files identical) | 11 files, see fortis_colosseum/README.md | wave table, modifiers, LoS links |
| 2026-10-03 | https://github.com/willediger/Colosseum-Waves | 94376dd6 | 3 files (colosseum_waves/) | wave and reinforcement recorder |
| 2026-10-03 | https://github.com/SuperNerdEric/combat-logger | a5c6f304 (hub pin) | ColosseumHelper.java, id_lists_excerpt.txt | attack animation, graphic, npc id lists |
| 2026-10-03 | https://github.com/bopsec/bop-plugins | 6ce8337b (hub pin of inf-colo-additions) | TzhaarColoAdditionsPlugin.java lines 51-69 (inf_colo_additions/excerpt.txt) | arena bounds, pillar loc names |
| 2026-10-03 | https://github.com/OldSchoolSDK/InfernoTrainer branch merge-sims | cc557916 | src/content/colosseum/ (17 files, no assets/rendering) -> sol_heredit_trainer/ | Sol Heredit simulator (colosim.com) |
| 2026-10-03 | https://github.com/Supalosa/osrs-colosseum (los.colosim.com) | 5b1734f0 | src/constants.ts, lineOfSight.ts, types.ts, utils.ts, venator.ts READ; nothing copied (no licence) | LoS tool constants quoted in CODE_CONSTANTS.md E |
| 2026-10-03 | https://github.com/chsami/Microbot-Hub (sparse: colosseumprayer) | 4cb81986 | read only | bot heuristics, not used |
| 2026-10-03 | https://github.com/markd315/colo-invo; Zxv975/fortis_colosseum_split_analyser; OldSchoolSDK/osrs-sdk (04fdaee, no Colosseum files) | 5268d223; 41a533fa; 04fdaee | listed only | no mechanics |
| 2026-10-03 | https://colosim.com | (live HTML, main.js 5.4 MB not stored) | index HTML | identifies the Sol Heredit Trainer build |
| 2026-10-03 | https://raw.githubusercontent.com/runelite/runelite/master/runelite-api/src/main/java/net/runelite/api/gameval/{AnimationID,SpotanimID,NpcID,ObjectID,VarbitID}.java | 04b96b7a | extract -> runelite_gameval/colosseum_names.tsv (267 rows) | names to ids |

Local products: sources/CODE_CONSTANTS.md, SIMULATORS.md, code_id_map.tsv, runelite_gameval/colosseum_names.tsv, and the five source folders.

## Reference servers (part 3)
None fetched, as instructed. What the tree already took from them: not examined here (no reference server is in this worktree; docs/minigames/colosseum has no server-derived file). No balance number from a server is used.

## What this part states (numbers; file:line; quote). Full tables in CODE_CONSTANTS.md.

- Wave table: fortis_colosseum/WaveSpawns.java:51 `modifiers.contains(Modifier.QUARTET) ? 4 : 3, Enemy.FREMENNIK`; :53 `if (wave <= 6)` shaman; :58 `(wave >= 4 && wave <= 6) || (wave >= 10)` shaman reinforcement; :65 jaguar reinforcement wave <= 6; :71-77 javelins `wave - 1` for waves 2-3 and `2 - (wave % 2)` from 5; :81-85 manticore `single = wave <= 8`; :89 shockwave waves 7, 8, 11; :95 `wave >= 7` minotaur reinforcement; :40 wave 12 Sol.
- Orb spotanims 2681 magic, 2683 ranged, 2685 melee: fortis_colosseum/ManticoreOrbType.java:24-31.
- Reinforcement window: colosseum_waves/ColosseumWavesPlugin.java:216 `ticksSinceWaveStart > 10`.
- Region ids 7216 / 7316: fortis_colosseum/ColosseumStateTracker.java:33-34.
- Arena box 1808-1840 x 3090-3123: inf_colo_additions/excerpt.txt:53-56.
- Sol Heredit 1500 hp, attack 350, strength 400, defence 200, range 350, magic 300, size 5 (cache agrees): sol_heredit_trainer/js/mobs/SolHeredit.ts:208-213, :271.
- Sol phase hitpoints 1500/1350/1125/750/375/150: SolHeredit.ts:121-128.
- Sol attack delays: spear 7 then 6 (:417), shield 6 then 5 (:430), triple short 12/11 (:614), long 12 (:623), grapple 7 (:667); max hit 70 (:288); first attack spear (:157, whose comment reads "first attack is always a spear?", question mark the author's).
- Sol triple parry hits 15/25/35 (short) and 15/30/45 (long): SolHeredit.ts:673-689.
- Grapple 20 + rand(25): SolHeredit.ts:657; slam 20 + rand(25): entities/SolGroundSlam.ts:18; sand pool 5 + rand(5): SolSandPool.ts:83; laser 60 + rand(20): LaserOrb.ts:183; laser cooldown 25-35 then 12: SolHeredit.ts:145-147.
- LoS tool cooldowns: shaman 5 / javelin 5 / jaguar 5 / minotaur 5 / shockwave 5 / manticore 10; ranges 10/15/1/1/15/15; sizes 1/3/2/3/3/3: SUPA src/constants.ts:20-30.
- Manticore charge 10 ticks, post-fire delay 5, first attack not before tick 3: SUPA constants.ts:60-61, :69.
- Colosim LoS grid is 34 x 34 with y inverted: SUPA lineOfSight.ts:32-33; fortis_colosseum/LosLinks.java:247.
- Modifier bit ids 0-13 with level varbits (Mantimayhem 4588, Bees 9791, ...): fortis_colosseum/Modifier.java:19-32.
