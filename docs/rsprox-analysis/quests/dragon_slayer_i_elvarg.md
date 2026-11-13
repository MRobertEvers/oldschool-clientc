# Dragon Slayer I: Elvarg, the combat audit (RSProx index 4558)

The boss fight of Dragon Slayer I, audited against the one recording of it and
rebuilt to match. Scope: her npc config (`quests/quest_dragon/configs/quest_dragon.npc`
`[elvarg]`, `[elvarg_alive]`), her script (`quests/quest_dragon/scripts/elvarg.rs2`),
one line of the shared ranged/magic attack gate, and a focused test
(`test/combat/elvarg.lua`). Her death's quest half (the stage, the body locs, the head)
is `quest_dragon.rs2`'s and is cited, not audited, here. Written 2026-11-12.

## Sources

| what | count |
|---|---|
| Sessions on the index with Elvarg (`rsprox_index.py search npc 816 --any`; 8033, 6118 and 6349 return none, 817 is the cutscene form in the same session) | **1**: index 4558, rev 242.1, world 386, activity "Blast Furnace" (a normal world, not seasonal) |
| Fights in it | **1**: the player climbs the lair wall [5121], she breathes at [5123], dies at the splat [5167], the death seq [5168], gone [5173] |
| Transcripts used | default, `--bas`, `--all-players`, `--all-players --bas`, `--raw` |
| Recorded attacks | 11: 9 breaths, 2 claws |
| Recorded splats on the recorder from her | 10 (8 breath landings, 2 claws); one breath ([5123]) never splatted |
| Fireballs | 54 (6 per breath, every field fitted) |
| Other sightings | [5072]..[5077] (the player outside the wall, she at 2858,9634, no engagement at 14 tiles) |

The recorder: attack 58, defence 40, strength 42, hitpoints 61, ranged 70 (81 on a ranging
potion, [5114]), magic 50, prayer above 44 (Eagle Eye on). Kit ([5133] appearance): robin
hood hat, Ava's accumulator, necklace of fangs, adamant knives (rapid), Saradomin d'hide body
and chaps, anti-dragon shield, combat bracelet, blessed Saradomin boots. Prayers: Protect from
Melee + Eagle Eye to [5133], then Protect from Magic + Eagle Eye. Bonuses from the cache objs:
slash defence 96, magic defence 93.

One fight is one roll. Everything below that is a rule fitted to this fight says so, and
every count is out of what one fight can give.

## Action inventory (`rsprox_actions.py --from 5110 --to 5175`)

| tick | action | verdict |
|---|---|---|
| [5112] x2 | prayer21 (Eagle Eye) | the recorder's kit (prayer book, generic) |
| [5114] | 4-dose ranging potion | the recorder's kit (generic drink) |
| [5115] x2 | shark (the first refused, [5155] eaten) | the recorder's kit (generic eat) |
| [5116] x2 | `oploc1 dragon_slayer_qip_stalagtite_jump` | quest_dragon's climb (crandor.rs2), not this audit's |
| [5118] | 4-dose prayer restore | kit |
| [5126] x3 | `opnpc2` Elvarg (Attack) | **this audit**: the player's ranged attack on her (refused in ours before the fix, below) |
| [5133] | prayer13 (Protect from Magic) | kit; drives her damage table |
| [5167] | 3-dose prayer restore | kit |
| [5172] x2 | quick-prayer orb | kit |
| [5121] teleport 2845,9635 -> 2847,9635 | the climb's exactmove | quest_dragon's climb |

## What was wrong (ours before)

From the quest gate's run before this audit (`build/quest_gate/dragon/ticklog.tsv` 770..821,
a melee player) and the first run of the new test:

1. **Ranged and magic attacks on her were refused**: `~npc_is_attackable_opt`
   (`skill_combat/scripts/player/player_magic.rs2`) asks `npc_hasop(2)`, and the engine's
   `npc_hasop` reads the spawned multinpc SHELL (`elvarg`, 816, no ops), not the rung the
   player sees (`elvarg_alive`, op2 Attack). "You can't attack this npc." and no throw: 0
   throws in 400 ticks with the recorder's knives. Recorded: 17 throws.
2. **Two breaths, neither the recorded one.** LostCity's melee-range breath
   (`firebreath_attack` spotanim on her + `dragon_firebreath_middle_attack` (84), instant
   damage, no projectile) and its "AP" breath (seq 84, ONE `fireblast_travel` on LostCity's
   generic flight, start height 92 in old units = 368 raw, impact spotanim height 92). Recorded:
   one breath at every range, seq 81, six fireballs, two area sounds.
3. **No breath sounds**: neither `dragonslayer_dragonbreath` (3750) nor `fireblast_hit` (156).
4. **Silent claw**: the generated block's `attack_sound=null`; recorded synth 408 on 2 of 2.
5. **Double attacks**: an OP swing that rolled 1/8 into AP was followed by the AP breath on
   the next tick (ours 811 + 812), because the engine's AP bridge does not read the attack clock.
6. **Prayer drain 10% of the BASE level** (`stat_drain`); recorded 10% of what is left, rounded
   down (5 of 5 clean breaths).
7. **Death**: seq with no delay, the death sound as synths, no stomp sound, the menu off on the
   zero splat (a tick early), no west face, npc freed on K+3 (three ticks with neither her nor
   her body drawn).
8. **Headbar**: `healthbar_standard_60` (id 17) from the generated block; recorded id 0
   (`healthbar_standard_30`) on all 16 splats.
9. **Hunt**: the engine's `huntmode=aggressive` (no line of sight, no stage filter) with
   huntrange 7: she took players behind the lair's wall and re-took a slayer she is hidden from.
   Recorded: she engaged at 11 tiles.
10. **Switching**: LostCity's chain (1/2, 1/8, plus a guessed 1/4 fire in OP) with the
    engine's errand modes; no rule held her at range while the player attacked from there.

## What she does now (the model, fitted)

JagexAsh on Elvarg (twitter.com/JagexAsh/status/1756992041777561878, quoted in LostCity's
elvarg.rs2): **"AP -> OP: 1/2, subject to a line-of-walk check. OP -> AP: 1/8"**. AP = the breath
from range; OP = the claw beside the player. The roll is made after each attack of the mode
she is in. One rule is added, fitted to 4558: **while a player is attacking her from beyond her
claw, she is held in AP**. It explains three things the chain alone cannot:

| recorded | chain alone | chain + hold |
|---|---|---|
| [5127] she takes her 4th step although her clock was ready; [5128] she breathes from where she stood, 3 tiles off | she cannot stop short of melee in OP (probability 0) | his first throw at [5127] (his turn, after hers) holds her from [5128] |
| [5128]..[5152]: 7 breaths, no step, every breath rolling 1/2 to close | (1/2)^7 = 1/128 | held: the roll cannot take her out of AP |
| [5151] he steps aside and stops throwing; [5152] breath; [5153] she walks in | — | the hold lapses ([5150] his last working tick), [5152]'s roll goes OP (1/2), [5153] the step |

Beside him ([5156]..[5164]) the hold does not apply and the chain runs: claw [5156], breath
[5160] (OP -> AP, 1/8), claw [5164] (AP -> OP, 1/2). LostCity's "25% of OP swings are fire"
(2006 videos) is dropped: the chain alone gives a beside-the-player fight 20% breaths
(stationary AP share (1/8)/(1/8 + 1/2)); ours measured 0.27, 0.21, 0.17, 0.21, 0.21 over five
scriptrun melee fights (85 breaths in 402 attacks, 21%).

UNVERIFIED (no data): whether the hold is Jagex's own rule or a side effect of something else in
their AI; its reach for a spell (ours holds her for a tick after each landed spell hit, through
`[ai_queue1]`); her behaviour against a player she cannot path to who does not attack her.

### Where it runs (engine constraints found)

- `attackrange=11` (her breath reached 11 at [5123]). With it, the engine's reach test for a
  4x4 npc is a line of sight that FAILS with the player beside her (the ray meets her own
  footprint): CombatNpcTick returns early and the clock-owned `[ai_opplayer2]` never fired once
  she was beside the player (run 1: one breath, then nothing for 400 ticks).
- So her turn is her `[ai_timer]` (every tick): the hunt, the hold, the chase step and the
  attacks, on her own clock (npc var 1). The engine keeps her target, her face, her approach
  from beyond 11, and the fight's OPENING swing (`[ai_opplayer2]`), which is the one that
  passes the engine's single-way test: a test run showed a giant skeleton still claiming the
  player from outside the wall for 4 ticks after he climbed in, and her timer had breathed at
  him anyway.
- `nc_size(npc_type)` answers 0 for her shell; the melee-reach test uses her footprint 4.
- `npc_approach` steps a tile at once (one per tick, recorded walk speed).

## Npc info, block by block

Inventory (`npc_block_inventory.py --values`): no kind recorded that ours never sends.

| block | recorded (1 fight) | ours before | ours after (test run `elvarg_audit`, 2 fights) | verdict |
|---|---|---|---|---|
| `[add]` | 1, spawnangle north, `facing type=reset` on the add | engine | engine | engine-generic (face_add_latch) |
| `[sequence]` 81 dragon_firebreath_all_attack, delay 0 | 9 | 0 (84 instead) | 32 | **fixed** |
| `[sequence]` 84 dragon_firebreath_middle_attack | 0 | every breath | 0 | **fixed** |
| `[sequence]` 80 dragon_attack, delay 0 | 2 | yes | 50 | matched |
| `[sequence]` 89 dragon_block, delayed to the player's projectile (47 at 3 tiles, 37 at 1) | 15 | engine | delayed 42 at 2 tiles, 37 at 1 | matched (the delay is the player's knife flight; engine-generic). Ours logs an undelayed call first in the same tick; the last call is what the mask sends |
| `[sequence]` 4642 dragon_death_elvarg delay 7 | 1 ([5168]) | delay 0 | delay 7, K+1 | **fixed** |
| `[spotanim]` on her | 0 | `firebreath_attack` on the melee breath | 0 | **fixed** |
| `[facing] player=` | 9: engagement + re-sends on every tick the angle turned (her 4 steps, his step [5152], her step [5153], his step [5160]) | engine | engagement + re-sends (engine, turned angles) | engine-generic; matched where the geometry repeats (ours: her spawn is closer, see Open) |
| `[facing] type=reset` at the death seq | 1 ([5168]) | yes | 1 per death, K+1 | matched |
| `[facing] coord=` (2803,9637) a tick after the death seq (50 W, 1 N of her SW tile) | 1 ([5169]) | none | 1 per death, K+2 (2802,9638 from 2852,9637) | **fixed** |
| `[hit]` on her | 16 (14 damage, 2 block) | engine | engine | the recorder's kit (generic) |
| `[headbar]` id 0, fill /30 | 16 | id 17, /60 | id 0, /30 | **fixed** (`healthbar=healthbar_standard_30`) |
| `[enabled_ops]` 0b00000 with the death seq | 1 ([5168]) | on the zero splat (K) | K+1, with the seq | **fixed** |
| movement | walk, 1 tile a tick: 4 W [5124]..[5127], 1 NW [5153] | engine pursuit | timer chase, 1 tile a tick | matched (speed); per-tick pattern needs the recorded spawn (Open) |
| `[del]` | K+6 [5173], with the body loc | K+3 | K+6, with the body loc | **fixed** |
| say / transformation / level_change / bas / body_customisation / transparency / exact_move | none | none | none | matched |

Multinpc stance (`multinpc_stance_audit.py`): shell `elvarg` (size 4, dragon_ready,
dragon_walk) over rung `elvarg_alive` (size 1, no stances): DIFFERS by construction; the
client draws the shell's (deob `Statics.method4320`). Not photographed in this audit (Open).

## The breath (54 fireballs, 9 breaths)

| field | recorded | rule | ours after |
|---|---|---|---|
| seq | 81, no delay | 9/9 | 81 |
| packet | `map_projanim_v2` x6 | 9/9 | `projanim_pl_raw` x6 (raw heights) |
| source | her SW tile + (1, 1) | 9/9 | `movecoord(npc_coord, 1, 0, 1)` |
| target | the player (homing) | 9/9 | `uid` |
| starttime | 51, 56, 61, 66, 71, 75 | 54/54 | same list |
| endtime | 60 + 10 x Chebyshev(source, player) + 5 x n | 54/54 (d 12 -> 180, d 4 -> 100, d 3 -> 90, d 2 -> 80) | same |
| progress | 200 | 54/54 | 200 |
| startheight | 70 (raw) | 54/54 | 70 |
| endheight | 70..109, per fireball | 54 values, min 70 max 109 | `70 + random(40)` (192 sent: 70..109) |
| angle | 8..23, per fireball | 54 values, min 8 max 23 (15 never drawn, p = 0.03 per value) | `8 + random(16)` (192 sent: 8..23) |
| impact spotanim | `fireblast_impact` (131) on the player, height 90, delay = the first endtime | 9/9 | same |
| `sound_area fireblast_hit` (156) | at the player's tile, delay = first endtime, range 10, loops 1 | 9/9 | same |
| `sound_area dragonslayer_dragonbreath` (3750) | at her SW tile, no delay, range 15, loops 1 | 9/9 | same |
| splat | floor(first endtime / 30) ticks on | 8/8 landed (+3 at 3..4 tiles, +2 at 2) | same (32/32 in the test) |
| the player's block anim | none | 2 of 2 clean ticks ([5151], [5162]) | none (27 of 32 landing ticks had no anim; the rest were his own swing) |
| the spam lines | on the breath's tick | 9/9 | same |
| prayer drain | floor(current / 10), on the breath's tick | 5/5 clean ticks ([5123] 40->36, [5128] 34->31, [5132] 30->27, [5140] 22->20, [5148] 16->15) | `stat_sub(prayer, divide(stat(prayer), 10), 0)` |

`(endtime + 15) // 30` (the skill's general rule) misplaces the [5160] splat (+3, recorded +2);
`floor(endtime / 30)` fits all 8.

## Sounds

| sound | packet | recorded | ours before | ours after |
|---|---|---|---|---|
| dragonslayer_dragonbreath (3750) | sound_area, her SW, r15, d0 | 9 | 0 | every breath |
| fireblast_hit (156) | sound_area, his tile, r10, delay landing | 9 | 0 | every breath |
| dragon_attack (408) | synth to him | 2 (both claws) | 0 | every claw |
| dragon_hit (410) | synth to the hitter, d0 | every hit on her | yes (param defend_sound) | matched |
| dragon_death (409) | sound_area, her SW, r10, d7 | 1 | synths d0 | **fixed** |
| dragonslayer_dragonstomp3 (3752) | sound_area, her SW, r10, d70 | 1 | 0 | **fixed** |
| human_hit_1..4 / dragon_shield_1..5, d20 | synth, his own hit sounds | every splat | engine | engine-generic, matched |

## Damage (`rsprox_player_hits.py`, attribution corrected)

The tool files the two claws ([5156], [5164]) as breath landings (a later fireball of the
volley lands within a tick); the claw seq and the recorder's block anim on those ticks place them.

| attack | protection at the attack | recorded n | values | zeros | max | rule | ours after (6 scriptrun probes, same damage code as the test) |
|---|---|---|---|---|---|---|---|
| breath | shield | 2 ([5128], [5132]) | 6, 4 | 0 | 6 | wiki max 10 | n 86, 0..10, zeros 7% |
| breath | shield + Protect from Magic | 6 ([5136]..[5160]) | 0, 1, 4, 7, 0, 0 | 3 | 7 | wiki max 7 (hit) | n 68, 0..7, zeros 22% |
| breath | ([5123], shield, 11 tiles) | 1 | no splat at all (HP and `tracking_damage_taken` unchanged) | | | Open | splats |
| claw | Protect from Magic (not melee) | 2 | 0, 7 | 1 | 7 | slash, max 8 (wiki; strength 70, no bonus), accuracy rolled | n 438, 0..8, zeros 72% (expected 71% from the rolls: A 5056, D 7680) |

The wiki's matrix stays (70 / 10 / 55 / 55 / 40 / 7 / 7 / 4 / super antifire 0): every
recorded value is under its row's bound and the shield + Protect from Magic row's 7 was hit.
**Accuracy of the breath: not identified.** No roll (0..max) against a magic roll (the
recorder's magic defence roll 8,635 vs her 5,056: 29% to land): likelihood 2.2 : 1 for no roll
on 8 splats. Kept: no roll (the wiki's dragonfire). The 3 zeros of 6 under shield + prayer are
the reason to look again with more fights.

## Residual audit (every splat has a cause)

`rsprox_hit_causes.py` cannot read our tick log for her (she is a map spawn with no
`npc_spawn` row); the same rule was applied by hand to ours.

| side | splats | causes |
|---|---|---|
| recorded, on the player, [5118]..[5173] | 10 from her | 8 breath landings, 2 claws, 0 unexplained (plus the lost [5123] breath, Open) |
| recorded, on her | 16 | the recorder's knives (kit) |
| ours (`elvarg_audit`), on the player | 82 | 32 breath landings, 50 claws, **0 unexplained** |
| ours, on her | 78 | the player's knives and sword |
| heals | recorded 0 | ours 3 x `regen` +1 (engine npc regeneration, no splat) | matched (no splat either side) |

## Messages

| line | type | recorded | ours |
|---|---|---|---|
| Your shield absorbs most of the dragon fire. | spam | 9/9 breaths (shield worn) | every breath (8 of 8 in the test's leg 1) |
| Your prayers resist some of the dragon fire! | spam | 6/6 breaths with Protect from Magic on as she breathed; 0/3 before | same (6 in leg 1) |
| Your potion protects you from the heat of the dragon fire! | spam | not recorded (no antifire) | kept, UNVERIFIED (LostCity's guess) |
| (no line without a shield) | | not recorded | none, UNVERIFIED |

## Cadence, hunt, death

| item | recorded | ours after |
|---|---|---|
| attack rate | 4 ([5128]..[5164]; 5 for the first gap, she was walking) | 4 (her clock) |
| engagement | [5123], 2 ticks after the climb, from 11 tiles, face + breath on one tick | hunt in her timer (LOS, huntrange 11, never a player she is hidden from), opening breath by the engine swing on the face's tick; engaged C+1 when nothing claims the player (C+5 in the test: a giant skeleton outside held him) |
| death (K = the zero splat) | K+1 seq 4642 d7, reset, ops 0, 409 d7 r10, 3752 d70 r10; K+2 coord face; K+6 del + body | same (2 deaths in the test; the parent's quest run: seq + sounds 847, face 848, del + body 852) |
| respawn | not recorded | 50 ticks after the del (`npc_setrespawn(50)`: a script's npc_del arms none) |
| after K | nothing | nothing (timer stopped, target dropped; before: a breath on K+1 and a claw on K+5) |

## Rows ours sends that the recording lacks

| ours | verdict |
|---|---|
| a second, undelayed `npc_anim dragon_block` row on each throw tick, before the delayed one | the player's attack script plays the block and `npc_defend` then delays it; one mask, the last call goes on the wire (kit / engine-generic) |
| `npc_heal regen` +1 every 100 ticks | engine npc regeneration; no splat on either side |
| a breath landing on K+1 when she died with one in flight (the test's leg 2, 430 -> 432) | unverified: none was in flight at [5167] (Open 6) |
| claws and breaths of a melee fight (leg 2) | no recording of a melee fight; the chain's share is the JagexAsh rule, measured above |
| `projectile adamant_tknife_travel`, `human_stake2` throws | the test player's kit, generic |

## Test

`python3 tools/quest_gate/run.py --script test/combat/elvarg.lua --name elvarg_audit
--no-publish` (client lane, embedded server): **32/32 PASS** (2026-11-12, final run).
Leg 1 (the recorder's kit and fight): 8 breaths, no claw (the player stood two tiles off,
then beside her for the kill), 8 shield lines and 6 prayer lines, her death K+1 seq d7 + 409 d7
r10 + 3752 d70 r10 + ops + reset, K+2 the west face, K+6 npc gone with the body loc, the head at
K+26 (the parent's queue). Leg 2 (bronze sword beside her respawn): 24 breaths, 50 claws, the
player's shield block on every claw, death as leg 1. Fireballs traced with
`TORIRSSERVER_PROJ_DEBUG=1`: 192, every field in the recorded ranges. Six headless scriptrun
probes (`torirsserver --scriptrun`, private pack) gave the damage and chain numbers above.

## Stats against the cache

`configs/all.npc [elvarg]`: stat1..4 = 70/70/70/80, stat5 default, stat6 = 70; slash 40,
stab 20, crush 40, magic 30 defence. `quest_dragon.npc` states the same; ranged defence 20 is
the wiki's (light/standard/heavy 20), not in the cache record. `check_npc_cache_levels.py`:
nothing for Elvarg. Attack and strength bonuses 0 (wiki), so the claw's max is 8.

## Changes

- `OSRS-Content/.../quests/quest_dragon/scripts/elvarg.rs2`: rewritten (hunt, hold, chain,
  chase, breath, claw, death procs `~elvarg_death` / `[ai_queue4]` / `[ai_queue5]`).
- `OSRS-Content/.../quests/quest_dragon/configs/quest_dragon.npc` `[elvarg]`, `[elvarg_alive]`:
  `huntmode=none`, `huntrange 11`, `attackrange 11`, `death_script=yes`,
  `healthbar=healthbar_standard_30`.
- `OSRS-Content/.../_explicit/configs/server_fields.generated.npc`: the two Elvarg
  `healthbar=healthbar_standard_60` lines removed (stated twice fails the servpack).
- `OSRS-Content/.../skill_combat/scripts/player/player_magic.rs2` `~npc_is_attackable_opt`:
  Elvarg beside the Desert Treasure ice blocks (the parent agreed the one line).
- `test/combat/elvarg.lua`: the recorder's kit and fight (leg 1), then a melee fight beside
  her respawn (leg 2). `run.py --script test/combat/elvarg.lua --name elvarg_audit --no-publish`.
- quest_dragon.rs2 (the parent's): its `[ai_queue3,elvarg*]` call `~elvarg_death` first and its
  queue's first wait is 4.

## Open

1. **The lost first breath** ([5123] -> no splat at [5129], no HP change, no
   `tracking_damage_taken` change). Candidates: the player's climb delay, his Attack click at
   [5126] clearing a weak queue, a dodge by moving 4 tiles. One sample; ours always splats.
2. **Her spawn tile.** `areas/world/configs/m44_150.spawn` (generated by gen_spawns.py) puts her
   at 2852,9637; both recorded sightings are at x 2858 (2858,9634 [5072], 2858,9635 [5118]). With
   the generated tile she meets a climber at 5 tiles, not 11, so the first approach and the
   three-tile stand cannot be replayed tick for tick. Not this audit's file.
3. **Engagement tick**: recorded C+2 after the climb, ours C+1 with nothing else claiming him.
   Not explained (hunt cadence, the climb's delay, or a single-way window of 7).
4. **Engine**: `npc_hasop` should resolve the multinpc for the active player (the
   player_magic.rs2 line is the content stopgap); the ranged reach test fails beside a 4x4 npc;
   `nc_size` answers 0 for the shell; the hunt has no line of sight. All in src/torirsserver.
5. **Breath accuracy** (no roll vs a magic roll) and **the hold rule** rest on one fight; the
   claw/breath share in melee (20%) is the JagexAsh chain, never recorded beyond 3 attacks.
6. **A breath in flight when she dies** still splats in ours (the [5160]..[5164] pattern never
   had one in flight at [5167]); unverified either way.
7. **Visual**: the shell's stance over the rung on our client, and the six-fireball volley, not
   photographed.
8. **The player's kit**: our adamant knife throw is `human_stake2` (929); the recording's is
   `human_stake2_pvn` (7617). Generic weapon table, not Elvarg's.
9. **wanderrange 32 / maxrange 32**: not evidenced either way (she moved one tile between the
   two sightings).
