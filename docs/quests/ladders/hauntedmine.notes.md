Haunted Mine notes (driven 2026-09-30, source = wiki + Quest Helper; LostCity's quest_haunted is Ernest the Chicken).

Zealot: stands at 3444,3258. Start needs Priest in Peril. Crafting 35 is checked at the outcrop, not at start.
Dialogue: allegiance -> "Saradomin" -> "challenges and quests" -> topic menu. "What quest is that then?" writes stage 1.
"Is there any other way into the mines?" writes heardaboutkey; only then does op3 Pickpocket give the key.
Long Zealot pages split in two; a click on the 2nd/3rd option row straight after them can miss, end with Escape.

Entrances: south tunnel hauntedmine_back_entrance2 (3429,3225), north tunnel back_entrance1 (3430,3233).
Ladders reuse one loc name at several tiles; the port tells them apart by the loc's own tile (hauntedmine_dungeon.rs2:65,127,138,148).
Level 3 north: ladder 2710,4540 -> collect room (cart at 2774,4537, dialogue "Take it."); ladder 2732,4529 -> lift room.
Collect-room ladder 2774,4540 climbs back to level 3 north at 2710,4538, not to the lift room.
The collect room lies inside the cart room rectangle: the cart op asks the collect zone first (dungeon.rs2:150).

Cart puzzle: pull levers 1,2,5,6 once each (targets 1), leave 3,4,7,8 at 0, then Check the panel at 2769,4522.
Wrong settings sink (fungus lost) or return the cart; the fungus then has to be picked again.
Fungus crumbles to ashes if dropped or carried out of the mine (timer, dungeon.rs2 fungus_monitor).

Lift room: chisel is a ground spawn at 2800,4501 (the crate loc is unused). Valve 2808,4496 needs the Zealot's key (kept).
Race: ghost shuts the valve after 80 ticks (constant hmq_lift_race_ticks, wiki gives no number). The walk valve->lift is long.
Turn the valve again after losing; the lift needs the fungus in the pack.

Flooded room: lift lands 2726,4455. East stair 2746,4436 -> Dayth room (lands 2810,4453). West stair 2692,4436 -> crystal
entrance (2758,4453). Without the fungus either stair drops you in a dark room; its bottom stair (2731,4561 / 2709,4591) returns you.
West stairs and the reward doors (2772..2773,4450) need the crystal-mine key.

Dayth: the key (2788,4455) sits inside invisible walls; approach from the north row (z 4456) or press it from 2795,4456.
Pressing it spawns Treus; he teleports around so the attack drops often, re-press Attack. Pixel presses on him answer covered.
Fight: ranged from 2799,4455; the cranes (2787,4451 / 2793,4458) and track rows hurt anyone beside them. Bring 20+ food.
Kill writes stage 9; pick up the key (stage 10), cut the outcrop 2787,4428 with a chisel (stage 11, 22000 Strength xp).
Cutscene: the wiki lists one (cart ride after Start); not ported, owner is speccing cutscenes.
