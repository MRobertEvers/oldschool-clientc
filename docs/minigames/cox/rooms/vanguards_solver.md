# The solo Vanguards solver: plan

File to come: `script/plugins/quest_driver/cox2_vanguards.lua`, a `QD.T2` room
(`cox_vanguards`) on the v2 stack, like `cox2_olm.lua`; test
`test/raids/cox_vanguards.lua`, entered with `::coxtest 11 <n>` (the player
stands past the previous room's exit Passage and walks in). Mechanics and every
number: `vanguards_spec.md` (rows V1-V19 cited below). Method:
`docs/CONTENT_SOLVER_MANUAL.md`.

## The strategy a strong solo human plays

From Synq (1:38:49-1:45:29), the wiki's Strategies section and the CoX
Vanguards plugin most solo raiders run:

1. **Wake from the edge.** Enter along the wall; the burst (V2-V3) happens when
   I come within 6 of a hole. Know each hole's style from its npc id the tick
   it rises (7527 melee, 7528 ranged, 7529 magic).
2. **Count every vanguard's hitpoints exactly**, as the plugin does: start at
   base (180), subtract every damage splat, +1 when it turns into the walking
   form (V14), back to base on a heal splat (V11). The overhead bar (1/30 of
   180 = 6 hp a notch) is only a cross-check.
3. **Always hit the one with the most hitpoints**, with the style it is weak to
   (ranged one: melee; melee one: magic; magic one: ranged), and only while my
   max hit cannot push the gap to the reset: a swing of max `d` on `t` is
   safe when, with `t - d` in place of `t`, the most minus the least of the
   living three stays `<= floor(0.4 * base) - 1` (the plugin's "damage to
   reset" number, `hp - (other_hi - rt)`). A thrall or a venom tick counts
   against the same budget. If no swing is safe, switch to the lower-max
   weapon or wait for the next shuffle.
4. **Draw one vanguard at a time.** Aggression is 3 tiles (V4): approach the
   target from the side away from the other two; stay on the room's edge
   (wiki: "Staying around the edges of the room helps avoid drawing attacks
   from the others"). Melee one: attack from range with the trident, from the
   tile nearest it that is furthest from the magic one; use its 9-tile leash
   (V9) to safespot it when the layout allows. Ranged one: tentacle; stand
   under it between its swings if hits stack up (it never moves, V9). Magic
   one: blowpipe from outside its 3-tile aggro.
5. **Pray against the one attacking me** (protect from melee / missiles /
   magic): a protected hit is a third (V8). Melee prayer is read at its swing,
   ranged/magic prayers at the projectile's landing (2 / 3 ticks after the
   swing).
6. **Shuffles** (V10, V13-V16): the moment the closing animations play, the
   targets drop; predict each walker's next hole (all go the same way round;
   the direction shows on the first step) and walk round the PERIMETER to
   meet the one I need, never inside a walking 3x3 (trample V15).
7. **The finish**: once all three are below `floor(0.4 * base)` (the plugin's
   "good to clear"), kill them in any order -- fast (CA Together We'll Fall:
   all within 10 s of the first; not required for PASS). A killing blow while
   the other two are still high is not a kill (V17): it resets the room.
8. **Loot**: each death drops its potions at its centre (V18); pick up the
   overload(s), Xeric's aids, the style potions (Synq: "Vanguards drop the most
   supplies").

## Perception (only what a client is sent)

| fact | verb | notes |
|---|---|---|
| which vanguard is which, where | `api_drive.npcs` rows: `npc_id` (7525-7529), `server_x/z`, size 3 | a style is an npc id; a walking one (7526) keeps the identity of the slot that turned into it (track by `slot`) |
| shells closing / opening, attacks | `npcs` `anim_primary`: 7437/7442/7447 closing, 7433/7438/7443 opening, 7436/7441/7446 attacks, 7428 burst | the attack anim on tick T is a swing; melee lands on T, ranged on T+2, magic on T+3 |
| volley tiles | `api_drive.projectiles` (1331 magic, 1332-1334 rocks): target tile and landing cycle | who is hit was decided at the swing (V6) |
| the gap tell | `npcs` `spotanim_id` 56 / 57 | a cross-check of my own count |
| a death | the npc row GONE (despawn), not the 7432 animation: a blow the rules turn into "left at 1" still plays 7432 today (spec E1) | |
| hitpoints | **MISSING**: see below. Today: `hit_damage`/`hit_cycle` (newest splat only, no type) and `health_ratio` | |
| my prayer, hp, stats | `varbit` prayer states, `skill`, `inv_count` | |
| drops | `api_drive.objs` | |
| my swings | the monitor's own record + the splat that follows | |

### Engine work this room needs (missing verbs)

1. **`api_drive.npc_hitsplats(slot, since_cycle)`**: every hitsplat on an npc
   since a cycle, each `{type, damage, cycle}` (damage / block / heal), from
   the packet, on both lanes. The newest-splat-only row loses two splats that
   land on one tick (my hit + a thrall's, or three melee hits on a raider) and
   cannot tell a heal splat (the reset) from a hit. Exact hp tracking is the
   whole room; the plugin every solo raider runs does exactly this.
2. Optional, for the HUD (spec U11, Jagex 18 Jun 2020): once the boss HUD
   exists, its three bars (varbits) are a second hp source; reading varbits is
   already supported.

## PREDICT: the threat field

| hazard | term | window | damage (expected / max, solo) |
|---|---|---|---|
| melee vanguard swing | the melee reach of its 3x3 at T if it targets me; its chase path (leash 9 from its dig tile) | every 4 ticks from its first swing | 3 hits, each 0..18 x hit chance; protected 0..5 |
| ranged / magic volley | my tile at the swing tick (the decision), landing 2 / 3 ticks later; extras on random tiles within 3 | every 4 ticks | 0..22 each x hit chance (protected 0..7); up to 3 if I stand where extras can land on me (only my own tile is certain) |
| aggression | a tile within 3 of a non-target vanguard's footprint = a second attacker | while it has no target | that vanguard's swing stream |
| trample | the 3x3 of each walking vanguard along its predicted path to the next hole | the walk | 3-6 a tick (forbid) |
| reset | a planned swing whose max could push the gap to floor(0.4 x base) | always | the whole room's progress (forbid) |

## STRATEGY: the mode machine

| mode | goal | leaves when |
|---|---|---|
| ENTER | walk in along the wall to a wake tile 6 from the nearest hole, prayers off, overload drunk | the burst (7428) |
| PICK | choose the target: the living vanguard with the most hp (count), ties to the nearest; choose weapon (weakness) and approach tile (attack range, >3 from the other two, on the edge) | a target and tile |
| FIGHT | attack the target while safe (step 3); pray its style; eat/drink on the budget | a shuffle starts (closing anims) / my max unsafe / target dead |
| FOLLOW | predict the walk (same way round), go round the perimeter to the target's next hole outside every walking 3x3 | the opening anims |
| HOLD | no safe swing on anyone: stand out of all aggro zones, pray the attacker | the next shuffle or the gap changes |
| CLEAR | all living below floor(0.4 x base): kill the lowest first with the highest-dps weapon | all dead |
| LOOT | pick up the drops (overloads first) | done |

## The kit

The Olm kit (`test/raids/cox_olm_solo.lua`: We Do Raids minimum gear, CoX (+)
potions) already has one weapon per weakness: abyssal tentacle (melee, for the
ranged vanguard), trident of the swamp (magic, for the melee vanguard; long
range to stay out of reach) with the mystic switch, toxic blowpipe (ranged,
for the magic vanguard: low max hits are the point -- wiki "weapons with low
damage per hit such as toxic blowpipe are highly recommended"). Nothing new
is required. The room pays for itself: the drops (V18) refill the Olm supply
budget.

## Probes the first solver run prints

| probe | why |
|---|---|
| the shuffle direction on the first walking step vs the predicted next hole | V14 |
| hits taken by source per vanguard; any hit from a vanguard I did not engage (aggro mistake) | V4 |
| my hp count vs the overhead bar at every splat, and vs the swirl tell | V11-V12, the missing verb |
| the tick of each landing vs the attack anim | V5-V8 |
| any reset (heal splats) | must be 0 |

## PASS

On 48 seeds (`::coxtest 11 0..47`): all three vanguards dead, the raider
alive, **0 resets** (no heal splat on a vanguard; CA Perfectly Balanced), no
script aborts, and every drop picked up. Recorded, not gated: the time from
the burst to the last death, damage taken by source, and whether the three
died within 10 s (Together We'll Fall).

## Round 2 result (2026-10-18)

`script/plugins/quest_driver/cox2_vanguards.lua` + `test/raids/cox_vanguards.lua`
(hp counted from the npc rows' `splats`; target = most counted hp; aggro
priced 15 within 3 of an unengaged vanguard; walkers' 3x3 + 2 forbidden 3
ticks; prayer against the last swinger; wake tile computed per variant).
48-seed sweep (`--name vanguards0..47`): **46/46 that entered PASS, 0 resets**;
seeds 46 and 47 never entered (`::coxtest 11 46/47` exceeds the 500k
instruction budget inside `~cox_spawn_vasa` -- foundation). Damage taken
180-370 a run (overload 50 included); 3-10 trample hits a run remain (the
walk path is not predicted beyond the ring).

## Round 3 result (2026-10-18)

GREEN now includes **0 trample hits**. The threat field forbids each walking
(or closing) shell's 3x3 along its predicted walk to every hole (a tile a
tick, diagonal first; the 3x3 forbidden, the ring round it priced), and the
3x3 of every combat form (never end a tick under one: a shell can close any
tick). Also: the observation runs in PREDICT (a closing seen this tick is in
this tick's field), Auto Retaliate is turned off at entry (a retaliation
held the attack on the wrong vanguard and reset the room), a target change
holds the old attack, aggression stops being priced after 8 swingless ticks,
counted hp never drops under 1, and the vanguards are walked in a fixed order.
The solver finds its own room (here / the room ahead / a neighbour) and is
called with `{ max_ticks }` only (whole-raid contract). 48/48 scriptrun
(`--name vanguards0..47`): 0 resets, 0 trample hits.

## Round 4: the budget kit (2026-10-19)

The relay arrives on `QD.COX.SOLO_KIT` (wiki Minimum, no overload, no raid
potions: 7 brews, 2 super restores, a sanfew, a super combat and a ranging
pre-pot). Harness `test/raids/_cox_room_budget.lua --name vanguards<n>`:
**12/12 died** (1032 ticks, ~229 dealt in 128-191 hits, 28 brew doses).

### Measured (b_vanguards sweep, ticklog `hit_npc` x the raider row's weapon)

| finding | evidence (vanguards0) |
|---|---|
| target flip-flop: "most counted hp" re-picked every tick, so a 1-hp lead switched target, weapon and walk | t109 magic, t115 melee, t125 ranged, t180 magic, t199 melee, t210 magic, t215 ranged...: 20+ switches in 300 ticks, gear swapped every 5-10 ticks |
| every switch engages one more vanguard (it keeps its target until the shuffle): 2-3 attackers most phases, one prayer | trace `*` on all three at t130, t160, t215; 575 taken (212 melee, 363 volleys) |
| the prayer followed the LAST swinger, so a melee vanguard's first swing in reach landed on the missile prayer | t178 ranged swing -> protect missiles; t181 melee 15+14 unprotected |
| no stat restore at all: 28 brew doses drained attack/strength/ranged/magic to 1 | `budget.left ... ranged=1/99 attack=1/99 magic=1/99`, restore doses used 4 (prayer sips only); damage per 50 ticks 31-61 before t250, 0-11 after t400 |
| the boost prayers were on (Piety / Rigour / Augury, raider row bits 26/24/27) | 122-151 ticks each |

The damage was low because the swings were few (a switch every few ticks:
walk, re-wield, re-engage) and, after the first 5 brews, because the stats
the swings roll on were drained.

### The budget method

1. **Commit to a target** (the plugin's "damage to reset" number, plan step 3
   made exact): keep hitting the current target while its next swing is SAFE,
   i.e. with every pending swing of mine at its max and this swing at its max,
   the living's most hp minus least hp stays `<= GAP - 1 - 6` (72 of 180; 6
   for venom and the 1-tick splat lag; a kill counts as 0, so once all are
   under 66 every swing is safe). Max hits come from the live levels (trident
   of the swamp `floor(magic/3)-2` x1.12, blowpipe dragon darts with Rigour,
   tentacle with Piety). Only when the target's swing is unsafe pick the safe
   vanguard with the most hp. One switch a phase instead of one every 5 ticks.
2. **Pray the next landing**: for each vanguard attacking me, the tick its
   next hit is read (melee: its next swing, if it can be in reach by then;
   ranged/magic: swing + 2 / + 3) -- the earliest wins, melee on a tie.
3. **Restore the fighting stats** with the raid's shared rule
   (`QD.COX.restore_intent`, any of attack/strength/ranged/magic 20 under
   base), the heal floor dropped when no restore is left (`QD.COX.brew_floor`).

### Rounds (scriptrun, seeds vanguards0..23, both tests)

| round | change | harness | room test | median ticks | heal doses (median) |
|---|---|---|---|---|---|
| base | most counted hp, every tick | 0/12 | - | died ~1030 | 28 |
| r1 | commit + gap budget, volley-blind prayer, restores | 23/24 | 23/24 | 662 / 746 | 15 |
| r2 | prayer on the next READ (volleys from `api_drive.projectiles`: locked orb/rock, land = first sight + duration // 30) | 22/24 | 23/24 | 697 / 694 | 8 |
| r3 | an unseen splat of mine counted at its max; HOLD (no swing this tick) when only it makes the swing unsafe | 23/24 | 20/24 | 976 / 1065 | 15 |
| r4 | the hold only on the tick the next swing is committed; trident within 4; regeneration counted | **24/24** | **24/24** | **622 / 656** | **7 / 10** |

What each failure taught:

- r2 seed 23 (reset): the trident's splat lands `ceil(end / 30)` ticks after
  the cast (end = the projectile's end cycle, 56 + 10 a tile: 2-3 ticks within
  4 tiles, 4 beyond -- fit over 197 casts). At 4 it is unseen when the next
  cast is committed, and two unseen-sized hits (27 + 29) opened the gap. Fix:
  count my unseen swing (`QD.COXVG.pending`: no new splat on that vanguard
  since my swing) at its max, and stand within 4 with the trident.
- r2 seed 13 (stuck 1900 ticks): the first safety test took the gap's max
  from the target's CURRENT hp, so with the other two 67 apart nothing was
  ever safe -- not even the highest. The gap is now taken at the target's
  max-damage end only (`QD.COXVG.safe`): hitting the highest is always safe.
- r3 (40% slower): the hold fired on every tick of the trident's flight. It
  matters only on the tick before the weapon is ready (`S.x2.swing_next`).
- r3 seed 5 / room 3 (resets in the end game): the counts ran 9-12 hp LOW by
  t1300 -- every vanguard regenerates 1 hp each 100 ticks (ticklog `npc_heal
  1` at t182, 282, ... on all three at once), which no splat shows; a kill on
  the "1 hp" one with the other at "63" was a kill with it at 73 (V17: left at
  1, reset). Now +1 to every living one under base on a 100-tick phase. The
  phase is guessed from first sight (off by up to 100 ticks: at most 1 hp per
  vanguard, inside GAP_MARGIN 8); the bar-rise learner never fired in 48 runs
  (the bar is not active between hits).

### r4 per seed (room ticks, heal doses, restore doses, prayer points spent)

Harness (`_cox_room_budget.lua`): 24/24, 0 resets; ticks 421-1114 (median
622); heal 0-11 doses (median 7); restore 3-7 (median 5); prayer 143-218
(median 162). Room test (`cox_vanguards.lua`, budget arrival + the relay's
pre-pot): 24/24 GREEN (0 resets, 0 trample, exit crystal gone); ticks 421-952
(median 656); heal 0-15 (median 10); restore 3-8 (median 6); prayer 143-233
(median 166). Restore doses include the prayer sips the shared supplies take.
Target switches 9-13 a run (base: 35-106).

Validation, no edits: harness seeds vanguards24..35, 12/12, 0 resets, median
595 ticks, heal 2-9 doses, restore 4-6.

Content note (not changed, out of scope): r1 harness seed 14, ticklog t1317,
the ranged and magic vanguards shelled (`npc_retype` -> 7526) while the melee
one, at 0-1 hp after a "left at 1" blow, did not; at t1327 it logged a retype
7527 -> 7527, and the reset followed at t1328. A 0-hp survivor skipping a
shuffle (and its +1) may be a `~cox_vanguard_shell_one` / death-queue
ordering edge.
