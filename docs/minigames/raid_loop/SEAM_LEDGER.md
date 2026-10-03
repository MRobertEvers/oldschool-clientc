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

## matthew-mbp-m4-raid-b1-seam5 (2026-10-03; parent 40fa35e49, OSRS-Content d2134f89f5)

Triage `SEAM_TRIAGE_2026-10-03c.md`. Conformance 264/264 (162 verbs + 102 seam rows);
suite 118 green + deserttreasure RED (the seam1 baseline, nothing moved); C selftest 11
failures (the baseline set); ::tobrun OK 56 (fixers).

- `attack_fast_path`: LANDED. ticks <= 2 or `opts.quick` presses fast (one aim, one press,
  one re-aim, one more press; a covered answer names the offered copies by slot); the kill
  wait re-presses a fast fight fast; row seam.attack_fast_path PASS; the quest press is
  tick-identical (chompybird's three ticks=1 probes now press fast, green). Still open:
  tob_nylocas is a tactics problem (out of food at click+448 with the cap lifted); a far
  named-copy cast silently not cast; the slow cast's "never ran" detail when XP was paid.
- `tob_verzik_room4_and_entry_records`: LANDED. Entry pillar 200, Entry levels per phase,
  P3 pool 600, cache levels on maiden_elemental, the blood slugs and the reds, the Maiden
  record lines applied. Still open: Entry P2 lightning unsourced (kept Normal 48); barrier
  pool still P1 + 2 x P2; P2 overkill and the Athanatos heal carry into P3; the tob_verzik
  attempt must drop its 1100-total rows (out of food in P3 at 626).
- `tob_maiden_room4`: LANDED. Entry/Hard Maiden, crabs and blood spawns are their mode's
  records (Entry def 80, 140s; raid.lua lists them); Normal 70/50/30 had Defence 1 (fixed);
  the dodged-throw cap is right (61 throws, max 1); the freeze curve named and driven with
  Ancients. Still open: `::tobboss` and the `::tobcrab*` debugprocs know only Normal/100 %
  forms; `~tob_spawn_boss` still adds the Normal body; Rush/Blitz skip the curve; the
  committed tob_maiden test must use the `_story` symbols.
- `tob_sotetseg_hit_delay`: NOT LANDED (no file changed). The cause is the eat:
  food.rs2 `p_delay(^eat_delay)` holds every queued npc hit, where LostCity's consume.rs2
  never p_delays; engine and raid queues already match LostCity. Proved in a scratch content
  copy (+1 x8 with an eat, arena 77/77). Open: a food.rs2 port (new server-only varp) plus
  the other consumption and potion `p_delay`s, as its own seam with an eat-heavy quest set.

## matthew-mbp-m4-raid-b1-seam6 (2026-10-03; parent 6049cf9bd, OSRS-Content 2cddff56d5)

Triage `SEAM_TRIAGE_2026-10-03d.md`. Conformance 265/265 (162 verbs + 103 seam rows);
suite 118 green + deserttreasure RED (the seam1 baseline; troll and regicide moved RED
with the eat port in and are green again without it); C selftest 11 failures (the
baseline set); ::tobrun OK 56.

- `eat_delay_port`: LANDED 2026-10-03 on the owner's decision, after the closer had held it
  back: OSRS-Content 7936c59bf9 and the parent commit "raid-driver: the eat-delay port lands".
  LostCity consume.rs2's clock shape (consume_shared.rs2, varps 7218-7220; no p_delay or
  p_stopaction in any consume script; +3 on a running weapon delay). Conformance 267/267
  (162 verbs + 105 seam rows: seam.eat_does_not_hold_queued_hit '+1,+1,+1 eaten',
  seam.eat_delay_clocks 'no eat 4,4; eaten 7,7'); C selftest 11 failures, the baseline set
  (the full-health bite waits 3 ticks); ::tobrun OK 56. Suite 116 green + deserttreasure RED
  (seam1 baseline) + troll and regicide green -> RED, accepted: troll row 31 player.died at
  tick 465 (all 26 sharks eaten at the Troll general, general 1/30), regicide row 205
  goKillGuardAtSecondForest-walk-toForests (died at the end of leg 4, out of shark after the
  Tyras guard, then the tripwire snag and poison); the quest loop re-authors both with more
  food or prayer. Still open: the ToA supply drinks' p_delay(1); the unported fast-food data.
- `tob_nylocas_prayer_and_damage`: LANDED. A wave nylocas's swing is 0 under the matching
  protection prayer (0 of 52 matched swings landed); Vasilias's prayed melee writes a 0
  row; row seam.nylocas_protect_blocks_wave_hit PASS. Still open: spec rows
  nylocas.prayer_reduction and vasilias_prayed_max for the spec pass; the Entry/Hard
  prayed max 17 is unsourced; no accuracy roll; magic-style gear reduction; tob_nylocas
  must pray during the waves.
- `tob_verzik_room5`: LANDED. Three pools end to end (1300 solo Entry, 6750 Normal),
  overkill dropped and heals clamped at each phase, thresholds compared whole (120/600
  enrages, 121 does not; reds 140/400), every-mode readouts and ::tobboss phase_hp. Still
  open: she is attackable through the fall; the first Athanatos roll has no ceiling (18
  against 0-12 in one run); she wears the Normal forms in Entry; tob_verzik's pool rows
  must read ::tobboss phase_hp.

## matthew-mbp-m4-raid-b1-seam7 (2026-10-03): presentation

Triage `SEAM_TRIAGE_2026-10-03e.md`; parent cf5694e9f, OSRS-Content a006110486. No verb
changed; conformance 275/275 (162 verbs + 113 seam rows, eight new). Suite 116 green +
deserttreasure, regicide and troll RED (the baseline, nothing moved); C selftest 11
failures, the baseline set; ::tobrun OK 56.

- `ticklog_sound_and_music_rows`: LANDED. `sound`/`music`/`jingle` tick log rows (the
  world.c and combat.c hooks kept); seq_frame_sounds.py; rows seam.ticklog_sound_rows and
  seam.ticklog_music_row PASS. Still open: the jingle row and the `distance` source are
  undriven; area sounds send loops 0 and are silent (sound.rs2); no player spotanim/anim
  kind.
- `tob_maiden_presentation`: LANDED (the blackstorm double removed; row
  seam.maiden_blackstorm_sound_once). Still open: 32972/32973 and 3989 unsourced;
  maiden_spawn re-graded a later quest's asset.
- `tob_bloat_presentation`: LANDED (stun graphic 1575, two doubles removed; row
  seam.bloat_down_sounds_in_band). Still open: fly variants 1/4/6 and three locs
  unsourced; the stun graphic has no tick log row.
- `tob_nylocas_presentation`: LANDED (small death graphics, sizemid projectile, Vasilias
  and Prinkipas land with 9030, five doubles removed; row seam.nylocas_presentation).
  Still open: 7990/8001 and 3982/3993/3946/3959 unsourced; the small's 'turn' seq.
- `tob_sotetseg_presentation`: LANDED (death-ball sound and message, sharer impacts, rag
  graphic, tornado sounds, maze-proc seq, plain/dark floor, the melee double). Still
  open: the floor turns plain at fight start, not on entry (tob_raid.rs2 one line); the
  tornado stalls when the runner stands; Hard Mode's divided maze (a mechanic); 1603,
  3970, 8141, 33036 unsourced; the area sounds are silent until sound.rs2 sends loops 1.
- `tob_xarpus_presentation`: LANDED (acid pools dissolve at collapse +4..+7 with
  1551-1554; [M124] in the plan). Still open: 3231/3944/3949 need a recording's audio;
  the tob_xarpus attempt's splat-lifetime row must be bounded before the collapse.
- `tob_verzik_presentation`: LANDED (zap shock, death bat, throne transform and trapdoor
  at +5, Athanatos spawn and globules, yellows, P3-death sound and eight doubles; the
  closer's final-tree run 27/27). Still open: the zap and the Athanatos heal land before
  their projectiles (mechanics); 1596 as a projectile; the Hard debris graphic, 1599,
  3028, 3988, 4008 unsourced.
- `tob_boss_hit_and_defend_sounds`: LANDED (seven hit sounds; row seam.tob_boss_hit_sound).
  Still open: 3977 unsourced; the 12-tile carry is from the south-west tile; generator
  drift (516 ledgers, 420 blocks).
- `tob_treasure_vault`: LANDED (trapdoor, vault, five chests, tob_chests, crystal, Ver
  Sinhaza claim; the roll unchanged). Still open: the common table and unique roll are
  not the wiki's; the stairs, Discard-all confirmation and side-chest ownership; the war
  table is inside the spectator enclosure; varbit 11958's carrier is server-only.
- `tob_death_spectate_and_lobby_services`: LANDED (cage, rejoin, Entry restart, Normal/Hard
  wipe, tob_midway_stores, gravestone chest, deposit box, orators; row
  seam.tob_death_cage_then_entry_restart). Still open: bosses target the cage (every room's
  hunt); varp1746 undeclared; ~tob_orator_place uncalled; the death line in state.lua;
  the Mysterious Stranger and escape crystal.
- `tob_party_board_and_scoreboard_interfaces`: LANDED (party list and details fed, the door
  needs a party, infoboard and scoreboard; row seam.tob_party_board_forms_a_party). Still
  open: varps 1740/3052/1746 undeclared; sub-0 button latch (engine); the first-push
  GOSUB stall (client); t.chat.count under a modal; apply/accept/kick unproved with one
  client.
- `tob_music_and_title_card`: LANDED (unlocks where played, the vault track, jingle 250
  on every boss death, card sound 3952). Still open: the engine's region unlock writes the
  wrong varp (556 never unlocks; the vault prints two unlock lines).

## matthew-mbp-m4-raid-b1-seam8 (2026-10-03): what the presentation spec pass found

Triage `SEAM_TRIAGE_2026-10-03f.md`; parent 0397a0b29, OSRS-Content 26ba1bd604. No verb
changed; conformance 294/294 (162 verbs + 132 seam rows, nineteen new). Suite 115 green +
deserttreasure, regicide and troll RED (the baseline) + forgettabletale RED (its setup
names a dbrow with no ::complete arm and was green only through the old music bug's write
to varp 11; the corrected copy ran 292/292; the one-line fix is the quest loop's). C
selftest 11 failures, the baseline set; ::tobrun OK 56; cheats 23/23.

- `tob_maiden_death_and_cage`: LANDED (8093 once; no hunt targets the cage; row
  seam.tob_maiden_cage_and_one_death_anim). Still open: the dying_a hold is 2 ticks, not 4
  (engine row: [ai_queue3] at the CORPSE stage); the pool-loc patch is ready but needs the
  C selftest and tob_maiden.lua re-keyed to spotanim 1579; tob_maiden.lua must name the
  _story records.
- `tob_bloat_sounds_and_cage`: LANDED (3971 the defend sound via tob.npc, applied by the
  closer; 3308 per volley, grade E M163; cage skip; rows seam.bloat_hand_sound_and_cage,
  seam.bloat_defend_sound). Still open: the natural cage hit does not reproduce solo.
- `tob_nylocas_deaths_and_cage`: LANDED (support npc unanimated; 8074 sourced; 4020 is the
  hit sound; cage skip incl. Vasilias; row seam.nylocas_cage_skipped_support_unanimated).
  Still open: the small arrive delay (engine row); Hard Vasilias' spreads need two players.
- `tob_sotetseg_tornado_and_cage`: LANDED (the mode's tornado record; cage skip; realm let
  go on a runner's death; rows seam.tob_sotetseg_cage_not_targeted,
  seam.tob_sotetseg_tornado_mode_record). Still open: tob.npc blocks for the _story/_hard
  tornado; ricochet/sharing skips and the Hard arena tornado unexercised.
- `tob_xarpus_death_sound_and_cage`: LANDED (3549 in band only; spit/bounce/damage skip the
  cage). Still open: a caged raider is not released when the room is won; the spit landing
  waits on a dying target; M71 stays E at measured 3.
- `tob_verzik_forms_and_presentation`: LANDED (Entry/Hard records, configs/tob_verzik.npc,
  8373, crabs and ball on a regular attack, 4000 in band, 1595 to the blast, 8128 once, zap
  with its ball, cage skip; raid.lua knows the forms; row
  seam.verzik_entry_forms_cage_and_death). Still open: the Athanatos heal timing (needs an
  npc_heal row); ::tobvz/::tobmelee Normal-only; tob_verzik.lua must be re-authored to the
  Entry records.
- `tob_rewards_and_completion`: LANDED (team roll, weights, 29-row table, mode shares,
  tertiaries, completions, CA thresholds and six tasks, ::tobrewards; the moved constants
  deleted from tob.constant; row seam.tob_reward_table). Still open: M22 scaling, the
  deadweight, Lil' Zik as a follower, HM Grandmaster, 27 hook-needing CA tasks, perfect
  bits on a raid resumed at Verzik.
- `tob_hud_chat_lines_and_room_flow`: LANDED (6448 permille, 6440 = 2, the recorders'
  lines, fight logout a death, jingle to every raider, Sotetseg's floor at entry; rows
  seam.tob_hud_status_and_wave_line, seam.tob_logout_in_fight_is_death). Still open: the
  party-wide paths need two players; a logout in the maze; ~tob_restore heals only the
  clearer; the watchdog double-arm; the lobby HUD box.
- `tob_lobby_stranger_and_supply_chest`: LANDED (varps 1740/1746/3052 declared, the
  Stranger's shop, the escape crystal, shroud tiers via ::tobstrangershroud, Hard chest
  -4 with M164; rows seam.tob_partylist_button_reads_mycontroller,
  seam.tob_stranger_sells_escape_crystal_and_it_leaves,
  seam.tob_hard_chest_pays_fewer_and_label_reads_it). Still open: the Poll 83 login call
  in tob_raid.rs2, A Night at the Theatre's Stranger talk, ~tob_orator_place uncalled.
- `sound_area_loops`: LANDED (loops 1 in sound.rs2; row seam.area_sound_plays_once). Still
  open: about 104 direct `sound_synth(x, 0, y)` calls (prayer activation among them); the
  2004 lanes' loops-0 sounds are silent in our client.
- `ticklog_player_kinds_and_engine_rows`: LANDED (player_anim, player_spotanim, loc_anim,
  npc_say; the music variable map with its C selftest stanza; the sub-0 latch; rows
  seam.ticklog_player_presentation_rows, seam.ticklog_loc_anim_row,
  seam.ticklog_npc_say_row, seam.music_region_unlocks_its_musicmulti,
  seam.pausebutton_sub_zero_latches). Still open: npc_sound_nearby's carry unsourced (not
  changed); tob_board.rs2's stale sub-0 comments; cows and Al Kharid warriors never speak.
- Closer: the OSRS-Content selftest/quests/quest_cook and quest_druid evidence that two
  fixer runs republished is still dirty in the working tree (the restore was refused by
  the permission check); it is not committed.

## matthew-mbp-m4-raid-b1-seam9 (2026-10-03; parent fd3cc7dc9, OSRS-Content 24198b54ed): what the client draws, the death stages, the cage on a win

- `client_played_anim_reads`: LANDED (npc rows gain pose_anim/pose_frame/pose_kind and
  ready/walk/turn/run_anim from the client's secondary track and idle set; projectile and
  spotanim rows gain seq/seq_frame; loc rows gain seq/seq_frame and
  ambient_sound/range/inner/random from world->area_sounds; `size` was already there;
  rows seam.npc_pose_reads_the_drawn_track, seam.element_seq_projectile_and_static_loc,
  seam.loc_and_graphic_seq_in_bloat_room, seam.npc_pose_follows_a_retype). Still open: no
  room test uses the new fields yet (the room authors' next pass); tob_bloat.lua's
  av.idle.seq filters on "tile unchanged" and reads 8081, it must filter on pose_kind;
  t.npc.state_text does not print the pose; a Canifis citizen never turns werewolf
  (CONTENT_BUGS seam9).
- `tob_death_stage_arrive_delay_and_heal_row`: LANDED (Maiden's living records
  death_delay=0 and npc_death_step runs [ai_queue3] at K+1: dying_a K+1..K+5, fade K+5,
  free K+9; seam8's pool-loc patch with its C selftest stanza, a thrown pool is graphic
  1579 alone; no arrive delay for a record stating death_delay under 2, small Nylocas
  anim +1 / free +2 walking or standing; tick log kind npc_heal with source; rows
  seam.ticklog_npc_heal_row, seam.maiden_death_forms_on_recorded_ticks,
  seam.small_nylocas_dies_without_arrive_wait). Still open: tob_maiden.lua's pool rows key
  on loc 32984 (re-key to map_spotanim 1579); npc_spawn never sets death_seq_tick = -1
  (engine, CONTENT_BUGS seam9); the Athanatos heal timing itself.
- `tob_cage_release_on_room_win`: LANDED (every room releases its caged raider on the win
  through the cage's own per-tick queue, wiki Strategies:3; Xarpus's spit and orb
  landings are his npc queues 4/5 with a carried due tick, landing at T+f whatever the
  target is doing; rows seam.tob_cage_released_on_room_win,
  seam.tob_xarpus_landing_ignores_a_dying_target). Still open: the tob_xarpus room copy
  dies to acid-pool sweeps by tick ~205 (needs a pool-avoiding stance, author's); orb
  timing moved +1 -> +0 (spec rows written on the old timing move); landings in flight
  are dropped when Xarpus dies (no source either way).

## matthew-mbp-m4-raid-b1-seam10 (2026-10-03): the rows the sixth ToB room launch lost (triage SEAM_TRIAGE_2026-10-03h.md)

No fixer of this pass left a report, a progress note, a conformance snippet or an edit:
the state dir held only triage.json when the closer ran, and none of the four seams'
content files differ from HEAD. Nothing landed; the triage document is committed so the
next launch starts from it. Gates on the unchanged tree: scripts compile (42416),
spec_check clean, check-quest-verbs (162 verbs, 141 seam rows), check-drive-abi,
check-pt-switch, test-quest-cheats, test-plugin-lua, lint 127 clean, conformance 303/303
PASS, quest suite 115 green plus the four baseline REDs (deserttreasure, forgettabletale,
regicide, troll), nothing moved.

- `tob_nylocas_vasilias_entry_rows`: NOT LANDED (no fixer report). Still open, all of it:
  vasilias_switch_entry (measured 9/10 vs 15), vasilias_attacks_entry (2 vs 3-4),
  pillar_collapse_entry_min (3/15/27 hp vs 30+) and explosion_radius (1 vs 2) unsettled
  from their source lines; vasilias_reflect, entry_recoil_cap and av.vasilias_death.seq
  unconfirmed; no recipe; the text-row detail format question unanswered.
- `tob_verzik_purple_globule_and_yellow_blast`: NOT LANDED (no fixer report). Still open:
  av.p2_purple.poison_globule (1588 never seen after an Athanatos landing),
  av.p3_yellows.gfx_blast (1597 never seen; a yellow pool placed under her footprint at
  6423,99), the weapon-swap hit attribution for tech.p1_cap_melee_ranged, and the exact
  reads for throne_seq, jingle, map_locs, death_cage and barrier.
- `tob_xarpus_entry_solo_survival`: NOT LANDED (no fixer report). Still open: the room
  copy dies in phase 2 near tick 205 (spit and pool damage per tick not yet measured
  against the Entry rows), spit_landing 3 vs spec 2 after seam9's queue move, no
  surviving recipe proved under two run names, and why the author's pass did not
  reproduce for the reviewer.
- `tob_bloat_stomp_defence`: NOT LANDED (no fixer report). Still open: the Dragon
  warhammer drain is intermittent (80 of 80 in most runs, 80 to 56 in one), cause not
  found, no deterministic recipe for bloat.stomp_defence.
