# Seam ledger -- the waves loop

One line per seam per pass: the key, what happened to it (landed, kept as a patch, or
reverted), and what remains open. The detail is in each pass's state directory
(`build/seam_state/<pass>/`) and in `DRIVER_NOTES.md`, `CONTENT_BUGS.md` and
`FORKED_FROM.md` beside this file.

## matthew-mbp-m4-waves-b1-seam1 (2026-10-03)

- driver_port; landed (the raid loop's driver rows ported: t.tick, t.ticklog.*, t.prayer.*, t.npc.state/await_anim/await_face, t.world.spotanims/projectiles/hazard_at, t.player.step_tick, the fast press; server tick-log hooks; tools/waves_gate run/gate/suite; conformance 260/260 PASS; the quest suite 131 runs, 129 green under the gate with the Quest Helper guides found, the two red are red on HEAD). Open: QD.wave is empty and `wave` is not in PARTS (seam pass 2: t.wave.enter/state); ENG-1..ENG-4 in CONTENT_BUGS.md unmeasured on this branch; the three seam rows sit after seam.drain_survives_xp_gain (row order differs from the raid file, and that v3 row is fragile against the stat_restore timer); helper_coverage.py cannot find quest-helper from this worktree (DRIVER_NOTES.md); wanted and eadgar red on HEAD under grader frame_entries (v3 ca6b4cba5) -- wanted's fix 43ed0d817 is on the quest loop's b56 branch only.

## matthew-mbp-m4-waves-b1-seam2 (2026-10-03)

- wave_enter_state_pause; landed (t.wave.state/enter/await_wave/await_clear/pause/resume, `wave` in PARTS; conformance rows moved by the closer to the end of the harness, just before `finish`, behind a Protect from Missiles stage (ENG-19 and ENG-2 moved rows downstream in two earlier places)). Open: the clear->paused leg and the logout-button pause are not playable on this content (ENG-5, ENG-8); resume by click is blocked by the entrance (ENG-6, ENG-7); wave 69 enter not proved.
- prayer_flick; landed (t.prayer.set_on_tick/switch/flick on server ticks; t.prayer.set decides "already so?" from the server's varbit; T-1 measured on a city guard and the Inferno bat; a one-tick flick costs 0). Open: prayer regenerates (ENG-10); drain mechanism differs from LostCity (ENG-11); bat flight not constant (ENG-12).
- los_and_pack; landed (t.world.los and t.npc.pack read from the embedded server's own line routines, new C torirs_server_los_query.c and torirs_plugin_drive_los.c; transitional nil guards removed by the closer). Open: the Inferno pillars block no line of sight (ENG-13) and the nibblers have no engine target (ENG-14); ENG-3 settled, not a defect (ENG-15).
- supplies_by_dose; landed (t.player.drink and t.inv.doses; conformance rows moved by the closer before the shop block with their own ::clearinv, the drink row accepts the re-attack's own failure word). Open: eat/drink costs measured for seam pass 3 (ENG-16, ENG-17, ENG-18); no eat verb with then_attack yet.
- colosseum_choices; not a seam this pass (design row: no Colosseum interface is opened by the server yet).
