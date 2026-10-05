-- quest-driver / raid_play_tob_nylocas: the The Nylocas plan for t.raid.play
-- (raid_play.lua), its own driver part (raid seam29 play_library_own_files) so
-- the room seam that writes it touches no other room's file.  Until it has a
-- `decide` function the plan answers `unsupported` with its `unsupported` line.

QD.raid._play_plan("tob_nylocas", {
    room = "nylocas",
    unsupported = "the Nylocas plan is raid seam30's play_tob_nylocas (SEAM_TRIAGE_2026-10-05i.md); PLAY_NOTES.md 'The other four rooms' holds its sourced mechanics",
})
