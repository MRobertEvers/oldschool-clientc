# fix.coldwar_agility_course_enforced -- notebook
## step 1 (read)
- instructor: coldwar_outpost.rs2 [opnpc1,peng_agility_instructor] writes 100->105 with no course check.
- lap tracker: agility_lap.rs2 temp varps varp6669_agility_lap_course / varp6670_agility_lap_step; penguin = course 17,
  steps crusher=1 stone7=2 icicles 3-6 ice=7 gate=8 (gate calls ~agility_lap_complete -> resets lap).
- %varb3305_peng_agility_state is used by the port for icelord kills (reset at capture, stage 125).
- sources: wiki Transcript:Cold_War instructor "You've successfully completed the physical"; Quick guide course
  bullets then "Talk to Agility Instructor"; QH ColdWar.java steps.put(100) zones start->water->stones->tread->ice->done->instructor.
## step 2 (repro, run --name seam_cw_agility, scripts in build/seam_state/vm-b1-seam1/agility/)
- course.lua ledger: C7.walkInstructor from the start stalls at 2651,4040 (fence, instructor east of it);
  C8.gate PASS from the WEST (start) side -> tile 2652,4038 (EAST, instructor's side): the gate (wall rot 0 = west edge
  of 2652,4039; script always telejumps loc z-1) carries a player who ran nothing INTO the finish pen. That + the
  unchecked instructor = the bypass. From the east the same z-1 leaves the player east (no way back to the door).
- driver: steps01/stepstone01 (level 0) are other_floor (the concurrent driver seam); stones unreachable without them.
## plan
- instructor: 100->105 only if lap (varp6669=penguin(17), varp6670>=7: crusher, stone7, 4 icicles, ice in order) OR
  %varb3305_peng_agility_state=1 (set by the gate when a full lap completes at stage 100). Capture resets it (icelords).
- gate: cross the wall to the other side (east->west = the lap's last obstacle, XP+lap; west->east plain crossing).
## step 3 (course probe after first fix)
- course.lua run: Crusher npc menu = only "Examine Crusher" (all.npc has no op) -> [opnpc1,crushblock] never fires ->
  agility lap step 1 never written -> the penguin lap never starts. So the lap-based check would make 105 impossible.
- REPLACED it: ~coldwar_course_leg(n) in coldwar_outpost.rs2 counts %varb3305_peng_agility_state 1 (stone 7), 2 (icicle
  x=2662), 3 (ice) in order at stage 100; instructor needs 3, resets it to 0. penguin_course.rs2 calls it from those three.
- gate: from west -> loc square (east, plain crossing, no XP); from east -> x-1 (west), XP + lap 8.
- repro.lua after fix (first version): R2.gate west->2652,4039; R2.instructor refusal page 019-npc.png; R2.stage100_unchanged PASS;
  R3.gate east->2651,4039.
- scripts compile (exit 0). coldwar_debug.rs2 is modified by the clockwork seam worker -- not mine.
## step 4
- stone 7 was unreachable from stone 6 (coast_6 lvl0 sh10 at 2635,4064 + inviswall): changed to [aploc1] with
  distance<=2 p_aprange(2) (seam-facts (k) pattern). Run: C4.stone7 PASS tile 2635,4065,1, agility_state=1;
  C5.icicle4 PASS -> agility_state=2. Remaining: icicle1 pick flake (retry), ice: click glitters01 (player at 2664,4077).
## step 5 (positive proof PASSED, ledger agility/course.after.ledger.tsv)
- ice slide: ice now telejumps over the hill top then (p_delay 1) to the finish line 1_41_63_33_7 (2657,4039) -- the
  slide lane is closed by inviswall 38848 at south edge of 2656-2657,4040 (parity's "4 tiles short").
- course.lua: C4.stone7 PASS 2635,4065 state=1; C5.icicle4 state stays 1->2 at x2662; C6.finish tile 2657,4039,1 state=3;
  C7.instructor + chat 045-npc "successfully completed the physical"; C7.stage105 PASS; C8.gate east->2651,4039; walk to 2643,4034.
- next: re-run repro.lua (negative) on final content, regressions.
## step 6 (DONE)
- repro.after (name seam_cw_agility_neg): west->gate 2652,4039, instructor refuses (019-npc.png), R2.stage100_unchanged PASS.
- regressions: cooks_assistant 48/0 green, druid 30/0 green (gate.py), check-drive-abi PASS, check-pt-switch PASS,
  check_agility_course_contract Penguin 540 ok. No coldwar C selftest; no test/quests/coldwar.lua exists (unauthored).
- report written to fix.coldwar_agility_course_enforced.json
- open issue updated with driver_other_floor_loc's finding (water leg = engine ocean overlay 537 + content route)
