Forgettable Tale -- driven notes (2026-09-30, parity3e; start to scroll)

Stand tiles: Veldaban 2827,10212. Drunken Dwarf 2912,10221 (2913,10219 is walled off).
Barmaid 2916,10195 (the Laughing Miner bar). Rowdy 2907,10198. Gauss 2839,10195.
Conductor4 2906,10173. Conductor8 2922,10167. Rind 2854,10197, patch 2854,10201.
Khorvak: goto 2864,9878; conductor6 2874,9874; return cart from 2875,9870.

Carts: the map has none. The script places (2919,10169) to White Wolf Mountain and the
secret one (2919,10164); stand at 2919,10166 for the secret one. Tickets cost 100 each way.
The secret cart needs both hands empty, 2 free slots and the boards gone.

Director: the guide says Purple Pewter, but ::complete giantdwarf leaves company 0 = Blue
Opal. Use dwarf_city_director_blue_opal, level 1, (2869,10203) (stand 2867,10205). Only
your own company director removes the boards and writes stage 100.

Waits are real minutes (4 per kelda stage, 8 per vat stage): t.clock.skip(16) for the patch,
t.clock.skip(40) for the vat. Vat/barrel varbits sit in farming_varp_9, now transmitted and
saved (skill_farming/configs/farming_patch.varp). Brewing is use-item-on-vat in order: 2
water, 2 malt, kelda hops, yeast (Cooking 22). Blandebir fills a pot for 25 coins; the pot
is on the table upstairs. Valve is a click; the barrel takes a beer glass.

Puzzles: hub is level 1 at 1861,4954 (card box, control box, cart). Search the hub box once
per group, each platform box once. Stones are counters: a click cycles empty, green, yellow,
skipping a colour with none left. A junction keeps its stone after Ok; clear every junction
before the next route. Route layouts (index 0-based, Y yellow G green): forget_route_expected
in forget_puzzle.rs2. Buttons are forget_puzzleN:switch_a.. and ok_button (IF1: invoke op 0).
ui.await_close answers a bare ok; grade it with t.check. Routes 1,4,7 land on the small
platform, 2,5,8 the wide one (boxes are loc_add-ed on arrival, forget_puzzle.rs2:481);
routes 3,6,9 lead to the story rooms; every story exit is forget_story_exit_next.

Library: only the bookcase at (1904,4967,2) counts (opts {at=...}); both paper crates too.
The exit asks Yes/No (choose only). Listening npcs stand at 1877,4979 (forget_story.rs2).
Room six: wait about 8 ticks after the cart; 9 pages play; you wake at 2922,10166 (120).
Continue Veldaban's last page (t.chat.continue_) before stage 130 is written.
Finale: buy a beer from the barmaid (2 coins), eat a kebab inside the pub ground floor.

Deviations from the guide:
- Rowdy asks for one of 24 junk items; the test stages it with ::give.
- Kebab has no Keldagrim vendor: the player brings one (forget_pub.rs2:14).
- Cutscenes are not ported (5 scenes, spec pending); story-room npcs are global.
- Ring of charos fare discount, Rind's optional letter: not done.
