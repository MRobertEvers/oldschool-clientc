# Colosseum-Waves (willediger) -- pinned 2026-10-03

- Repo: https://github.com/willediger/Colosseum-Waves @94376dd6 (2025-08-13), BSD-2-Clause (LICENSE here). It is NOT in runelite/plugin-hub@fc877fc0 (no manifest names willediger); found by `gh search repos`. Its code was folded into fortis-colosseum's LosLinks.
- Files copied (src/main/java/com/colosseumwaves/): ColosseumWavesPlugin.java, ManticoreHandler.java, NpcSpawn.java. No wave table: it only RECORDS wave-start and reinforcement positions and builds a https://los.colosim.com link.
- Provenance: client-side observer.

## Constants it states
| What | Value | Where |
|---|---|---|
| Colosseum region | 7216 | ColosseumWavesPlugin.java:68 |
| Colosim LoS type ids by npc | Shaman 1, Javelin 2, Jaguar 3, Manticore 4, Minotaur 5 (also red-flag routefind), Shockwave 6 | ColosseumWavesPlugin.java:76-82 |
| Reinforcements phase begins when ticksSinceWaveStart > 10 | 10 | ColosseumWavesPlugin.java:215-216 |
| Wave start/complete chat patterns | "Wave: (\d+)" / "Wave (\d+) completed" | ColosseumWavesPlugin.java:72-73 |
| Manticore orb spotanims | magic/ranged/melee = VFX_MANTICORE_01_PROJECTILE_{MAGIC,RANGED,MELEE}_01 | ManticoreHandler.java:58-60 |
| Orb order max 3 entries; charged when 3 orbs seen; with Mantimayhem 3 the order can be any permutation, otherwise the first orb decides | | ManticoreHandler.java:50-110 |
