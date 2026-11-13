# Monkey Madness I against RSProx index 4562..4565

Four sessions of one playthrough by the owner (rev 242.1, world 386, 2026-11-12/13, normal
world): 4562 "Start to Finding Dentures and mould" (9,586 ticks), 4563 "Up to GreeGree"
(5,830), 4564 "before boss" (3,316+), 4565 "Boss" (2,654). `[n]` is a tick in the named
session. Our side before this pass was a LostCity port (`quest_mm/`, 6,430 lines, 56 files
citing LostCity).

The Jungle Demon fight has its own document, `monkey_madness_i_demon.md` (a separate boss
audit, still running when this was written).

**What this pass is and is not.** It rebuilt the cutscenes, the chapter cards and every
fade-and-move frame from their packets, corrected every dialogue page that differed, and
fixed the journal's text. It did NOT run the wire audit: no section of our tick log has been
laid against the recording, the npc-info block inventory, the movement audit and the
ambience count were not run. Those are listed under Open; nothing below should be read as
"audited on our wire" unless it says so.

Tools used: `tools/rsprox/rsprox_actions.py`, `rsprox_pages.py`, `rsprox_cutscene.py`; three
scratch scripts kept beside this file's history only as results (a page checker over all
four sessions against every `.rs2` literal and against the wiki, a fade tabulator, a journal
extractor).

## Stages and variables

Jagex's stage numbers are ours (the LostCity names fit): `mm_main` (varp 365) 0..10 and the
varbits `mm_narnode` 121, `mm_caranock` 122, `mm_daero` 123, `mm_waydar` 124, `mm_lumdo` 125,
`mm_garkor` 126, `mm_zooknock` 127, `mm_lumo` 129. Every write in the recording:

| var | write | tick | goes out with | ours |
|---|---|---|---|---|
| mm_narnode 0→1, 1→2 | 4562 [83], [131] | the worried pages | matched (unchanged) |
| mm_main 0→1 | 4562 [203] | the "Yes." answer's page, with "You've started a new quest: @q_mes@Monkey Madness I" | **fixed**: the menu was "Start Monkey Madness?" / "Yes" / "No", no line |
| mm_narnode 2→3 | 4562 [205] | the Royal Seal box | matched |
| mm_main 1→2 | 4562 [515] | the shipyard gate | not checked on our wire |
| mm_caranock 0→1→2→3 | 4562 [759], [773], [774] | Caranock's pages | not checked |
| mm_narnode 3→7 | 4562 [1100]..[1134] | Narnode's report pages | not checked |
| mm_daero 0→7 | 4562 [1301]..[2619] | 5→6 goes out with the hangar scene's unlock [2578] | **fixed** for 5→6; others not checked |
| mm_waydar 0→1 | 4562 [2748] | | not checked |
| mm_lumdo 0→1→2 | 4562 [3043], [3084] | | not checked |
| mm_main 2→3, mm_lumdo 2→3 | 4562 [3835] | the move out of the Karamja scene (E+4) | **fixed** (was before the fade back) |
| mm_zooknock 0→5 | 4562 [4272]..[7678] | | not checked |
| mm_lumo 0→1 | 4562 [4898] | | not checked |
| mm_garkor 0→1→2 | 4562 [7147], [7153] | | not checked |
| mm_main 3→4, mm_zooknock 5→6 | 4563 [5163] | the last click of the Caranock/Waydar scene (E) | **fixed** |
| mm_garkor 2→3→4 | 4564 [1150], [1182] | | not checked |
| mm_main 4→5 | 4565 [1605] | the last click of the Awowogei scene (E) | **fixed** |
| mm_garkor 4→5 | 4565 [1614] | the Chapter 4 card (E+9) | **fixed** |
| mm_garkor 5→6, mm_main 5→6, mm_garkor 6→7 | 4565 [1685], [2134], [2164] | the sigil; the kill; Garkor after it | the demon doc |
| mm_main 6→7→8→9 | 4565 [2503], [2508], [2511] | Narnode's report, the reward box, the scroll | not checked on our wire |
| mm_main 9→10 | 4565 [2616] | the training's xp lines | **fixed** (tick) |

The monkey child, Awowogei, Kruk and elder-guard varbits never appear on the wire (their
base varp is not transmitted, or Jagex keeps them elsewhere): our `varbit_1xx` copies of
LostCity's flags are unverified. `tools/rsprox/varp_transmit_audit.py` was not run.

## Cutscenes

Seven scenes and five cards, found with the grep in the skill's step 2b. Before this pass:
cards were pairs of message boxes, the three "meanwhile" scenes ran on the template tiles
with stand-in spawns and `cam_*` rates 100/100, the HUD stayed up ("Known gap" in the
header), and every fade ran T, T+3, T+5, T+8.

### The card: `mm_message` (225)

| card | tick | packets | ours |
|---|---|---|---|
| Chapter 1 | 4562 [213], shut [224] | `midi_song 147 (0,0,0,0)`, jingle `new_chapter_monkey_madness` (136), 13 blank `if_settext`, `mm_ms2` "Monkey Madness: Chapter 1" colour 32767, `mm_ms4..7` colour 30653 with "In which our hero finds himself drawn back into Glough's web" / "of deception and deceit.", `mm_upright` "Close Window", synth `ooks` (1679), `toplevel_mainmodal_open(-1,-1)`, opened in the main modal | **rebuilt** (`~mm_card_chapter`) |
| Chapter 2 | 4562 [3840], shut [3863] | the same; "…engaging in severe quantities" / "of monkey business." | rebuilt |
| Chapter 3 | 4563 [5172], shut [5182] | the same; "…contending with life as a" / "monkey." | rebuilt |
| Chapter 4 | 4565 [1614], shut [1626] | the same; "The Final Battle" | rebuilt |
| "Meanwhile, far away in Karamja..." etc. | 4562 [3689]; 4563 [5051]; 4565 [1492] | `mm_ms5` alone in 32767, no "Close Window", jingle `meanwhile_monkey_madness` (135) | rebuilt (`~mm_card_line`) |
| "Several hours later..." | 4565 [2597], shut by the server [2612] | `mm_ms5` alone, no jingle | rebuilt |

On the player's close of a chapter card the fade overlay is closed and the area's song is
sent ([3863] Island Life with three unlock lines, [5182] Find My Way, [1626] Anywhere).
Ours closes the overlay and releases the music hold (`[if_close,mm_message]`); **open**: the
engine has no "start the area's song now", so the song returns at the next square crossed.
The female wording ("heroine", "herself") is LostCity's and unrecorded.

### The scene frame (three scenes, identical)

| tick | recorded | ours |
|---|---|---|
| T | silence, jingle 135, the black `948(0,255,0,0,50)`, the card, the chat shut | `~mm_scene_open` |
| T+5 | "Suspicious" unlocked and played (308), the lock (fov_clamp, cutscene_status, ops hidden, minimap 2, gravestone, area sounds 25, draw distance), the player in a copy (raw 2895,2727 → 11108,1637), the cast, the card shut, 5 shakes cleared, xp drops and sides shut, `highlighting_off`, `minimap_toggle 2` | `~mm_scene_lock`, `~mm_scene_enter`, `~mm_scene_hud_lock` |
| T+6 | `cam_moveto`/`cam_lookat` at 232/232, the black lifts, page one | per scene |
| pages | the player's `[facing] coord=` the speaker on each page | `~mm_scene_page` |
| E | the last click: `midi_song 147 (0,50,0,0)`, the black, the chat shut | `~mm_scene_close` |
| E+4 | the player put back | `~mm_scene_leave` |
| E+5 | the unlock, `cam_unlock false`, `cam_reset`, sides, `highlighting_on`, the black lifts, `minimap_toggle 0` | `~mm_scene_unlock` |
| E+9 | the next chapter's card | `@mm_chN` |

| scene | T..E | starts | copy | cast | camera | pages | verdict |
|---|---|---|---|---|---|---|---|
| Karamja: the Foreman and Caranock | 4562 [3689]..[3831] | the tick after Waydar's last page is shut | square 40,71, player at 36,37 | `grandtree_foreman` 43,38 facing 43,36; `mm_caranock` 43,36 (spawn north) facing 43,38; the office's 2 `shark` (44,33 / 47,33) and 2 `seabird1` (33,44 / 48,32), wandering | moveto 39,37 h400; lookat 49,37 h160 | 14, moods as recorded | rebuilt |
| below the atoll: Caranock and Waydar | 4563 [5051]..[5163] | the click that shuts Zooknock's last page | square 41,71 level 0, player at 47,28 | `mm_waydar_1op` 46,15 and `mm_caranock` 48,15, spawn south, turned to each other at T+6 | moveto 47,24 h600; lookat 47,15 h160 | 19 | rebuilt; the three spiders ours added are not in the recording and are gone |
| below the atoll: Awowogei | 4565 [1492]..[1605] | the tick after Garkor's last page is shut | square 41,71 level 1, player at 47,28 | `mm_awowogei_cutscene` 54,35 (spawn west) facing 53,35; `mm_caranock` 53,36; `mm_waydar_1op` 53,34; 3 `mm_cutscene_spider` at 53,38 / 53,32 / 55,35 | moveto 47,36 h600; lookat 53,35 h160 (ours had 47,35 h740 / h240) | 25; a two-tick gap before "What of my human?" ([1580] → [1582]); the player is not turned on Awowogei's pages | rebuilt |

The stand-in types are gone: the scenes use Jagex's `mm_waydar_1op` (6675) and
`mm_caranock` (1460) as sent.

### The hangar opens (4562 [2557]..[2581])

| tick | recorded | ours |
|---|---|---|
| T [2557] | the click that slides the last piece: puzzle shut, a message box with no continue, `cam_shake(0,10,0,0)`, synth `rumbling_loop` loops 5 delay 15, the black | rebuilt (was two chat lines and a 4-tick wait) |
| T+3 | the lock, the player in a copy of square 40,70 at 25,27 facing 24,29; `mm_waydar_1op` 25,31 facing 24,33; `mm_daero_1op` 23,30 facing 21,31; `cam_reset` | rebuilt (was the template square, no cast) |
| T+4 | `cam_moveto` 25,25 h600 rate 232/100; `cam_lookat` 16,41 h350 232/100; the shake; the black lifts | rebuilt (was 31,26 → 16,38 at 100/100) |
| T+8..T+11 | `loc_anim m_glider_unfold` on the gliders at 16,30 / 16,34 / 16,38 / 16,46, one a tick, a `wing_unfold` synth with each; at T+8 `cam_moveto` 26,30 h550 rate 1/1 | rebuilt (all four on one tick, one synth, no drift) |
| T+18 | the black | rebuilt |
| T+21 | the unlock, `mm_daero` 5→6, the player at 2649,4507 | rebuilt |
| T+22 | the black lifts, `midi_song technology (296) 0,60,60,0`; T+24 the overlay shut | rebuilt |

### The sigil's arrival and the final battle

4565 [1741]..[1778] and the fight: the demon document.

## Fades with a move inside them (93 recorded)

Offsets from the fade to black. `close` is the overlay's `if_closesub`; `busy` (12393) goes
back to 0 with it (4562 [4969]).

| shape | move | lift | close | recorded uses | ours before | now |
|---|---|---|---|---|---|---|
| long | T+4 | T+5 | T+9 | 66 fades move at T+4 and lift at T+5; the overlay shuts at T+9 in 46 of them, at T+7 in 5 (the bunker pads among them), and outside the tabulated window or under a box in 15. Daero's blindfold, a jail guard's punch, Kruk (the line "Kruk escorts you to Awowogei." at T+5), the monkey minder in and out, Lumdo back to Crash Island, the arena's trapdoor and crack | T+3 / T+5 / T+8 | `~mm_fade_move` |
| archers | T+4 | T+5 | T+9 with the box "You have been hit by poisoned arrows and knocked out!" | 5/5 captures show the box | the box only on a 1-in-10 roll | fixed: always |
| short | T+3 | T+4 | T+6 | 12 of this shape: Lumdo to the atoll (always 2802,2707), the hangar scene's exit, Captain Barnaby's ship | T+3 / T+5 / T+8, landing 2801,2707 in two places | `~mm_fade_move_short`, tile fixed |
| knockout | T+3 with the box "You are knocked unconscious by a blow to the head!" and the lift | T+3 | T+5..T+7, with the box's click | 6 | T+3 / T+5, box after | fixed |
| trapdoor (temple) | T+2 | T+3 | T+5, no line | 3 | a line "You climb down the trapdoor." and T+3 / T+5 / T+8 | fixed, line removed |
| the hole under the crate | T+4 to 2802,9166 | T+5 with the landing box | — | 1 | landing 2801,9169 | fixed |
| glider flights (fade colour 1512708, 20 cycles) | T+1 | T+3 | — | not counted here (the tabulator skipped this colour) | not this quest's script | **open**: not compared |
| bunker teleport pad | T+4, synth `teleport_all` at T | T+5 | T+7 | 2 | not compared | **open** |

The scene opens are the three fades with the move at T+5 and the lift at T+6.

The jail guard's punch (4562 [4958]/[4959]; 22 recorded: 17, 4 and 1 in 4562..4564): the guard turns to the player's
tile and the player's page is shut on one tick; on the next the guard plays
`m_gorilla_attack`, the player is thrown (`exactmove` 20/40) with `human_mm_flyback`
delay 20, takes a hit (12 at [4959]; ours deals a fifth of current hitpoints, LostCity's rule, unverified), synths `gorilla_bigpunch` and `human_hit`; the
black the tick after. Ours did this from the guard's `ai_timer` with a `p_delay`, which the
server aborts ("suspended on p_delay/p_arrivedelay with no player", seen in the first test
run of this pass): **the punch never happened**. It is now a player queue raised by the
timer. Not yet compared on our wire.

## Pages

1,092 distinct recorded pages, menus, boxes and game messages across the four sessions,
checked mechanically against every string literal in the script tree (text with `<br>` → a
space, mood from the chathead seq).

| | before | after |
|---|---|---|
| in `quest_mm` with the recorded mood | 862 | 913 |
| in another package (generic content) | 64 | 64 |
| mood differs | 25 | 8 |
| menu differs | 10 | 4 |
| text not found | 131 | 103 |

Of the 103 left, 90 are not this quest's text defects: system and generic game messages
(41: level-ups, potions, food, music unlocks, shop prices, diary lines), monkey gibberish
rolled per page (25), the crates' composed boxes (13), gendered lines the checker cannot
expand (9), Zooknock's composed "We still need…" lines (2). The other 13 are in Open
(items 6 and the snake pit, Smite, smelting and alchemy boxes, which are generic).

What changed (each with its tick in the script):

- **Start**: "Start the Monkey Madness I quest?" / "Yes." / "No." and the new-quest line (4562 [201], [203]).
- **Moods** (25): Daero, Zooknock, Lumdo, Lumo, Bunkdo, Garkor, the jail guards (angry), the Elder Guard (angry), Padulah, the monkey minder, the monkey child, Uwogo (shock).
- **Wording**: "A human... here?"; "decommission" (twice); "Good day to you too Sergeant."; "You are clearly a human of many talents."; the Elder Guard's "Grrr ... Get out of my way."; "Don't listen to him, Awowogei!"; two double spaces; the reward box's spacing.
- **Menus**: `<col=0000ff>Return to previous menu` (Daero, three menus); Daga's third row.
- **New from the recording**: Garkor's "Come back when you look like a Karamjan monkey, and hurry!" (4564 [1169]); Padulah's "Ah yes. How stupid of me not to see the likeness." and the end of his "I'll be back later." branch (two invented lines removed); Muruwoi after the captive (4565 [1424]); King Awowogei's three "Hello stranger" pages for a monkey who is not the Karamjan envoy, and his name on the "still pondering" page (4565 [1382]..[1391]); the monkey child refusing a second talisman (4563 [4084]); Daga's dragon scimitar answer (4564 [777]); Daero's page before the training choice and the seven pages after it (4565 [2583], [2616]..[2637]); Zooknock's spell graphic `windwave_impact` h80 as the talisman comes back (4563 [5038]).
- **The crates** (11 kinds of crate, 64 boxes, 4562 [8489]..[9324]): one item box three ticks after the op, "This crate is full of... bananas.`<br><br>`Do you wish to take one?", its buttons "No|Yes" by `objbox_setbuttons` (Yes answers sub 1); with a full pack "…`<br>`You don't have space to take any." with a continue. Ours was a message box, then a menu, then a chat line.

## The journal

39 opens, 22 distinct journals, at the stages in the table of contents below. 54 distinct
paragraphs compared as text (rows joined, tags stripped): 47 were already ours.

| stage (mm_main / varbits) | session [tick] | rows |
|---|---|---|
| 2 / narnode 3 | 4562 [569] | 15 |
| 2 / caranock 3 | 4562 [784] | 28 |
| 2 / narnode 7 | 4562 [1145] | 33 |
| 2 / daero 2, 4, 6, 7 | 4562 [1329], [2024], [2593], [2771] | 38, 44, 46, 51 |
| 2 / lumdo 2 | 4562 [3106] | 66 |
| 3 / zooknock 3; lumo 1; garkor 2 | 4562 [4526], [4926], [6875], [7191] | 16, 19, 21, 30 |
| 3 / zooknock 5 | 4563 [94], [773], [1521], [4134] | 44, 44, 44, 54 |
| 4 / zooknock 6; garkor 4 | 4563 [5442]; 4564 [1207], [2475]; 4565 [1376] | 30, 38, 41, 41 |
| 5 | 4565 [1628] | 39 |
| 6 | 4565 [2280] | 28 |

Fixed: the title ("Monkey Madness I"); "The Foreman … Gnome Liaison Officer" (was "foreman",
"Liason"); "Sergeant Garkor" in chapter 3's copy (Jagex's own "Gakor" stays in chapter 2's,
4562 [7191], 4563 [4134]); the Monkey Child section's first two paragraphs, which the port
had commented out ("doesn't fit"), restored where 4563 [4134] shows them.

**Open**: rows were compared as joined text only. The script still carries LostCity's forced
`|` breaks, so the WRAPS are unverified against the 350 px rule; the rest of the Monkey
Child section is LostCity's and stays off (the one recorded open, after the toy went back,
shows only two paragraphs); the not-started overview (interface 782) and the completed
journal were not recorded.

## Action inventory

208 + 139 + 128 + 75 distinct ops across the sessions (`rsprox_actions.py`). Verdicts by
class only: this pass did not give each row its own verdict, which the skill requires.

| class | rows | verdict |
|---|---|---|
| this quest's npcs and locs (Narnode, Daero, Waydar, Lumdo, Caranock, Garkor, Zooknock, the sappers, the jail and its doors, the crates, the child, the aunt, Kruk, the elder guards, Awowogei's throne, the minders, the zoo monkeys) | most | pages and frames above; timing per op not traced |
| generic, in other packages: spirit tree, Grand Tree ladders and doors, gliders, banks, tablets (Varrock ×9, Falador, Ardougne ×3), potions, food, prayers, pickups, drops, the charter crew, Captain Barnaby, the log balance, superheat/alchemy on the backpack monkey | many | **no verdict**: not diffed in this pass |
| the recorder's kit (adamant knives, Protect prayers, necklace of fangs) | some | theirs |
| a random event (Capt' Arnav's chest, 4562 [6506]..[6535]) | 7 | not this content |

Teleports: 137 + 60 + 30 + 33 rows. The quest's own landings were read for the frames above
(hangar 2393,9892 / 2649,4516; Crash Island; the atoll 2802,2707; the jail 2770..2776 ×
2793..2794; Kruk 2802,2762; the minder 2604,3281; the temple tunnel 2807,9201; the cavern
2802,9166). The bamboo ladders, the watchtowers, the bridge jump and the stairs were not
checked.

## Open

1. **No wire audit.** No section's tick log has been diffed against the recording: sounds
   (the guards' and spiders' attack sounds, `ooks`, the arrows' `arrowlaunch2` area sound at
   range 10), projectiles (the archers' `iron_arrow_travel` 41 / 66..116, angle 15, heights
   60 → 146), npc anims, loc changes, music envelopes.
2. **Npc info not built**: `npc_block_inventory.py`, `rsprox_npcinfo.py`,
   `npc_movement_audit.py` and `multinpc_stance_audit.py` were not run. Every `[facing]`,
   `[say]` and `[enabled_ops]` of the atoll's monkeys, the gnomes' walks (Daero and Waydar
   wander in the recording) and the multinpc shells (`mm_daero` 2020, `mm_waydar` 2021,
   `mm_lumdo` 2022, `mm_garkor` 7181, `mm_zooknock` 7193, …) are unchecked.
3. **Ambience** (step 3) not counted.
4. The chapter card's close does not start the area's song (needs an engine command).
5. Daero's training: on T+5/T+6 the recorder is hit (`human_hit`, shield-block synths); the
   exercises are real blows and are not built. The strength course's tile is unrecorded.
6. Text with no home yet: the three wise monkeys before Recipe for Disaster ("These monkeys
   are too busy arguing with each other to notice you." as a monkey; "MMMGHGHGGMH!!!",
   "What's going on ARRGH!", "Arrgh! Human! Scary! Go away!" as a human with the amulet;
   ours answers "Ook ook.", invented, in `recipefordisaster_monkey.rs2`); Oobapohk's "May I
   browse your store?" / "Sorry, I'm currently out of javelins."; the monkey archer's
   "Sorry, I am far too busy to speak right now."; the box "You cannot attack as a
   monkey."; "The monkey in your backpack disappears in a puff of smoke!" on a teleport;
   the snake pit's "You climb up the rope and out of the pit."
7. Bunkdo's "Yeah?" is quiz at 4562 [8889] and neutral at [6593]: three call sites, all
   quiz now; which one is the neutral one is not resolved.
8. King Awowogei's stranger pages are scoped to after the captive (the one recording);
   before it, unverified.
9. The poison on an archer capture: one in five recorded, odds unverified (kept at 1 in 10).
10. Music unlock lines are sent as `@mes_hl_red@Name` at rev 242; our shared proc sends
    `<col=ff3045>Name`. Shared code, not changed here.
11. The glider and teleport-pad fades (table above).
12. The quest test (`test/quests/mm.lua`): see the hand-off message for its last run; the
    jail guards now really punch, which the test's routes were not written for.
