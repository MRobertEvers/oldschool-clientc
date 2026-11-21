# Random events from the recordings (2026-11-21)

Every cached RSProx session (about 4,540) was searched for `macro_*` npc adds, ops and the
event rooms. Each event below is built from the sessions named, with the OSRS wiki only where
no recording reaches (marked *unverified* in the scripts). Prison Pete, the Genie, Rick, Jekyll,
Arnav, the frog, the quiz, Molly, the certer and the strange plant already existed and are not
repeated here.

## What was found

| Event | Invitation npc | Sessions with it | Recorded inside | Built |
|---|---|---|---|---|
| Sandwich lady | `macro_sandwich_lady_npc` | 57 | 4536, 4573, 4196 (tray) | yes, `macro_sandwich_lady` |
| Mysterious Old Man (gift) | `macro_mysterious_old_man` | 50 | 4605 | yes, `macro_mysterious_old_man` |
| Count Check | `macro_countcheck_surface` | 43 | 4568, 4412, 1138 (fail) | yes, `macro_count_check` |
| Beekeeper | `macro_beekeeper_invitation` | 41 | none (3203 dismissed) | **no** (see Open) |
| Evil Bob (fishing) | `macro_evil_bob_outside` | 40 | 4635, 4508, 4543 | yes, `macro_evil_bob_island` |
| Pillory | `macro_pillory_guard` | 38 | 4527, 1105 | yes, `macro_pillory` |
| Drunken dwarf | `macro_dwarf` | 35 | 1101 | yes, `macro_drunken_dwarf` |
| Drill Demon | `macro_drilldemon_invitation` | 34 | 4629 | yes, `macro_drill_demon` |
| Gravedigger | `macro_gravedigger_invitation` | 32 | none (3258 another player's) | **no** (see Open) |
| Freaky Forester | `macro_forester_invitation` | 30 | 3976 | yes, `macro_freaky_forester` |
| Maze | `macro_maze_invitation` | 30 | 4485 | yes, `macro_maze` |
| Mime | `macro_mime_invitation` | 26 | 1101 | yes, `macro_mime` |

`macro_swarm`, `macro_shade`, `macro_zombie` and `macro_pirate` are world spawns in fixed places
(the swarm at 2748,3180 and 1747..1825,3714..3790, shades and a zombie at 2356..2365,5209..5216,
the pirate at Port Sarim), never added beside the recorder: not random events.

## The shared frame (`macro_invite/scripts/macro_invite.rs2`)

The invitation is the Genie's cadence, which every recorded event follows: S added beside the
player facing south, following; S+1 a smoke puff (86, height 124) on the add tile, smokepuff
(1930) at range 7, the greeting; S+10..S+90 a reminder every ten ticks with the type re-sent;
S+100 the closing line; S+103 gone in smoke. Dismiss (3203): `emote_ya_boo_sucks` at delay 10,
gone two ticks on.

| Pool | Greeting (recorded) | Reminders (recorded) | Closing |
|---|---|---|---|
| Dwarf | "'Ello der <n>! *hic*", `dwarf_drunkwave`, synth 2297 | 5 lines | "Aww, my mate doesn't like me." |
| Count Check | "Ahahahaha, <n>!", `emote_wave`, synth 2663 | 2 lines, each with a wave | wiki line |
| Old man family (gift, maze, mime, forester) | a pool line | "Talk to me, <n>!" and 4 more | "Maybe later." |
| Beekeeper/gravedigger | — | "Can I borrow you for a minute, <n>?" and 3 more | — |
| Drill Demon | "Private <n>, atten-SHUN!" | 4 lines, all "Private <n>" | "Maybe later." (4003) |
| Pillory | a pool line | 4 lines | "Maybe later." |
| Sandwich lady | "Sandwiches, <n>!" | 6 lines | "Maybe later." |
| Evil Bob | "Mew!" etc., cat_attack | 4 lines | "Meeeeowby laaaater..." |

Trips away share `~macro_away` (the move, the seq stopped, smokepuff to the player, the camera
reset, the minimap back to normal) and `~macro_home`; the varps 9835..9838 (perm) hold the way
home, the progress, the target and the event, and a login inside an event goes home
(`~macro_event_login`). `varb10056_macro_last` is written as recorded: Drill Demon 2, forester 5,
maze 7, mime 8, pillory 14.

## Per event

**Sandwich lady** (4536 [3000..3021]). Talk: the page naming the food; its click opens
`sandwich_tray` (297) after clientscript 2524, seven models in a shuffled order and the label
"Have a <food> for free!". The pick: the reward line, "Hope that fills you up!", the food, the
tray closed, all on that tick; she leaves nine ticks after the page. Models 10726 = baguette and
10728 = chocolate bar are recorded; the other five are assigned alphabetically, *unverified*.
The tray is a pause with seven resume buttons. Wrong pick: the wiki's line, no reward.

**Drunken dwarf** (1101 [20507]). Both reward lines, beer and kebab, his page; gone three ticks
after.

**Mysterious Old Man** (4605 [3007]). Reward line, present, page; gone three ticks after. The
wiki's 150-row table; its six coin weights are not split on the page (10/10/6/10/10/10 used).

**Count Check** (4568 [527..531]). Without a PIN: the game line, his page, the message box; gone
two ticks after. With `%varp6403_bankpin_set` the wiki's pass and a lamp. Re-talk: "I must be
leaving now." (1138).

**Pillory** (4527 [9006..9033], 1105). Arrest page, menu "What would you like to say?", the
answer; its click: smoke (height 92, range 5), then the cage at Varrock 3228,3407, the quiz's
panel lock, the guard added again for seven ticks, `pillory_locked`, the instruction overlay
(mesoverlay, clientscript 1974) and `pillory` (27). Locks pair with keys by shape (lock 13382 =
key 13396, 13390 = 4141, 13393 = 11032, 13394 = 13395; both sessions). Small locks: 9757 shut,
9758 open, 4730 unused. Each right key: the line and `pillory_unlock`. The last: `pillory_success`,
"You've escaped!", home, `macro_last` 14, the reward line. The tramp's throw (one recorded) is a
1-in-3 roll every six ticks, *unverified* cadence. A wrong key follows the wiki (one more lock, the
count starts again). Only Varrock's cage is used: Seers' and Yanille's cage tiles are not recorded.

**Drill Demon** (4629 [2516..2607]). The posts are varbits 1335..1338, each the exercise its sign
shows (1 jog, 2 sit ups, 3 push ups, 4 star jumps), all four re-written with each order; every mat
played the seq of its post. **Five** right exercises ended the recorded event (the wiki says four):
the script follows the recording. Order: his page, the sign's object box, then "Okay." / "I want
to leave." left up while the player walks to a mat. The sit-ups order's line and a wrong mat are
*unverified* (no recording). Reward: camo top first, then helmet, bottoms, then a lamp.

**Freaky Forester** (3976 [3026..3067]). Asked for two tails, the recorder killed a one-tailed
pheasant and it was accepted. The carcass is dropped for the killer; the Forester's "Now bring me
the raw pheasant."; hand-in thanks; the exit fires the tick after the player reaches 2594,4775
(the portal's loc is not in the transcript). Home with the reward line, the Forester's page two
ticks on. Lederhosen hat, top, legs, then a lamp.

**Mime** (1101 [10007..10154]). Round: spotlight on the mime (loc_anim on 2010,4761 / 2007,4761),
his emote two ticks on, his bow six ticks after, the spotlight to the player, the emote panel
(`macro_mime_emotes`) two ticks later; the copy; seven ticks on, cheer or cry; the next round's
spotlight eight to ten ticks after. A wrong copy does not reset the count (1101: one wrong, four
right). Buttons 0..4 are recorded (think, laugh, climb rope, glass box, cry); 5..7 (dance, lean,
glass wall) *unverified*.

**Maze** (4485 [505..663]). The percent is varp 531 itself (clientscript `macro_maze_timer` prints
it), written as three octal digits in varbits 1335/1336/1337: 1,4,4 = 100 at the start, 1,0,7 = 71
at the centre. 1% every five ticks. Walls: on the tick the player steps through, an `inviswall` on
the wall and `macro_maze_wallinactive` (rotation + 1) on the far tile, `door_open`, both put back
two ticks later; the far tile for the recorded rotation-3 walls is to the west (fitted to two
crossings). Chest: the box, the chest emptied. Centre: cheer, jingle `maze_centre`, the overlay
shut; seven ticks on three reward lines and home. The reward fits the wiki's formula exactly:
total level x percent / 30 / divisor (4485: coal 35, feathers 796, nature 8 at 71%). The three
other start corners are mirrored from the recorded one, *unverified*.

**Evil Bob (fishing)** (4635, 4508, 4543). The right spot is 2526,4791 in all three. Arrival tile
differs per trip (three recorded). Servant pages, the pointing box with overlay 186 and the camera
to the spot; fishing (nine ticks), uncooking (`human_firecooking` delay 30, three ticks), feeding,
the portal ("~Be seeing you!", raspberry), home, 650 xp. Wrong fish, cooked fish fed and the Magic
reward follow the wiki.

## Test

`test/randoms/random_events.lua` (`python3 tools/quest_gate/run.py --script
test/randoms/random_events.lua --no-publish`) starts each built event with its `::` hook and
drives it to its end: dwarf, Old Man, Count Check (fail), sandwich lady, pillory (keys read off
the server), mime (four copies), Drill Demon (five orders, mats read off the posts), Evil Bob
(the whole island), Freaky Forester (kill, hand-in, exit tile), the maze's arrival and clock.
96/96 on 2026-11-21. It does not walk the maze to the centre or take a wrong branch.

Three engine rules the first runs hit, each now in the scripts:
- A normal queue waits while a modal is up: the pillory's escape is a `strongqueue`.
- A script that has waited (`p_delay`) closes the chat box when it ends (`Player.executeScript`),
  so the mime's emote panel is opened by a queue entry of its own that never waits.
- Varp 531 is one word shared by Prison Pete, Arnav, the drill posts, the maze percent and the
  forester's pheasants (multinpcs on it: 0 shows no pheasant). `~macro_transmit_clear` clears it
  before the maze (which prints it whole) and around the forester; the Drill Demon clears its
  posts on leaving.

## Open

- **Beekeeper**: no recording reaches the hive. Its panel (`beehive`) is built by dragging parts
  onto slots, and the server handles IF_BUTTOND in C only (backpack, bank, tele nexus): a script
  trigger for interface drags is needed first. Not scheduled.
- **Gravedigger**: no recording reaches Leo's graveyard; the wiki gives no grave, coffin or
  landing coordinates or coffin contents. Not scheduled.
- Sandwich tray models 10727 and 10729..10732; mime buttons 5..7; Drill Demon's sit-ups line and
  a wrong mat; the pillory's other two cages and a wrong key; the maze's other three corners and
  its chest lines; the forester's portal loc; Evil Bob's wrong-fish and Magic branches.
- The drill sergeant walks a route in 4629; ours stands.
- Emote unlocks (mime, drill, zombie) are already unlocked in this tree (interface_emote), so
  those rewards give outfit pieces or a lamp only.
