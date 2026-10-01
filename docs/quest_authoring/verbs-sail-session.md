# Verbs: `sail` and `session` (section 3)

## `sail` (`sail.lua`) -- a sea leg and a courier task, all through the client

### `t.sail.*`

`t.sail.state()` -> `(ok, reading)` (`aboard`, `hull_x/z`, `at_helm`, `sails_set`, `state`,
`arrivals`, `arrival_last`, `client_live`; `unsupported` on a socket run);
`t.sail.board(gangplank, op=2 "Board-previous")`; `t.sail.helm("Helm")`; `t.sail.sails(true|false)`;
`t.sail.sail_to(x, z, radius=2, ticks)` -- the HULL's tile, a straight line through the client's own
"Set heading" row, so route round a coast with legs (a straight line NE from Catherby runs aground
on the pier: 2800,3400 -> 2835,3402 -> 2835,3416 reaches the Current Affairs ripple);
`t.sail.await_arrival("[mapzone,0_43_52]"|nil, ticks)` -- a NEW bound arrival after the call, so
sail_to a tile short of the boundary first; `t.sail.disembark(gangplank)` (since seam24 `board`,
`helm`, `sail_to`'s heading ring and `disembark` each set their OWN camera pose, so never aim the
camera before them; `t.sail._press_deck_row` keeps the CALLER's pose -- aim it yourself for a sea
crate, as pryingtimes testKey does -- except that a row naming a ROOT npc beside the hull (a stopped
current duck) is framed automatically; `disembark` frames the plank itself and returns once the
CLIENT stands ashore with its scene settled);

#### Port tasks (wiki Courier tasks)

port tasks (wiki Courier tasks): `t.sail.task_board(board)`, `task_accept(index)`,
`cargo_take(ledger, op=3)`, `cargo_load("cargo hold")`, `cargo_deliver(ledger)` (a delivery needs
quest_pandemonium's ledger op1 to fall through to `port_tasks.rs2` -- open), `t.sail.tasks()` ->
`(ok, slots, text)`.

#### Setup for a Catherby leg

Setup for a Catherby leg: `::setlevel sailing 20`, `::setvar sailing_boat_1_owned 1`,
`sailing_boat_1_type 1` (skiff), `sailing_boat_1_port 6`, `sailing_last_personal_boat_boarded 1`,
`sailing_boat_1_hotspot_6 1` (a hold); walk onto the pier from the shore (2803,3430) -- a
`goto_tile` onto the pier strands the player.

## `t.session.*` (`session.lua`, seam 18) -- listed under section 3's `world` / `drive` / `player`

### `t.session.logout` / `login` / `relog` / `screen`

`t.session.*` (`session.lua`, seam 18) -- the guide's "or log out", through the client's own
screens: `t.session.logout()` -> `ok` `refused` `not_found` `not_visible` `timeout` (opens the
logout tab, waits until `logout:logout` is DISPLAYED, presses it, settles on the title screen; the
detail says whether the server closed the session or the client's 5 s fallback did);
`t.session.login(user?, password?)` -> `ok` `refused` `timeout` (Existing User, types the run's
account -- the session dir's name, which is `run.py --user` -- and `test`, settles on the world
answering a player tile, puts the backpack tab back; `refused` = the handshake bounced to the title,
client.log `login: rejected reply=N`); `t.session.relog()` = both; `t.session.screen()` ->
`(ok, "game"|"title"|"connecting"|"boot", n)`; aliases `t.player.logout/login`.

They take no target, so write `t.check("<step>", t.session.relog())` --
`t.exec(name, t.session.relog)` is FAIL `bad verb/target`. A relog RE-BOOTS the embedded server from
the save: spawned npcs, loc_adds, ground items and npc positions are gone (the point for "log out to
lose the temporary npc", a bug for anything else). Do NOT relog aboard at sea: the login is refused
(`reply=173`, open engine seam) -- relog ashore.

Aboard-ship facts from seam passes 16 and 18 (the deck staging tile, hull-projected radius, deck
floor objs) are in `seam-facts.md`.
