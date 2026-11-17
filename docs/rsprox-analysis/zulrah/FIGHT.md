# Zulrah's fight, from RSProx

The fight inside the shrine: the rise, the rotations and what happens when a fight
outlives one, every attack and what it sends, the clouds, the snakelings, the damage,
every npc-info block, the death. Everything around it (Zul-Andra, the boat, the
arrival and the exit, the kill's lines, the loot, the music) is [README.md](README.md).

Scripts: `minigame_zulrah/scripts/zulrah_phases.rs2` (the runner), `zulrah_combat.rs2`
(spits, tail, hits taken, death), `zulrah_adds.rs2` (clouds, eggs, snakelings),
`zulrah_fight_api.rs2` (what the session calls), `zulrah_selftest.rs2` (`::zulrahrun`);
data `configs/zulrah_phase.dbtable/.dbrow` (written by [gen_dbrow.py](gen_dbrow.py)),
`zulrah.constant`, `zulrah.npc`, `zulrah.varp`. The Near-Reality port they replace is gone.

**Every recording is Grid Master** (seasonal: the recorder attacks every 2 ticks, takes
about 10% less damage, fights beside a `league_guardian`). Timings, ratios and shapes
carry over. Damage maxima are biased low, and the cap on hits Zulrah takes is not
observable (the recorder hit it for up to 221).

## Sessions

| index | rsprox.net | rev | fights | what it shows |
|---|---|---|---|---|
| 3259 | 3276 | 234.1 | 2 | fight 0 [8318..8847]: 20 surfacings over 529 ticks, rotation 4 whole, then rotation 2 phases 2-8 (the only rotation wrap recorded); fight 1 [9088..9388]: rotation 4 phases 1-11, killed [9382]. 48 snakelings |
| 819 | 820 | 234.1 | 47 | 47 kills, 12 of which reach phase 2 (crimson middle 6, tanzanite east 5, green east 1); 2 snakelings hatched on a death tick |
| 3324 | 3342 | 235.1 | 4 | 4 kills inside phase 1 |

53 rises, 52 deaths (3259 fight 0 ends with the recorder teleporting out, [8844] `human_castteleport`). No session is
on a normal world, and none reaches a rotation's later phases except 3259's.

Tools (run from the repo root; `tools/` beside this file):

```sh
R=~/.rsprox-index-cache/home-raw/.rsprox/binary/archive
python3 docs/rsprox-analysis/zulrah/tools/zphases.py $R/idx3259.txt     # per-phase summary of every fight
python3 docs/rsprox-analysis/zulrah/tools/ztick.py <ticklog.tsv>        # the same for our tick log
python3 docs/rsprox-analysis/zulrah/tools/zlife.py rec|ours <files>     # snakeling lives, block by block
python3 docs/rsprox-analysis/zulrah/tools/zstats.py                      # snakelings, clouds, venom, damage
python3 docs/rsprox-analysis/zulrah/tools/zdmg.py <txt>                  # every splat on the recorder with its causes
python3 docs/rsprox-analysis/zulrah/tools/zrot_md.py                     # the rotation tables below, from the dbrow
python3 docs/rsprox-analysis/zulrah/gen_dbrow.py > .../configs/zulrah_phase.dbrow
```

The test is `test/bosses/zulrah.lua` (below). Every "ours" figure here is its final
run's tick log unless a row says otherwise.

## Positions and spots

| what | recorded | ours | verdict |
|---|---|---|---|
| Positions (5x5 south-west tile, template) | middle (2266,3073), south (2266,3062), east (2276,3071), west (2256,3071) | the same, as local tiles (26,33) (26,22) (36,31) (16,31) in `zulrah.constant` | match |
| Surfacing angle per position | middle 0, south 1024, east 416, west 1632, 20/20 surfacings of 3259 fight 0, 41/41 overall | `~zulrah_pos_angle`, sent as `face_angle` on the surfacing | match |
| Where clouds and eggs land | nine fixed spots, 281/281 throws: (2263,3070) (2263,3073) (2263,3076) (2266,3069) (2269,3069) (2272,3069) (2273,3072) (2273,3075) (2273,3078) | `~zulrah_spot(1..9)`; a cloud's 3x3 centred on its spot, a snakeling on the spot | match |
| A throw's source tile | the position's middle tile stepped once along the throw's heading: 265/274; the other 9 one heading behind, each the first throw after a spit run | precomputed per step in the dbrow (`src`), always the throw's own heading | 265/274; the lag is Open |
| A throw's angle | the angle from the middle tile to the spot: 274/274 | precomputed per step (`angle`) | match |

## Rotations

Four rotations, each a fixed list of phases. Phase 1 is the opener (green, middle, four
cloud barrages) and is shared. "At the start of the fight, Zulrah will be in one of four
possible rotations, chosen at random" (wiki): `~zulrah_begin_fight` rolls 1..4.

What is recorded: rotation 4 whole, twice (3259 fight 0 phases 1-13 and fight 1 phases
1-11); rotation 2's phases 2-7 and the start of 8 (3259 [8686..8845]); and the second
surfacing of every rotation (819: crimson middle 6 times, the first tail at +3 each;
tanzanite east 5 times, eggs at +3/+6; green east once, a spit at +3). Every other row is
the wiki's (Zulrah/Strategies), with snakeling and cloud spots from the old Near-Reality
table where the wiki names none: **UNVERIFIED** until a recording reaches them.

Steps are `spots` for clouds (first orb + second orb) and eggs, `attack N style` for a
spit run (`rng` green ranged, `mix` tanzanite 1-in-4 ranged, `jadr`/`jadm` alternating
from ranged/magic), `tail`; in brackets the ticks to the next step, the last one to the
dive. A tail's own gap is computed (its whip plus two, plus one more after the last).

Rotation 1 (recorded: phases 1-2)

| phase | surfaces | does |
|---|---|---|
| 1 | green middle | cloud s5+s6 (3); cloud s1+s4 (3); cloud s7+s8 (3); cloud s3+s2 (4) |
| 2 | crimson middle | tail; tail |
| 3 | tanzanite middle | attack 4 mix (3) |
| 4 | green south | attack 5 rng (3); egg s3 (3); egg s2 (3); cloud s1+s4 (3); cloud s6+s7 (3); egg s8 (3); egg s9 (4) |
| 5 | crimson middle | tail; tail |
| 6 | tanzanite west | attack 5 mix (3) |
| 7 | green south | cloud s5+s6 (3); cloud s1+s4 (3); cloud s2+s3 (4); egg s6 (3); egg s8 (3); egg s9 (3); egg s7 (4) |
| 8 | tanzanite south | attack 5 mix (3); egg s1 (3); cloud s4+s5 (3); egg s3 (3); cloud s6+s7 (3); egg s2 (4) |
| 9 | green west | attack 10 jadr (3); cloud s1+s4 (3); cloud s5+s6 (3); cloud s2+s3 (3); cloud s7+s8 (4) |
| 10 | crimson middle | tail; tail |
| 11 | green middle | attack 5 rng (4); cloud s5+s6 (3); cloud s1+s4 (3); cloud s7+s8 (3); cloud s3+s2 (4) |

Rotation 2 (recorded: phases 1-7 and the first two spits of 8, 3259 [8686..8845])

| phase | surfaces | does | recorded |
|---|---|---|---|
| 1 | green middle | the opener | 53/53 |
| 2 | crimson middle | tail; tail | [8686] TT at +3, +10 |
| 3 | tanzanite middle | attack 4 mix (3) | [8708] mmmm |
| 4 | green west | cloud s7+s6 (3); cloud s8+s9 (3); cloud s5+s4 (4); egg s4 (3); egg s1 (3); egg s2 (3); egg s3 (4) | [8726] CCCEEEE at 3,6,9,13,16,19,22, dive 26 |
| 5 | tanzanite south | attack 5 mix (3); egg s3 (3); egg s2 (3); cloud s1+s4 (3); cloud s6+s7 (3); egg s8 (3); egg s9 (4) | [8755] rrmmmEECCEE, dive 37 |
| 6 | crimson middle | tail; tail | [8795] TT at +3, +11 |
| 7 | green east | attack 5 rng (3) | [8817] rrrrr, dive 18 |
| 8 | tanzanite south | attack 5 mix (3); egg s1 (3); cloud s4+s5 (3); egg s3 (3); cloud s6+s7 (3); egg s2 (4) | [8838] mm, then the recording ends |
| 9 | green west | attack 10 jadr (3); cloud s1+s4 (3); cloud s5+s6 (3); cloud s2+s3 (3); cloud s7+s8 (4) | wiki |
| 10 | crimson middle | tail; tail | wiki |
| 11 | green middle | attack 5 rng (4); cloud s5+s6 (3); cloud s1+s4 (3); cloud s7+s8 (3); cloud s3+s2 (4) | wiki |

Rotation 3 (recorded: phases 1-2, 819 [11575])

| phase | surfaces | does |
|---|---|---|
| 1 | green middle | the opener |
| 2 | green east | attack 5 rng (3); egg s9 (3); egg s8 (3); egg s7 (4) |
| 3 | crimson middle | cloud s9+s8 (3); egg s7 (3); cloud s6+s5 (3); egg s4 (3); cloud s1+s2 (3); egg s3 (4); tail; tail |
| 4 | tanzanite west | attack 5 mix (3) |
| 5 | green south | attack 5 rng (3) |
| 6 | tanzanite east | attack 5 mix (3) |
| 7 | green middle | cloud s9+s8 (3); cloud s7+s6 (3); cloud s5+s4 (4); egg s1 (3); egg s3 (3); egg s2 (4) |
| 8 | green west | attack 5 rng (3) |
| 9 | tanzanite middle | attack 5 mix (3); cloud s3+s2 (3); cloud s1+s4 (4); egg s5 (3); egg s6 (3); egg s7 (4) |
| 10 | green east | attack 10 jadm (3) |
| 11 | tanzanite middle | egg s3 (3); egg s2 (3); egg s8 (3); egg s9 (4) |
| 12 | green middle | attack 5 rng (4); cloud s5+s6 (3); cloud s1+s4 (3); cloud s7+s8 (3); cloud s3+s2 (4) |

Rotation 4 (recorded whole, 3259 fight 0 [8318..8686] and fight 1 [9088..9382])

| phase | surfaces | does | recorded (fight 0 / fight 1) |
|---|---|---|---|
| 1 | green middle | the opener | CCCC at 13,16,19,22 (the camera-locked first boarding) / 11,14,17,20 |
| 2 | tanzanite east | egg s6 (3); egg s9 (3); egg s8 (3); egg s7 (4); attack 6 mix (3) | EEEErmrmmm / EEEEmmmrrm |
| 3 | green south | attack 4 rng (3); cloud s1+s5 (3); cloud s4+s6 (4) | rrrCC / rrCC (a spit spent out of reach) |
| 4 | tanzanite west | egg s3 (3); egg s2 (3); egg s4 (3); egg s5 (4); attack 4 mix (3) | EEEEmmmm / EEEErmmm |
| 5 | crimson middle | tail; tail; cloud s1+s5 (3); cloud s4+s6 (4) | TTCC / TTCC |
| 6 | green east | attack 4 rng (3) | rrrr / rrrr |
| 7 | green south | egg s3, s2, s1, s7, s8 (3 apart); egg s9 (4); cloud s8+s9 (3); cloud s6+s7 (3); cloud s5+s4 (4) | EEEEEECCC / EEEEEECCC |
| 8 | tanzanite west | attack 5 mix (3); egg s3 (3); egg s2 (3); egg s1 (3); egg s4 (4) | rmmmmEEEE / mrmrrEEEE |
| 9 | green middle | attack 5 rng (3) | rrrrr / rrrrr |
| 10 | tanzanite middle | attack 4 mix (3); cloud s2+s4 (3); cloud s1+s3 (3); cloud s5+s6 (4) | mmmmCCC / rrmmCCC |
| 11 | green east | attack 8 jadm (3) | mrmrmrmr / mrmr, killed |
| 12 | tanzanite middle | egg s3 (3); egg s2 (3); egg s8 (3); egg s9 (4) | EEEE |
| 13 | green middle | attack 5 rng (4); the opener's four barrages | rrrrrCCCC |

`::zulrahrun` (zulrah_selftest.rs2) pins the table: every phase present, the opener,
rotation 4 as recorded, the phase-2 tells, every rotation ending on the opener's phase,
every throw leaving from its heading's tile, the damage cap. It passes (7 checks), from
the world selftest too.

## The long fight

| rule | recorded | ours | verdict |
|---|---|---|---|
| A rotation's last phase is the next rotation's phase 1 | 3259 fight 0: phase 13 [8651] green middle rrrrr + the opener's four barrages = rotation 4's last; the next surfacing [8686] is crimson middle (phase 2 of rotation 1 or 2), then [8708] tanzanite middle, then [8726] green WEST = rotation 2 | `~zulrah_next_phase`: past a rotation's last phase it rolls a new rotation and continues at that rotation's phase 2 | match (1/1 wrap) |
| Which rotation comes next | 1 wrap: 4 -> 2 | uniform 1..4, the same one allowed ("it is possible to be the same rotation as the previous one", wiki) | wiki rule; the one recorded wrap is consistent |
| The phases after a wrap | rotation 2 phases 2-7 and the start of 8 exactly as the table: 7/7 phases | the table | match |
| How long the fight can go | fight 0 ran 529 ticks (20 surfacings) with the table never deviating | the runner wraps forever (rotation 4 -> 2 in the test, 20 surfacings) | match |

"All rotations after the first one have a chance for Zulrah to randomly be in a phase
different from the expected phase. There is also a chance for Zulrah to enter a form where
it will randomly switch between mage and ranged attacks" (wiki, Zulrah/Strategies; Zulrah:
"if the fight takes a long time (3+ minutes), it is possible for Zulrah to be in its green
form and attack unpredictably"). Neither is in any recording: the one post-wrap rotation
(3259, minutes 3 to 5 of the fight) followed the table for 7 phases, every green spit run
after minute 3 was all ranged ([8651] rrrrr, [8817] rrrrr) and the wiki gives no rate or
shape. **Not implemented: Open.**

## Timing

| event | recorded | ours | verdict |
|---|---|---|---|
| The rise | `[add]` green in the middle, spawnangle south, `snakeboss_spawn`, the tick after the arrival box is continued: 53/53 | `~zulrah_begin_fight` from the session on that tick (`npc_add`, `snakeboss_spawn`) | match |
| The opener's first barrage | rise +11: 52/53; +13 the one camera-locked first boarding (3259 [8331]) | +11 | match (the camera-locked +13 is Open) |
| Barrage cadence | 3 apart within a run of throws, 4 when a run of a different kind follows | dbrow gaps from the same rule | match |
| Dive after the last throw / spit | throw +4: 28/28; spit +3: 11/11; tail: whip + 2 + 1 | the dbrow gaps; the tail's computed | match |
| Surfacing after the dive | sink +3: 41/41 | `^zulrah_dive_ticks` 3 | match |
| First action after surfacing | surfacing +3: 41/41 | `^zulrah_phase_first` 3 | match |
| Hits on Zulrah while it is down | none from the sink to sink +5 in 31 dives; a hit in flight lands at sink +6 (20 of them) | `[ai_queue2]` re-queues a hit due in S..S+5 to S+6 | match |
| Landings while it is down | clouds and eggs due during the dive land when it is back up (3259 [8350], [8410], [8469]) | `~zulrah_land_due` holds them | match |
| The player's own attack at the dive | stops on the sink tick; no player attack S..S+5 in 31 dives | `[queue,zulrah_dive_stop]` -> `p_stopaction` on S | match |
| Our phase offsets, rotation 4 then 2 | 3259 fight 0, below | test run, below | match but for spits spent out of reach (position) and random rolls |

Per phase, recorded (3259 fight 0, offsets from the add) against ours (the test, which
stands where the recorder stood ten ticks into each phase):

| # | rotation:phase | recorded | ours | verdict |
|---|---|---|---|---|
| 1 | 4:1 | CCCC 13,16,19,22, dive 26 | CCCC 11,14,17,20, dive 24 | camera-locked opener (fight 1: 11..20, dive 24) |
| 2 | 4:2 | +29 EEEErmrmmm 3..31, dive 34 | +27 EEEEmrmmmm 3..31, dive 34 | match (styles roll) |
| 3 | 4:3 | +66 rrrCC 4,7,10,13,16, dive 20 | +64 rrrCC 4,7,10,13,16, dive 20 | match (the +3 attempt spent in both) |
| 4 | 4:4 | +89 EEEEmmmm, dive 28 | +87 EEEErmmm, dive 28 | match |
| 5 | 4:5 | +120 TTCC 3,10,18,21, dive 25 | +118 TTCC 3,10,18,21, dive 25 | match |
| 6 | 4:6 | +148 rrrr 3..12, dive 15 | +146 rr 5,8, dive 11 | two spits spent out of reach (the test player was 8+ tiles off at +3/+4) |
| 7 | 4:7 | +166 EEEEEECCC, dive 32 | +160 EEEEEECCC, dive 32 | match |
| 8 | 4:8 | +201 rmmmmEEEE, dive 31 | +195 rmmrEEEE (first spit +4), dive 29 | one spit spent out of reach |
| 9 | 4:9 | +235 rrrrr, dive 18 | +227 rrrrr, dive 18 | match |
| 10 | 4:10 | +256 mmmmCCC, dive 25 | +248 mrmmCCC, dive 25 | match |
| 11 | 4:11 | +284 mrmrmrmr, dive 27 | +276 mrmrmrmr, dive 27 | match |
| 12 | 4:12 | +314 EEEE, dive 16 | +306 EEEE, dive 16 | match |
| 13 | 4:13 | +333 rrrrrCCCC, dive 32 | +325 rrrrrCCCC, dive 32 | match |
| 14 | 2:2 | +368 TT 3,10, dive 19 | +360 TT 3,11, dive 20 | the tail's side rolls (right: next at +7, left: +8) |
| 15 | 2:3 | +390 mmmm, dive 15 | +383 mmmr, dive 15 | match |
| 16 | 2:4 | +408 CCCEEEE 3,6,9,13..22, dive 26 | +401 the same | match |
| 17 | 2:5 | +437 rrmmmEECCEE, dive 37 | +430 rmmmmEECCEE, dive 37 | match |
| 18 | 2:6 | +477 TT 3,11, dive 19 | +470 TT 3,11, dive 19 | match |
| 19 | 2:7 | +499 rrrrr, dive 18 | +492 rrrrr, dive 18 | match |
| 20 | 2:8 | +520 mm (the recording ends) | +513 rmmmm, killed | match as far as recorded |

## Spits

| what | recorded | ours | verdict |
|---|---|---|---|
| A spit's throw tick | `snakeboss_attack_acidx1`; `map_projanim_v2` `snakeboss_orb` (ranged) / `snakeboss_fireball` (magic) 40/90, angle 0, progress 0, raw height 370 -> 50, homing on the player, from the middle tile stepped along the heading; `sound_area` waterwave_/firewave_cast_and_fire range 4 on the source; `synth_sound` waterwave_/firewave_hit delay 90: 95/95 | `~zulrah_spit` the same, `projanim_pl_raw`; 60 spits in the test, 32 ranged 28 magic, every one 40/90 | match |
| The splat | throw +3: 112 of 115 (rsprox_hit_causes, `proj:` exact) | throw +3, 60/60 (queued for 2 from the softtimer: the run before this landed all 60 at +4) | match (fixed in this pass) |
| The player's block animation for a spit | none, 0 of 95 | none | match |
| Cadence in a run | 3 ticks, the face on the player from the run's first attempt | `^zulrah_attack_ticks` 3 | match |
| Out of reach | an attempt with the player beyond the reach spends the spit and is retried next tick (3259 [8387]: 8 tiles from green south; [9155]/[9156]: 10 tiles, two spent) | `~zulrah_in_reach`, footprint distance > 7 spends it | match (the threshold is fitted: spat at 6, 8/8; spent at 8, 2/2) |
| No line of sight | the attempt is retried without being spent (3259 [9346], the west pillar, the run a tick longer) | `npc_approached` false: retried, not spent | match |
| Tanzanite's style | ranged 13 of 49 spits | 1 in 4 (`^zulrah_mix_ranged_in`) | match |
| Jad runs | green east alternates from magic (rotation 4 phase 11: mrmrmrmr, 2/2); rotation 1/2's west run from ranged (wiki) | `jadm` / `jadr` modes | match (jadr unverified) |
| Envenom | "The ranged and magic attacks will envenom the player unless they miss, even if blocked by a protection prayer" (wiki); the recorder's first venom tick follows a 0 splat (3259) | `queue(zulrah_envenom)` with the splat on a non-miss | match |
| The face | `[face_pathingentity]` on the player on the run's first attempt, one per run (22 runs, 22 faces); reset 3 ticks after the last spit (14/14) | `npc_attackplayer` on the first attempt; reset at +3 (13/13 runs) | the reset matches; ours re-sends the face (Open: the face re-send rule) |

## Clouds

| what | recorded | ours | verdict |
|---|---|---|---|
| A barrage's throw | `face_angle` toward the first spot, `snakeboss_attack_acidx1`, two `snakeboss_double_orb` 40/90 angle 0 and 45/120 angle 10, raw 370 -> 20 (226/226); `snail_spit` range 7 on the source | `~zulrah_throw_clouds`; 23 barrages in the test, 23 + 23 projectiles at 40/90 and 45/120 | match |
| Landing | the first orb +3, the second +4 | `~zulrah_land_add` slots | match |
| The cloud | `snakeboss_poisoncloud` shape 10 on spot - (1,1), a random rotation (0: 102, 1: 113, 2: 92, 3: 77 of 384), `smokepuff` range 4 on its centre | `loc_add` random(4), `smokepuff` range 4 | match |
| Its life | `loc_del` 30 ticks later with its own rotation: 68/68 that were not cleared by a death | `loc_add` duration 30 | match |
| The bite | every tick a cloud covers the tile the player stood on as the tick began: one splat 1..4 (437/437 such ticks), hitsplat 5; 0/7 the tick he walked in, 12/12 the tick he walked out | `~zulrah_cloud_tick`, `hitsplat_teal` 1..4 | match |
| A landing on the player | bites once more, 4 or 5, before the tick's own bite: 22 of 51 landings | 22 in 51, 4..5 | match |
| Values | 1: 116, 2: 114, 3: 117, 4: 143, 5: 10 | 1..4 uniform, 4..5 on a landing | match |
| Cleared at the death | all nine spots get `invisible_type8_blocking` added and deleted on the death's tick, a cloud there or not: 52/52 | `~zulrah_clear_clouds` | match |

## Eggs and snakelings

| what | recorded | ours | verdict |
|---|---|---|---|
| The egg | `face_angle`, `snakeboss_attack_acidx1`, `snakeboss_egg` 40/120 angle 0, raw 370 -> 20 (55/55), `snail_spit2` range 7; hatches +4 | `~zulrah_throw_egg`; 30 in the test, all 40/120 | match |
| The hatch | `[add]` on the spot, `snakeboss_pet_spawn`, `[face_pathingentity]` on the player, `snake_attack` range 3 on the tile | `~zulrah_hatch` | match |
| The kind | magic 22, melee 26 of 48 | 1 in 2 | match |
| Spawn angle | south 23, north 16, east 7, west 4 (varies per tile and hatch) | always south (the type's) | Open (no visible effect: the face on the player goes with the add) |
| First swing | +3 at the earliest (15/48), later when it had to crawl | npc attack delay 3 from the hatch: +3 in 17 of 30 | match |
| Swing cadence | 3 ticks: 103 of 107 gaps | attackrate 3: 215 of 224 gaps | match |
| Movement | a tile a tick toward the player, never a wander (250 walks) | `defaultmode none`, chase only; gaps 1 (`npc_movement_audit paths`) | match |
| Chase range | never gives the player up: 48/48 kept the face on him until their death | `maxrange 30` (the generated 7 dropped 7 of 31 on the tick after the hatch: fixed in this pass) | match |
| Melee swing | `snakeboss_pet_attack`, `seasnake_attack` synth, block + splat on the swing tick, up to 15 | `[ai_opplayer2,snakeboss_minion_melee]`; 98/98 swings with the synth | match |
| Magic swing | `snakeboss_pet_attack`, `zap` range 3 on its tile, `snakeboss_minion_spell` 10/30 angle 0 progress 92 raw 60 -> 65 (114/114), block delayed 30, splat +1, up to 13 | `[ai_opplayer2,snakeboss_minion_magic]`; 154/154 spells 10/30, every splat +1 | match |
| Its end of life | the face reset and the menu off on +50, the sequence then (3/3 standing) or after the step it was taking (2/2 that crawled on +49: +52), the body gone 2 ticks after the sequence: 5 aged out | `[ai_queue4]`: `npc_facereset`, `npc_arrivedelay`, `npc_die` (the face on +50 29/29; the sequence on +50 20, +52 9) | match but the menu (Open: the menu rides with the sequence) |
| Killed | the splat (16, 1) and headbar at K; at K+1 the face reset, the menu off, the death sequence (K+1 standing: 21, K+2: 20, K+3: 2 after a step); `seasnake_death` with the sequence; gone 2 after the sequence: 43/43 | the engine's death; the test shoots one: splat and headbar at K, face reset and the sequence K+1, gone K+3 | match but the menu: ours at K, recorded K+1 43/43 (Open: engine) |
| The defend animation on a player's shot | `snakeboss_pet_defend` on the attack tick, delay = flight (3259 [8384] delay 52, the one shot the recorder made himself) | the same, delay 51 | match |
| Zulrah dies | an egg hatching on the death tick is added already dying: `[add]` with `[hit] 17/1`, headbar 0, `snakeboss_pet_spawn`, `[transformation] snakeboss_minion_dying`, `enabled_ops 0b11111`, `level_change 0`; D+1 the death sequence and the menu off; `seasnake_death` synth on D+1; gone D+3 (819, 2/2) | `~zulrah_kill_snakelings` after the landings: the type change and a 1 (`hitsplat_damage_other`) | match but the sounds (Open: an unowned hit's sounds) |
| "Kill 3 Snakelings simultaneously" | (combat achievement, wiki) | counted per tick in `~zulrah_snakeling_died` | wiki |

## The tail

| what | recorded | ours | verdict |
|---|---|---|---|
| The swing | `face_angle` toward the player's tile (7/9), `snakeboss_attack_tail_right` or `_left` | `~zulrah_tail_step`, the side rolled | match |
| The whip | `whip` area sound range 2 on the struck tile, swing +5 (right) or +6 (left): 8/8 | `^zulrah_tail_whip_right/left` | match |
| From behind a pillar | the swing goes for the arm's mouth instead (4 times from (2263,3073)/(2264,3072)) | the pillar tiles' special case | match |
| Next swing / the dive | the whip + 2; the dive one more after the last | the computed gap | match |
| A hit | never: the recorder was not struck in 13 swings | "dealt 20-30 damage and be stunned for several seconds" (wiki): typeless 20..30, `p_stun` 5 ticks, if the player is within a tile of the struck tile | wiki (the damage, the stun length and the reach are Open) |
| A shove | none in any source | none (the Near-Reality force-move is gone) | match |

## Damage

Every splat on the recorder in all three sessions (`zstats.py`; protected = the matching
protection prayer up on the attack's tick). Grid Master recorders take about 10% less, so
the recorded maxima are low.

| attack | unprotected: n, max, values | protected | wiki max | ours | verdict |
|---|---|---|---|---|---|
| Zulrah magic spit | 7, max 36: 4 7 19 25 27 36 36 | 19/19 zero | 41 | roll 0..41 on a hit, 0 when protected; the test's unprotected spits reached 41 | match |
| Zulrah ranged spit | 13, max 19 (4 zeros) | 35/35 zero | 41 | the same | match |
| Snakeling melee | 26, max 15 (4 zeros) | 6/6 zero | 15 | roll 0..15 | match |
| Snakeling magic | 54, max 13 (39 zeros) | 39/39 zero | 13 | roll 0..13, 0 when protected | match |
| Cloud | 1..4 a tick, 4..5 on a landing | (no prayer) | (none) | the same | match |
| Venom | 6, then +2 every 30 ticks to 20 (6: 21, 8..18: 2 each, 20: 10; 21 of 23 gaps 30), hitsplat 5 | `venom.rs2`: the same ramp; the splat was `hitsplat_poison` (68), now `hitsplat_teal` (5) | match (fixed in this pass, shared file) |
| Tail | never recorded | 20..30 typeless | wiki |
| Damage Zulrah takes | capped at 50, above it 45..50 (wiki; Mod Ash 22 March 2017) | `~zulrah_cap` on the three boss types only (`::zulrahrun` check 7) | wiki: the GM recorder hit it for up to 221 |

## Hit causes

`tools/rsprox/rsprox_hit_causes.py --npc '^snakeboss_(boss|minion)'` on the raw transcripts,
every splat on the recorder given a cause:

| cause | recorded | ours (test) | verdict |
|---|---|---|---|
| `type:hitsplat_teal` (clouds, venom) | 553, 1..20 | 93, 1..20 (before the venom fix: 75 clouds, and the venom as `type:poison`, 16) | match |
| `proj:snakeboss_minion_spell` | 102 + 6 a tick off | 156 + 45 a tick off; by the tick log every one of the 154 spells' splats is +1, the tool pairs spells landing together | match |
| `proj:snakeboss_orb` | 58 + 3 a tick off | 48 + 2 (before the spit fix 24 + 21 a tick off) | match |
| `proj:snakeboss_fireball` | 54 | 49 + 2 (before the spit fix 8 + 25 a tick off) | match |
| `npc:snakeboss_minion_melee:snakeboss_pet_attack@0` | 17 | 8 | match |
| `spot:highlvlalchemy_casting` (4), `sweep:snakeboss_boss_magic@1` (1), `npc:...defend` (1) | 6 | | each a melee snakeling's swing on that tick (zdmg.py `melee=1`): the tool attributes swings by an old face block format |
| `hit:snakeboss_boss_*:content` | 21 splats on Zulrah | | the recorder's own hits the tool could not pair with a projectile (blowpipe specials, the killing blows) |
| `npc:snakeboss_boss_*:snakeboss_attack_tail_left@6` | 0 | 1 (24) | the test player stood on the struck tile: the wiki's hit |
| `sweep:snakeboss_boss_magic@0` | 0 | 3 (0..7) | a cloud bite or venom tick on a surfacing tick (the jump is the "sweep") |
| `none` | 0 | 0 | zero unexplained |

## Npc info, per block kind

`npc_block_inventory.py --values` over the three transcripts and the test's tick log. The
tool counts ours under the type BEFORE a transformation and does not split our `npc_face`
rows into the transcriber's `face_angle` / `face_pathingentity`; the "ours" column below
counts our rows directly.

Zulrah (all three forms):

| block | recorded | ours (one fight, 20 surfacings) | verdict |
|---|---|---|---|
| add | spawnangle south, `snakeboss_spawn`: 53/53 | 1/1 | match |
| face_angle on a surfacing | the position's angle, 41/41 | 19/19 | match |
| face_angle on a throw | the angle to the spot, 274/274 throws | every throw | match |
| face_angle on a tail | toward the player's tile, 7/9 | every swing | match |
| face_pathingentity on the player | once per spit run: 22 runs, 22 blocks | 13 runs, 39 blocks: one per run plus a re-send the next tick and while the player walks | Open: the face re-send rule |
| face_pathingentity reset | spit run's last + 3: 14/14; with the death | 13/13 | match |
| sequence | `snakeboss_spawn`, `_emergefast` and `_sinkfast` per dive (41/41), `_attack_acidx1` per throw and spit, `_attack_tail_left/right`, `snakeboss_defend` delayed to the player's landing, `snakeboss_death` | the same | match |
| transformation | on every surfacing, the same form included: 41/41 | 19/19 | match |
| enabled_ops | 0b11111 on every surfacing (41/41), 0b00000 with the death (52/52) | 19/19, 1/1 | match |
| level_change | 725 on every surfacing: 41/41 | 725, 19/19 | match |
| hit / headbar | the player's hits; headbar 20 (`healthbar_standard_120`) | headbar 20 | match |
| spotanim | `failedspell_impact`, `fireblast_impact`, `league05_guardian_attack_magic_impactanim_01`: the recorder's spells and his Grid Master guardian | none | the recorder's kit |
| multinpc | not a multinpc (`multinpc_stance_audit.py`: only the Zul-Andra priests are) | | n/a |

Snakelings:

| block | recorded | ours | verdict |
|---|---|---|---|
| add | `snakeboss_pet_spawn` + face on the player: 48/48 | 30/30 | match |
| spawnangle | varies (above) | south | Open |
| face_pathingentity | exactly two per life: the player at the add, reset at the death (48/48) | the add's, a re-send whenever the angle to the player turns, the reset at the death | Open: the face re-send rule |
| sequence | spawn, `snakeboss_pet_attack`, `_defend` (a player's shot), `_death` | the same | match |
| enabled_ops | 0b00000 with the death, K+1 | the aged: with the sequence; killed: K | Open (two rows above) |
| hit / headbar | 16/1 and headbar 0 (killing) | the same (`hitsplat_damage` 1, headbar id 0) | match |
| `snakeboss_minion_dying` add | hit 17/1, headbar, spawn seq, transformation, enabled_ops 0b11111, level 0 (2/2) | the type change and the hit on the same tick | match by construction; not reached by the test |

## Movement

`npc_movement_audit.py defaults minigames/minigame_zulrah`: the three boss forms
`defaultmode none`, `nomove`; the snakelings `defaultmode none`, wanderrange 0, chase only;
`snakeboss_minion_dying` `nomove`. `paths`: recorded boss moves are telejumps only (30);
ours are its dive jumps. Snakelings step a tile a tick in both (gap 1: recorded 111/126
magic, 74/81 melee; ours the same shape). The Zul-Andra npcs that wander belong to the
session ([README.md](README.md)).

## Death

| what | recorded | ours | verdict |
|---|---|---|---|
| The killing hit's tick (D) | `snakeboss_death` with the splat (50/52; 2 a tick later), `enabled_ops 0b00000`, the face reset, `seasnake_hit` + `seasnake_death` synths to the hitter: 52/52 | `npc_setdeathanimnow`, death_delay 5 | match |
| What it was about to throw | still leaves on D (face, acidx1 under the death sequence, projectiles, sound) and lands nowhere: 3 of the 52 | `~zulrah_last_throw` | match |
| Clouds | the nine spots cleared on D: 52/52 | `~zulrah_clear_clouds` | match |
| Snakelings | dying on D (above) | `~zulrah_kill_snakelings` | match |
| A spit already in flight | lands after the death ("If Zulrah dies just as it launches an attack, the attack can still deal damage", wiki) | the player's queued hit lands | match |
| D+6 | Zulrah deleted; the loot, `failedspell_impact` and the exit loc (the session's) | `[ai_queue3]` -> `@zulrah_died` at D+6 | match |

## The test

`test/bosses/zulrah.lua`: one fight, rotation 4 then rotation 2 from its phase 2
(`::zulrahnext 2` stands in for the roll), entered through the quick-board with the
arrival box continued. The player walks to where the recorder stood ten ticks into each
phase, prays against the form, is healed by cheat, shoots one snakeling from rise + 175,
and after rise + 525 takes Zulrah to 1 and kills it with a bow.

```
TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
  ./src/<objdir>/torirsserver --scriptrun test/bosses/zulrah.lua --bots 1 --name z0 \
  --ticks 2400 --session <dir> --fixture test/bosses/fixtures/fresh_lumbridge.ini
```

Final run: `SCRIPTRUN PASS: 566 ticks` with zulrah.ticklog, rise, forms, phases (20
surfacings), snakeling (shot, `ok`), killed (tick 556) and handoff (the session's kill
count 1). The world selftest's Zulrah stanzas (`::zulrahrun` and the live opening) pass
after waiting for the recorded rise instead of assuming it.

## Open

1. **The face re-send rule: settled, a revision change.** Measured across revisions with
   `tools/rsprox/rsprox_face_resend.py` (a face on the recorder, the angle from the npc's
   centre to his last-tick tile turned, a face block re-sent or not): rev 234/235 (index
   3259, 819, 3324) 2 re-sent of 8,184 turns, for EVERY npc type in them (the Fight Caves
   swarm, nechryaels, the Hunllef, kalphites, snakelings); rev 236 (3909) 0/341, rev 237
   (1102) 0/40; rev 238 (4169) 26/26, rev 239 (4262) 2378/2429, rev 241 (4292) 891/892,
   rev 242 (4578) 310/310, (4346) 151/153. Jagex started re-sending between 237 and 238,
   so `npc_face_follow` is right for every wire this server speaks (239, 241, 242); the
   Zulrah recordings simply predate it. No change.
2. **The menu at a death: fixed in the engine.** `tools/rsprox/rsprox_death_ops.py`
   (the headbar's fill=0 as the kill): the menu goes at kill + 1, where the death
   animation also plays when nothing delays it, for every ordinary npc on every revision
   (index 4262 207/207; 3259 554 (+1,+1), 47 (+1,+2), 41 (+1,+3) as (menu, animation)),
   and at kill + 0 only when the animation goes with the blow (`npc_setdeathanimnow`,
   60 (0,0)). `npc_death_begin` hid it on the blow for every death; it now waits for the
   QUEUED step (kill + 1) unless the animation went with the blow, and a scripted
   `npc_die` still hides it on its own tick (a death transformation keeps its restore).
   The world selftest's goblin now checks the killing tick keeps the menu and the next
   tick hides it in one block (both wires). The aged-out snakeling hides its menu with
   `npc_setops(0)` on +50 beside the face reset, before its arrive delay (5/5).
3. **An unowned hit's sounds** (the snakelings dying with Zulrah): ours sends
   `sound_area id=-1 range 10` for the hit and the death sound as an area sound; Jagex
   sends no hit sound and `seasnake_death` as a synth on D+1 (819, 2/2). Engine rule
   (`hit_has_player_dealer`).
4. **The wiki's long-fight deviations: built, at CHOSEN rates (owner, 2026-11-17).** After
   the first rotation, a phase change detours 1 time in 8 (`^zulrah_detour_in`) to a middle
   phase (2 .. last - 1) of any rotation, then the table resumes where it would have been;
   a green spit run goes wild 1 time in 8 (`^zulrah_wild_in`), each spit then magic or
   ranged at even odds. Neither rate is sourced: no recording shows a deviation (the one
   post-wrap rotation, 3259 [8686..8847], kept the table 7/7) and the wiki gives none.
   `::zulrahtable` turns both off (the replay test uses it); `::zulrahrun` check 8 walks
   400 phase changes (none in rotation 1, none with the table forced, each detour a middle
   phase that returns to the table).
5. **Unrecorded rows of the table**: rotations 1 and 3 after phase 2, rotation 2 after
   phase 8's second spit, and every egg/cloud spot in them (Near-Reality's where the wiki
   names none). The rotation-1/2 Jad run's ranged-first start is the wiki's.
6. **The throw-source lag**: 9 of 274 recorded throws left from the previous heading's
   tile, each the first throw after a spit run; ours always use the throw's own heading.
7. **The camera-locked first boarding** opens at +13 (3259 [8331]); ours always +11.
8. **The tail's hit**: never recorded. Damage 20..30 and "avoided by moving two tiles
   away" (wiki, Zulrah) as built: it hits within one tile of its target, so two tiles
   off is clear. The stun is 5 ticks (owner's call, 2026-11-17); the wiki says only
   "several seconds" and the Stun (status) page does not list Zulrah. UNVERIFIED.
9. **The damage cap** (50, then 45..50) is Jagex's own statement (Mod Ash, 22 March 2017, quoted by the wiki), built as worded. Every recording is Grid Master and hits Zulrah for up to 221 (827 hits, 3259/819/3324), so none can show it; a normal-world kill would.
10. **Snakeling spawn angle**: recorded varies (south 23, north 16, east 7, west 4); ours
    south. The rule (toward the player? the spot's side?) is not worked out.
11. **The reach threshold** for a spit (footprint distance 7) is fitted between 6 (spat
    at) and 8 (spent); 7 itself is unobserved.
12. **`snakeboss_minion_dying` added on the death tick** is built (the hatch lands before
    the death's cleanup) but no test reaches it.
13. `tools/zulrah_import_nr.py` (the Near-Reality import) still writes `zulrah_phase.dbrow`
    from the old table; it should be deleted. `gen_dbrow.py` here is the generator.
14. Varps: the fight's new state reuses ledger-bound names already allocated to this
    content (`varp7599..7602_zulrah_pend_cloud0..3` hold the four landings,
    `varp7603..7606_zulrah_pend_orb0..3` the heading, the face-reset tick, the dive clock
    and the face latch, `varp7381_zulrah_jad_next` the forced next rotation), because the
    ledger binds names to ids.
