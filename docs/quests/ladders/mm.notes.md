Monkey Madness I -- what the ladder cannot know (parity3f, 2026-09-30)

SOURCE: LostCity quest_mm (LostCity_Server/content/scripts/quests/quest_mm). The wiki-mapped
varbits are varbit_99..134 (LC mm_flags/mm_misc bits): 103 puzzle solved, 104 knocked out,
111/112 collecting amulet/talisman, 114 waiting for battle, 115 been to battle, 117 kruk,
118 awowogei, 119 child, 120 elder guard, 131 jail count. mm_gnomes varbits (mm_daero ...)
are not sent to the client: read them with t.var.server / await_server.

WHERE THINGS REALLY STAND
- Zooknock (mm_zooknock) 2804,9145 in the dungeon, not 2805,9143. Garkor 2805,2762.
- Shipyard worker at the gate 2944,3040: with mm_main=1 his dialogue accepts the seal
  (mm_caranock.rs2 mm_shipyardworker_dialogue); the foreman then talks (mm_foreman_dialogue).
- Hangar before the puzzle: m37_154 (2392,9889); panel loc bunker_controlpanal 2394,9883.

THE PUZZLE (step clickPuzzle): the cache's interface 306 trail_slidepuzzle, 5x5, random board
(255 blank moves from solved). Pieces are children 0..24 of trail_slidepuzzle:pieces (sub =
slot, op 1 Move; armed by if_setevents in mm_puzzle.rs2). ::mmpuzzle prints the board (rows of
5, 0 = blank, newest message last: sort t.msg.last rows by serial). Solving it plays the hangar
cutscene (shake, fade, gliders unfold in m40_70) and ends at m41_70 with mm_daero=6.

CUTSCENES (HUD stays up so the chatbox shows the speech): Waydar's order to Lumdo -> Meanwhile
card -> foreman/Caranock scene on the shipyard deck (0_40_71) -> Chapter 2. Zooknock's last
talisman page queues the Caranock/Waydar scene (0_41_71), Garkor's "Listen closely" the
Awowogei scene (1_41_71); each ends on its chapter card. Drain stops at "none" between pages
(if_close, p_delay(1)): loop until the stage changes. Sigil Hold -> shake, fade, camera pull
back, squad appears, queue garkor_final_battle.

JAIL AND GUARDS: ravine archers (huntrange 15) knock out on 1 in 20 arrows when the arrow lands;
monkey guards knock out (named guards always, plain ones 1 in 10); Aberab/Trefaji patrol the
route in quest_mm.enum and punch anything not in monkey form in sight. Wake in the cell
0_43_43_18_41; Lumo's banter follows 10 ticks later; pick mm_jail_door (Thieving, 15 xp).
Elder guards punch an unmonkeyed player in reach. The aunt calls the guards when she sees a
human (the child leg's "wait for the aunt").

MONKEY FORM: Hold the greegree (op 2) in Ape Atoll/zoo zones; transmog + stance come from the
obj overlay params; the timer reverts it the tick the greegree leaves the hand. Archers,
guards and the aunt ignore a monkey; the monkey minder needs it plus the M'speak amulet.

DIFFERENT FROM THE GUIDE: the demon fight is the pack's owner-private design (mm_demon.rs2),
not LostCity's shared one; no clue scroll from the zoo monkey (the pack has no clue rolls).
