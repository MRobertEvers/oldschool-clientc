# combat-logger (SuperNerdEric) -- pinned 2026-10-03

- Repo: https://github.com/SuperNerdEric/combat-logger @a5c6f304 (the plugin-hub pin of plugins/combat-logger, runelite/plugin-hub@fc877fc0), BSD-2-Clause (LICENSE here). A RuneLite plugin that logs encounters for analysis (it is a recorder, so its id lists say what the author OBSERVED as attacks).
- Copied: encounters/ColosseumHelper.java; id_lists_excerpt.txt (the Colosseum lines of NpcAttackAnimationIds, GraphicsObjectIdsToTrack, NpcIdsToTrack, BossIds, Game/GroundObjectIdsToTrack, LeaderboardRegions, with source line numbers).
- Not copied: the rest of the plugin.
- Ids are RuneLite gameval names; the numbers are in ../runelite_gameval/colosseum_names.tsv and ../code_id_map.tsv.

## Constants it states
| What | Value | Where |
|---|---|---|
| Colosseum region | 7216 | LeaderboardRegions.java:36 |
| Modifier selection script | 4931; selected modifier varbit VarbitID.COLOSSEUM_SELECTED_MODIFIER (9788) | ColosseumHelper.java:33, :250 |
| Manticore attack animation | NPC_MANTICORE_01_TRIPLE_THROW (10869); charge NPC_MANTICORE_01_TRIPLE_CHARGE (10868) | ColosseumHelper.java:94, :126, :186-187 |
| Orb order read from spotanims on the manticore on the charge/throw animation | | ColosseumHelper.java:186-210 |
| Per-npc "attack" animation list | see ../CODE_CONSTANTS.md section C | id_lists_excerpt.txt (NpcAttackAnimationIds.java:62-76) |
