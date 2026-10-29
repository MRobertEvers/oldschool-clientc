# The Garden of Death -- RSProx index 4427

Rev 241.1, world 369, 2026-10-29. The recorder (Farming 29) plays a farm run,
then the whole quest end to end at [1218..2558] and logs out in the fourth ruin. Transcripts:
`~/.rsprox-index-cache/home-default/.rsprox/binary/archive/idx4427.txt` and
`home-bas/.../idx4427.txt`.

Ours: `OSRS-Content/osrs-content/server/scripts/quests/quest_gardenofdeath/`
(rebuilt from this recording; it was a wiki port), test
`test/quests/gardenofdeath.lua` (scriptrun: 208 rows pass; `quest.scroll_title`
and `quest.journal` fail on every quest under scriptrun, no clientscripts).

## Stages

Jagex writes every even value 0..56; the cache's multilocs read the same
numbers (vines 22 Cut / 24 Squeeze, the holes 16 / 30 / 40). Ours were the
wiki's 13/27/37/43/47 and a stage set no recording sends.

| value | written by | tick |
|---|---|---|
| 2 | tent, Yes. (with "You've started a new quest") | [1367] |
| 4 | journal opened | [1369] |
| 6 | hole 1 climbed (with the seq, L+2) | [1406] |
| 8 | tablet 1 taken | [1428] |
| 10 | tablet 1 read | [1464] |
| 12 | "Take the note?" Yes. (the word list) | [1472] |
| 14 | the word list first opened | [1479] |
| 16 | the 5th word of ruin 1 (on the answer's tick) | [1618] |
| 18, 20 | hole 2, same tick | [1833] |
| 22 | the vine note taken | [1847] |
| 24 | the vines cut (C+3) | [1862] |
| 26 / 28 | tablet 2 taken / read | [1886] / [1890] |
| 30 | the 5th word of ruin 2 | [1947] |
| 32, 34 | hole 3 | [2049] |
| 36 / 38 | tablet 3 taken / read | [2096] / [2099] |
| 40 | the 7th word of ruin 3 | [2148] |
| 42, 44 | hole 4 | [2329] |
| 46 / 48 | tablet 4 taken / read | [2345] / [2348] |
| 50 | the 10th word of ruin 4 | [2412] |
| 52 | tablet 4 read again | [2416] |
| 54 | the warning note taken | [2446] |
| 56 | the warning note's long scroll closed | [2481] |

Only `tgod` (14609) is transmitted; the word state is server-only (our varps
8005..8010, `transmit=no`).

## The words

| R | ours | verdict |
|---|---|---|
| The list (804) is `tgod_translations_setup` (6945) with one string, "Word - A/B<br>", "???" for none, in word order | `~god_translations_text` | **matched**: 6 of the 7 recorded lists byte for byte on our wire; the 7th ([1929]) is mid-ruin-2, not re-opened by the test |
| The list arrives with Osto Below/Under, Tlane Garden, Tasol Structure/Building, Kamat Method/Process, Moki Need/Require | `~god_list_given` | fixed (ours granted nothing) |
| A word joins the list when its tablet is read, or when one of its meanings is learned (Ami [2128], Ehke, Ralo [2434]) | seen = read tablet; list = seen or known | fixed (ours: carvings marked words seen) |
| "Attempt Translation" is an IF_BUTTON: synth interface_select1, the list closed, clientscript 110 (meslayer mode 9), answered with RESUME_P_STRINGDIALOG | `[if_button1,tgod_translations:translate]`, `runclientscript*(110)` + `p_namedialog_wait` | fixed; **engine**: `handle_resume_textdialog` now releases a parked `p_namedialog_wait` with a string reply (it only stored it before) |
| not a word ("is", " water"): "You can't think of any suitable translations for that word.", then the prompt again | same | fixed (ours asked "Attempt another?") |
| known: "You've already discovered this translation:<br>Water = Ates", the prompt again | same | fixed |
| new: prayer_boost, "You've discovered a new translation:<br>Time = Miki", "Attempt another translation?" Yes./No. | same | matched |
| the last word of a ruin: the stage, the new-translation box, then found_gem and "You feel that you've translated enough words to read some of the text on the stone tablet.", the loop ends | same | fixed |
| "north" taken with the lake map unread [1618]; "no" (Ami) in ruin 3 with tablet 4 unread and no carving read [2128] | a word may be learned once its ruin has been entered (`~god_word_available`) | fixed (ours required the carving) |
| words needed per ruin: 5 / 5 / 7 / 10, the Quest Helper sets | `~god_stage_words_known` | matched (the stage moved on each recorded last word) |

## The stone plaque (748)

All 11 rows sent (empties too), `if_setmodel background 46830`, 2524, the
mount. The tablet in the Old Ones' tongue; once the list is held (stage >= 12),
a blank row and the sentences again, every known word `<col=33cc59>` when its
meaning in the sentence is known, `<col=32b5ff>` (another known meaning) when
not; the block centred on the 11 rows.

| R | ours | verdict |
|---|---|---|
| 8 plaques ([1464], [1890], [2099], [2105], [2348], [2416], [2433], [2439]) | `~god_plaque_open` | **matched: 8 of 8, all 11 rows byte for byte** (incl. colours, wraps, centring) |
| wrap: p12, a 396 px row fits, 403 does not; the rows' box is 394 | split at 400 (fitted) | matched |
| our tablet table had Xali in "...ates xali" as Earth; Jagex shows a learned "earth" blue there [2105] | alt 52 (Land) | fixed |
| our tablets used `00c000` / `4aa3ff` and a row per sentence in interface 804 | 748, Jagex's colours, running text | fixed |
| the close button is the server's; the note page comes on that tick ([1468], [2422], [2442]) or when a modal that replaced it closes ([2438]) | `[if_button1,stoneplaque:close]`, the page from a queue | matched |
| tablet 1: "As you finish reading, you spot a note stuck to the back<br>of the tablet." (objbox, the list), "Take the note?" Yes., stage 12, pick2, "You take the note. Upon closer inspection, ..." | same | matched |
| tablet 4 at 50/52: "As you finish reading, you spot a note on the floor." (not "stuck to the back", which the wiki says), full pack: "...However, you don't have enough room to take it." and no menu | same | fixed |

## Interactions

| interaction | R | ours | verdict |
|---|---|---|---|
| tent start | L+2 the menu "Start The Garden of Death quest?" Yes./No.; Yes: stage, the quest line, pick2, objbox [1365..1367] | same | matched |
| tent again | "You search the tent, but find nothing of interest." [1397] | same | matched |
| journal | book, title "Kasonde's Journal", 2 spreads, human_readbook, turn spotanims/seqs both ways [1369..1377] | `~book_read_*` with Jagex's own breaks | matched (every row of both spreads on our wire) |
| camping equipment | pick2 + "...find<br>some secateurs. You take them." / nothing of interest [1385], [1390] | same | matched |
| hole down x4 | L+1 face, L+2 stage + human_pickupfloor, L+3 the jump | `~god_climb_down` | matched on the tick log ([42..44]) |
| landings | 1309,9869 / 1375,10032 / 1296,10033 / 1456,9824 | same | fixed (3 of 4 were off by a tile or two) |
| rope up x4 | L+1 face, L+2 human_reachforladder, L+3 the jump; 1309,3469 / 1364,3636 / 1315,3617; ruin 4's (46336) 1449,3511 (index 1676 [904..913], rev 226) | `~god_climb_up` | fixed (ours: an instant teleport, wrong tiles) |
| stone tables | L+1 face, L+2 stage + pick2 + objbox; full pack [2086]; carried [2430] | `p_arrivedelay` + `~god_take_found` | matched |
| plant diagram table / carving | the carving taken [1549]; read: 803 mounted, its close button the server's [1558] | `[if_button1,<carving>:close]` x14 | fixed (no close handler) |
| hourglass chest | objbox, "The chest contains some hourglasses.<br>A label on the chest reads 'Miki Toka'." [1561] | same; the other labels' break [unverified] | matched |
| vines (stage 20, Inspect) | the vine note: full pack [1842], taken + stage 22 [1847] | same | fixed (ours: a wiki line, stage 22 without the note) |
| vine note | interface 291, 12 rows, text centred (rows 3..10) [1850] | `~god_note`, `[if_button1,note:close]` | matched (rows 3 and 10 checked) |
| vines Cut | O: pruning_mid_high + farming_prune; O+3: stage, seq -1, "You cut through the vines."; the secateurs used on the vines do the same (index 1676 [349..352]) | `[oploc1]` and `[oplocu,tgod_vines_cut]` | fixed (ours: no anim, no sound, no delay, no use-on) |
| vines Squeeze | the line, face, regicide_tightfit, exactmove 0..76, squeeze_in; south angle 0 [1867], north 1024 [1965] | same | fixed (ours: two forcemoves, no seq) |
| the boat | L+1 "Where would you like to go?" (Molch Island, Molch, Battlefront, Shayzien less this dock, Cancel); the answer: "You travel to X." with no continue + fade out; C+3 the jump + fade in; C+5 both down | same | fixed (ours: 3 options, no Cancel, "You row across..."); landings Molch Island 1369,3639 (also 1676 [328]), Molch 1341,3645, Shayzien 1408,3612 matched; Battlefront 1383,3665, the tile the player boards from ([1806]), as every recorded landing is; no recording sails there |
| warning note | longscroll (625), `longscroll_setup` (418) "" + the text | same | matched |
| the end | the long scroll's close: stage 56, qp, the line, disease -50/0, the scroll (tablet 1, 78,128, "1 Quest Point", "10,000 Farming XP"), jingle 153 9955 ms; the scroll's close: "Although you've now discovered the fate of the Old Ones, ..."; the level-up after that box [2496] | same | fixed (the box now comes before the level-up) |
| destroy (4 tablets, the list, the warning note) | confirmdestroy_init with "You can obtain another from the dungeon <place>." [2516..2543] | `[opheld5,...]` x6 | fixed (unbound) |

## Music

| R | ours | verdict |
|---|---|---|
| each climb down: midi 729 "The Old Ones" (0/60/60/0), the first with its unlock line | rows for squares 5274, 5532, 5276, 5785 in `docs/audio/music_regions.tsv` (Kronos predates the quest) | fixed |
| Molch Island (square 5432): "Gill Bill" (618) with its unlock | a row | fixed |
| the boat's region music changes at C+5 [1813], [1990], [2244], when the scene's delay ends, not at the jump (C+3) | engine: a square entered while the player is delayed starts its track on the first tick the delay is over (`ToriRSServer_WorldUpdateMap`) | fixed: Gill Bill on our wire at C+5. Across 4300..4429: busy at the jump and released later, the track on the release tick 153/178; free at the jump, on the jump's tick 329/347 |

## Journal (questjournal 119)

Rows for stages 4 [1400], 14 [1523], 16 [1632], [1818] and 28 [1924] are
copied with Jagex's colours; the not-started overview's start text [1230] is in
`quest_overview_start.rs2`. Stages 2, 6..12, 18..26 reuse the nearest recorded
current-step text, 30..56 are written the way the recorded paragraphs are:
**[unverified]**.

## Action inventory (`rsprox_actions.py`)

| rows | verdict |
|---|---|
| tgod_* loc, obj and interface ops, the boat | this content: above |
| bank chest / booths / banker, bank interface, stats, skill guide, world map, quest list, prayer, potions, lobster, vial smash | generic, bound (`interface_bank`, `player/scripts/consumption`, `general/scripts/food.rs2`, ...) |
| tablet breaks: Varrock, Ardougne, Kourend, Battlefront | generic, bound (`teleport_tablet.rs2`) |
| Lunar Catherby tablet [291] | **was unbound**; fixed: same break as the standard tablets, lands 2801,3449 (scriptrun: 2801,3449) |
| ring of dueling, glory, Kharedst's memoirs (worn) | generic, bound |
| tox_xerician_altar Pray-at [2036] | generic (`prayer_altar` category) |
| farming patches: rake, compost, seed, harvest, herbs | generic (`skill_farming`), not diffed here |
| tool leprechaun: use produce on him [362] x9 | **fixed**: `[opnpcu]` on every Tool Leprechaun type (`farming_tools.rs2`); the stack noted with "The leprechaun exchanges your items for banknotes." / "...your item for a banknote."; Falador's (3053,3305) refuses a cabbage, chatsad3, byte for byte [481]; produce per the wiki's list; a note / other items [unverified]. `test/farming/leprechaun_notes.lua` 17/17 |
| Grand Exchange clerk Exchange, offers [191..240] | 3draster-f4's port (in progress, at the owner's request) |
| kitten Interact ([1533]: "Stroke" -> "You softly stroke your cat.", human_pickupfloor, the player's chathap1 page "That cat sure loves to be stroked."), use raw sardine on it [1718], call follower | 3draster-f4's port (`quest_fluffs/kitten_pet.rs2`, at the owner's request) |

Teleports: every recorded move has its cause above (tablets, jewellery, the
boat, the ropes/holes, the squeeze).

## Open

- Grand Exchange and the kitten: 3draster-f4's ports.
- No recording shows, in 4427, 1676 (rev 226, the whole quest again, no journal
  opened) or anywhere else on the index (235 and 3259 only carry the varbit):
  the journal text for stages 2, 6..12, 18..26, 30..56; the hint and strangler
  notes (the `note` interface as the vine note's, the wiki's words); the compass;
  the labels other than 'Miki Toka' (its break copied); "You feel like that word
  has potential..."; the leprechaun's lines for a note and for anything not
  produce. All are the wiki's, marked [unverified] where they are written.
