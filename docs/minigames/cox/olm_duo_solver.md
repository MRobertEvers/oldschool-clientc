# The Great Olm, a duo: the solver's plan (Synq's mage skipping)

The method is Synq's *The EASIEST Mage Skipping Guide 2026 (Duo Olm)*
(`sources/transcripts/yt_8Ta9VkVaJ7M.md`; the tiles read off the video's frames).
The mechanics are `OLM_SPEC.md` (the content's own rules, RSProx-checked for
parties: `docs/rsprox-analysis/cox/olm.md` "Parties"). The solo solver
(`script/plugins/quest_driver/cox2_olm.lua`, `olm_solver.md`) is the base: one
file, a `role` option — `skip` (the mage skipper) or `p2` (player 2) — and the
model counts both raiders.

## What changes in the model

- **Both raiders in the count.** Every step (`CO.step`) adds the partner's bin
  to the view counts; "nobody in view" means neither raider. The partner is
  read off `api_drive.players()` each tick (its tile at the end of the tick, the
  same read as mine).
- **The partner's hits steer.** A claw hitsplat that is not one of my launches
  is kept as a landing on the tick it shows (already: an "orphan"), so the
  partner's casts on the mage hand count toward its side.
- **The teleport is a pair.** Two raiders are always the pair: the swap at
  +10 and each hit +11 for the distance between them; one tile, no effect.
  Both meet on one tile (below).

## Synq's method, in the model's terms

West wall (the east mirrors z -> 72 - z): the mage (right) claw is SOUTH,
footprint z 29..33; the melee (left) claw NORTH, z 39..43. The views: S
z 26..36, M 31..41, N 36..44. Synq's "left" is the mage hand's side, "right"
the melee hand's (facing Olm).

**Player 2** ("go to the far safe spot … never go past this tile. Going past
this tile will cause the head to face the center"): z 42 — the last row of
the north-only view and the only one inside the trident's Long range (9) of the
mage claw (gap 9). Mage on Long range at the mage hand; never off that view.

**The skipper** ("the more experienced player"; long range): on the mage hand
too. With player 2's hits steering the south, the cycle is (each read is the
end of the tick before the step):

| step | head faces | read: the skipper | the step |
|---|---|---|---|
| special | S | right (z 37..41: out of S) | nobody in S: skipped; the head turns N (player 2 and me) |
| basic 1 | N | left (out of N) | at player 2 ("each person tanks a basic") |
| empty | N | left | the south (me + the steer) outnumbers player 2: the head turns S |
| basic 2 | S | left | at me ("always tank basic two") |

Left is the south-only bins (z ≤ 30, by the mage claw) or z 31..35 when the
model says the steer holds; the model picks the nearer that gives the step
above. Off the cycle (an early run, a missed steer, a teleport) the step search
with both raiders recovers it (Synq: "pay attention to the next special … tank
basic two and run the head to set it right back up").

**Entry** (both): overload, brew, restore before the barrier; the special
attack on the melee hand on entry (a clench chance); the skipper then stands
where the head turns right, and runs left as it turns.

## The phases

| phase | skipper | player 2 |
|---|---|---|
| P1, P2: the mage hand up | mage skip (above); the trident on Long range | home (z 42 / 30), the mage hand on Long range |
| the mage hand dead | the melee hand from the thumb (12, 39) | the melee hand from (12, 42): still north-only |
| teleport | wait at the thumb | "wait for one basic attack before grouping up", then onto the skipper's tile |
| P3 | the solo states: the melee hand low first, the mage hand low, kill the melee hand first ("if you kill the mage hand first, you run the risk of the melee hand healing"); skip the healing-hand special (run after basic two) | home; the hands in the same order |
| the head | turn the head so each tanks one basic (the step search with both raiders) | home, the crossbow at the head |
| healing pools | on the pool for its final tick ("the same tick the head does its next attack") | the same |

Flame walls, lightning, crystals, acid, the burn: the solo solver's hazards,
unchanged (Synq: "tank basic two at the center of the head … if you do get
flame walled, it'll get nulled"; the planner's walls already price it).

## Not followed

- **Thralls** ("strongly recommend"): no Arceuus thralls, death charge or vile
  vigour in this content. Without them the steer is the casts' alone.
- The watering can: the kit's Water Strike douses instead (the solo kit).

## Kit

The solo test's (We Do Raids minimum, OLM's head kit) on both seats: the trident
(the hands), the tentacle (the melee hand), the dragon crossbow (the head), the
BGS (the entry special), the raid's (+) potions.

## GREEN

The head dead, both raiders alive. Reported: specials fired / skipped in the
mage-skip states, autos at each seat, teleports nulled.

## Round 1 (2026-10-18)

`test/raids/cox_olm_duo.lua`, seat 1 the skipper, seat 2 player 2; `--name olmduo0..4`:
**5 of 5 green** (the head dead, both alive), no script errors.

- **The model is exact with two raiders:** step mismatches 0-1 of ~180 a run, and 0 for every
  content check (auto, special, clench, clock, burst, ghost, wall).
- **The mage skip:** once its cycle is entered it holds for the rest of the mage state
  (no cycle -> recover in any run). Specials skipped in the mage states: 9/13, 7/9, 8/10,
  7/9 and 10/12. The ones that fire are the phase's opening crystal burst, before the
  cycle is set up (Synq: "if it doesn't [cripple] make sure to dodge the crystal out first").
- **Each tanks a basic:** autos at the skipper 51-80 a fight, at player 2 51-80.
- **The teleport:** every pairing (4-6 a fight) found the pair on one tile at the swap's
  read (seed 1: 5 of 5, no swap hit).
- Damage taken: 238-342 a seat over the fight, paid from 6 Xeric's aids each.

Found on the way, for every Olm solver: the content's P1 first action moved to rise + 6
(`^cox_olm_first_act_p1`, RECORDED, OSRS-Content ff130e6540); the solver counted + 9 and
every step's kind was one out. Fixed (`CO.V.FIRST_P1 = 6`); the solo test is green on it
with 0 mismatches.

Open: the opening crystal burst (the entry "go to this tile so the head turns right" is the
step search's, not Synq's tile); a 48-seed sweep.
