# forgettabletale -- driven notes (client-verified, 353 rows, start to scroll)

Stand tiles (guide tile unreachable or wrong):
- Veldaban: stand 2827,10212. Drunken Dwarf: stand 2912,10221 (2913,10219 is walled off).
- Barmaid 2916,10193. Rowdy 2907,10198. Gauss 2839,10195.
- Conductor4 2906,10173; WWM cart click from 2919,10170. Conductor8 2922,10167.
- Khorvak: ::tele 2864,9878. Conductor6 2874,9874; return cart click from 2875,9870.
- Blue Opal director: level 1, stand 2867,10205. Secret cart: click keldagrim_train_cart with at=2919,10164.
- Rind: stand 2854,10197; patch 2854,10201.

Doors / carts / puzzle:
- Tunnel hub is level 1 near 1861,4954: card box 1862,4954, control box 1860,4955, train cart.
- Platform box and return cart are loc_add-ed on arrival (forget_puzzle.rs2:481); lost on server restart until next ride/login.
- Stones are counters, not items. Click cycles empty -> green -> yellow, skipping a colour with none left.
  A junction keeps its stone after Ok; clear it (click to empty) to reuse it.
- Group 1 hub 1G1Y; group 2 hub 1G2Y; group 3 hub 2G2Y; each platform box adds a stone (forget_puzzle.rs2:374).
- Routes 3/6/9 (layout in forget_route_expected) lead to the story rooms; others to a small (x<1900) or wide platform.
- Every story-room exit is forget_story_exit_next; the library one asks "Yes." and needs the
  bookcase (at=1904,4967, not the other copies) and both crates withpapers1/2 first.

Dialogue gates: Drunken Dwarf "Yes" chain, barmaid "A beer, please." / "A dwarven stout, please.",
director/conductor "Ask about closed off tunnel.", Veldaban "Very interested!" / "Yes." / "Sounds like just the job for me!".

Wanderers: none that block. Fights: none.

Deviations from the guide:
- Listening npcs stand at 1877..1879,4979 (constant forget_listening_*_coord, forget_story.rs2:19); the guide's
  gallery at z59 is sealed rock from the arrival cave, so talk_to said "can't reach".
- Statue/camera intro skipped; cutscene npcs are global, not private (forget_story.rs2:133).
- Kebab has no Keldagrim vendor: the player brings one; the finale fires on eating it with a beer inside
  the Laughing Miner (forget_pub.rs2:14).
- Cart fare 100 coins, not 200 (constant forget_fare). Ring of charos discount not done.
- Dialogue is paraphrased. Rind's optional letter and the barbarian planting are not done.
- Waits (kelda growth, brewing) are real; tests skip them with ::forget_growkelda / ::forget_ferment.
- Vat and barrel varbits are not transmitted: assert those steps by chat message, not varbit.
