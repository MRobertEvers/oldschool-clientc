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

## matthew-mbp-m4-raid-b1-seam3 (2026-10-03, what the first ToB room pass exposed)

Content OSRS-Content 93707f5d60, parent 4767f8589. Conformance 262/262 (162 verbs + 100 seam rows); suite 118 green + deserttreasure RED (the baseline); C selftest 11, the baseline set; ::tobrun OK 56.

- `tob_shared_combat_scripts`: LANDED. tob_retaliate.rs2 (93 no-op `[ai_queue1]`
  bindings: Sotetseg's attacks all on his 5-tick clock, every 8138 +1), a cast is a magic
  hit and recomputes the combat varps (12 Fire Bolts: 10 damaging, 6 Hagios killed), the
  raids' personal hit queues cleared at death (no hit after the respawn). Still open: the
  ENGINE retaliate=no row for CoX/ToA/Zulrah, combat varps never recomputed on a stat
  change, powered staves and magic specials still the weapon's style, the doubled death
  message.
- `tob_sotetseg_room2`: LANDED. First attack 6, Entry maxima 20 melee / 22 ball, hp kept
  across both maze retypes, path lit on the runner's first realm tick, off on 3 = 1, the
  maze waits for a late runner; closer moved the ::tobrun pin to 6. Still open: an eat on
  proc+2 lands the runner at proc+5, Entry prayed melee 10 derived, the HUD bar at 17 %
  on the death tick, melee_roll_adjacent / death_ball_hit_entry_solo not re-measured,
  tob.constant's maze_seen comment stale.
- `tob_maiden_room2`: LANDED. Blackstorm rows name her world slot; no throw at or landing
  on a dead or departed raider; Entry pool 10 + 2c disclosed as E [M121] (divisor 1).
  Still open: the HIT_NPC tick-log row has no dealer (engine), Entry pool unsourced.
- `tob_nylocas_room2`: LANDED. Entry explosions 1-8 (wiki), killed standing small +2
  (death_delay=1). Still open: walking small +3/+4 (engine arrive delay,
  torirs_server_combat.c:2830-2836), protection prayer vs wave nylocas (spec gap), Entry
  Vasilias plays the Normal 10-tick / 2-attack windows against the Entry rows, [M93]
  promotable to D.
- `tob_verzik_room2`: LANDED. Entry P1 60 (30 prayed), urnbomb 16, slam 16, stomp ~34, P3
  melee 36, P3 auto 20; the P2 stomp is rolled in every mode; P2 footprint and bounce
  measured (adjacent slam 75 %, under her the stomp). Still open: Entry gaps (lightning,
  crab blasts, Athanatos landing, blood spell, power blast, web snap), P1 bolt rows
  unattributed (npc_slot -1), P3 melee also hits under her (unverified).
- `tob_xarpus_room2`: LANDED. Poison capped at 11 (Entry halved, max 6), stomp pair
  capped at 9 per tick (Entry 5). Still open: the buff formula [M70] under the cap, Entry
  P3 retaliation 50-75 vs "38+", a proposed `xarpus.p2.stomp_max_entry` row.
- `tob_bloat_room2`: LANDED. Later walks 34..42 (down-to-down 68), Entry hand 20-25
  [M62], supply chests placed after Bloat (wiki pin) and Sotetseg, reward once, Entry 10
  bandages, one stamina per chest. Still open: bandages have no Heal script, the
  Sotetseg chest flank unsourced, the chest never shows open, points are shared by the
  party.
- `npc_facing_read`: LANDED. `t.npc.state` face_x/face_z/face_tick, `t.npc.await_face`,
  tick-log kind `npc_face`; rows npc.await_face and seam.npc_facing_read PASS (Hans);
  Xarpus P2/P3 gaze read in face_xarpus2. Still open: no C selftest stanza pins npc_face;
  a `::tele` on a run's first tick can crash the client.
- `attack_exact_copy`: LANDED IN PART. A failed attack on another slot drops the
  engagement stamp, a covered press names the copies the menu offered, row
  seam.attack_exact_copy_on_one_tile PASS. REVERTED by the closer: closing a stale menu
  before every press (its tick moved hauntedmine, childrenofthesun and thefeud green ->
  RED; each green again without it). Still open: a press inside a stale menu can take
  another copy's row (CONTENT_BUGS.md); neither tob_nylocas nor tob_verzik is unblocked.
- `coverage_scope_sidecars`: LANDED. Six `<room>.scope.tsv` sidecars, every row
  classified; closer skipped `.scope.tsv` in spec_check.py and raid_coverage.py's
  whole-raid glob. Still open: `parse_row` grades only the first element of a measured
  list; one scope per row (no normal+hard, party ignores the mode).

## matthew-mbp-m4-raid-b1-seam4 (2026-10-03; parent 80b6fbc9d, OSRS-Content a44e3d97bf)

Triage `SEAM_TRIAGE_2026-10-03b.md`. Conformance 263/263 (162 verbs + 101 seam rows);
suite 118 green + deserttreasure RED (the seam1 baseline, nothing moved); C selftest 11
failures (the baseline set); ::tobrun OK 56.

- `tob_entry_records_and_readouts`: LANDED. Entry Athanatos pool 30, Defence/Magic 40
  (cache record 10844) on the spawned type; Normal/Hard Athanatos Defence/Magic 50; story
  records audited against cache_npc_*.txt; ::tobboss prints att/str/rng/mag and size,
  ::tobpurple added (s4e_verzik_entry_c: killed in 4 hits, P3 at tick 128). Still open:
  Entry Verzik and Maiden fight at Normal levels, three Normal records have no levels,
  Entry P2 lightning and the P3 pool 400; tob_verzik's own test stops at P2 cycle 4.
- `tob_maiden_room3`: LANDED (3 of 4 rows). Blood spawns 1000 permille, two extras on
  every throw, Entry trail 2-5. The death rows were NOT changed: four blert raids show
  K+1/K+5/K+9 as ours does. Still open: restate `maiden.death_a_len`/`death_total` in the
  spec table, dying_a id at K+3, Normal trail 5-13; the committed tob_maiden test lost its
  accidental pool rows (copy 60/1, coverage 32 of 39) and must drive them on purpose.
- `tob_sotetseg_room3`: LANDED. Solo Entry death ball a flat 15 (three sources), ball max
  22 confirmed over 40 balls, first attack 6 and the clock unchanged. Still open: the spec
  row's tolerance to `exact` and the Entry Mode source; gear reduction on the flat 15
  unsourced; the author attempt's maze-2 tornado step.
- `tob_xarpus_room3`: LANDED. The generator lets a minigame own a death queue
  (MINIGAME_DEATH_QUEUES), so the Entry kill plays 8063 for 2 ticks with the book and no
  bones; Entry retaliation floor 38 (ceiling 57 [M123], plan row added); stomp already
  within cap (Entry 5, Normal 9); exit route through the gate documented. Still open:
  K -> retype 3 ticks (death_delay), the exit opens mid-fight, no book in Normal/Hard,
  `xarpus_death_story` has no overlay, the author attempt's exit and uplift rows.
- `npc_state_size_and_stale_menu`: LANDED. `size` on every npc pool row (row
  seam.npc_state_size PASS: goblin 1, cow 2; npc.state_text grades size); a stale menu is
  dismissed only when the press would select another copy's row (5 -> 0 hits on the copy
  not asked for; the three seam3 regressions stay green and tick-identical). Still open:
  a press on a stale menu's Walk here / Examine / Cancel row or title bar behaves as
  before, because three green quest timelines depend on it.
