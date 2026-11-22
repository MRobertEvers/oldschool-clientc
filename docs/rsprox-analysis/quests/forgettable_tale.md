# Forgettable Tale... (A Forgettable Tale of a Drunken Dwarf) from RSProx

Recordings (the owner's, rev 242.1, recorder `LoveTori` -> `<displayname>` everywhere):

| index | covers | stages |
|---|---|---|
| 4637 | start (Veldaban [515..763], the opening flyover [771..845]), the Drunken Dwarf, Rowdy dwarf, Khorvak, Gauss, Rind, raking and planting; the rest of the session is Enlightened Journey | 0 -> 40 |
| 4646 | harvest, Rind, the brewery, the barrel, the stout, the tale (four fades), the conductors, the Green Gemstone director, the three puzzle groups, the listening room, the library, the last scene and the memory, Veldaban, the pub ending | 40 -> 140 |
| 4641, 4643 | Enlightened Journey sessions that log in at 40 / 140 (the login varbit list only) | |

Our content before this pass (quest_forgettabletale, built from Quest Helper and the wiki, no
recording) had none of the recording's pages: every Veldaban, Drunken Dwarf, Rowdy, Gauss,
Khorvak, conductor, director, story-room and pub line was invented. The ending (eat a kebab
in the pub) does not exist: the recorded ending is DRINKING THE BEER in the Laughing Miner at
stage 130 (4646 [5135]), which starts a crowd scene; stage 140 goes out at its end.

## Stages (`forget_quest`, varbit 822 on varp 521)

All recorded values equal ours (0 10 20 30 40 50 60 65 70 80 100 105 110 115 118 119 120 130 140).

| value | goes out with | index [tick] |
|---|---|---|
| 10 | the click on Veldaban's last page ("Excellent, good luck..."), with "You've started a new quest: @q_mes@Forgettable Tale...</col>" and the fade | 4637 [771] |
| 20 | the Drunken Dwarf's "I'll shtop, right after you get me shome of the good shtuff!" page | 4637 [1051] |
| 30 | the click after "Maybe that's just because I don't drink as much as you."; the first kelda seed added with it | 4637 [1129] |
| 40 | the click on Rind's "Just take care of the weed..." (goes out with "Thank you! Your advice is much appreciated!") | 4637 [2206] |
| 50 | the harvest (farming 8 -> 3, kelda_hops added) | 4646 [293] |
| 60 | filling the glass at the barrel ("You fill the glass with Kelda Stout. The barrel is now empty.") | 4646 [2217] |
| 65 | the stout given (the click on "Here you go then.") | 4646 [2332] |
| 70 | the click on "But I shall investigate that tunnel immediately!..." | 4646 [2405] |
| 80 | the click closing the conductor's "Perhaps you have an influential friend?" | 4646 [2586] |
| 100 | the click on "Forthwith, <displayname>, forthwith!..." (boarding_removed 1 with it) | 4646 [2656] |
| 105..119 | the puzzle groups and story rooms (below) | 4646 [53378..71950] |
| 120 | the end of the last scene | 4646 [71950] |
| 130 | the click on "Mmm, yes, I'll be perfectly alright..." (Veldaban) | 4646 [5017] |
| 140 | five ticks after the pub copy is left, with the scroll | 4646 [5301] |

## The opening flyover (4637 [771..845]) — no copy, the world's own city

| tick | packets |
|---|---|
| C=771 | stage 10, the start line, fade out, chat closed |
| C+4 | the lock (`option_areasounds_override` 100), `p_telejump(2876,10180,0)`, hud lock, minimap 2 |
| C+5 | `p_telejump(2910,10204,0)` |
| C+6 | mesbox " " with no continue, `midi_song silence`, `midi_jingle forgetful jingle open (106)`, fade in |
| C+7 | `cam_moveto(2888,10176) 1200 232/232`, `cam_lookat(2880,10178) 700 232/232` |
| C+15 | `cam_moveto(2911,10182) 1200 2/2` |
| C+24 | `cam_lookat(2914,10220) 700 2/2` |
| C+28 | fade overlay closed; `keldagrim_titles` opened, `keldagrim_rise_of_part_2` shown |
| C+42 | rise_of hidden, `keldagrim_forgettable` (152:16) shown |
| C+48 | `cam_moveto(2909,10214) 1200 2/2` |
| C+54 | forgettable hidden |
| C+59 | titles closed, fade out |
| C+62 | `p_telejump(2913,10218,0)` facing (2913,10219), the unlock, hud unlock, the mesbox closed, fade in, minimap 0 |
| C+65 | `midi_song land of the dwarves (396)`; C+66 the player's line "Well, here I am..." |
| click | chat closed, fade overlay closed |

## The tale (4646 [2355..2405]) — four fades, no camera, no move

The click on "First of all..." (C): fade out + chat closed. C+4 fade in; C+7 "...and then I
nearly fell into the Abyss! But I..." (no continue); C+14 closed + fade out; C+18 fade in;
C+21 "...so I had to run for my life, but then..."; C+26 closed + fade; C+30 in; C+33 "...pink
dragons..."; C+38 closed + fade; C+42 in; C+45 "...and I don't really remember anything after
that..." (continue).

## The tunnels

- The region is per player and built at run time: every entry is a new instance copy
  (`--raw`: 13232,1072 ...) and `forget_region_status` (837) 1; Jagex streams the track locs
  (`keldagrim_track_*`) per plane while hopping the player across the region, then lands them
  at the hub (1863,4956,1). Our map `m29_77` already holds the hub (box 1862,4954; machinery
  1860,4955; cart 1864,4957) and some track.
- The junction stones are BACKPACK ITEMS: `turnleft_trackjunction_control_card` (6120, the
  green stone) and `turnright_trackjunction_control_card` (6119, the yellow). Dropping one:
  mesbox "You'd better hang on to this, it looks important." (4646 [2920]). "Return to
  Keldagrim?" Yes deletes them (4646 [2947]).
- Opening the machinery (4646 [3001], [3070]): every junction back to 0, `forget_num_left` =
  green stones carried, `forget_num_right` = yellow stones carried. With none: mesbox "You look at
  the strange machinery and feel as if you're missing something... perhaps something to put into
  those strange holes in the stone slab?" ([2984]).
- A junction click cycles 0 -> 1 (a green: num_left - 1) -> 2 (green back, a yellow: num_right
  - 1) -> 0 (yellow back), skipping a colour with none left; synth `forget_placestone` (1116) on
  every change. With neither: "You've run out of stones." ([2988]) / "Out of stones." ([2991]).
  OURS HAD THE COLOURS SWAPPED (green on num_right).
- The hub box: "You find a green stone and a yellow stone in the storage box. You wonder what you
  could use these for..." (group 1, [2904]); "You find a green stone and two yellow stones in the
  storage box." (group 2, [3654]); "You find two green stones and two yellow stones in the storage
  box." (group 3, [4311]); emptied: "There is nothing more inside this box."; synths
  `forget_box_open` (1111) at O, `forget_box_close` (1109) with the line (O+3), busy 1.
- The cart ride: `exactmove` per straight run (delay1 30, delay2 135/90/80..), `cart_away` (1602)
  delay 30 on the first, `forget_cart_turn` (1113) on each turn; a blocked junction: the player's
  "This junction is blocked. I'd better go back to the start and find some way to make it go left
  or right." then a walk back to the hub; a dead end: "I've come to a dead end, I'd better go back
  to the start."

## The pub ending (4646 [5135..5301]) — a copy of the Laughing Miner

D = drinking a beer (inventory op 2) at stage 130 in the pub with a kebab in the pack: beer ->
beer glass in the slot (the glass and the kebab both go at D+160, [5295] slots 3, 5), `human_eat`, synth `liquid`, "You drink
the beer. You feel slightly reinvigorated..." / "...and slightly dizzy too." (spam), fade out.
D+4 the lock + mesbox " "; D+5 into the copy (template 2916,10190,0); D+8 the stance
`human_sitting_drunk_ready` and the crowd (patron1..5, sitting, the drunken dwarf), each facing
the chair; D+9 fade in; then overhead lines, three ticks per half: the player "I knew it were you
matey!" / "'Ere, have some of the" / "good stuff!" (D+11..17), the dwarf "Very kind of you," /
"<displayname>!" (D+26), patron2, patron3, patron5, the player's kebab tale (D+66..78), laughs,
mesboxes " " (D+90, D+111), `human_sitting_drunk_fall` 30 (D+109), Rowdy at the door (D+112),
camera re-aims (D+113, 114, 138), Colonel Grimsson (`forget_redaxe_henchman3_cutscene`) at D+119,
`forgettable_evil_dwarf_laugh` + "Ha, the spell has worked its" / "magic again." (D+126), the
helmet (`changetype henchman4`, D+136), "No one will believe that" / "drunken fool now!" (D+140),
fade out D+156, out of the copy D+161, unlock + fade in D+162, stage 140 and the scroll D+166.

The scroll ([5301]): model `kelda_hops`; "2 Quest Points", "5,000 Farming XP", "5,000 Cooking
XP", "2 Mature Dwarven Stouts" (two unstacked stouts). Ours had the model and the order wrong.

## Action inventory (rsprox_actions.py over 4637 [0..2300] and 4646)

| action | verdict |
|---|---|
| Veldaban, Drunken Dwarf (+ use-items: "Nothing interesting happens."), Rowdy, Gauss, Khorvak, Rind, Tombar/old dwarf (generic) | this content, rebuilt page by page |
| patch rake/plant/harvest, kelda hops destroy | this content (rake/plant: 3draster-04's 388d8018e4; the harvest shape fixed here) |
| Blandebir (5-option brewing menu), vat (4 ingredients, wrong-step mesboxes), valve, barrel, kelda stout Drink | this content / generic brewing, rebuilt |
| Barmaid "I'd like a Kelda stout please." | this content, added |
| conductor 1/2/4 Talk-to, conductor 4 Travel (WWM), conductor 6 Travel, carts | generic Keldagrim travel: conductor 4 Talk-to now the station menu; WWM added to Travel; every cart ride is its recording (see "Every recorded minecart ride") |
| conductor 8 "Ask about closed off tunnel." | this content, rebuilt |
| Green Gemstone director | this content, rebuilt (player's company) |
| secret cart, hub box, machinery, junction switches, Ok, hub cart (1, 2), return carts, platform boxes | this content, rebuilt as items + recorded lines; per-route rides Open |
| listening room npcs, story exits, library crates/bookcases, book (392), letterscroll (70) | this content; the book's and letters' texts Open |
| beer drink in the pub | this content: the ending |
| bank, shops (Hervi silver task, gem stall, bakery, crafting, xbows, sculptor), Blast Furnace, stamina potions, Ali Morrisane, glory/Varrock teleports, quest list/journal clicks, skill guide, logout | the recorder's own errands: not this content |

## The rides (every track, not only the recorded ones)

`OSRS-Content/.../quest_forgettabletale/scripts/forget_tracks.rs2` is GENERATED from 4646 by
three scratch tools (kept with this doc's history; rerun them to regenerate): the loc build of
each group (`loc_add_change` of `keldagrim_track_*`, planes 0, 1, 3: 505 / 594 / 707 pieces),
a walker over it, and the movement rules below. The model reproduces all 20 recorded rides
tile for tile before any unrecorded branch was built from it.

**The trees.** Each group is a binary tree of switch junctions (`tjunction_a` rot 0): 8, 12 and
19, exactly the three interfaces' switches. Switch i is the i-th junction breadth-first from
the hub, north before south (every recorded layout agrees, and so does Quest Helper's route
table); group three's only exception, junctions 16 and 17 swapped, is forced by ride [4653].
Ends: a buffer (dead end), a tunnel mouth (a platform, a story room, a plane-3 bridge or a
plane-3 dead end), or two branches merging (`tjunction_a` rot 2). 83 leaves: 40 empty
switches, 30 dead ends, 10 platform arrivals, 3 rooms.

**The rules** (4646 [2874..4692]):
- the start: onto the cart's tile (1864,4957) in `human_minecart_anim`; moves at T+1 (3 tiles,
  delay2 135, `cart_away` delay 30), T+4 (90), T+6, then one a tick;
- a straight move is up to 3 tiles (delay2 60; 1 tile 40);
- a turn at a junction or bend (x,z): exactmove (x,z)->(x, z±(m-1)) with m = min(4, distance to
  the corner), the real tile (x+1, z±m); a corner turns east with exactmove (c,zc)->(c+m-1,zc)
  and tile (c+m, row) where row is the line east of the corner; a 3-tile turn is delay2 75;
- `forget_cart_turn` once for a move that turns and once for one that arrives at a turn;
- an empty switch: the cart stops a tile short (last move delay2 80), the player's line the
  next tick, the hub three ticks after the click; a dead end: last move 130, the line three
  ticks later, the hub three ticks after the click;
- a mouth: the move that would arrive at the last tile goes through it (per-mouth length and
  delay recorded), onto plane 3; ours jumps without the forced move there, because our encoder
  sends the move relative to the tile after the jump;
- a platform: standing four ticks after the arrival, then one step off the cart toward its box;
- the group-two bridge: plane 3 (1897,4991) -> (1901,4990) -> plane 1 junction (1904,4958).
- Ok shows the junctions as set (`tjunction_a_left` green, `_right` yellow, [3021]).

Each entry to the tunnels is a new copy of square 29_77 with the group's pieces laid in it
(`~forget_enter_tunnels`); the story rooms and the last scene play in that copy. Group one's
third platform (row 4990, box 1915,4987) was never reached in a recording: its return cart and
its box ("The box turns out to be completely empty...", as group two's third) are by analogy.

Tests: `test/quests/forgettabletale.lua` (the quest, 312/312) and
`test/quests/forgettabletale_tracks.lua` (all 80 non-room leaves, 269/269).

## Wire audit of the scenes (2026-11-22)

Every scene played under scriptrun with `TORIRS_SCRIPTRUN_WIRELOG` (a driver that sets the
stage and the junctions with cheats, since scriptrun cannot press the machinery) and diffed
against 4646 with `tools/rsprox/wirelog_tells.py`: interfaces, vars, camera, sounds, songs and
the player's moves and facing against the default transcript, every cart ride against the
`--raw` one (copy coordinates). After the fixes every scene diffs to nothing but the lines
named under "left".

| scene (4646) | found and fixed | left |
|---|---|---|
| the tunnels' copy, every entry ([2798], [3580], [4270] rebuild_region) | Jagex composes it: the template's planes 0 and 1 with the platforms' zones (template planes 2 and 3) copied over them past each mouth, so a cart rolls out of a mouth straight onto its platform. Ours copied the square and jumped every mouth to plane 3. Now built zone by zone (`~forget_tunnels_copy`), every ride in copy coordinates (`tools/rsprox/forgettable_tale/copymap.py`) | -- |
| tunnel entry, every one ([2798..2807], [2967..2976], [3580..3589], [4270..4279]) | eight hops, one a tick: the centres of the copy's four quadrants on planes 0 and 1, the same for every group (ours had per-group template tiles), the first with a facing reset (`p_facereset`, new); the box flags and "Right on Track" on H+1 of every entry; the junctions reset when the machinery opens, not when a room is left | -- |
| pan 1 [2807..2826] | the lock with area sounds 100 and Jagex's fuller panel lock; no second look-at; the line with no continue at A+4; the unlock by itself at A+19 | account-state lines (xp drops off in the fixture) |
| pan 2 [3589..3604] | neutral mood; the unlock at A+15 | -- |
| pan 3 [4279..4304] | neutral; the drift at A+3, a second look-at at A+10, the line at A+6, the unlock at A+25 | -- |
| every cart ride, secret cart and White Wolf Mountain rides included | `p_exactmove` takes a 0..3 direction, so every angle went out as north; a straight step carries `[facing] angle`, a turn the reset (the engine's step-after-an-angle rule); boarding turns the player one tile east, the way the cart goes (`facesquare` now drops the pending face toward the clicked loc); delays and tick gaps; turn sounds (a mouth's extra, the tails'); no step jumps | -- |
| listening room [3362..3473] | the rooms' song and its unlock at A+4; the overlay at A+7; the side panels taken, the music tab shown at A+3 and given back with the first hop out; the talk from A+12; each later page shuts the last; the closing line a continue page (a strong queue) | the recorder's own clicks on the figures |
| library [4074..4082], exit [4266] | the song at A+4, the overlay at A+8; the menu shut on the answer | the recorder's searches |
| last scene [4692..4887] | two moves past the mouth before 119, with the black; the tunnel boarded again at T+3; region status 0 at T+191 with the area's song; the lock as the pans'; npc pages with no chat line, said overhead in 184-px pieces every three ticks, the head for the pieces, the page packed from whole pieces (Jagex breaks a page only where a piece ends); the facing reset at T+12; the last line with no continue; "Right on Track" at T+176 is the jingle's end, not the scene's | -- |
| pub ending [5135..5311] | Jagex's copy: 32x32 tiles from 2896,10184, without the plane-1 zone over the chair, so the D+6 camera looks in over empty ground (ours copied the whole square: rock); the lock as the pans'; the D+6 hop above and back; the facing reset on entry; silence and "forgetful jingle last" at D+9; the page shape and the pieces; five sounds; the glass and the kebab at D+160; the scroll "You have completed Forgettable Tale..." with the hops at 74,128 and 917; Quest Complete 1; both jingles' comebacks (D+168, D+176) | "Very kind of you, <name>!" splits by the name's width (the bot's fits one piece) |

### Cart step timing

`gen.py`'s "rides matched 20 of 20" compared landing tiles only. `delays.py` replays the same
rides for each step's delay, the ticks to the next and the turn sounds; `copymap.py` the
copy-local path against `--raw`. All four print 20 of 20. A three-tile step running into a
turn, or into a mouth of delay 90 or more, is delay 90 and the next step waits a tick more;
the start is 135 then 90 (60 when its second step is two tiles); a stop is 80 over one tile and
130 over more.

### Generic fixes made with this audit (not only this quest's)

- `p_facereset` (opcode 11372): the player's FACING reset, which no command sent.
- `facesquare` drops a pending reorient to the clicked loc: the script's square is the face.
- The engine sends `1508("0")` (`messagebox_bind_actions`) whenever a messagebox leaves the
  chat modal: before the close on a continue click, after a script's close, before a mount
  that replaces it (4646: every one of 128).
- A jingle that starts under another keeps the first one's comeback (4368 [230]/[235], 4646
  [5303]/[5311]); recorded lengths for jingles 104 and 105.
- `~music_unlock`'s line uses `@mes_hl_red@` (every rev 241/242 session; `<col=ff3045>` is
  239 and older).
- The quest scroll opens with `917` (every rev 242 scroll from index 4602 on; 2524 before).
- `maps/instance_singleway.csv`: the tunnels' and the pub's squares (no 4605 in any copy).
- `wirelog_tells.py`: ids for named sounds and songs, `cam_unlock`, and our `face_entity -1`
  as `face_reset`, so equal packets diff equal.
- The story rooms are copies of their own (`~forget_room_copy`, forget_story.rs2), as 4646's
  rebuilds at [3365], [4077] and [4704] are: only the room's zones (10, 9 and 30 on plane 0;
  10, 9 and 24 on plane 1), template planes 2/3 onto 0/1, the tunnels' copy freed after the
  move. The grid keeps the template square's layout with only those zones set, which goes out
  as the same zone pattern (our listening-room rebuild: 157 bytes against 820-924 for a tunnel
  copy).
- The client contours a loc added at run time (`world_builder_apply_loc_change`), as the deob
  does on a loc add (`Statics.method8559` -> `class393.method8974`: light, then contour). The
  queue was drained only at a build's End, so the ~1,800 scripted track pieces were drawn flat
  at their centre height; 45 of them sit on the platform mouths' ramps (6 in group 1, 22 in
  group 2, 17 in group 3) and floated half above the slope. Rotation and mirroring matched on
  every path. Checked by a probe (a sloped station rail deleted and re-added: identical to the
  static one); not yet photographed in a live session.
- The quest test presses the junction switches and Ok with op 1, as Jagex's client sends them
  (`[if_buttonx] ... op=1`); op 0 is an IF1 button's press and scriptrun refused it, so the
  Tests page's scriptrun run never set a junction and its replay never showed the interface
  change. The quest driver's click-loc settle also counts a multiloc's resolved child changing
  (the hops patch, flipped by varbit 823), which the headless host keeps in one row. The whole
  test passes under scriptrun now (1741 ticks).

The selftest's home-teleport check fails the same on HEAD's engine (the 2026-11-20 home
teleport rewrite).

## Every recorded minecart ride (2026-11-22)

Every exactmove run of three or more steps in 1741, 3224, 4494, 4495, 4531, 4637 and 4646
(`allrides.py`). 1741's are agility jumps (seq 741, 3067) and 4531's a magic carpet: no carts.
The carts:

| ride | recordings | ours |
| --- | --- | --- |
| GE trapdoor -> Keldagrim (12 steps, fade at the click, move +3, seated +4, ride +5) | 4494 [2816], [4447]; 4495 x3; 4637 [411], [1310]; 4646 [140] | `~keldagrim_trapdoor_ride`; seated a tick after the move (was the same tick) |
| Ice Mountain -> Keldagrim (the same frame) | 3224 [7123] | `[oploc1,icemountain_train_cart]` -> the trapdoor ride |
| Keldagrim -> GE, track 3 (9 steps) | 4494 [2766]; 4495 [1282]; 3224 [7411] | `~keldagrim_cart_to_exchange`; turned east a tick before boarding (new) |
| Keldagrim -> Ice Mountain, track 1 (7 steps, fade +1, land 2995,9835 +4, lift +7, close +10) | 4494 [2875]; 3224 [7484] | NEW `~keldagrim_cart_to_icemountain` (was a 2-tick teleport); wire-diffed against 4494: equal but Jagex's sevenfold fade and `busy` a tick earlier |
| Keldagrim -> WWM, track 4 / WWM -> Keldagrim | 4637 [1485], [1979] | `~forget_ride_to_wwm` / `_from_wwm`; return seated a tick after the move (was the same tick) |
| the secret cart, the 20 tunnel routes | 4646 | `~forget_secret_cart`, `forget_tracks.rs2` |

What every ride does that ours did not:

- **The weapon, shield and boots come off** for the ride into `minecart_temp_inv` (the cache's,
  size 3) and go back on with the landing: 3224 [7499] empties worn slots 3, 5 and 10; 4494
  [2770], [2820], [2893] and 4637 [418], [1319], [1508], [1986] the weapon, the only one worn,
  back at [2786], [2838], [2907], [435], [1336], [1527], [2004]. `~keldagrim_cart_seat` /
  `~keldagrim_cart_unseat` on every cart, the tunnels' included (inferred there: 4646 wore none
  of the three). A logout on a cart gives them back (`~keldagrim_cart_login`, unrecorded).
- **The station's carts.** Jagex adds 23 `keldagrim_train_cart` locs to the station's queues
  (4646 [5296], the same set in every session): tracks 1 and 3 from 2911 / 2909 to the front at
  2923, track 4 from 2911 to 2919, the spur's two at 2917 and 2919. We placed four.
- **No cart model rides along.** Nothing on the wire draws one: the board deletes the station
  cart, the appearance carries only the `human_minecart_anim` stance (seq 2148, framemap 0, the
  human skeleton) with `lefthand=hide`, `righthand=hide`, and no spotanim, npc, loc or obj
  follows the player; the deob has no special case for the seq. What a Jagex client shows on a
  ride is the seated player over the rails.

The pub scene's props: D+8 [5143] puts `forget_cutscene_beer` in the right hand and
`forget_cutscene_chair` in the left (real worn-inventory writes), D+160 [5295] empties both. Ours
sent neither; now it does, with what the player held there kept in `minecart_temp_inv`
(inferred: the recorder held nothing).

## Open

- The old dwarf at White Wolf Mountain ("Hello there youngster.", 4637 [1690]) is Recipe for
  Disaster's `hundred_dwarf_dad`; ours greets by that quest's state and the recorder's state
  (varp 6946, not transmitted) is unknown, so which line belongs to which state is unsettled.
- Hervi's "Show me the gold!" sum (never chosen): the bars' value; his task is the recorded
  one (3 silver bars, 18 minutes) where Jagex may roll it.
- The cart stash on the tunnel carts and the pub's holding inventory are inferred (no recorder
  wore anything in those slots there); a shield or boots on a Keldagrim cart is recorded only
  in 3224 (rev 2xx).
- Ice Mountain / White Wolf Mountain fares: half after The Giant Dwarf, inferred from two
  sessions (150 in 4494, 75 and 50 in 4637).
- Unrecorded branches written from the wiki transcript (moods unverified): Veldaban's two "No."
  answers and his post-quest talk, Khorvak's drink branch, Rowdy's other 23 items' phrasing,
  Gauss's repeat and no-glass lines, the Drunken Dwarf's lost seeds / no beer / no stout lines,
  the barmaid's "already got one", Rind at stage 40, the story rooms' early-exit lines.
- Journal stages 20, 65, 105..119 and 140 were never opened in a recording.
- Leaving a story room the way you came, or with "No." to moving on, rebuilds the tunnels
  under a fade (`~forget_room_back_to_tunnels`): unrecorded, every recorded exit went on.
- npc_block_inventory of the scenes' npcs on our tick log: not yet run (the wire audit below
  covers interfaces, vars, camera, sounds, the player's moves and facing, not npc info).
