## leg 1
- Ends at 2707,3733,0 (Relleka, beside Larry), quiet; varb3293_peng_quest = 25 (clockwork_penguin). Checkpoint 1 written.
- Backpack: clockwork book (peng_book), hammer, spade. Oak planks and nails were spent on the hide. Nothing worn.
- Setup gives levels hunter 10, agility 30, crafting 30, construction 34, thieving 15 and ::give plank_oak 10, nails 10, hammer, spade (leg 1 only). Leg 2's suit items (steel bar, plank, silk, ::coldwarpoh) are NOT in setup yet.
- File is in legs form: legs[1] = larry_and_hide; bind has constants not_started..clockwork_penguin, complete=135; add stage constants as needed.
- Larry's teleport to the iceberg lands ~2658,3987,1 (fade); the hide is used with t.player.use_on(item, by_symbol("loc","peng_observer_cabin_multiloc")); long npc lines page in two (match each page).
- Row boat 2654,3985,1 -> Relleka 2707,3732. Next step: enterPoh (see notes.md Suit section).

## leg 2
- Ends at 3203,3269,0 (Lumbridge sheep farm, beside the Thing), quiet, IN THE PENGUIN SUIT (varb3306_peng_transmog=1, suit in cape slot); varb3293_peng_quest = 50 (zoo_return). Checkpoint 2 written. Larry (peng_larry_zoo) stands at 3212,3263 here.
- Backpack: peng_book, peng_report_1, hammer, spade. Worn: suit (cape slot). Setup gives steel_bar, woodplank, silk (leg 2 only, all spent). Levels as leg 1 (no new ones).
- Stage constants added to bind: suit_iceberg=30, zoo_trust=35, zoo_report=40, lumbridge_visit=45, zoo_return=50.
- ::coldwarpoh is called inside leg 2 (t.cheat), then portal op 2 Home, bench poh_clockmaking_3 op 1 (Clockwork; then Clockwork toys -> Clockwork penguin); goto_tile out of the house to the zoo worked.
- Larry teleports (iceberg talk, zoo) work with options "Yes" (zoo bring) and "Yes." (header option on iceberg); partial option text needs /pattern/.
- Emote greeting: read varb3300..3302_peng_emote_1..3 (1 shiver 2 spin 3 clap 4 bow 5 cheer 6 wave 7 preen 8 flap), t.ui.widget("peng_emote:peng_emote_<name>") + t.ui.invoke(w, 0); both greetings this run were clap, preen, bow.
- Next: talk to the zoo penguin at stage 50 (secret phrase; cod or Ring of Charos), taking the suit off needs Larry (op1 talk while suited).

## leg 3
- Ends at 2644,4004,1 (iceberg outpost entrance, beside Noodle), quiet, IN THE PENGUIN SUIT (varb3306_peng_transmog=1); varb3293_peng_quest = 85 (kgp_again). Checkpoint 3 written.
- Backpack: peng_book, peng_report_1, peng_report_2, peng_report_3, peng_id, peng_cowbell, hammer, spade. Setup also gives raw_cod, swamp_tar, feather x5 (all spent except the last two never needed). Levels as leg 1.
- Bind constants added: thing_return=55, fred=60, outpost_info=65, iceberg_kgp=70, noodle1=75, noodle2=80, kgp_again=85.
- Next: talk to the KGP agent peng_kgp at 2640,4007 (state 85, "Let's see your ID" -> 90), then the avalanche peng_aval_l/r enters (needs suit on).
- Surprises: the suit comes off when leaving the zoo/farm, so each stop re-suits via Larry op3 (talk as a penguin turns you back); Larry's zoo teleport 'Yes' lands at the zoo; long npc lines page in two (Fred); choose needs /pattern/ for partial text; the zoo gate leaf may still be open in a full run (loc_near branch).

## leg 4
- BLOCKED at agilityExitWater (t.blocked in the file, after enterAgilityCourse): player stands at 2643,4034,1 (course start, level 1), quiet, IN THE SUIT; varb3293_peng_quest = 100 (agility_done). Steps 4.32-4.35 are PASS rows; 4.36-4.41 not driven (water leg 2628-2635,4053-4065,0 unwalkable, a later seam pass fixes it).
- Backpack: peng_book, peng_id, peng_cowbell, hammer, spade (the three reports were handed over). No new setup items or levels in leg 4. Bind constants added: debrief=90, agility_ready=95, agility_done=100, army_report=105, pingpong_go=110.
- The avalanche click lands inside at 2658,10373; the debrief room cannot be walked to the course door (walk ends in place), so goto_tile 2633,10403 (the south side of the door, plain travel) then click_loc peng_base_double_door_mid_agility (wall on the south edge of 2633,10404).
- The legs block hash changed, so the first --from-leg 4 needs one full run (checkpoint 3 is rewritten by it).
