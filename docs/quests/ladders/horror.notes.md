# horror -- relay notes (parity3e, LostCity source, driven leg by leg)

Source: LostCity_Server/content/scripts/quests/quest_horror (LostCity_Content2 has none). Content:
quest_horror/scripts/horror_interactions.rs2 (route), horror_encounter.rs2 (Jossik, both fights),
horror_jossik.rs2 (casket), horror_diary.rs2 (books). OSRS wins where the brief says: 30 nails per plank.

- Planks: woodplank spawns 2550-2556,3573-3576 (m39_55). Both sides need plank + hammer + 30 steel nails.
  Use the first plank at 2596,3608, CROSS that side (click it, op1, jump to 2598,3608), use the second
  on the east side at 2598,3608. `click_loc` for the second half picks the wrong side if you stay west.
- Gunnjorn 2547,3553: talk once with the quest started and the door still locked, he gives horror_key.
- Door 2509,3636 (oploc1): first click unlocks (key consumed), second walks into the COPY at 0_38_71
  (2445,4596). Without both planks Larrissa refuses ("fix the bridge"). The copy, not the real lighthouse.
- Copy stairs: base 2442,4600 op1 -> middle (bookcase 2444,4604, choose "Take all three books"), middle
  op2 -> top (cog 2443,4599). Use swamp_tar, molten_glass, then tinderbox ON the cog (oplocu). The repair
  drops you on the REAL lighthouse top floor (2506,3640, plane 2); its stairs lead down to the copy.
- Ladder horror_ladder_top at 2445,4603 (plane 0 of the copy) -> cavern 2518,4618. Before state 4 it says
  "You must fix the lighthouse". THE CAVERN DECK IS PLANE 0 (bridge deck; LostCity wrote plane 1). A press
  on the wall or ladder from plane 1 never hits: locs are held on plane 0. goto the deck with level 0.
- Strange wall 2514-2516,4627: Study (op1) opens interface horror_metaldoor; use rune/arrow/sword ON
  horror_mid_right_door, each asks "Yes". A sword must be a sword/longsword (not rusty), the arrow not
  ogre/training. far_right_door (2516,4627) opens only from the south and only with all six in.
- Deck ladder horror_ladder_top2 2515,4630 -> floor 2515,4632; floor ladder base2 2515,4631 -> deck 2515,4629.
- Jossik 2518,4633. Talking at state 4 queues the juvenile (camera lookat x6, then reset); state 5 queues
  the mother (cam_moveto 0_39_72_18_37 h500, lookat, reset). The wave can be retried: timeout 1000 ticks.
- Juvenile: 120 hp, hits by MELEE (drove it: fists, 99 str, about 130 ticks). A manual earth_blast cast on it
  paid xp but showed no splat in 60 recasts; not explained. Mother takes ONLY the matching element per colour
  (white wind, blue water, brown earth, red fire; orange melee, green ranged), 30 ticks per colour. She hit
  a 99-hp test player dead in ~34 casts: bring protect prayers or lots of food; she switches style with them.
- Completion (NOT driven yet) teleports to 0_39_156_19_17, casket to the backpack (Jossik keeps it if full).
  Jossik upstairs, 2509,3639 plane 1, runs the casket game (not driven either).
- Not ported: Barcrawl prerequisite + Yes/No offer (Barcrawl is not playable), private instancing, the
  god-book pages / 5,000 coin purchases (LostCity horror_godbook.rs2).
