# The Fremennik Trials from RSProx (index 4686, 4687, 4688; rev 243)

Three sessions, recorded 2026-11-26, play the quest start to finish:

| Session | What it covers |
|---|---|
| 4686 | Rellekka's citizens, then the start with Brundt. |
| 4687 | Manni's drinking contest, Sigli and the Draugen, and Swensen's maze. |
| 4688 | The rest of the maze, Peer's house, Olaf's lyre, Sigmund's trade chain, Thorvald and Koschei, and the completion. |

All three are revision 243, so they need the two fixes in [[rsprox-new-revision-transcribe]]. The 4677 master index is spliced in, and each original is kept as `idx468N.rev243mi.bin`. The `--bas` transcripts are in `~/.rsprox-index-cache/home-bas`.

Our content was LostCity's 2004 port (`quest_viking`), already close in its text. Before this change, 663 of the 750 distinct recorded pages matched; afterwards, every recorded page that is in scope matches.

## Stages and vars

Jagex sends only `varp347_viking` and `varbit6486_askelladen_hasop`, plus `varbit812_dagannoth_island_exposition_spam` for Jarvald. The trial progress is server-side, so our `varp6874_viking_bits` and the other authored varps are now `transmit=no`.

| Stage write | Recorded | When it goes out |
|---|---|---|
| 0→1 | 4686 [1718] | On the Yes of "Start The Fremennik Trials quest?", with "You've started a new quest", before the player's page. |
| +1 per trial | 4687 [1573], [2202]; 4688 [129], [2516], [5825], [7416], [8950] | Each trial's own tick (tables below). |
| →10 | 4688 [9087] | On the click of "Then let us put the formality aside...". |

The completion tick also carries the scroll, the rating box, the "Congratulations" line and the XP: 2,812.4 in each of the ten skills.

The scroll's rows are Jagex's own:
- "3 Quest Points"
- "2,800 XP each in Strength,"
- "Defence, Attack, Hitpoints,"
- "Fishing, Thieving, Agility,"
- "Crafting, Fletching &"
- "Woodcutting"

The helmet model sits at 74,128. Brundt names the player when the scroll closes ([9092]); the level-ups follow after the name ([9094]). Before this change:
- the completion was queued;
- the scroll read "Access to Miscellania..." and the helm lines;
- the name page had ";" for "." and opened on its own.

## Pages

`$S/ft/pagecheck.py` compares every recorded page, its mood and its speaker kind against the tree.

**Fixed:**
- the start menu;
- two Sigli pages ("...Let me get this straight;" and "Well outerlander, should you accept my challenge...");
- Peer's post-bank pages ("So I just have to enter by one door...") and his riddle boxes ("My third is in night", "...you will ever possess.|What am I?");
- Thorvald's "If you leave you will of course fail the test..." page;
- Olaf's "Fossegrimen" (not "the Fossegrimen"), his reminder menus ("I don't need another reminder") and the first stage-reminder page;
- Thora's "Gielinor" and her "Not a problem, outerlander..." page;
- Swensen's "Thanks!", which is the player's page;
- 15 moods.

**Added (npcs with no lines before):**
- Jarvald;
- Hambling (spawned too);
- Haskell and Torfinn;
- the Rellekka port master ("Greetings, sailor.");
- Lokar's refusal, with the Lunar Diplomacy requirements box;
- Mord and Maria ("I do not speak to outlanders.");
- Ingrid's outerlander line.

## Manni

Measured from the click on "As you wish outerlander":
- C+0: Manni plays `viking_tankard_drink`;
- C+2: "The Fremennik drinks his tankard first...";
- C+7: the player plays `viking_keg_drink_full`, with the `liquid` synth (not with low-alcohol beer);
- C+9: the keg is removed; `cam_shake` 3/0/20/2 (drunk only);
- C+15: the player's page.

| Item | Recorded | Ours | Verdict |
|---|---|---|---|
| Contest beat (above) | 4687 [421..452], [1061..1084], [1546..1573] | Same ticks | Matched |
| Lighting the strange object | Synth `fuse_light` [1440] | Was `sound_area exploding_vial` | Fixed |
| The object bursting in the backpack | Line, "Ow!", `sound_area exploding_vial` r5 at the player, object gone; the hit a tick later (188 ticks after lighting) [1117]→[1305] | `exploding_vial` added at the burst | Fixed |
| Keg on low-alcohol keg | Bang line, `sound_area explosion` r10 at 2659,3685; next tick "You empty the keg..." and five "What was THAT?"; they face the pipe the tick after [1476..1478] | Says and faces were on one tick | Fixed |
| Stage 1→2 | On the click of Manni's last page, after "Congratulations! You have completed the Revellers' trial." [1572], [1573] | Was written one page early | Fixed |

## Sigli and the Draugen

| Item | Recorded (4687) | Ours before | Now |
|---|---|---|---|
| Locate | The first press adds a public butterfly, `viking_draugen_safe` [1958]; every press points at it (5/5 in view); a press within 3 of it adds the Draugen beside the hunter, facing them, with "The Draugen is here! Beware!" [2045]; the butterfly flies on and leaves 25 ticks after the kill [2179] | An invented hidden tile, revealed only on that exact tile, and the butterfly deleted | The recorded model (LostCity had it; our port had replaced it) |
| Butterfly | Wanders a 12×12 box (x 2601..2612, z 3642..3653); a second one, another player's, at 2685,3627 (session 3122) | — | Rises at one of those two places or LostCity's four anchors |
| Draugen | 60 hp; `human_sword_slash` every 4 ticks; `human_sword_def`; `unarmed_punch`/`ghost_hit` | Same | Matched |
| Draugen death | The tick after the killing hit: removed, smokepuff map anim, `ghost_death`, "You absorb...", talisman swap, no death seq [2153]→[2154] | `human_death`, 2-tick delay, silent | Fixed (`death_script`) |
| Talisman drop | Ordinary drop [1944]; Sigli gives a new one on the click of "Do not lose THIS one." [1952] | The drop also re-added one | Fixed |

## Swensen's maze

- **Correct portals 1..7:** each lands where ours does, then a one-tile step, as ours (4688 [10..122]).
- **Wrong portals:** each wrong portal sends the player to a random tile out of a list of 20, then a one-tile step off it. Every recorded landing is a portal tile of `m41_156`, and 16 of the 20 were seen (4687 [2694..2934]). LostCity listed 2627,10037 twice; the second entry is now 2639,10039, which was landed on twice ([2809], [2865]).
- **Ladders:** face at L+1, `human_reachforladder` at L+2, teleport at L+3, Swensen's page at L+4.

## Peer's house

Matched against the recording:
- every search line and its delay;
- the box contents (seercrate pick, seercrate_2 ship, boxes_1 magnet, boxes_3 thread);
- the scales chest;
- the vase lines;
- the frozen table and the range;
- the trapdoors;
- the door frames.

Fixed:
- **The mural:** it is drawn with `if_setobject` on each coin slot, then clientscript 2524 with (0,150) (4688 [1381], [1608], [1655]). Ours hid components instead.
- **The drain:** the line and the emptied item come a tick after the pour (3/3).
- **Burnt items:** the ashes go to the first free slot.
- **Leaving with the key:** "You have successfully completed the Seer's Trial." [2516], which ours had left out on the strength of a video.
- **An item used on Peer:** the reply about the easy Fremennik diary [7729].

## Olaf

| Item | Recorded (4688) | Verdict |
|---|---|---|
| Askeladden's rock | `askelladen_hasop`=1 with the rock [3640] | Fixed (never written before) |
| Pet rock op | One menu, Talk/Stroke/Feed/Fetch/Stay; each answer's lines, seqs, spotanims and the stick projectile [3852..3903] | Fixed (ours was three 2004 ops) |
| Pet rock wielded | Stance `petrock_human_*` [3826..3832] | Fixed (`quest_viking.obj`) |
| The altar | Talking to it gives Fossegrimen's chathead dialogue and both menus [4732..4754]. Trout gets the default (2/2). Bass is "requires a greater offering". Shark enchants: `xbows_diamond_tips_spotanim`, `viking_lyre_enchant`, `enchant_sapphire_amulet`, the lyre in the fish's slot, her page, no npc [5065] | Fixed |
| Practice tune | Lines every 3 ticks; the putaway at O+13, "perfectly tuned" at O+15 | Fixed (ours was a tick early) |
| Stage show | Face 2658,3665; `viking_lyre_ready` the tick before line 1; four lines 4 ticks apart; jingles 165/164/163; Olaf's pages; the stage on the second page's click [5741..5825] | Fixed (face, ready seq) |
| Musical tree | "You swing your axe at the tree." is a spam message [4062] | Fixed |

## Sigmund

The trade chain's pages and item swaps match. Fixed:
- the moods;
- Yrsa's Change-shoes refusal ("Only Fremenniks may change their shoes here.");
- her page's trailing space.

## Thorvald and Koschei

| Item | Recorded (4688) | Ours before | Now |
|---|---|---|---|
| Descent | Face at L+1; the teleport to 2671,10098 and the climb at L+2; "Explore this battleground..." at L+5 [8450..8455] | Teleport before the anim, at 2672 | Fixed |
| Spawn | 22 ticks on, 2 tiles off: smokepuff and its sound r10, hint arrow (`current_hint_arrow` 7), his line [8474] | No sound, no hint | Fixed |
| Forms | **One npc.** Killing hit at H; at H+1 change of type, `sanctuary`, level change, face reset; at H+2 his page (chathead); his line on its click. Form 4's page sets prayer to 0 [8526..8839] | A new npc for each form, a game message for form 3, an extra "drains all of your Prayer" line | Fixed (`death_script`) |
| Lines | "Hey outerlander! Let's see what you have!", "You can never defeat me, `<displayname>`!" | Not in the list | Added |
| Hits taken | `human_hit` (513) each time | Silent | Fixed |
| Form 4 | Swing every tick, max 1; prayer 0; hitpoints regenerate (bar 27→28) | Swing and max matched | Matched |
| Honour death | D, the tick after the hit leaving 1 hitpoint: stage +1, his swing lands nothing. D+2: `viking_drop_to_knee`. D+6: "Oh dear you are...", the player to 2667,3692,1, Koschei gone, hint off. D+11: the two lines, hitpoints and prayer full, `viking_drop_to_knee_and_backtoready`, Thorvald's five pages (chathead, no npc) [8950..9044] | All on one tick, "Oh dear, you are...", the player on level 0 and Thorvald's pages only if he was within 8 tiles | Fixed |

## Rellekka

The town's anvil, furnace, pottery oven and house fire refuse outerlanders (4686 [848..902]). Our tree had no bindings for them, so the ordinary skill code ran.

## Inventories

**Actions** (`rsprox_actions.py`, `$S/ft/actions_468N.txt`): every row is this content (above) or generic. The generic ones:
- banks, the Grand Exchange, teleports, potions;
- doors and curtains;
- the trapped chest, which is thieving;
- vegetables;
- the recorder's own kit: staff autocast, prayers, the cat.

**Npc types** (`rsprox_npc_types.py`):
- Hambling was never spawned; now he is.
- Jarvald, Haskell and Torfinn have scripts now.
- The guards, wolves, fox, golden sheep and diary npc are generic world npcs. None was clicked in the recordings.

**Assets** (`content_asset_inventory.py --unused`):
- Now used: Hambling, `viking_dummy_coin`/`coin2`/`lid`, `viking_lyre_enchant` and `viking_drop_to_knee`.
- Still unused: the Fremennik Exiles assets and the `_noop` npc variants, none of which this quest's recordings use.

**Stances** (`player_bas_spans.py`): the staff throughout, which is the recorder's own, and the pet rock.

**Cutscenes:** none. The sessions have no `cutscene_status`, camera or fade lines.

## Checks

- `test/quests/viking.lua` drives the whole quest, now through the butterfly, the four in-place forms, the honour death upstairs and the rating-then-name ending. PASS: 2,063 ticks.
- `test/quests/_journals.lua` covers the six distinct Fremennik journals recorded (4686 [1739]; 4687 [474], [1581], [2166]; 4688 [2757], [4412]), row for row. PASS.
- `test/quests/olafsquest.lua`: PASS.
- `test/quests/thefremennikisles.lua` fails at `performForMawnis.act_over`. The committed content fails the same way (a pack built from HEAD), so this predates the change.

## Open

- **Jarvald's trip to Waterbirth Island:** no recording sails it, and the wiki gives no landing tile. Yes does nothing.
- **Stage 0 and stage 10:**
  - the completed journal and the overview are not recorded;
  - most post-quest lines are LostCity's or the wiki's;
  - form 4 beaten is LostCity's.
- **Butterfly:** where it rises past the two recorded places is LostCity's guess. Its 25-tick departure after the kill is one sample.
- **Fossegrimen:** her verdict on raw fish other than trout and bass is not recorded; they get the default.
- **The inventory firecracker's hit:** it lands on the burst tick in ours, the tick after in the recording.
