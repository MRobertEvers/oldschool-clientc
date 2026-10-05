-- quest-driver / raid_play_tob_maiden: the Maiden of Sugadinti plan for t.raid.play
-- (raid_play.lua), its own driver part (raid seam29 play_library_own_files) so
-- the room seam that writes it touches no other room's file.  Until it has a
-- `decide` function the plan answers `unsupported` with its `unsupported` line.

QD.raid._play_plan("tob_maiden", {
    -- Maiden: the full strategy table is PLAY_NOTES.md "Maiden"; the decide
    -- function is the re-author pass's (seam27 proves the library on Bloat).
    room = "maiden",
    boss = { entry = "tob_maiden_100_story", normal = "tob_maiden_100", hard = "tob_maiden_100_hard" },
    -- spec maiden.cad / maiden.first (grade B): first attack tick 9, every 10.
    attack_first = 9, attack_every = 10,
    modes = { entry = {}, normal = {}, hard = {} },
    walk_prayers = { "protectfrommagic" },
    down_prayers = {},
    unsupported = "the Maiden plan is raid seam30's play_tob_maiden (SEAM_TRIAGE_2026-10-05i.md); PLAY_NOTES.md 'Maiden' holds its full strategy table",
})
