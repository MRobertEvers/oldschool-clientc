# Colosseum simulators and other repos looked at -- 2026-10-03

| Repo | Commit | Licence | What it is | Action |
|---|---|---|---|---|
| Supalosa/osrs-colosseum (https://los.colosim.com, the Vite page title is "Colosseum LOS") | 5b1734f0 (2026-08-24) | NONE (README: "Originally written by Backseat (bistools) and iFreedive") | Wave line-of-sight tool: per-npc size, range, cooldown, manticore orb patterns and timers, pillar geometry | NOT copied (no licence). Every constant is quoted with file:line in ../CODE_CONSTANTS.md section E. Files read: src/constants.ts, src/lineOfSight.ts, src/types.ts, src/utils.ts, src/venator.ts |
| OldSchoolSDK/InfernoTrainer branch merge-sims (colosim.com "Sol Heredit Trainer") | cc557916 | GPL-3.0 | Sol Heredit fight and hazards | Copied to sol_heredit_trainer/ |
| LlemonDuck/fortis-colosseum | e8b26968 | BSD-2 | Hub plugin: modifiers, splits, LoS links, wave overlay | Copied (mechanics files) |
| willediger/Colosseum-Waves | 94376dd6 | BSD-2 | Wave recorder to LoS link | Copied |
| SuperNerdEric/combat-logger | a5c6f304 | BSD-2 | Encounter logger (hub) | Copied the Colosseum id lists and ColosseumHelper |
| chsami/Microbot-Hub colosseumprayer | 4cb81986 | BSD-2 | A bot's prayer arbiter (Microbot, not RuneLite hub) | Read only (a clone is in build/corpus_tmp/microbot-hub). States: TILE_RANGE_RANGED_MAGING = 18 (ColosseumPrayerScript.java:45); javelin autos on even game ticks, shaman on odd (:414). Both are heuristics of a bot author, not mechanics: grade E, not in the table |
| markd315/colo-invo | 5268d223 | none | Loot/price calculator (deploy.py, update_prices.py, views/index.html) | Read only; no mechanics. A reward-table worker may read views/index.html for the reward list: not fetched into sources |
| Zxv975/fortis_colosseum_split_analyser | 41a533fa | GPL-3 | Analyses split files written by the fortis-colosseum plugin | Not copied; no mechanics |
| NickClark787/DesktopScape | not cloned | GPL-3 | Gear optimizer with a Sol Heredit DPS sim | Not cloned: DPS maths, no wave mechanics (listed for the record) |
| bopsec/bop-plugins@6ce8337b (hub plugin inf-colo-additions, TzhaarColoAdditions*) | 6ce8337b | BSD-2 | Pillar markers for the Inferno and Colosseum (overlay only) | Not copied: no mechanics |
| runelite/runelite gameval (AnimationID, SpotanimID, NpcID, ObjectID, VarbitID) | 04b96b7a | BSD-2 | Name <-> id table the plugins reference | colosseum_names.tsv (extract) |
| danbisdev stacksolver | not cloned | none | stack solver | Not cloned |

Searches that returned nothing relevant: github repo search for "colosim" (only unrelated repos), "sol heredit", "manticore runelite", "colosseum runelite". The plugin hub manifest (runelite/plugin-hub@fc877fc0, 2841 plugins) has two Colosseum entries: plugins/fortis-colosseum and plugins/inf-colo-additions; no manticore, sol-heredit or wave plugin of its own.
