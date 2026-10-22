# Dreadnoth the Jellied: spec from RSProx

Spectated: index 4301 [307..549] (three raiders), 4302 [134..412] (five),
4303 [726..1144] (two) and [2421..3187] (five). The wiki
(`sources/Dreadnoth_the_Jellied.wiki`, `sources/TFA_Strategies.wiki`) has the
stats and the shape of the fight. He is 5x5; tiles relative to his
south-west tile. Tools: `tools/dn_jelly.py` (the blob, each jelly's swings,
the boss's melee), `tools/dn_blood.py` (hits on blood tiles),
`tools/dn_landing.py` (every enrage landing).

## 1. The room

| fact | evidence |
|---|---|
| Template square (65,88); raiders arrive west of the arena at (4172,5662); over its wall by its own Archive transporter (`tfa_teleporter_jelly`, at (4177,5663)), eight tiles east of it, sprite 409 (4176 -> 4185) | 4301 [321..324] (3/3 raiders) |
| `tfa_jelly_boss` added with the room at (4189,5661), spawnangle west, `[facing] coord=(4181,5663)` | 4/4 |
| Engaged when a raider comes within 4 of his 5x5 (the transporter's landing is 4 from him): music 895 that tick, +1 the bar (2100 at 1-2 raiders, 3050 at three) | 4301 [321..322] |

## 2. The fight

Measured over all nine sessions with Dreadnoth (index 4301, 4302, 4303,
4304, 4308, 4309, 4310, 4312, 4316: 13 fights; the four `rsprox local
(mrobe)` / `TFA` sessions 4309..4316 are B0aty's party), `--all-players`
transcripts, tools `tools/dn_rules.py`, `dn_cadence.py`, `dn_melee.py`,
`dn_clock.py`, `dn_leap.py`:

- **Crawl.** One tile every 2 ticks, sent as `crawl` (343 of 408 gaps 2; the
  rest span a slam phase, a freeze or a stand beside his target).
  His first step is 2 ticks after the engage face (4312 [744] -> [746],
  [2940] -> [2942]).
- **The step.** From his south-west tile at his target's tile as the tick
  began: diagonal, else along x, else along z, and never a step that puts his
  5x5 over the target. 364 of 420 steps are the plain line; 44 more are the
  arena's edge (his south-west stays within x 4184..4194, z 5656..5666); 12
  are a corner slide (target diagonal to his box: he steps along x to put it
  on a side, 12/12). With the target on a side of his 5x5 he stands (0 crawls
  in 21 such ticks). Jellies never stop him: 45 steps go into a jelly's tile.
- **The swing.** `dreadnoth_melee01` on the 4-tick clock (22 of 55 gaps 4;
  the longer ones nobody beside him)
  whenever ANY raider is within a tile of his 5x5, corners and under him
  included, whoever his target is (64 swings; in 20 the target was 2..5 away).
  It hits every raider within that tile (74 of 78 splatted, 3 of 32 at two
  tiles, 3 of 44 at three), the wiki's "hit everyone around him". He swings
  on a tick he does not step (65 of 67): a step that brings a raider beside
  him is swung at the tick after.
- **His target.** The raider who engaged him; then a re-roll to another raider:
  about 1 in 64 chase ticks (26 over
  1673 chase ticks, gaps 3..77), half the phase resumes (17 of 36, on the
  resume tick), and every third enrage landing (§6). Not the nearest, not the
  last attacker (45 of 80 switches went to a raider who was not the newest
  attacker).
- **Under him.** A raider standing under his 5x5 changes nothing unless it is
  his target; his target under him makes him take a cardinal step at random
  (4303 [846] west, [848] south), as his jellies step off their target's tile
  (W 26, N 25, S 21, E 19 of 91: `tools/dn_junder.py`).
- **Freezes.** He still swings frozen (4312 [795..803]) but does not step. He
  resists 40%, his jellies 20%: a freeze of n ticks holds floor(n x 0.6) (0.8),
  counted from the cast for an ice spell, from the arrow's landing for the
  scorching bow (`tools/dn_freeze.py`, `dn_freeze4.py`):

  | freeze | base | holds him | recorded | holds a jelly | recorded |
  |---|---|---|---|---|---|
  | Ice Barrage, kodai | 32 | 19: steps at cast + 20 | 13/13 | 25: cast + 26 | 13/14 |
  | Ice Barrage, ice ancient sceptre (+10%, rounded down) | 35 | 21: cast + 22 | 4/4 | 28: cast + 29 | 5/5 |
  | Scorching shackles (demons only) | 20 | 12: landing + 13 | 7/7 | - | - |

  A barrage on him while frozen does nothing (4/4: 4304 [866], [2438],
  [2440], [2445]); a second scorching spec while bound binds him again from its
  own landing (2/2: 4303 [2680], [2757]). After a thaw he is immune 5 ticks
  (fails 1, 3, 4 ticks after; holds 6), and his engage counts as a thaw: the
  first barrage 3, 3, 4 ticks after his first step never held him (4302 [160],
  4304 [742], 4308 [836]). A freeze can start inside a slam hold and outlast it
  (4312 [865] -> [885], [3069] -> [3089], 4316 [1139] -> [1159]).

## 3. Slam phases (85%, 65%, 45%)

Phase k (k = 1, 2, 3) starts on the tick his health first reads at or under
85 / 65 / 45 % (4 fights: 82.9-84.2, 61.4-65.0, 37.6-44.7 at the slams) and is
k slams 6 ticks apart (4/4):

| tick | recorded |
|---|---|
| J | `dreadnoth_jump01` |
| J+1 | every raider: spotanim `rockfall` slot 2 and the slam's hit (36 recorded); sound 12361 r20 size 20 at his middle; to every raider three `vfx_dreadnoth_ranged_proj01`, starttimes 10/12/14, endtimes 90/120/150, angle 25, progress 200, heights 450/30 (v2: ours 112/7), landing J+4, J+5, J+6; blood (§5): three tries at a tile -1..5 from his south-west tile, his body included, 1-3 landing (24 slams; 4301 [351]: (-1,-1), (4,3), (5,-1)) |
| each slam's J+4.. | that slam seeds a blob on every raider's tile (raiders on one tile share it; 64/64 seeds on a raider's tile; 4310 [705] 6/3/3 with two of four on one tile, 4308 [858] 9/3, 4312 [773] 11 with all four on one: `dn_seeds.py`) and each spreads three tiles a tick: the seed's `jelly_spawning_idle` + `tob_bloat_blood_splat` at once, each other tile a `jelly_spawning_idle` map projectile from the seed (0/30, heights 0) with both map anims at delay 30; every tile already shown is sent again (`jelly_spawning_idle`, no splat) every tick until they hatch |
| last J+8 | the jellies (§4) hatch on the blob's tiles: `jelly_spawn`, spawnangle south, ops none, +1 op2 |

The blob's size (4 fights, 12 phases): a phase hatches min(12, 3 x raiders x
k), split evenly over its k slams, each at most 3 a raider: 6/12/12 at two
raiders, 9/11/11 at three (two tiles blocked), 12/12/12 at five. Phase 1 at
three raiders, 4301 [354..358]: 3, 6, 9 tiles over three ticks; phase 3
[436..452]: 3+1, 3+1, 3. Solo: 3, 6, 9. Checked again on 4304 [720..1045]
(three: 9/12/11), [2166..2557] (two: 6/11/11) and 4308 [820..1027] (B0aty's,
four: 12/11/11); the jellies' swings every 3, the slams 6 apart, the leaps
every 8 and the seven jellies at K+3 in all three.

## 4. The jellies

`tfa_jelly_minion` "Jelly", 75 hp. Each one goes for the raider nearest it
(462/462 first faces), turning to another when one is nearer or, on the tick
before a swing, to one as near (300 turns, 130 of them there: `dn_jtarget.py`).
It walks a tile a tick, steps and swings on one tick (428 swings), swings only
from a side of its raider's tile (1205 of 1235: `dn_jswing.py`), and jellies
stack: 1522 of 2235 steps land on another jelly's tile, 113 inside his 5x5.
The first swing (`jelly_attack`) comes at
hatch+2, then every 3 ticks (4301: 54 of 61 gaps are 3; the cache's attack
rate 5 is not what they do); nearly every hit is blocked under the raiders'
prayers, and the wiki says they drain prayer even through protect from melee.
Dead: `jelly_death`, and a blood tile where it died (4301 [370..382]: deaths
and blood on the same ticks). Ours: melee up to 8; protect from melee blocks
it but drains 4 prayer (unverified).

## 5. Blood

`tfa_jelly_boss_damage_tile` (a random rotation) + `tob_bloat_blood_splat` +
sound 12387 r10: up to three at each slam, up to five at each enrage landing (in and round his 5x5), one where
a jelly dies, and during the enrage at each landing (§6). They stay until the
room is won (4301 [546]: all 99 replaced at once by `invisible_type8_nonblocking`,
each at its own rotation). A raider on one is hit for 7 or 8 every 2 ticks,
from the tick after they step on (`tools/dn_blood.py`, 14 runs over three
fights: 4303 [2831, 2833, 2835], [2735, 2737], [3115..3119], [1130, 1132]).

## 6. The enrage (25%)

| tick | recorded |
|---|---|
| E | "*glub glub*" at 23.4-25.8% (4/4) |
| E+5, then every 8 (now and then 9..14) | `dreadnoth_jump02`; `teleport` at his target, from the tile they stood on as the tick began: within 5 tiles each way of centring his 5x5 on them he lands centred, further he jumps exactly 5 tiles in the nearest of the eight directions; then the arena holds his south-west tile to x 4184..4194, z 5656..5666 (97/97 leaps: `tools/dn_leaprule.py`; 4312's two last leaps "to his spawn" are that 5-tile jump from a corner). After every third landing (+3) he turns to another raider (4301 2/2, 4303 6/6, 4304 5/5, 4312 2/2: `dn_leap.py`, `dn_clock.py`) with `exact_move` (old, 0, new, 90, the travel yaw: 4301 [476..516] 175, 1791, 511, 1279, 1279, 1968); `tfa_boss_jelly_5x5_jump` on the landing's middle |
| +3 | the landing: `rockfall` slot 2 on every raider, 12361 r20 size 20 at his middle; to each raider a `vfx_dreadnoth_ranged_proj01` 10/90 (at them) and a `vfx_dreadnoth_ranged_proj02` 10/180 angle 30 heights 650/60 (ours 162/15) at their tile, with sound 12362 r15 size 5 there; 1-5 blood tiles in and round his new 5x5 (offsets -1..5). Whoever is under him takes 30-71 and he heals about 38 (hitsplat 6: 28..44 over 11 landings, 89 with two raiders under him at 4303 [2969]) |
| +9 | each `proj02` lands: a blood tile on its tile (0-4 a landing), and a raider still on it is hit (4303 [2975]: 41) |

## 7. Death

| tick | recorded |
|---|---|
| K | `dreadnoth_death01` |
| K+3 | seven jellies jump out of him: each added on a tile of his 5x5, spawnangle north, `npc_jelly_jump_1`, `exact_move` (its tile, 0, twice its offset from his middle, 28-30, angle 1024) (4301 [526] 7/7, 4302 [382] 7) |
| K+4 | he is deleted |
| last jelly dead +3 | "Challenge complete: Dreadnoth the Jellied. ..."; the blood cleared; +3 the navigator at (4184,5663) facing east |

## Audit: our scriptrun against 4301, 4302, 4303 (2026-10-21)

Run: `test/guthixian_archive/tfa_dreadnoth.lua` (solo; `::tfaroom 4 2`, the
transporter, `::tfaboss` by stages, `::tfadnjellies` between phases since a
bow kills a party's jellies too slowly; it dodges the enrage leaps and prays
missiles through them). Inventory: `tools/rsprox_tells.py --anchor
16665,16666 idx4301.txt idx4302.txt idx4303.txt --ours <ticklog>` (counts
per window).

| tell | recording | ours | verdict |
|---|---|---|---|
| entry | the transporter (409), music 895 that tick, +1 the bar | [103] teleport + 409 + 895, [104] the bar | matched |
| melee | `dreadnoth_melee01` every 4 beside his target | same (the bot stands beside him, so 49 swings) | matched (rate) |
| slam | J jump01; J+1 rockfall slot 2, 12361 r20 s20, 3x proj01 10/90 12/120 14/150 a25 p200 112/7, 2-3 blood on his rim | same | matched |
| blob | per slam a seed at J+4, 3 tiles a tick (0/30 map projectile, splats at 30), resent every tick | same; 3, 6, 9 solo | matched |
| jellies | hatch at the last J+8 (`jelly_spawn`, south, op2 at +1), swings from +2 every 3, `jelly_death` + blood | same | matched (damage unverified) |
| blood | `tfa_jelly_boss_damage_tile` random rotation + splat + 12387 r10; 7-8 every 2 ticks on it | same | matched |
| enrage | "*glub glub*" at 25%, jump02 +5 then every 8, teleport + exact_move yaw, 5x5 jump spot | same | matched |
| landing | rockfall, 12361, proj01 at the raider, proj02 at their tile + 12362 r15 s5, 1-5 blood, crush + heal | same | matched (crush, heal approximate) |
| proj02 | lands at +6 on its tile: blood, a hit if still there | same | matched (hit unverified) |
| death | K death01; K+3 seven jellies out (jump_1, exact_move, north); K+4 deleted | same | matched |
| win | last jelly +3 the line and every blood tile to `invisible_type8_nonblocking` at its rotation; +3 the navigator (4184,5663) facing east | [575] last jelly, [578] line + 28 tiles, [581] navigator | matched |
| music | `midi_song_v2` with fades | plain `midi_song` | **open (engine)** |
| damage | slam 36 recorded, shots up to ~30, crush 30-71, proj02 41 | slam 0..40 (prayed 0), shots 0..30 (prayed 0), crush 30..71, proj02 20..40 | approximate |
| jelly prayer drain | the wiki only | 4 a blocked swing | unverified |
| landing without protect from melee | the wiki: "players will take damage regardless of location" | 0..40 to every raider not praying it | unverified |
| defend sounds | not sent to a spectator | null | **open** |

## NPC info audit (2026-10-21)

Every NPC_INFO block of every npc of the room, recording against our tick log, per life:
`tools/rsprox/npc_stream.py --match <type> --summary <transcript> <ticklog>` (counts per
life), `tools/rsprox/npc_timeline.py` (the recording's blocks) and
`tools/rsprox/ticklog_npc_timeline.py` (ours, with the file-only `npc_info` rows:
spawn angle, ops, level, transparency, exact move, timed bar). Facing is part of npc info
and is audited block by block (the entity face, the coord face and its `instant`, the angle,
the reset, the walk type).

| npc | block | recorded | ours now | verdict |
|---|---|---|---|---|
| Dreadnoth | spawn | `spawnangle=west`, coord face (4181,5663), level x party (941 at three, 994 at four, 1056 at five; none sent at one or two) 6/6 | ours had north-west | fixed |
| Dreadnoth | engage | `[facing] player, walktype=turnonwalk` 6/6 | the entity, now turn-on-walk | fixed |
| Dreadnoth | leaps | teleport + exact move + `level_change` (the re-add), no face | same | matched |
| Dreadnoth | death | seq 14782 + ops none, **no reset**, del +4 (4301, 4308) | ours sent a reset | fixed |
| jelly | hatch | `spawnangle=south`, spawn seq, ops none; +1 ops 0b00010; +2 the entity, held (227/227, 226/227) | ours: no entity, a tile face every swing | fixed |
| jelly | jumpers | north, exact move, `angle=1024`; no ops block until +2, the entity +3 (28/28) | ours sent ops none at add | fixed |
| jelly | death | reset + ops none (+ the seq on D 159, D+1 31, D+2 58 of 250); del seq+3 (250/250) | ours: seq and del on one tick, so no death seq was ever seen | fixed |
| navigator | add | its side's angle, `angle=1536, instant=true`, fade in; +1 `reset` (13/13 east; Ezekiel's west one no reset, 1/1) | ours: no angle face, no reset | fixed |

## Damage audit (2026-10-21)

Every splat on every raider of the room, put down to its mechanic by
`tools/tfa_mech.py --room dreadnoth` over the `--all-players` transcripts of index
4301..4308 (the party runs are spectated, so the raiders' prayers are not on the
wire: a mechanic's zeros are its prayed hits, its nonzero tail mostly the unprayed
ones). Each mechanic is matched by its own geometry: a projectile by its target and
landing tick, a tile hazard by the map anim or loc on the raider's tile, a swing by
the seq beside the raider.

| mechanic | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| slam (J+1, every raider) | 133/141 0 under protect from melee; 22..44 (8, none below 22) | 0..40 | 20..44 | fixed |
| slam shots (proj01) | 541/581 0 under protect from missiles; 1..33 | 0..30 | 0..33 | fixed |
| jelly swing | 640/658 0 under protect from melee; 4..12 | 0..8 | 0..12 | superseded below (the fourth pass) |
| melee | 38/71 0; 2..30 | 0..30 | same | wrong: superseded below (the fourth pass) |
| blood tile | 7, 8 (48 of 56) every 2 ticks | 7..8 | same | matched |
| landing, not under him | 147/174 0 under protect from melee; 16..38 | 0..40 | 15..38 | fixed |
| landing, under his 5x5 | 26..36 (4); his heal 28..44 a raider, 89 for two | 30..71 crush, heal 34..44 | 25..44, he heals what he deals | fixed (the plan's 30..71 was his heal splat) |
| proj02 on its tile | 19, 20, 41 | 20..40 | 19..41 | fixed |

## Pathing, cadence and targets: second pass (2026-10-21)

The first audit ran a bot standing beside him, so his chase, his swing's
reach and every party rule were never exercised: ours walked (not crawled),
swung only at an adjacent target and on the tick after a slam, kept one
target for good, and sent every jelly, every blob seed and every leap after
raider 0. Rules in §2, §3, §4, §6 above; ours measured with
`tools/dn_ours.py` on `test/guthixian_archive/tfa_dreadnoth_party.lua` (three
seats in one room by `::tfahost` / `::tfajoin`, `tfa_party_debug.rs2`: the
lobby has no join yet) and the solo test.

| rule | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| move kind | `crawl` 168/168 | `walk` (`npc_approach`) | `crawl` (`npc_setmovespeed(2)`) | fixed |
| step cadence | 2 ticks (343 of 408 gaps) | 2 | 2 (42 of 49 gaps; the rest standing beside his target) | matched |
| first step | face +2 | face +1 | face +2 | fixed |
| step rule | sw line at the start-of-tick tile, x then z, never over the target, stand on a side | `npc_approach` (naive path to beside, decided in the player phase on stale tiles) | the rule, in his own `[ai_timer]` before his step; 35 of 51 the plain line, 16 the arena's edge / a corner | fixed |
| npc collision | none (45 steps into jellies) | `blockwalk=npc` | `blockwalk=none` | fixed |
| swing trigger | any raider within 1 of his 5x5 | his target, cardinally beside | any raider within 1 | fixed |
| swing hits | every raider within 1 | his target | every raider within 1 (54 hit at 0..1) | fixed |
| swing vs step | not on a step tick (65/67) | either | 47/47 on a rest tick | fixed |
| slam hold | nothing from the first slam to the last +10 (36/36) | swung at J+1 | crawls at J+10 | fixed |
| enrage line | waits for the resume when crossed in a phase (3/3) | at once | after the hold | fixed |
| target switches | ~1/64 chase ticks, half the resumes, every 3rd landing, never the same raider | never | the same | fixed (rates fitted) |
| turn tick | on the switch's tick | (none) | on the tick (`npc_faceplayer`) | fixed |
| leap | onto his target, within the arena | raider 0, unclamped | his target, clamped | fixed |
| blob seeds | one per raider tile, the slam's quota split | one, on raider 0 | one per raider tile | fixed |
| jelly target | nearest (462/462), re-picked among the nearest before each swing | raider 0, every tick | nearest; re-pick before each swing (12/12 first faces nearest) | fixed |
| jelly swing | from a side (1205/1235), on its step tick too | corners and under (273 corner, 40 under of 454) | from a side (553/553) | fixed |
| jelly stacking | 1522 of 2235 steps onto another jelly | blocked (one jelly oscillated two tiles for 40 ticks) | 471 of 723 | fixed |
| phrases | "*glub glub*" only, 12/12 | same | same | matched |

Third pass (2026-10-21), `test/guthixian_archive/tfa_dreadnoth_freeze.lua`
(`tools/dn_ours_freeze.py`) and the party test (`tools/dn_ours_leap.py`):

| rule | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| kodai barrage | steps at cast + 20 (13/13) | cast + 33 (no resistance) | cast + 20 | fixed |
| ice ancient sceptre barrage | cast + 22 (4/4) | cast + 33 (no sceptre bonus) | cast + 22 | fixed |
| barrage inside the post-thaw immunity | ignored (3/3) | ignored | ignored | matched |
| barrage just after his engage | ignored (3/3) | held | ignored | fixed |
| scorching bow spec | landing + 13 (7/7) | no bind at all | landing + 13 | fixed |
| second spec while bound | the second's landing + 13 (2/2) | - | the second's landing + 13 | fixed |
| leap | the centred-or-5-tile rule (97/97) | centred on the target where the room tick saw them | the rule, in his own turn (16/16) | fixed |
| target under him | a random cardinal step (2/2) | stood | a random cardinal step | fixed |

The freeze rules are the combat code's, not this room's: `freeze_resistance`
and `demon` are npc params (`skill_combat/configs/npc_combat.param`),
`~npc_freeze_resisted` applies the first to every player freeze (ice spells,
the ZGS, jade bolts, the scorching bow), the ancient sceptres' 10% is in
`~pvm_freeze_effect`, and the scorching bow's bind is the new engine command
`npc_bind` (a freeze that renews).

Still open:
- **Switch rates** are fits (1/64, 1/2), not a rule the wire states.
- **The scorching bow's burn** still lands on any target and as one 5-damage
  hit (its comment; not this room's).
- **Other demons** bind to the scorching bow only once a config sets `demon`.

## Melee and the jellies' swings, by the raiders' own prayers: fourth pass (2026-10-21)

The damage audit above could not see prayers (the parties were spectated) and
took every 0 for a prayed hit. The raiders' overheads are in their appearance
blocks: `tools/rsprox/rsprox_index.py get ... --all-players --bas` now prints
them (`[status] overheadicon=`: 0 melee, 1 missiles, 2 magic; the `--bas`
filter gained PLAYER_APPEARANCE_STATUS). The mapping checks out on his slam:
245 of 246 splats 0 under protect from melee, 22..44 under protect from
missiles. `tools/dn_prayhits.py`, `dn_style.py`, `dn_jstyle.py`.

| attack | recorded | ours before | ours now |
|---|---|---|---|
| his melee, raider praying melee | hits through it: 33 clean swings (no jellies, not on blood), 23 hit 2..30 spread evenly; all 109: none above 30 | always 0 | after an accuracy roll, 0..45 cut to two-thirds: 0..30 |
| his melee, raider not praying melee | 2 splats in nine sessions (7, 25): too few to say | 0..30 | 0..45 after an accuracy roll: his max-hit formula (strength 285, +35), owner's call 2026-10-21, **unverified** |
| his melee's accuracy | 4 misses in 26; the wiki's +185 attack bonus against each raider's kit predicts 4.8..5.2 (stab, slash, crush alike); the cache record states none (0 would predict 16) | no roll | `npc_player_hit_roll`, crush, +185 |
| his levels | stat1..6: 280/125/285/2100/250/220 | Attack 1 (no server field: the engine never reads the cache's) | the record's (set in `tfa_dreadnoth.npc` by the weapons audit, 19:05) |
| jelly, raider praying melee | 6820 of 6938 splat 0; the rest are blood the same tick | 0, a drain | same |
| jelly, raider not praying melee | clean tiles: 8 misses in 28, hits 5..10 evenly (5 8, 6 9, 7 11, 8 10, 9 11, 10 7), never 1..4 | 0..12, no miss | 5..10 after an accuracy roll, +100 attack bonus (a fit: it predicts 8.3..9.4 misses) |

Open: his unprayed melee (2 recorded splats; 0..45 with prayer taking a third
off is a choice, not a measurement), the melee style (the three styles
predict within 0.4 misses of each other) and the jellies' attack bonus (a fit
on 28 swings).
