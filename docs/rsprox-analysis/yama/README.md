# Yama's Domain: combat audit (RSProx)

The fight's implementation is `OSRS-Content/osrs-content/server/scripts/minigames/minigame_chasm_of_fire/`:
`yama_fight.rs2` (instance, entry, challenge, leaving), `yama_attacks.rs2` (the clock, slots, specials,
damage, the kill), `yama_hazards.rs2` (waves, streaks, flares, glyphs, the P3 crash), `yama_judge.rs2`
(portal, the Judge, the return, the end), `yama_debug.rs2` (the test's cheats), `configs/yama.npc`,
`configs/yama_fight.constant`. The Voice, the throne's ops and the rewards are `yama_access.rs2` /
`yama_rewards.rs2` (not this audit's).

**Sources.** Index 4498 (rev 242.1, normal world, the owner, duo with wehst, 16 summons at ticks
[409] [684] [1701] [2574] [3436] [4234] [5051] [5543] [6032] [6705] [7157] [7982] [8767] [9383] [9905]
[10755]; 8 kills, the rest deaths/leaves) is the primary source; every number below is from it unless
marked. Grid Master sessions (index 798, 3268, 3322, 3329, 3368, 3371, 799, 787, 3267, rev 234/235):
contract fights (798 [2315..], 3329, 3368, 3371), hits against prayer states 4498 never shows. Index
2628 is not Yama. Wiki pages (`docs/minigames/yama/sources/`) only for what no recording shows, marked
*wiki*. All tiles below are template tiles of square 23_157 (x 1472..1535, z 10048..10111).

**Tools** (`docs/rsprox-analysis/yama/tools/`): `ymparse.py` (transcript parser), `yama_tl.py` (timeline).
The one-off tables below came from scripts built on `ymparse.py` in the session scratchpad; the
tile-rule checks (`wave` and `crash` rows below) are re-runnable from the same parser.

## 1. Fight structure

| phase | attack clock | specials | hazards | leaves at |
|---|---|---|---|---|
| Summon | add at S+2, hud S+5, glyphs S+7, first attack S+10 (16/16: 8 ticks after the add) | | | |
| P1 | 8 ticks | Slot 0 regular; then [special, regular x3] repeating | alternating wave/streak on the third regular's turn | HP ≤ 1666 (2/3), portal replaces the next attack |
| Judge | | | | Judge dead → back to the arena |
| P2 | 8 ticks, same shape, stomp specials | | double waves (one each side of the swing) | HP ≤ 833 (1/3) |
| Judge | | | | |
| P3 | 7 ticks | crash/regular alternation (table below), no glyphs | waves on crash slot 3 and regulars 11, 13, 14 | death |

Attack seqs (`npc_yama01_magic01` 12144 delay 30 + `vfx_npc_yama_magic_{fire,shadow}_spotanim01` delay
30) and the cycle are 417/417 regular attacks recorded; the P3 token stream (below) repeats exactly in
all 12 fights that reached phase 3.

### Phase 1/2 slots (15 fights tabulated)
Slot 0 is a regular of the style opposite the north pair of glyphs (layout A = north pair fire →
shadow, layout B = north pair shadow → fire: 15/15). Then specials and three regulars of the special's
element alternate. The flare summon (`magic01` with no spot) takes a *shadow special slot*: in fire-first
layouts the 2nd or 4th special slot (684, 2574, 10755 on the 2nd; 1701, 4234, 6032, 7982 on the 4th),
in shadow-first layouts the 3rd or 5th (5051, 7157 on the 3rd; 3436 none before the portal). After a
flare the regular style does NOT flip and the next special is the shadow one (fight 684: flare, then
3 more fire regulars, then spike). Hazards ride the third regular of each block: two ticks before
it for waves (wave @-2 16/16 in P1), one tick before it for streaks (-1), P2's waves one on each side
(@-2 and @+2: 22/22), alternating wave/streak starting with the wave in layout A, the streak in layout
B (38 streak events; both layouts 15/15).

Portal: replaces a regular (the portal seq, not the regular's). Decision table in §7.

### Phase 3 tokens (12 fights, 16/16 of those reaching the slots)
`C R R C+W R R R | C R+2f R C R+W R R+W R+W R R` repeating the 10-slot block from slot 7:
C = crash (no spot), R = regular (alternating style, starting with the north glyphs' element),
W = waves (both diagonals at once on the attack's tick), 2f = two flares on the attack's tick. A
melee swing replaces a regular when the target is adjacent (seen 684 slot 14, 1701 slot 6, 9383
slot 13); it does not flip the alternation, and it adds one flare.

## 2. Attacks: tells, ticks, damage (R = recorded, O = ours)

Regular: `A` seq+spot (delay 30), raiders' `vfx_player_yama_magic_*_impact01` slot 2 at A, the hit at
A+3 (64+31+... all at +3), **on every raider** (both splats at the same tick). Prayer: fire = Protect
from Magic, shadow = Protect from Missiles, read at the landing (198/220 best among A..A+3, zeros are
misses). P1/P2 prayed: 0 (64/64, 54/54 fire; 30/30, 31/31 shadow). P3 prayed: 0..3 (5 zeros, values 1,2,3).
Unprayed/wrong prayer: to 34 (recorded max 34 in 4498; wiki 46, used as the roll's max: *wiki*). 2 ticks
after a landed hit: poison 100 (5 events in 4498, "You have been poisoned!").

Melee: `npc_yama01_melee01` at A, hit A+1 on the target and anyone within 3 tiles of them (*wiki*,
4498 shows the target's splats only). Protect from Melee cuts to 6 (P1/2) or 22 (P3) (*wiki*).

| special | A (tell) | landing | area | damage (R) | ours |
|---|---|---|---|---|---|
| Rock (P1 fire, `npc_yama01_magic02` + `vfx_yama_flaming_rock_spotanim_01`) | message to each raider, two projectiles from his middle (`...projectile_01` 65/180/60/100, heights 2000/0), per-raider `human_*block` delay 180, impact spot delay 180, synth delay 180 | A+6, each raider | n/a (per raider) | absorbed by glyph (51/51 of 44-splats at +6); unprayed unknown | absorbed; 40..80 roll (*no sample*) |
| Spike (P1 shadow) | as rock, `vfx_yama_shadow_spike_*`; impact spot at the landing, no delay | A+6 | | absorbed 50/50; unabsorbed 46, 70 in 2 GM sessions | same roll |
| Meteor (P2 fire, `stomp01` + `vfx_yama_meteor_spotanim01` + one of three lines) | A+2: `yama_meteor_npc` sw = target tile -3; run off for the target; A+6 landing, `cam_shake` x3 | A+6 at the tile, 5x5 (d≤2 80, d=3 40; d≤1 lethal *wiki*) | Chebyshev to the tile | 80, 56, 36, 18 recorded (dt=6); absorbed 48/55 | d≤1 121, d=2 80, d=3 40, glyph absorbs |
| Root (P2 shadow, `stomp01` + `vfx_yama_shadow_root_spotanim_01`) | A+2 projectile per raider (raw heights 10/10, 90 cycles), defend seq delay 90 | A+5 | per raider | absorbed 57/57; unabsorbed 17..60 + bind ("Yama briefly binds you!" once) | same |

Crash (P3): `magic01` no spot at C; at C+2, C+5, C+8 each raider gets a rock on the tile they stand on
(`vfx_player_yama_falling_rock_impact01` centre delay 0 and two side tiles delay 3); a bowtie of
`vfx_flaming_skull_shadow_impact01` at +3 with fixed tile/delay rings. **Only the centre tile at the
landing hurts** (18/18 hits on-centre, 0/157 off-centre, 3 stray rows noise): `yama_crash_event`, 12..20.

## 3. Hazards

Shadow waves (`vfx_shadow_wall_02/03`): the whole wave is sent once: 514 tiles (identical tile set
every event, 111 events) with per-tile delay: kind 3 `420-15(dx-dz)`, kind 2 `15(dx+dz)-480` (exact,
514/514). P1 one wave of a random diagonal, P2 two (one each side of the swing), P3 both at once.
Hit rule: floor(delay/30) ticks of the wave's age with the raider on the tile at the end of the tick
before: 33 hits all on matching tiles with one ambiguous row; 34 "no hit" rows all explained
(dead, portal, kill, Glyph of Shadow absorb). Damage 16..20 (the recorder's splats on the line's tick:
16 x4, 17 x6, 18 x4, 19 x3, 20 x8; ours `~random_range(16, 20)`), then "You've been injured..." and
protection prayers off: the recorder's re-presses at +3, +3, +4, +5, +5 after the line all took, so
the block is at most 3 ticks (ours `^yama_prayer_block_ticks = 3`; no press before +3 was recorded, so
a shorter block cannot be ruled out).

Fire streaks (`vfx_fire_wall_01`): 6..10 lanes (5/14/10/7/2 of 38 events). Lane origins by row are
fixed per side (table in `yama_hazards.rs2`, `yama_streak_origin`). Age 0..2 the origin charges (delay
15); age 3 the first step (+-1, delay 30); then x15 = origin ± 2(age-3), x30 = origin ± (2(age-3)+1),
only on the floor mask. Hit tile rule: sent-last-tick tiles, 18 splats, not a clean rule (Open).

Void flares: spot tiles fixed to four corners (1510,10081) (1498,10073) (1495,10081) (1507,10073):
two distinct per summon (P1/P2 at +2 after the seq; P3 on the attack); +1 ops on; +3 timed bar
(630 cycles P1/P2, 600 P3, 360 for the melee's). Death seq and gone 3 ticks later. Explosion: no
recording reaches it (*wiki*, unverified, on the bar's end: 42..44 scaled, heals Yama).

Glyphs (3x3 locs, middle tile only; layouts A/B fixed by the throne pair): stepping on the middle
wards both raiders 10 ticks (message, spot `vfx_yama_{fire,shadow}_immunity` slot 2 on both; the
stepper's own hit 2/6/9/12 fire, drain shadow; `bind_impact` synth); spent loc + deactivate loc, next
tick inactive, lit again 96 ticks on (`[703]→[799]`, `[908]→[1004]`). Portal spends all (`[830]`).

## 4. Portal and Judge

Portal timing rule used: at the attack tick, portal if HP at the previous attack was ≤ threshold:
21/26 recorded portals match; the other 5 are the threshold crossed after the previous attack
(ours one attack late there). Lines: "You bore me."/"Enough."/"Begone"/"I grow tired of you."

Shape (`[828..836]`): P seq+spot+line and face to the target tile; raiders' floor portal spot slot 5,
face his middle; P+2 fall seq delay 5, glyphs out, flares die; P+4 hud off, fade out, minimap; P+6
Judge up (`yama_judge_of_yama` at 1478/1528,10098, creator west, partner east; recorder fell east
because wehst created the domain [533]), teleport with on-face seq and spot height 638; the fade in;
P+8 closed. Judge: casts every 4 ticks then 5 (10 casts then death at [881]/[882]); fire surges down the
causeway 5 tiles wide; the cave shakes with his 5th and 10th casts: add+17..19 on all 26 recorded
Judges and add+40..44 on the 9 that lived that long (5..7 each), the camera reset a tick later (30/30
shakes in 4498: +1, a few +2); his platform fireball (`luc2_undead_demon_fireball_large_proj` 345/0,
120 cycles). Death seq `judgeofyama_death`, gone +5 (26/26), then the return. Yama's reset walk is
`telejump` to 1501,10076; players fall back to 1502,10085.

The Judge's prayer (4498 [854..875]): head frame 7 = melee (+ranged), 6 = magic (+ranged); the frame
flips at an attack's launch to that attack's style and a hit of the style already prayed lands 0. The
recorder's landed hits are max hits: 57 magic (Dark Demonbane under Mark of Darkness, +50% with the
purging staff) and 22 melee (the emberlight, +70% demonbane passive). Ours: `~yama_judge_prayer`, the
passive in `~yama_hit_prepare` (the combat code has no demon attribute to read), always-max in
`~player_npc_always_max`; the solver's kills land 57 and 22 alternately (22..27 hits a Judge, 48 seeds).
The stones: `[aploc1,yama_steppingstone]` reaches 3 (198 of 206 recorded clicks are at Chebyshev 2..3,
130 at 3). The fireball's tile and the surge columns are still approximations (Open).

## 5. Entry, summon, kill, death, exit

Entry `[531..536]` (every entry): O mesbox no continue + fade 948 out; O+1 minimap_state 2; O+2
teleport to 1503,10050 facing 1501,10050, throne add, preloads, music 0/60/60/0; O+3 box with
continue, fade in, minimap back, multiway 1; O+5 all shut, music again. Implemented as recorded
(preloads: the list of 17 recorded names).

Summon (S = throne anim tick, S=C+1 after a page; 16/16): S the throne's anim+spot+ops off, three
firewall locs (rotations vary), music; S+2 the add (spot+seq, ops 0, instant face north of the
throne); S+5 hud + ops 0b00010; S+7 glyphs; S+10 first attack.

Kill: K despawn seq+spot, ops off, firewalls → invisible; K+5 throne back (del+add with spawn anim),
success count line, duration line; K+7 Yama gone; K+9 throne line; K+11 rewards `~yama_reward`, ops
0b00101; K+15 boss hud off. Implemented with those offsets.

Death: to 1440,10080 (Voice's room); hook in `player/death.rs2` (`~yama_on_death`). Outcome varps
set for the coordinator's greeting (`%varp9562_yama_last_outcome`).

Magic defence (tools/yama_accuracy.py, the recorder's Dark Demonbane casts on Yama, outcome = the
first own splat 1..5 ticks on): accuracy 0.84/0.74 P1, 0.80/0.87 P2, 0.79/0.87 P3 (Yama facing the
recorder / the partner; 54..92 resolved casts a cell). It does not move with his face target or the
phase, so the wiki's +60 (target using magic) and phase-3 +81 are not in the recording; ours is -30
in every state (`~yama_magic_defence_bonus`). The Grid Master sessions cannot test it: their solo
magic (3329, 3368, 3371, eye of ayak) never misses (Leagues).

Contracts: seen in 798, 3329, 3368, 3371: base hp (3000 Bloodied Blows, 2875 Glyphic Attenuation, else
2500) and 7-tick cycles recorded (798: 3000 @ [2854], 2875 @ [3399]); thresholds 75%/50% *wiki*.
Prayer penetration, drain and flare rules of each contract: **not implemented (Open)**.

## 6. Wire audit against our tick log (test run y1, solo, 2,650 ticks)

`npc_block_inventory` (recorded in 4498, 16 fights, vs ours, one test fight):

| type | block | R | O | verdict |
|---|---|---|---|---|
| yama | sequence | 627 | 347 | rates per fight match (per-kind compared) |
| yama | spotanim (magic fire/shadow, specials, portal, summon) | 549 | 84 | same names, counts by fight length |
| yama | say | 80 | 12 | specials' and portal lines present; target-switch lines probabilistic |
| yama | enabled_ops, facing, headbar, hit | present | present | |
| yama_meteor_npc | sequence, spotanim, bas | 24 | 5 | matched per spawn |
| yama_judge_of_yama | sequence (casts + death) | 265 | 22 | present; test judges die early by cheat |
| yama_judge_of_yama | headbar, hit, spotanim | 301/301/76 | 0 | **test does not hit the Judge** (Open) |
| fire_surge_raised | enabled_ops, exact_move, facing | 4256 | 32/513/32 | surges live on the wire |
| yama_voidflare | sequence, enabled_ops, headbar | | | present |
| yama_voidflare | body_customisation | 48 | 0 | **dropped: three model ids on one of each pair's adds, our command takes one (Open)** |
| yama_throne | say, facing | 20/23 | 0/0 | **throne kill line sent on K+9 is `npc_say` on a throne re-added at K+5 (checked in the run's `npc_anim` rows only); greeting lines belong to the access side** |

Residual hit-cause audit (`rsprox_hit_causes.py` on the all-players transcript vs the test log,
the run's output was read in the session): wave splats on-tile match in value range; regular hits' timing and
range match; the leftover recorded rows are the glyph-step hits (`spot:vfx_yama_fire_immunity`, 2..18,
45 rows: our test steps on few glyphs), the cave shake (`demon_casting@2:adj`, 5..7, test's early
Judge kill) and flares' death explosions (Open).

## 7. Damage summary (R)

| cause | prayer | n | zeros | values (R) |
|---|---|---|---|---|
| regular fire, P1/2 | Protect from Magic | 118 | 118 | 0 |
| regular shadow, P1/2 | Protect from Missiles | 61 | 61 | 0 |
| regular, P3 | matching | many | few | 0..3 |
| regular, unprayed | none/wrong | 40 | 18 | 1..34 (R max 34) |
| wave | none reduces | 47+43 | | 16..20 |
| crash | none reduces | 25 | 0 | 12..19 |
| meteor | none (glyph only) | 8 | 0 | 18..80 |
| spike | glyph | 3 | 0 | 17..70 (GM) |
| root | glyph | 5 | 0 | 17..60 |

## 8. Open list (honest gaps)

- Duo: not run. The partner joins by the Voice's "Join" (op4, a name prompt, `p_findmutualfriend`),
  so both seats must be on each other's friends lists, and neither lane's drive API can add a friend
  (no FRIENDLIST_ADD press: scriptrun needs a `d_friend_add` beside `d_resume_namedialog`, the live
  client a verb on `net_out_friendlist_add`). Target switching (1/8 an attack), the per-raider drop
  split and the per-tick facing diff wait on it.
- Live-lane parity: not run (no `torirs_questtest` binary built in this checkout; run.py builds one
  and the shared packs). Scriptrun only.
- Wire: the packet-kind diff (tools/yama_wirekinds.py, section 6) leaves the boss HP HUD refresh
  (the recording re-sends `if_setcolour` x3 + `hp_hud_open` on most ticks, we do not: engine HUD),
  `ambientsound_stop` (204 in one recorded fight, 0 ours) and the zone ambience unexplained; packets
  inside our UPDATE_ZONE_PARTIAL_ENCLOSED are not decoded by the wirelog, so map anims and loc changes
  were compared through the tick log only.
- The Judge's fireball tile and the surge column rule; the flare explosion (no recording reaches it).
- Streak hit judgement (no clean rule in 18 splats); the streak's "seared" bind (wiki) is not
  implemented.
- Portal crossings: 21/26 recorded match the rule, 5 cross an attack early (ours one attack late).
- Flare `body_customisation` (3 models on one flare of each pair) dropped: the engine command sets one.
- Stance movement (chase on a melee hit) is from the wiki; no recording shows a melee target.
- Contract rules beyond hp and the 7-tick cycle (798, 3329, 3368, 3371): not implemented, one row each
  in AUDIT.tsv.
- `yama_transition_damage_taken` is transmitted now (varp 4711) but never written.

## 9. What a solver perceives (tell → client packet → decision)

| mechanic | tell (client) | when | decision |
|---|---|---|---|
| regular | npc seq 12144 + spot 3246/3243 (delay 30) | A, hit A+3 | pray fire/shadow on A+2 at the latest |
| melee | seq 12146 | A, hit A+1 | be >3 tiles from target or pray melee |
| rock/spike | npc seq 12145 + spot, 2 projectiles | A, land A+6 | glyph step within ~5 ticks |
| meteor | seq 12148 + spot 3270; npc 14182 at A+2 over target's tile | A, A+6 | leave the 5x5 by A+5 (run is off) |
| root | seq 12148 + spot 3259; projectile at A+2 | A+5 | glyph, or eat |
| wave | bulk `spotanim_map` (3265/3266), 514 tiles | delay table above | stand off the front's tile on its tick (dodge window: previous tick's tile) |
| streak | `spotanim_map` 3267 lanes | age-based | leave the lane tile before it's hot |
| flare | npc 14179 add + bar (`npc_headbar_timed`) | | kill within the bar |
| glyph | loc `floorkit_summoning03_full01/02` | | step on the middle tile |
| crash | seq 12144 no spot, rock map_anim on own tile | +3 | move off the tile before landing |
| portal | seq 12147 + spot 3276 + line | P | |
| Judge | npc 14180, cast seq 69, surge npcs 13507 + exact moves | | hop stones in the gap of the surges |

## 10. Running the tests

The solver (`test/bosses/yama_solver.lua`, plan `docs/minigames/yama/solver.md`) plays the real route
with no cheats and needs no `--dev`:
```
TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
  ./src/build_yama_combat_opt/torirsserver --scriptrun test/bosses/yama_solver.lua --name y000 \
  --ticks 5400 --session <dir with saves/> --fixture test/bosses/fixtures/yama_kit.ini
python3 docs/rsprox-analysis/yama/tools/yama_audit.py <session dir>...   # the PASS row per run
```
The run name seeds the run (first 12 characters). `TORIRS_SCRIPTRUN_WIRELOG=<file>` adds the wirelog
for `tools/yama_wirekinds.py`.

`test/bosses/yama.lua` (job 1, cheat-driven: `::yamaenter`, `::yamachallenge`, `::yamahp`,
`::yamajudgekill`, `::yamakill`) remains for quick phase checks; it is not evidence of play.

## 11. Solver results and the gap verdicts (2026-11-09)

PASS row (solver.md, written before the first sweep): solo, no contract, no cheats, real route; Yama
killed through both Judges and phase 3; no death; the Judges die to the bot's hits; no style-wrong
protection prayer on a telegraphed regular (audited at each landing from the tick log).

| measure | 48 seeds y000..y047 |
|---|---|
| kills | 47 / 48 (y046 dies at tick 710 in phase 2: three unabsorbed stomp specials 45/57/51 and a melee 37 after its 12 mantas) |
| both Judges killed by the bot's hits | 47 / 47 kills (22..27 landed hits a run, 57 magic / 22 melee) |
| style-wrong prayer on a telegraphed regular | 0 of 3,895 regulars |
| prayer off at a landing (a wave's block) | 25 of 3,895 |
| fight length (challenge to despawn) | 889..1176 ticks, median 1055 |
| mantas eaten | 6.8 a run (max 12) |

| gap | verdict | evidence |
|---|---|---|
| 1 Judge hits and headbars | implemented, matches | 57 / 22 alternate, his frame flips at launch, prayed style lands 0 (4498 [854..875]) |
| 1 prayer flip rule | implemented | the solver reads the frame and wins every Judge |
| 1 stones | fixed: aploc reach 3 | 198/206 recorded clicks at 2..3 |
| 1 shakes | fixed: 5th and 10th cast, cam_reset +1 | add+17..19 (26/26), +40..44 (9/9); reset +1 (30/30) |
| 1 surge columns, fireball tile | Open | approximated |
| 2 magic defence by style / phase | tested: not in the recording; -30 always | yama_accuracy.py table, section 5 |
| 2 stance on the last hit | implemented (`~yama_hit_prepare`), movement untestable | no recorded melee target |
| 3 varp_transmit_audit | fixed 4711, 4715 (and 4716 earlier) | the audit lists no other Yama varp |
| 3 packet-kind wirelog diff | run on a real kill; HUD refresh and ambience differ (Open) | tools/yama_wirekinds.py |
| 4 facing diff, target switching | Open (needs the duo) | |
| 5 portal crossings | unchanged: 21/26 | |
| 5 streak judgement | Open | |
| 5 wave damage, prayer block | fixed: 16..20, block 3 | 25 recorded splats; re-presses at +3..+5 took |
| 5 flare body models | Open (engine: one model per command) | |
| 6 contracts 798, 3329, 3368, 3371 | Open, one AUDIT row each | hp and cycle only |

The solver's own lessons: a regular's spot on Yama's row is masked when the bot's own impact spot lands
the same tick (the row keeps one spotanim), so specials are read from the seq plus the overhead line or
the chat line, and phase 3's styles from the token stream; a wave launched while another is on the map
is found by the jump in the row count; Yama melees anyone in the ring next to him on his attack tick,
so the walk home goes round it.
