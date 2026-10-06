# fortis-colosseum (RuneLite plugin hub) -- pinned 2026-10-03

- Repo: https://github.com/LlemonDuck/fortis-colosseum, licence BSD-2-Clause (LICENSE here, intact).
- Plugin hub pin: plugins/fortis-colosseum in runelite/plugin-hub@fc877fc0 names commit b86cb480. Copied from HEAD e8b26968 (2026-10-03). `git diff b86cb480 e8b26968 -- src/main` touches only FortisColosseumConfig, LootHider and SplitsOverlay: none of the files below differ.
- Files copied (all under src/main/java/com/duckblade/osrs/fortis/): util/spawns/{WaveSpawns,Enemy,WaveSpawn}.java, util/{Modifier,ColosseumStateTracker,ColosseumState}.java, features/loslinks/LosLinks.java, features/loslinks/model/{ManticoreOrbOrder,ManticoreOrbType,NpcSpawn,WaveSpawnRecord}.java. Not copied: loot, splits, overlays, config (no mechanics).
- Credits: LosLinks derives from willediger/Colosseum-Waves (see ../colosseum_waves/README.md) and targets https://los.colosim.com (= Supalosa/osrs-colosseum, see ../SIMULATORS.md).
- Provenance: a client-side observer. The wave table in WaveSpawns.java is the author's encoding of what players observed (it is the plugin's "expected spawns" for its overlay); it is not Jagex data. Grade it D until a second source (wiki wave table, Blert) agrees.

## Constants it states (file:line in the copies here)

| What | Value | Where |
|---|---|---|
| Colosseum region / lobby region | 7216 / 7316 | ColosseumStateTracker.java:34 / :33 |
| Modifier select script, selected-modifier varbit | 4931 / 9788 | ColosseumStateTracker.java:36-37 |
| Modifier bit ids (bitmask `1 << id`), level varbits, sprite ids | table in Modifier.java:19-32 (Bees 2/9791, Blasphemy 4/9790, Doom 8/10681, Dynamic Duo 9/-1, Frailty 12/9796, Mantimayhem 0/4588, Myopia 11/9795, Reentry 1/9792, Red Flag 13/-1, Relentless 5/9798, Solarflare 10/9797, Quartet 6/-1, Totemic 7/-1, Volatility 3/9799) | Modifier.java |
| Wave 12 = Sol Heredit alone (+1 Fremennik with Quartet) | | WaveSpawns.java:40-47 |
| Fremennik every wave 1-11 | 3, or 4 with Quartet | WaveSpawns.java:51 |
| Serpent Shaman spawn | 1, waves 1-6 | WaveSpawns.java:53 |
| Serpent Shaman reinforcement | 1, waves 4-6 and 10-11 | WaveSpawns.java:58-61 |
| Jaguar Warrior reinforcement | 1, waves 1-6 (never a wave spawn) | WaveSpawns.java:65-67 |
| Javelin Colossus | w2: 1, w3: 2, w4: none, w>=5: 2-(w%2) (w5 1, w6 2, ... w11 1) | WaveSpawns.java:71-77 |
| Manticore | 1 on waves 4-8, 2 on waves 9-11 | WaveSpawns.java:81-85 |
| Shockwave Colossus | 1 on waves 7, 8, 11 (2 with Dynamic Duo) | WaveSpawns.java:89-91 |
| Minotaur reinforcement | 1, waves 7-11 ("replaces jaguar warrior") | WaveSpawns.java:95-97 |
| Bees modifier | spawn count = Bees level varbit | WaveSpawns.java:34-36 |
| Wave start / reinforcement | an npc that spawns on any tick after the wave-start tick is a reinforcement | LosLinks.java:166, :201 |
| LoS grid | scene origin = south-west pillar min tile minus 8 in x and y; y inverted, max y = 33 | LosLinks.java:246-247, :283-284 |
| Wave start message | "Wave: N" parsed by substring; wave 12 forced | ColosseumStateTracker.java:116-129 |
| Manticore orb order | 3 orbs recorded in order first/second/third; an uncharged manticore shows none; "u" prefix = uncharged | ManticoreOrbOrder.java, NpcSpawn.java:30-35 |
| Orb spotanim -> style | magic VFX_MANTICORE_01_PROJECTILE_MAGIC_01 (2681) 'm', ranged ..._RANGED_01 (2683) 'r', melee ..._MELEE_01 (2685) 'M' | ManticoreOrbType.java:24-31 |
| Minimus npc | id 12808 (gameval now says COLOSSEUM_MASTER) | ColosseumStateTracker.java:137 |
| Colosim LoS ids | Shaman 1 (reinf 7), Javelin 2, Jaguar 3, Manticore 4, Minotaur 5, Shockwave 6 | Enemy.java:12-17 |

Derived per-wave table (computed from WaveSpawns.java; see ../CODE_CONSTANTS.md section B).
