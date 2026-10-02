## leg 1
- Ends at Varrock tailor Asyff, tile 3281,3397 level 0, outside any dialogue. Quest varb2780_eaglepeak_quest = 15 (entered); varb3110_eaglepeak_nickolaus_chat = 5.
- Backpack: 2 hunting_fake_beak + 2 hunting_eagle_cape (disguises; Nickolaus takes one spare pair later), hunting_book_of_birds, 10 coins. Metal feather consumed on the door. Nothing worn.
- Setup: ::clearinv, hunter 27 (Charlie refuses below it), yellowdye, swamp_tar, 60 coins. No other levels.
- File is the legs form (bind + legs table); leg 1 is "charlie_to_asyff". Append leg 2 and run --from-leg 2.
- Surprises: after entering the cave (level 3) walk_to 2005,4971 works (no goto needed); the shout works at once with the default camera in this run. goto_tile into the cavern is flagged CHEAT by helper_coverage for later room entrances, so walk to rooms or click the tunnel mouths from where you stand.
- Parity notes: Nickolaus needs camera facing north if talk_to says "framed nothing"; bronze room needs the net-trap pedestal stood up by the tunnel mouth.

## leg 2
- Ends in the main cavern (level 3) at 1986,4949 just out of the bronze tunnel, outside any dialogue. Quest varb2780_eaglepeak_quest = 15 (unchanged).
- Backpack: eaglepeak_crystal_feather3 (bronze) x1, 2 hunting_fake_beak, 2 hunting_eagle_cape, hunting_book_of_birds, 10 coins. Nothing worn. No new setup levels.
- Cave entered by goto 2329,3496 then click; walk_to 1987,4950 (1986,4951 times out) then click the bronze mouth. In the room goto_tile between winches is fine.
- Net drops on the first Take; all four winches then work; room pedestal is stood up by script.
- Checkpoint 2 written; run --from-leg 3 next.

## leg 3
- Ends in the main cavern (level 3) at 1986,4972 on the silver tunnel mouth, outside any dialogue. Quest varb2780_eaglepeak_quest = 15 (unchanged).
- Backpack: eaglepeak_crystal_feather3 (bronze) x1 + eaglepeak_crystal_feather2 (silver) x1, 2 hunting_fake_beak, 2 hunting_eagle_cape, hunting_book_of_birds, 10 coins. Nothing worn. No new setup levels (hunter 27 from leg 1 covers the trail).
- walk_to north from the bronze mouth (1986,4949) never moves (4 tiles tried); goto_tile 1987,4971 then click the silver mouth works. helper_coverage flags that goto as a CHEAT for enterGoldRoom (the heuristic matches the gold entrance symbol near the silver mouth), so gold leg should check it.
- Kebbit: inspect opening shows a mesbox (chat.play "mesbox:A kebbit watches..."); threaten is talk_to npc op 3, choose "Taunt the kebbit."; the silver feather lands on the kebbit tile, click_obj takes it.
- Checkpoint 3 written; run --from-leg 4 next.

## leg 4
- Ends in the gold room (level 2) at 1948,4898 beside feeder2, outside any dialogue. Quest varb2780_eaglepeak_quest = 15. Gold gate3 = 0 (lever3 pushed up), gate4 = 1; birds 5, 4, 1, 2 set (feeders 4, 3, 1a, 2); bird 3 (mechbird3) still to do.
- Backpack: eaglepeak_bird_seed x2 (6 taken, 4 spent), bronze feather3 x1, silver feather2 x1, 2 fake beak, 2 eagle cape, book of birds, 10 coins. Nothing worn. No new setup.
- Leg 5 still needs: lever1 down (gate1) and lever2 down (gate2), feeder2a (bird3, needs gate1+gate2+bird2), then Take the gold feather from eaglepeak_dungeon_pedestal_puzzle1 at about 1928,4906.
- Silver mouth tile 1986,4972 is boxed in for the walker: goto_tile 1988,4973 (level 3) first, then walk_to 2022,4982 works and click_loc enters the gold mouth.
- Guide order differs from the port's canonical one: feeder1a (bird1) is fed with both lever3 and lever4 DOWN, then lever3 is pushed up. fillFeeder3 (feeder3a, wrong-bird recovery) is a GUIDE-GAP comment in leg 4.
- In-room goto_tile between levers/feeders is fine; feeder1a stood at 1931,4915.
- Checkpoint 4 written; run --from-leg 5 next.

## leg 5
- Ends in the main cavern (level 3) at 2002,4948 beside the stone door (feather door), outside any dialogue. Quest varb2780_eaglepeak_quest = 15. All three feathers inserted (gold feather1, silver tracking 6, bronze feather3); door not yet opened.
- Backpack: eaglepeak_bird_seed x1, 2 hunting_fake_beak, 2 hunting_eagle_cape, hunting_book_of_birds, 10 coins. Nothing worn. No new setup.
- Leg 6 opens the door with oploc1 on eaglepeak_gate_mirror (feather_door.rs2:56, teleports past it when all three are in).
- exitmid in the gold room needs click_loc opts { stand_on_square = true }. Gold mouth tile 2023,4982 is boxed in: goto_tile 2021,4982 (level 3) then walk_to 2002,4948 works.
- fillFeeder5 / fillFeeder4Again re-feed already-seeded feeders; the content refuses and keeps the seed (rows check the seed count is unchanged).
- Checkpoint 5 written; run --from-leg 6 next.

## leg 6
- Ends at Ardougne Zoo, Charlie, tile 2607,3263 level 0, quest varb2780_eaglepeak_quest = 40 (complete), scroll closed.
- Backpack: hunting_box_trap, hunting_book_of_birds, 10 coins, 1 bird seed; beak and cape worn (spare set taken by Nickolaus). Setup gives no extra levels.
- Door: oploc1 eaglepeak_gate_mirror from 2002,4948 teleports past; eagle guard talk_to sneaks in (needs beak+cape worn); from the nest the same guard (talk_to) sends the player back out at 2007,4952.
- leavePeak: goto_tile 1993,4980 then click exitmid with stand_on_square (walkable centre square); declared GUIDE-GAP (eaglepeak.rs2:178), as is leg 5's gold exitmid (gold_room.rs2:343), so helper_coverage reads CONTENT_GAP not FULL; gate.py green, lint clean.
- Camp talk at 2317,3503 gives ferret and box trap via mesboxes; Charlie pays 2500 Hunter XP (asserted literally).
- Full run 205 rows PASS (3 runs of the full test plus 3 leg runs).

## ROUND 2 (orchestrator, 2026-10-02, after sampler matthew-mbp-m4-b50)
Legs 4 and 5 are reopened (their notebooks retired as leg<K>.progress.round1.md); legs 1-3 and 6 stand.
The round-1 file (6d75971d2) is installed with two ROUND 2 t.blocked markers:
- leg 4: fillFeeder7 (feeder1a, the guide's recovery step for a blocked lever 1) was pressed before pushLever1Up,
  moving mechanical bird 1 early. Drop it; follow the guide's main-path order.
- leg 5: fillFeeder5 then read as a refused press (fillFeeder5.kept). It must be a driven press feeder1 accepts
  (gold_room.rs2 eaglepeak_gold_feeder_ready case 1) that moves bird 1.
The goto_tile detail now says "at <landing> from <departure>" (driver b50); a goto past a gate the guide names reads CHEAT.
