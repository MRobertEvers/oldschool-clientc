# Code constants for the Fortis Colosseum -- every number the pinned code states

Written 2026-10-03 by the code part of the corpus pass. Citations are `dir/file:line` under `docs/minigames/colosseum/sources/` for copied files and `SUPA <file>:<line>` for Supalosa/osrs-colosseum@5b1734f0 (no licence, so not copied; clone in build/corpus_tmp/supalosa). Source labels: FORT = fortis_colosseum, WAVES = colosseum_waves, LOGGER = combat_logger, SOLSIM = sol_heredit_trainer, SUPA, CACHE = cache_npc.txt / rig/*.tsv of this tree, GV = runelite_gameval/colosseum_names.tsv.

## Reading rules

- Every row here is a CLIENT-SIDE OBSERVER's or simulator author's statement. None is Jagex data. A row is grade D alone; grade C needs a second source that is INDEPENDENT. Independence caveats are written per row: FORT's LoS ids, WAVES's and SUPA's npc types are one schema (FORT and WAVES both emit links to SUPA's page), so their agreement is NOT two sources. SOLSIM credits Supalosa for "engine and logic" and player testers for feedback.
- "Agrees" / "Disagrees" names the other pinned source. A disagreement stays open; nothing is averaged.
- Cache cross-checks (CACHE) are first-party data and are what turns a simulator's number into a confirmed one: they are noted where they exist.

## A. Wave table (the one the cache does not have) -- FORT only

Source: FORT fortis_colosseum/WaveSpawns.java:34-97. No second code source states a wave table (WAVES records, SUPA only simulates). OPEN: must be cross-checked against the wiki wave pages and Blert.

| Wave | Spawn at wave start | Reinforcements | Line(s) |
|---|---|---|---|
| 1 | 3 Fremennik, 1 Serpent Shaman | 1 Jaguar Warrior | :51 :53-56 :65-67 |
| 2 | 3 Fremennik, 1 Shaman, 1 Javelin Colossus | 1 Jaguar | :71-73 |
| 3 | 3 Frem, 1 Shaman, 2 Javelin | 1 Jaguar | :71-73 |
| 4 | 3 Frem, 1 Shaman, 1 Manticore (no javelin) | 1 Shaman, 1 Jaguar | :58-61 :81-85 |
| 5 | 3 Frem, 1 Shaman, 1 Javelin, 1 Manticore | 1 Shaman, 1 Jaguar | :75-77 |
| 6 | 3 Frem, 1 Shaman, 2 Javelin, 1 Manticore | 1 Shaman, 1 Jaguar | |
| 7 | 3 Frem, 1 Javelin, 1 Manticore, 1 Shockwave | 1 Minotaur | :89-91 :95-97 |
| 8 | 3 Frem, 2 Javelin, 1 Manticore, 1 Shockwave | 1 Minotaur | |
| 9 | 3 Frem, 1 Javelin, 2 Manticore | 1 Minotaur | :84-85 |
| 10 | 3 Frem, 2 Javelin, 2 Manticore | 1 Shaman, 1 Minotaur | :58-61 |
| 11 | 3 Frem, 1 Javelin, 2 Manticore, 1 Shockwave | 1 Shaman, 1 Minotaur | |
| 12 | Sol Heredit (+1 Fremennik with Quartet) | none | :40-47 |

Modifier effects on the table (FORT WaveSpawns.java): Quartet = 4 Fremennik instead of 3 (:51), and 1 Fremennik with Sol on wave 12 (:42-45); Dynamic Duo = 2 Shockwave Colossi on waves 7, 8, 11 (:89-91); Bees = N "Angry Bees" where N is the Bees level varbit (:34-36). Javelin count on waves >= 5 is 2-(wave%2) (:77) so odd waves 1, even waves 2. Reinforcement timing: FORT LosLinks.java:166,201 treats any tracked npc that appears on a tick other than the wave-start tick as a reinforcement; WAVES ColosseumWavesPlugin.java:215-216 opens its reinforcement capture window when ticksSinceWaveStart > 10. These do not give the reinforcement tick; OPEN (wiki/Blert).

## B. Arena, regions, modifiers

| Quantity | Value | Source | Agrees / disagrees |
|---|---|---|---|
| Colosseum region id | 7216 (= 28*256+48, map square m28_48) | FORT ColosseumStateTracker.java:34; WAVES ColosseumWavesPlugin.java:68; LOGGER id_lists_excerpt.txt (LeaderboardRegions.java:36); inf_colo_additions/excerpt.txt:51 | three agree; matches cache_map (square m28_48, LEDGER_cache) |
| Lobby region id | 7316 (= 28*256+148, map square m28_148, the lobby square named in LEDGER_cache) | FORT ColosseumStateTracker.java:33; inf_colo_additions/excerpt.txt:52 | two agree |
| Arena world bounds | x 1808-1840, y 3090-3123 | inf_colo_additions/excerpt.txt:53-56 | SOLSIM ARENA_WEST 19, EAST 34, NORTH 18, SOUTH 33 (Constants.ts:2-5) is a 15-tile span in its own local frame; bop's box is 32 wide (likely the whole instanced scene). Different quantities, not a disagreement; the spec worker should read the walkable arena from cache_map |
| LoS tool grid | 34 x 34 tiles (MAP_WIDTH/MAP_HEIGHT) | SUPA lineOfSight.ts:32-33 | FORT LosLinks.java:247 "max y = 33" agrees |
| LoS scene origin | south-west pillar min tile minus 8 in x and y | FORT LosLinks.java:283-284 | |
| Pillar loc names | PILLAR_CIVITAS01_COLOSSEUM01, ..._02 (ids not in runelite master gameval at fetch: the names were renamed; ids unresolved by this part) | FORT LosLinks.java:277; inf_colo_additions/excerpt.txt:67 | cache_locs.txt of this tree is the id source |
| Player start (sim) | 27, 29 | SOLSIM ColosseumRegion.ts:80-81 | |
| Sol start (sim) | 25, 24 (5x5, south-west corner = origin) | SOLSIM ColosseumRegion.ts:109 | |
| Modifier select script / varbit | 4931 / 9788 (COLOSSEUM_SELECTED_MODIFIER) | FORT ColosseumStateTracker.java:36-37; LOGGER ColosseumHelper.java:33,250 | agree; GV lists VarbitID COLOSSEUM_SELECTED_MODIFIER = 9788 |
| Modifier bit ids 0-13 and level varbits | see FORT Modifier.java:19-32 | FORT | compare against cache enum_5312 (cache_enums_dbrows.txt): the spec worker should diff the two |
| Modifier level varbits | Mantimayhem 4588 (moved by Jagex when added, per FORT Modifier.java comment), Bees 9791, Blasphemy 9790, Doom 10681, Frailty 9796, Myopia 9795, Reentry 9792, Relentless 9798, Solarflare 9797, Volatility 9799 | FORT Modifier.java:19-32 | GV: COLOSSEUM_SOL_GRAPPLE_PENDING 9800, COLOSSEUM_SOL_FAILURES 9810 are named in GV (VarbitID) |

## C. Animation, projectile and graphic ids named by code, and the rig rows they identify

Full machine-readable table with file and line per use: `code_id_map.tsv` (85 rows; columns plugin, file, line, kind, gameval name, id, plugin comment, rig rows as npc/cache_name/ticks/role/tier). Names resolve to ids through GV (runelite/runelite@04b96b7a). Ids were checked against rig/*.tsv of this tree (seq and spotanim rows). A plugin naming an id as an ATTACK of that monster is an observer's statement and promotes the rig candidate from "name"/"rig" tier to "name + observer"; it does not make it grade C by itself.

| id (gameval name) | Plugin says | Rig row (tier) | Effect on the candidate |
|---|---|---|---|
| seq 10847 NPC_JAGUAR_RANGER_CLAWS_ATTACK | LOGGER NpcAttackAnimationIds.java:63 "Jaguar warrior auto" | colosseum_jaguar_warrior attack1, 2.1 ticks (name) | CONFIRMS the attack1 candidate (second source: cache name + observer) |
| seq 10859 NPC_SERPENT_MAGER_CASTING | LOGGER :64 "Serpent shaman auto" | colosseum_standard_mager attack1, 3.33 ticks (name) | CONFIRMS |
| seq 10843 NPC_MINOTAUR_BOSS_ATTACK_MELEE | LOGGER :65 "Minotaur auto" | colosseum_minotaur attack1, 2.0 ticks (rig+name) | CONFIRMS |
| seq 10850 NPC_FREMENNIK_WARBANDER_ARCHER_ATT_COLOSSEUM | LOGGER :66 "Fremennik archer auto" | colosseum_warbander_ranged_female attack, 1.9 ticks | CONFIRMS |
| seq 10853 NPC_FREMENNIK_WARBANDER_MAGE_ZAROS_VERTICAL_CASTING_WALKMERGE | LOGGER :67 "Fremennik seer auto" | colosseum_warbander_mage_male | CONFIRMS |
| seq 10856 NPC_FREMENNIK_WARBANDER_MELEE_HUMAN_SWORD_STAB | LOGGER :68 "Fremennik berserker auto" | colosseum_warbander_melee_male attack, 1.3 ticks | CONFIRMS |
| seq 10892 NPC_COLOSSI_JAVELIN_01_RANGE_ATTACK | LOGGER :69 "Javelin colossus auto" | colosseum_javelin_colossus attack1, 3.0 ticks (rig+name) | CONFIRMS |
| seq 10893 NPC_COLOSSI_JAVELIN_01_ARTILLERY_ATTACK | LOGGER :70 "Javelin colossus toss" | colosseum_javelin_colossus attack2, 3.0 ticks | CONFIRMS attack2 as an attack (the "toss" is the artillery attack) |
| seq 10869 NPC_MANTICORE_01_TRIPLE_THROW | LOGGER :71 "Manticore attack"; ColosseumHelper.java:94,126,186-187 | colosseum_manticore attack1, 3.0 ticks (rig+name) | CONFIRMS |
| seq 10868 NPC_MANTICORE_01_TRIPLE_CHARGE | ColosseumHelper.java:186-187 (read together with the throw to take orb spotanims) | colosseum_manticore transition, 4.0 ticks | CONFIRMS it is the charge phase before the throw |
| seq 10903 NPC_COLOSSI_SHOCKWAVE_01_CLAPATTACK | LOGGER :72 "Shockwave colossus auto" | colosseum_shockwave_colossus attack1, 3.0 ticks (rig+name, `rig/shockwave_colossus.tsv:25`); the same seq is role "unknown" (rig) on the shared-rig rows of javelin colossus, sol_p1 and boss_seated | CONFIRMS attack1 of the Shockwave Colossus (closer, 2026-10-03: this row first said "unknown -> PROMOTES"; the committed rig row was already attack1 rig+name, and `code_id_map.tsv` and `blert/ID_TABLE.md:15` say so). The plugin names no other npc for it, so the three "unknown" rows stay unknown |
| seq 10883 NPC_COLOSSI_FINALBOSS_01_MELEE_ATTACK_TELEGRAPH | LOGGER :73 "Sol Heredit spear attack"; SOLSIM SolHeredit.ts:58 SpearSlow | colosseum_sol_p1 attack2, 6.0 ticks | CONFIRMS (3 sources agree on the id) |
| seq 10884 NPC_COLOSSI_FINALBOSS_01_GRAPPLE_ATTACK_TELEGRAPH | LOGGER :74 "Sol Heredit break"; SOLSIM SolHeredit.ts:59 | attack3, 4.0 ticks | CONFIRMS |
| seq 10885 NPC_COLOSSI_FINALBOSS_01_SHIELDSLAM_TELEGRAPH | LOGGER :75 "Sol Heredit shield attack"; SOLSIM :60 | attack4, 4.0 ticks | CONFIRMS |
| seq 10887 NPC_COLOSSI_FINALBOSS_TRIPLEATTACK_SHORTER | LOGGER :76 "Sol Heredit triple parry attack"; SOLSIM :62 TripleAttackShort | attack6, 11.0 ticks | CONFIRMS |
| seq 10886 NPC_COLOSSI_FINALBOSS_TRIPLEATTACK (long) | SOLSIM SolHeredit.ts:61 TripleAttackLong (not in LOGGER's list) | attack5, 12.0 ticks | CONFIRMS by SOLSIM alone |
| seq 10874 idle, 10878 walk, 10888 death | SOLSIM :56,57,63 (comments) | idle, walk, death (8.33 ticks; SOLSIM deathAnimationLength 8 at SolHeredit.ts:862-864) | agree |
| seq 10882 NPC_COLOSSI_FINALBOSS_01_MELEE_ATTACK | no plugin names it | attack1, 4.0 ticks (rig+name) | NOT confirmed: both simulators and the logger use 10883 for the spear |
| spotanim 2681 / 2683 / 2685 VFX_MANTICORE_01_PROJECTILE_MAGIC / RANGED / MELEE_01 | FORT ManticoreOrbType.java:24-31; WAVES ManticoreHandler.java:58-60; LOGGER ColosseumHelper.java:195-205; Microbot ManticoreProjectilePrayers.java:18-24 | rig spotanim rows (2.0 ticks, role other) | CONFIRMS: these are the orb styles. They are read as spotanims on the NPC (ActorSpotAnim) in FORT LosLinks.java:233-237 and ColosseumHelper.java:186-210, and also tracked as projectiles by Microbot |
| spotanim 2669 / 2670 / 2671 SPOTANIM_COLOSSI_FINALBOSS_01/02/03_MELEE | LOGGER GraphicsObjectIdsToTrack.java:45-49 "Colosseum Sol dust" | colosseum_sol_p1 spotanim (name) | CONFIRMS: ground-slam dust of the spear/shield attacks (the logger's tag is "dust"; which attack is which stays OPEN) |
| spotanim 2689-2691 VFX_COLOSSEUM_CRYSTAL_CHARGE_01_BEAM_01..03 | LOGGER :57-61 "Sol laser scan" | colosseum_beam_crystal 4.1 ticks | CONFIRMS laser charge/scan |
| spotanim 2693-2695 VFX_COLOSSEUM_CRYSTAL_ATTACK_01_BEAM_01..03 | LOGGER :63-67 "Sol laser shot" | colosseum_beam_crystal 4.1 ticks | CONFIRMS laser shot |
| spotanim 2698 VFX_COLOSSEUM_SUNFIRE_LIGHTNING_01_BEAM_01 | LOGGER :69 "Sol sunfire pool" | NOT in rig | NEW candidate: the rig pass missed it. Add to the rig tables |
| npc 12818 COLOSSEUM_MANTICORE, 12811 STANDARD_MAGER, 12812 MINOTAUR, 12813 MINOTAUR_ROUTEFIND, 12817 JAVELIN, 12810 JAGUAR, 12819 SHOCKWAVE, 12821 SOL_P1, 12822 DOOM_SCORPION, 12823 MODIFIER_BEES, 12825 HEALING_TOTEM, 12827 BOSS_SEATED, 12814/12815/12816 WARBANDER ranged/mage/melee | FORT LosLinks.java:84-90; WAVES ColosseumWavesPlugin.java:76-82; LOGGER NpcIdsToTrack.java:662-675, BossIds.java:278-279 | cache_npc.txt same ids and names | CONFIRMS the id <-> role mapping. COLOSSEUM_MINOTAUR_ROUTEFIND (12813) is the Red Flag minotaur (FORT :89 "Minotaur (Red Flag)") |
| npc 12808 | FORT ColosseumStateTracker.java:137 as MINIMUS_12808 (now COLOSSEUM_MASTER) | cache npc 12808 colosseum_master | CONFIRMS Minimus |
| objects COLOSSEUM_MOLTEN_POOL_1 / _2 | LOGGER GameObjectIdsToTrack.java:21, GroundObjectIdsToTrack.java:17 "reentry pool" | ids unresolved here (gameval names removed) | OPEN: Reentry pool locs, resolve via cache_locs |

Not named by any pinned code (so no promotion): the Fremennik archer/mage/berserker projectile ids, jaguar charge/leap, Sol's ranged/magic hits, Doom scorpion, Totemic healing totem effects, minotaur heal projectile ids, javelin and shockwave projectile ids.

## D. Sol Heredit (and hazards) -- SOLSIM, sol_heredit_trainer/js/ (GPL-3, Supalosa engine; player-tested)

Paths below are relative to sol_heredit_trainer/js/. "CACHE agrees" means the cache dump in this tree states the same number (first-party). Everything else is D until a second source agrees.

| Quantity | Value | Source file:line | Other source |
|---|---|---|---|
| Hitpoints | 1500 (1725 in the sim's "echo max hp" option) | mobs/SolHeredit.ts:213 | CACHE agrees: npc 12821 stat4=1500 (cache_npc.txt) |
| Attack / defence / strength / ranged / magic levels | 350 / 200 / 400 / 350 / 300 | SolHeredit.ts:208-212 | CACHE agrees: stat1=350, stat2=200, stat3=400, stat5=350, stat6=300 |
| Size | 5 | SolHeredit.ts:271-273 | CACHE agrees: size=5 |
| Combat level | 1200 | SolHeredit.ts:184-186 | CACHE says vislevel=1563: DISAGREES (the sim label is stale; use the cache) |
| Max hit (melee and magic) | 70 | SolHeredit.ts:281-289 | OPEN (wiki/Blert) |
| Attack bonuses | stab 250, magic 80, range 150 | SolHeredit.ts:219-224 | OPEN |
| Defence bonuses | stab 65, slash 5, crush 30, magic 750, range 825 | SolHeredit.ts:228-234 | OPEN |
| Movement | 2 tiles per tick (maxSpeed), stunned 4 ticks at start | SolHeredit.ts:866-868, :202 | OPEN |
| Phase thresholds (hitpoints) | 1500, 1350, 1125, 750, 375, 150 with lines "Let's start by testing your footwork." / "Not bad. Let's try something else..." / "Impressive. Let's see how you handle this..." / "You can't win!" / "Ralos guides my hand!" / "LET'S END THIS!" | SolHeredit.ts:121-128 | OPEN (overhead texts: second source needed; thresholds as fractions: 100, 90, 75, 50, 25, 10 percent of 1500) |
| First attack | the spear, hedged by its own author: `forceAttack: Attacks \| null = Attacks.SPEAR; // first attack is always a spear?` (the question mark is the source's) | SolHeredit.ts:157 | Blert: the first Sol attack animation after his spawn is the thrust, n=12, 5 ticks in 10 and 6 in 2 (`blert/SAMPLE_SUMMARY.md:862`; an animation, observed) |
| Attack pool | shield x2, spear x2 (autos, double weight), triple-long if phase >= 3 and special ready, triple-short if phase 1-2 and special ready, grapple if phase >= 2 and special ready; special cooldown 2 attacks | SolHeredit.ts:110, :388-401 | OPEN (a guess at weighting: the source comment says "hacky 2x weighting for autos") |
| Spear | anim 10883, freeze 6, damage lands +2 ticks, then end sound +3; alternates a first and second pattern; line length 7; delay to the next attack 7 ticks (phase 0-1) or 6 (phase >= 2) | SolHeredit.ts:408-417, :489, :545 | rig: 10883 = 6.0 ticks |
| Shield | anim 10885, freeze 4, damage +2 ticks, alternating rings (rect x-7..x+12, y-12..y+7 with the inner ring 4 then 5 left clear); next attack in 6 ticks (phase 0-1) or 5 | SolHeredit.ts:421-430, :601-606 | rig: 10885 = 4.0 ticks |
| Triple (short) | anim 10887; parry hits +2 (15 dmg, overhead window 3 ticks), +5 (25, window 2), +8 (35, window 2); next attack 12 ticks (phase 1), 11 (phase >= 2) | SolHeredit.ts:609-615, :670-690 | rig: 10887 = 11.0 ticks |
| Triple (long) | anim 10886; hits +2 (15), +5 (30), +9 (45, window 3); next attack 12 | SolHeredit.ts:618-623, :673-689 | rig: 10886 = 12.0 ticks |
| Grapple | anim 10884, freeze 5; a random equipment slot (body, cape, gloves, legs, feet) is called by overhead text; clicking that slot within the window cancels the damage; otherwise 20 + rand(25) = 20-44; damage at +4; next attack 7 | SolHeredit.ts:626-668, :84-91 (slot texts) | OPEN |
| Parry hit | hit +ticks after the telegraph; if a protection overhead was on in the last N ticks it is unblockable; all protections are switched off after the hit | SolHeredit.ts:700-716 (doParryAttack), :693-698 | OPEN (mechanic is a simulator design; second source needed) |
| Ground slam tile damage | 20 + rand(25) = 20-44 | entities/SolGroundSlam.ts:18 | OPEN |
| Sand pool (phase transition) | damage 5 + rand(5) = 5-9 per tick when on the tile after it ages 2 ticks | entities/SolSandPool.ts:77-84 | OPEN |
| Phase transition | 7 ticks, freeze 5, aggro dropped for 5; pools placed at the player's tile at +1 plus 5 more (4 for the last phase) within 4 tiles; pools clamped to the arena interior | SolHeredit.ts:719-742, :744-767 | OPEN |
| Final phase | every 3rd tick a pool is placed under the player (first after 7 ticks) | SolHeredit.ts:166, :318-323 | OPEN |
| Laser orbs | one orb added per phase transition 1-4, up to 4 (N, E, S, W edges); fire every 25-35 ticks (random) before the last phase and every 12 after; fire freeze 9 ticks; damage 60 + rand(20) = 60-79 | SolHeredit.ts:145-147, :720-740, :801-805, :815-835; entities/LaserOrb.ts:121 (firingFreeze = 9), :183 | OPEN |
| Solar Flare orbs | 4 orbs patrolling a square 4 tiles on a side at (21,20), (28,20), (21,27), (28,27); level 1: 2 ticks per tile, wait 7, damage 5-9; level 2: 2 ticks per tile, wait not set by the code (initial 4), damage 10-19; level 3: 1 tick per tile, wait 2, damage 10-19 | ColosseumRegion.ts:172-175; entities/SolarFlareOrb.ts:11-12, :90-103 | OPEN (Solarflare modifier levels I-III) |

## E. Wave monsters' line-of-sight constants -- SUPA (no licence, quoted)

| Quantity | Value | Source | Other source |
|---|---|---|---|
| Serpent Shaman | size 1, range 10, cooldown 5 | SUPA src/constants.ts:20 | CACHE size default 1 |
| Javelin Colossus | size 3, range 15, cooldown 5 | constants.ts:26 | CACHE size=3 |
| Jaguar Warrior | size 2, range 1, cooldown 5 | constants.ts:27 | CACHE size=2 |
| Manticore | size 3, range 15, cooldown 10 | constants.ts:28 | CACHE size=3 |
| Minotaur | size 3, range 1, cooldown 5 | constants.ts:29 | CACHE size=3; heal range 7 (constants.ts:66, drawn as the minotaur's heal circle) |
| Shockwave Colossus | size 3, range 15, cooldown 5 | constants.ts:30 | CACHE size=3 |
| Player | size 1, range 10 | constants.ts:24 | |
| First attack delay from wave start | an npc cannot attack before tick 3 (DELAY_FIRST_ATTACK_TICKS = 3); it moves from tick 1 and may gain line of sight from tick 2 | constants.ts:69; lineOfSight.ts:940-942 | OPEN (Blert first-attack tick) |
| Manticore charge | an uncharged manticore that gains LoS starts charging: timer 10 ticks (MANTICORE_CHARGE_TIME); after one manticore fires, every other ready manticore is delayed 5 ticks (MANTICORE_DELAY) | constants.ts:60-61; lineOfSight.ts:778-870, :965-985 | OPEN |
| Manticore orb order | "r" = range, mage, melee; "m" = mage, range, melee; with Mantimayhem III also "Mrm","Mmr","rMm","mMr"; the three styles are emitted on three consecutive ticks | constants.ts:43-49; lineOfSight.ts:895, :912-927 | FORT ManticoreOrbOrder.java:20-30 agrees (third orb melee means standard; first orb names the pattern); BUT FORT targets this tool, so not independent. Orb spotanim ids from the cache (2681/2683/2685) are the independent check |
| Attack timing semantic | cooldown decremented once per tick, attack sets cooldown to cd (5 means one attack every 5 ticks) | lineOfSight.ts:746, :897, :903 | OPEN: the cache's attack animation lengths (ticks) are 3.0 for javelin/shockwave, 2.1 jaguar, 3.33 shaman, 2.0 minotaur; the 5-tick cadence is NOT in the cache |
| Line of sight | pillars as blocked tile spans (blockedTileRanges, x-spans per row y 0..33, door 15-19 at rows 0 and 33, walls) | constants.ts:72-207; lineOfSight.ts:532-535, :656-659 | OPEN: reconcile with cache_map |
| Mob type ids in replay URLs | shaman 1, javelin 2, jaguar 3, manticore 4, minotaur 5, shockwave 6, reinforcement shaman 7 | constants.ts:5-13 | same schema as FORT Enemy.java:12-17, WAVES ColosseumWavesPlugin.java:76-82 (not independent). NOTE constants.ts:22-31 NPC_INFO also carries a different `id` field ("legacy") that must NOT be used as an npc id |

## F. Cross-source summary a spec worker needs

1. Agree (two independent kinds of source): Sol id set 10883-10887 (SOLSIM comments, LOGGER list, cache names); sizes 2/3/3/3/3 and Sol stats (SUPA/SOLSIM vs cache_npc); orb spotanims 2681/2683/2685 (four plugins, cache names); the attack anims in section C.
2. Disagree / stale: Sol combat level 1200 (SOLSIM) vs cache 1563.
3. Single-source (D), needs a second source before use: the whole wave table (section A), every cooldown and range in section E, Sol max hit 70, attack pool weights, phase thresholds as hitpoints, laser/orb cadences and all damage ranges in section D, first-attack tick 3, manticore charge 10 / delay 5.
4. Not stated by any pinned code: wave spawn TILES (WAVES records them to a link but has no table; the tiles live in players' shared LoS links), reinforcement tick, the Fremennik, jaguar, minotaur and shockwave attack mechanics beyond cooldown/range, reward tables, glory rules, the pet.
