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

## Burning claws (owner, 2026-10-12)
`NV.special` (Sotetseg's / P3's machine): idle -> wield (bone_claws, two-handed, a free slot)
-> armed (intents.spec, spec_cost 350, through QD.T2.exec.spec_gate) -> back (the melee set:
tentacle AND dragon defender) -> idle. Active only while I swing melee at a melee Prinkipas or at
Vasilias' melee form, the weapon in hand, and her/its colour change >= SPEC_LEAD (4) ticks on (the
target's t_ok1 = change - 1 keeps every swing off the change tick). The spec_ok marks (the shared
warhammer dump) are gone from both targets. Private root, same binary, ny1..ny8 + q1..q8:
| | before (r6 file) | claws |
|---|---|---|
| pass | 16/16 | 16/16 |
| room ticks | 467 | 448 |
| Prinkipas kill (all three) | 37.4 | 31.8 (waves 10/20: 9 / 14 vs 18 / 23) |
| Vasilias kill | 105.3 | 95.1 |
| specials per seat | 2.0 hammer | 3.4 claws |
| team damage | 342 | 286 |
| supports lost | 9 | 3 |
| deaths / nulls | 0 / 0 | 0 / 0 |
272 claws specials, every one with a melee-form Prinkipas/Vasilias alive; 390 claws -> tentacle
swaps, every one with the defender in the same tick.

## The pair affinity (owner, 2026-10-12)
Owner: "the ranges and magers always end up attacking the same nylo, often resulting in wasted hits.
Perhaps you can put affinity on the duplicate roles."

THE CAUSE, confirmed from the tick logs (`hit_npc`'s f column is the dealer pid; a hit on a nylocas already
dead is never logged, so a swing with no hit of its own is a wasted one). `NV.candidates` removed, for each
lower seat of my style, that seat's TOP-VALUE nylocas. But the planner picks among up to 5 targets by value
against time, walk included, so the seat often swung at a different one, and the "claimed" nylocas was
not the one the partner actually attacked. On 48 seeds, 452 of seat 5's wasted swings went out ON THE SAME
TICK as seat 1's at the same nylocas, and seat 1's hit landed first. For the mages, 183 of seat 4's wasted
swings came AFTER seat 3's trident was already in flight at the same nylocas, and 160 went out on the same
tick. Nothing checked for a hit already on the way.

THE CHANGE (`tob2_nylocas.lua`). Everything comes from the view every seat shares: the pair's tiles, the rows'
tiles, the projectiles. There is no barrier mark and no side channel.
- `NV.split`: every nylocas of the pair's style goes to the pair seat nearer it (footprint gap from the seat's
  tile). A tie goes to the seat whose STATION is nearer. If one seat then holds none and the other holds two or
  more, the cheapest one to hand over moves. One nylocas is never split by its npc SLOT: each client numbers
  the npcs its own way (q1 t185: slot 118 to seat 1, 124 to seat 5).
- HELD: a pair seat's projectile in flight at a nylocas (its source tile is that seat's tile) keeps it that
  seat's for as long as the shot flies.
- `NV.split_of` caches the split per tick and covers the lane nylocas settling into the style too. Splitting
  only the attackable ones left lane targets outside the split, and those were the ones the partner swung at.
- `NV.candidates`, own style in a pair: only my share, plus the URGENT ones (chewing a pillar under 30%). Those
  are both seats': splitting a falling pillar's chewers lost a pillar on 14 of 144 seeds, against 10 when they
  were shared. With none of my own, I take the partner's nylocas that need two hits, at AFF_EMPTY (0.5) of
  their worth. No split while a Prinkipas is in its now/focus state, and none for the guard seat's pick
  (`free`).
- `NV.inflight_mult`: a one-hit nylocas with n raider projectiles at it is worth (1 - P_KILL)^n. P_KILL is
  0.65, measured over 48 seeds: a seat's first hit finishes a fresh small one 0.65-0.70 of the time.
- Two NULL hazards the split exposed, both also present on base (4 shields, 6 nulls on 144 seeds):
  - `NV.barrage_safe`: no barrage when a big nylocas within 2 of the splash may burst on the cast's tick
    (detonation within 3, a projectile at it, or a bar already down). Its two smalls of any style spawn on its
    tiles (q8, q20, q32, q35, base q74/q124). No barrage either with a Prinkipas within 2 of the splash.
  - A lane nylocas still flickering is only a pull to its settle tile, never a target. An attack press does
    not wait for t_ok0, so the server swings the moment the nylocas is in reach, in whatever style it shows
    then (a3 q25 t348, base q142).

Same binary (build_tob2f_opt), seeds q1..q144, the same harness (hm_nylocas_v2.lua):
| | before | after |
|---|---|---|
| pass | 144/144 | 144/144 |
| wasted swings / seed (s1 s2 s3 s4 s5) | 31.1 (0.9 0.9 6.5 10.2 12.7) | 16.8 (0.5 1.4 3.6 4.1 7.2) |
| ranged pair / magic pair wasted | 13.6 / 16.7 | 7.7 / 7.6 |
| duplicate-target swings / seed, ranged / magic | 28.1 / 21.7 | 8.2 / 8.1 |
| overkill hp / seed (s3 s4 s5) | 11.1 15.9 22.2 | 5.7 6.2 10.6 |
| room ticks (done) | 446.1 | 443.7 |
| pillar hp chewed / seed | 432.6 | 415.9 |
| supports lost (seeds) | 19 (17) | 11 (10) |
| team damage | 259 | 240 |
| deaths / nulls (shields) | 0 / 6 (4) | 0 / 0 (0) |
| barrage casts / seed | 9.4 | 5.9 |
How the metrics are defined:
- duplicate-target swing: a pair seat swings at the nylocas its partner is attacking, on the same tick, while
  another nylocas of the pair's style stands.
- wasted swing: a swing with no hit of its own (its target died first).

Scorer: nyaff.py in the worker's scratch root.
- Variant a9: split the urgent ones too, with an urgent-first balance, sharing the partner's at 0.85 unless
  under its shot. Result: wasted 15.4, duplicates 6.5 / 6.0, room 441.7, BUT pillars lost on 14 seeds.
- Not done: the live lane. Projectile rows are built in whole server ticks on both lanes
  (drive_ui_projectiles), as Maiden's storm reads already rely on.
