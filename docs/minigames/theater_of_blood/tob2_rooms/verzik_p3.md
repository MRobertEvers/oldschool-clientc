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
