# losttribe -- relay notes (parity3f, wiki source, driven leg by leg)

Source: OSRS wiki + Quest Helper (LostCity has no Lost Tribe). Content: quest_losttribe/scripts/.

Stages: 1 started, 4 dug (contact 2 = Duke's leave), 5 brooch shown (^lt_book), 6 book read
(^lt_symbol), 7 generals, 8 contact, 9 ham_hunt, 10 treaty, 11 complete. Sigmund spawns 3209,3219 f1.

- Witness: always Bob (3231,3203). Cook is a red herring (quest_cook.rs2:14). Hans/Aereck do not tell.
- Cellar: kitchen trapdoor qip_cook_trapdoor_open 3209,3216 (plain travel). Rubble at 3219,9618: use a
  pickaxe on lost_tribe_cellar_wall (losttribe.rs2 mine_rubble). The hole is ONE loc both sides; from
  the cellar it goes in, from x>=3220 it comes out at 3218,9618 (losttribe.rs2 cavewall_hole).
- Brooch lies at 3230,9610 after the dig (kazgar stands there too).
- Reldo (3210,3494) is a hint only: ask "What can you tell me about this brooch?" (needs the brooch).
  The bookcase 3207,3496 works from stage 5. The book is interface 183: the arrows are server
  driven ("Turn page" if_button), six spreads, bookmark varbit = spread. The LAST spread (5 right
  presses) is what moves 5 -> 6 (losttribe.rs2 lost_tribe_book_*). Opening only is not enough.
- Generals: Wartface 2957,3511 behind goblin_outpost_poordoor_double_inner. Stage 6 only; the
  option "It doesn't really matter." leads to the long tail, the other two loop.
- Tunnels: the marked path is 36 waypoints from 3222,9618 to 3317,9612 (ladder.tsv guide travelLine);
  every waypoint walks with walk_to (12 ticks each). The cache's invisible trap locs sit beside it:
  trap_floor (3238,9622 3239,9622 3244,9635 3244,9636 and m51 ones) = collapsing floor, you land
  3209,9585 in the swamp caves and the light goes out; trap_ceiling = rockfall, 2 damage and the light
  goes out. Mapzone timer: losttribe_tunnels.rs2. Their meaning is inferred from the loc names.
- Bow: Goblin Bow (emote tab) within 3 tiles of Mistag at stage 7 starts the greeting. No unlock
  gate (emote.rs2 models none). Mistag spawns as shell lost_tribe_mistag 3319,9615.
- Mistag option "Can you show me the way out of the mine?" is offered from stage 8 on (finish.rs2
  lost_tribe_mistag_menu) and lands at 3230,9610. Kazgar (lost_tribe_guide 3230,9610) takes you back
  with "Can you show me the way to the mines?" (lands 3319,9615).
- Sigmund: pickpocket is op 3 (needs Thieving 13), key opens the chest 3209,3217 f1 -- stand on
  3209,3216 (the loc faces south). HAM trapdoor 3166,3252 (pick-lock op 5); crate 3152,9645 inside.
  The hideout is travel here (plain trapdoor); Duke at stage 9 with silverware accepts at once.
- H.A.M. lair entry (seam37 losttribe_trapdoor_maplink): drive it, never goto_tile into the lair.
  `click_loc("osf_trapdoor_closed", 5)` (Pick-Lock, sets varb235_ham_thief=1), then
  `click_loc("osf_trapdoor_open", 1)` lands on 3149,9652,0 from ANY side of the trapdoor
  (losttribe_ham.rs2 [oploc1,osf_trapdoor_open] -> ^lt_ham_trapdoor_in); `click_loc("osf_ham_ladder", 1)`
  returns to 3165,3251,0. Before the fix the Climb-down said "You can't go any further." (it went
  through ~climb_ladder(-1); maplink.dbrow had no row because the harvest names the multiloc base 5492, which has no Climb-down op).
  Sources: RuneLite shortest-path transports.tsv:1064-1068 (tools/data/shortest_path/transports/),
  2009scape HamHideoutPlugin.java, OSRS wiki H.A.M. Hideout (entrance west of the Lumbridge general
  store; lair focus area 3137-3199 x 9601-9663). Proof: build/quest_gate/seam37_losttribe_copy rows
  100-106 (enter, crate, silverware, ladder out).
- Post quest: use the brooch on Mistag for a mining helmet (losttribe_mistag.rs2 useitem).
- Not built: the treaty-signing cutscene (wiki, spec pending: cutscenes/ owner session).
