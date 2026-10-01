The Slug Menace -- author notes (source: wiki; no LostCity quest)
Brief: the wiki "Walkthrough" is the spec; Quest Helper's swamp-paste steps are out of date.
Tiffy: upgrades the Commorb (v1 is exchanged for v2, one item).
Witchaven: O'Niall pier 2739,3311; Maledict church 2722,3283; Hobb 2711,3292; Holgart 2721,3304.
 The three can be talked to in any order; the third bumps slug2_main to 5, then O'Niall again (6).
Ruin ladder 2696,3283 is open to all (Family Crest uses it). It lands at 2696,9683.
False wall slug2_hidden_entrance: click once to push (doorbit), click again to go through
 (slugmenace_witchaven.rs2:oploc1). It lands at the sea slug dungeon, 2351,5070ish.
Dead sea slug: laid on the floor 2349,5093 each entry (witchaven.rs2); giant lobsters rarely drop one.
 Pick it up before the door (Take, op3). Giant lobsters (level 45) wander the path.
Imposing door 2351,5093: an unscanned click costs 5 damage. Use the Commorb v2 on it -> transcript.
Jorral 2436,3346 (Making History's npc): translate the transcript; Maledict refuses until then.
Pages: desk (Hobb's house, 2709,3294) = Page 1, Lovecraft (2732,3290) = Page 2; both only after
 Maledict's second talk. O'Niall then tears Page 3 into three fragments (3 free slots).
 Pages 1 and 2 are KEPT (they carry the rune shaping ops).
Jeb 2719,3304 ferries to the platform only with a dead sea slug in the pack.
Bailey (platform) takes the slug for sea slug glue. Glue on a fragment opens the puzzle
 (slugmenace_puzzle.rs2). Fragments flip/rotate/move 2 px per click for the SELECTED ones;
 the select rows are dynamic children (child 2 = Select, child 0 = Show). Solved = all three flip 1 rot 1
 and within 2 px of fragment 1. The step size is invented (wiki gives none).
Shaping: Page 1 op2 earth / op3 air, Page 2 op2 fire / op3 water, Page 3 op2 mind; chisel + essence;
 success 43% at Runecraft 1 to 99% at 99, a failure destroys the essence (slugmenace_pages.rs2).
Altars charge the blanks through runecraft.rs2's [oplocu,_rc_altar]; a wrong altar says so.
Runes on the door in any order; the last one sets slug2_main 12; click the door to spawn the Prince.
Slug Prince (hand-spawned 2351,5093, level 62, 70 hp, configs/theslugmenace.npc): about 70 ticks at
 Attack/Strength 60 with a rune scimitar; bring food. NOT implemented: melee-only damage and the prayer drain.
Not implemented: Hobb's Savant scan (slug2_scan_mayor bits), Mother Mallum reveal after the last rune,
 the Hobb-leaves-the-church scene (cutscene spec pending, docs/quests/cutscenes/).
