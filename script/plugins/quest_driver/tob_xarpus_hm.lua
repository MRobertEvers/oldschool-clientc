-- quest-driver / tob_xarpus_hm: XARPUS, HARD MODE, five raiders.
--
--   t.raid.xarpus_hm_solve(opts) -> result, detail, record
--
-- The Normal solver (tob_xarpus.lua: the exhumeds, the step-back stack, the
-- Dawnbringer) with Hard Mode's stare (wiki Theatre_of_Blood/Hard_Mode
-- :353-357, docs/minigames/theater_of_blood/HMT_STRATEGIES.md):
--
--   the ring   "Xarpus will spit poison around the edges of the arena,
--              covering up to two tiles' worth": real pools, which the
--              shared measure reads as pools; the stack's tour skips any tile
--              that is one, and nothing else walks the outer two rings.
--   the stare  "Xarpus will immediately focus on the quadrant he was attacked
--              from, overriding his natural rotation. The team should spread
--              out on all four quadrants and attack on different ticks ... the
--              quadrant with two players should ... attack on the same tick
--              together." Every seat holds a quadrant (SW, SE, NE, NW, the fifth
--              sharing SW) and swings on the shared 4-tick rhythm only when he
--              is not facing it: each swing turns him to its quadrant, so the
--              next rhythm tick is everyone else's.
-- ==========================================================================

QD.XARP_HM = {
    QUAD = { [1] = "SW", [2] = "SE", [3] = "NE", [4] = "NW", [5] = "SW" },
}
