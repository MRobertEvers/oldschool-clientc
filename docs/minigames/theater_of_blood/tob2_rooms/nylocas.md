# ToB v2: the Nylocas, Hard Mode, five raiders (plan)

File: `script/plugins/quest_driver/tob2_nylocas.lua`, `QD.raid.nylocas_v2_solve(opts)`.
Perception is v1's, called: `_nylo_measure` (lives, waves, births, nulls), `_nylo_hm_ids`,
`_nylo_hm_prince` (colours, M), `_nylo_phase`, `_nylo_attackable`, `_nylo_clump` (the safe
barrage 3x3, now and after the step), `_nylo_hm_decide_boss` (her colour clock, spread/dot
tiles), `_nylo_hm_guard` (P(fall alone), P(saved)), `_nylo_enter`, `QD.NYLO_WAVES`.

## Kit (fixed, worktree tob_relay.lua)
Every seat: void melee + tentacle + defender worn, blowpipe (Rapid, 2h), trident, Ancients
(barrage), all three sets except seat 5 (no mage helm/occult/cape; trident yes).
Roles (v1 HM): 1 ranged W, 2 melee S, 3 magic+barrage E, 4 magic+barrage N, 5 ranged SE.
Help is melee only (owner): allowed styles = {own, melee}; Prinkipas and Vasilias in their colour.

## Threat field (my seat, hp; B terms)
| hazard | term | window | damage exp/max | prayer |
|---|---|---|---|---|
| detonation (S+51 small / S+52 big, reach 2 of footprint) | `B.hit` footprint+2 (+1 moving), forbid | det-2 .. det-1 | 9.5/18, 11/21 | none (forbid: lethal zone) |
| aggro swing (P2: every 3, nearest by footprint gap, tie lowest pid, read end T-1) | `B.hit` footprint + min(reach, r_near), r_near from the other raiders' gaps (lower pid wins ties); reach melee 1, ranged/magic 8 | T-1 for each predicted swing T: last seq_tick+1 (R4) + 3k, or now+1 when none seen | small 5/17, big 7/24 | style, `land=1`: prayer read at the swing (P2, R3) |
| Prinkipas swing (every 4, nearest, reach 8, 3x3) | `B.hit` as an aggro | as above, rate 4 | 9/38 | melee 0, ranged/magic factor 17/38 |
| pillar collapse (room-wide, unprayable, 1..50) | `B.hit` over the whole box | the earliest tick its chewers can finish it (2 a bite, every 3) | P(fall in window) x 25.5 / 50 | none |
| Vasilias (boss phase) | v1's must tiles as a `B.zone` (require, damage tier): spread in ranged form, dot in magic form; within 8 of her (soft) | her form | | her colour's overhead |

THE OVERHEAD, swing by swing (v1 main tree `_nylo_hm_swing_overhead`): from the same predicted
swings, choice(T) = the heaviest style due at my tile at T (a Prinkipas weighs 380, so it wins);
nothing due: the last choice stands (a Prinkipas alive: its colour). `intents.pray` = choice(now+1)
(R5: pressed now, in force at now+1, the swing at now+1 reads it). Every later change of choice
goes into `B.pray_at(protect, T)`, so `B.prayed` gives each predicted swing exactly the factor the
policy will give it: the threat field and the prayer channel are one model.

## Value (hp currency)
A pillar hp is worth `PILLAR_WORTH(h) = COLLAPSE_TEAM_HP / max(h, PILLAR_HP/4)` team hp, h = its
hp left (150 at five; bar % x 1.5). COLLAPSE_TEAM_HP = 200: 5 raiders x 25.5 (mean of 1..50) +
its chewers turned aggro (~70). Derivation of the scale: a clean run bites 564 times (~508 hp of the
600), a collapse run 718 (peer session): the room sits at the edge, so every bite is ~1/150 of a
collapse on a full pillar and more on a low one (hazard ~1/h, floored at a quarter).
- walker (incoming, not chewing): bites from its arrival to its detonation, (det - arrive)/3 x 0.9
  x worth(its pillar: the wave table's support; a split's nearest). THE INTERCEPTION VALUE: a
  fresh walker carries all its bites, so killing it in the lane or at the mouth is worth the most.
- chewer: the same from now.
- aggro: swings left x exp x P_UNPRAYED (0.35) + its detonation near a raider (DET_NEAR 4).
- per swing: the kill value / the hits it needs (small 1, big 2, 1 under half bar).
- barrage: the sum over the magic nylos in its 3x3.
- Prinkipas: owner rule, not the currency: a focus seat has only it as a target (value PRINCE_SWING
  12/swing); t_ok1 = its next colour change - 1 (a press on the change tick is a wrong style).
- Vasilias: her colour's swing value 3/swing, t_ok1 = next switch - 1.

## Mode machine (per seat, re-derived every tick from the view, E14)
| mode | when | targets | gear / intents |
|---|---|---|---|
| wait | no supports yet | - | - |
| W-prince | a Prinkipas in state now (wave 10: all) or focus (20, 30: seats 2,4,5; guards 1,3 when no pillar to save; wave 30 only while every pillar >= 30% and 4 stand) | the Prinkipas | its colour's set, boost |
| W-guard | seat 1/3 and v1 `_nylo_hm_guard` names a pillar (P(fall alone) >= 0.3, P(saved) >= 0.75) | its chewers in my style or melee | that set |
| W-barrage | seat 3/4, mage set worn, `_nylo_clump` >= 2 magic, cast clock done | the clump primary (range 10, speed 5) | `cast` |
| W-nylo / C-nylo | otherwise, a nylo of {own, melee} attackable | top 5 by value of the chosen style (lower seats of the style claim their top one; a helper leaves the style-owner's top one) | the style's set; switch only when the other style's best score > KEEP x worn's |
| W-intercept | nothing attackable now | lane nylos whose SETTLED style (table) is mine, `t_ok0` = settle+2 / in range, path along the lane | own set; pull to the mouth (v1 `_nylo_hm_intercept`) |
| due | room empty, no Vasilias | - | melee set, Protect from Melee, eat to hp-20, pull beside her landing |
| B-colour | Vasilias | her (t_ok1 = switch-1) | her colour's set + overhead; must tile |
Never a target in a style whose weapon is not WORN now (v1 n29: a wield and an attack in one tick
swung the old weapon): a switch tick has no targets, only a pull toward the best one.

## PROBES
- P1: aggro hit rate (exp 5/7) and P_UNPRAYED (0.35): measure from the tick log.
- P2: swing phase from seq_tick+1: monitor `threat` mismatches.
- P3: Prinkipas reach 8 and prayed factor 17/38 (v1 says so; unmeasured).
- P4: COLLAPSE_TEAM_HP 200 and the worth floor: compare bites per run, v1 vs v2.
- P5: projectile flight: ranged/magic hits land later than T; with land=1 the monitor's `threat`
  rule mis-times them (gap: a hit needs a prayer-read tick separate from its landing tick).

## PASS
Owner: no raider dead, 0 wrong-style (nulls 0 in every seat's summary, no reflect). Measures:
damage per seat, collapses (supports lost), pillar hp chewed, nylos that reached a pillar, ticks.

## Results (2026-10-12, scriptrun, seeds ny1..ny8, committed v1 vs v2)
| | v1 | v2 probe (p1) | v2 final (batch 3) |
|---|---|---|---|
| pass | 7/8 (ny8: seat 1 dead) | 1/1 | 8/8 |
| room ticks (done) | 483 | 521 | 481 |
| team damage | 778 | 496 (ny1) | 468 |
| worst seat | 288 | 143 | 178 |
| supports lost | 17 | 1 | 8 |
| pillar bites / hp | 638 / 565 | 598 / 521 | 559 / 496 |
| dmg: aggro / collapse / Prinkipas / Vasilias | 379 / 273 / 17 / 122 | | 142 / 138 / 38 / 138 |
| nulls (wrong style) | 0 | 0 | 0 |

Batches: (1) overhead waits a tick for a wield when nothing at me needs it, switch-tick targets
with t_ok0 = now+2, the held wave-30 Prinkipas focused in the cleanup -- the last one let three
pillars fall and killed a seat (ny1): reverted. (2) P1 measured: 250 aggro swings did 103 through
the swing-by-swing overhead (0.08 of exp), P_UNPRAYED 0.35 -> 0.1: walkers outrank aggros. (3)
Vasilias' form tiles (dot only works if nobody is nearer than it: the bounce spares only the
target's tile), pre-move after her second attack, her melee tiles on her south edge; Prinkipas ties
random (Vasilias' rule), not lowest pid. Monitor (final 8 seeds): move 4.0%, swing 18.9% (mostly
"predicted, none": a one-hit nylo killed by another seat), threat 1.9%, stall 0.
