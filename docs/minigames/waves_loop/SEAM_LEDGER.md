# Seam ledger -- the waves loop

One line per seam per pass: the key, what happened to it (landed, kept as a patch, or
reverted), and what remains open. The detail is in each pass's state directory
(`build/seam_state/<pass>/`) and in `DRIVER_NOTES.md`, `CONTENT_BUGS.md` and
`FORKED_FROM.md` beside this file.

## matthew-mbp-m4-waves-b1-seam1 (2026-10-03)

- driver_port; landed (the raid loop's driver rows ported: t.tick, t.ticklog.*, t.prayer.*, t.npc.state/await_anim/await_face, t.world.spotanims/projectiles/hazard_at, t.player.step_tick, the fast press; server tick-log hooks; tools/waves_gate run/gate/suite; conformance 260/260 PASS; the quest suite 131 runs, 129 green under the gate with the Quest Helper guides found, the two red are red on HEAD). Open: QD.wave is empty and `wave` is not in PARTS (seam pass 2: t.wave.enter/state); ENG-1..ENG-4 in CONTENT_BUGS.md unmeasured on this branch; the three seam rows sit after seam.drain_survives_xp_gain (row order differs from the raid file, and that v3 row is fragile against the stat_restore timer); helper_coverage.py cannot find quest-helper from this worktree (DRIVER_NOTES.md); wanted and eadgar red on HEAD under grader frame_entries (v3 ca6b4cba5) -- wanted's fix 43ed0d817 is on the quest loop's b56 branch only.
