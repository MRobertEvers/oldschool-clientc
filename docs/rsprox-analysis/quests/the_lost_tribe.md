# The Lost Tribe from RSProx index 4454

**Recordings.**
- RSProx index 4454 (rev 241, w495, 2026-11-02, "The Lost Tribe") is the whole quest, start to scroll, in 3,971 ticks.
- Index 3165 (rev 232) walks the maze and reads the south marker.
- Index 3259 (rev 234) shows the brooch spawn and Sigmund in passing.
- Index 4403 is a second Goblin Village soldier talk.

Every cite below is a 4454 tick unless it names another session.

**What our tree had.** A 2009scape/wiki port: §16 of the `rsprox-content` skill applied.
- Bob was the fixed witness.
- Stages 2 and 3 never existed.
- Most lines were invented: "Have you found out what it was?", "You find a small key on Sigmund.", the Duke's "You appear to have misplaced the peace treaty".
- The signing was "soft-skipped" into plain dialogue.
- The maze trap sent the player to an invented tile.
- The darkness never reached the client.
- The journal was a stub.

**Files** (OSRS-Content `osrs-content/server/scripts/`):
- `quests/quest_losttribe/scripts/`: all seven files rebuilt.
  - `losttribe.rs2`: Sigmund, the start and the roll, the witness, the Duke's row, the dig, the hole, the brooch, Reldo, the book.
  - `losttribe_tunnels.rs2`: the dark, the traps, the markers, hole 2, the cave goblins, Nardok.
  - `losttribe_mistag.rs2`: Mistag, the emotes, the guided walks, Kazgar.
  - `losttribe_generals.rs2`: the generals.
  - `losttribe_ham.rs2`: the key, the chest, the trapdoor, the crate.
  - `losttribe_finish.rs2`: the signing.
  - `losttribe_journal.rs2`: the journal.
- `quests/quest_losttribe/configs/`: `losttribe.constant` (Jagex's stages), `losttribe.varp` (+ `varp425_goblin_caves` transmitted), `losttribe.spawn` (the brooch).
- The Lost Tribe's hooks in shared npcs:
  - `areas/lumbridge/scripts/{duke_horacio,hans,citizens,father_aereck,bob}.rs2`
  - `quests/quest_cook/scripts/quest_cook.rs2`
  - `areas/varrock/scripts/reldo.rs2`
  - `interface_emote/scripts/emote.rs2`
  - `quests/quest_deathtothedorgeshuun/scripts/dttd_savezanik.rs2` (hole 2's undug mesbox)
- Generic content the session exercised:
  - `areas/lumbridge/scripts/donie.rs2` (new): Donie had no Talk-to.
  - `areas/lumbridge/scripts/sheared_ram_bartender.rs2` (new): the bartender had no Talk-to.
  - `ladders_stairs/scripts/ladders.rs2`: a middle staircase's Climb now asks which way.
  - `areas/falador/scripts/goblin_village/goblin_soldiers.rs2`: 4454's answer to "What is the goblin new century?".
  - `interface_emote/configs/emote.constant`: emote delay 0.
  - `shop/dttd/scripts/nardoks_bone_weapons.rs2` and `tools/gen_shop_scripts.py`: Nardok's shop is quest-gated.
- `_explicit/configs/server_fields.generated.varp`: `varp425_goblin_caves`'s scope, transmit and protect lines deleted; they are stated in `losttribe.varp`.

**Test.** `test/quests/losttribe.lua` follows the recorded flow and pins the recorded witness with `::losttribewitness 2`.
- Scriptrun (private packs, `src/build_lt_opt/torirsserver`): **203 of 204** rows pass. The one failure is `quest.journal`, which no scriptrun can pass because it has no quest list.
- The neighbouring tests fail exactly as they do on a HEAD baseline pack: `priest` 70/71, `gobdip` 107/110, `rfd_start` 18/20. Their failures are driver rows, not content.

## 1. Every action in the recording

| tick | action | verdict |
|---|---|---|
| 6, 27.., 50, 321.. (x19), 3949 | welcome screen, quest list, overview, journal close, scroll close | engine/UI; the journal is §8 |
| 60.., 176.. (x7), 1452.. | Varrock, Lumbridge and Falador tablets | generic, `teleport_tablet.rs2` (recorded shape, earlier ports); Lumbridge lands in its 3x3 here too |
| 80..167, 248..259 | bank booths, bank tabs, withdraws; GE clerk and offers | generic, the recorder's own kit |
| 193.., 197, 199, 245, 270 | Lumbridge castle spiral stairs: Climb-up, Climb, Climb-down | generic. **Fixed**: Climb on a middle floor opens "Climb up or down the stairs?" ([198]; also index 4411 [1076]) |
| 202.., 220.. | kitchen trapdoor, cellar ladder | generic maplinks; landings 3210,9616 / 3209,3217 match |
| 210, 332, 412 | sacks, crate, crate2_old | generic searches |
| 243.., 532, 571.., 586.., 1514, 1543, 1859.. | doors | generic door procs (unchanged) |
| 282.. (x12) | Duke Horacio | **this content** (§3). **Fixed**: his Rune Mysteries answer is [287]'s |
| 292.., 3345 | Sigmund Talk-to, Pickpocket | **this content** (§2, §6) |
| 322, 326 | chests in the castle | generic |
| 345, 358, 502, 524, 540 | men and women of Lumbridge | **this content**: the cellar row (§2) |
| 403, 772 | pickaxe on the cellar rubble | **this content** (§4) |
| 428.. (x9) | the Cook | **this content**: the witness (§2). His Recipe for Disaster "dining room" talk ([753], [1516..1527], [3295..3299]) is **Open** |
| 467, 610 | Donie | **added**: no binding before; the recorded rows (§2) |
| 483 | Hans | **this content** (§2) |
| 515 | Hatius Cosaintus | **Open**: no binding in our tree |
| 547 | Father Aereck | **this content** (§2) |
| 574 | the Sheared Ram's bartender | **added**: no binding before |
| 597, 620 | Lumbridge general store keeper and assistant | generic (`tutors.rs2`, unchanged) |
| 642.., 1049, 1275, 2333 | stamina potions | the recorder's kit |
| 783.., 2986.. | the hole, both sides | **this content** (§4) |
| 798.. (x4), 1326, 1331 | brooch take, drop, retake | **this content** (§4); drop and take are generic |
| 804.. | brooch Look-at (op 2) | **this content**: `brooch_closeup` |
| 815 | run button | engine |
| 818..1046 | wall markers | **this content** (§5) |
| 1073.., 3173, 3846.. | Mistag | **this content** (§5, §7) |
| 1089..1181, 2877, 3168 | cave goblin guards, miners; Nardok | **this content** (§5) |
| 1307..1360 | Varrock palace door, Reldo | **this content** (§4) |
| 1366, 1385 | library bookcases; the quest's bookcase | generic / **this content** |
| 1401..1471 | the goblin symbol book | **this content** (§4) |
| 1499..1511 | willow roots taken and dropped, the cook's range | generic |
| 1541 | man Attack | engine combat |
| 1866..1891 | Goblin Village doors, a soldier, the generals | **this content** (§4). **Fixed**: the soldier's new-century answer ([1873]) |
| 2513.. | tinderbox on the unlit black candle | generic (`thrantax_altar.rs2`, recorded silent relight) |
| 2526..2561 | swamp-cave goblins | **this content** (§5) |
| 2882.. | emotes Goblin Bow and Salute | **this content** (§5). **Fixed**: emote delay 0 |
| 2964, 2970 | hole 2: search, pickaxe | **this content** (§5) |
| 3009 | rat Attack | engine combat |
| 3149, 3824 | Kazgar | **this content** (§7) |
| 3357..3371 | Sigmund's chest, the key dropped and retaken | **this content** (§6) |
| 3483, 3495 | spiders Attack | engine combat |
| 3500, 3516 | the H.A.M. trapdoor: Pick-Lock, Climb-down | **this content** (§6) |
| 3524 | a H.A.M. member | **Open**: talk unbound in our tree (menu at [3525]) |
| 3584.., 3594, 3598 | the crate; silverware dropped and retaken | **this content** (§6) |
| 3707 | silverware on Sigmund | "Nothing interesting happens." (engine default; matches) |
| 3747.., 3749 | treaty on treaty; treaty dropped | engine default / generic drop |
| 3971 | logout | engine |

**Teleports** (`rsprox_actions.py`): every one is a tablet, a stair or ladder, the hole, Mistag's or Kazgar's walk (§5, §7), the river (§5), or the signing's copy and back (§9).

## 2. The start and the witness

- Sigmund's "Do you have any quests for me?" leads to two pages, then "Start The Lost Tribe quest?" ([305..308]).
- On "Yes." the player says "So what do you need me to do?" ([310]).
- On that page's click, everything goes out together with Sigmund's page ([311]): stage 1, `lost_tribe_contact` 0 -> 2, and "You've started a new quest".
- **`lost_tribe_contact` is the witness, a roll.** The wiki names four candidates (the Cook, Hans, Father Aereck, Bob). Value 2 is the Cook, the recorded one ([429..448]).
  - The other three values are an unverified order, written in `losttribe.constant`.
  - Hans and Father Aereck are recorded as non-witnesses ([494], [549]).
  - Each npc gives the witness pages only when rolled. The witness's stage 2 goes out with "Thank you, `<name>`!..." ([448]).
- The question shows at stages 1 and 2 (Donie still asks at [611]).
  - Men and women: no greeting; their menu opens straight on "Hello, how's it going?|Do you know what happened in the cellar?" ([360]). Their answer is one of three (two recorded, the third RS3 wiki).
  - Hans: the cellar row takes his first row's place ([487]).
  - Father Aereck: after The Restless Ghost the question follows his thanks with no menu ([547]).
  - The Cook: a two-row menu ([429]).
  - Donie: her row is fifth ([473]).

## 3. The Duke and Sigmund, stage by stage

| stage | Duke's row (R) | Sigmund's row (R) | write |
|---|---|---|---|
| 1 | wiki "What happened in the cellar?" | "What was I doing again?" [341] | |
| 2 | "The cook says he saw something in the cellar" [695] (no stop) | "...in the cellar." [676] | 3 on the closing click [723] |
| 3 | wiki "What was I doing again?" | wiki | |
| 4 | "I dug through the rubble..." [1225]: the brooch scene | same [1215] | 5 with the last page [1250] |
| 5 | none recorded or documented | stage 4's (unverified) | |
| 6 | "I found out about the symbol..." [1563] | same [1551] | |
| 7 | wiki "...goblin village..." | wiki | |
| 8 | "I've made contact with the cave goblins..." [3062] | same [3047] | 9 with "Unless it is returned" [3088] |
| 9 | "What was I doing again?" [3096]; ham 1: "Did you know Sigmund is a member of HAM?" [3395]; silverware: "I found the missing silverware..." [3685] | "Tell me about the silverware..." [3322] | ham 3 with the Duke's page [3398]; 10 with "I see I was ill-advised." [3725] |
| 10 | "What was I doing again?" [3752]; without the treaty, a copy on the last click and `sigmund_leaving` 0 [3759] | absent (multinpc -1) | |

- **The dismissal [3725].** On the click that opens "I see I was ill-advised.":
  - stage 10 and `sigmund_leaving` 1;
  - every silverware deleted (two in the pack, slots 17 and 18);
  - Sigmund replaced by `lost_tribe_sigmund_leaving` on his tile, which walks to the stairs and is gone 11 ticks on ([3736]).
- The treaty goes into the pack on the objbox's click ([3742]).

## 4. The cellar, the brooch, Reldo, the book, the generals

| interaction | R | ours |
|---|---|---|
| pickaxe before permission | L+1 the mesbox "You should find out what happened here before you go vandalizing the castle any further." [405] | same |
| the dig | L+1 engine face; L+2 face east; L+3 `human_mining_iron_pickaxe_wall`; L+6 the stage, seq -1, the objbox with the pickaxe [772..778] | same; per-pickaxe wall seq |
| squeeze | O the line and a face to 3220,9618; O+1 `human_pipesqueeze`; O+2 the move and `human_pipeunsqueeze` [783..785], both sides [2989..2991] | same (scriptrun 219/220) |
| brooch | a public spawn re-sent with every zone [209]; a take leaves it ([799], [801]: two taken) | `losttribe.spawn`; `[opobj3]` adds without removing. The pile's del/add re-send is not sent (Open) |
| Reldo | brooch row first at stage 5 with it in the pack [1318]; without it, absent [1329]; repeatable [1354] | same |
| bookcase | the player's page, then the book and the mesbox on its click [1388..1390] | same |
| the book | opens at the bookmark; each turn writes the bookmark, hides six spreads, shows both arrow layers and the spread, then hides the arrow layer that has nowhere to go. The stage goes out on the turn onto the symbols [1430]; `com_48` (the Dorgeshuun) closes the book on the player's line [1433..1434] | same |
| generals | every menu walked [1891..2284]; the stage on the click closing "That good idea. Watch." [2076]; T+1 Bentnoze `surface_goblin_update_dance` with the player's dance at delay 45; T+5 Wartface's bow with the player's; T+9 the mesbox [2085] | same (ours 468/472 four apart) |

## 5. The tunnels, the dark, the traps, the cave goblins

- **The dark.** `darkness_level` sits on `varp425_goblin_caves`, generated `transmit=no`: it never reached our client (**fixed**).
  - With a lit black candle: 2 with `darkness_medium` [786].
  - Without a light: 3 with `darkness_dark`, `minimap_state` 2 and `minimap_toggle` 2 [2506].
  - Back to 0 with the overlay closed when the player leaves [2992].
  - Each change goes out a tick after the move.
  - A fade takes the overlay slot, and the dark is re-opened 2 ticks after the fade back ([2955], [3163]).
  - Ours: a soft timer armed by the four squares. Soft because the river's queue holds the player and the dark still has to go out on its tick.
- **Floor traps** are `lost_tribe_trap_floor` wall decor on ten tiles (`maps/m50_150`, `m51_150`). Both recorded steps onto one fell (2/2: 3302,9618 [2499], 3239,9622 [2664]). The fall, tick by tick:

  | tick | recorded |
  |---|---|
  | T (the tick after the step) | "The floor collapses beneath you!", `desert_comedy_fall`, `lost_tribe_collapsing_floor` and its `collapsing_floor` anim, `pitfall2` |
  | T+4 | `comedy_smoke_fall` delay 10 on the tile; fade to black |
  | T+6 | "You are swept into an underground river.", the move to 3231,9579, open flames out, the fade overlay closed, `sizzle` |
  | T+7 | `watersplash`, `splash_and_river` |
  | T+9 | exactmove to 3230,9582 (0, 90, north) |

  - Ours matches on every row (scriptrun ticks 244/248/250/251/253).
  - The floor's loc is placed from the timer, so the tile cannot fire again on the ticks before the river.
  - The floor's revert is unrecorded (100 ticks, unverified).
- **Ceiling traps:** no session steps under one. Ours is 2009scape's (`mm_roofcollapse`, 1..7 damage, no line), unverified.
- **Markers:** `cave_goblin_markers` (62) with the symbol model set before the open. North 6923, east 6922, west 6927 (4454); south 6924 (index 3165 [2877]).
- **Hole 2** (3224,9601..9603):
  - Undug Search: the mesbox "The tunnel is blocked with rocks." [2965].
  - Pickaxe from standing: seq on O, then on O+3 `hole_2_dug`, seq -1 and the objbox [2970..2973].
  - The squeeze once dug is DTTD's (transports.tsv).
- **Cave goblins before contact (stage 7):**
  - guards: "Surface-dweller! You should leave the mines now!";
  - miners: "Help! An outsider!";
  - swamp-cave goblins: their line plus "The cave goblins are too scared to talk to you." [2534..2561];
  - Nardok, Talk-to and Trade alike: "You ain't getting nuthin' off me, surface-dweller!" [1093], [1097]. Our shop opened for anyone: **fixed** through the shop generator's quest-gated override.
  - After contact the guard says "Have a nice day!" [3168]. Other post-contact lines are unrecorded (Open).

## 6. The silverware

| interaction | R | ours |
|---|---|---|
| pickpocket | on the op `human_pickpocket`, the key, objbox "You steal a little key." [3345] | same |
| chest without key | mesbox [3362] | same |
| chest with key | on L+1 `ham` 1, the key replaced by the shirt, then robe and hood; `opendungeonchest` for 4 ticks, `chest_open`, objbox; the player's page on the click [3375..3379] | same (999 -> 1003) |
| trapdoor Pick-Lock | a try every 7 ticks: the line, `human_openchest`, `pick`; failing, a tick later `human_lockedchest`, `locked`; the third try gives: both lines, `ham_thief` 1, `locked`, and a tick later `chest_open` [3500..3515] | same. The odds (1 in 3) are one session's, unverified |
| Climb-down | on the op the line, `ham_thief` 0, the move to 3149,9652, `trapdoor_open`; a tick later "... and enter a dimly lit cavern area." [3516..3517] | same |
| crate | on L+1 the silverware and objbox, as often as the pack has none [3587], [3595] | same |

## 7. Mistag and Kazgar

- **Before contact.** Talk-to gets two pages [1077..1079].
- **The goblin bow at stage 7** (Mistag 3 tiles off) answers on the emote's own tick ([2882]):
  - the greeting's chathead is `lost_tribe_mistag_1op`;
  - Mistag's `dttd_zanik_bow` goes out with the third page [2886];
  - stage 8 goes out on the click that opens "We blocked up our tunnel..." [2899].
- **Other emotes.** The salute: "Eeek! The war-dance!" [2908]. From stage 8 the bow is returned, at delay 15 with a face and nothing said [2912]. Our reach is 8 tiles, because Mistag wanders (unverified).
- **Menus.** Stage 8 has four rows; the two stories return to it ([2919..2946]). Stage 9 has three rows; the first sets `mistag_denial` with "Oh no!" [3184] and returns, the second ends [3191..3200].
- **The treaty** has no menu ([3846..3851]).
- **The guided walks** (Mistag out, Kazgar in):
  - on the click: the spam line, a fade to 150, the chat closed;
  - T+3: the move;
  - T+4: the fade back;
  - T+6: the dark again ([2949..2955], [3157..3163]).
- **Landings:** 3235,9610 and 3309,9613 (scriptrun matches both).

## 8. The journal

- Opened at every stage 1..10. The rows were copied verbatim, `<str>` paragraphs and `<col>` tags included: **98 of 98 recorded rows** are in `losttribe_journal.rs2` (mechanical check, the Cook substituted).
- Stage 9's conditional rows are as recorded:
  - the blank row and Mistag's denial with `mistag_denial` [3364];
  - the H.A.M. rows at ham 3 [3425];
  - the last rows by whether the silverware is carried [3701].
- **Unverified:**
  - stage 9 at ham 1;
  - the three unrecorded witnesses' rows (the Cook's with the name changed, so their wraps are unverified);
  - the completed journal (never opened).

## 9. The signing (cutscene)

Recorded [3853..3949], in a copy of Lumbridge Castle's ground floor (template 3207,3217 -> raw 10311,9489). Ours uses `general/scripts/cutscene_frame.rs2` with fov_clamp 1.

| tick | recorded | ours |
|---|---|---|
| T [3853] | fade to 150, chat closed | same |
| T+2 | lock, copy, cast (Duke 6,23 S; Ur-tag 7,16 S; Sigmund 13,22 SW facing 8,12; Mistag 7,15 S), hud, title mesbox with no continue, song 410 | same (scriptrun 1291) |
| T+3 | camera 8,26/300 -> 6,22/100 at 232; fade in | same |
| T+6 | the dining door opened (`nicedoor_open`), the player walked to 7,22; T+7 Ur-tag and Mistag walk | same (1298) |
| T+12 | bow, camera, the player's page | same (1304) |
| clicks | each camera re-aim, npc bow / `emote_no` / `emote_laugh`, face and walk on its recorded click | same |
| E (4 after the last click) | unlock, stage 11, the ring, music 147, fade | same; **xp moved to E+6** (below) |
| E+3 / E+4 / E+6 | out to 3207,3217 / fade in / scroll (brooch at 78,128; "3,000 Mining XP\|A Ring of Life\|Dorgeshuun Mine Access") | same |

- **Region music held.** The copy's square started Lumbridge's region song over song 410 on the first page. Region music is now held (`midi_hold`) for the scene, and left to run out so the scroll's tick sends no region track.
- **XP after the scroll.** Jagex sends the xp at E. Ours sends it after queueing the scroll, so a level-up waits behind the scroll as it does on the recording ([3949]). With the xp at E, the level-up's queue ran first and took the screen.

## Open

- The Cook's Recipe for Disaster "dining room" recollection ([753], [1516..1527]; `hundred_timebubble_exposition` [1525]) and its four-row menu: no implementation in our tree.
- Hatius Cosaintus [515] and the H.A.M. hideout member [3524..3525] have no Talk-to.
- Donie's own talk away from the quest is unported (only the recorded rows exist).
- The brooch take does not re-send the pile ([799]: `obj_del` + `obj_enabled_ops` + `obj_add`).
- Four page rows wrap after a hyphen at Jagex ("well-<br>crafted", "north-<br>west", "War-<br>God", "above-<br>people"); our layout wraps at spaces.
- The dark's close on a tablet out goes one tick late (the timer's tick; Jagex sends it with the teleport [1190]).
- `test/quests/_journals.lua` has no Lost Tribe rows (the live-client journal check).
- Post-contact cave goblin and miner talk; Nardok's Talk-to after the quest.
- Unverified, from the wiki: the witness order for Bob, Hans and Father Aereck; stage 1/3/7 Duke and Sigmund rows; Mistag's mine and god rows; Kazgar's goodbye; hole 2's far side (DTTD's); the ceiling trap; the trapdoor's odds; Mistag's emote reach; the floor's revert time.
- The Goblin Village soldiers fight each other in the background of [1880..2090] (npc-vs-npc swings and splats); not looked at here.
