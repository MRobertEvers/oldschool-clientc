# Jewelled crabs: solver plan (phase 4)

Unit `crabs_thieving`, 2026-10-18. The tick spec and audit are
[`crabs_spec.md`](crabs_spec.md); this file is the plan for
`script/plugins/quest_driver/cox2_crabs.lua` (a `QD.T2` room, shaped like
`cox2_olm.lua`) and `test/raids/cox_crabs.lua`. No code until F3 (`::coxtest 5 <n>`)
is in and the content of the spec's audit is green on a probe.

## The strategy a strong solo plays

Sources: Synq 0:20:40-0:25:34, ThePerfectG's bad-crabs solve, the wiki Strategies,
The Academy. A solo has three crabs and one body, so the method is **park and
paint**:

1. **Read the layout.** Variant (CCW "good", THRU/CW "bad"/"rare" in the players'
   names -- which name is which is a probe, it depends on the turn) and turn give,
   in template terms, the focus tile, the orb's first heading and the four crystal
   tiles. Everything after is a route plan in that frame.
2. **Plan the bounce chain per crystal.** For each crystal, the crab tiles that turn
   the orb's path onto it: a crab on the orb's line, one tile before the turn point,
   turns it clockwise. A crystal reached with no bounce needs a white orb (the black
   crystal); one bounce with a painted crab sets the colour (yellow <- blue/magic,
   magenta <- green/ranged, cyan <- red/melee or smash). Order the crystals so each
   parked crab is reused (ThePerfectG: hammer two crabs in place, drag the third,
   re-hammer before the stun lapses).
3. **Lure.** A crab aggros at 3 tiles and follows (it chases, `givechase=no` means it
   does not path round walls far). Pull it so its tile lands on the planned bounce
   tile: the crab steps onto the tile beside me, so I stand one tile past the target
   on the crab's line. Stack two crabs by running back and forth through them
   (Synq 0:22:56).
4. **Park.** Smash (op3) with the pack hammer / DWH: 50-60 ticks of stun, red for 8.
5. **Paint at the right moment.** The colour lasts 8 ticks, so paint the bounce
   crab when the orb is <= 8 ticks from it (one style attack: magic for blue,
   ranged for green, melee for red; no attack delay on crabs, so switches are
   tick-to-tick). A white bounce needs the crab DIM: let the smash's red lapse (8
   ticks) before the orb arrives.
6. **Kill a bad orb.** If the orb took the wrong colour, recolour the next crab it
   will hit (a different colour destroys it) or step into it (25% of current HP --
   only at high HP, never below 8).
7. **Keep out of the line.** Never stand on the orb's next tile unless killing it;
   move diagonally round a crab to leave it in place (Synq).

Prayer: Protect from Melee whenever a crab is within 3 tiles and not stunned
(fairly accurate melee, 4 ticks).

## Perception (api_drive, client-side only)

| need | verb | status |
|---|---|---|
| crabs: tile, colour, stunned | `api_drive.npcs` rows: `server_x/z`, `npc_id` (7576 dim / 7577 red / 7578 green / 7579 blue), `spotanim_sent_id` = 245 and `spotanim_tick` (stun start; the stun timer is the plugin's: 50 ticks certain, then 0-10 unknown) | exists |
| orb: tile, colour, heading | `api_drive.npcs` rows `npc_id` 7580-7583; heading = the last tile step (first heading from the spec) | exists |
| crystals lit | `api_drive.locs` / `t.world.loc_near("raids_lasercrabs_smallcrystal_white")` | exists |
| big crystal gone | `t.world.loc_near("raids_lasercrabs_bigcrystal_5")` absent | exists |
| orb bursts | `api_drive.spotanims` (119/122/128 bolt, 157/160/163/166 wave) | exists |
| crab target (who it attacks) | `facing_entity` on the crab row | exists |
| the room's variant and turn | no packet says it; derive from the crystal and focus tiles seen (four crystal tiles fix variant + turn uniquely) | derived (no verb needed) |
| **missing** | none known. Engine work instead: a renewing freeze (A9) and the combat funnel's crab no-delay/stop rule (A15) -- without them the bot's re-smash and paint-then-switch timings differ from OSRS | engine |

## Mode machine

`ENTER -> SURVEY (variant, turn, crab tiles) -> for each crystal in plan order:
LURE(crab_i -> tile) -> PARK (smash) -> WAIT_ORB -> PAINT (at orb distance <= 7 ticks
to the bounce) -> CONFIRM (crystal white) | RECOVER (bad orb: kill it) -> DONE (big
crystal gone, walk to the exit Passage)`.

Re-smash guard: a parked crab whose 245 tick is >= 48 ticks old is re-smashed before
the next orb uses it.

## Kit

The CoX solo kit plus: a hammer in the pack (or DWH wielded), one ranged weapon and
a magic weapon (staff/trident/Tumeken's shadow -- with a magic bonus above -64),
melee main hand. Never drop equipment.

## Threat field

| hazard | window | damage | term |
|---|---|---|---|
| orb on my tile | each tick the orb's next tile is mine | ceil(0.25 x min(99, HP)) to me and anyone within 1 | forbid unless RECOVER chooses it |
| crab melee | crab within 1 tile and not stunned, every 4 ticks | unpublished max (~10 NR); 0 under Protect from Melee | expected HP |

## Probes (first run prints)

1. Each variant x turn: the four crystal tiles seen vs the spec's table; the orb's
   first heading.
2. Smash: 245 on the crab, stun length (ticks until it moves), red for 8.
3. A painted bounce: the orb's colour after it; a wrong-colour bounce destroys it.
4. Solve sequence ticks: crystal lit -> next orb; crabs gone +1; crystal gone +2.

## PASS

Room solved (all four small crystals white, the big crystal gone) and the player
through the exit Passage, alive, with **no orb hit taken** and fewer than 5 wasted
orbs after the first crystal (Playing with Lasers is "none": tracked, not required),
within 150 s of entering; 0 `abort` / `script error` lines; on 48 `::coxtest 5 n` seeds
(every variant and turn).

## Round 2 result (2026-10-18)

`script/plugins/quest_driver/cox2_crabs.lua` + `test/raids/cox_crabs.lua`, the
whole raid's kit (no hammer: the solver takes the room's spawn; Auto Retaliate
turned off through the combat tab). The plan above, as built:
- survey: the client's route to every template tile (96 a tick);
- the chain search: breadth-first, fewest NEW crabs, crabs already held are
  fixed bouncers (free), resumable (1200 nodes a tick), re-planned only when
  the held crabs no longer bring the orb home;
- park LAST chain crab first (the orb keeps its old line until the first one
  stands); lure = walk S -> Q through P only in a clear window, smash a crab
  SETTLED on P (there two ticks, me still and cardinal);
- fire: stand off the planned path (and its neighbours), paint the coloured
  chain crab 2-6 ticks before the orb's bounce at it, never with an op that
  walks me;
- hazards: the orb's path with all crabs, held crabs only, each free crab
  removed, a free crab stepped onto each of its next tiles; the same for the
  next orb from the sculpture at its earliest and latest birth.

48 seeds (`cr0..cr47`): 45 PASS, 3 FAIL, every one "1 orb hit" (all rooms
solved, exits open): a free crab stepping onto a newborn orb's first tile
beside me. Solve 230-880 ticks in the room, median ~310 (slower than a human:
re-smash does not renew a stun here).

## Round 3 plan: one fixed solution per variant (2026-10-19)

Owner: "a special algorithm for each crab room layout"; "Dragon warhammer can be
used ... as the hammer"; "no runner should ever drop real gear".

Every room of a variant is the same puzzle in template coordinates: crab spawns
(`~cox_crab_spawn_tile`), the four small crystals (`~cox_crab_crystal_pos_tile`),
the focus and the orb's first heading (`variant + 1`: E / S / W) are fixed; the
turn is undone by `QD.COX.template`. The chains below were derived offline from
the surveyed walk map of each variant (the solver's `crmap` marks, cr1 / cr0 /
cr3) with a copy of `CR.sim` (scratch `plan.py`: every chain of up to three dim
crabs to each crystal, then an order picked by hand so each step ADDS crabs to
the held ones, or replaces only the crab parked first -- the one whose stun
lapses first). Crystal index = `^cox_crab_crystal_pos_tile` position + 1 (1
black <- white, 2 yellow <- blue, 3 magenta <- green, 4 cyan <- red). Tiles in
the orb's path order; `*` the painted crab (red = the smash, green = the
crossbow, blue = the trident).

| variant | step | crystal | crabs (path order) | orb ticks |
|---|---|---|---|---|
| 0 ccw, focus (13,9) E | 1 | 4 cyan | (25,9)*red (24,7) (22,8) | 20 |
| | 2 | 1 black | (25,9) (24,7) (18,8) | 21 |
| | 3 | 3 magenta | (25,9)*green (24,7) (17,8) | 28 |
| | 4 | 2 yellow | (25,9)*blue (24,7) (14,8) | 35 |
| 1 thru, focus (22,19) S | 1 | 4 cyan | (22,10)*red (8,11) | 22 |
| | 2 | 1 black | (22,10) (14,11) [+ (8,11) idle] | 16 |
| | 3 | 3 magenta | (22,16)*green | 14 |
| | 4 | 2 yellow | (22,14)*blue (20,15) (21,17) | 9 |
| 2 cw, focus (18,14) W | 1 | 3 magenta | (11,14)*green | 11 |
| | 2 | 1 black | (11,14) (12,16) (14,15) | 12 |
| | 3 | 4 cyan | (7,14)*red (8,24) | 23 |
| | 4 | 2 yellow | (7,14)*blue (8,16) (10,15) | 17 |

Variant 0 is the hook every crystal needs (east on z=9, south at x=24, west on
z=8, north up the crystal's column): two crabs park once at (25,9)/(24,7) and
the third slides west (22 -> 18 -> 17 -> 14). Variants 1 and 2 are two groups
each; between them the crab parked first lapses first and is led on.

At run time (`CR.table_check`, after the survey): the three crabs seen on entry
each within 3 tiles of a distinct table spawn, a loc on each crystal tile, every
chain `CR.sim`-checked on the surveyed map to its crystal, every chain tile
approachable. Pass -> `cr.path = "table"`: the current step is the first unlit
row; its missing crabs are parked last-first (the round-2 lure); all held and
the sim (held crabs only) brings the orb home -> fire; otherwise wait off the
path. Fail -> the round-2 breadth-first search, logged `crabs table: off (...)`.

The smash weapon: a wielded blunt weapon or a hammer in the pack. A kit with
the dragon warhammer (the relay's) WIELDS it for every smash (one-handed: the
defender stays, the tentacle goes to its pack slot, no free slot needed) and
never takes the room's hammer; a crossbow / trident paint swaps it out and the
next smash swaps it back. Only a kit with no smash weapon takes the spawn.
Nothing brought in is put down (`QD.COX.may_drop`).

Prayer: Protect from Melee only while a free crab is within 2 tiles, turned
OFF (`intents.pray_off`) when none is within 3; off before the room ends.
Round 2 never turned it off (425 lit ticks a room).

## Round 3 result (2026-10-19)

Kit: the relay's arrival kit (`QD.COX.SOLO_KIT` spelled out in
`test/raids/cox_crabs.lua`, no anglerfish, 27 in the pack, no hammer). The
test reports `CRABS_REPORT variant turn ticks prayer_spent prayer_lit path`.
48 seeds, the same kit, round 2's solver (`base`) vs this one (`r3b`):

| variant | base green | base median ticks / prayer | r3b green | r3b median ticks / prayer |
|---|---|---|---|---|
| 0 ccw (19 seeds) | 15 | 569 / 82 | 16 | 786 / 90 |
| 1 thru (20) | 20 | 259 / 35 | 20 | 203 / 24 |
| 2 cw (9) | 9 | 310 / 43 | 9 | 325 / 34 |
| all | 44 | | 45 | |

`path table` on every seed (the run-time check passed on every variant and
turn). Every smash wielded the dragon warhammer; no hammer taken, nothing
dropped; every passing room left on the tentacle + defender, prayers off.

Failures (all variant 0): cr16, cr46 one orb hit each (the east-wall corner,
three crabs and me in a 3x3 while the hook is built); cr4 out of time on the
last step, leading a crab onto (14,8) (two approaches only, the W one ends
under the sculpture). Variant 0 did not move toward green over two sweeps
(r3a, last-first park order: 15/19, median 625; r3b, (24,7) parked first:
16/19, median 786); the search with this round's prayer and smash changes
was 14/19, median 466 (r3c). Why: every v0 crystal needs all three crabs,
so each step waits for a stun to lapse (re-smash does not renew, A9) and
re-leads it; the cyan chain has no alternative and sits in the corner. Next:
lead onto (14,8) from the south with the newborn orb's first tile forbidden
only while an orb is due, and park the corner crabs from outside the 3x3.

Prayer: lit only near a crab now (Protect from Melee off when none within 3);
variant 1/2 spend 24/34 points. Variant 0 is still ~90: crabs follow me the
whole slow room.

## Round 4 plan: ccw robust, honest stuns (2026-10-19)

Evidence first (relay r15 raid1/raid4, both ccw turn 3, both from a full
start):
- `crabs table: off v0 t3 (crabs seen 2)`: the relay arrives after the crabs
  have wandered (and one can be out of view), so the spawn-tile check fails --
  6 of 7 relay entries went to the search (`crab at 18,9 off the spawns` ...).
  The harness (`_cox_room_budget.lua crabs<n>`) and `cox_crabs.lua` enter a
  fresh room: 24/24 and the table on everywhere (ccw t3 = crabs4/11/16/20 all
  green; cr4/11/16/20/26/31/38/45/46 in `cox_crabs.lua`). The map is the same
  (raid1's `crmap` = cr1's), so it is NOT the template<->world mapping.
- The search then led a crab onto (14,8) from the west: Q = (13,8), the
  sculpture's doorstep. A free crab stood on (14,9) and turned every orb south
  onto (13,8) -- Q is forbidden every tick, stage `q` has no time limit, and I
  stood on (14,8) between two free crabs from t709 to the 4000-tick limit
  (399 of 411 damage from crab 7576 once prayer ran out at t3490).
- Stuns are read wrong: `npc_freeze` does not renew a frozen crab nor freeze
  one inside its 5-tick immunity, but the plugin restarts its timer on every
  re-sent 245 (the fire's red paint, a smash on a crab still frozen at 48-60).
  Measured (crabs6, step 1, 478 ticks): (22,8) re-smashed at age 54, read as
  held for 47 more, walked away 3 ticks later; most park time is these
  phantom holds and fetching a far crab that never woke (t133-166).

Changes (all in `cox2_crabs.lua`):
1. **Table check without spawn tiles**: crystal locs + every chain sim-checked
   + every tile approachable. The variant comes from the room lookup.
2. **Honest stun per crab slot**: a 245 starts a stun only if the crab is
   smashable (never stunned, or seen off its stun tile >= 5 ticks ago, or the
   stun >= 66 ticks old). Held = < 50 and not moved; "maybe" 50-60; free after.
   A crab in the maybe/immune window is never smashed (the op is void) and is
   not a lead candidate (it cannot follow).
3. **Re-park in place**: a chain crab on its tile whose stun runs out while I
   stand cardinally beside it is re-smashed as soon as it is smashable (it
   stays: a crab beside its target does not step).
4. **Wake first**: at a step with two or more crabs to park, every free crab
   farther than 5 is woken (walk beside it) before the first lead.
5. **No stalls**: stage `q` gives up after 10 ticks short of Q (that approach
   marked tried); waiting anywhere with a free crab beside me for 12 ticks
   gives up the approach.
6. **Compact ccw chains** (stuns cannot carry two crabs across steps, so the
   east-wall hook buys nothing after step 1): step 2 c1 (21,9) (20,7) (18,8);
   step 3 c3 (20,9)*green (19,7) (17,8); step 4 c2 (17,9)*blue (16,7) (14,8);
   step 1 stays (25,9)*red (24,7) (22,8) (the only cyan chain). Orb paths
   13-19 ticks instead of 21-35, so a step needs its three stuns overlapping
   for less time. Every step parked last-first.
7. **Wandered start in the test**: `cox_crabs.lua` cr100+n = cr n entered,
   then 300 ticks idle before the solver (the relay's arrival).

## Round 4 result (2026-10-19)

Built as planned, plus four found on the sweeps (r4a -> r4d):
- the wake is COMMITTED to one crab until it has been within 3 of me (an
  uncommitted wake flip-flopped with the lead as the crab wandered, cr4);
- re-park in place only for a crab never off its stun tile; at Q, a crab on
  P inside its immunity is waited for (moving me off Q broke the lead and
  put me on the orb's tile beside the sculpture, cr4/cr16);
- stage `s` gives up an approach after 24 ticks, and a free crab beside S on
  the orb's line is DRAGGED off (two tiles away, then back): at S (16,8) for
  (14,8) a crab on (16,9) sent every newborn orb down x=15 (cr18, cr131);
- an approach whose Q is beside the sculpture costs 250 (the second crab
  trailing stops on the orb's first tile and turns the newborn onto Q, cr104;
  (14,8) is led from the south, Q (14,7)); waiting for a stun is not "stuck";
- fire: a stand whose follower blocks the chain for 8 ticks is given up, and
  the stand keeps the path out of its 2-ring for a bolt/spell paint (v2 cr33:
  a crab cornered on (12,15) aborted every orb for 1,000 ticks);
- hazard: a free crab reaches the orb's i-th tile in i ticks (+1 for row lag),
  not only from beside it (v1 cr43, a crab freed two tiles off the doorstep).

| test | variant | before (green, median ticks / prayer) | after |
|---|---|---|---|
| cox_crabs cr0..47 | 0 ccw | 16/19 (t0 5/5, t1 5/5, t3 6/9), 786 / 90 | 19/19 (t3 9/9), 305 / 29 |
| | 1 thru | 20/20, 203 / 24 | 20/20, 213 / 24 |
| | 2 cw | 9/9, 325 / 34 | 9/9, 273 / 30 |
| cox_crabs cr100..147 (wandered) | 0 / 1 / 2 | (not run; the relay: table off 6/7) | 19/19, 20/20, 9/9; 303 / 211 / 273 |
| harness crabs0..23 | 0 / 1 / 2 | 11/11, 10/10, 3/3; v0 594 | 11/11, 10/10, 3/3; v0 309 |
| harness crabs24..59 | 0 (t2 2/2) / 1 / 2 | -- | 15/15, 13/13, 8/8 |

Orb hits: 0 on all 96 cox_crabs runs (before: 2 in 48); 1 in 60 harness runs
(crabs58, ccw t2, after the (19,7) smash at step 3: I stepped onto (20,7) the
tick the orb, turned at (20,9) by step 2's crab still held on (21,9), was due
there -- the smash op's own step, unconfirmed). Ticks: ccw max 420 (before
1,470); the 4,000-tick limit is 10x off. Prayer: ccw 28-29 points (before 90).
ccw turn 2 is not in cox_crabs.lua's 48 seeds; crabs58/59 deal it.
