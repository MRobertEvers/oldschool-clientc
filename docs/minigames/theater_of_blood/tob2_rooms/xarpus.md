# Xarpus, Hard Mode, five raiders, on v2 (tob2_xarpus.lua)

Spec: solver_specs/xarpus.md (Normal trio); HM rules read from the content
(OSRS-Content tob_xarpus.rs2, tob_damage.rs2, tob.constant). v1: tob_xarpus.lua,
tob_xarpus_hm.lua. Kit: void + tentacle (melee, speed 4), no thralls.

## Threat field (absolute ticks; a window names the tick ENDS read)

| hazard | B term | window | dmg exp/max | prayer |
|---|---|---|---|---|
| pool (HM ring of 98 from fight tick 3, every spit/chain pool) | `B.hit` forbid, `sample="end"` (M3: run middle free) | now+1..h | 7/11 | none (typeless) |
| his 5x5 (stomp <= 9/tick; also skips a spit) | `B.hit` forbid | from the gather (24th exhumed gone) | 5/9 | none |
| spit/chain in flight (`S.landings`, v1 `_xarp_measure`) | `B.hit` 3x3 | L-1..L (v1's L may be one early: both) | 7/11 | none |
| its pool | `B.hit` forbid | L..h | | |
| spit slot ahead S_k = U+7+4k (aims at my end-of-S-1 tile; the stack makes it mine) | `B.mark` t_mark S-1, r 1 | S+2..S+3 (outer ring lands S+4) | 7/11 | none |
| its chains (aim at my end-of-S+2 tile, land S+5) | `B.mark` t_mark S+2, r 1 | S+4 | 7/11 | none |
| a landed spit's chains (aim at end of L-1) | `B.mark` t_mark L-1, r 1 | L+1..L+2 | 7/11 | none |
| P3 retaliation (HM: faced = the quadrant of the LAST landed hit, turned = its landing tick; a swing at W > turned from a tile in the faced quadrant, boundary inclusive: 50-75 x uplift) | NOT a tile hazard: the boss target is offered only when my step-1 swing is safe | d+1 | 62/105 | unblockable |

Exhumed heal orbs are not damage to me: they are a soft cost (`B.zone` require
on the tile, E+2..E+7, 3 hp a tick off it).

## Mode machine (re-derived each tick from v1 `_xarp_phase`)

- exhumeds: `exhumed` (one assigned: pull 1.0 + require zone) / `home` (5 homes, pull 0.3) /
  `gather` (24th exhumed closed: pull 1.5 to the stack's first tile, his 5x5 forbidden).
  Assignment, identical on every seat: each new exhumed (oldest first) to the raider ON it
  soonest by the end of E+2 (a busy raider counted from its own exhumed's tile once released
  at E'+7, 2 tiles a tick), then the earlier arrival, the nearer, the lower pid.
- stack (U .. last spit + 5): v1's step-back cycle, ported unchanged in rule (TOUR + lookahead
  pick): B_k at ends S-2,S-1; A_k beside him at S, S+1; B_k+1 at S+2,S+3. Pulls 1.5/tile/tick
  to the known tiles over the horizon; the marks above keep every tick end out of the splashes;
  the boss target (tentacle) is always offered (its value only fits the A ticks).
- stare: pull to my quadrant's side tile (QD.XARP_HM.QUAD: 1 SW, 2 SE, 3 NE, 4 NW, 5 SW).
  The boss target ONLY when I stand beside him AND my tile is not in the faced quadrant
  (inclusive), read at the end of d (a hit landing at d+1 would only free a swing at d+1).
  Otherwise no target: the plan cannot attack, and an attack under way is held (E3).
  No fixed rhythm: seats swing whenever safe, so the faced quadrant rotates by itself.
- done: leader takes the north gate's barrier copy and searches the skeleton (v1's loc ops).

## Value

`HP_PER_BOSS_HP = 0.05`: the room's unavoidable damage is ~0 (v1 xa1-8: 37 a run, all stomp
at the screech walk, avoidable), so taken/dealt gives ~0. A swing (exp ~10) = 0.5 hp: above a
click (0.05), below one tile of the stack pull (1.5), so the stack's position always wins.

## PROBE rows

1. Is the faced quadrant visible at decision d for a hit landing at d (face_x/face_z centre +-5)?
   Measure: retaliations (hp drop >= 38 in stare) must be 0; `hold` ticks per seat.
2. v1's landing tick L (seen + duration//30) vs the content's floor((87+5d)/30): splash hits
   cover L-1..L until settled.
3. Tentacle swing exp vs Defence 200 (value constant).
4. The walk at the screech: no tick end under his 5x5 (all128c's 1,940 damage): the decision log's
   `walk` tiles near (32..36, 33..37) after Q.

## PASS

No raider dead; 0 damage from pools, splashes, stomp and retaliation (the room has no unavoidable
damage); heal orbs <= v1's; ticks <= v1's on the same seeds.
