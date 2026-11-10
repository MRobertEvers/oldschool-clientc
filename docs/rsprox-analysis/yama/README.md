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

The solver plays the real route with no cheats in the fight. Its tests are built the way the
Theatre of Blood's are: `test/bosses/yama_solver.lua` (solo) and `test/bosses/yama_duo.lua` (duo) are
one call, `QD_ROOT.raid.yama_test({ fixture = "yama_kit.ini", party = 1|2, max_frames = 400000 })`,
with those three fields literal for run.py and the Scripts tab (prepare_scripts.py reads them by
regex); the kit and the play are the driver part `script/plugins/quest_driver/yama_solver.lua`
(`QD.raid.yama_test`, `QD.raid.yama_solve`), listed in `src/plugin/torirs_plugin_drive_parts.h`
like `tob_relay.lua`. Plan: `docs/minigames/yama/solver.md`.
```
# scriptrun (two bots for the duo)
TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> TORIRSSERVER_STAFF_LEVEL=2 \
  ./src/build_yama_combat_opt/torirsserver --scriptrun test/bosses/yama_duo.lua --bots 2 --name d000 \
  --ticks 2600 --session <dir with saves/> --fixture test/bosses/fixtures/yama_kit.ini --dev
python3 docs/rsprox-analysis/yama/tools/yama_audit.py <session dir>...   # the PASS row per run
# the live client lane: one client per seat, the party link between them
QUEST_BINARY=<embed client> python3 tools/raid_gate/run.py --script test/bosses/yama_duo.lua \
  --party 2 --name <seed> --no-build --no-publish
QUEST_BINARY=<embed client> python3 tools/quest_gate/run.py --script test/bosses/yama_solver.lua \
  --name <seed> --fixture <abs path of test/bosses/fixtures/yama_kit.ini> --no-build --no-publish
# the Scripts tab: ./launch run osrs242-scripts, Scripts, Yama Solver / Yama Duo, Play
```

The kit is set by the test's `setup` cheats (`::resetcharacter`, levels, the
recorder's worn kit, supplies, the Voice's progress varbit, the signed contract),
so it needs a staff server: a watched Play resets the account and never loads
`yama_kit.ini` (2026-11-10, it started in Falador with nothing on).

Live lane, 2026-11-10: 0 of 4 seeds killed before the fix below, 7 of 8 after
(the eighth misread a meteor; the scriptrun sweep's one loss was stomps). The
live `api_drive.spotanims` kept only the nearest 512 rows; one shadow wave is
514 tiles, so a second wave never showed and the bot took 16 waves a fight.
`DRIVE_UI_HAZARD_CAP` is now the ring's 2048, as scriptrun answers. The two
lanes are not yet tick-identical: the live fight starts a tick earlier and the
live runs differ from each other on one save.
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

## 12. The owner's batch (2026-11-10): one row each

| item | recorded | was | now | evidence |
|---|---|---|---|---|
| HUD max and the kill | `hpbar_hud_hp/basehp` 2500/2500 at [687], 0 at the kill; the killing splat is what was left (49 at [4740], 55 [5512], 8 [6465], 16 [7617]) with the despawn on that tick | `npc_setheadbarreserve(1)` showed 2499 (Judge 399) and the kill clamped the last hit to hp-1 | no reserve; `param=death_script,yes` on Yama and the Judge (the engine queues `[ai_queue3]` and starts no death); the killing hit is whole and the engine's splat clamps to what was left; phases 1-2 still never reach 0 (the portal ends them) | HUD max 2500 on every run (varp 1682); `[ai_queue3,yama]`, `[ai_queue3,yama_judge_of_yama]` |
| shield splats | 147 `hitsplat_shield_max_me` (44) value 0 on the recorder: 80 on special landings, 59 on regulars inside the 10-tick ward, 8 on hazards | the ward absorbed, but the engine rewrote every 0-value hit to the block splat (26) | `zero_hit_splat` (torirs_server_combat.c): a script's shield-family splat survives a 0 | 18..27 shield splats a solo run, 1,032 on 48 seeds; the ward covers regulars, specials, waves, streaks, meteor, root and now the flares' explosions |
| flares | wiki: P1/P2 one shadow and one flame flare, P3 shadow; 42..45 (P3 23..25) if they charge, the matching glyph takes it; always the max hit from magic and standard ranged; 200% demonbane vulnerability doubles demonbane *bonuses* | any magic hit doubled; explosion scaled by flare hp, no glyph | elements kept per slot (`^yama_v_flare_el`), the glyph of the flare's element takes the explosion, 42..45 / 23..25; magic and ranged always max; only Mark of Darkness's demonbane % and the melee passive double on them | `test/bosses/yama_bonus.lua` (C = 2A on a flare) |
| melee | solo 3268/3322/798, target tile at the end of A-1: 37 melees all within 1 of his 5x5; 7 regulars with the target that close; chase after a melee hit 13 of 21 ticks | always melee when adjacent; the bot never came adjacent | melee on 37/44 when within 1 (`~yama_melee_swings`); the chase as before | 23 melee swings on 48 solo seeds; the bot's home distance drawn from 4498's mage (1: 2%, 2: 9%, 3: 19%, 4: 26%, 5: 17%, 6: 24%, 7: 3%) and Protect from Melee when beside him |
| demonbane bonuses | wiki: Mark of Darkness +25% demonbane damage after equipment, +50% only with the purging staff as the weapon; duration (base magic x 3) ticks, x5 when cast with the staff; the emberlight +70% vs demons | Mark applied before the equipment %, duration x1, line "You mark yourself with darkness." | Mark after the equipment %, the staff checked on the hit, the flare doubling, duration x5 with the staff, the recorded line `@mes_hl_pur@You have placed a Mark of Darkness upon yourself.</col>` | `yama_bonus.lua`: staff off, no Mark 35; staff off, Mark 52 (35 + 50% on a flare); staff on 38, staff on + Mark 76; the demonbane spells' accuracy bonus (+20/40/80%) is not modelled (Open) |
| weapon after the Judge | the recorder swaps back to the staff ([873]) | the solver kept the emberlight | the solver reads the worn container and equips the staff before casting | `staff_back` 2 a run |
| thralls | 68 greater ghost casts in 4498: cast at the summon+1..4, cooldown varbit 1 for 17 ticks, risen +5 with the line, swings every 4 (seq 11101, water strike sound, `thrall_magic_travel` 15/60/15/32 heights 110/124, hit 2 ticks on, 0..3), back to the grave a Magic level of ticks after rising; recast a median 75 ticks after it returns | thralls followed and never fought | `thrall.rs2`: the ghosts swing at the npc their owner last hit (named in `~player_hit_npc_prepare`), the recorded shape, cooldown, duration, lines; skeletons and zombies still do not fight (no recording) | 52..106 thrall shots a solo run (3,623 on 48 seeds) |
| target switch (duo) | 4498 --all-players: 20 switches in 577 attacks, every phase 1-2 switch on the phase's 13th attack (19 of 23 phases that reached it), the line ("A change of pace." / "I grow tired of you." / "Your strikes lack bite.") the tick before; one P3 switch in 13 phases | 1/8 a swing, any phase | the tick before the 13th attack of phases 1-2, 19/23; none in phase 3 | `targetfit` in the session; the top-damage rule fits only 12 of 23 |
| duo route | the partner joins by the Voice's op4 name prompt, mutual friends only | no lane could add a friend | `api_drive.friend_add` on both lanes (`d_friend_add`, `lua_drive_friend_add`), `t.player.add_friend` | 8 duo seeds: both seats join, both kill, both get a drop |
| rewards | K+11 drops for each raider | Yama's timer ran the kill's events and he is removed at K+7: K+9, K+11 and K+15 never ran (no drops, the hud never closed) | the re-added throne's timer runs them | `yama.drop` on every kill (each seat sees its own pile) |
| hazards after the kill | 8 of 8 kills with waves on the map: no splat on the recorder after the despawn | waves, streaks and crash rocks kept landing | the kill cancels them and drops pending events | no hit after the despawn |
| facing | one recorded fight: 133 ticks with a face row in 1,017 (125 player, 4 reset, 3 coord) | a player face every tick, 63 resets in P3 | faced once per target change | duo d000: 84 ticks with a row in 432 (81 player, 2 reset, 5 coord); the recording's re-sends are not every-attack and are not fitted (Open) |
| registers | | `^yama_v_crash` (unused) and `^yama_v_surge` both at 144 | the crash's dead registers removed; the surges own 144..151 | |

Proof (2026-11-10, the same PASS row as section 11, plus the new counts; `yama_audit.py`):

| lane | seeds | kills | deaths | style-wrong prayers | shield splats | melee swings | thrall shots | HUD max |
|---|---|---|---|---|---|---|---|---|
| scriptrun solo | 48 (y000..y047) | 47 | 1 (y016, at the second Judge with no food left) | 0 of 2,880 (14 found the prayer shut by a wave) | 18..27 a run, 1,032 (495 specials, 522 regulars, 15 hazards; the recorder's 147: 80 / 59 / 8) | 23 | 52..106 a run | 2500 |
| scriptrun duo (two seats) | 8 (d000..d007) | 8, both seats alive, both with a drop | 0 | 0 of 414 | 14..27 a run | 0 | 89..124 a run | 2500 |
| live client (`src/torirs_yamalive`, unlocked) | 9 (ylive00..ylive08) | 8 | 1 (ylive02: six wave hits in seven ticks in phase 2) | 0 | 18..28 a run | 1 | 35..111 a run | 2500 |

Still different: the demonbane spells' accuracy bonus (+20%, +40% with the staff or Mark, +80% with
both) is not modelled, so Yama's magic defence (-30) is a fit that absorbs it; Mark of Darkness's expiry
lines are not sent; the skeleton and zombie thralls still do not fight; the Judge-phase surge columns and
fireball tile, the streak hit rule, the flare body models and the contracts' rules are as before (section
8); Yama's re-sent faces are not fitted (ours 81 player faces in a 432-tick duo fight, the recording 125
in 1,017); the live lane is not tick-identical to scriptrun (one live death the sweep does not show).

## 13. Solo and duo from the client (2026-11-10, second batch)

The owner played both from `./launch run osrs242-scripts` (Scripts tab, watched Play): the solo
showed no row after `walk.throne`, the duo's seat 1 sat on the welcome screen and waited at barrier
`yama_partner_in` for good while seat 2 died with "link to the leader closed after 100 boundaries".

| item | found | now | evidence |
|---|---|---|---|
| structure | the tests carried the kit and the run; the duo's barriers were unchecked and the creator passed `yama_creator_in` before landing | the ToB shape: thin tests (`QD_ROOT.raid.yama_test`), the kit and the play in the driver part; the entry is the relay's party enter (friends, barrier `yama_friends`; creator Travels, `yama_entered`; partner Joins and lands, `yama_joined`, passed whatever the result; the creator then sees the partner); every barrier a ledger row, one that does not pass ends the seat's run (FAIL, `t.finish(1)`) | ledgers below |
| the start | the fight began when the creator arrived | Phase 9: each seat on its tile, `yama_ready`, `::synctimers` on every seat, `yama_synced`, then the creator sits; `yama_end` before the finish so the creator's finish (which closes a watched party) waits for the partner's rows | the same barrier rows on both seats, the same tick |
| a dead partner | `t.party.barrier` waited out its timeout (400 ticks, four minutes watched) on a leader whose member had gone | `api_drive.party()` gives a world's host `members_joined` / `members_open` (torirs_plugin_drive.c); the barrier ends at once, `refused`, when a member that joined is gone | the first watched duo after the change failed its join and ended in 91 ticks with `duo.partner_in` FAIL and the party closed, instead of hanging |
| the welcome screen | a person's login opens it (`login_shows_welcome`); a fresh start's leader and a launched member both played behind it | `QD.session.welcome_play` presses its Play after the start (core.lua), as the person would | `watch.start ... pressed the welcome screen's Play` on both seats |
| the name prompt | the partner's Join typed `yamaduo2` in one frame and Enter; the prompt showed only the last letter (`f*` for `abcdef`): every key of one frame read the same `%varcstring359`. And the client sent no RESUME_P_NAMEDIALOG at all: 66 client-state commands were answered as `group_getvar` (fixed in the tree by fff992ccf at 08:40; the owner's 09:28 binary predates it) | `t.text` types one character a frame (ui.lua) | `<- op 33 RESUME_P_NAMEDIALOG 7 byte(s)` and "That player is not an online mutual friend on this world." for `abcdef`; `duo.join ... landed 6431,66` on every live seed |
| run | a watched Play's own account had run off (yama_kit.ini has it on); walking, the solo reached the glyphs late and died in phase 1 (69 and 63) | the solver presses the run orb when run is off (`run.on` row) | `run.on PASS p1 pressed the run orb` |
| progress rows | rows only at the end of the fight | `yama.p1`, `judge.1`, `yama.p2`, `judge.2`, `yama.p3`, `yama.fought` as each begins, with hitpoints and prayer | the ledgers below |
| two seats, one stream | both seats drew the same home tile and stood on it together | seat n's stream is the run's, then n | yld08 (both dead on one tile) before, 0 such after |
| a glyph out of reach | a seat ran from the north side through him to the south glyph and was one tile short when the special landed (yld08, ydu012) | out of reach in the ticks left: stay, eat above 80 | 16/16 solo and 16/16 duo after |
| lockstep grade | gate.py's boundary pattern was anchored to the line, and another thread's stderr ("Failed to load map terra") landed inside the leader's boundary 54 line: a FAIL "at boundary 55" with every digest equal | the pattern finds the line anywhere (gate.py `PARTY_TRACE_RE`) | yld02 re-graded: 595 boundaries, the same tick and digest on both seats |

Proof (`src/torirs_yamalive` = this tree's client, built after fff992ccf):

| lane | seeds | result |
|---|---|---|
| the Scripts tab's Play, headless (TORIRS_DRIVE_AUTOPLAY standing in for the button; profile osrs242-scripts' env and args; `start = fresh`, party 2, launched member), before the per-seat stream and the glyph reach | wd2 | Yama killed at tick 600, both seats alive, both drops; 24/24 and 21/21 rows PASS; the party closed and seat 2 exited 0 |
| the same, solo (`start = reset`) | ws2 | Yama killed at tick 914, alive, the drop; 15/15 rows PASS, a row at each phase as it began |
| live party via `tools/raid_gate/run.py --party 2` (unlocked), after the last two solver changes | yld09..yld14 | 6/6: both seats kill, alive, a drop each; `party.lockstep` PASS on every seed |
| scriptrun solo | yg000..yg015 | 16/16 |
| scriptrun duo (`--bots 2`) | ydg000..ydg015 | 16/16 |

## 14. Everything that differs from the recordings (2026-11-10, third pass)

| item | recorded | was | now | evidence |
|---|---|---|---|---|
| Dark Demonbane accuracy, measured | each cast's outcome is the area sound on its tick at his tile: `dark_demonbane_hit` (accuracy passed) or `spellfail` (a splash shows no splat at all, which left 307 of 827 casts unresolved before). 587 casts carry exactly one: with Mystic Might (on for 489 of them) phases 1-2 hit 262 of 374 (0.701), phase 3 92 of 115 (0.800); no dependence on his face target (0.725 / 0.712) | read from splats: 0.74..0.87 | `tools/yama_accuracy.py` reads the sounds | validation: a hit sound has the recorder's damage splat 3 ticks on 376 times of 421, a splash none 138 of 165 |
| the demonbane spells' accuracy | wiki: +20% against demons, doubled by the purging staff, doubled under the Mark (+80% with both), doubled again on a 200% target (flares) | not modelled | `~pvm_spell_hit_roll` (player_magic.rs2) | |
| Yama's magic defence | fitted to the measured accuracy against the recorder's roll (99 Magic, Mystic Might, +157 magic attack, +80%): +47 in phases 1-2, +10 in phase 3. The wiki's +60 against a magic-using target is 1.4 standard errors from the phase 1-2 fit; its phase 3 +81 is five away (rejected). Phase 1 against phase 3 differs by 2.3 standard errors | -30 | +47 / +10 (`~yama_magic_defence_bonus`); a melee-using primary target (wiki -30) is not recorded and not modelled | 16 solo seeds: phase 1 0.672, phase 2 0.700, phase 3 0.817 (was 0.811 overall) |
| Mystic Might | the recorder prays it on 489 of 587 casts read (40 activations) | the solver never did | the solver turns it on while casting (prayer 15 or more) | |
| Dark Demonbane's timing and sounds | no projectile: the splat 3 ticks after the cast at every distance 1..9 (376 of 376); `dark_demonbane_cast` synth to the caster; `dark_demonbane_hit` (or `spellfail`) at the target's tile, delay 60, on the cast tick | the hit a tick after the cast; no cast or hit sound | `spotanim_proj,null,0,0,60,0,0,0,0`, `sound_cast`, `sound_impact` on the spell's row (magic_combat_spells.dbrow) | cast to hit_npc 3 ticks on 1,103 of the casts in 16 seeds; both sounds on the live lane (yld15) |
| impact graphics on Yama | none: 0 of 827 casts (the hit graphic shows on the Judge 76 times and the flares 87) | `dark_demonbane_hit_spotanim` / `failedspell_impact` on him, 118 + 17 a run | `param=spell_impact_hidden,yes` on Yama | 0 on every run since |
| demonbane damage on Yama | max 58 over 376 hits; the values come in pairs (16,17 19,20 ... 54,55 57,58) | max 57 (38 x 1.5) | unchanged: neither x1.5 (57) nor the wiki's +20% for Yama (x1.6 = 60) gives 58 | **Open** |
| shields and glyphs per kill | eight completed fights: 11.2 shield splats, 16.6 damage splats a fight on the recorder; specials warded 78 of 82 | | | 16 duo seeds (seat 1): 11.5 shields, 18.3 damage splats a fight; specials warded 164 of 178 (92%): matched |
| facing | a player face follows 285 of the 293 ticks the target stepped (201 the next tick, 84 the same), one per step; 158 player faces a thousand fight ticks | one face per target change (92% of steps faced anyway, by the combat facing) | phases 1-2 re-face on each step of the target (`^yama_v_faced_at`, register 159); phase 3 keeps one face per target (re-facing there left a reset every third tick) | 528 of 555 steps faced (95%), but 298 the same tick and 230 the next: the npc/player phase order differs (**Open**) |
| ambientsound_stop | every 5 ticks from login, everywhere, in all 18 rev-241/242 recordings checked; no ambientsound_start in any | not sent | not changed: an engine-wide rule, not Yama's, and sending it would cut every ambient bed our server starts | **Open** (engine) |
| HP HUD re-send | `if_setcolour` x3 + `hp_hud_open` on 81% of fight ticks (1..3 a tick); also in other bosses' recordings (4480: 82 of 114 HUD ticks) | sent once | not changed: the hpbar_hud engine command's behaviour, not Yama's | **Open** (engine) |
| Mark of Darkness expiry lines | none recorded (the staff's x5 Mark outlasts every fight) | not sent | wiki: "Your Mark of Darkness is about to run out." 10 ticks before, "Your Mark of Darkness has faded away." at the end; a recast keeps an older cast quiet; the fade animation is not sent (id unrecorded) | probe: a 297-tick Mark warned at 288, faded at 298 |

Not reached: portal crossings, the streak hit rule, the Judge's surge columns and fireball tile, the flare body models (engine), the skeleton and zombie thralls, ylive02, live against scriptrun tick identity (**Open**).

Exit with a party up: the driver shuts down before the Scripts plugin's `on_stop` closes its
party, and `drive_launch_queue` then queued onto a cleared `g_app` (SIGSEGV on exit, seen on a watched
duo Play with `quit`). It now answers refused (torirs_plugin_drive.c).

Shared driver changes, one seed each: the ToB trio `tob_bloat --party 3` (tbchk1) 44/44, lockstep
PASS; the watched quest `_ratcatchers_refusals` (fresh start, welcome screen pressed) 41/41. Two other
watched quests fail for reasons the driver changes do not touch: Cook's Assistant has no coins
(`fresh_lumbridge.ini` lost its kit in d5b504eda) and Rune Mysteries cannot reach the Duke, the same
on run.py (rmchk1).

Proof (this pass): the Scripts tab's Play on the final client, headless: duo wd4 (fresh, launched seat 2) killed at 649, both seats alive, 24/24 and 21/21, the client exiting 0 with its party up; solo ws4 (reset) killed at 1141, alive, 15/15. Scriptrun solo 16/16 (yz000..015), duo 14/16 (ydz000..015: two seats killed by an unwarded special, 98 to 0 in three ticks, and by a phase-2 burst; the fights are longer with the measured accuracy: median 933 ticks solo, was 828); live duo yld15 43/43. Shared driver changes: one ToB trio (`tob_bloat --party 3`, tbchk1) 44/44 with the lockstep PASS.

## 15. The rest of the list (2026-11-10, fourth pass)

| item | recorded | was | now | status |
|---|---|---|---|---|
| live against scriptrun tick identity | | the lanes reached the throne 2 ticks apart solo, 28 duo (scriptrun's name prompt is not the client's) | the solver waits at the throne for world tick 85 (solo) / 125 (duo), then `::synctimers`: with the same name (live `--name <seed>_p1`, scriptrun `--name <seed>`) both lanes spawn Yama on tick 89 and the logs are identical from the spawn to 369, 378, 294, 272 (yid001..004) | **Open**: where they part the live client is a tick later: the Judge's stepping stone (`[aploc1,yama_steppingstone]` 370 against 371, yid001; 378, yid002), a cast (272 against 273, yid004), a cast against a step (262 yid000, 294 yid003). Live-only deaths 2 of 5 (yid000 on the stones at 373, yid002 in phase 3 at 1039). The client sends a world op the frame it is pressed; why the server sees it a tick late is not found. run.py names a solo account `<name>`, scriptrun `<name>_p1`, so the RNG differs unless the live run is given the `_p1` name |
| ylive02 | | died at 450 at 6429,101: 25, 20, 19, 17, 16, 19, 2 from 444 to 450 | not reproducible: the run is on the code before the accuracy and timing passes, and a live run cannot be replayed onto this code | **Open**; the live-only deaths now are the row above's |
| portal crossings | 13 phase 1 portals with the crossing read off `hpbar_hud_hp` <= 1666: 3 portal at the first attack slot after it (3, 6, 8 ticks on), 10 at the next slot (the first slot 1..7 ticks on) | portal when his HP at the previous attack is at or under the threshold | unchanged | **Open**: no delay or decision-tick rule separates the two groups (3 ticks is in both); ours matches the 10 and is one attack late for the 3. Phase 2 cannot be read: after the first portal the HUD shows the Judge |
| streak hits | 14 times the recorder (4498) had a fire-wall tile sent where he stood or stepped: hit 10 of 10 times he was on it at the end of the send tick and still there at the end of the next (the splat the next tick: 7 warded zeroes, 16, 17, 18), 0 of 4 otherwise ([975] on it at the end of the send tick and off the next; [2870], [4516], [10157]) | the tick after the send, on it at the end of the send tick | unchanged | **Open**: ours is 13 of 14, the one off being [975]. "Still there after the next tick's move" cannot be asked from the hazard tick, which runs before players move (no hook after movement); checking a tick later would put every streak splat a tick late |
| Judge: fireball and surges | the fireball lands on the raider's tile at the start of the cast tick (157 of 157); fireball when he stands at dz -4..+1 of the Judge, surges at dz -7..-21; surges in his x-1..x+3 (29/26/35/30/32), the row below him; 70 casts with two surges, never in one column, 12 with one | fireball the same; surges at x-2..x+2, one or two at even odds, two could share a column | two picks of the five columns x-1..x+3, one surge when they land together (`~yama_surge_spawn`, yama_judge.rs2) | matched (the single-surge share is 1 in 5 against 12 of 82 recorded) |
| flare body models | `[body_customisation]` 48 times on `yama_voidflare`: models 17003, 17006, 17009 on one flare of each pair | dropped | unchanged | **Open** (engine): `npc_bodymodel` holds one model and both encoders (torirs_server_encode.c, rev 239 and 241 blocks) write a list of one. Three models need a new opcode (compiler, command kinds, coverage table, encoders), and a pack using it aborts every server binary built before it, so it is the owner's to schedule |
| facing timing | on a target's single steps (no step the tick before or after): the face the next tick 138, none 36, the same tick 0 | | unchanged | partial: ours (16 duo seeds) the next tick 123, the same tick 10, none 6, both 1. The earlier "ours the same tick" count came from runs of steps; the residual is phase order (npcs act before players move) |
| 58 against 57 | demonbane hits on Yama come in pairs, 6/7, 9/10, 12/13 ... 54/55, 57/58 (376 hits) | max 57 | unchanged | **Open**: no single multiplier, addition or two-step floor reproduces the pairs with at most one miss; the closest, floor(floor(r x 1.5) x 1.07) over r 0..37, misses 2 |
| thralls | see below | ghosts only; skeletons and zombies stood still | all three kinds | matched where recorded; tiers below greater by the greater's shape (no recording) |
| deaths in the solver's runs | the recorder never let a flare reach the end of its bar; 48 of 55 meteor landings absorbed by a glyph, the other 7 hit 80, 56, 36, 18 (section 2) | 14/16 duo (ydz): an unwarded special (86 on seat 2, ydz015), a phase-2 burst (18, 18, 17, then 41 + 14, yd5003) | three kinds: (1) the meteor with no glyph lit: the seat stood still at two tiles and took 80 (the watched duo wd5, seat 1 at 626; seat 2 killed Yama alone): the solver now walks out to four tiles from the meteor's tile when no glyph is under it (`meteor_escape`, its target's run is off so it reaches three, 40): it fired 34 times in 17 of the 32 seat-runs of yd8; (2) a run for a glyph that lands late: the special lands first and the seat is rooted in the wave lanes (ys8002: 33, then 16, 17, 17, 19, 11 at 6429,101, the tile ylive02 died on; yd8014 seat 2); (3) both flares of a pair exploding after the seat ate instead of finishing them (yd6004 both seats 43 + 16 and 45 + 21; ys7007 43 + 29). Scriptrun this pass: solo 30/32 (ys6 8/8, ys7 7/8, ys8 15/16), duo 30/32 (yd6 7/8, yd7 8/8, yd8 15/16) | partial: (1) fixed in the solver; (2) and (3) **Open** (solver): the glyph's reach rule counts Chebyshev tiles at two a tick and the walk around him is longer; the flares lose to eating. The flare explosion's damage is the wiki's (Y49) |
| `ambientsound_stop` | every 5 ticks from login, `fade=true`, in all 21 rev-241/242 recordings read, wherever the recorder is, and no `ambientsound_start` in any: rev 242.1 4480, 4490, 4491, 4493, 4494, 4495, 4496, 4499, 4500, 4501, 4502, 4503 and 4498 (Yama, 2,269 sends; the rest are Keldagrim's dwarves, gnomes, the Stronghold cave's demons, open country); rev 241.1 4392 (Chambers of Xeric), 4433 (King Arthur's knights), 4444 (Varlamore), 4446 (the Digsite), 4451 (The Feud's villagers), 4456 (Death Plateau's trolls), 4462 (Ghosts Ahoy's villagers), 4464 (swans and ducks). Rev 237 (4043, Araxxor) sends none | not sent | not changed: the owner's decision | **Open** (engine) |
| HP HUD re-send | `if_setcolour` x3 (`hpbar_hud` 303:14..16) + `hp_hud_open` re-sent on most ticks a boss HUD is up: Yama 4498 (6,802 `hp_hud_open`, 81% of fight ticks, 1..3 a tick), Chambers of Xeric 4392 (359, sparse over 3,673 ticks), Araxxor 4043 (8,724), and the HUDs in 4480 (120; 82 of 114 HUD ticks) and 4444 (95; 65 of 225) | sent once | not changed: the owner's decision | **Open** (engine) |

### Thralls, all three kinds (OSRS-Content `skill_magic/scripts/spells/thrall.rs2`)

Recorded casts: greater ghost 68 (4498), greater skeleton 5 (4392) and 10 (4292), greater zombie 1
(4292 [2108]); swings by other raiders' thralls in 4294, 4308..4316 (rev 241). Counts are greater
thralls'.

- The cast: `human_spellcast_resurrect`, the kind's cast spot (1873/1874/1875) and synth
  (`resurrect_greater_{ghost,skele,zombie}_cast_and_fire`), the spawn spot and seq, six prayer points
  (79->73, 59->53, 41->35, 25->19, 81->75), the used varbit = the spell's place in the book (ghost 6,
  skeleton 7, zombie 8; lesser 0..2 and superior 3..5 by the same order). Was: ghosts only; the
  others printed "You resurrect a thrall to fight at your side." and never swung.
- A thrall still out when the next is cast goes back with its line, the active varbit 0 and
  `resurrect_end` on the cast tick (4292 [2108]). Was: deleted silently.
- The lines: "You resurrect a greater {ghostly,skeletal,zombified} thrall." five ticks on, "Your
  greater ... thrall returns to the grave." The npc goes at cast + Magic + 3 and the line, the varbit
  and `resurrect_end` follow a tick later: Magic 99 -> +103 (4392 three casts, 4498), 123 -> +127,
  125 -> +129 (4292). Was: npc and line together at cast + Magic + 2, no sound.
- The swing clock: every tick it looks; it swings when four ticks have passed since its last swing
  (the cast + 4 for the first: no recorded first swing is earlier) and it has a target in reach. A
  target change mid-clock swings as soon as it is noted (4392 [651] -> [658]). Was: a fixed 4-tick
  grid from the cast that skipped a whole step when the target came late.
- Facing: from its first swing at a target it faces the target's middle every tick (4498 [777..],
  4292 [2116..2129]); the middle is the south-west tile + size / 2 (Yama +2,+2, 848 of 848). Was:
  faced his south-west tile on the swing tick only, turned back to its owner between swings.
- Ghost (4498, rev 241): its own graphic slot cleared (`spotanim -1` height 96), the swing, water
  strike at its tile (range 1 in rev 242, 12 in rev 241), `thrall_magic_travel` 15 -> 25 + 5d (d =
  Chebyshev to the target's south-west tile; 1,596 of 1,599 swings), the hit E / 30 ticks on (+1 for
  E <= 55, +2 from 60; 0..3). On a target that shows spell impacts, `thrall_magic_impact` with the
  landing (rev 241, 615 of 744 swings; Yama hides it). Rev 241's impact sound (`waterstrike_hit`) is
  in no rev-242 swing (0 of 855) and is not sent. Was: endtime 60 and the hit +2 at every distance.
- Skeleton (4392, 4312, 4308..4316): the cleared slot, `skeleton_update_champion_attack_thrall`,
  `longbow` 20 cycles late (range 1, by the ghost's rev-242 change; rev 241: 12),
  `thrall_ranged_travel` 57 -> 67 + 5d (1,654 of 1,709), the hit E / 30 ticks on (+2 to 87, +3 to
  117, +4 from 122); on the target a cleared graphic slot (1,540 of 1,713) and its defend anim (43:
  the npcs that have one), both with the landing.
- Zombie (4292, 12 swings): no missile; `zombie_update_attack_normal_thrall`, `zombie_attack` at its
  tile, the hit the next tick (0..3). It leaves its owner for its target and walks to the nearest
  tile in reach of the target's footprint (from 3681,5406 to 3684,5411 for Akkha's shadow at
  3685,5412), swinging the tick after it arrives ([2115]/[2116], [2137]/[2138], [2164]/[2165]); back
  to its owner when the target is gone.

Probe (thrall_probe, two hill giants at Lumbridge): the skeleton swings at cast + 4 then every 4, at
d 1 its arrow lands at 72 and the splat follows 2 ticks on; the zombie replacing it brings the
skeleton's line and `resurrect_end` on the cast tick, walks one tile and swings the tick after,
splats a tick on, stays at its giant while its owner walks off, and goes at cast + 102 with its line
and sound at + 103; all four lines verbatim. On Yama the ghost's flights now run 45..80 cycles (60
most often) and its hits land 1 or 2 ticks on by distance.

Not reached: the first swing waiting on the owner's first landed hit (4498: the first swing comes 1..3
ticks after the owner's first demonbane after the cast, at cast + 4..10; ours takes the target when
the owner's attack is prepared, so it swings at cast + 4 when he attacks at once) (**Open**).

Proof (this pass): scriptrun solo 30/32 (ys6, ys7, ys8), duo 30/32 (yd6, yd7, yd8); the thrall probe
above. The Scripts tab's Play on the private packs, headless: solo ws5 15/15 (the kill at 942, alive),
duo wd5 failed (seat 1 stood in an unwarded meteor at 626, seat 2 killed Yama alone), which is what the
meteor escape is for; after it duo wd6 24/24 on seat 1, 17/17 on seat 2 at its last status (the kill
at 840, both alive) and solo ws6 15/15 (the kill at 1118, alive); every client exited 0 but wd5's 1.
