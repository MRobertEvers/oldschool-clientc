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

First launch (9817cf668): no fixer reported, nothing landed. SECOND LAUNCH: four fixers
reported and all four seams landed, in OSRS-Content 0bc66408fc and parent 55ab5edc8. No verb
changed; conformance 308/308 (162 verbs + 146 seam rows, five new); scripts compile (42416);
spec_check clean; quest suite 115 green plus the four baseline REDs (deserttreasure,
forgettabletale, regicide, troll), nothing moved; C selftest 11 FAIL (the baseline set),
tobrun OK 56; cheats, verbs, drive-abi, pt-switch, plugin-lua pass; lint 127 clean.
conformance.py MAX_FRAMES 120000 -> 160000; raid_coverage.py parses tol bracket<=N.

- `tob_nylocas_vasilias_entry_rows`: LANDED. Content fixed from the source lines (no spec
  row loosened): Entry windows 14 then 15 (blert m10, wiki Vasilias:92), 3-4 attacks per
  window, collapse 30-50 in Entry (Entry Mode :179), detonation reach from the footprint
  (Entry Mode :176), Vasilias reflected and healed but never nulled (wiki Vasilias:82,
  Strategies:754, Jagex DSF:52). Row seam.vasilias_entry_window_and_reflect PASS. Still
  open: entry_recoil_cap stays E [M97] (no figure); pillar_collapse_max could be promoted
  E -> D on Strategies:726 (not applied: M95 open-table rework); the room copy dies after
  three supports fall (the author's tactics).
- `tob_verzik_purple_globule_and_yellow_blast`: LANDED. A poisoned hit bursts the
  Athanatos (wiki Athanatos:54, Strategies:923); yellow pools only on walkable tiles (Entry
  Mode :251, Strategies:968; the pool was on the wall row, never under her). Rows
  seam.verzik_athanatos_poison_bursts and seam.verzik_yellow_pool_walkable PASS; spec rows
  poison_globule and p3_death.jingle reworded. Still open: the author's file brings no
  poisoned ammo; death_cage needs a verb that counts every copy of a loc; room runs are not
  reproducible across run names.
- `tob_xarpus_entry_solo_survival`: LANDED. Standing acid hurts only the puddle's own tile
  (Entry Mode :207, wiki Xarpus:257, Strategies:844); the wiki strategy killed him under four
  names; recipe in DRIVER_NOTES. Row seam.tob_xarpus_acid_is_the_puddle_tile PASS; spec row
  xarpus.p2.pool_reach added. Still open: tob_xarpus.lua's planner must mark only the
  puddle's tile (author's); spit_landing stays E at 3 (M71); a mid-run crossing costs
  nothing (unsourced).
- `tob_bloat_stomp_defence`: LANDED as a recipe (no content bug). The drain is
  deterministic (30 % of current Defence on a special that deals damage; the stomp restores
  it on T+29); the misses were the probe's. Row seam.special_attack_spent PASS. Still open:
  tob_bloat.lua still carries the faulty probe; spec.bloat.fly_first reads 3 against 1 and
  tech_flinch_tiles comes and goes (the author's).

## matthew-mbp-m4-raid-b1-seam11 (2026-10-03; parent 864f2cc46, OSRS-Content e7f9dd5269): the rig pass's findings and what the seventh ToB room launch left (triage SEAM_TRIAGE_2026-10-03i.md)

- `tob_rig_ledger_faults`: LANDED. Authored attack_anim null for the seven rig families
  still missing it, all modes, plus death null on the death bat; generated-then-authored
  layering proved (the Athanatos null held before the edit); Matomenos plays 8098 on its
  spawn tick; generator a4 role gate and own-stance gate (generator NOT regenerated);
  `::tobnpcanim`. Rows seam.authored_null_attack_anim_holds and
  seam.matomenos_spawn_seq_on_spawn_tick PASS; spec rows maiden.av.crab_spawn.seq and
  xarpus.av.death_a.seq added. Still open: nc_param reads no overlay (engine row); a
  `--write` would drop generated death anims (restate first); 54 slots change on the next
  write.
- KEPT ROOMS: tob_maiden 107/107 PASS and tob_xarpus 119/119 PASS on the final tree, gate
  green, but coverage 62 of 63 each, not FULL: the new rows maiden.av.crab_spawn.seq
  (tick log: 6 of 6 crabs npc_anim 8098 on the npc_spawn tick) and xarpus.av.death_a.seq
  (tick log: npc_death K=281, 8062 on 10768 at 282, 8063 on 10769 at 284) are not measured
  by the committed tests. The next room launch re-authors one row in each; no other
  measured number moved.
- `tob_sotetseg_maze_proc_and_creeper`: LANDED. No 8142 on the boss at the proc
  (av.maze.boss_seq 0, grade C); a ball thrown on the proc tick is not counted
  (magic_per_ball_proc_tick 11, D); tornado 9004 on spawn, 9005 then removal a tick later;
  rag recipe for two safe samples in DRIVER_NOTES. Proved by scratch runs sote11_a/b, no
  conformance row (content only). Still open: the size-3 tornado stalls when the path
  starts at column 12 or 13 (needs centring and moverestrict=passthru in tob.npc);
  tob_sotetseg.lua (author WIP) must re-author maze.boss_seq, maze_boss_idle_at_proc,
  magic_per_ball and the tornado rows.
- `tob_verzik_webs_and_unmeasured_rows`: LANDED. Webs per cast is a party row (one web per
  cast solo, Strategies:953); the enrage tornado now touches a standing raider (range 1);
  P2 Matomenos 8098 and tornado 9004/9005 play; a no-god scratch killed her twice
  (t741, t748) and measured p1_cap, p2_crab_lifetime, reds_absorb_window, p3_web_lifetime,
  tornado pct/heal_mult/respawn; zap bounces and self-damage scoped party, tornado_min
  stat. Row seam.verzik_tornado_touches_a_standing_raider PASS. Still open: the tornado's
  range-1 contact needs an M number (no source says shared tile or adjacent); prayed blood
  spell heals on the roll against Mod Ash (source-ranking call); tob_verzik.lua (author
  WIP) must re-author with the recipes.
- `ticklog_raw_damage_and_loc_count`: LANDED. hit_player (field g, TSV column after the
  label) and hit_npc (field e) carry `raw`; `t.world.loc_copies` counts every placed copy.
  Rows world.loc_copies, seam.ticklog_hit_raw (needed the worn bow off; fixed by the
  closer) and seam.loc_copies_verzik_death_cage PASS. Still open: raw is the hit as the
  script dealt it, never a script's pre-prayer roll; verify_tob_timings.py does not read
  hit_player raw yet.
- Unrelated, recorded: the committed tob_bloat.lua fails tech.protect_from_missiles on the
  current tree and on HEAD content alike (125 fly hits, largest 7 > 6); not this pass.

## matthew-mbp-m4-raid-b1-seam12 (2026-10-03; parent 5099f50e3, OSRS-Content 6f82ec574e): the Sotetseg tornado and proc-tick ball, Verzik P3 against its sources (triage SEAM_TRIAGE_2026-10-03j.md)

- `tob_sotetseg_tornado_stall_and_proc_ball`: LANDED (content). The maze tornado is centred
  on the path (one column inward on rows touching column 0 or 13) and walks through players
  (moverestrict=passthru); it follows from start columns 13, 12, 1 and 7 (sote12_after_a/b)
  where 13 and 12 stalled (sote12_before). magic_per_ball_proc_tick is produced by a driven
  player (5-tick bow aligned to his attack - 1, a hold past the slot after a death ball):
  11 and 11 in sote12_procball_c, and again in the closer's close12_sote. No conformance
  row (content only). Still open: one column short of Near-Reality's centre at the walls
  (needs a wall-ignoring npc walk); the arena tornado is unmeasured solo; the scratches used
  `::god`, so tob_sotetseg.lua must re-author the row on food and prayer.
- `tob_verzik_p3_against_sources`: LANDED (content). P3 prayer read at the landing; enrage
  latched; green ball on 8125; a prayed blood-spell raider takes 0 and counts half the roll
  (Mod Ash row settled by the Blert pull). Spec rows p2_heal_spell_fraction (A -> B),
  p3_enrage_latch (new, C), p3_prayer_read (new, D), av.p3_ball.seq (8125). vz12_kill.lua
  killed her as vz12_kill_a and vz12_kill_b. No conformance row (content only). Still open:
  the kill recipe died under a third name (close12_vz, 8 tornado touches, every seam12 row
  PASS), so the flee is not yet robust; the P3 auto splat is one tick later (T+4); the
  blood-spell heal timing (cast tick vs Blert's T+2); tob.constant's stale Mod Ash comment;
  tob_verzik.lua (author WIP) must re-author with the kit and flee.
- KEPT ROOMS: tob_maiden 108/108, tob_xarpus 120/120, tob_bloat 94/94 PASS on the final
  tree; gate green; coverage FULL (63, 63, 55). No number they measure moved.
- Gates: quest suite 115 green + deserttreasure, forgettabletale, regicide, troll RED (the
  baseline; nothing moved); conformance 314/314 PASS (unchanged, 163 verbs + 151 seam
  rows); server selftest 11 FAIL (baseline), tobrun OK 56; cheats 23/23; check-quest-verbs,
  check-drive-abi, check-pt-switch, test-plugin-lua pass; lint 119 clean; spec_check clean.

## matthew-mbp-m4-raid-b1-seam13 (2026-10-04; parent 0037b16de, OSRS-Content 6ca627de58): the whole raid as a relay, in two halves (triage SEAM_TRIAGE_2026-10-04.md)

- `tob_relay_lobby_to_nylocas`: LANDED as a RECIPE (no content or driver change). Ver Sinhaza
  to Sotetseg's entrance by click with one kit: board, party, Entry, door, the Maiden, Bloat, the
  chest and the Nylocas. Scratch relay1/relay_lobby_to_sotetseg.lua, green once (s13_relay_a9
  92/92) and survived in 2 of 10 runs. The other eight died in the Nylocas, which took 880-1179
  hitpoints against the ~820 the kit still holds. The raid-wide rows it reaches all measured
  (lobby, party, door, hud, music, chest, wave lines, card sound). Its bandage Heal was withdrawn
  for the equivalent one below. No conformance row. Still open: the Nylocas starves a driven
  solo raider; the Entry freeze rule ("frozen nylocas cannot attack the pillars") is missing
  from tob_nylocas.rs2 (suspected, not measured); t.player.inv_op / equip block 3-17 ticks per
  press in a raid room, so the scratch uses the private t.player._inv_press until a fight-speed
  verb exists; t.inv.count cannot count dragon_dagger_p++.
- `tob_relay_sotetseg_to_vault`: LANDED (content plus a RECIPE). The bandages' Heal is bound
  (wiki item page; prayer not restored). Insulated boots take 40 percent off Verzik's P2
  lightning (wiki item page). Sotetseg's entrance to Ver Sinhaza by click (both mazes, chest,
  Xarpus, the Dawnbringer, Verzik to her death, vault, own chest, Take-all, out) runs green as
  s13b_full_c 55/55 and s13b_full_e 58/58. Conformance seam.tob_entry_bandages_heal PASS (SEAM
  151 -> 152). Spec row raidwide.vault.music_on_entry reworded to the teleport anchor (0 exact,
  unchanged). Still open: no supply margin (Verzik leaves 0 food in every completed run); the
  bandage prayer restore and the Magic boost (the two wiki pages disagree); P2 lightning has no
  Entry figure; tob.constant's stale "25 with insulated boots" comment.
- CLOSER, kit reconciliation: the two halves' kits differ in every worn slot and in supplies
  (assumed 4 bandages, 6 sharks, 8+8 doses; measured 0, 0, 11 brew and 5 restore doses). The
  second half with the measured kit (close13_kit3, scratch close/relay_second_half_measured_kit.lua)
  killed Sotetseg (460 ticks) and Xarpus, then DIED in Verzik P2 at tick 1253 with every supply
  spent. The halves do not join yet. t.raid.enter's ~tob_debug_kit gives 4+4 br_ potions to a
  raider holding no br_ brew; the first measured-kit run took that gift and got only 8 bandages.
- KEPT ROOMS on the final tree: tob_maiden 108/108, tob_bloat 94/94, tob_xarpus 120/120,
  tob_sotetseg 155/155, tob_nylocas 162/162 PASS, all gate green with coverage FULL (63, 55, 63,
  83, 73); tob_verzik 239 PASS + BLOCKED, 140 of 142 (as 7bd321420). No number moved.
- Gates: quest suite 115 green + deserttreasure, forgettabletale, regicide, troll RED (the
  baseline; nothing moved); conformance 315/315 PASS (163 verbs + 152 seam rows); cheats
  pass; check-quest-verbs, check-drive-abi, check-pt-switch, test-plugin-lua pass; lint 127
  clean; spec_check clean. C unchanged, so the server selftest was not re-run. The shot PNGs
  were not read: the Read tool's hook timed out on every image this pass.

## matthew-mbp-m4-raid-b1-seam14 (2026-10-04; parent e9fbfd6b5, OSRS-Content 2333c33455): why one Entry solo raider could not carry the Theatre (triage SEAM_TRIAGE_2026-10-04b.md)

- `fight_speed_inventory_presses`: LANDED (driver Lua, pointer.lua; no C). t.player.eat,
  t.player.drink and opts.quick on inv_op and equip: one real click on the backpack cell, read
  back on the next tick (equip, eat, drink, quick inv_op 1 tick each; slow inv_op 3-4).
  Conformance player.eat, player.drink, seam.held_press_fight_speed PASS (VERB 163 -> 165, SEAM
  +1); seam.eat_does_not_hold_queued_hit rewritten to aim the eat into the delay window, PASS on
  the tree (+1) and FAIL on a scratchpad copy with the pre-port food.rs2 (+3). Still open:
  `::give dragon_dagger_p++` gives dragon_dagger_p (torirs_server_world.c cheat_id_from_name
  underscores before the exact lookup; t.inv.count is right), row seam.give_takes_the_exact_symbol
  waits unmerged in the state dir; npc.await_dead_engaged's opts.eat still eats slowly; the kept
  rooms still use the slow presses (a tob_verzik copy with fast presses kills Verzik at 714).
- `tob_nylocas_entry_damage_taken`: LANDED (content). Wave nylocas and Vasilias swings are
  accuracy-rolled (LostCity npc_combat_melee.rs2:27-28; cache attack stats), and an Entry frozen
  nylocas does not bite (Entry Mode :166). Room scratch 545-969 -> 361-441 damage, 1 of 3 -> 3 of
  3 survive. Conformance seam.nylocas_swing_rolls_accuracy (9 of 14 at 0) and
  seam.nylocas_frozen_no_bite_entry (0 bites after the burst) PASS (SEAM +2). Spec rows
  nylocas.swing_miss_entry (C), frozen_bites_entry (D), vasilias_prayed_miss_entry (C) applied.
  Still open: the Normal/Hard freeze ("usually"); Vasilias' Entry prayed max (Normal 17).
- `tob_verzik_entry_supply_margin`: LANDED as a finding plus a behaviour-neutral constant
  (^tob_verzik_p2_zap_boots_pct = 60, both quotes). Every Verzik damage source is at or under its
  figure, so nothing was lowered. Bandage Magic +4 settled by the 9 June 2021 hotfix line. No
  conformance row. Still open: the Entry chest drops leftovers it should keep (tob_chest.rs2, not
  in the seam's files; patch proposed, not applied); the bandages restore no prayer (no figure);
  no Entry figure for the P2 lightning, blood spell, crabs, tornado heal; Verzik's pillars use the
  Normal records.
- tob_nylocas must be re-authored: spec.nylocas.vasilias_reflect PASS -> FAIL (reflects 20/11/6;
  the 20 is above the test's press-time max of 17 computed from the strength read at the press;
  tob_damage.rs2's reflect is unchanged, the accuracy roll moved the run; the same in three runs).
  Coverage FULL 73 -> 71 of 76: the three new Entry spec rows are not measured by the kept test,
  and explosion_radius is unmeasured in this run. A copy also read
  spec.nylocas.pillar_collapse_entry_min 31,36,41 -> 0,31,36,41 (a missed in-flight swing from a
  freed nylocas counted as a collapse; filter by damage >= 30). tech.prayer's 0 count now
  includes misses as well as blocks.
- KEPT ROOMS on the final tree: tob_maiden 108/108, tob_bloat 94/94, tob_xarpus 120/120,
  tob_sotetseg 155/155 PASS, gate green, coverage FULL (63, 55, 63, 83); tob_nylocas 151/152
  (above); tob_verzik 239 PASS + BLOCKED, 140 of 142 (unchanged, gate accepts --allow-blocked).
- RELAY VERDICT: first half (seam13 relay1) 3 of 3 green on the final tree (close14_r1a 99/99,
  r1b 97/97, r1c 98/98; seam13 had 2 of 10). The Nylocas took 392, 279, 330 and the kit at
  Sotetseg's entrance was 24-34 brew doses and 11-14 restore doses (seam13 measured 11 and 5).
  Second half with seam13's measured kit: 0 of 3 (Xarpus tick 908; Verzik P2 1290 and 1273), all
  supplies spent. With the kit the first half now leaves: 0 of 3, dead in Verzik P2 at 1680 with
  24 brew and 11 restore doses spent. With that kit plus Protect from Magic held from the first
  reds: 0 of 3, dead in Xarpus at 850 with only 4 brew doses drunk (its loop heals from bandages
  alone). The whole raid cannot be authored yet; what remains is the second half's recipe
  (brews at Xarpus, magic and prayer at Verzik P2), not the content. Those second-half runs were
  identical under all three names.
- Gates: quest suite 115 green + deserttreasure, forgettabletale, regicide, troll RED (the
  baseline; nothing moved); conformance 320/320 PASS (165 verbs + 155 seam rows); cheats,
  check-quest-verbs, check-drive-abi, check-pt-switch, test-plugin-lua pass; lint 127 clean;
  spec_check clean. C unchanged, so the server selftest was not re-run. PNGs were not read: the
  Read tool's hook timed out.

## matthew-mbp-m4-raid-b1-seam15 (2026-10-04; parent 53a8aa8e0, OSRS-Content 946c357a71): the super restore, Verzik's tornado at her death, the joined relay, ::give's exact symbol (triage SEAM_TRIAGE_2026-10-04c.md)

- `super_restore_heals_hitpoints`: LANDED (content, prayer_potion.rs2). The super restore, its
  br_ copy and the Castlewars brew restore every stat except Hitpoints (wiki [Super restore]
  oldid 15183989:53, pinned with Restore potion, Sanfew serum, Blighted super restore and
  Castlewars brew). Conformance seam.super_restore_no_hitpoints PASS (hitpoints 54 -> 54,
  attack 66 -> 98). Still open (CONTENT_BUGS): Sanfew serum skips the non-combat stats, the
  Super restore mix restores only the combat five, the Prayer cape / holy wrench bonus.
- `tob_verzik_tornado_removed_on_death`: LANDED (content, tob_verzik.rs2). No touch or heal
  while she lies dying, every tornado fades (9005) on the bat's tick and is freed one tick
  later, no respawn on a fallen Verzik; her in-flight auto still lands (sourced: 15 of 62
  Blert P3 rooms). Conformance seam.verzik_tornado_gone_with_her PASS (death 2454, bat 2457,
  9005 2457, free 2458, no touch, no spawn). Spec rows verzik.p3_tornado_end (D) and
  verzik.p3_inflight_after_death (B) applied with the Blert analysis pinned; spec_check clean.
  Still open: the tob.constant comment for register 2 (documentation).
- `tob_relay_joined`: LANDED (content, tob_chest.rs2: an Entry chest keeps leftovers until ten
  are taken; Entry Mode:7). Conformance seam.tob_entry_chest_keeps_leftovers PASS (3 -> 8 -> 10,
  then empty). The joined relay (trj/joined.lua, the notice board to the vault, no
  t.raid.enter) is the recipe "The whole raid in one run" in DRIVER_NOTES.md. Still open: it is
  green under 1 of 5 run names (below), so tob_entry is not authored.
- `give_takes_the_exact_symbol`: LANDED (engine, torirs_server_world.c cheat_symbol_exact: the
  typed gameval first, then the underscored form). Conformance seam.give_takes_the_exact_symbol
  PASS (p++ x1, p+ x1, Bronze_sword -> bronze_sword x1); RED on a HEAD binary built in a
  throwaway worktree (fixer). No committed test gives a symbol with punctuation.
- KEPT ROOMS on the final tree: tob_bloat 94/94, tob_xarpus, tob_nylocas 165/165 and
  tob_sotetseg 155/155 (FULL 83) green.
- tob_maiden must be re-authored: 109/110, spec.maiden.av.hit_sound FAIL. The restores no
  longer heal (11 drunk), so the walk moved, and a hit on tick 355 came as the raider stepped
  from 13 tiles (6439,101) to 12 (6438,100). The row reads the end-of-tick tile; the sound
  went by the tile he left. Measure from the tile at tick - 1 (DRIVER_NOTES, "A hit on the tick
  you step into range"). Not a content fault, nothing reverted.
- tob_verzik must be re-authored: 188 PASS, 22 FAIL + BLOCKED (was 239 + BLOCKED). With no
  heal from restores the raider is out of food in P3 at tick 636 (hp 1, 183 of 400 dealt), so
  every P3, death and vault row is unmeasured. The super restore fixer saw the same copy die in
  P2 at 530. Its kit needs brew doses or food for the hitpoints the restores used to give.
- RELAY VERDICT on authoring tob_entry: NOT YET. trj/joined.lua is green under 1 of 5 names:
  s15k1 160/160 (re-run by the closer on the final tree with the same ticks: Verzik dead on
  2919, exit 3677,3219, the raider at 8 hp with every potion drunk). The other four died:
  c15alpha in Verzik P3 at 2921 (16 brew / 7 restore doses at Sotetseg's entrance; everything
  drunk); c15bravo at Xarpus at 2410 (16 / 6; 10 bandages, 16 brew, 6 restore doses drunk;
  missed 39); c15charlie never killed Xarpus (12 / 4; no food from about 2700); the fixer's
  s15j7 (P3 2972, food out) and s15j5b (P3 2986, prayer out). The first half's Nylocas decides
  the second half: 12-21 brew doses reach Sotetseg's entrance and the green run needed 18. The
  bounds are sourced (two chests of 10 bandages, Entry Mode:33/:151/:197; the 28-slot kit;
  P3 600 hp, cache_npc_verzik.txt:552; the P3 ball at 75% of the Hitpoints level); nothing was
  lowered. The levers left are the scratch's: P2/P3 damage (melee P2 with the scythe), and
  carrying restores in place of the super combat and stamina once the first half no longer
  needs them.
- Gates: quest suite 115 green + deserttreasure, forgettabletale, regicide, troll RED (the
  baseline, same first failing rows as seam14; no quest moved from the super restore);
  conformance 324/324 PASS (165 verbs + 159 seam rows); test-quest-cheats, check-quest-verbs,
  check-drive-abi, check-pt-switch, test-plugin-lua pass; lint 127 clean; spec_check clean;
  server C selftest 11 failures, the baseline set (junglepotion x4, itgronigen x5, chatmenu,
  same-script loc). PNGs read in this close: s15k1 118-verzik.kill (the raider at 8 hp, her
  bar 2.4%, 'You have finished your potion.'), c15alpha 118-verzik.kill-FAIL (0 hp, a 74 green
  ball on the raider, her bar 27.4%), tob_verzik 102-drive.p3state630 (1 hp, her bar 63%, the
  pack all empty vials). The seam rows in _conformance.lua take no shots; they rest on the
  ledger rows quoted above.

## matthew-mbp-m4-raid-b1-seam16 (2026-10-04; parent 2be1a3db5, OSRS-Content a098da317e): what an Entry solo raider spends, room by room, and the wiki's kit (triage SEAM_TRIAGE_2026-10-04d.md)

- `tob_maiden_entry_prayed_autos`: LANDED as a sourced verdict, no behaviour change (comments
  in tob_maiden.rs2 and tob.constant). The blackstorm always lands and Protect from Magic halves
  it (Strategies:590); a prayed Entry hit above 9 is the leak term c (61 of 61 hits matched at
  their launch-tick c). The 18 and the 2/3 the triage read as autos were blood pools. Prayer was
  not drained by Maiden: the 13 "restores" were presses on the relay's Strength<85 trigger, 6
  doses drunk. Conformance seam.maiden_blackstorm_always_lands_entry PASS "6 blackstorms
  launched, 6 landed, 6 raw 9 (Entry, Protect from Magic, c 0, Defence 99)". Spec rows
  maiden.auto_land_rate_entry and maiden.auto_prayed_entry (D) applied. Still open: the Entry
  table's "preventable with Prayer" (:111) against Strategies' "halved" is D against D; a Blert
  per-hit Entry dataset would settle it.
- `tob_entry_solo_room_costs`: LANDED (content, tob_bloat.rs2 and tob_verzik.rs2). Bloat's stomp
  needs line of sight (Entry Mode :134, :145; two guides agree); a falling-flesh tile rolled
  twice lands once (blert bloat_events.csv: 15 or 14 graphics in 310 of 1 050 drops); Verzik's
  P3 ranged, magic and melee attacks roll accuracy (cache verzik_phase3_story, Strategies:942,
  Entry :243/:245, LostCity npc_combat_melee.rs2:27-28). Conformance seam.bloat_stomp_needs_sight
  PASS "HIDDEN at local 29,27 (6 from his SW): stomp on down+29 none; ... SEEN at local 24,24
  (2 from his SW): stomp on down+29 raw 26"; seam.verzik_p3_attacks_roll_accuracy PASS "17
  unprayed ranged/magic autos, 8 at 0, max 20; 3 melee, 2 at 0 (rune armour, Defence 99)".
  Both were RED on a HEAD pack (fixer: stomp raw 34 on the hidden raider; 0 of 16 autos and 0
  of 17 melee at 0). Spec bloat.hand_tiles 16 exact -> 14-16 range (B, blert distribution),
  bloat.stomp_range text and sources, verzik.p3_auto_miss_entry (C) new. Nylocas and Xarpus:
  no content change; their cost is the recipe's (supports collapsed beside him, detonations
  within 2 tiles, exhumeds covered 1-4 orbs late, the P2 1-tile step inside the splash).
  Still open (CONTENT_BUGS seam16): Xarpus [M70] absorbed share, Verzik P2 stomp and urnbomb
  unrolled, the Entry green ball's figure.
- `tob_relay_wiki_kit`: LANDED as a measurement (scratch only, trj/plan.py plan 17, no tracked
  file). RELAY VERDICT on authoring tob_entry: NOT AUTHORED. 0 of 5 names survive with the
  wiki's kit (at least 4 were needed). Xarpus phase 2 decides 3 of 5: acid splats (dealer 10768,
  projectile 1555) 51-63 hits for 273-337 a run plus 81-110 from pools, the step-back "missed"
  23-34 times as the board fills over a 360+ tick phase 2. The Nylocas decides 2 of 5: w16delta
  brewed Magic below Fire Strike's 13 and died with Vasilias at 17%; w16echo died in the waves
  after a 328-damage Bloat. Content finding (grade D, open): sara_brew.rs2:30 DRAINS Defence
  where the wiki says a brew raises it by 20% + 2 of base; seam17 should settle it first and
  re-run these five names.

  | name | result | died | Maiden dmg/eats/restores | Bloat dmg/eats | Nylo dmg/eats/restores | at Sotetseg's entrance | Sote dmg/used | Xarpus dmg/used |
  |---|---|---|---|---|---|---|---|---|
  | w16alpha | died | Xarpus P2 (bar 23.7%, no screech), 2444 | 261/11/2 | 105/7 | 281/23/7 | brew 9 doses, restore 0, 20 free | 92/brew 4 | 383/bandages 10, brew 5, restore 0; no food from 2413 |
  | w16bravo | died | Xarpus P2 (no screech), 2379 | 188/8/0 | 112/5 | 355/33/12 | brew 9 doses, restore 0, 20 free | 79/brew 3 | 398/bandages 10, brew 6, restore 0; no food from 2365 |
  | w16charlie | died | Xarpus P2 (no screech), 2341 | 216/9/0 | 163/11 | 248/24/6 | brew 11 doses, restore 1 dose, 19 free | 63/brew 2 | 447/bandages 10, brew 9, restore 1 |
  | w16delta | died | Nylocas boss phase (Vasilias 17%), 1779 | 267/12/3 | 0/0 | 568/-/- | not reached (bloat.after: brew 22, restore 6 doses) | - | - |
  | w16echo | died | Nylocas waves (529 ticks in), ~1510 | 200/8/0 | 328/27 | 354/21/0 | not reached (bloat.after: brew 9, restore 0) | - | - |

  No name reached the vault, so supplies at the vault are unmeasured.
- KEPT ROOMS on the final tree (tools/raid_gate/run.py --no-build --no-publish, gate.py):
  tob_nylocas 165/165 FULL 76, tob_xarpus 120/120 FULL 63, tob_sotetseg 155/155 FULL 83 green.
- tob_maiden must be re-authored: measure maiden.auto_land_rate_entry and
  maiden.auto_prayed_entry (the room is 110/110 green; the gate now reads 63 of 65 measured).
  m16/analysis.lua measures both.
- tob_bloat must be re-authored: its spec.bloat.hand_tiles row still carries "spec 16, tol
  exact" where the table now says 14-16 range (gate: tolerance and spec value mismatch). The
  room is 94/94 and its ledger is the kept one except that row's measured 14,15,16.
- tob_verzik must be re-authored: 256 PASS, 14 FAIL (was green). The P3 roll moved the seeded
  fight and the raider died in P3 at tick 799 with Verzik at 10.2% and no food (shot
  134-drive.p3state798: hp 34, prayer 17, "You have finished your potion."), so every P3 death
  and vault row is unmeasured; verzik.p3_auto_miss_entry also needs its row. Nothing reverted:
  the fix is sourced.
- CLOSER, conformance: the three new rows failed in the full harness at first and were fixed in
  _conformance.lua only. The tick log is one per session (ticklog.start is idempotent), so the
  Maiden row now reads only its own ticks and sits after seam.maiden_blackstorm_sound_once (which
  had counted its blood throws); its raider stood in a pool losing 5 prayer a tick, so prayer is
  topped up every 5 ticks. The Verzik row filters its retypes and anims to its own ticks, clears
  the backpack and gives all four rune pieces before equipping them.
- Gates: quest suite 115 green + deserttreasure, forgettabletale, regicide, troll RED (the
  baseline, same first failing rows: troll player.died at tick 465, regicide
  goKillGuardAtSecondForest-walk-toForests; nothing moved); conformance 327/327 PASS (165 verbs
  + 162 seam rows; SEAM_COUNT 159 -> 162); test-quest-cheats, check-quest-verbs,
  check-drive-abi, check-pt-switch, test-plugin-lua pass; lint 127 clean; spec_check --all
  clean. No C changed, so the server C selftest was not re-run (the room-costs fixer ran it on the
  edited pack: 11 failures, the baseline set, tobrun OK 56). PNGs read in this close: tob_verzik
  134-drive.p3state798; the w16alpha shot could not be opened (the editor's Read hook timed out;
  the relay fixer read it: Xarpus at 23.7%, floor covered in pools).

## matthew-mbp-m4-raid-b1-seam18 (2026-10-04; parent 1746816fb, OSRS-Content 6c7a65615a): the Saradomin brew raises Defence, and the relay's recipe under the five names (triage SEAM_TRIAGE_2026-10-04f.md)

- `saradomin_brew_raises_defence`: LANDED (content, sara_brew.rs2). `stat_drain(defence, 2, 10)`
  became `stat_boost(defence, 2, 20)`. Source: wiki [Saradomin brew] revid 15322175, pinned at
  docs/minigames/theater_of_blood/sources/wiki_Saradomin_brew.wikitext with a manifest row: :56
  "raises Hitpoints by 15% + 2 and Defence by 20% + 2 of their base levels ... above their base
  level by up to the amount restored"; :89 "Defence boost is calculated with: floor((Defence
  Level) * 1/5) + 2". The engine's stat_boost step is constant + BASE * percent / 100
  (SS_OP_STAT_BOOST), so (2, 20) is the wiki's formula and its min(current + d, base + d) is the
  "up to the amount restored" cap. The owner confirmed the fix on 2026-10-04. Conformance
  seam.brew_raises_defence PASS "defence 99 -> 120 (want 120 = base 99 + floor(99/5) + 2),
  strength 99 -> 88 (want 88), hitpoints 99 -> 115 (want 115)"; seam.brew_defence_no_stack PASS
  "defence 120 -> 120 of base 99". RED on a HEAD pack (fixer, run sbd18_red): "defence 99 -> 88".
  The triage's 121/87/116 were off by one against the page's own table (:109 "95–99 || +21").
  god_brew.rs2, barbarian_mix.rs2 and castlewars_brew.rs2 carry no copy of the brew lines.
  Still open (CONTENT_BUGS seam18): br_potion.rs2:80 (the ToA supply brew, "constant for
  constant" with the ordinary families) still drains Defence and was outside the file list; and
  whether the four drains are of the current level (:56, :114) or the base (:58, which holds
  only for a base step) -- the page disagrees with itself, the drains were left as they were.
- `tob_relay_recipe`: LANDED as a measurement (scratch only: trj/plans18.py exec'd from plan.py
  into trj/joined.lua; no tracked file, no conformance row). RELAY VERDICT on authoring
  tob_entry: NOT AUTHORED. 0 of 5 names survive the whole Entry raid with the wiki's kit (at
  least 4 were needed).
  BREW-ONLY MEASUREMENT (one run of seam16's plan-17 script, unchanged, under w16alpha on the
  fixed pack): seam16 died in Xarpus P2 at t2444 (Xarpus 383 damage, step-back missed 23, no food
  from 2413); with the brew fix Xarpus dies at t2378 (215 damage, missed 1; 9 bandages, 3 brew, 1
  restore) and the raider dies in Verzik P2 at t2759 (234 damage, no food). The brew was worth one
  room for w16alpha. The same script on the other four (one run each): bravo identical to seam16
  (Xarpus 2379), charlie Xarpus 2341 (same tick), delta Nylocas 1779 -> Sotetseg 2414, echo still
  the Nylocas (~1648).
  RECIPE PLANS (each A/B under w16alpha against the plan before it): 18 bow at the Matomenos
  (Entry :120) killed 0 of 6 before her and is off; 18b the same rule with the scythe, by the
  crab's slot from the tick it is seen: reached her 3, killed before her 3, Maiden 261 -> 144
  damage, 11 eats -> 5 sharks, no brew. 19 shark-only Maiden, restore only at prayer < 20: same
  tick log as 18b. 20 detonation step: MEASURED WORSE and left off (Nylocas 324 -> 368); seam16's
  "117 from detonations" were three support collapses (37/37/43), which hit everyone in the room
  (tob_nylocas.rs2:1680). 21 restore before a Vasilias cast under Fire Strike's 13: never fired
  under w16alpha. 22 Xarpus: the splash is judged on the land-1 tile within 1 of the aim (xspit.py
  over w16bravo's 132 spits: d0 14/14, d1 32/32, d2 0/15), so the step goes out to a clean ring-2
  tile and two steps to a side tile two from it, never through a pool: w16alpha Xarpus 243 -> 97
  damage, splash hits 21/90 -> 12/88, bandages 10 -> 3; 22r/22r2 under w16charlie: missed 47 -> 26,
  still died. 23 the kit is counted by doses at Sotetseg's entrance and the vault.

  | name | result | died | Maiden dmg/eats/crabs reached | Bloat dmg/eats | Nylo dmg/eats/restore presses | Nylo kit used | Sote dmg | Xarpus dmg |
  |---|---|---|---|---|---|---|---|---|
  | w16alpha | died | Verzik P3, t2895 (Verzik at 26.2%) | 144/5/3 | 102/3 | 324/31/8 | sharks 2, bandages 10, brew 10, restore 6 | 94 | 147 |
  | w16bravo | died | Sotetseg, t2351 | 188/7/4 | 155/8 | 377/33/8 | bandages 10, brew 14, restore 6 | 217 | - |
  | w16charlie | died | Xarpus, t2445 | 202/8/3 | 72/2 | 285/29/12 | bandages 10, brew 11, restore 7 | 77 | 464 |
  | w16delta | died | Sotetseg, t2300 | 233/9/3 | 189/9 | 323/30/7 | bandages 10, brew 13, restore 4 | 153 | - |
  | w16echo | died | Sotetseg, t2338 | 201/8/3 | 155/9 | 337/31/7 | bandages 10, brew 15, restore 5 | 185 | - |

  (build/seam_state/matthew-mbp-m4-raid-b1-seam18/table.tob_relay_recipe.tsv; the final1 copy
  of the same five runs differs only in alpha t2926 and charlie t2431.) THE DECIDING ROOM is the
  Nylocas as a supply sink: 285-377 damage and 29-33 eats per name, every Bloat-chest bandage, 10-15
  brew doses and 4-7 restore doses. The second half starts with 0 restore doses in 4 of 5 names
  and dies where prayer or food runs out: Sotetseg x3 (prayer gone, unprayed projectile 1606),
  Xarpus x1, Verzik P3 x1. That is the RECIPE, not content: the wiki fights the Nylocas with
  Ancient Magicks (Entry :157 "highly recommended", :166 burst/barrage the clumps, :92 the rune
  pouch) and the relay casts single-target Fire Strike; no source says our aggro or collapse
  damage is wrong, so no content row is claimed. Still open: the Ancient Magicks Nylocas (needs
  the ancient spellbook in setup and an ancient spell through t.player.cast, untested); whether
  the kit's 8 restore doses can carry Sotetseg (:30 "one super restore per three brews"); Xarpus
  P2 on a long phase (the wiki's Entry answer is Ranged from range moving 2 tiles a spit, :207);
  the bow plan was not checked against a ranged max-hit source. Every A/B is one run under one
  name.
- ROOMS on the final tree (tools/raid_gate/run.py --no-build --no-publish, gate.py --allow-blocked,
  one run each under its own id): tob_maiden 111/111 green (63 of 65 measured, the same two
  unmeasured rows as seam16), tob_bloat 95/95 green (the seam16 hand_tiles exact-vs-range finding
  still open), tob_xarpus 121/121 FULL 63, tob_sotetseg 156/156 FULL 83 green.
- tob_nylocas must be re-authored: the brew fix moved it from 165/165 (seam16; the fixer's HEAD-brew
  A/B: 48 doses, Vasilias killed at 875 ticks) to 91 PASS / 24 FAIL. The seeded waves now run the
  raider to 18 brew doses by tick 462 (Attack and Strength 63, lowest hp 26, 5 restores), two
  supports fall, and the loop ends at 490 ticks with the last support about to fall (shot
  020-fight.loop: "A support collapses!", "The chamber is full - the next wave is held back").
  Vasilias never spawns, so her 22 spec rows, room.complete_line and tech.prayer FAIL. The fixer
  ran six more names: 0 deaths on the HEAD pack, 3 on the fixed pack (ticks 490-498, explosion
  clusters); not established as systematic. Not reverted: the fix is sourced.
- tob_verzik must be re-authored (unchanged by this pass; seam16's finding stands): 256 PASS / 14
  FAIL, the raider dies in P3 at about tick 799 with no food (drive.p3state798 "prayer 17 hp 34
  fish 0 restores 0"); the fixer's HEAD-pack A/B was identical. tob_maiden and tob_bloat keep
  seam16's re-author notes.
- The Saradomin brew raises Defence, quests: none went red. Quest suite on the final tree 115
  green + deserttreasure, forgettabletale, regicide, troll RED, each with the same first failing
  step as seam16's gate (killKamil-engage; talkToVeldaban-dialog; goKillGuardAtSecondForest-walk-
  toForests; troll player.died at tick 465). Nothing moved.
- Gates: conformance 329/329 PASS (165 verbs + 164 seam rows; SEAM_COUNT 162 -> 164; the one FAIL
  in the SUMMARY is the deliberate ledger_verdict.word_probe); test-quest-cheats 23/23,
  check-quest-verbs, check-drive-abi, check-pt-switch, test-plugin-lua pass; lint 127 clean
  (QUEST_HELPER_ROOT set); spec_check clean (no table row changed). No C changed, so the server C
  selftest was not re-run. Scripts pack 42432, rc 0. PNGs read in this close: tob_nylocas
  020-fight.loop.

## matthew-mbp-m4-raid-b1-seam17 (2026-10-04; parent 3736f3d22, OSRS-Content f8fefeb837 unchanged): three driven clients in one raid (triage SEAM_TRIAGE_2026-10-04e.md)

- `three_clients_one_world`: LANDED (engine; torirs_server_embed.c/.h, net_transport_embed.c,
  make test-embed-party-link 9/9). Option A: the leader's embedded world takes members over
  a loopback party link in READY/TICK lock step. Smoke leader row
  seam.three_clients_one_world PASS `ticks 4..166 (163 ticks): 3 player_tile rows on every
  tick`. The three tick logs agree on one npc_spawn row: the Normal Maiden's
  `2948 149 npc_spawn 1570 8360 105283740` is in p1/, p2/ and p3/ticklog.tsv (copies of the
  world's one log), and p2's and p3's own TICK trace reads `boundary 150 -> server tick 149`.
  Two closer runs: tick logs byte-identical. Still open: TORIRSSERVER_EMBED_CLIENT_MAX is 4,
  so a five-raider party needs it raised; POSIX only.
- `party_run_and_verbs`: LANDED (run.py run_party / --party, gate.py party union,
  raid_gate `_` ids, raid.lua t.party.* with 12 conformance rows, now 177 verbs; also
  src/plugin/torirs_plugin_drive.c and plugin_api.meta.lua, which were outside the seam's
  list but kept: without them a member's script never starts, as pty_before shows on the
  engine-only binary). test/raids/_party_smoke.lua 99/99 PASS twice: Entry party formed,
  applied and accepted (party.members_3), `Is your party ready? Members: 3. Mode: Entry.`, the
  members follow in, everyone sees the other two, then Normal: spec.maiden.hp_3 2625,
  spec.raidwide.scale.party_3_or_fewer 750, spec.raidwide.hud.boss_hp_full 1000,
  raidwide.hud.orb_full p0=p1=p2=27 on the leader. Still open: a member's server readers
  (t.tick, t.ticklog, t.var.server, t.raid.*) answer unsupported; t.session.relog on a
  member types `p<n>`; a member's view of later raiders' orbs stays 0 (CONTENT_BUGS seam17).
  tob_entry_trio (Normal/Hard rooms with three raiders) is not authored yet.
- Gates on the final tree: cooks_assistant and druid ledgers byte-identical to a base build
  at da183511b in a throwaway worktree; quest suite 115 green + deserttreasure,
  forgettabletale, regicide and troll RED, the baseline. Three of the four are
  byte-identical to the base, and forgettabletale differs only in a Lua table address.
  The six ToB rooms are green and FULL (maiden 110, bloat 94, nylocas 165, sotetseg 155,
  xarpus 120, verzik 264). Conformance 336/336 PASS (177 verbs + 159 seams).
  test-quest-cheats, check-quest-verbs, check-drive-abi, check-pt-switch and test-plugin-lua
  pass. Lint is clean on 127 files. torirsserver --selftest has 11 failures, the baseline
  set. test-torirsserver-embed is 79 ok / 48 FAILED, identical to the base. `make
  test-torirsserver` stops at torirsserver-servpack's Membership check: that is content,
  and the content was not touched in this pass.

## matthew-mbp-m4-raid-b1-seam19 (2026-10-04; parent 15c2ace2b, OSRS-Content 007a1c7c9b): a party of three at a room's entrance (triage SEAM_TRIAGE_2026-10-04g.md)

- `tob_party_room_bring_along`: LANDED (content, tob.rs2, tob_hud.rs2, tob_raid.rs2).
  `::tobjoinroom [0|1|2]` is a member's half of `::tobmode`, built on the door's join
  (`~tob_join_raid` + `~tob_party_join`). It is not named `::tobjoin` because
  tob_selftest.rs2:2755 already owns that name for the C selftest. `::tobstate` ends
  ` party=N scale=K`. Every raider's party orbs are current (`~tob_hud_orbs_party`; wiki
  Theatre of Blood/Strategies :578, raidwide.tsv `raidwide.hud.orb_full` A), and a joiner gets
  the room card that mounts the HUD. The seam17 CONTENT_BUGS orb row is FIXED. Still open
  (CONTENT_BUGS seam19): the fight watchdog runs only for the raider who crosses the barrier,
  and room music reaches only the builder or starter. Folding the two joins is also open.
- `tob_party_room_test_shape`: LANDED (driver raid.lua, `_party_smoke.lua` phase C, fixture
  tob_normal_done.ini, raid_coverage.py party scope, raid_author.workflow.js `args.party`).
  Three conformance seam rows were merged (SEAM_COUNT 164 -> 167). The closer made
  `seam.tobstate_reads_party_and_scale` close a chat page first: in the full harness
  `seam.super_restore_no_hitpoints` left a nightshade "player" page open, and the orb write
  (`[queue,tob_room_settle]`, a normal queue) waited behind it (partyslot 0 / p0 0; bisected
  with harness subsets and a probe run). Open: the fixture has no conformance row (a
  one-fixture harness cannot load it; the fixer's scratch_hard_door.lua proves it).
- Gates on the final tree: `_party_smoke` twice, 133/133 PASS and gate green each time. Its
  tick logs are byte-identical (5561 lines). `spec.bloat.hp_3` reads "measured 1500 (spec 1500
  hp, grade A, tol exact) ... bloat.tsv bloat.hp_normal '2000,1750,1500' ... = 2000 x 750 /
  1000 (raidwide.scale.party_3_or_fewer 750 permille)". p2 and p3 read
  `raidwide.hud.orb_full.entrance` and `.members_in_fight` as "p0=27 p1=27 p2=27". The
  cooks_assistant and druid ledgers are byte-identical to build/merge17_check. Conformance
  is 344/344 PASS (177 verbs + 167 seams). The six Entry rooms read maiden 110/0, bloat 94/0,
  xarpus 120/0, sotetseg 155/0, nylocas 91/24 and verzik 256/14, the known state. The quest
  suite is 115 green, with deserttreasure, forgettabletale, regicide and troll RED on the
  same first failing steps as seam18. test-quest-cheats, check-quest-verbs, check-drive-abi,
  check-pt-switch and test-plugin-lua pass. Lint is clean on 127 files. No C changed; the
  fixer's private torirsserver --selftest with this pack had 11 failures, the baseline set.
  Determinism is not guaranteed for three clients: the fixers measured one run in three
  shifted by one tick from tick 114-116 (a member's typed command lands at its own
  frame-timed boundary).

## matthew-mbp-m4-raid-b1-seam20 (2026-10-04; parent 4bfaff1b6, OSRS-Content 123b17c26c): when a protection prayer is read (triage SEAM_TRIAGE_2026-10-04h.md)

- `tob_prayer_read_on_send`: LANDED as an audit, no read moved (rs2 comments, spec rows, two
  conformance rows; script.dat byte-identical, sha 5b367bd7). Sotetseg's ball keeps its landing
  read, sourced by blert (21 Normal and Hard rooms: of 30 balls thrown with Protect from Magic
  off and raised within four ticks, 23 cost nothing; pinned as
  sources/blert_api/spec_pass_sotetseg/seam20_prayer_read.txt, re-run by the closer
  byte-identical); spec row `sotetseg.ball_prayer_read_tick` (B). The P2 urnbomb keeps its
  landing read, sourced by wiki_Verzik_Vitur.wikitext:394 and :397 and Strategies :907;
  `verzik.p2_bomb_prayer_read_tick` (D). Verzik P3 untouched (owner's ruling, one comment line).
  Every other prayer read in minigame_tob is on the send tick (CONTENT_BUGS.md "From seam20").
  Conformance `seam.tob_sotetseg_ball_prayer_read_at_landing` and
  `seam.verzik_p2_urnbomb_prayer_read_at_landing` merged (SEAM_COUNT 167 -> 169).
- tob_sotetseg must be re-authored: measure `sotetseg.ball_prayer_read_tick` (the closer gave
  the row sidecar scope `all`; the name heuristic had skipped it as a Hard row). Coverage gate
  83 of 84; the ledger is unchanged, 156 PASS / 0 FAIL.
- tob_verzik must be re-authored: measure `verzik.p2_bomb_prayer_read_tick`. Coverage gate 145
  of 146; the ledger is unchanged, 258 PASS / 0 FAIL.
- Open: the tick inside the Sotetseg flight is settled only to "the landing, give or take a
  tick" (three class B balls lost nothing with the prayer dropped by T+4), with M100. The
  ricochets' read is by the wiki's "similar projectiles", never measured (needs a party).
  Verzik P3's melee reads no protection prayer; nobody has checked a source on that.
- Gates: conformance 346/346 PASS (177 verbs + 169 seams). Rooms maiden 113/0, bloat 95/0,
  nylocas 178/0, xarpus 121/0, sotetseg 156/0, verzik 258/0. Maiden, bloat, nylocas and xarpus
  are FULL; sotetseg and verzik are one row short, as above. Quest suite: 115 green, with
  deserttreasure, forgettabletale, regicide and troll RED on the same first failing rows as
  seam19. test-quest-cheats, check-quest-verbs, check-drive-abi, check-pt-switch and
  test-plugin-lua pass. Lint is clean on 127 files and spec_check is clean. No C changed, so
  the server selftest was not re-run (baseline 11).
