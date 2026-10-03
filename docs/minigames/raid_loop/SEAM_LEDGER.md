# Raid seam ledger

One line per seam after each raid seam pass: the seam key, whether it landed, and what
is still open. Triage: `SEAM_TRIAGE_<date>.md`. The facts a test author needs are in
`DRIVER_NOTES.md`.

## matthew-mbp-m4-raid-b1-seam1 (2026-10-02, the driver seam of RAID_ORCHESTRATOR.md section 4)

- `prayer_set_read`: LANDED. `t.prayer.set/read/points`, conformance rows prayer.set,
  prayer.read and prayer.points PASS. Still open: the overhead icon has no reader (needs
  a headicon field on the drive player snapshot, in C), and the curses book is not
  handled.
- `client_npc_state_and_tile_hazards`: LANDED. `t.npc.state/state_text/await_anim`,
  `t.world.spotanims/projectiles/hazard_at`, and the client keeps the spotanim id on map
  graphics and projectiles, with six conformance rows PASS. Still open: seq and
  spotanim ids are numbers because api_drive.symbol has no seq or spotanim kind. An
  ENGINE finding: an ordinary npc hit by magic may never retaliate (man: one swing then
  none, or none at all in the full harness; cow: none). The npc.await_anim row works
  around it with a player punch.
- `server_tick_log_and_step_tick`: LANDED. `t.tick`, `t.ticklog.start/mark/rows/gaps/slot`,
  `t.player.step_tick`, and the server tick log (torirs_server_ticklog.c), with seven
  conformance rows PASS. The LostCity addXp rule (a drained stat stays drained) is
  proved by its C selftest stanza. That fix moved the quest test deserttreasure from green
  to RED: the Ice Path chill (chill.rs2, ported from LostCity) now really drains, so the
  troll fight took 466 ticks instead of 376 and magic was below Fire Blast's 59 at
  Kamil. An A/B run without the hunk passed 249/249. The quest loop's test needs a
  restore before Kamil. Still open: embed teardown does not disable the log
  (it leaks at exit only), and a relog turns the log off.
- `raid_room_entry_verbs`: LANDED. `t.raid.enter/state/leave/start_tile`; content
  `::tobmode`, `::tobstate`, `::toastate`, `::coxseed`, `::coxgoto` and `::coxstate`,
  all debug affordances that change no game behaviour. Four conformance rows PASS; they
  run last in the harness. Still open (CONTENT): the ToA barriers that
  `~toa_add_barrier` spawns do not cross into the arena (blocks zebak, kephri, akkha,
  baba and wardens); Wardens P1 lands started; CoX lands at the room centre, with no
  door-tile landing.
- `raid_tests_directory`: LANDED. `test/raids/` (README, fixtures/fresh_lumbridge.ini)
  and `tools/raid_gate/{run,gate,suite}.py`, using the TORIRS_QUEST_TESTS_DIR and
  TORIRS_QUEST_PUBLISH_DIR overrides in quest_list.py. No conformance row (tooling
  only). Still open: raid coverage from the encounter spec tables is a later seam.
- `client_ground_obj_merge`: LANDED. Every OBJ_ADD is one client ground row, and
  identical cold piles all land. Two seam rows PASS
  (seam.two_identical_drops_are_two_ground_rows, seam.two_cold_identical_piles_both_land).
  Still open: OBJ_COUNT and OBJ_DEL carry no old count or quantity on the client path;
  app_placeholder.c looks up the oldest row, not the first row without an element; there
  are stale comments in world.h, world_test_unit.c and pointer.lua.

## matthew-mbp-m4-raid-b1-seam2 (2026-10-02, the Theatre of Blood content the spec pass found wrong)

Content OSRS-Content ebe115ad09, parent 8bfe2d9aa. Every fixed CONTENT_BUGS.md row now
names that commit; what stays open is listed there under "Still open after seam2". No
verb changed: conformance 259/259, suite 118 green + deserttreasure RED (the seam1
baseline), C selftest 11 failures (the pre-existing non-ToB set), `::tobrun` OK 56.

- `tob_mode_scaling_and_shared_procs`: LANDED. Entry stacking (`~tob_entry_scale`),
  mode records for Bloat/Sotetseg/Xarpus with cache levels in tob.npc, Hard tables,
  Verzik 2625/3062/3500, 1-tick watchdog, boss found from the fight tile, `::tobscale`
  and `::tobboss`; the room fixers' constants and pins applied. Still open: Maiden and
  Verzik (and Maiden's crabs/slugs) stay Normal ids in Entry/Hard; Xarpus Entry death
  skips `~tob_xarpus_died` (generated drop trigger).
- `tob_maiden_room`: LANDED. Transmog keeps hp, Hard period 10 - ceil(c/2), solo Hard
  set of seven, slugs step every tick, halving, Entry slug 10, death free at K+9
  (s2close_maiden_hard), no solo ramp; closer: dying crabs left to the engine at her
  death (selftest SILENT DEATH). Still open: dying_a retype at K+3 (engine corpse stage),
  spec row maiden.blood_spawn_step to restate, splat drawn 11 vs blert 14, Hard scuff.
- `tob_bloat_room`: LANDED. Rise tick walks (D+33), lockout 5, free hand clock, no tank
  hands, strict gates, turn 1/17 and 1/7, down chance refit 25/28, Entry 8/40. Still
  open: later walks one tick short (33 vs blert min 34, down-to-down 67 vs 68), Entry
  flesh damage unsourced, bloat.tsv turn-rate rows to restate.
- `tob_nylocas_room`: LANDED. Lifetimes 52/55, natural splits, killed big +6/+7, split
  tiles, flicker +5, aggro on the box edge, Vasilias two per form and SW landing, Hard
  cap 75, bite 0.9, no Prinkipas self-destruct; closer: `~tmpk_` block folded into
  constants, a nylocas killed mid-explosion dies through the death path, the C stanza
  waits for the despawn and defends the supports (Vasilias lands, 36 colour changes).
  Still open: killed small +3/+5 vs +2 (tob.npc death_delay + an engine arrive-delay
  row), first-attack offset by style pair.
- `tob_sotetseg_room`: LANDED. Ten balls then the death ball and +10, flight 16 at any
  distance, melee +1, maze stall 5 / teleport +3, cycle on the world phase, post-maze
  guard, defence floor 100; closer: the three timing procs are tob.constant entries.
  Still open: both mazes of one room never driven, the guard's melee case, the M29 cue
  half, Hard's divided maze.
- `tob_xarpus_room`: LANDED. 75 % opening of his pool, Hard ring on fight tick 3, Hard
  tables 9/20/24 and solo gap 12, heals 21 / Entry 6, handoff 7, Hard P3 gaze; closer:
  the eight literals read their constants. Still open: Entry death presentation, Hard
  duo and four/five-man figures by code only.
- `tob_verzik_room`: LANDED. Bolt T+3, Athanatos from attack 0 then 20+ and +6, six-tick
  P2->P3 with 8118, yellows 14/20 then +7, single red, green ball 221, webs 42, P3 max 33;
  closer: her pool carried at every form change (solo P1 shield 3750 -> 1500), Entry
  stacking, Entry Defence 10/120/120, reds 20 and crabs 3, reds sized as their pool
  (solo 150, were 200). Still open: Entry P3 pool 600 (reuses 400), the 8373 form not
  worn, P3 34-at-enrage by code.
- `tools_verify_tob_timings`: LANDED. Reads the current constants (both KeyErrors gone),
  new blert checks, `--ticklog` over our ticklog.tsv with `--start-tick/--start-mark`.
  Still open: no room-start row in the tick log; the Xarpus spawn gap is a note on blert
  streams (no party scale); a stale `--strict` mention in the docstring.
