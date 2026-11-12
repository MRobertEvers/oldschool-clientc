# The Tourist Trap from RSProx 4531 (rev 242, 2026-11-11)

Source: RSProx index 4531, world 370, recorded 2026-11-11. It is a full playthrough, from
the Irena start confirm to the scroll ([1849]..[12308]), with Varrock, Al Kharid and
Bedabin errands, a Postie Pete random event and a magic carpet ride. `[n]` is a tick in
that session. Transcripts: `rsprox_index.py get 4531` (also `--bas`).
The wiki transcript compared against it is in
[tourist_trap_wiki_drafts.md](tourist_trap_wiki_drafts.md).

The port this rebuilds was LostCity's (2004) on the OSRS rework's records. What it had
wrong, by area:
- **Irena:** the start had LostCity's three-question menus instead of the confirm.
- **Captain:** his overheads, his insult ladder, and the desert dump.
- **The cell:** escaping it was a Strength roll with a window search and a "rocky
  escarpment" menu, not the rework's bars, scramble and cliff.
- **Camp and mine:** the gate had no walk-through leaves; the guards' arrests were
  overheads where Jagex sends timed pages.
- **Siad:** "Prepare to die" and "take your plans" never called his guards.
- **The cart rides and the barrel:** instant teleports instead of scenes in a copy.
- **Ana:** her later talks ("Hello again!", three questions) and the barrel talk were
  missing.
- **Missing pieces:** stage 2 was never written; the journal was LostCity text; the slaves
  had no chatter.
- **Unbound ops:** the bookshelves in Siad's office, the cactus with the scarf, the cart
  camel, the Ugthanki, the punishment rocks' mined-out form and the Al Kharid window had
  no binding.

## Files

| file | what |
|---|---|
| `quest_desertrescue/scripts/irena.rs2` | start confirm, the four questions, the handover, the reward and completion |
| `ana.rs2` | Ana in the mine (first meeting, "Hello again!", the barrel talk), the squeeze, the drop, the thanks at the pass |
| `mercenary.rs2`, `mercenary_captain.rs2` | the mercenaries' pages, the bet, stage 2, the captain's talk, Attack, the insult ladder, the desert dump, the key box on his death |
| `mining_camp_gate.rs2` | the gate as two walk-through leaves, the search line, the weapon/armour and escaping-slave arrests, the jail break at the gate, the shared search tail |
| `captain_siad.rs2` | Siad's dialogue, both distractions, the guards called, the chest, the desks |
| `mining_slave.rs2`, `camp_guard.rs2` | the slaves (pages, brush-off, chatter), the guards (caught, cave guard, lift guard, punishment cell guard) |
| `al_shabim.rs2`, `bedabin_nomad_guard.rs2` | Al Shabim; the tent guard and curtain |
| `quest_desertrescue.rs2` | footprints, the cell escape, mine doors, mine cave, anvil, feathers, punishment rocks, the mine cart ride, the barrel-on-cart scene, the winch, the flatback cart escape, the cactus, the cart camel, shared page procs |
| `desertrescue_journal.rs2` | the journal, row for row |
| `general_use/scripts/bookcases.rs2` | the Falador-style shelves (were unbound) |
| `areas/area_desert/scripts/bedabin_nomad.rs2` | the nomad's pineapple line; the Ugthanki (was unbound) |
| `areas/area_alkharid/scripts/alkharid_curtains.rs2` | the big window's low-Agility box (was unbound) |
| `_explicit/configs/server_fields.generated.npc` | `timer=5` on the six slave types (their chatter) |
| `quest_desertrescue/configs/desertrescue.loc` | punishment rocks deplete to `punishrocks_no_ore` |
| `test/quests/desertrescue.lua` | the gate, on the recorded flows |

## Stages (varp 197 `desertrescue`)

Our numbers equal Jagex's; stage 2 was missing and is now written.

| value | set on | tick |
|---|---|---|
| 1 | Irena's fourth page after "Yes.", with "You've started a new quest" | [1904] |
| 2 | asking a mercenary "I'm looking for a woman called Ana" | [1994] |
| 3 | the captain's opening menu, after "You approach the Mercenary Captain." | [2044] |
| 4 | the captain's death tick, with the key and its box | [3397] |
| 5 | through the gate with the key | [3441] |
| 6 | the slave's "Really, that would be great..." | [4931] |
| 7 | the lock picked | [4970] |
| 8 | the clothes traded | [4994] |
| 9 | first time through the mine doors, with the box | [5048] |
| 10 | the cave guard's "I'm sure you get my meaning..." | [5345] |
| 11 | Al Shabim's key box | [5843] |
| 12 | the chest box's click, with the plans | [6789] |
| 13 | Al Shabim's last page about the anvil | [7328] |
| 14 | the dart tip box's click | [7488] |
| 15 | three ticks after the dart's box | [7560] |
| 16 | the "Dart Construction" box | [7631] |
| 17 | the cave guard's "Great! Just what I've been looking for!" | [7951] |
| 18 | the cart ride's click | [9256] |
| 19 | Ana into the barrel | [9843] |
| 20 | 44 ticks into the barrel-on-cart scene | [10342] |
| 21 | Ana's barrel picked up in the loading room | [10449] |
| 22 | the lift guard's last page | [10560] |
| 23 | the winch's barrel box | [10718] |
| 24 | Ana's barrel picked up beside the winch | [10743] |
| 25 | Ana's barrel on the flatback cart | [10782] |
| 26 | the cart ride out | [11364] |
| 27 | Ana's key box's click, with Irena turned happy | [12279] |
| 28, 29 | each skill picked | [12296], [12302] |
| 30 | two ticks after "Okay, that's all the skills I can teach you!", the scroll | [12308] |

The quest's own varbits are all transmitted: `cell_wall` (2801), `picklock_attempts`
(2802, not written by us), `mineslave` (2803), `bronze_used` (2804, now counts anvil
failures), `ana_state` (2805), `cart_attempts` (2806, not written by us), `irena_state`
(2807), `ana_winchside_barrel` (2808), `flatback_cart_ana` (2809), `ana_visible_shanty_pass`
(3733). Server-side bits stay on varp 6656; a new bit 17 (`met_ana`) selects Ana's
"Hello again!".

## Pages

790 distinct recorded pages and menu rows. 715 are verbatim in the scripts with their
recorded mood (the checker joins `<br>` to spaces; coloured asides are written with
Jagex's own `<br>`s). Of the rest:
- 73 are checker artefacts: soft-wrapped boxes it does not match, and pages sent
  through procs it does not parse.
- 7 are not literal text in the scripts:
  - the bet's amounts and the payout, built with `tostring`;
  - Postie Pete's random-event line;
  - the carpet menu;
  - the generic pickaxe and Agility-window boxes.
- 1 mood row is the escaping slave's page, drawn by a named-head proc.

## Interaction shapes (each from the recording; ours built to it)

| interaction | recorded | ticks |
|---|---|---|
| bend the bars | L+1 face; L+2 `tourtrap_qip_bend_bars` delay 10 + `tt_bend_bars` delay 10; +4 the line and `cell_wall` 1 (2/2, no roll) | [3469..3477], [6857] |
| squeeze out | the line, `escape_through_bars` delay 10, `tt_squeeze_out`, exactmove one tile west 50..93 | [3483], [6868] |
| the rock | L+1 face; L+2 exactmove three tiles west 61..253, `scrabble_success` or `_fail_part1`, `climbing_loop` x8; a slip scrapes for 1, 12 ticks on | [3489..3493], [6880..6892] |
| cliff up | face; +1 `climb_cliff_fail_pt1`; +12 a tile west; +13 a 0..1 hit | [3516..3529], [6895..6908] |
| cliff down | L+3 face west; L+4 `cliff_drop_pt1`; L+8 a tile west; walk to 3269,3039 | [3533..3544], [6912..6923] |
| camp gate in | L+1 the search line, the step, both leaves as inviswall + `inacmetalgateopenl/r` for 2 ticks, `iron_door_open` | [3440..3443] |
| armed/armoured | +2 overhead + echo, +3 overhead, +3 echo + overhead, +3 echo + the search | [3443..3452], [5977..5986] |
| the search | the line; +4 cell line; +2 "Into the cell" overhead; +2 the move | 15 searches |
| the desert dump | pages with no continue every 4 ticks, the fade out, the move, the get-up seq, water taken, fade in, the guard's page, lines + box | [1747..1768], [2729..2759] |
| mine doors | the lines and the move on the op; the inner double door as walk-through leaves a tick later, `bigdoor_open` | [5043..5048], [5475..5480] |
| mine cave | L+1 search line, the line, fade out, `tt_down_stairs`; +3 move, fade in, `tt_up_stairs`; +3 the last line | [7957..7964], [10587..10595] |
| anvil | smithing seq; +3 box without continue; +9 the outcome; failures keep the bar (2/2, the third forged) | [7419..7488] |
| feathers | box, "tricky" box, then nothing (fail) or the dart's box | [7528..7557] |
| cart ride | the climb-in seq; C+2 into a copy, carts removed, the player drawn as a cart; C+7 fade in; C+8 `tt_cart_loop` x21 and the ride along the rails; C+34 fade; C+37 out, the box | [9256..9293], [9304..9341] |
| barrel on the cart | C+3 a copy with a cart npc; C+10 loaded; hops 2 tiles a tick to the loading room and back empty; C+44 the stage and out | [10298..10345] |
| winch | the line, `human_winching`, the winch's loc anim, `tt_winching_loop` x5; +12 the outcome | [5520..5536], [10705..10720] |
| escape cart | the box; on its click the climb seq and fade out; +3 a box without continue; +3 stage, move, barrel, `tt_cart_loop` x20, fade in | [11357..11366] |
| the end | barrel shown; a tick on Ana out and her thanks; the key box's click: stage 27, Irena happy; +2 Ana gone; +3 "Hi Ana!"; +4 Irena's pages; the scroll 2 ticks after the last page | [12252..12308] |

## Journal (`desertrescue_journal.rs2`)

The journal was opened 39 times in the recording, giving 14 distinct row sets at stages
3, 5, 10..19 and 26 (stage 26 twice: before and after Ana is let out at the pass).

Our rows now match all of them. They use the older format: a blank first row, blank rows
between paragraphs, and every finished row `<str>`. Jagex's own typos are kept: "whenn",
" Hammer", "prototype dart<col=000080> ,". The title is "The Tourist Trap" (ours said
"Tourist Trap").

UNVERIFIED, keeping LostCity's text in the recorded format:
- stages 1, 2, 4, 6..9, 20..25 and 27..30;
- the rows for missing weapon items and for a lost pineapple.

## Npc info (`npc_block_inventory.py` 4531 against gate run tt4531j)

| npc | kind | recorded | ours | verdict |
|---|---|---|---|---|
| desertminingcaptain | say | 1 | 0 | built (Attack's overhead); the test fights the duel, never Attack |
| capt_siad | say, facing | 8, 23 | 0, 0 | built ("Guards! Guards!", "GUARDS!"); the test distracts him by sailing, so it never reaches them |
| slave types (4) | say | 9 lines, many | 0 before | built: the chatter timer; brush-off overhead fixed |
| ana | say | 1 | 0 | built (the drop); not in the test |
| mining_cart_driver | say | 15 | 1 | "Tut !" added on Attract; "He, he, he, ha!" existed |
| guards, mercs | say, facing, seq | arrests, fights | few | built for every recorded arrest; the test takes no arrest |
| slave_rowdy | fight blocks | 35 seq, 14 hits | 0 | the recorder's fight; not compared |

## Sounds

Built from the recording: `tt_bend_bars`, `tt_squeeze_out`, `climbing_loop`, `iron_door_open`,
`bigdoor_open`, `tt_down_stairs`/`tt_up_stairs`, `tt_cart_loop` (x21 delay 30; x20),
`tt_winching_loop`, `curtain_open`.

## Action inventory (`rsprox_actions.py`, 379 rows), verdicts by group

| rows | verdict |
|---|---|
| Irena, mercenaries, captain (talk/Attack/Watch), Siad, slaves, guards, Al Shabim, Bedabin nomad/guard/fighter, mine cart driver, Ana (all forms), Irena happy | this content: rebuilt above |
| footprints (all forms, both ops), cactus scarf, camp gate, cell door, cell bars, climb rock, cliffs, ladders, desks, bookcase (Siad), chest, curtain, flatback cart, crate, camel, mine doors, mine caves, winch, winch bucket, barrels, mine carts, punishment rocks (+ no ore), cell gates, wrought gates, anvil, use-ons (bar, barrel, plans, pineapple, feathers) | this content: rebuilt, or checked against ours |
| fai_falador bookshelves, Ugthanki, kharid big window | generic, were unbound: bound |
| tablets, glory rubs (submenu and worn), bank booths/chests/deposits, Shantay (talk, buy-pass, shop), the doorway, waterskins, stamina/prayer/attack potions, sharks, prayers, equips, drops and pickups, coal/tin/copper rocks, magic carpet, home/minigame teleports, gemstone crabs | generic, exercised by the session: Shantay and the doorway are The Feud's rebuild (the test was updated to them); the rest unchanged |
| Postie Pete (Evil Twin random) | generic random event: "Tell her to get stuffed." mood fixed |
| quest list / journal / overview / quest rating / logout | engine and interface |

Teleports: every one (cell, mine, carts, dumps) is covered above; the rest are the
recorder's own tablets, glory and carpet.

## Gate test

`run.py desertrescue`: 204/204 PASS (run tt4531j). The test was updated for:
- Irena's confirm, Al Kharid without the hyphen, "Yes. I'd like to try.";
- the longer cart rides and the barrel scene;
- the handover order at the end;
- Shantay's third page and the desert warning interface (The Feud's rebuild).

## Open

- The test's barrel search after the return ride must name the barrel tile (3303,9415).
  The driver's nearest-barrel pick sends no packet after leaving a copy of the same square.
  That is a client-side stale-scene question, not content.
- The gate jail break's desert dump draws the gate guard's head on "Guards!! Guards!".
  Jagex's draws Ana's ([11440]).
- Not written by us: `picklock_attempts` (2802) and `cart_attempts` (2806). The anvil's
  guaranteed third try is inferred from 2/2.
- The cliff-up hit (0..1) and the rock's slip odds are LostCity's roll; there are only two
  samples each.
- The guards' walk toward the player before an arrest is not scripted.
- `buff_bar_desert_heat_timer_iteration` (varp 3198) is recorded and never sent. It is
  generic desert heat.
- Not committed: the changes are in the working tree only.
