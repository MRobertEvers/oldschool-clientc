# Verzik P3, Hard Mode, v2 (`tob2_verzik_p3.lua`, `QD.raid.verzik_p3_v2_solve`)

The relay's opts are passed as given: `{ max_ticks, base, weapon, spec_weapon }`. `spec_weapon` defaults
to the kit's `dragon_warhammer` when it is carried; the committed kit has no claws.
Harnesses: `$S/hm_verzik_v1.lua` and `hm_verzik_v2.lua`. Seat 1 gets the Dawnbringer it would have
carried in from Xarpus. Without it, HM P1 cannot be damaged and every seat dies in P1.
That is relay state restored, not a kit change.

## The one currency
`HP_PER_BOSS_HP = 0.27` (team hp taken per tick / team damage dealt per tick), measured on the
committed v1 (`vp_v1` vp2/vp4/vp6, `p3cal.py`):
- Her pool is 4550 (3500, plus the 1050 HM heal at 5%).
- The team took 1020-1489 in P3, and 99% of that was hers. That gives 0.22-0.33.
- A tentacle swing does 15-17 (`SWING_EXP` 16), so one swing is worth 4.3 hp.
- A tornado touch costs half my hp, plus three times that healed onto her: `hp/2 * (1 + 3 * 0.27)`.
- A warhammer special while armed: `SPEC_VALUE` 30 (PROBE).

## Threat field (predict)
| hazard | B term | window | damage exp / max | prayer |
|---|---|---|---|---|
| auto in flight (seen at T) | hit, arena, `style` = S.prot | read T+2, lands T+3 | 7 / 61, B halves it at landing | yes (P4) |
| autos at future slots (rotation: autos, plus crabs riding one) | hit, arena, pre-scaled | slot+2 | 7 / 30 only if landing within 3 ticks, else 7 (the rescue eats on a predicted death, not on the sum of twelve ticks) | assumed right |
| her melee at a scan (slot-1 tick end) | zone, lethal | v1 `F.scans` | 50% of 63 | none |
| crab blast (v1 `S.blasts`) | hit forbid, gap 2 or less; hit priced, gap 3 | v1 from..to | 26-63 / 8 | none |
| web seen (v1 `S.web_hazards`) | hit forbid, `pass` | land-1 .. land+12 | 20 / 40, plus a 10-tick stick | none |
| web on my own tile (the throws read it) | MARK, t_mark now..now+7 (8 marks); hit for tick-ends already past | t_mark+4 .. +17 | forbid | none |
| knockback (webs walk) | hit forbid, centre 7x7 | slot .. slot+12, until the spin | | |
| her body during the spin | hit forbid, `pass` | | | |
| other raiders' yellow pools | hit forbid | strike k read | 80 | none |
| my tornadoes (v1 owners, at most 4, nearest first) | CHASER | | as above | none |

Scans for my seat:
- Exempt when her known tank is someone else.
- Her known tank is me: the outer band (gap 1-2) and her outer ring are lethal, so I end under her (the inner 5x5) or 3+ off.
- Tank not known yet: gap 0-2 is lethal, so 3+ off (oracle T4).

## Strategy modes (re-derived each tick)
`wait` (transition: top up to 90) > `yellows` (require-zone on `mine[k]` at its read, pull to the next)
> `ball-target`/`ball-next` (require-zone at the read; margin priced) / `ball-clear` (gap 0-2 lethal
for the whole flight) > `web-<gather|hold|step|join|lost>` (v1 `_vzp3_web_run` called for its S.wr
state, not its order; the stack tile and its predicted clockwise steps every 4 tick-ends are soft
require-zones, 2 hp a tick off) > `kite` (enraged) > `dps` (exempt) / `tank` / `away`.

Her target: range 1, speed = the weapon being wielded THIS tick. Not attackable:
- during the yellows' charge (until the last strike);
- on the webs walk;
- from the slot of a webs or yellows special;
- in my ball hold window, read-1 .. read+1. Lesson 47: the hold is a walk, so she is left out of the targets and the executive's stay sends the hold walk.

Her footprint: her NEXT step is predicted (the naive follow toward her known tank, when it is 2+ off),
then held. Followed over the whole horizon, the kite waited for her to come.

Spec weapon sub-machine (v1 main tree): `idle` > `wield` (gear) > `armed` (`intents.spec`) > `back` (main weapon).

Floors: eat 65 and brew 65 (every auto prayed, P4; the rescue covers predicted deaths); top-up 90.
Piety follows v1's budget (`vz_prayer_spare`).

## PROBE
- `SPEC_VALUE`.
- The webs' "every tick-end of mine draws a web" assumption: conservative, five raiders take turns.
- `STACK_COST`.
- Whether HM P3 crabs persist.
- The prayed auto mean (7).

## PASS
No raider dead, no wrong-style prayer, at least as well as v1, and the HP and supplies SPENT in P3 at least as low as v1's.

## Results (8 seeds vp1..vp8, real P1+P2 v1 in front, so P3 starts from the post-P2 state)
| | pass | P3 ticks | team dmg | worst seat | swings | touches | consumes | deaths |
|---|---|---|---|---|---|---|---|---|
| v1 committed | 7/8 | 375 | 1237 | 239-356 | 288 | 0.4 | 136 | 1 |
| v2 (final, vp_v2g2) | 7/8 | 380 | 1440 | 302-431 | 332 | 6.6 | 155 | 2 |

v2 swings 15% more than v1: the kite does 4.7 seat-ticks a swing, the web run 4.0. It loses that in tornado
touches (each heals her about 105) and takes 16% more damage. Not yet better than v1.

### Batch 4 (coordinator, 2026-10-12): tornado pricing checked, Piety off
- Tornado pricing was already complete. `dmg_exp = hp/2 * (1 + 3 * 0.27)` covers half my current hp
  plus the heal (3 x hp/2 = ~105 at 70 hp, x 0.27 = ~28 team hp). `dmg_max = hp/2`, so hp_worst sees
  the hit. The respawn (16 ticks later, 4-8 tiles off) is not a cost: it resets the distance.
  The C model's touch timing matches the content rule. `touch.py`: of 62 touches in vp_v2i, 4 were
  PREDICTED (loss 25+ at t-2/t-1, accepted as the least bad), 56 followed a step-1 chase ATTACK and
  54 had the seat standing where the plan had it moving. They are gap 1, not underpricing.
- `intents.pray_off = {"piety"}` once `vz_prayer_spare` runs out (v1's rule).
- vp_v2i: 6/8, P3 376 ticks, team damage 1418, touches 7.8 a seed, 3 deaths. Per seed (touches / P3 damage):
  | seed | v1 | v2 before (vp_v2g2) | v2 after (vp_v2i) |
  |---|---|---|---|
  | vp1 | 2 / 1465 (died) | 4 / 1336 | 5 / 1236 |
  | vp2 | 0 / 1204 | 5 / 1366 | 5 / 1353 |
  | vp3 | 1 / 1189 | 5 / 1320 | 19 / 1968 (2 died) |
  | vp4 | 0 / 1020 | 6 / 1391 | 4 / 1249 |
  | vp5 | 0 / 1081 | 9 / 1484 | 14 / 1887 (1 died) |
  | vp6 | 0 / 1489 | 5 / 1476 | 2 / 1041 |
  | vp7 | 0 / 1124 | 13 / 1818 (2 died) | 5 / 1169 |
  | vp8 | 1 / 1321 | 6 / 1327 | 8 / 1440 |
  The seeds' outcomes swing with the touch count, which is gap 1 (the room does not work around it).

## Convention: strategy windows vs invulnerability
A REFUSED press (the target's t_ok window) is only for real invulnerability: the yellows' charge,
the webs walk, the slot of a webs or yellows special. There the server stands me still, and the
planner's free "refused attack" with `inter == i` matches it. A STRATEGY window, where she is
attackable but I must not attack (the ball hold, read-1..read+1), drops her from B's targets on the
ticks it covers. The executive's stay then sends a walk to my own tile (E3). She is given
`t_ok1 = read - 2` when the window starts later in the horizon. A refused-attack node there would
emit `act=attack` and keep a real attack (and its follow walk) going.

## Gaps (shared layers / planner)
1. Attack-press reach is judged BEFORE her npc step. The planner judges reach against the
   footprint at step k, after npcs move (M4). The server takes the press in the input phase, against
   her old footprint, and the follow then meets her new one. So a chase step the plan predicts is not
   taken when she steps toward me: I stand, and the adjacent tornado touches. Evidence: every touch
   in vp_v2a and vp_v2g (decision log `attack>X!` at t-2, seat unmoved) and `mon move` repeats
   (vp_v2g2 vp2 t852-855 `attack 6438,88>6438,87 got 6438,88`). Banning it at step 1 cannot be
   expressed: a step-1-only ban made every plan defer its swing to step 2 and never take it (the kite
   fell from 4.7 to 14.8 ticks a swing). Needed in the planner: an ATTACK whose step needs a chase,
   scored also as "stand still" when a chaser is adjacent (the worst of the two outcomes), or a target
   footprint distribution (her random step when the tank is under her).
2. A refused attack with `inter == i` costs no click and shows as `act=attack`, so the executive
   keeps the attack. That is harmless for real invulnerability; for a strategy window (the ball hold)
   the room has to drop the target instead.
3. (DONE by the shared layers: `intents.pray_off`, used for Piety.)
4. Churn: the shared layers changed at 19:06, 19:13, 19:17 and 19:20 and the binary at 19:11, all
   during this room's probes. Single-seed comparisons across those times were confounded (vp_v2h was
   measured on other layers than vp_v2g).

## Batch 5-6 (2026-10-12, ZERO AVOIDABLE in P3; binary with C batches 5/6, forbid chasers)

### Damage classification (`$S/vp/avoid.py` over `hit_player`)
| source | perceived | class | why / content |
|---|---|---|---|
| her auto (seq 8124 magic, 8125 ranged, 14406 the crab summon riding one), prayed | seq, projectile; lands +2..+5 | UNAVOIDABLE | hits every raider (P4); prayed half only |
| her auto, unprayed part | head icon at landing | AVOIDABLE | the prayer channel (measured 0 on every v2 seed) |
| the ball's last holder, 49 | projectile 1598 chain | UNAVOIDABLE | tob_verzik.rs2 `~tob_verzik_ball_land`: Hard Mode hops end at `^tob_verzik_ball_max_bounces` 3; the next holder takes `^tob_verzik_hard_ball_bounce3` (49) every ball. In hp_worst as `ball-last` |
| ball unbounced / exploded / hop 89 or 74 | roles | AVOIDABLE | lethal require-zones at the read |
| tornado touch (npc 10863) | v1 owners | AVOIDABLE | CHASER `forbid = true` |
| crab blast (10858-10860), every band incl. the flat 7 at gap 3 | v1 `S.blasts` | AVOIDABLE | forbid hits (gap 0-2 and 3) |
| her melee | scans | AVOIDABLE | lethal zones (tank / away) |
| webs, yellows on a shared pool, the knockback | as above | AVOIDABLE | forbid |
| poison (source -1, 3-2-1 every 30 ticks) | | UNAVOIDABLE in P3 | carried in from P2 |

### Model changes
- Her step: path = HOLD; path_alt = her other step. That is the step that flips MY reach (in to out, or out to in) when my tornado is within 2; otherwise one random cardinal step when anyone is under her, the naive follow toward a known tank 2+ off, or a step toward me. Scored as the worse.
- `under-her` soft cost 1.5 for every mode but the tank's.
- `B.target` does not forward `path_alt`, so the room sets it on `B.spec.targets[i]`: a GAP in tob2_plan.lua.
- The ball margin (read-1, read+1) stays PRICED. As a rule it outranked the tornado: vp_v2n/o went to 4/8 and 1/8 with 44-57 touches the plan took.

### Results, vp1..vp8 (v1 = committed, vp_v1; v2 = vp_v2r)
| | pass | P3 ticks | team dmg | avoidable (team, 8 seeds) | touches/seed | doses/seat/100 ticks (wields excluded) |
|---|---|---|---|---|---|---|
| v1 | 7/8 | 375 | 1237 | 371 (crabs 268, tornado 73, her 30) | 0.4 | 7.23 |
| v2 | 8/8 | 360 | 1130 | 248 (crabs 138, tornado 110) | 0.4 | 6.75 |

Avoidable per seed (v1 / v2):

| seed | vp1 | vp2 | vp3 | vp4 | vp5 | vp6 | vp7 | vp8 |
|---|---|---|---|---|---|---|---|---|
| v1 / v2 | 80/76 | 28/42 | 42/7 | 7/14 | 42/35 | 79/28 | 7/0 | 86/46 |

The residual is not yet 0:
- Crab 7s at gap 3: the blast is decided one tick ahead, from an owner the v1 tracker guesses as the nearest raider. The P2 worker's owner-from-face rule is not yet ported.
- Three tornado touches: chase routes that differed from the planner's route (vp2 t1036, vp7 t988 in vp_v2q).

## Batch 7 (boosts, P3 spec weapons, the ball chain; installed 2026-10-12 as tob2_verzik_p3.va.lua)
- BOOSTS: the room owns the boost potions (`VZ.boosts`; the shared supplies keep food, the brew floor,
  the prayer restore and Piety). A super restore at Attack OR Strength 20 under base. A super combat
  while either is under base+5 and both are within 8 of base. Measured on 8 seeds (`stat.py`, the
  coordinator's stat marks): Attack and Strength both +10 or more on 50% of P3 seat-ticks, against v1's 44%
  (v1 logs Attack only). At or under base 13-20% (v1 25%). Worst -38 to -66 (v1 -77).
- SPEC WEAPONS: dragon_claws (500, two-handed), dragon_dagger (250), crystal_halberd (300, two-handed,
  7 ticks). Taken from opts.spec_weapon; there is no default in P3. Two-handed swaps go back to the
  tentacle and the parrying dagger. Every seat carried its weapon (40/40 for each weapon).
- THE BALL:
  - The landing reads the TARGET's tile, so the target and the next meet at a tile toward the next.
    The target walks by as much as the next cannot cover in time.
  - The next is the unvisited raider NEAREST the target. Seats out of view (the players list is about 15 tiles)
    come from their last-seen tiles.
  - The hold covers the projectile's own landing as well as v1's fixed +7.
  - Deaths fixed: claws vp3 (two kinds of un-bounced ball).

| (vp1..vp8) | pass | P3 ticks | team dmg | avoidable (team) | brew doses/seat | specials/seat |
|---|---|---|---|---|---|---|
| v1 | 7/8 | 375 | 1237 | 371 | 13.8 | - |
| v2 claws (va_claws) | 7/8 | 321 | 1076 | 354 | 11.4 | 2.0 |
| v2 dagger (va_dagger) | 7/8 | 335 | 1126 | 237 | 12.1 | 4.9 |
| v2 halberd (va_halberd) | 8/8 | 359 | 1269 | 485 | 13.7 | 4.0 |

Failures:
- claws vp6: seat 1 starved. It entered with 19 brew doses (freezer kit) and left with 0, taking 383.
- dagger vp4: seat 3 took an UNPREDICTED tornado touch (38; plan risk 0 at t880), then the
  unavoidable last-holder 49.

## Batch 8 (installed 2026-10-12 as tob2_verzik_p3.vf.lua): crab owner from its face, burning claws, level-scaled swings
- CRABS (`VZ.crabs`, the content's rule verified on the tick log):
  - The owner is voted from the face square: the drive stamps face_tick a tick early, so the tile is the owner's at the end of face_tick. A match on stacked seats gives no vote.
  - The crab steps its SW tile toward the owner's tile on each axis.
  - Arrival at now+1 is decided from the tiles seen now. The blast reads max(born+6, A), forbidden at gap <= 3.
  - A crab not yet arrived forbids gap <= 1 at every tick end: one wider at step 1 while the owner is unknown, plus a possible-arrival band.
  - A wrong arrival latch is dropped when the crab moves or outlives its read.
  - v1's nearest-raider guess is gone.
  - Crab damage on 8 claws seeds: 64 (batch 7: 125).
- bone_claws (burning claws) in SPEC_WEAPONS: 350, two-handed, three hits. Value 33 x HP_PER_BOSS_HP, PROBE.
- The swing's value is scaled by current Attack/Strength (a linear PROBE model, normalised at base+10).

| (vp1..vp8) | pass | P3 ticks | team dmg | avoidable | touches/seed | att&str +10 | doses/seat/100t |
|---|---|---|---|---|---|---|---|
| v1 | 7/8 | 375 | 1237 | 371 | 0.4 | 44% (att only) | 7.2 |
| batch 7, dragon claws | 7/8 | 321 | 1076 | 354 | 0.9 | 50% | 8.6 |
| batch 8, dragon claws | 8/8 | 343 | 1141 | 360 | 1.0 | 51% | 8.5 |
| batch 8, burning claws | 8/8 | 326 | 1057 | 486 | 1.1 | 50% | 9.3 |

THE REMAINING TOUCHES ARE HER STEP. `offplan.py` and `herwhy.py` over the 17 touches in vf_claws and vf_bone:
- 15 of 17 were a chase press whose end tile moved with her step. The server re-routed to her F1, as C batch 5 models.
- Her step was neither hold nor my single path_alt.
- 13 of 17 steps match no naive follow toward any seat, and no seat was under her.

The planner needs MULTI-ALT for her (hold plus her 8 steps, the worst scored) whenever my tornado is within 2. One alt cannot name a step the content does not determine from what a seat sees.
## Batch 9 (OFFLINE, $S/tob2_verzik_p3.vg_offline.lua; not installed until the 128 sweep ends)
- Combats paced on QD.T2.budget.spendable (P3 reserve 0). Ticks left = her hp left (bar x 3500, + the 1050 HM
  heal while she is over 5%) / team dps over her last 40 ticks. With spendable x 56 >= ticks left, free;
  otherwise one dose every ticks-left / spendable.
  vp (full kits): unchanged (8/8, 325 ticks, 1065 dmg, 5.0 combats a seat). The relay with the budget's
  4-dose P3 share is where it matters.
- BURNING CLAWS' extra avoidable damage is not the weapon. No avoidable hit landed within 6 ticks of a special
  animation (bone 11140, dragon 7514: 0 of 432 / 0 of 354). The specials all fire early, before the enrage.
  The difference is trajectory noise across 8 seeds (per-seed 0..164) plus long-flight autos (+6..+9 ticks
  to far seats) that avoid.py's 2..5-tick window filed as "her other".
- BOXED IN (the plan already had rule risk at k1, no legal tile): vg_bone vp6 t1032-1037, NE corner
  6440-6446,79-81, three crabs arriving at once with tornadoes; vp8 t990-1000, east edge. Strategy
  needed: keep off the walls while crabs are up.
- KEEP A LEGAL TILE OPEN (offline, $S/tob2_verzik_p3.vh_offline.lua). While a crab is up:
  - an arrival one tick LATER (its owner within 4 of the crab's next tile) prices the blast band at now+2, at 5;
  - the edge margin is 5 at weight 1.0.
  Burning claws, vp1..vp8:

  | | pass | P3 ticks | team dmg | crab dmg | tornado | avoidable | rule-risk plans (crab) |
  |---|---|---|---|---|---|---|---|
  | vg_bone | 8/8 | 325 | 1065 | 249 | 250 | 501 | 13 |
  | vh_bone | 8/8 | 318 | 1009 | 28 | 201 | 229 | 12 |
- SPECIALS DUMPED (owner 2026-10-12, offline $S/tob2_verzik_p3.vs_offline.lua). Every swing with a special's energy
  is a special, at the start and through the enrage. The wield is a held entry in the plan.
  Burning claws, vp1..vp8:

  | | pass | P3 ticks | team damage | avoidable | specials/seat | energy left |
  |---|---|---|---|---|---|---|
  | vh_bone (corner fix) | 8/8 | 318 | 1009 | 229 | 3.0 | - |
  | vs_bone (dump) | 8/8 | 297 | 957 | 214 | 4.0 | 109 avg (under the 350 cost) |

## Batch 10 (INSTALLED 2026-10-12, $S/tob2_verzik_p3.vp9.lua): batch 9 + corner fix + special dump + path_alts
- Her nine options with my tornado within 2: hold (path) plus 8 one-step paths (path_alts), the worst chase scored.
- Specials through QD.T2.budget.spec_ok and intents.spec_cost (exec.spec_gate). In P3 that is energy >= cost.
- Burning claws, vp1..vp8:

| | pass | P3 ticks | team dmg | tornado touches | avoidable (true) | specials/seat | plan nodes/seat |
|---|---|---|---|---|---|---|---|
| vs_bone (offline) | 8/8 | 297 | 957 | 6 | 214 | 4.0 | 2.67M |
| vp9_bone | 8/8 | 288 | 888 | 0 | 8 (crab 7+1) | 4.0 | 2.59M |

  avoid.py's "her other" (30) here is prayed autos with a 6-tick flight to far seats (<= 15 each), not avoidable.

## Batch 11 (OFFLINE, $S/tob2_verzik_p3.v11_offline.lua; install after the r10 128 sweep)
- relay_r10_128 f39, seat 3 dead at t3314 to 49 (the ball's last holder, predicted: le49 at t3312, hp 84,
  no rescue needed) + 35 (HER MELEE, seq 8123, the same tick; seat 5 took 46 of it). The ball's "next"
  (seat 2) was her tank. Its plan had rule risk 12 "scan" at t3312: the next's hold and the tank's scan
  collided, and it kept the hold beside her. FIX: the ball's meeting tile is moved 3+ off her footprint
  (BALL_OFF_HER), so neither holder is in her melee at a scan.
- The last holder cannot be picked by hp: other seats' hp is not in the players list (no shared view of
  it). The 49 is in the target's hp_worst, and the rescue covers it.
- path_alts gate widened to 3 (ALTS_GATE): vq_bone vp6's two touches had the tornado 3 off at the
  decision, and a 2-tile chase toward it.
- f39 (full relay, hm_relay_v2.lua): PASS. Burning claws, vp1..vp8 (vq2_bone): 8/8, P3 295, team 885,
  touches 0, avoidable crab 8 + one melee (vp7 t1024, 60 on seat 5: no seat's plan broke a scan rule, so
  some seat misread who her tank was - v1's face perception, open).

## Batch 12 (INSTALLED 2026-10-12, $S/tob2_verzik_p3.v12.lua): batch 11 + the dead seat out of the ball chain
- relay_r10_128 f123, two seats lost:
  - Seat 4 starved: it entered P3 with 0 fish and 3 brew doses, and died at hp 2.
  - Seat 3 ate the un-bounced Hard Mode ball, which kills (`$hit = $cur`, 68 at hp 68) at t3420. Its "next" was seat 4: dead, caged at t3404, but its last arena tile was 9 ticks old and still in `seen_at`.
- Fix:
  - a seat seen outside the arena (the cage) is forgotten at once;
  - a "next" no longer seen is picked again.
- f123 rerun (full relay, current shared layers): PASS. vp1..vp8 (vq3_bone): 8/8, the same as vq2_bone.
- runs/sote_share/f91: seat 5 starved, with no avoidable damage.
  - It entered with 0 fish and 11 brew doses.
  - It took 258 in P3, all from her autos (24 prayed hits, mean 9.6 against the team's 6.5-7.7, plus one unprayed of 13 like every seat).
  - It died at t3239 with the team holding 10 brew doses.
  - This needs in-fight sharing or a larger share for the seat that runs dry. P3's own lever is fewer ticks.

## Prototype: a healing exchange in the P2 -> P3 transition (NOT installed; $S/tob2_verzik_p3.share_proto.lua)
- Design (VZ.share):
  - Forecast = 16 x brew doses + 20 x fish + hp over the floor - 0.75 x 330 (P3 intake) - 30.
  - A seat with 64 or more spare drops a full brew (at most 2).
  - A seat in deficit takes the nearest full brew on the floor (a Take, keeping 2 slots free).
  - Perception only.
- Measured on f91, f123, f36, f66, f68, f71 (full relay, current shared layers):
  - The baseline (batch 12) PASSES all six, with no seat deaths.
  - The door share now lands every seat in P3 on 18-30 brew doses (the poorest seat per seed: 18, 18, 22, 18, 27, 22).
  - The naive prototype also passes all six with identical P3 ticks. But every seat forecast a surplus and dropped, no seat was in deficit, and the brews stayed on the floor: team brew doses left at her death 32-64 against the baseline's 70-96 (6-8 a seat lost).
  - Not worth installing as is.
- If starvation comes back: the giver cannot see a taker's need (no side channel), so a perception-only version needs a signal:
  1. A deficit seat walks to a fixed REQUEST tile on the first transition tick.
  2. Givers who see a seat there drop on that tile; the taker takes.
  3. A giver takes back its own drop if it is still there when the window ends.
  The window is short: her form appears about 4 ticks into the P3 solve and her first auto lands about 10 ticks later.
