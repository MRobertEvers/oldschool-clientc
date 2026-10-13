-- quest-driver / tob_bloat_hm: THE PESTILENT BLOAT, HARD MODE, five raiders.
--
--   t.raid.bloat_hm_solve(opts) -> result, detail, record
--
-- The Normal solver (tob_bloat.lua: the lap, the watchers, the hide, the
-- down) on Hard Mode's numbers (wiki Theatre_of_Blood/Hard_Mode :319):
--
--   turns   "The boss can change directions multiple times rather than once
--           per rotation": his turn cooldown is 16 walking ticks, not 32
--           (tob_bloat.rs2 ^tob_bloat_turn_cooldown_hard), so the reversal
--           branch is in the watchers twice as often.
--   hands   "parts will fall from the ceiling for the entire duration of the
--           fight": the shared spec already forbids every shadow in every
--           phase, the down included.
--   seats   five stands beside the east barrier.
-- ==========================================================================

QD.BLOAT_HM = QD.raid._tob_derive(QD.BLOAT, {
    TURN_CD = 16,
    -- (the barrier is three tiles wide, z 30..32: seats 4 and 5 stand behind
    -- seats 2 and 3 and cross onto their tiles)
    SEAT_X = { 41, 41, 41, 42, 42 },
    SEAT_Z = { 31, 30, 32, 30, 32 },
})

function QD.raid.bloat_hm_solve(opts)
    assert(QD.raid.tob_mode == "hard", "bloat_hm_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.raid._bloat_run("bloat_hm_solve", QD.BLOAT_HM, opts)
end
