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

- He crawls toward his target, one tile every 2 ticks (`crawl`, 168 steps).
- Beside them, `dreadnoth_melee01` every 4 ticks (cache attack rate 4; 4301 [329], [333]). Nobody stood beside him for long in the recordings (4 swings in 4301, 14 in 4303's first fight), and he swings with jellies up (4303 [840]: 12 alive).

## 3. Slam phases (85%, 65%, 45%)

Phase k (k = 1, 2, 3) starts on the tick his health first reads at or under
85 / 65 / 45 % (4 fights: 82.9-84.2, 61.4-65.0, 37.6-44.7 at the slams) and is
k slams 6 ticks apart (4/4):

| tick | recorded |
|---|---|
| J | `dreadnoth_jump01` |
| J+1 | every raider: spotanim `rockfall` slot 2 and the slam's hit (36 recorded); sound 12361 r20 size 20 at his middle; to every raider three `vfx_dreadnoth_ranged_proj01`, starttimes 10/12/14, endtimes 90/120/150, angle 25, progress 200, heights 450/30 (v2: ours 112/7), landing J+4, J+5, J+6; blood (§5): three tries at a tile -1..5 from his south-west tile, his body included, 1-3 landing (24 slams; 4301 [351]: (-1,-1), (4,3), (5,-1)) |
| each slam's J+4.. | that slam seeds a blob on a raider's tile and it spreads three tiles a tick: the seed's `jelly_spawning_idle` + `tob_bloat_blood_splat` at once, each other tile a `jelly_spawning_idle` map projectile from the seed (0/30, heights 0) with both map anims at delay 30; every tile already shown is sent again (`jelly_spawning_idle`, no splat) every tick until they hatch |
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

`tfa_jelly_minion` "Jelly", 75 hp. The first swing (`jelly_attack`) comes at
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
| E+5, then every 8 (now and then 9..14) | `dreadnoth_jump02`; `teleport` onto a raider (his 5x5 centred on them) with `exact_move` (old, 0, new, 90, the travel yaw: 4301 [476..516] 175, 1791, 511, 1279, 1279, 1968); `tfa_boss_jelly_5x5_jump` on the landing's middle |
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
