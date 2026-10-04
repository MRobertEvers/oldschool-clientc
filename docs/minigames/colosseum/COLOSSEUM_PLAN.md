# Fortis Colosseum -- build plan

Written 2026-10-03 by the plan agent of the waves loop (`docs/WAVES_ORCHESTRATOR.md`
section 8). Our server has **no Colosseum content**: this document says what the
rev-239 cache already ships, what the corpus supplies and at what grade, what must be
built and in what order, and which numbers no source settles yet. It writes no
script; every build seam it names is sourced row by row from a spec table written
**before** that seam is built (`encounters/<unit>.tsv`, nine columns, section 6 of the
orchestrator). Nothing here is from memory, and nothing is invented: a number with
no source line below is an open row in section 5.

Paths are relative to `docs/minigames/colosseum/` unless they start with `OSRS-Content/`,
`docs/`, `tools/` or `test/`. Cite shorthands are the corpus index's (`SOURCES.md`
section 10): `W:` = `sources/wiki/wiki_<page>.wikitext`, `N:` =
`sources/newsposts/wiki_Update_<key>.wikitext`, `B:` = `sources/blert/`, `CC:` =
`sources/CODE_CONSTANTS.md`, `ST:` = `sources/transcripts/STATED.md`, `LW:` =
`sources/LEDGER_wiki.md`. `D1`-`D35` are the 35 rows of `SOURCES.md` section 10 in
table order (D1 reinforcement time ... D35 handicap id); `N1`-`N7` are seven more this
plan found (section 4.0). `Mn` rows are section 5.

Grades are the standard's (orchestrator section 3): **A** cache or Jagex, **B** Blert's
OBSERVED rows (never its asserted ones, `B:PROVENANCE.md` section 1), **C** two
independent non-recorder sources, **D** one, **E** none (a disclosed `[Mn]`). FORT,
WAVES and SUPA share one schema and count as one source (`SOURCES.md` section 4). The
2004 source is not a source in this loop and is cited nowhere.

## Contents

1. What the cache ships
2. What the cache does not have, and who supplies it
3. The systems around the fight
4. The units (22), one section each; 4.0 the disagreement index
5. Open measurements
6. Build order
7. What the build must not repeat from the Inferno port

### Definition of done (for the build, not this plan)

The full-run test (orchestrator section 6) enters through the lobby by click, picks a
modifier by click before every wave, fights waves 1-11 with the player's own attacks,
kills Sol Heredit, takes the chest by click, and its tick ledger reads FULL against
the 22 unit tables; every presentation row is asserted from the tick log and seen in
a shot; every `CONTENT_BUGS.md` row this plan's seams open names its fixing commit.

---

## 1. What the cache ships

Dumped by `tools/waves_gate/cache_dump.py --game colosseum` (`sources/LEDGER_cache.md`);
the 1,419-row inventory of these assets is `AV_INVENTORY.tsv`/`.md`. **The cache gives
identities and assets, not the fight** (`AV_INVENTORY.md` "What a plan must know" 1):
no npc record binds an attack, defend or death animation, and no spotanim is bound.

### 1.1 Npcs (`sources/cache_npc.txt`, record header line)

| id | symbol | name | hp (stat4) | size | level | dump line |
|---|---|---|---:|---:|---:|---|
| 12814 | colosseum_warbander_ranged_female | Fremennik warband archer | 50 | 1 | 104 | :219 |
| 12815 | colosseum_warbander_mage_male | Fremennik warband seer | 50 | 1 | 104 | :256 |
| 12816 | colosseum_warbander_melee_male | Fremennik warband berserker | 48 | 1 | 103 | :293 |
| 12811 | colosseum_standard_mager | Serpent shaman | 125 | 1 | 161 | :115 |
| 12810 | colosseum_jaguar_warrior | Jaguar warrior | 125 | 2 | 234 | :76 |
| 12817 | colosseum_javelin_colossus | Javelin Colossus | 220 | 3 | 278 | :330 |
| 12819 | colosseum_shockwave_colossus | Shockwave Colossus | 125 | 3 | 239 | :406 |
| 12812 | colosseum_minotaur | Minotaur | 225 | 3 | 318 | :151 |
| 12813 | colosseum_minotaur_routefind | Minotaur | 225 | 3 | 318 | :185 |
| 12818 | colosseum_manticore | Manticore | 250 | 3 | 320 | :367 |
| 12821 | colosseum_sol_p1 | Sol Heredit | 1500 | 5 | 1563 | :450 |
| 12827 | colosseum_boss_seated | Sol Heredit (seated, no ops) | - | 4 | 0 | :575 |
| 12820 | colosseum_safespot_dying | Pillar | - | 3 | 0 | :441 |
| 12822 | colosseum_doom_scorpion | Doom scorpion | - | - | 0 | :490 |
| 12823 | colosseum_modifier_bees | Bee Swarm | - | 2 | 0 | :517 |
| 12824 | colosseum_beam_crystal | (no name) | 25 | - | 0 | :533 |
| 12825 | colosseum_healing_totem | Healing totem | - | - | 0 | :544 |
| 12826 | colosseum_solar_flare | (no name) | 25 | - | 0 | :560 |
| 12828-12832 | colosseum_{human,manticore,minotaur,colossi,sol}_gib | (gibs, no rig) | - | - | - | :590-:623 |
| 12807 / 12808 | colosseum_master_outside / colosseum_master | Minimus (12808: op1 Start-wave, op3 Leave) | - | - | 0 | :24 / :38 |
| 12809 | colosseum_glory | Gloria | - | - | 0 | :52 |
| 12833 | colosseum_passionate_supporter | Passionate Supporter (op2 Attack) | 10 | - | 7 | :634 |
| 12834-12836 | colosseeum_gladiator_1..3 | gladiators (purpose unsourced) | - | - | - | :680-:724 |
| 13110-13113, 13420/13421 | lobby guards, Ueman and Seia | (Talk-to) | - | - | - | :803-:998 |
| 12857 / 12767 | solheredit_pet / poh_solheredit_pet | Smol Heredit | - | - | 0 | :783 / :5 |

`enum_5318` (`sources/cache_enums_dbrows.txt:757`) keys 1-9 to the nine wave monsters
and is read by nothing: **do not read a wave order into its keys** (`AV_INVENTORY.md`
"What a plan must know" 3).

### 1.2 Locs and the two map squares (`sources/cache_map.txt`, `sources/cache_locs.txt`)

- **Arena, m28_48, region 7216** (world 1792-1855, 3072-3135; `cache_map.txt:7`). The
  fight floor on level 0 is the square bounded by `inviswall_blockrange` 85 at local
  x 18-47, z 20-49 (`:8`). Four `pillar_civitas01_colosseum01` 52490 "Pillar" (3x3,
  `cache_locs.txt:4929`) stand at local x 24-39, z 26-41 (`cache_map.txt:229`, placements
  `maps/m28_48.jl2:2608` on). `colosseum_entrance_outside` 50749 (op1 Enter) at local 4,34
  = world 1796,3106 (`cache_map.txt:268`).
- **Lobby, m28_148, region 7316** (world 1792-1855, 9472-9535): `colosseum_entrance`
  50751 "Entrance" op1 Enter at local 18,34 (`:313`), `colosseum_exit_lobby` 50750
  "Stairs" at 4,34 (`:312`, `:372`), `colosseum_bank` 50748 at 13,29 (`:311`, `:371`),
  `colosseum_scoreboard` 50747 (View, View-stats, View-glory) at 12,41 (`:310`, `:370`).
- **Placed by no map, so spawned by the server** (`cache_locs.txt:839-1010`):
  `colosseum_reward` 50741 "Rewards Chest" (Search; spawn anim
  `chest_colosseum01_reward01_spawn_01`), `colosseum_exit` 50752 "Gate" (Exit,
  Quick-exit), `colosseum_molten_pool_1..3` 50743-50745, `colosseum_holy_fire` 50746
  "Molten Sand", `colosseum_wave_egg_shell` 50742.
- Region ids 7216/7316 agree across three plugin sources and the cache (`CC:B`). Whether
  the fight runs in an instance copy is **not cache data** (`AV_INVENTORY.md` section 1).

### 1.3 Interfaces and the vars that drive them

Interfaces (`sources/cache_interfaces.txt`, record line): 626 `colosseum_intermission`
(`:375`) and 865 `colosseum_intermission_2` (`:1298`), the choice between waves, drawn by
clientscripts 4931-4965 from `varb9788_colosseum_selected_modifier`, `varc1194` (selected
card) and `varc1196` (tab), with invs `colosseum_rewards_future`/`_previous` (4 slots each)
as the reward preview; 866 `colosseum_reward` "Wave Complete!" (`:1803`, clientscript 4956
at `:3684`); 246 `colosseum_reward_chest` (`:3`) and 864 `colosseum_reward_chest_2`
(`:996`, clientscripts 4923-4930 over inv `colosseum_rewards`, 16 slots, tab `varc1195`);
867 `colosseum_scoreboard` (`:2247`, hooks only); 592 `dizanas_quiver` (`:198`).

Vars (`sources/cache_vars.txt`, line). **Read by a clientscript, so the server must set
them for the client to draw right:** `varp4130_colosseum_glory` (`:17`, tiers by 4922),
`varp4132_colosseum_current_glory` (`:30`), `varp4134_colosseum_wave_damage_taken` (`:43`),
`varp4136_colosseum_last_wave_duration` (`:56`), `varp4137_colosseum_total_duration`
(`:65`), `varb9788` (`:112`), the ten modifier rank varbits `varb4588`, `9790`-`9799`,
`10681` (`:98-:173`, `:313`; Bees! reads the one named *toxicity*), `varb9800
_colosseum_sol_grapple_pending` (`:180`, read by `wear_updateslot_546`),
`varb9801_colosseum_doom_stacks_client` (`:187`, the buff bar). **Read by nothing in
the cache (server state, names are not purposes):** `varp4131` total waves completed
(`:26`), `4133` wave start time (`:39`), `4135` last modifier glory (`:52`), `4138`
rewards_1 (`:71`), `4139` killtime (`:75`), `varb9802` loot opened, `9804` Gloria met,
`9806` herb patch chat, `9807` master intro, `9808` supporter speech, `9809` boss
cutscene seen, `9810` Sol failures, `9811`/`9812` killtime best/latest, `9993` entry
warning, `11410` highest wave (`:194-:320`). Collection log `varb9990` (`:292`), CA total
`varb9778` (`:105`).

### 1.4 The modifier table (`sources/cache_enums_dbrows.txt:726-745`, structs `:884-1033`)

`enum_5312` maps index 0-13 to fourteen structs; Blert's handicap ids are the same
indices (`B:plugin/src/main/java/io/blert/challenges/colosseum/Handicap.java:29-43`) and
a recorded option is `index + 30 x (level - 1)` (`B:blert/challenge-harder/src/processing/colosseum.rs:27`).
Each struct carries name (`param_1896`), level texts (`1897-1899`), has-levels (`1901`),
glory value (`1903`) and icon (`1914`); `AV_INVENTORY.md` section 3 has the table:

| idx | struct | modifier | levels | glory 1903 | rank varbit |
|---:|---:|---|---:|---:|---|
| 0 | 915 | Mantimayhem | 3 | 150 | varb4588 |
| 1 | 891 | Reentry | 3 | 150 | varb9792 |
| 2 | 892 | Bees! | 3 | 150 | varb9791 (toxicity) |
| 3 | 893 | Volatility | 3 | 100 | varb9799 |
| 4 | 894 | Blasphemy | 3 | 100 | varb9790 |
| 5 | 897 | Relentless | 3 | 200 | varb9798 |
| 6 | 898 | Quartet | 1 | 100 | - |
| 7 | 899 | Totemic | 1 | 200 | - |
| 8 | 900 | Doom | 3 | 200 | varb10681 |
| 9 | 901 | Dynamic Duo | 1 | 150 | - |
| 10 | 902 | Solarflare | 3 | 250 | varb9797 |
| 11 | 903 | Myopia | 3 | 200 | varb9795 |
| 12 | 906 | Frailty | 3 | 200 | varb9796 |
| 13 | 907 | Red Flag | 1 | 250 | - |

The struct texts are the cache's own statement of each effect (e.g. struct_891 Reentry
"Javelins leave a temporary pool of molten sand where they land."). The numbers behind
the texts are not cache data (section 2).

### 1.5 Music, jingle, sounds

Song 782 "Are You Not Entertained?", unlock hint "in the Fortis Colosseum."
(`sources/cache_music.txt:8-10`); jingle 305 `glorious_champion_fortis_colosseum`
(`:4`). No `docs/audio/music_regions.tsv` row names region 7216 or 7316. Sounds
(`sources/cache_sounds.txt`): the synth menus `synth_bosscolossi` (291, Sol),
`colosseummodifiers` (61), `manticore` (48), `javelincolossi` (19), `shockwavecolossi`
(14), `minotaur` (14), `glaiveofralos` (11); **332 sounds are in a menu only, with no
binding**: the server plays them and the cache does not say when (`AV_INVENTORY.md`
"Re-check" list).

### 1.6 Items and the collection log

Collection log `enum_5414` via struct_909 (`AV_INVENTORY.md` section 6): Smol heredit
28960, Dizana's quiver (uncharged) 28947, Sunfire fanatic helm 28933 / cuirass 28936 /
chausses 28939, Echo crystal 28942, Tonalztics of ralos (uncharged) 28919, Sunfire
splinters 28924, uncut onyx 6571. The quiver has 18 objs (charged, uncharged, infinite,
broken, max cape and hood) in `sources/cache_objs.txt`.

### 1.7 What our tree already has

- `OSRS-Content/osrs239-content/server/scripts/quests/quest_twilightspromise/scripts/twilightspromise.rs2:351-370`
  owns `[oploc1,colosseum_entrance_outside]`: at Meat and Greet stages it teleports to
  `^mg_arena_coord` 0_28_48_31_33, at Twilight's Promise stages to the lobby
  (0_28_148_27_12 or 0_28_148_13_50), else prints "The Colosseum entrance.". `:372`
  owns `[oploc1,colosseum_exit_lobby]` (teleport 0_28_48_4_34). `maplink.dbrow:445-456`
  links the stairs. `tele_destinations.rs2:848` maps "fortis_colosseum" to 0_28_48_33_37.
  **`colosseum_entrance` (lobby Enter) and `colosseum_master` have no handler.**
- Rewards built elsewhere: Dizana's quiver (`interface_equipment/scripts/quiver.rs2`,
  `quiver.varp`, `player_ranged.rs2:510`), the tonalztics
  (`skill_combat/scripts/player/gear/tonalztics_of_ralos.rs2` and its special), the
  menagerie pet (`poh_menagerie.rs2:99,146,185`), the jewellery-box teleports.
- `npc_combat/c/colosseum_*.combat` (34 generated ledgers) are **not sources**; Sol's
  are wrong (`CONTENT_BUGS.md` RIG-1..RIG-4: he is given the javelin colossus's attack
  and death).

---

## 2. What the cache does not have, and who supplies it

"Best grade" is the highest the corpus can reach for the row today; a row below C is
an open measurement in section 5.

| Quantity | Sources (file:line) | Best grade | Open |
|---|---|---|---|
| Wave table, tick-0 set | Blert tick-0 multisets, 18 runs, all 12 waves (`B:SAMPLE_SUMMARY.md:3-26`); wiki (`W:Fortis_Colosseum_Strategies:882-904`); FORT (`CC:A`, `fortis_colosseum/WaveSpawns.java:34-97`). All three read the same | **B** | D4, D34 |
| Reinforcements, which and when | Blert: every reinforcement spawns on wave tick **66**, waves 2-11 (`B:SAMPLE_SUMMARY.md:874-897`); wiki "40 seconds" (`W:Fortis_Colosseum_Strategies:876`, `W:Fortis_Colosseum:35`) = 66.7 ticks; one guide 45 s (`ST:12`) | **B** for 66 and the kinds; wave 1 open | D1, D5, M1-M2 |
| Spawn tiles, tick 0 and reinforcement | Blert spawn tiles (`B:SAMPLE_SUMMARY.md:28-830`, spawn index `:967`); wiki "completely random", "12 default spawns" (`W:Fortis_Colosseum:35`, `W:Fortis_Colosseum_Modifiers:89`), north or south gate "closest" (`W:Fortis_Colosseum:35`); no code (`CC:F.4`) | B for observed tiles; the rule E | D7, M3-M5 |
| Attack speeds | Blert gaps: Fremennik 6, jaguar/shaman/javelin/shockwave/minotaur 5, manticore 10 (`B:SAMPLE_SUMMARY.md:831-851`); wiki (`LW:182-286`); SUPA (`CC:E`) | **B** | D8 |
| Attack styles, ranges, max hits | wiki infoboxes (`LW:178-339`); SUPA ranges (`CC:E`); max hits disagree (D10, D11) and no recorder sees a hit | C (style, range), D (max hits) | M10-M11 |
| Attack animations | Blert NPC_ATTACK ids (`B:ID_TABLE.md`) promote the rig rows for all nine monsters and four Sol telegraphs | **B** | M40-M41 |
| Projectiles, impacts, hit delays | spotanim names only (`RIG_ANIMATIONS.md`); Blert records no projectile | D (name) / E (delay) | M9, M10, M12 |
| Sol Heredit's sequence | Blert animation sets and dust/laser/pool events (`B:SAMPLE_SUMMARY.md:1175-1187`); wiki Sol + Strategies; SOLSIM (`CC:D`, GPL-3, never copied); guides | B (animations, first attack, laser scan->shot), D (hit ticks, damage, pool rule) | D16-D25, M19-M28 |
| Modifier numbers | Jagex posts (`SOURCES.md` 1.2: bees 12 ticks/50 respawn, doom 15/10/5, relentless 33/66/all and +1/+3/+6, totemic 30 % and 2 min, Mantimayhem tiers); wiki Modifiers page; FORT | **A** for the posted numbers, D for the rest | D26, M29-M30 |
| Reward table | Jagex posts: unique rates per wave and weights 9/16, 6/16, 1/16 (`N:Undead_Pirates_Colosseum_Changes_more:134-179`); wiki chest page normal tables (`W:Rewards_Chest_Fortis_Colosseum:119-340`) | A (uniques), D (normal tables) | D2, D3, M32-M33 |
| Glory | cache clientscripts 4956/4943/4922 (client display, `sources/cache_interfaces.txt:3450-3725`, `:2960`); wiki Glory page; Jagex First Look (performance-based, `N:Fortis_Colosseum_First_Look_Rewards:120-122`) | A (display, tiers), C/D (server formula) | N1, N2, M31 |
| Pet, quiver | Jagex: pet 1/200 on wave-12 completion, quiver guaranteed, quiver trade 4,000 splinters or 1/200 (`N:Varlamore_Tweaks_Drop_Rates:108,215-224`) | **A** | - |
| Death, fee | wiki 75 % up to 125,000 until 100 waves (`W:Fortis_Colosseum:177`); posts 50 -> 100 waves (`N:Varlamore_Part_One:38`, `N:Easter_Varlamore_Updates:64`) | A (100 waves), D (125,000 cap) | D27, M34 |
| Entry requirement and fee | wiki: Children of the Sun (`W:Fortis_Colosseum:21`); Minimus must be spoken to once (`W:Minimus:21`); no source states an entry fee | D | M35 |
| Minimus dialogue | not pinned: the wiki page carries a `Hastranscript` template only (`W:Minimus`) | E | M35 |

---

## 3. The systems around the fight

Each paragraph is the spec rows a seam must carry, with its sources and its grade.
The rows go into the unit table named in brackets.

**3.1 Entry and fee** [`colosseum_entry_and_minimus`]. Requirement: Children of the Sun
(`W:Fortis_Colosseum:21`, D; the quest is `quests/quest_childrenofthesun/`). Minimus
"must be spoken to at least once before entering the arena" (`W:Minimus:21`, D; the
cache's `varb9807_colosseum_master_intro` is the likely carrier, unsourced). No source
states an **entry fee**; none is built (M35). DMM worlds refuse a skulled player
(`N:Permanent_Deadman_World_345_Improvements:51`): not in scope for our worlds, recorded
so nobody adds it to normal worlds. The path is outside entrance 50749 -> lobby ->
lobby entrance 50751 -> arena; the outside handler already exists for two quests
(section 1.7) and **must be extended, not duplicated** (`AV_INVENTORY.md` "What a plan
must know" 7).

**3.2 Minimus and his dialogue** [`colosseum_entry_and_minimus`]. Two records: 12807
outside, 12808 in the arena with op1 Start-wave and op3 Leave (`cache_npc.txt:24,38`,
A). Between waves "if players do not pick a handicap for the next wave, Minimus will
appear next to them" (`W:Minimus:23`, D); Blert counts the wave by Minimus's spawns
(`B:PROVENANCE.md` ASSERTED "The wave number"), so he spawns once per intermission
(when and where: M36). He takes unblessed quivers for 4,000 splinters or a 1/200 pet
roll (`N:Varlamore_Tweaks_Drop_Rates:108,224`, A). His words are **not pinned** (M35):
the spec pass fetches `Transcript:Minimus` before the seam, or the seam ships the
cache's op text only.

**3.3 The instance and the arena build** [`arena_and_spawn_tiles`]. The arena is
m28_48 with its four 3x3 pillars (section 1.2, A). Our Inferno builds its arena with
`~map_instance_from_square(^inferno_template)` and frees the handle on leave
(`OSRS-Content/.../minigame_inferno/scripts/inferno.rs2:216-238`, `:111-125`): the
Colosseum seam reuses that proc; whether Jagex instances it is not cache data (Blert
records world coordinates inside region 7216, `B:PROVENANCE.md` "Challenge start").
**The pillars are cache locs placed by the map**, so line of sight through them is the
engine's loc blockrange, not a script (lesson 7.2). The fight floor: bop's box
x 1808-1840, y 3090-3123 (`sources/inf_colo_additions/excerpt.txt:53-56`, D) and the
walled square local 18-47 x 20-49 (`cache_map.txt:8`, A): settle the walkable tiles from
the cache map.

**3.4 The wave loop** [`wave_table`, `wave_reinforcements`]. Twelve waves; tick-0 set
per section 4.1; reinforcements on wave tick 66 (B). The "Wave: N" chat message is
Blert's tick 0 (`B:SAMPLE_SUMMARY.md:28-30`). A wave ends when its last monster dies;
a reinforcement spawning on the clearing tick is killed (`W:Fortis_Colosseum:35`, D,
M5). Between waves: wave-complete interface 866, Minimus, the modifier choice, then
Start-wave (M37 for the delay). Special attack energy does **not** regenerate between
waves (`N:Undead_Pirates_Colosseum_Changes_more:215`, A); acid pools (molten sand) deal
no damage between waves (`N:Undead_Pirates_Tweaks_Varlamore_CAs_More:46`, A); doom
stacks reset at the end of each wave (`N:Undead_Pirates_Colosseum_Changes_more:80`, A).
What else is restored between waves is open (M43).

**3.5 The between-wave modifier choice** [`modifier_system`]. Three options per
intermission (wiki Modifiers page; Blert `HANDICAP_CHOICE` options,
`B:SAMPLE_SUMMARY.md:1160-1173`). Observed: before wave 1 the options were indices 4, 5
and 12 (Blasphemy, Relentless, Frailty) in **18 of 18** runs; Mantimayhem (0/30/60) is
never offered for wave 12, as the post says (`N:Varlamore_Tweaks_GameJam_V_Commences_More:36`);
Dynamic Duo is "not given after wave 11" (`W:Fortis_Colosseum_Modifiers:91`). The offer
rule is M30. The client draws the cards from enum_5312 and the rank varbits; the server
sets `varb9788`, the rank varbits and the preview invs (section 1.3).

**3.6 The reward pool and the cash-out** [`reward_pool_and_cash_out`]. Each completed
wave adds a reward (`W:Fortis_Colosseum:179`, D); before each wave interface shows this
wave's loot and the next's (`W:Fortis_Colosseum:198`, D; invs `colosseum_rewards_future`
/ `_previous`, A). Leaving via Minimus between waves cashes out; dying, logging out or
teleporting out forfeits (`W:Fortis_Colosseum:179`, D). Ending a run or killing Sol
spawns the Rewards Chest 50741 in the arena centre with Minimus beside it
(`W:Fortis_Colosseum:198`, D). Rates: section 4.15.

**3.7 Glory** [`glory`]. Earned per completed wave from damage taken, speed, and the
modifiers held (`N:Fortis_Colosseum_First_Look_Rewards:120-122`, A); kept as a personal
best per run, not cumulative (`W:Glory:9`, D); kept on a later death, not on a teleport
out (`W:Glory:5`, D). The client's display formula is cache code (4956; section 4.16).
Tier unlocks at 2,000 / 5,000 / 8,000 / 12,000 / 16,000 / 20,000 (cache 4922 at
`cache_interfaces.txt:2960`, A; the wiki names the unlocks: lobby bank chest, Civitas
respawn, Oriana and the house-agents, ring of dueling teleport, the disease-free herb
patch, the Fortis Salute emote 53, `W:Glory` rewards table). Gloria (12809) explains it.

**3.8 The quiver and the pet** [`quiver_and_pet`]. Dizana's quiver is "Only acquired
upon a completion of the Colosseum" (`N:Varlamore_Tweaks_Drop_Rates:216`, A). The pet
Smol Heredit is 1/200, not affected by any other factor; the post's footnote says it
"can only be rolled on completion of Wave 11" (`N:Varlamore_Tweaks_Drop_Rates:222`, A)
while the chest page says "completing wave 12 ... a flat 1/200 chance"
(`W:Rewards_Chest_Fortis_Colosseum:112`, D): disagreement N5 (the same wave-numbering
question as D2). A spare quiver goes to Minimus for 4,000 splinters or another 1/200
(A, 3.2).

**3.9 Death and what is lost** [`death_and_fee`]. Death in the arena respawns the
player in the lobby with their grave (`W:Fortis_Colosseum:177`, D); the reclamation fee
is reduced 75 % until 100 completed waves (A, the cap is D, D27). The pooled loot is
lost (3.6). Doom, under its modifier, kills outright at 15/10/5 stacks (A).

**3.10 Pause and logout** [`death_and_fee`]. No source describes a pause. Logging out
forfeits the pooled loot (`W:Fortis_Colosseum:179`, D); stats are restored only when
leaving through Minimus (same line). Where a player who logged out mid-wave logs back
in, and whether combat delays the logout, are M44. **There is no pause to build**:
the build must not invent the Inferno's.

**3.11 Music** [`colosseum_music`]. "Are You Not Entertained?" unlocks "upon starting
the first wave, not by simply entering" (`W:Are_You_Not_Entertained:20`, D; cache hint
A). The jingle "Glorious Champion" plays on defeating Sol (`W:Fortis_Colosseum:197`, D;
jingle 305 A). Region binding: M45.

**3.12 Combat achievements** [`colosseum_combat_achievements`]. Thirteen tasks
(`N:Undead_Pirates_Tweaks_Varlamore_CAs_More:116`, A; thirteen structs with
`param_1312=25`, 929-945, A). Each task is a spec row whose test drives it.

**3.13 Collection log and records** [`reward_items`, `colosseum_combat_achievements`].
`enum_5414` (A) lists the logged items; `varb9990` is the completed bit; kill times
(`varp4139`, `varb9811/9812`), highest wave (`varb11410`) and total waves (`varp4131`) are
server state with no cache reader (M46). Glory varp4130 is on the hiscores
(dbrow `hiscores_activity_colosseum_glory`, A). A valuable claim sends a clan broadcast
(`N:Pet_Insurance_Rework_More:34`, A).

---

## 4. The units

The 22 unit ids are `SOURCES.md` section 11's. Each unit's table is
`encounters/<unit>.tsv` with a `<unit>.scope.tsv` sidecar; mechanic, presentation and
reward rows sit side by side. "Rig" tiers are `RIG_ANIMATIONS.md`'s (`bound` >
`rig+name` > `rig+sound` > `rig` > `name`); **B-confirmed** means Blert observed that
animation id on that npc (`B:ID_TABLE.md`), which promotes the candidate to the
monster's attack marker (not its hit tick). Anything else stays a candidate until a
plugin id or a picture promotes it (owner rule 4).

### 4.0 Disagreement index

D-rows are `SOURCES.md` section 10 in table order; the plan gives no verdict on any.

| Row | Quantity | Unit(s) | What settles it |
|---|---|---|---|
| D1 | reinforcement time 40 s / 66 ticks / 45 s | wave_reinforcements | Blert's 66 is observed in every wave 2-11 stream; 40 s = 66.7 ticks; the 45 s guide may count from another origin (`ST:118`) |
| D2 | unique roll numbered by wave completed vs reward wave | reward_pool_and_cash_out | quote the post's table header (`N:Undead_Pirates_Colosseum_Changes_more:130-134`) beside the chest page's (`W:Rewards_Chest_Fortis_Colosseum:35`) |
| D3 | unique rates, Apr 2024 vs superseded | reward_pool_and_cash_out | the later post is dated; both are A |
| D4 | tick-0 set: second archer/seer in 1 run per wave, second shockwave in 2 runs of wave 11 | wave_table, modifier_system | those runs' handicap choices (Quartet idx 6, Dynamic Duo idx 9) from the same streams |
| D5 | wave 1 reinforcement (wiki/FORT jaguar; Blert none, no wave 1 lasted 66 ticks) | wave_reinforcements | M2 |
| D6 | "new enemies when two javelins are present" sentence vs the wave table | wave_table | the table is B; the sentence is one line of one page |
| D7 | spawn locations random vs observed tiles | arena_and_spawn_tiles | M3, M4 |
| D8 | attack cadence (wiki, Blert, SUPA agree; cache lengths are not cadence) | every monster | already B; listed so nobody reads a sequence length as a cadence |
| D9 | javelin special every 4th or 5th | javelin_colossus | M8 |
| D10 | shaman max hit 28 / 27 / 56 | serpent_shaman | M11 |
| D11 | manticore max hit 31/36/31 vs 34 | manticore | M11 |
| D12 | manticore burst timing | manticore | M12 |
| D13 | manticore orb order | manticore, modifier_system | Blert observed first orbs (558 mage-first, 406 range-first); orbs 2-3 asserted: M12, M13 |
| D14 | manticore pair copy (15 tiles + LoS) vs alternate every 5 | manticore | M14 |
| D15 | berserker hp 48 (cache, wiki, post) vs 50 (Blert constant) | fremennik_trio | cache is A; Blert's constant is asserted, not observed |
| D16 | Sol combat level 1200 (SOLSIM) vs 1563 (cache) | sol_heredit_attacks | cache is A |
| D17 | Sol max hit 44 / 45 / 70 | sol_heredit_attacks | M19, M26 |
| D18 | grapple window 3 / 4 / 5 ticks | sol_heredit_attacks | M25 |
| D19 | triple spacing | sol_heredit_attacks | M26 |
| D20 | Sol attack delays | sol_heredit_attacks | M20 |
| D21 | Sol hazard shapes | sol_heredit_attacks | M21 |
| D22 | Sol laser damage and cadence | sol_heredit_phases | M23 |
| D23 | Sol phase thresholds (wiki and SOLSIM agree in value; one is a simulator) | sol_heredit_phases | M22 |
| D24 | Sol first attack | sol_heredit_attacks | Blert observed thrust first in 12/12 |
| D25 | Sol animation names (10886 vs 10887 for the triple) | sol_heredit_attacks, wave_presentation | M41 |
| D26 | totem heal 30 % vs 40 % -> 30 %, respawn 1 vs 2 min | modifier_system | the post is A and dated; M42 for the heal itself |
| D27 | death fee: 75 % up to 125,000 until 100 vs 50 waves | death_and_fee | the later post is A; M34 for the cap |
| D28 | tonalztics 0-75 % vs 0-50 % | reward_items | not built here (the weapon exists in our tree) |
| D29 | wave length (Blert only) | wave_table | no disagreement in value; a sanity bracket for tests |
| D30 | "jaguar mager" | jaguar_warrior, serpent_shaman | no source names one; the brief's term folds into the two units |
| D31 | minotaur ids 12812 / 12813 | minotaur, modifier_system | M50 |
| D32 | Minimus npc id | colosseum_entry_and_minimus | cache 12808 is A |
| D33 | arena bounds (bop's box vs SOLSIM's local span) | arena_and_spawn_tiles | different quantities; read the cache map |
| D34 | wave-12 Fremennik under Quartet | wave_table, sol_heredit_attacks | M49 |
| D35 | handicap id schemes | modifier_system | settled by this plan: Blert `Handicap.java:29-43` indices equal enum_5312's (section 1.4) |
| D43 | wiki: minotaurs "immediately move" "unlike other reinforcements" vs Blert: jaguar and shaman also change tile one tick after spawning (jaguar 68 of 68, shaman 63 of 74 on tick 1, 11 on tick 2) | wave_reinforcements | `encounters/wave_reinforcements.queries.py:q_movement` (B); the wiki sentence may mean the others stand still when their target is far; the row ships the observed 1 tick |
| D44 | Fremennik cadence: cache `attackrate` param 5 on all three records (`sources/cache_npc.txt:244,281,318`, also 5 on the jaguar, shaman, minotaur, colossi) vs Blert gaps 6 (230 archer, 94 seer, 34 berserker attacks, every one on its own phase) and wiki 'attack speed 6', 'fixed 6 tick cycle'; the manticore's param 10 agrees with its observed 10 | fremennik_trio | settled for the build by the observed 6 (B); the param is not the cadence for this trio |
| D45 | Fremennik seer's side: wiki 'seer to their east' vs Blert 37 south, 27 east, 7 west of 94 attacks (`encounters/fremennik_trio.queries.py:q_side`) | fremennik_trio | M58 |
| N1 | glory for completing wave 12: cache 4956 adds 1000 (`sources/cache_interfaces.txt` 4956 lines 20-23 of `torirs_colosseum_wave_complete.cs2`) vs the wiki's 1,200 (`W:Glory:87-88`) | glory | M31 |
| N2 | modifier glory: 4956 adds `param_1903` once; 4943/4930 multiply by rank; the wiki's per-level values are base x level (`LW:359-394`) | glory, modifier_system | M31 |
| N3 | minotaur heal radius: "within 6 tiles" (`W:Minotaur_Fortis_Colosseum:43`) vs "no more than 7 tiles" (`:52`) and SUPA 7 (`CC:E`) | minotaur | M15 |
| N4 | CA "Speed-Chaser" 28:00 (cache struct, `AV_INVENTORY.md:245`; `W:Colosseum_Speed_Chaser:7`) vs 30:00 at release (`N:Undead_Pirates_Tweaks_Varlamore_CAs_More:233`) | colosseum_combat_achievements | dated; the rev-239 cache is what ships |
| N5 | pet rolled on wave 11 completion (post) vs wave 12 (chest page) | quiver_and_pet | same as D2 |
| N6 | Relentless bonus: "Bonus 'minimum hits' ... 1, 3 and 6 per tier" (`N:Undead_Pirates_Colosseum_Changes_more:106`) vs "max hit increased by 1/3/6" (`W:Fortis_Colosseum_Modifiers:172,177,182`) | modifier_system | M29 |
| N7 | Sol's barricaded arena: 16x16 with four corner tiles (`W:Fortis_Colosseum:184`) vs roughly 16x15 within the pillars (`W:Fortis_Colosseum_Strategies:1414`) | sol_heredit_phases, arena_and_spawn_tiles | M28 |

`SOURCES.md` section 11 also gives the serpent shaman "the heal to other monsters below
75 percent"; no pinned line says so (`W:Serpent_shaman` has no heal; the heal rule is
the minotaur's, `W:Minotaur_Fortis_Colosseum:43-52`). The shaman table carries no heal
row unless a source turns up.

### 4.1 `wave_table`

The twelve tick-0 sets. Blert (18 runs), the wiki and FORT agree (B):

| Wave | Tick 0 (Fremennik = archer + seer + berserker) | Reinforcements on tick 66 | Blert wave length median (min-max) |
|---:|---|---|---|
| 1 | Fremennik, shaman | jaguar (wiki, FORT; never observed, M2) | 34 (23-58) |
| 2 | Fremennik, shaman, javelin | jaguar | 59 (51-102) |
| 3 | Fremennik, shaman, 2 javelin | jaguar | 123 (90-157) |
| 4 | Fremennik, shaman, manticore | jaguar, shaman | 104 (47-155) |
| 5 | Fremennik, shaman, javelin, manticore | jaguar, shaman | 167 (120-205) |
| 6 | Fremennik, shaman, 2 javelin, manticore | jaguar, shaman | 189 (159-234) |
| 7 | Fremennik, javelin, manticore, shockwave | minotaur | 164 (119-262) |
| 8 | Fremennik, 2 javelin, manticore, shockwave | minotaur | 190 (154-269) |
| 9 | Fremennik, javelin, 2 manticore | minotaur | 184 (143-309) |
| 10 | Fremennik, 2 javelin, 2 manticore | minotaur, shaman | 256 (182-344) |
| 11 | Fremennik, javelin, 2 manticore, shockwave | minotaur, shaman | 237 (166-339) |
| 12 | Sol Heredit (no Fremennik, D34) | none | 223 (202-258) |

Sources: `B:SAMPLE_SUMMARY.md:3-26` (sets), `:874-897` (reinforcements), `:952-965`
(lengths); `W:Fortis_Colosseum_Strategies:882-904`; `CC:A`. Modifier variants: Quartet
adds a fourth Fremennik (FORT `WaveSpawns.java:51`, D; Blert's one-run extra archer or
seer per wave is the observed side, D4); Dynamic Duo a second shockwave on waves 7, 8
and 11 (FORT `:89-91`, D; observed in 2 runs of wave 11). FORT's javelin count on waves
5+ is `2 - (wave % 2)` (`CC:A`): a reading of the same table, not a rule to build.
**Mechanic rows:** each wave's set and count; which kinds are random within the set
(none observed: every run's set is identical apart from the modifier variants).
**Presentation rows:** the "Wave: N" message on tick 0; each monster's spawn sequence
(section 4.22). **Reward rows:** none (4.15). Disagreements D4, D6, D29, D34. Open M49.

### 4.2 `wave_reinforcements`

Every reinforcement in the sample spawned on wave tick **66** (`B:SAMPLE_SUMMARY.md:874-897`,
B; wave 2 n=5, wave 3 n=18, wave 4 9+9, waves 5-6 18+18, 7-9 minotaur 18, 10 17+17, 11
11-12). Wave 4's 9-of-18 counts are runs that cleared before tick 66 (wave 4 min 47).
The wiki: they "arrive from either the north or south gates, depending on which one they
are closest to"; one spawning "in the same tick that the wave is considered cleared"
is killed (`W:Fortis_Colosseum:35`, `W:Fortis_Colosseum_Strategies:876`, D). Observed
north tiles (1823-1825, 3120) (`B:SAMPLE_SUMMARY.md:28-830`). **Mechanic rows:** tick
66, the kinds per wave, the gate rule, the same-tick kill, first attack after a
reinforcement spawn (Blert: shaman mode 8, jaguar and minotaur spread 3-131 because
they walk in, `B:SAMPLE_SUMMARY.md:853-861`). **Presentation:** the spawn sequences
(minotaur `_spawn` 10845 rig+name). Techniques: "Stand on tile A as reinforcements
spawn", "Reinforcement timer" (`sources/wiki/TECHNIQUES.md:94-105`). D1, D5. Open M1,
M2, M4, M5, M6.

### 4.3 `arena_and_spawn_tiles`

The arena square, the pillars, the twelve default spawn tiles, the reinforcement
tiles and line of sight (section 3.3). Sources: the cache map (A), Blert tick-0 and
spawn-index tiles (`B:SAMPLE_SUMMARY.md:28-830`, `:967-1159`, B for each observed tile;
note tick-0 positions are where the npc stood when the client first saw it, not a
spawn time), the wiki's unnamed tile markers (`W:Module_Tile_markers_Colosseum_json`,
region 7216), SUPA's blocked-tile spans (`CC:E`, unlicensed, quoted only), the
"12 default spawns" (`W:Fortis_Colosseum_Modifiers:89`), player start (SOLSIM 27,29,
`CC:B`, D), Sol's start (25,24, D). **Mechanic rows:** walkable tiles; pillar
footprints from `maps/m28_48.jl2:2608-2611`; line of sight through each pillar both
ways (the test reads `t.world.los`); spawn tiles per kind; gates. Technique rows:
"Jaguar warrior safespot (NW pillar)", "Destack / pillar run"
(`TECHNIQUES.md:56`, `:88`). **Presentation:** none of its own. D7, D33. Open M3, M4.
Also `colosseum_safespot_dying` 12820 "Pillar" (size 3, no rig): purpose unsourced;
nothing is built on it until a source names it.

### 4.4 `fremennik_trio`

Archer 12814 (ranged), seer 12815 (magic), berserker 12816 (melee), hp 50/50/48 (A;
the berserker's 50 -> 48 is a post, `N:Pride_2024:17,123`). Attack on a fixed **6-tick
cycle relative to wave start**, melee only when adjacent and not moving
(`W:Fortis_Colosseum_Strategies:1237`, `W:Fremennik_warband_berserker:48`, D; Blert gaps
6, B). Attacking with a member's weakness always hits for a max hit (`W:Fortis_Colosseum:62`,
`W:Fortis_Colosseum_Strategies:678`, D). Posts: ranged level raised, melee max hit
lowered (`N:Varlamore_Tweaks_Drop_Rates:103-104`, A, no number). **Animations:** archer
10850, seer 10853, berserker 10856 (rig tier `name`, **B-confirmed**); defends 10851,
10854, 10857 and deaths 10852, 10855, 10858 (`name`, candidates; their death ends on a
20,000-cycle frame, `AV_INVENTORY.md` "What a plan must know" 9: despawn must not wait
for it). Projectiles: unnamed (M10). Gib `colosseum_human_gib` 12828 and spotanims
2713-2720 (`name`, unstated). D15. Open M7, M10, M11, M40, M47.

### 4.5 `javelin_colossus`

12817, hp 220, size 3, range 15, 5-tick cadence (A size and hp; B cadence; SUPA range D,
wiki C). Auto and a sky-javelin toss "every fifth attack" or "after every four autos"
landing "6 ticks later" (`W:Fortis_Colosseum_Strategies:788`, `W:Javelin_Colossus:40`,
D); dodge by moving (`TECHNIQUES.md:82`). The javelin launch is excluded from the
no-damage glory bonus (`W:Glory:101`, D). **Animations:** auto 10892, toss 10893
(`rig+name`, **B-confirmed**; 2,013 and 450 events), death 10894 (`rig+name`), spawn
10891 `_walkfade` (`rig+sound`). Graphics 2673 spearhead, 2674/2675 artillery
slow/fast, 2676 artillery fire, 2677/2678 spearhead fire (`name`). Under Reentry the
toss leaves molten sand (struct_891, A). D9. Open M8, M9, M10, M11.

### 4.6 `jaguar_warrior`

12810, hp 125, size 2, melee range 1, 5-tick cadence (A hp/size; B cadence; SUPA range
D). A reinforcement only (waves 1-6, section 4.1). Safespot behind the north-west
pillar (`TECHNIQUES.md:56`, D). **Animations:** attack 10847 (tier `name`,
**B-confirmed**, 160 events in 15 runs), defend 10848 and death 10849 (`name`,
candidates); no spotanim names a jaguar. The brief's "its mager" is not a separate
monster in any source (D30). A CA: "Kill a Jaguar Warrior using a Claw-type weapon
special attack" (`N:Undead_Pirates_Tweaks_Varlamore_CAs_More:208`, A). Open M6, M11,
M18, M40.

### 4.7 `serpent_shaman`

`colosseum_standard_mager` 12811, hp 125, size 1, magic only (Water Surge), range 10,
5-tick cadence (`W:Serpent_shaman:40`, `:15`; A hp; B cadence). Max hit 28 or 27 (D10).
Present at tick 0 on waves 1-6 and as a reinforcement on 4-6, 10, 11
(`W:Fortis_Colosseum:95`, B). Animations updated in 2024 "so that their spells better
reflect their powers" and "Water Surge now looks even more impressive"
(`N:Undead_Pirates_Colosseum_Changes_more:208`, `N:Undead_Pirates_Tweaks_Varlamore_CAs_More:49`,
A, no ids). **Animations:** cast 10859 (`name`, **B-confirmed**, 1,064 events), death
10860 (`name`); the projectile and impact are unnamed (no spotanim names the serpent;
the impact sound `varl_serpent_shaman_water_impact_01` is menu-only). Technique: manual
off-tick of a reinforcement shaman (`TECHNIQUES.md:157`). D10. Open M6, M10, M11, M16.

### 4.8 `manticore`

12818, hp 250, size 3, range 15 (A, D). On sighting the player it charges for 10 ticks,
then fires three orbs one tick apart; the first two are range-then-magic or
magic-then-range and the last is melee; pray before launch, not in flight
(`W:Manticore:41`, `W:Fortis_Colosseum_Strategies:816`, D). From wave 9 a pair copies a
pattern within 15 tiles with line of sight, and a ready manticore is delayed 5 ticks
when another fires (`W:Manticore:45,47`, D). Mantimayhem I doubles the orbs, II adds
venom, III removes the forced melee last orb (`N:Undead_Pirates_Colosseum_Changes_more:95-97`,
A). **Observed:** bursts start every 10 ticks (731 of 822 gaps); first-orb orders
mage-range-melee 558, range-mage-melee 406 (`B:SAMPLE_SUMMARY.md:899-910`; orbs 2 and 3
are the plugin's assertion). **Animations:** throw 10869 (`rig+name`, **B-confirmed** as
the burst start), charge 10868 (`rig+name`, the plugin's style read window, never an
event), deaths 10866/10867 and spawns 10870/10871 (`rig+name`). Orb spotanims 2681 magic,
2683 ranged, 2685 melee, impacts 2682/2684/2686 (`name`; four plugins use them so, `CC:F`
1), explosion 2721. CA: wave 4 without avoidable manticore damage (A). D11-D14. Open M10,
M11, M12, M13, M14.

### 4.9 `shockwave_colossus`

12819, hp 125, size 3, range 15, 5-tick cadence (A, D, B). The clap. Second shockwave
under Dynamic Duo, spawned "near the main Colossus, but not necessarily on one of the
12 default spawns" (`W:Fortis_Colosseum_Modifiers:89`, D). **Animations:** clap 10903
(`rig+name`, **B-confirmed**), death 10895 (`rig+name`); graphic 2679 and
`vfx_colossi_shockwave_clap_proj` (`name`). Sounds: synth `shockwavecolossi` 14. What the
clap hits (style, area, delay) no pinned line states: M17. Open M10, M11, M17.

### 4.10 `minotaur`

12812 and `colosseum_minotaur_routefind` 12813 (identical stats, hp 225, size 3,
`param_26` 2 vs 4). Melee, 5-tick cadence (B); max 74 (wiki, `LW:282-294`, D). Out of
melee it heals another wounded monster "to full" when that monster is below 75 % and
its centre tile is within line of sight and 6 or 7 tiles (`W:Minotaur_Fortis_Colosseum:43-52`,
D, citing Mod Arcane on reddit; N3). Technique: "Minotaur heal lure", "Tick-eating the
minotaur" (`TECHNIQUES.md:63`, `:76`). CA: wave 7 with no minotaur heal (A).
**Animations:** melee 10843 (`rig+name`, **B-confirmed**, also on 12813), magic/heal
10844 (`rig+name`, frame sounds heal_charge/heal_cast), spawn 10845 (5 ticks), defend
10841, death 10846 (`rig+name`); the `_louder`/`mag_` variants 11584-11587, 11746-11748
are candidates nobody names. Graphic 2723 explosion (`name`). Red Flag's minotaur is
12813 per the code ledger (D31). Open M11, M15, M50.

### 4.11 `modifier_system`

The choice (section 3.5), the fourteen modifiers of section 1.4, their levels,
stacking (a held levelled modifier is offered again one level up: Blert's wave-2
options carry 34 = Blasphemy II in 16 of 18 runs after wave 1's fixed 4/5/12, observed
but the rule is M30) and their glory (`param_1903`, A; N2). Per modifier, what the
sources fix (A = a post in `SOURCES.md` 1.2, D = the wiki Modifiers page `LW:357-412`):

| Modifier | Fixed by a source | Open (M29 unless noted) |
|---|---|---|
| Bees! | swarm 12823 moves every 12 ticks, respawns after 50 (A); up to 10 unblockable poison per tick beneath the player (D); N swarms = level (FORT, D) | damage per level |
| Blasphemy | prayer drained by 20 % of damage taken (D); self-damage excluded (A) | II, III percentages |
| Doom | death at 15/10/5 stacks (A), reset per wave (A), Colosseum sources only (A), any damage taken (A); Doom scorpion 12822 | what adds a stack beyond "damage taken" (M48) |
| Dynamic Duo | second shockwave on 7, 8, 11 (FORT, D; 2 observed runs); not offered after wave 11 (D) | its spawn tile |
| Frailty | base hitpoints -10/-20/-40 %, overheal disabled (D) | - |
| Mantimayhem | I double orbs, II venom, III no forced melee (A); not offered for wave 12 (A) | - |
| Myopia | range -2 (I) and -6 (III) tiles; manual casts unaffected (D); autocasts affected (A) | II |
| Quartet | a fourth Fremennik (FORT, D; Blert one run per wave) | which member |
| Red Flag | glory 250 (A); effect text in struct_907 (A) | numbers; minotaur 12813 (M50) |
| Reentry | javelins leave molten sand where they land (struct_891, A); Blert pool events per wave 6:4 ... 11:26 (`B:SAMPLE_SUMMARY.md:1187`) | pool life, damage |
| Relentless | Defence ignored 33 %/66 %/all (A); bonus 1/3/6 "minimum hits" (A) vs "max hit increased" (D): N6 | - |
| Solarflare | an orb circling the pillars, every 2 ticks with a 7-tick corner stop (I), every tick with a 2-tick stop and prayer disable (III) (D); SOLSIM tiles and damages (`CC:D`, D) | II, damage |
| Totemic | totem 12825 at <= 50 % hp, heals 30 % (A), respawns after 2 min (A), 1 hitpoint (D), respects the player's attack delay (`N:Royal_Titans:42`, A) | heal interval (M42) |
| Volatility | on death explodes over size + 1 (D) | damage |

**Presentation:** bees 10821-10824 (`rig+name`), 2707/2708 jar; totem 10827/10828, 2687
projectile (Blert's heal marker), 2688 impact; doom scorpion 6252-6256 (bound pair,
`rig+name` rest), 2711/2712; beam crystal 10798-10802 and beams 2689-2697 (Sol); synth
`colosseummodifiers` 61 sounds. **Reward rows:** each modifier's glory. D4, D13, D26,
D31, D35, N2, N6. Open M29, M30, M42, M48, M50.

### 4.12 `sol_heredit_attacks`

12821, hp 1500, size 5, levels 350/200/400/350/300 (A; SOLSIM agrees except its combat
level, D16). Weak to slash; two AoE melee attacks with two patterns each, not
reduced by Protect from Melee, each with safe tiles (`W:Fortis_Colosseum:186`, D):
spear ("trident") and shield slam; below 90 % the triple parry, below 75 % the grapple
(`W:Sol_Heredit:94-102`, D; `W:Fortis_Colosseum:188` says both specials "after falling
below 90%"). Posts: spear and shield range extended (`N:Varlamore_Part_One:31`, A); no
grapple damage after 0 hp (`N:Varlamore_Tweaks_GameJam_V_Commences_More:37`, A).
**Observed (B):** first attack is the thrust in 12/12 streams, 5 ticks after his
spawn in 10 (D24); thrust gaps 5-7 and 12-14, slam 5-7 (`B:SAMPLE_SUMMARY.md:831-851`);
dust patterns (trident 1/2, shield 1/2 by direction, `:1175-1180`); grapple outcomes
with equipment slots 1, 5, 7, 8, 9 (`:1185`). **Animations:** thrust 10883
`_melee_attack_telegraph`, slam 10885 `_shieldslam_telegraph`, grapple 10884
`_grapple_attack_telegraph`, short triple 10887 (all `rig+name`, **B-confirmed**); long
triple 10886 and plain melee 10882 (`rig+name`, never seen by Blert: M41). Graphics
2667/2668 triple telegraphs, 2669-2672 melee dust (Blert's SOL_DUST markers), 2666 land.
`varb9800` (grapple pending) redraws the called slot (A). Technique rows: hover and L
methods, delay by walking away, triple parry, grapple parry (`TECHNIQUES.md:106-138`).
CAs: no damage from spear/shield/grapple/triple; Fortis Salute N/E/S/W below 10 %; no
running; ten kills (A). D16-D21, D24, D25, D34. Open M19, M20, M21, M25, M26, M41.

### 4.13 `sol_heredit_phases`

Phases at 90/75/50/25/10 % (`W:Sol_Heredit:92`, D; SOLSIM 1350/1125/750/375/150 hp,
D; two sources but SOLSIM is a simulator, so C at best after a guide agrees, D23).
Each phase starts with six light beams in a 9x9 area around the player, molten sand
after 2 ticks (`W:Sol_Heredit:92`, D); a crystal (`colosseum_beam_crystal` 12824)
charges a beam (`W:Fortis_Colosseum:186`, D). At 10 % (150 hp) the enrage: one sand
tile every 3 ticks around the player (`W:Sol_Heredit:106`, D;
`W:Fortis_Colosseum:190`). Gladiators close the arena to 16x16 with corner tiles
(`W:Fortis_Colosseum:184`) or about 16x15 within the pillars
(`W:Fortis_Colosseum_Strategies:1414`): N7. **Observed (B):** first dust tick 14-15,
first pool tick 26-57, first laser tick 35-69; laser scan -> shot 4 ticks (59) or 3
(11); 48 laser prisms over 12 streams (`B:SAMPLE_SUMMARY.md:1175-1187`, `:874-897`).
Sol is first seen on wave tick 6 in 12/12 (tick 0 asserted -1, `B:PROVENANCE.md`).
**Animations:** jump 10876 and land 10877 (`rig+name`) from the seated 12827, death
10888 + 2680, explosion 2724; crystal 10798-10802, beams 2689-2697 (scan 2689-2691, shot
2693-2695 as Blert reads them), pool 2698 (in no rig table yet, `SOURCES.md` section 12);
`colosseum_holy_fire` 50746 "Molten Sand" (spawned). Jingle 305 on the kill. D22, D23,
N7. Open M22, M23, M24, M27, M28, M38.

### 4.14 `colosseum_entry_and_minimus`

Sections 3.1, 3.2, 3.4 (the intermission). Rows: requirement, the first talk, the path
by click (outside 50749 -> lobby -> 50751 -> arena), Minimus 12808 Start-wave and Leave,
his walk-in between waves, the lobby bank chest gated at 2,000 glory (`W:Fortis_Colosseum:28`,
D), Gloria, the scoreboard 867 (View-stats, View-glory, A ops). Sources: cache (A), wiki
(D); Blert none. **Presentation:** Minimus has no attack; his walk-in sequence is
unsourced. D32. Open M35, M36, M37.

### 4.15 `reward_pool_and_cash_out`

Per completed wave one roll; from wave 3's end the unique table can roll
(`W:Rewards_Chest_Fortis_Colosseum:21`, D), at 1/124, 1/110, 1/96, 1/82, 1/68, 1/54,
1/40, 1/26, 1/12 for waves 3-11 by the post's numbering
(`N:Undead_Pirates_Colosseum_Changes_more:134-169`, A; D2, D3); a unique replaces
that wave's normal roll; glory never affects the odds (`N:Varlamore_Tweaks_Drop_Rates:201`,
A). The unique is a sunfire armour piece 9/16 (favouring a missing piece), an echo
crystal 6/16 (1/10 for 2 or 3), or the tonalztics 1/16 from wave 7 (A,
`:177-179`; the chest page gives waves 4-6 as 6/10 armour, 4/10 echo,
`W:Rewards_Chest_Fortis_Colosseum:23-30`). Wave 1 always gives 80 sunfire splinters
(`:114`, D). Normal tables per wave: `W:Rewards_Chest_Fortis_Colosseum:119-340` only
(D, M32). Cash-out between waves through Minimus; forfeit on death, logout, teleport
(3.6). Interfaces 866, 626/865 preview, chest 50741 + 864 (A). D2, D3. Open M32, M33.

### 4.16 `glory`

Display formula, cache clientscript 4956 (`sources/cache_interfaces.txt:3684-3725`,
A for what the client shows): `100 x wave` (+1000 on wave 12, N1) + no-damage bonus
`100 x wave` when `varp4134 <= 0` + speed bonus `(500 - varp4136) x wave` when
`0 < varp4136 < 500` + each held modifier's `param_1903` (x rank in 4943/4930, N2). The
wiki's table (`W:Glory:21-156`) gives the same completion and no-damage values per
wave, the time bonus as "start 500 x wave, minus wave per tick", odd results rounded
down to even (`W:Glory:161`), a 72,000 theoretical maximum (`:164`). No-damage excludes
environmental and self damage and the javelin launch (`:101`). The "Lots!" display at
max int (4956 line 9-10, A). Tiers (section 3.7, A). D -; N1, N2. Open M31.

### 4.17 `reward_items`

What the rolls hand out; the items' own behaviour is mostly built already (section
1.7) and out of this plan's scope except where the Colosseum touches it: echo crystal
charges 6,000 into echo boots, cap 60,000 (`N:Undead_Pirates_Colosseum_Changes_more:187-188`,
A); sunfire splinters -> sunfire runes (A); tonalztics numbers (D28). Collection log
entries (section 1.6, A). Clan broadcast on a valuable claim (A). D28. Open M46.

### 4.18 `quiver_and_pet`

Section 3.8. Quiver on completion (A); pet 1/200 (A, N5); the Minimus trade (A); the
pet follower 12857 and menagerie 12767 (A), varbits 9814/9816. Blessed quiver: wiki
page pinned (`W:Blessed_Dizana_s_quiver`), not part of the run. N5. Open M33.

### 4.19 `death_and_fee`

Sections 3.9-3.10. Respawn in the lobby with a grave; fee -75 % until 100 waves (A),
up to 125,000 (D); loot forfeited; Doom kills at its stack cap. D27. Open M34, M44.

### 4.20 `colosseum_music`

Song 782 unlocked on the first wave start; jingle 305 on Sol's death (section 3.11).
Open M45.

### 4.21 `colosseum_combat_achievements`

Thirteen tasks, `param_1312=25` structs 929-945 (A), the post's task list
(`N:Undead_Pirates_Tweaks_Varlamore_CAs_More:198-258`, A; eleven are quoted in
`SOURCES.md` 1.2, the other two are in the structs): wave 4 without avoidable
manticore damage; wave 7 with no minotaur heal; a jaguar killed by a claw special;
wave 11 with Red Flag, Dynamic Duo or Doom II; completion under 30:00/28:00 (N4) and
24:00; Sol ten times; Sol without running; Sol with no spear/shield/grapple/triple
damage; Sol with Bees! II, Quartet and Solarflare II; the Fortis Salute below 10 %.
Each is a row whose test completes it by play. N4. Open M46.

### 4.22 `wave_presentation`

Every attack, spawn, death and transition per monster names its sequence, graphic,
projectile and sound from a cited binding: the rows of 4.4-4.13 plus `B:ID_TABLE.md`,
`sources/code_id_map.tsv` (85 rows), `RIG_ANIMATIONS.md` and `sources/rig/*.tsv`.
B-confirmed attack markers: 10847, 10859, 10843, 10850, 10853, 10856, 10892, 10893,
10903, 10869, 10883, 10884, 10885, 10887. Every other row is a candidate. Sounds: a
seq's frame sounds play with it (A); the 332 menu-only sounds are `unknown_purpose`
and stay silent until a source says when (owner standard: never invent). Gibs
12828-12832 have no rig. Open M10, M16, M39, M40, M41, M47.

---

## 5. Open measurements

One row per number or rule no source settles at grade C or better. "Blert query" means
a pass over `sources/blert_api/*.json` (or `observed_npc_events.tsv`) with
`tools/waves_gate/verify_blert.py`, OBSERVED events only; a larger sample is fetched
under the same 3-second throttle. "Frame count" means `tools/waves_gate/frame_count.py`
on a pinned video (Sun fish `M7sIf6mx4vw` covers every unit, Sol from 54:00,
`SOURCES.md` section 8). Until a row closes, its spec value carries `[Mn]`, grade E,
tolerance `approx`.

| M | Unit | Question | Measurement that closes it |
|---|---|---|---|
| M1 | wave_reinforcements | which gate a reinforcement uses ("closest to the player") and what tick 66 counts from | Blert query: per reinforcement spawn, its tile against the player's tile on tick 65-66 (`PLAYER_UPDATE` is observed, `B:PROVENANCE.md:45-69`) |
| M2 | wave_reinforcements | does wave 1 spawn a jaguar on tick 66 | Blert query for a wave-1 stream longer than 66 ticks in a larger sample; else a frame count of a slow wave 1 |
| M3 | arena_and_spawn_tiles | the twelve default spawn tiles and the draw (per kind, with or without repeats) | Blert query: tick-0 tiles per kind over every stream, clustered; compare with the wiki tile-marker module |
| M4 | arena_and_spawn_tiles | reinforcement spawn tiles, north and south | Blert query: spawn tiles of every post-tick-0 spawn |
| M5 | wave_reinforcements | a reinforcement spawning on the clearing tick is killed | Blert query: streams whose last death is on tick 66 (despawn is not hp zero: read with care) |
| M6 | every monster | first attack after wave start (SUPA: not before tick 3) | Blert query: earliest NPC_ATTACK per tick-0 npc |
| M7 | fremennik_trio | the 6-tick cycle's phase against wave start | Blert query: archer/seer attack ticks mod 6 |
| M8 | javelin_colossus | toss after 4 autos or every 5th attack | Blert query: per javelin, autos (10892) between tosses (10893) over 2,241 autos |
| M9 | javelin_colossus | sky javelin landing delay and damage | frame count (toss frame to impact); damage has no source: E |
| M10 | every ranged/magic monster | projectile flight, attack tick to hit tick | frame count per monster; Blert records no hit |
| M11 | every monster | max hits (D10, D11; Fremennik, jaguar, javelin, shockwave, minotaur 74) | none recorded; wiki only (D) until a video shows a max hitsplat |
| M12 | manticore | orb 2 and 3 spacing, and whether the charge 10868 releases | frame count of a burst |
| M13 | manticore | first-orb style odds (558 vs 406 observed) | Blert query restricted to single-manticore waves 4-8 |
| M14 | manticore | pair copy and the 5-tick delay | Blert query: burst-start gaps between the two manticores of waves 9-11 |
| M15 | minotaur | heal radius 6 or 7, threshold, amount, the heal animation 10844 | Blert query: HEAL hitsplats on tracked npcs (`NPC_UPDATE` boost, observed, `B:PROVENANCE.md:45-69`) against the minotaur's position, the healed npc's hp and centre-to-centre distance; 10844 is not in the plugin table, so its frame needs a frame count |
| M16 | serpent_shaman | Water Surge projectile and impact graphic | a plugin id, else a picture of each candidate spotanim |
| M17 | shockwave_colossus | what the clap hits (style, area, delay) | wiki page re-read for a stated line; frame count |
| M18 | jaguar_warrior | its max hit and whether it is ever at tick 0 | wiki (D) and Blert tick-0 sets (none seen) |
| M19 | sol_heredit_attacks | hit tick after each telegraph (spear, shield, triple, grapple) | frame count, Sun fish from 54:00 |
| M20 | sol_heredit_attacks | the attack pool and its phase gates | Blert query: Sol attack sequence per stream against the dust/pool phase marks |
| M21 | sol_heredit_attacks | pattern alternation and safe tiles | Blert query: dust (pattern, direction) sequence per stream; tile shapes from a frame grab |
| M22 | sol_heredit_phases | thresholds as hitpoints and the transition (freeze, beams) | Blert query: Sol's hitpoints per tick (cache 1500 minus the observed `NPC_UPDATE` damage) at each pool and laser first tick; frame count for the freeze |
| M23 | sol_heredit_phases | laser cadence and the 3-or-4-tick scan to shot | Blert query: laser scan/shot ticks per phase (already 4:59, 3:11) |
| M24 | sol_heredit_phases | pool damage and placement; final-phase sand every 3 ticks | frame count; damage has no non-simulator source |
| M25 | sol_heredit_attacks | grapple window (3/4/5) and damage | Blert announcement-to-parry ticks (parry seen at +4) and a frame count |
| M26 | sol_heredit_attacks | triple parry windows, spacing, damages | frame count |
| M27 | sol_heredit_phases | the enrage at 150 hp (beams "every 7 seconds") | frame count |
| M28 | sol_heredit_phases | the barricade footprint (N7) and the gladiators 12834-12836 | frame grab of wave 12 from above; wiki tile markers |
| M29 | modifier_system | every modifier number not in a post (table 4.11) and N6 | the wiki Modifiers page per row (D), Blert DOOM/REENTRY events, frame counts |
| M30 | modifier_system | the offer rule: three options, wave 1 fixed, upgrades of held ones | Blert query: per run, offered triples against the choices already made |
| M31 | glory | the server's formula: varp4136 units (ticks?), the even rounding, +1000 on wave 12 (N1), rank weighting (N2) | a recorded wave-complete interface (866) beside the wave's known duration and modifiers; Blert `STAGE_UPDATE` "Wave N completed! Wave duration" chat is observed but not served by the API |
| M32 | reward_pool_and_cash_out | the normal per-wave tables (wiki only) and how many items a wave adds | the wiki is D; a second source (a drop-log post or the `markd315/colo-invo` list, `SOURCES.md` section 9) to reach C |
| M33 | reward_pool_and_cash_out, quiver_and_pet | which wave a roll belongs to (D2, N5) | quote both tables' headers line by line in the spec table; no measurement |
| M34 | death_and_fee | the 125,000 cap and what the grave holds | a Death's Office source beside `W:Fortis_Colosseum:177` |
| M35 | colosseum_entry_and_minimus | Minimus's words, the first-talk gate, whether any entry fee exists | fetch `Transcript:Minimus` (and Gloria) through the pinned wiki tool; no fee is built without a line |
| M36 | colosseum_entry_and_minimus | when and where Minimus appears after a clear | Blert query: Minimus 12808 spawn tick and tile after each wave's last death (the plugin counts his spawns) |
| M37 | wave_table | ticks from the choice (varbit 9788) to "Wave: N" and the first spawn | Blert query: HANDICAP_CHOICE tick (read 3 ticks after Minimus despawns) to the next wave's tick 0 |
| M38 | sol_heredit_phases | wave 12 start: tick 0 (asserted -1), Sol first seen tick 6, the seated 12827 -> 12821 swap | Blert query of the 12 wave-12 streams for 12827 despawn and 12821 spawn; frame count of the jump (10876/10877) |
| M39 | wave_presentation | skeletal sequence length (41 of 166 are skeletal; one keyframe per 20 ms assumed) | frame count of one, e.g. Sol's 10883 against its 6.0 ticks |
| M40 | fremennik_trio, jaguar_warrior, serpent_shaman | human-rig defend and death candidates (`name` tier) | a driver picture of each on the npc's own model |
| M41 | sol_heredit_attacks | are 10882 and 10886 ever played, and which triple is which (D25) | Blert raw stream read for any 10882/10886 animation change on 12821; frame count |
| M42 | modifier_system | totem heal interval and whether a heal lands (0 events in 208 waves is not 0 heals) | Blert: HEAL hitsplats on totem targets; frame count |
| M43 | wave_table | what is restored between waves (hp, prayer, stats); spec energy is not (A) | Blert `PLAYER_UPDATE` across an intermission; a frame grab of the orbs |
| M44 | death_and_fee | logout or teleport mid-wave: where the player returns, any combat logout delay | wiki re-read; a video; until then the build ends the run and returns to the lobby, tagged `[M44]` |
| M45 | colosseum_music | which tiles play song 782 and when the unlock fires | wiki music page (D) |
| M46 | colosseum_combat_achievements, reward_items | which vars count kills, waves, best times and the collection-log bit | the cache's readers of each var (none today); the CA structs' own params |
| M47 | wave_presentation | which death and gib each monster shows in the arena, and its despawn tick | Blert despawn tick after the last damage hitsplat; a picture |
| M48 | modifier_system | what adds a Doom stack; the scorpion's role | Blert `COLOSSEUM_DOOM_APPLIED` per wave (2-37 per wave in the sample) against hits |
| M49 | wave_table | wave 12 Fremennik under Quartet (D34) | Blert query: wave-12 spawns in Quartet runs (one Fremennik seer on tick 9 in one run) |
| M50 | minotaur, modifier_system | 12813 `routefind`: Red Flag's minotaur or something else (D31) | Blert query: 12813 spawns against the run's handicaps (5 events, one run, wave 11) |
| M51 | wave_table | what the player sees when each wave monster first appears: the Fremennik trio, Jaguar warrior, Serpent shaman and Shockwave colossus have no spawn sequence on their rigs; the javelin colossus (10891), manticore (10870/10871) and minotaur (10845) are rig candidates only | frame count or a driver picture of one arrival per kind (added by the wave_table spec pass) |
| M52 | arena_and_spawn_tiles | the twelfth default spawn tile, the draw weights (x=1811 tiles under-drawn), the minimum distance kept from the player (observed 4), the paired Colossus tile under Dynamic Duo | Blert query over a larger sample: tick-0 tiles per kind against the player tile, with and without Dynamic Duo |
| M53 | arena_and_spawn_tiles | the walkable-tile mask of the arena floor | read the engine collision flags of the instanced m28_48 in seam S3 and compare with the sampled player tiles |
| M54 | arena_and_spawn_tiles | where the game places the player on entry and at each wave start (modal tile 1815,3110) | a solo test reading the tile after entry and after Start-wave; Blert wave-start tile query |
| M55 | arena_and_spawn_tiles | the purpose of npc 12820 colosseum_safespot_dying (Pillar) | a named source; nothing is built on it until then |
| M56 | fremennik_trio | the berserker's attack-to-damage tick, and the tick a protection prayer is read on for all three members | none recorded (Blert sees no hit); a frame count or a solo test against a stationary player; added by the fremennik_trio spec pass |
| M57 | fremennik_trio | trio hit chance against a player (wiki: 'very accurate', no number) | none recorded; many solo hits at a fixed defence; added by the fremennik_trio spec pass |
| M58 | fremennik_trio | the rule that picks each member's standing side (D45) | Blert query over a larger sample restricted to a stationary player; added by the fremennik_trio spec pass |

**The five that matter most** (they gate the first seams or a whole unit): **M3**
(spawn tiles: wave 1 cannot be built without them), **M30** (the offer rule: every
wave starts with it), **M19** (Sol's hit ticks: every Sol row is a tell-to-hit pair),
**M31** (glory: the client already draws it, so a wrong server number is visible on
every wave), **M10** (projectile flight for every ranged and magic monster: every
prayer row depends on it).

---

## 6. Build order

New content lives in `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_colosseum/`
(`configs/`, `scripts/`), the Inferno's layout. One seam per file, in dependency order.
**Before a seam starts, its spec table(s) exist and pass the validator**; the seam
builds only rows graded A-D or tagged `[Mn]`, and opens a `CONTENT_BUGS.md` row for
anything it cannot source. Existing driver verbs (`docs/minigames/waves_loop/DRIVER_NOTES.md`):
`t.wave.state/enter/await_wave/await_clear` (answer `unsupported` for colosseum today),
`t.npc.await_anim/state/pack`, `t.world.los/spotanims/projectiles/hazard_at`,
`t.prayer.*`, `t.player.step_tick/drink`, `t.ticklog.*`.

**New driver verbs** (the driver seam, S0, before any content test):

| Verb | What it does, by click |
|---|---|
| V1 `t.wave.enter("colosseum", n)`, `t.wave.state("colosseum")` | a content debugproc `::colosseum <wave>` as the bring-along (the Inferno's shape, but a full run, not a one-wave practice); state reads active, wave, alive, intermission, held modifiers and ranks, pool, glory |
| V2 `t.colo.minimus(op)` | Minimus 12808 op1 Start-wave / op3 Leave, and Talk-to 12807 |
| V3 `t.colo.modifier.read()` / `.pick(card)` | reads the three cards on 626/865 (`varc1194`, `varb9788`), clicks one and confirms |
| V4 `t.colo.reward.read()` / `t.colo.chest.claim()` | reads invs `colosseum_rewards_future`/`_previous`/`colosseum_rewards`; clicks chest 50741 Search and takes through 864 |
| V5 `t.colo.grapple.parry()` | clicks the equipment slot Sol called (chat line, `varb9800`) on the worn-equipment tab |
| V6 `t.world.hazard_at` (extended) | molten sand 50746, pools 50743-50745, beams, the Solarflare orb, bee swarms |
| V7 `t.colo.glory()` | reads varp4130/4132/4134/4136 and the 866 panel text |

| # | Seam (file) | Builds | Spec table(s) first | Verbs needed |
|---|---|---|---|---|
| S0 | driver seam | V1-V7 | - | - |
| S1 | `configs/colosseum.constant`, `colosseum.varp`, `colosseum.npc` | ids, tiles, ticks, `[Mn]` tags; server-only vars; npc combat blocks from cache stats and B-confirmed animations (fixes RIG-1..RIG-4) | `arena_and_spawn_tiles`, the stat rows of every monster table | - |
| S2 | `scripts/colosseum_entry.rs2` | outside entrance -> lobby -> arena by click; Minimus's first talk; lobby bank gate; Gloria and scoreboard ops. Extends `twilightspromise.rs2:351`'s fallthrough by one call (the quest branches stay the quest loop's) | `colosseum_entry_and_minimus` | V2, a loc op by click |
| S3 | `scripts/colosseum_instance.rs2` | instance of m28_48, pillars from the map, start tile, free on every exit | `arena_and_spawn_tiles` | V1, `t.world.los` |
| S4 | `scripts/colosseum_waves.rs2` | tick-0 sets, tick-66 reinforcements and gates, clear and same-tick kill, "Wave: N", interface 866, the intermission and Minimus (choice slot inert until S12, its rows out of scope), vars 4133/4134/4136/4137 | `wave_table`, `wave_reinforcements` | V1, V2, V7 |
| S5 | `scripts/colosseum_fremennik.rs2` | the trio, 6-tick cycle, weakness max hit | `fremennik_trio` | - |
| S6 | `scripts/colosseum_shaman.rs2` | the serpent shaman | `serpent_shaman` | - |
| S7 | `scripts/colosseum_jaguar.rs2` | the jaguar warrior | `jaguar_warrior` | - |
| S8 | `scripts/colosseum_javelin.rs2` | auto and toss, landing | `javelin_colossus` | V6 |
| S9 | `scripts/colosseum_shockwave.rs2` | the clap | `shockwave_colossus` | - |
| S10 | `scripts/colosseum_minotaur.rs2` | melee and the heal | `minotaur` | - |
| S11 | `scripts/colosseum_manticore.rs2` | charge, three orbs, pair rules | `manticore` | - |
| S12 | `scripts/colosseum_modifiers.rs2` | the offer, the choice, ranks, preview invs, glory values | `modifier_system` (choice rows) | V3 |
| S13 | `scripts/colosseum_modifier_effects.rs2` | the fourteen effects and their creatures | `modifier_system` (effect rows) | V6 |
| S14 | `scripts/colosseum_sol.rs2` | jump-in, attacks, telegraphs, grapple, triple | `sol_heredit_attacks` | V5, V6 |
| S15 | `scripts/colosseum_sol_phases.rs2` | phases, beams, crystal, pools, barricade, enrage, death, jingle | `sol_heredit_phases` | V6 |
| S16 | `scripts/colosseum_rewards.rs2` | the pool per wave, rolls, cash-out, chest, quiver, pet, the Minimus trade, broadcast | `reward_pool_and_cash_out`, `quiver_and_pet`, `reward_items` | V4 |
| S17 | `scripts/colosseum_glory.rs2` | the formula, personal best, tiers and their unlocks | `glory` | V7 |
| S18 | `scripts/colosseum_death.rs2` | death to the lobby, grave, fee, logout and teleport forfeits | `death_and_fee` | `t.session` relog |
| S19 | `scripts/colosseum_records.rs2` | music unlock, CAs, collection log, kill times, counters | `colosseum_music`, `colosseum_combat_achievements` | - |

`wave_presentation` rows are written with each monster's table and built in that
monster's seam. Monsters run simplest first (one fixed cycle, then one style, then
melee, then two attacks, then the heal, then the manticore's charge and pair rule);
the modifiers come after every monster they touch; Sol last; then the systems that
need a finished run. The full-run test follows S19.

---

## 7. What the build must not repeat from the Inferno port

Each row is an Inferno fault (`docs/minigames/waves_loop/CONTENT_BUGS.md`), the rule the
Colosseum seams follow, and the test row that proves it.

| Inferno fault | Rule for the Colosseum | Proof row |
|---|---|---|
| ENG-5: a wave re-spawned every 8 ticks (the softtimer was never cleared) | a wave's set spawns once; the timer is cleared on the tick it fires; reinforcements arm once per wave | `wave_table.spawn_once`: per wave, spawn count = table + reinforcements, read from the tick log |
| ENG-13, INF-AV-002: pillars blocked no line of sight (an invisible npc stood in for the locs) | the pillars are the cache map's 52490 locs, nothing stands in for them | `arena_and_spawn_tiles.los_<pillar>`: `t.world.los` false through each pillar both ways, true around it |
| INF-AV-001, ENG-6, ENG-7: the entrance could not be clicked; the content walked to a copy the client did not draw | every op is handled on the loc the cache map places (50749 at 1796,3106; 50751 at 1810,9506), and the handler is the one already in the tree, extended | the entry test clicks from outside the building to the arena; no `::goto` |
| ENG-8: the pause was reachable only from the exit or a cheat; logout cleared the run | the Colosseum has no pause in any source: build none; logout follows 3.10 | a test presses the logout button mid-wave and reads the outcome against the `death_and_fee` rows |
| INF-AV-005, RIG-1..RIG-10: assets borrowed with no source (Fire Blast's projectile, generated ledgers) | every sequence, graphic and sound comes from a section 4 row with its tier; `npc_combat/c/colosseum_*.combat` is not read | each presentation row names its binding and its grade |
| INF-AV-006: a monster appeared with no spawn animation | each monster's spawn row is written: a sequence with a source, or "none" with the source that shows none | presentation rows per monster |
| INF-AV-003, inventory note 9: a death waited on an animation the rig does not have, or on a 20,000-cycle last frame | despawn on the sourced tick (M47), never on the end of a human-rig death sequence | `<unit>.despawn` row |
| INF-AV-008, INF-AV-009: no kill count, no combat achievement, no collection log | S19 is part of done; each CA has a test that earns it by play | `colosseum_combat_achievements.<task>` rows |
| ENG-9: no combat logout delay | no row until M44 settles it; the build does not borrow the engine default silently | M44 |
| ENG-12: one monster's hit flight changed between attacks | one rule per monster for attack tick to hit tick (M10) | `<unit>.hit_delay` row, a distribution, not one sample |
| ENG-19: the client's npc pool kept the arena's npcs after leaving | every exit (cash-out, death, logout, teleport) despawns the instance's npcs and frees the handle | `t.npc.pack` empty after each exit |
| The Inferno's `wave.enter` started a one-wave practice run that left after the wave | `::colosseum <wave>` places the player at the wave and the run continues to the end | the full-run test enters wave 1 by the debugproc only, as orchestrator section 6 allows |

## Orchestrator rulings on source disagreements (2026-10-03)

The owner was asked to rule on the disagreements below and delegated them ("Use your best
judgement"). These are the waves orchestrator's rulings. Each says what would overturn it.
A spec table or a build seam follows the ruling and cites this section; it does not
re-open the question unless it holds the evidence named.

**The rule behind all of them.** The target is the game as our cache has it (rev 239).
1. What the cache itself states or computes (a struct's text, an enum, a client script's
   arithmetic) is the game at that revision and outranks a wiki page, which describes
   today's game and may post-date a change. 2. A Jagex newspost outranks the wiki; where two
   posts differ, the later one wins; a post older than the cache that the cache contradicts
   is superseded by the cache. 3. Where two cache scripts disagree with each other, the one
   that computes the value the SERVER-facing interface shows (the summary or reward
   screen) outranks one that draws a tooltip or preview. 4. A ruling made on ranking alone
   is graded no higher than its best source and keeps an open row naming the observation
   that would confirm it.

| Id | Question | Ruling | Why | Overturned by |
|---|---|---|---|---|
| R1 (N1) | Glory for completing wave 12: +1,000 bonus (cache client script) or 1,200 with no bonus (wiki) | Follow the cache client script | Rule 1: it is the game's own arithmetic for what the player is shown | A frame of a real run's end screen whose total fits only the wiki's figure |
| R2 (N2) | Is a modifier's glory multiplied by its level (two client scripts disagree) | Follow the script that computes the run's total on the summary or reward screen; the glory unit's table names which script that is | Rule 3 | The same frame as R1, taken from a run with a modifier above level 1 |
| R3 (D2, N5) | Which wave the unique roll and the pet roll belong to: the wave just completed (Jagex posts) or the reward wave (wiki) | Write every roll as "rolled when wave N is completed", N taken from the latest Jagex post; if the wiki's numbering is the same event counted one wave later, say so in the row and close the disagreement | Rule 2, and the two may be one event under two numberings | A later Jagex post, or the cache's reward interface text stating the wave |
| R4 (N6) | Relentless: "minimum hits" (post) or max hits (wiki) | Use the modifier's own description text from the cache struct (enum 5312's entry for Relentless, each level's text); if that text is silent, the post | Rule 1: the struct text is what the game tells the player | Nothing short of a measured hit distribution from a recording |

Not built because no source states them, and to stay that way until one does: an entry
fee, and a pause. Logging out forfeits the run's loot (the plan's reading of the sources).
