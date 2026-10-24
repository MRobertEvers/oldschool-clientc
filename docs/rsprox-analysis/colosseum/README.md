# The Fortis Colosseum as RSProx recorded it

The Colosseum built from Jagex's own packets. Every rule below cites the sessions, the
tick, the tool and the count behind it. It supersedes the wiki/Blert-graded rows of
`docs/minigames/colosseum/COLOSSEUM_PLAN.md` wherever both speak (RSProx is
authoritative; the plan's `Mn` open rows are closed here where a recording shows them).

## Corpus

60 sessions hold Colosseum npcs (`rsprox_index.py search npc 12808 12810 ... --any`),
transcribed with `--bas` (`build/colosseum_private/paths_bas.txt`). Activity: Leagues VI
57, Grid Master 1 (index 3268), normal world 2 (index 1211, waves 1-6 only; index 1226,
lobby). Revisions 234-238. **Seasonal recorders attack faster and take less damage**:
every absolute damage number here is checked against index 1211 before it is used.

| completed | w1 | w2 | w3 | w4 | w5 | w6 | w7 | w8 | w9 | w10 | w11 | w12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| waves | 91 | 64 | 53 | 47 | 38 | 31 | 29 | 26 | 21 | ... | ... | ... |

Tools (all read the cached parse of `tools/rsprox/rsprox_colosseum.py`):
- `tools/rsprox/rsprox_colosseum.py waves <tx|@list>`: runs and waves.
- `tools/rsprox/colosseum/colo_waves.py table|spawns|intermission`: wave sets, spawn tiles, the intermission.
- `tools/rsprox/colosseum/colo_lives.py life|blocks <type>`: per-life npc info.
- `tools/rsprox/colosseum/colo_attacks.py <type> <seqs>`: cadence, first attack, projectile fields, hit delay.
- Ad-hoc fits: `build/colosseum_private/q_*.py` (spawn points, trio, reinforcements).

## 1. Waves

### 1.1 Tick-0 sets (all 12 waves, every completed wave)

Identical in every run apart from the modifier adds (Quartet, Dynamic Duo, Solarflare,
Bees!, Totemic, Volatility gibs). `colo_waves.py table`:

| wave | tick 0 | +66 |
|---|---|---|
| 1 | archer, seer, berserker, shaman (91/91) | jaguar (13 waves lasted 66) |
| 2 | + javelin | jaguar |
| 3 | + 2 javelin | jaguar |
| 4 | trio, shaman, manticore | jaguar, shaman |
| 5 | trio, shaman, javelin, manticore | jaguar, shaman |
| 6 | trio, shaman, 2 javelin, manticore | jaguar, shaman |
| 7 | trio, javelin, manticore, shockwave | minotaur |
| 8 | trio, 2 javelin, manticore, shockwave | minotaur |
| 9 | trio, javelin, 2 manticore | minotaur |
| 10 | trio, 2 javelin, 2 manticore | minotaur, shaman |
| 11 | trio, javelin, 2 manticore, shockwave | minotaur, shaman |
| 12 | Sol Heredit | - |

Reinforcements are added on wave tick **66** exactly, every one recorded.

### 1.2 Spawn tiles

**Twelve spawn points** (south-west tiles) serve the shaman, javelin, manticore and
shockwave at tick 0:
`1811,3104 1811,3109 1817,3106 1821,3103 1821,3109 1824,3099 1825,3114 1827,3103 1827,3109 1832,3107 1836,3104 1836,3109`.

**The draw** (`q_spawnfit.py`, 451 waves): each monster takes a point uniformly at random,
no point twice (0 shared in 451), from the points whose SW tile is at least **5** tiles
(Chebyshev) from the player's tile at the start of the wave tick. 0 violations; the
per-distance use rate fits uniform-among-eligible (chi2 11.1 on 22 bins; the next best
rule, 6 from the footprint centre by Euclid, 16.2 with 91 violations).

**The Fremennik trio** is a fixed formation (321/321): berserker at B, archer B+(-1,-1),
seer B+(+1,-1). B is uniform over x 1821..1827, z 3106..3112 (49 tiles, 451 waves; no
player exclusion: 4 spawned on the player's tile). **Quartet** adds a fourth member at
B+(0,-2): seer 54, archer 43, berserker 33 (uniform).

**Reinforcements** (`q_reinf.py`, 69 waves): all of a wave's reinforcements come from one
gate: **north** (z 3120) when the player's z (start of tick 66) is >= 3106, else **south**
(z 3092): 69/69, all five ties at z 3106 went north. x is 1823/1824/1825, distinct
for the two. Twice (player standing in the north gate at 1824,3121/3122) the shaman came
in at the centre 1824,3104 instead.

**Solarflare** adds four `colosseum_solar_flare` at the pillar corners 1819,3101 1819,3112
1830,3101 1830,3112 (103 each).

### 1.3 Every other spawn, and the spawn angles (`q_spawn2.py`, `q_angles.py`, `q_minimus*.py`, `q_totem_ang*.py`, `q_bee_step.py`)

- **Spawn angle per type**: every monster, Sol, the crystals, solar flares and swarms are
  added facing south (all of them, both sides). Gibs face the way the npc died, all eight
  directions (ours too since `npc_facedir` is aimed one tile from the gib's SW tile).
- **Minimus at D+3**: one tile north of the player, facing south (424/424 with that tile
  walkable); a blocked north puts him east facing west (13/13), north, east and south blocked
  west facing east (1/1; south before west unverified). The cash-out Minimus at 1830,3103
  faces west (67/67).
- **Bee jar**: thrown from the tile 5 north of its landing (62/62); its three sounds play at
  the arena centre 1824,3104, not the jar (30/30, 30/30, 25/25). The swarm steps on add+1,
  then every 2 (54/58), none on its add tick, so it is added facing south (62/62).
- **Totem**: the tile SW of its monster's SW tile, facing NE (100/107); a blocked diagonal
  puts it NE of the monster's NE tile facing SW (index 1229 [1487] [2313] [3550]). Four (two
  east, two south, on the normal tile) are unexplained.
- **Crystals** land on the 12 recorded tiles (both sides); the Dynamic Duo partner is on
  ring 2 around its post (26 recorded, all ring 2).

## 2. The Fremennik trio (archer 12814, seer 12815, berserker 12816)

`colo_attacks.py`, 2,768 attacks:
- **Add**: `spawnangle=south`, seq `npc_human_staffready_spawn` 10861 on the add tick
  (499/499 archer, 505/505 seer, 494/494 berserker).
- **Movement**: runs (2 tiles a tick) to its own side of the player, every tick it is
  not there; `[facing] type=reset` with each run, `[facing] coord=` the player's tile when
  it stands. **Sides** (the tile at each attack): archer west (-1,0) 732, seer east (+1,0)
  776, berserker north (0,+1) 781; each falls back to south (0,-1) (70 / 96 / 77) when its
  side is blocked; the rest are attacks made while the player moved.
- **Cadence**: a fixed 6-tick cycle from the add: berserker attacks may come on add+3+6k,
  seer add+4+6k, archer add+5+6k (every gap a multiple of 6: 1,165 of 1,165 gaps); a
  cycle tick is skipped when it is not standing adjacent. Attacks only at distance 1 (2,763/2,768).
- **Archer** 10850: projectile 9 `st10 et30 angle15 prog64 h163>146` (898/898), the splat at +1.
- **Seer** 10853: projectile 130 (fire blast) `st10 et30 angle16 prog64 h172>124` (964/964);
  spotanim 131 h124 d30 on the player, synth 7961 `varlamore_fremennik_mage_fire_hit_01` d0; splat +1.
- **Berserker** 10856: melee, splat at +1 (901/908).
- **Defend**: 10851 / 10854 / 10857, with the delay of the player's own hit.
- **Death**: 10852 / 10855 / 10858 the tick after the killing splat, `enabled_ops 0b00000`,
  `[del]` 3 ticks later.

## 3. Serpent shaman (12811)

- Add: 10861 like the trio. First cast add+3 (275/362), then every **5** ticks (1,465/1,551 gaps).
- Cast 10859 + spotanim 1458 h92 on itself; projectile 1459 `st67 angle14 prog32 h172>146`,
  **endtime = max(80, 67 + 4 x distance)** (1,726 shots, distance = Chebyshev source to target);
  on the player spotanim 1460 h124 and synth 8339 `varl_serpent_shaman_water_impact_01`,
  both delayed to the endtime; the splat at (endtime + 15) // 30.
- Death 10860.

## 4. Javelin colossus (12817, size 3)

- First attack add+3 (405/437), cadence 5.
- **Four autos, then a toss**, always (404/404 tosses follow exactly 4 autos).
- Auto 10892: projectile 2673 from the centre tile `st58 angle16 prog0 h200>138`, **endtime 90 at
  every distance** (2,647/2,647); synth 8348 `..._range_attack_hit_player_01` delayed 90.
- Toss 10893: synth 8382 at T; at **T+3** map anims 1446 `gargboss_debris_shadow_90` and 2674
  `..._artillery_slow`, both delay 30, and synth 8356 `..._artillery_attack_falling_whoosh_01`, on
  the player's tile at T+3; at **T+6** map anim 2265 `wild_falloff_meteor_blast_red` and
  sound_area 8352 (range 15) on that tile.
- Defend sound 8353 `varlamore_colossi_javelin_hit_01` (sound_area range 9).
- Death 10894.

## 5. Shockwave colossus (12819, size 3)

- First attack add+3 (91/101), cadence 5.
- Clap 10903: projectile 2520 `st50 angle0 prog40 h158>100`, **endtime = 76 + 8 x distance**;
  spotanim 2254 h64 and synth 5774 `toa_akka_projectle_impact_01` on the player, delayed to the endtime.
- Death 10895.

## 5b. Jaguar warrior (12810, size 2), minotaur (12812/12813, size 3), manticore (12818, size 3)

- **Jaguar** 10847, three claw splats on the swing's tick; first swing add+3 at the
  earliest: a jaguar with the player in reach on its add tick still waits until add+3 (2/2),
  otherwise it swings on the tick it comes into reach or the next (`q_first_reach.py`);
  cadence 5. Defend 10848 with the player's hit delay; death 10849.
- **Minotaur** melee 10843 at distance 1, cadence 5, first action add+3 (4/4 heals at +3).
  Out of reach it **heals** a wounded monster in sight (10844 on itself, spotanim 444 on
  the target at h64 x the target's half-size, a heal splat to its full bar). A heal holds
  it for 10 ticks: heal to heal 10 (12/12), heal to melee >= 10 (2 at 10, none at 5), melee
  to heal the usual 5 (`q_mino_heal.py`). Death 10846. Its defend sound is the empty one.
- **Manticore**: the charge C (10868), the three orbs shown in slots 1..3 at C+1..C+3
  (h300/400/500), the throw C+6 (10869) with the orbs leaving one a tick, each `st0 et30
  angle127` with progress 50 on the first; Mantimayhem I doubles each orb with an `st5`
  copy on the same tick (311 of 724 throws, every one in a Mantimayhem wave). First charge
  add+7, cadence 10. Death 10866.
- **Death to `[del]`**: 3 ticks after the death sequence for the trio, the shaman, the
  jaguar and the javelin colossus (2,330/2,330); **4** for the shockwave colossus (99/99),
  the minotaur (14/14) and the manticore (291/291). Sol: 9 (15/15, below).

## 5c. Sounds every monster sends

- **Defend sound**: a `sound_area` at the npc's centre, **range 9**, on every splat a player
  deals (human_hit 513, the javelin's 8353, the manticore's 600), and an empty `id=-1` for
  the shockwave colossus, the minotaur and Sol (`q_area_sounds.py`). Not a private synth.
- **The trio's swing**: an empty `sound_area id=-1 range=12 delay=0` from the npc's tile
  before its step, on every swing (archer 898, seer 964 and one at range 9, berserker 908;
  no swing without it, `q_trio_snd.py`).
- **Launch tile on a step tick**: the shaman's projectile leaves the tile it steps TO
  (166/166, `q_launch_tile.py`), so the attack is judged after the move.

## 5d. Npc info, every block (`tools/rsprox/npc_block_inventory.py`, 12 sessions against ours)

- **Facing**: every monster but the trio faces the player (`[facing] player=`) from add+1
  (shaman 284/284, javelin 321/321, manticore 208/208 ...) to its death seq, where a
  `[facing] type=reset` goes out on the death seq's own tick (shaman 272/276). Ours held
  this with `npc_setmode(playerface)` from the add (a bare `npc_faceplayer` is dropped by
  the engine's idle-face clear every turn: it shipped a reset every tick before). The
  engine now leaves a dying npc's face to its death stage (QUEUED, D+1), which also
  matches 4315/4292/4312 (~90 of ~113 deaths reset on the tick after the zero splat).
  The trio face the player's TILE (`coord=`) when they stand and reset when they run.
- **Javelin toss**: spotanim 2676 `npc_colossi_javelin_01_artillery_fire` delay 60 on the
  toss tick (394/394).
- **Gibs**: an instant `[facing]` on the add toward the player, to the nearest of 8, at
  2 x the size from the centre (347/347).
- **Crystals**: a `[facing] type=reset` as they set off (add+3/+4, 44/44); `[del]` on Sol's
  K+1 (death_delay 1 on the 10800 despawn seq).
- **Minimus** (619 lives): seq 10862 on the add, `[facing]` the player's tile; the line
  "A Grand Champion approaches!" / "A Rookie approaches!" by the player's glory rank.
- Kinds only the recorder causes: `face_pathingentity` (their click), sanguinesti heals,
  ice barrage / splash spotanims, Leagues relics; `face_angle` is rev <= 237's facing.

## 5e. Pathing and stats (`q_paths.py`)

- **First step**: the trio at add+2 (1,494/1,498), every other monster at add+1.
- **Steps**: the trio run (2 a tick, 80 %), the rest walk. A step is diagonal while both
  offsets to the target are 2+ (jaguar 100/123, shaman 191/219 with the player standing);
  a straight step there is another monster on the diagonal (55 of 75 straight steps with
  it occupied, 58 of 649 with it free: the pillars).
- **Who blocks**: the trio run THROUGH the other monsters (8 % of their steps end in one,
  700/9,340: `moverestrict=passthru`); everything else is blocked by them (under 2 %).
- **Stops**: melee types in reach (distance 1: 85-96 %); the ranged ones where the player
  is in reach and in sight, so wherever the player moved.
- **Hitpoints** (the damage that kills, every recorded life): archer 50, seer 50,
  berserker 48, shaman 125, jaguar 125, javelin 220, shockwave 125, minotaur 225,
  manticore 250, Sol 1500 (`hpbar_hud_basehp` 1500). All regenerate 1 every 100 ticks
  (Sol's HUD 1112 -> 1113 at 1348, 1448, 1548: why a Sol kill totals 1501). The
  levels are the cache's.

## 5f. Line of sight (`q_los_states2.py`, `colo_los_probe*.lua`, `q_los.py`, `q_los_step.py`)

The server's own routines (`t.world.los`, now answered in scriptrun too: `server_los`)
asked for every recorded (npc, player) pair, 16,427 of them. The arena's sight blockers are
four 3x3 pillars (1816..1818 / 1831..1833 x 3098..3100 / 3113..3115).
- **Attacks need the backward cast** (player tile -> the npc's footprint, the engine's
  ranged reach): where it and a forward ray disagree the monsters follow it (javelin 24
  attacks / 0 holds, shaman 28 / 4; forward-only: 1 / 17). Ours fires on `npc_approached`.
- **Within reach a monster stands, seeing the player or not** (the safespot): no-sight
  ticks within reach held 96-97 % (javelin 17,099 / 667 moved, manticore 11,753 / 441,
  shockwave 2,807 / 150, shaman 10,847 / 310); they move from distance reach + 1 (shaman 11,
  colossi 16). The step rule then matches javelin 96.0 %, manticore 95.6 %, shockwave
  94.1 %, shaman 95.9 % of recorded ticks (was 72-95 % with "walk until in sight").
  Ours holds 100 % (`colo_safe.lua` and the full runs).

## 6. Damage (all monsters)

`colo_damage.py` (splats only one attack can explain):
- **Protection prayer is read at the attack tick**, not the landing: prayed at the attack and
  not at the landing, 0 every time (shaman 12/12, javelin 3/3, berserker 1/1, seer 1/1,
  minotaur 1/1); unprayed at the attack and prayed at the landing, damage as unprayed
  (shaman 7/8 nonzero, javelin 7/11). Prayer blocks fully (1,656 prayed landings, all 0).
- Unprayed values against the wiki max hits: archer 0..14 flat (302; five above 14 under
  Relentless), seer max 12, berserker 29, shaman 28 (29 once, Relentless), javelin 48, jaguar
  48 per hit, minotaur 51 (n=12). The wiki's max hits stand.

- **Ours** (`q_dmg.py`, the tick log's raw column, 30 runs): the same max hits, plus
  Relentless's +1/+3/+6; non-zero hits uniform (archer 7.8 for 1..15 under Relentless I).
  The recorded shaman and jaguar hits sit high (2 of 26 shaman hits in 1..9, 1 of 17 jaguar
  in 1..14; p 0.01, 0.04) with no mechanism on the wiki ("each hit having its own
  independent accuracy and strength roll"): left as uniform, an open observation.
- **Landing ticks** (`q_hitxc.py`, `q_hitxc_d.py`: splats per offset from each attack):
  the shockwave's splat is on T + endtime / 30 rounded down (endtime 92..116 +3, 124..148 +4,
  156..172 +5, 180..204 +6; ours was a tick late from 108); the rest match (projectiles
  T + (et + 15) / 30, the trio and minotaur +1, the jaguar T, manticore orbs +1 each).
- **Projectiles** (`q_proj.py`, the wirelog): start, end against distance, angle and
  progress match for every Colosseum id (1459, 2673, 2520, 2681/2683/2685, 2687, 2708,
  2709). The manticore orders: mage first 58 % outside Mantimayhem III (558 / 964; z 4.3
  against even), any of six under III.

## 7. The intermission and the modifier offer

Wave complete on tick D (the "Wave N completed!" message, varps 4131/4136/4137/4132):
Minimus 12808 added at D+3 beside the player (`npc_human_ready_spawn` 10862, facing the
player), prayers switched off, `if_opensub mainmodal colosseum_intermission_2` (865) with
clientscript 4931 `(wave, mod1, mod2, mod3, int, long, long, heldmask)`. The choice
(`mod_button_1..3` 865:15..17, varbit 9788 = 1..3) and `right_button` 865:41 confirm:
Minimus `[del]`, synth 8010 `varl_audio_modifier_continue_12`, busy 0, modal closed; the
next "Wave: N" and its adds **5 ticks** after the confirm (index 1223 [106]->[111]).

**The offer** (`build/colosseum_private/q_offers.py`, 486 offers after waves 1-11, 112 before wave 1):
- Before wave 1 always Blasphemy, Relentless, Frailty (4, 5, 12): 112/112.
- The pool: every modifier except a held one-level modifier (0/242 re-offered) or a held
  modifier at level 3 (0/246); Dynamic Duo (9) and Red Flag (13) only after waves 6-10;
  after wave 11 not Mantimayhem (0), Reentry (1), Myopia (11), 9 or 13. 0 offers fall
  outside this pool.
- The draw: three cards by weighted sampling without replacement, a held (upgradable)
  modifier weight **6**, any other 1 (log-likelihood -2139.7; weights 5/7/8: -2146.6/
  -2140.5/-2145.8; uniform -2546.7; "up to two upgrade slots" models at best -2166.4).
  The cards are shown in draw order: card 1 is held 386/486, card 3 unheld 457/486.

**Timings across every session** (`q_flow.py`, 471 confirms, 438 intermissions): Minimus
and the modal at D+3 (D+8 after wave 11); the confirm's synth 8010 and Minimus's `[del]`
on one tick, the wave's adds 5 after (451; 18 at 11). Ours: the same.

## 8. Entry, lobby, cash-out, chest

- **Lobby entrance 50751** (index 1223 [46..63]): op at L+1 (face the gate); L+2 busy 1,
  fade out (`if_settext fade_overlay:message ""`, `if_opensub overlay_atmosphere fade_overlay`,
  948 `0,255,0,0,50`, hpbar hidden), `midi_song_v2 147` (fadeoutspeed 50); L+3
  `minimap_state` 2 + `minimap_toggle 2`; L+5 teleport into the arena copy at template
  1824,3094 facing 1824,3104, clientscript 1846 once per Colosseum seq (preload); L+6
  varps 264/265/266 = the three cards (4, 5, 12), the player walked north one tile a tick to
  1824,3101, `colosseum_boss_seated` 12827 added at 1823,3123 and `invisible_type8_blocking_size4`
  50703 there a tick later, fade in (948 `0,0,0,255,50`), `minimap_toggle 0`; L+9 Minimus
  added at 1824,3106 (10862, `say` "A Grand Champion approaches!" with the npc_say message,
  facing the player); L+12 "Let me know when you want to begin." (say), busy 0,
  `multiway_indicator` 1, the fade overlay closed, `set_player_op 2 null`.
- **The first card** comes from Minimus op1 (Start-wave): busy 1, modal 865 with
  `4931(0, 4, 5, 12, 0, <next value>, 0, 0)`; a card and Confirm: as between waves.
- **The arena is an instance copy** (index 1211 raw: the trio added at 12833,2405 for
  template 1823,3109).
- **Cash-out** (index 1211 [1010..1012]): `left_button` 865:10 then `confirm_confirm`
  865:22: the message `<col=229628>Search the chest nearby to retrieve your earned rewards!`,
  `midi_song_v2 147`, busy 0, Minimus deleted and re-added at 1830,3103 (`spawnangle=west`,
  10862, `enabled_ops 0b00100`, `[facing] coord=1828,3103 instant`), modal closed,
  `if_settext colosseum_reward_chest:colosseum_reward_value "Total Value: N"`; next tick the
  chest `colosseum_reward` 50741 at 1829,3105 rotation 1, sound_area 7971 delay 10 and
  7998 delay 45, range 15, on its tile.
- **The chest** (oploc1, L+1): varbit 9802 1, modal 864, clientscripts 4923 `(wave, heldmask)`
  and 4929 `(value)`, `if_setevents` 864:11 0..26 OP1 and 864:17 0..30 OP1-5, the value text,
  `interface_inv_init` over inv 843; the "Valuable drop" and collection-log lines at L+2.

- **Entry, every session** (104 entries, `q_flow.py`): L+1 the op, L+2 the fade, L+3 the
  minimap, L+5 the copy (E), the seated Sol E+1, Minimus 3 after it, "Let me know..." and
  busy 0 3 after him (104/104); the walk in is WALKED with the run orb on (104/105, the orb
  on in 110/110): seven single steps to 1824,3101. Ours: the same (`p_runblock(8, "")`).
- **Leave** (Minimus op3, "Yes."; 80 exits, `q_exit.py`): the choice X-2 with the fade, the
  minimap X-1, the lobby X on a tile within 1 of 1802,9506 (all nine), X+2 the fade in,
  the minimap, busy 0, multiway 0 and the lobby's track three times (787 or 811 at random,
  25/20: no ramp, then 0,60,60,0 twice).
- **Death** (12): `human_death` K+3, the lobby 4 ticks later on a tile within 2 of
  1802,9506 (12 tiles), "Oh dear, you are dead!", the minimap, song 147 and `you_are_dead`
  (jingle 90, length 4,718 ms) on X; X+1 busy 0, multiway 0 and the lobby's track. Ours
  sent a Colosseum death to the Ancient Prison (2900,5203: God Wars' Nex rectangle read in
  the copy) until `death.rs2` skipped that check for it.

## 9. Glory (varp4132, on the completion tick)

Glory for a wave = `100 x wave` + `(500 - duration) x wave` when 0 < duration < 500 ticks
(varp4136) + `param_1903 x level` for every modifier held through the wave (rank varbits at
the wave's start; one-level modifiers from the next 4931 mask) + `100 x wave` more when
varp4134 (damage taken) is 0; wave 12 adds a further **1,200**. 438 of 442 completions
match (`xlevel` 266 where level matters, 172 all-level-1); the 4 left are wave 12 before
the bonus was known, all 4 match with it. The wiki's "x level" (ruling R2) is right; the
plan's N1 (+1000) is not: +1200.

## 10. Sol Heredit (wave 12; 19 fights, `colo_sol.py`, `build/colosseum_private/q_sol*.py`)

**The opening** (index 1223 [745..751], W = confirm + 5): W varp4133, busy 1, the seated
12827 plays `..._arena_jump` 10876, spec regen 50, `midi_song_v2 782 (0,60,60,0)`; W+1
"<col=ff3045>Sol Heredit jumps down from his seat...</col>" and the fade out; W+2
minimap 2; W+4 the player teleported to 1825,3103 on plane 2, the seated Sol `[del]`;
W+5 back to plane 0 facing 1825,3104, `throne_civitas_colosseum01_big01` 52514 at
1823,3123, clientscript `cam_forceangle` (181, 0); W+6 minimap 0, busy 0, **Sol added at
1823,3108** (`spawnangle=south`, `..._arena_land` 10877, spotanim 2666 delay 15, `[facing]
coord=` the player instant), the barricade (below), the fade in and the 1846 preloads.

**The barricade**: `shield_combatant_1..6` (50753..50758, a random variant per tile) on
x 1817 (z 3101..3112, rot 3), x 1832 (rot 1), z 3099 (x 1819..1830, rot 2), z 3114 (rot 0).
The floor is x 1818..1831, z 3100..3113 without its four corners: 192 tiles (every dust
tile of 587 attacks lies in it).

**Attacks** (at the tick T of the telegraph; he attacks only with the player within 1 of
his 5x5, 347/347 spears):
- Spear `..._melee_attack_telegraph` 10883 and shield `..._shieldslam_telegraph` 10885: the
  dust (2669-2672, a random one of the four per tile, delay 2 x the tile's ring from his
  centre) on T+3, and the hit on T+3 for a player whose tile at the START of T+3 is in
  the shape: 185/204 in it hit (the rest the recorders' shield kit), 2/140 outside.
  Shapes (rotated to his facing): spear = the 5x5, the row in front, and prongs out to 8
  tiles in columns 1,3 (pattern 1) or 0,2,4 (pattern 2); shield = the whole floor except a
  safe ring at distance 2 (180) or 3 (63) from his footprint. Not reduced by Protect from
  Melee; damage up to 45.
- Triples: short `..._tripleattack_shorter` 10887 (spotanim 2668) hits 15, 25, 35 on
  T+3, T+6, T+9; long `..._tripleattack` 10886 (2667) hits 15, 30, 45 on T+3, T+6, T+10
  (16/31/46 under Relentless I). Protect from Melee on the hit tick blocks it; up on the
  tick before, Sol drops it: "Sol Heredit doesn't take kindly to your eager prayer." and
  the hit lands (15 of 15 recorded early prayers).
- Grapple `..._grapple_attack_telegraph` 10884 delay 30: a `say` (and the chat line)
  "<col=ff0000>I'LL BREAK YOUR <col=ffffff>BACK<col=FF0000>!" / CRUSH YOUR BODY / BREAK YOUR
  LEGS / CUT YOUR FEET OFF / TWIST YOUR HANDS OFF, varbit 9800 = 1, synths 8253 (75) and
  8213 (210); on G+4 varbit 9800 = 0 and either "You perfectly parry Sol Heredit's grapple!"
  (the slot clicked on G+4: 5/5) or "Sol Heredit grabs hold of your <col=00e6e6>ITEM</col>
  and deals massive damage!" with a splat (17..43) and synth 8095.
- First attack add+5 (14/19).
- **Gaps** when he does not step between (`q_sol_shieldgap.py`): spear 6, shield 5, one tick
  more 27 % of the time (spear 83/31, shield 44/16; not the player's distance); the
  recorded 7s and 8s beyond that are chases (every one has steps in it). Grapple 7 (17/19),
  long triple 12 (14/15), short triple 12 or **11** (23/6; no recorded field tells them
  apart), 7 after a phase line (66/74).
- **Specials** (grapple, short and long triple) are one cycle (`q_sol_specfit2.py`, 51
  gaps with no phase line between): the next may come **38** ticks after a short triple,
  **29** after a grapple or a long one, then on 35 % of the attack decisions past that
  (logL -90.6, against -104.6 for a single 29-tick gate). None above 90 %; 90-75 % short
  triples only; below 75 % 3 in 8 are grapples (22 of 59), the rest short triples above
  50 % and long below.
- Spear against shield: 58 % spear overall (347 / 590); by band 70, 55, 53, 62, 70 %
  (chi-square p ~ 0.05 against one rate; kept at one rate, see §13).
- **Movement** (`q_sol_shieldgap.py`): still for 5 ticks after a spear (his first step on
  T+6, 189/189 chases), 3 after a shield slam (T+4, 142/145), 4 after a grapple (G+5); free
  through a triple (steps from T+1 while its hits land). Then toward the player: one tile
  when 2 away, run (2) when 3+, a step off when the player is under him. An attack that
  is due goes first from where he stands (the player beside or under him); he never
  attacks on a tick he steps: the attack the tick after his last step (spear 185/189,
  shield 110/145, the rest +2). Chase gaps: spear 7/8/9 at 79/17/3 %, shield 5/6/7 at
  47/45/8 %; ours with a kiting player (`colo_sol_kite.lua`, 12 fights) 67/33 and
  43/40/14.

**Phases** at 90/75/50/25/10%: the line takes the place of the next attack due after the
crossing (0..8 ticks after it; his last attack exactly its own gap before, 76/76), the says: "Let's start by testing
your footwork." (at the start), "Not bad. Let's try something else...", "Impressive. Let's
see how you handle this...", "You can't win!", "Ralos guides my hand!", "LET'S END THIS!".
Each line P: P+1 six lightning strikes `vfx_colosseum_sunfire_lightning_01_beam_01` 2698
delay 30 on tiles near the player (the player's own tile among them), synths 8053 (30),
10046 (90); P+2 a beam crystal 12824 (seq 10798) on an edge (not after the 10% line);
P+6 the six tiles become `colosseum_holy_fire` 50746 "Molten Sand" (shape 10, a random
rotation); the next attack P+7.

**Crystals** (`q_sol_synths.py`, `q_sol_mananims.py`): crawl their edge one tile per 2
ticks from add+4, on their own 2-tick clock, through whoever stands there (onto the
player's tile 3 times, never around). The laser:
- **W, the warning**: synths 8253 `varl_colossi_totem_first_beam_10` (delay 75) and 8213
  `..._line_fuse_hiss_explosion_07` (210) (148/152), and every crystal stops (its last
  step on W-1 or W-2). The first W is the first crystal's add+5 (17/19); every later W is
  one of Sol's attacks: the first attack 29..35 ticks after the last W (0/59 below 29,
  1/7 at 29 rising to 15/16 at 35, every one from 36), and 11 after "LET'S END THIS"
  (5..10: 0/21; 11+: all). So the scan period is not fixed (30..48 recorded).
- **W+3 the scan**: seq 10801, a line of 13 tiles from each crystal (2689..2692 by crystal,
  delay 3..39 by 3); **W+7 the shot**: seq 10802, 2693..2696 along the line and 2697 one
  tile past its end, all map anims at **height 128**; at that end tile `sound_area` 8230
  `varl_colossi_totem_beam_explosion_01` range 20 size 20 (one per crystal, 319/319); a
  player on a line takes 65..75 (the tiles the sweep reaches by ~21 cycles on the shot
  tick, the rest a tick later).
- The crystals crawl again at shot+1 (72 of 270) or shot+3.
- Sol's spear and shield dust is 2669..2671 at random (2672 never), delay 2 x the ring
  from his centre, out to ring 10 (delay 20) where the floor allows.

**Eager prayer**: Protect from Melee up on the tick before a triple's hit: Sol drops
**only that prayer** (varbit 4101 67125248 to 67108864: the others stay), "Sol Heredit
doesn't take kindly to your eager prayer.", and it cannot be raised the next tick
("You've been injured and can't use that prayer!", 5/5 at drop+1; up again at +2).

**Grapple** answers (`colo_sol_kite.lua` exercises them): any of the five slots, worn or
empty (back 6, legs 6, body 5, feet 3, hands 2 of 22; an empty one named by the part,
"Feet"). A click on the named slot of the worn-items tab is his, not a Remove (no item
comes off): on G+4 "<col=229628>You perfectly parry Sol Heredit's grapple!" (5/5), on G+3
"<col=229628>You successfully defend your Legs from Sol Heredit's grapple!</col>"
(index 4117 [2296]), each with varbit 9800 = 0 and `warguild_parry_defend` 4177 on the
click and no hit; another slot "<col=ff3045>You defended the wrong body part!</col>", the
grapple still on (index 4095 [8741]). Unanswered: "<col=ff3045>Sol Heredit grabs hold of
your <col=00e6e6>ITEM</col> <col=ff3045>and deals massive damage!</col>" on G+4.

**His death** is the killing splat's own tick K: seq 10888, spotanim 2680, `enabled_ops
0b00000`, `[facing] type=reset`, the kill-count/wave/duration messages, the crystals' 10800
and the sand's 10814 (index 4110 [1550]); K+1 the death line (13/14), the crystals gone
(36 of 52) and the sand's 10814 again; K+2 every sand tile `invisible_type8_nonblocking`
(10 of 14 fights; a kill a tick after its splat sends 10814 once and clears on K+1);
K+8 the chest line, Minimus, the jingle; `[del]` at **K+9** (15/15) with Volatility's gib
(12832, explosion 2724 on his centre tile, +2,+2) on that tick. On landing (L): the
fade-in and minimap back on L, `cam_shake` axes 0..2 (random 5) on L+1, `cam_reset` L+4
(index 1223 [751..755]).

**Death** lines (one at random, K+1): "My compliments, champion...", "A worthy...
opponent...", "Your light shines... brightly...", "Ralos has smiled upon you..."; on
killing the player, two ticks after the killing splat (the tick before `human_death`,
4/4): "Your light shines no more.", "Pathetic, really...", "You had me excited for a
moment.", "Your lack of coordination is concerning." (`colo_sol_die.lua`).

## 11. Modifiers (`q_reentry.py`, `q_pool_life.py`, `q_bees.py`, `q_totem*.py`; ours: `colo_mods.lua`)

Levels per wave from the rank varbits (`colo_waves.waves_with_mods`); 438 recorded tosses.

**Reentry** (struct_891: "Javelins leave a temporary pool / a big permanent pool / a very big
permanent pool of molten sand where they land"):
- The falling javelin at T+3 is `npc_colossi_javelin_01_spearhead_fire_slow` 2677 under any
  level (141/141), `..._artillery_slow` 2674 without (282/282).
- At T+6 (the blast) the landing tile becomes `colosseum_molten_pool_1` (shape 10, random
  rotation) with `sound_area` 7973 `lava_spawn_06` delay 1 range 15 at I (45/45), 7991
  `lava_spawn_06_large` at II and III (64/64).
- II throws one more, III two (20/20, 44/44): `map_projanim` 2709 `st0 et30 angle20 prog10
  h0>0` from the landing tile to a neighbour; at T+7 that tile becomes `molten_pool_2` or
  `_3` (40 / 55) as **ground decor** (shape 22), random rotation. The neighbours are taken
  in a fixed order, x then z ascending ((-1,-1), (-1,0), (-1,1), (0,-1), ...), skipping
  tiles that already hold a pool (41/57 explained by pools alone; the rest skip the
  pillars' tiles).
- A toss that lands on a pool makes nothing new: no pool, no throw, no lava sound; the
  blast still plays (9/9).
- **Lifetime**: while Reentry is below II, every landing pool (the toss's and Volatility
  III's) lasts the wave: on the completion tick each becomes `invisible_type8_nonblocking`
  (shape 10; no `loc_del`, no despawn anim) (84/102; a pool made on the completion tick goes
  the next tick, 18/18); in Sol's wave at his `[del]` + 2 (K+11, 3/3). At II and III no
  pool is ever cleared (0 of 87), into Sol's fight.

**Volatility** (struct_893: "Enemies explode on death" / "in a large radius" / "Damaging
energy in a large radius on death and leave behind molten sand"):
- The gib (12828 human, 12829 manticore, 12830 minotaur, 12831 colossi, 12832 Sol) on the
  dying npc's SW tile on its **`[del]` tick** (306 of 363 at the death seq + 3; the
  shockwave's at +4 with its later `[del]`).
- The explosion map anim (2713 / 2721 / 2723 / 2722 / 2724) and the sound on the npc's
  **centre** tile (human +0, size 3 +1,+1, Sol +2,+2): `volatility_02` 7970 at I (295/295),
  `volatility_large_02` 7992 at II and III (188/188), both range 8 size 6.
- III leaves `colosseum_molten_pool_1` (shape 10) on the centre tile (90/90; none at I or
  II, 0 of 380).

**Solarflare** (struct: "Damaging energy orbits an area slowly / quickly and deals more
damage / very quickly, deals more damage and disables prayer"): four flares on wave tick 0
at every level (59/33/11 waves, never more), from the corners' inner tiles, each round a
5x5 square clockwise: I a tile every 2 ticks with 7-tick corner stops, II no stops, III a
tile a tick with 1-tick stops. In Sol's fight the four start round the centre (1823/1826
x 3105/3108; first legs S, W, E, N) on his landing + 3, every level (12/12).

**Bees!** (one / two / three swarms): the jar 2708 on wave tick 0 (Sol: landing + 3, 7/7),
the swarm 12823 on its tile 5 ticks later (156/156). It crawls a tile every 2 ticks at the
player and stings every tick he is under its 2x2, the splat (`hitsplat_poison`, 65 to him)
on the sting's tick (168/168): 5..10, 10..20 at III. One hit kills it (34/34): despawn
seq 10824, and 3 ticks later (52/52) it is hidden on its add tile -- seq cleared, no ops,
the bar removed, body model 32709 -- and back 50 ticks on (23/24) with Attack, crawling 5
ticks after (21/21). `q_bees.py`.

**Totemic**: see the proc header (`colosseum_modifiers.rs2`): a totem per monster at half
health or less (the tick its bar drops, 31/45), a tile SW of its SW tile, healing it at
add + 7 and every 6 (seq 10828, 2687), 30 % of its max capped at 75, landing 6 ticks on
(71/71); removed with its monster, 1 hitpoint (`[del]` 2 ticks after the hit, 22/22).
`q_totem.py`, `q_totem2.py`.

**Quartet**: a fourth trio member, any of the three (53/42/33), two tiles south of the
berserker (125/128; 3 mirrored, all with the berserker on z 3110). **Dynamic Duo**: the
second shockwave on the ring two tiles round the first. **Red Flag**: the routefinding
minotaur 12813 in place of 12812.

**Relentless** (ignore 33/66/100 % of defence, +1/+3/+6): the extra only on a hit that
lands (II and III: 0 or 6+, never 1..5). **Blasphemy**: prayer drained by
floor(damage x 20 x level / 100) of an unprayed hit (684/161/144 drops match their level).
**Mantimayhem**: I an `st5` copy of each orb; II venom (splat 5, 6..20 by 2); III any order.
**Myopia**, **Frailty**, **Doom**: from the struct text (no usable recorded measure: the
recorders' projectiles under Myopia, 7 Frailty waves, 6 Doom waves).

## 12. Ours against the recordings (six `colo_full.lua` runs, `build/colosseum_private/runs/full1..6`)

| check | recorded | ours |
|---|---|---|
| shaman launch tile on a step tick | new 166/166 | new 36/36 |
| javelin auto `st58 et90`, toss 4 autos then 1 | 2,647/2,647 | 4,601/4,601 |
| shockwave endtime 76+8d | every d 2..16 | every d 1..16 |
| manticore orb `st0 et30 angle127`, Mantimayhem `st5` copy | yes | yes |
| first attack add+3 (shaman/javelin/shockwave/jaguar/minotaur), add+7 manticore | yes | yes |
| cadence 5 (10 manticore, 6-tick trio cycle) | yes | yes |
| defend `sound_area` range 9 / `-1` for shockwave, minotaur, Sol | yes | yes |
| trio swing `sound_area -1 range 12` | 2,770 | 2,072 (every swing) |
| minotaur heal to melee | >= 10 | 10 (10/10) |
| death seq to `[del]` 3 / 4 (shockwave, minotaur, manticore) / Sol 9 | yes | yes |
| Sol gaps standing: spear 6/7, shield 5/6 | 73/27, 73/27 | 69/29, 72/27 (20 fights) |
| Sol specials gate 38 after a short triple, 29 otherwise | yes | yes |
| Sol short triple to next 11 / 12 | 6 / 23 | 21 / 59 |
| Sol phase line in place of the due attack | 76/76 | 100/100 |
| trio first step add+2; trio pass through monsters | 99 %, 8 % | 100 %, 2-7 % |
| hitpoints per type | as above | the same |
| Sol chase gaps (spear 7/8, shield 5/6/7) | 79/17, 47/45/8 | 67/33, 43/40/14 (kite) |
| laser W: P(attack is W) at 29..35 ticks | 14..94 % | 13..99 % |
| crystal hold gaps 9/10/11/12 | 13/13/47/27 % | 13/15/36/37 % |
| crystal map anims height 128, impact sound 8230 | yes | yes |
| monsters face the player add+1 .. death seq, reset on the death seq | yes | yes |
| javelin 2676 on the toss, gib facing, crystal facing reset | yes | yes |
| Minimus at D+3 every intermission | 619/619 | 11/11 per run |
| Solarflare: 4 per wave at t0; Sol's round the centre at landing + 3 | yes | yes (`colo_mods.lua`) |
| Bees: jar to swarm 5; kill, hide, back 50 ticks on | yes | yes |
| Totemic: per monster, add+7 / 6, heal 30 % (<= 75) | yes | yes |
| grapple parry / defend / wrong part / grab lines | yes | yes (kite) |
| Volatility gib on the `[del]` tick, sound by level, centre tile | yes | 126/126 |
| Reentry T+3 anim, T+6 pool + sound, T+7 thrown pools | yes | yes |
| Reentry I pools cleared on completion, II/III kept | yes | yes |
| spawn angles per type, Minimus fallback, jar source and sounds, swarm's first step | yes | yes |
| ranged attack on the backward cast; stand within reach out of sight | 96-97 % | 100 % |
| shockwave splat T + et / 30; manticore mage first 58 % | yes | yes |
| entry L+1..L+9, walked walk-in, confirm +5, D+3 | yes | yes (30/30) |
| leave and death exits: ticks, tiles, music, jingle length | yes | yes (30 + 6) |

## 12b. Second pass (2026-10-23): what changed

Every row below was a difference between the recordings and our wire; each is fixed, and the
regression (30 `colo_full`, 10 `colo_mods`, 6 `colo_die`, 2 Sol, `colo_doom`, `colo_choice`,
4 lobby runs) passed after the last change.

| what | recorded | was | now |
|---|---|---|---|
| projectile heights | raw (fireblast 172/124, iron arrow 163/146, watersurge 172/146, javelin 200/138, shockwave 158/100, manticore h/50, totem 220/138, jar 400/0) | script value x 4 (148, 164, 140, 48 ...) | the raw value: `projanim_pl_raw` / `projanim_npc_raw` / `projanim_map_raw` (engine opcodes 11287..11289) |
| magic impact spotanim (seer, shaman, shockwave) | on an accurate hit only, slot 2 | on every cast | on `~colo_accurate` = true |
| manticore orb impact + synth 8370 | on an unprayed accurate orb, the tick it lands | none | `colo_manticore_orb_land` queue |
| headbar per splat | one `[headbar]` per hitsplat, filled to the hp that splat left | one bar per tick | engine: `hp_after`/`bar_fill` per hitmark, 241 + 239 encoders, player and npc |
| HP HUD | never closed between waves; mounted with the gameframe | closed with no target | kept mounted, `%hpbar_hud_npc` = -1 |
| Doom | splat 73 with its stack bar, `vfx_doom_scorpion_attack_impact_01`, `doom_player_impact03`, the "N/cap points of doom!" line; at the cap "You have been doomed!", death spotanim + seq, death 7 ticks on | struct text only | `colo_doom_stack` / `colo_doomed`; `player/death.rs2` skips `human_death` for a doomed death |
| Frailty | 99 held at 90 (a cap of base - base x pct) | percentage of current | `sub(stat_base, ...)` |
| "Oh dear, you are dead!" | once | twice (`combat_death_message` + `respawn_message`) | once |
| venom line | "You have been poisoned by venom!" | other wording | the recorded line (`venom.rs2`) |
| Sol's eager-prayer line | `mes_spam` | `mes` | `mes_spam` |
| leaving the cards unchosen | "You must make a choice before continuing!", once per tick | nothing | `colo_choice_pending` (if_close, opnpc1/3) |
| confirm | state `starting` before the close | after | before (the close no longer reads as a dismiss) |
| intermission music | no 147 at D+3 | 147 + a cards-music queue | removed |
| lobby music on settle | 4 x `midi_song_envelope(787/811, 0, 60, 60, 0)` | 1 | 4 |
| lobby npcs | 15 (Minimus outside, Glory, 3 gladiators, 7 sparrers, 4 push-ups) | none | `colosseum_lobby.spawn`; sparrers swing every 4-5 ticks (1 in 5 defend) on the dummy beside them with `varrock_heavybag_defend_delayed` |
| npc wander offset (engine-wide) | uniform over -range..range (Glory chi-squared 5.9) | skewed (142.9) | `((v * (2r+1)) >> 24) - r` |
| manticore death sound | `magmaquiris_death` | null | set |

## 12c. Third pass (2026-10-24): what changed

| what | recorded | was | now |
|---|---|---|---|
| guaranteed max hits | the trio's weakness always lands its max (archer melee, seer ranged, berserker magic; the wiki's pages); every landed weak hit is splat 43 (8/8 barrages on the berserker); a freezing spell still rolls on the berserker (index 1211 barrage splashes, Ice Burst 5/5, Entangle 4/4) | ordinary rolls | `~colo_trio_weak_to` in `~player_npc_always_max`; `~player_npc_spell_hit_roll` (freeze_time > 0) rolls on the berserker. Probe: whip 2 x150, bow 24 x12, Fire Blast 16 x18, barrage 44/87 hits at 50 % and every hit max |
| manticore orb order | one order for the whole life (299/299) | redrawn each charge | `^colo_var_manti_order`, drawn at the first charge |
| two manticores | the second copies the first's order (61/62); both charge at add+7 (57/57), then the one due second on a tick waits 5 and keeps its 10: 7/17/27 against 7/22/32; a charge 1 tick after the other's goes (3) | independent | `~colo_manticore_pick_order`, `~colo_manticore_other_charged_now`; ours 66/66 one order, 18/18 shared, the recorded schedule |
| npc movement defaults | only Glory and the lobby gladiators wander; flares crawl their square, gibs never move | the generated `defaultmode=wander, wanderrange=5` on the flare, the gibs and 11 more | `defaultmode=none` blocks; `tools/rsprox/npc_movement_audit.py defaults` lists every type |
| solar flare steps | 2 along a side, 7 at a level-1 corner, 1/2 at level 3 (460 lives); never held up | a parity from the add tick (8 at corners); +1 +4 +6 (engine crawl rest) | timed from the last step; `moverestrict=passthru`; engine: the crawl rest is a tick of time |
| bees on a kill | ops 0b00000 at K and on the hide (K+3..5, `teleport` to the add tile, 52/52), 0b00010 back at K+55 | the revive reopened every op on the hide; `npc_tele` jumps | engine: a revive keeps the ops a script set that tick; `npc_teleslide` |
| a face on the player | `[facing] player=` re-sent on every tick the angle from the npc's centre to the player's tile (last tick's) turns: 9,673 on a turn, 6 without (59 sessions); Stronghold 148/6 | latched, sent once | engine `npc_face_follow` (every npc facing a player): ours 139 on a turn, 7 without |
| Sol's melee dust | delay = 2 x ring from his centre on all 54,139 tiles, rings 0..10 | the same rule | unchanged: ring 10 needs Sol near a wall, which a standing bot never brings about |
| per-player invs | -- | 16, full on a played account (the entry aborted) | 128 |
| scriptrun press reach | the client draws 25 tiles | any loc in the scene | both lanes refuse a copy beyond the draw distance (`DrivePick_Pressable`) |

## 13. Open gaps

- **Shaman and jaguar damage** sits high in the recordings (see §6); ours is uniform.
- **Four totems** face east or south on the normal tile (§1.3), unexplained.
- **A monster that moves and fires on one tick** is judged by the forward ray from its
  predicted tile (`npc_approached` reads the footprint it stands on).
- The **outside entrance** to the lobby (50749) is implemented but no test drives it.
- **Step-tick share**: recorded players move and the monsters attack on a step tick 9% of
  the time; our bot stands still (2%). The mechanic matches (the launch tile); the share
  is the player's.
- Reentry I pools landing on the player's tile: ours far fewer (the bot never moves, so the
  second toss lands on the first pool); the rule (a pool makes nothing) matches.
- Sol's spear-vs-shield share by hp band (70/55/53/62/70 %) is borderline non-constant;
  ours is one 58 % rate.
- Sol's short triple 11 vs 12 and the 27 % extra tick on spear/shield: drawn at the
  recorded rates; the deciding rule (if any) is not in the recordings.
- Chases: `colo_sol_kite.lua` steps the player away every 9..15 ticks; the recorders step
  after most spears (189 of 303), so our spear-chase sample is small (18). The monsters'
  chases are not exercised (the full-run bot stands still).
- Our engine accepts a button on an interface that is not open (the old `colo_full.lua`
  confirmed the cards before they arrived, so Minimus never came); a real client cannot.
  Engine-wide, not changed here; the content's own state checks hold the rest.
- Recorded monsters re-send `[facing] player=` several times a life; ours sends it once
  (the latch is held). No visible difference.
- Player queues: an entry queued from inside another queue lands a tick early or late by
  which slot it takes (the drain walks slots, not insertion order). The Colosseum avoids
  it (the bees ride Sol's npc queue); engine-wide, not changed.
- **Myopia** rests on its struct text (no recorded ranged wave under it). **Doom II and III**
  are unrecorded: the cap comes from the struct text, the bar's denominator is that cap.
- **Clientscript 5224** (`pvp_icons_comlevelrange`) is sent through the run; 98 % of them sit
  on a prayer toggle but the rest are unexplained, so ours sends none.
- **Gladiators 1 and 3** still show a wander chi-squared residual after the uniform offset.
- A size-2 jaguar on top of a standing player steps out in a random cardinal (the
  reference's naive path); boxed in east and west it flips north/south and stays on him.
  One recorded case (it stepped west and cleared): the rule is not refuted, the state is
  our bot's.
- The face re-send misses 9 of 148 turns (a shaman stepping in); the recordings' misses are
  their dying and out-of-view ticks.
- The HP HUD is mounted twice at login (the gameframe carry re-mounts it); invisible.
- Sol's holy fire landing on a permanent pool replaces it in ours (same layer); no recording
  shows either way.
