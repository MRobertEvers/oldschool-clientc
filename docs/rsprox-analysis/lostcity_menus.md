# LostCity-ported menus vs Jagex's interfaces

The LostCity port (2004 content, rev 254) replaced every interface it could not
carry over: IF1 screens became `~p_choice*` menus or `~skill_multi`, books and
scrolls became `~mesbox` pages, item boxes became plain mesboxes. Rev 241 has
Jagex's own interface for most of them, and the RSProx index shows Jagex
opening it. This is the ledger: what was found (2026-10-26), what is done, and
what is open.

How it was found: the port's own `// Era:` notes, LostCity's
`if_open*` calls diffed against ours, and the cache's interfaces that no server
script opens, each checked against `rsprox_index.py search interface <id>`.

## Done

| What | Jagex | Before | Recordings | Test |
|---|---|---|---|---|
| Anvil smithing, every anvil | `smithing` (312) | `skillmulti` family -> product chain | index 3449 [7271..7288], 3443, 3259, 800, 4289 [652..675] | `test/quests/_anvil_iface.lua`, `tutorialisland.lua` |
| Tanner (Ellis, Sbott, Ranging Guild) | `tanner` (324), IF1 Tan 1/5/X/All | `~p_choice5` + `skill_multi` | index 2950 [342], 3259 [666..669], 1841 (Sbott's prices) | `test/quests/_tanner_iface.lua` |
| Ship journeys: Port Sarim <-> Karamja, Port Sarim <-> Entrana, Port Sarim <-> Void Knight outpost, Captain Shanks | `ship_journey` (299) + clientscript 1131 | teleport + mesbox (Pest Control drew nothing: it passed journeys 14/15, the script knows 10/11) | index 4257 [99..113], 1418 [2541], [3719], 3990 [470], [490], 311, 1425, 1637, 1686, 2799 | `test/quests/_ship_journey.lua` |
| Item boxes (`~objbox`, `~doubleobjbox`) | objectbox (193) / objectbox_double (11) | `~mesbox` | the tutorial's [646] hammer box; 468 objbox pages in 23 transcripts (chat.rs2) | quest tests |
| Books, Jagex's rows: Book on Baxtorian, Witches' Diary, Diary of Herbi Flax, and the first spread of Ghrim's book | `book` (392) via `~book_read_spread` | `~mesbox` pages, partly paraphrased | index 4349 [453..456] (rev 241), 3229 [6569..6577], 3071 [291..306], 231 [19979] | `test/quests/_books.lua` |
| Books, LostCity's text (no recording reads them): History of Iban (chapters), Journal of Randas, Book of the elemental shield (and its slashed copy), Ghrim's spreads 2-6 | `book` (392) via `~book_read_spread_flow` | `~mesbox` pages; the slashed shield book read an invented line | none; marked unverified in each file | `test/quests/_books.lua` |
| Make-menus the port skipped (2026-10-27): herb + vial, secondary + unfinished potion, chisel on an uncut gem, feather on shafts, arrowtips on headless arrows, wool on an unstrung amulet / the salve shard, grapes on a jug, knife on bread (breadcrumbs, and the Below Ice Mountain sandwich), raw food used on a fire or range, raw beef/bear meat on a range (Sinew &#124; Cooked meat), Aggie's dyes | `skillmulti` (270) with Jagex's title and mode per menu | made one per click with no menu; cooking titled "How many do you wish to make?" | a scan of every recorded `skillmulti_setup` (128 sessions): 4187, 4074, 4076, 3123 (potions), 3259 (gems), 1099, 4143, 317 (arrows), 4185, 3046 (stringing), 1102 (wine), 3021 (bread), 4095, 4031, 1099 (cooking), 4262, 1733 (sinew), 3181, 1678 (Aggie) | `test/quests/_skill_menus.lua` 30/30; mortton, hero, belowicemountain, tutorialisland |

Make-menu rules, from that scan:
- The quantity cap Jagex sends is what the pack can make (12 guam leaves -> 12; arrows cap at 10 sets), and a single product the player can make once never gets the panel: no recorded single-cell menu carries a cap below 2. `~skill_multi1_makeable` does both.
- Titles: "How many gems would you like to cut?" (mode 12), "How many sets of 15 do you wish to feather?" / "... complete?" (mode 3, the 1/5/10 bar), "How many would you like to string?" (10), "How many bottles would you like?" (4). Cooking is "How many would you like to cook?" over the RAW item when one kind is held, and "What would you like to cook?" over the COOKED products when several are (mode 6; the karambwan is mode 7). Dough is "What sort of dough do you wish to make?".
- Cadence: unfinished potions one a tick, finished potions, stringing, arrow sets and wine every two ticks, each starting the tick after the pick (wine two ticks after). Gem cutting was only recorded boosted (the pack in one tick), so its two-tick step is unverified.
- Recorded lines that replaced ours: "You cut the sapphire." (lowercase), "You attach bronze arrowtips to 15 headless arrows.", "You carefully string the shard of crystal.", the wine line once with the last jug, "You cook the karambwan. It looks very dodgy.", "You accidentally burn the karambwan." (no "You cook the Karambwan on the fire..." first), "You dry a piece of meat and extract the sinew.", "You slice the bread into breadcrumbs.".
- Aggie: Make-dyes opens Blue | Red | Yellow; "Okay, make me some red dye, please." opens the one dye. The answer is a double item box, "You hand the redberries and payment to Aggie.<br>Aggie produces a red bottle and hands it to you." or "Aggie wants three redberries and five coins to make red dye." with five coins. Her Dyes op needs `gobdip_met_aggie`, which Jagex sets when the player asks "Can you make dyes for me please?" (1678 [1365]); nothing set it before.
- `~doubleobjbox` now matches both recordings of the box (4289 [235], 3181 [4214]): the chatbox reset first, no script 600.

Found on the way:
- Al Kharid's tanner is `ellis_tanner` (3231) in the rev-241 cache; nothing bound him, so Ellis did nothing. `tanner` (5809) is the Crafting Guild's.
- The customs officer had no Pay-fare / Travel binding.
- Jagex's boarding line is "You pay 30 coins and board the ship." (ours had "the 30 coins"); the Entrana monk's Take-boat boards with "After a quick search, the monk smiles at you and allows you to board." and no conversation; the Void squire's Travel says nothing.
- Jagex's anvil sends no synth: the hammer sound is the seq's.

Item boxes: 199 call sites restored by matching LostCity's text, 24 by hand (the object in a local: `$corpse`, `$staff`, `$treasure`, ...). What is left (70) is not a downgrade to undo blindly: 57 have no matching text in ours (the script was rewritten, several from recordings), 6 are plain `mes` lines that may be what the recording shows, and the trail-clue gifts (Murphy's sextant, Hazelmere's challenge scroll) wait on the trail port.

## Open

| What | Jagex | Ours | Recordings |
|---|---|---|---|
| Gianne's cook book, the Blurberry cocktail guide | LostCity had dedicated recipe screens; rev 241 has none by those names (`aluft_cooking` is the gnome-cooking Create screen, not a book) | paraphrased `~mesbox` "recipe stubs" | none |
| Regicide's Big Book o' Bangs | LostCity's `inter_167` with symbol pictures; rev 241's counterpart unknown | `~mesbox` | none |
| Scrolls: Nulodion's notes, Elena's note, Grand Tree scrolls, Underground Pass blood-well scroll; the Underground Pass tablets | a scroll interface (220, 222, 808, 625, 741 all exist) or `stoneplaque` (748): which one per item is not recorded | `~mesbox` | none (searched by object and by text, 2026-10-26) |
| Dragon Slayer: Ned's voyage to Crandor | rev 241 has `dragon_slayer_qip_cr_journey` (544), not `ship_journey` 3 | teleport + messages | none (interface 544 in 0 sessions) |
| Fishing Trawler: out and back, the reward net | `ship_journey` 7/8, `trawler_reward` (367) | teleport; fish straight to the pack | none (366/367/368 in 0 sessions) |
| Gnome gliders | `glidermap` (138) | `~p_choice` destinations | none (0 sessions) |
| Thormac | `thormac` (332) | `~p_choice5` | none (0 sessions) |
| Party Room chest | `partydrop_main` (265) / `partydrop_side` | Deposit stubbed | none (0 sessions) |
| Ranging Guild ticket exchange | `rangingguild_ticketexchange` (2) | "has nothing to trade with yet" | none (0 sessions) |
| Agility Arena ticket trader | `agilityarena_rewards` (107) | `~p_choice` + mesbox | none (0 sessions) |
| Karamja -> Ardougne (customs officer at Brimhaven) | clientscript 1131 has no such journey | hop | none |

Make-menu actions with no recording, left as they are: bolts, battlestaves, cannonballs, pestle and mortar, pies and pizzas, gnome cooking (`aluft_cooking`, 49). The karambwan's thoroughly/poorly question after Tinsay's lesson is ours (the one recorder had no lesson). Aggie's conversation outside the dye-making is still LostCity's wording; 3181 and 1678 record most of Jagex's ("What can I help you with?" options, "Three lots of redberries and five coins to you.", the black/white dye answer).

Everything still open needs a recording: each row above was searched in the index by interface id, and the books and scrolls by object and text too. `tools/rsprox/rsprox_book.py` turns any recorded read of interface 392 into `~book_read_spread` strings.
