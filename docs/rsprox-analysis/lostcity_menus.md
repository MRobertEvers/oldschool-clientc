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

Found on the way:
- Al Kharid's tanner is `ellis_tanner` (3231) in the rev-241 cache; nothing bound him, so Ellis did nothing. `tanner` (5809) is the Crafting Guild's.
- The customs officer had no Pay-fare / Travel binding.
- Jagex's boarding line is "You pay 30 coins and board the ship." (ours had "the 30 coins"); the Entrana monk's Take-boat boards with "After a quick search, the monk smiles at you and allows you to board." and no conversation; the Void squire's Travel says nothing.
- Jagex's anvil sends no synth: the hammer sound is the seq's.

Item boxes: 199 call sites restored by matching LostCity's text, 24 by hand (the object in a local: `$corpse`, `$staff`, `$treasure`, ...). What is left (70) is not a downgrade to undo blindly: 57 have no matching text in ours (the script was rewritten, several from recordings), 6 are plain `mes` lines that may be what the recording shows, and the trail-clue gifts (Murphy's sextant, Hazelmere's challenge scroll) wait on the trail port.

## Open

| What | Jagex | Ours | Recordings |
|---|---|---|---|
| Books: Gianne's cook book, cocktail guide, Ghrim's book, Regicide alchemy book, History of Iban, Underground Pass journal, Mort'ton serum book, Baxtorian's book, witches' diary, Elemental Workshop shield book | `book` (392) / `indexed_book` (680); `interface_book` already ports 392 (`~book_open` / `~book_spread`) | `~mesbox` pages, several paraphrased rather than LostCity's text | 392 in 46 sessions; find each book's own |
| Scrolls: Nulodion's notes, Elena's note, Grand Tree scrolls, Underground Pass blood-well scroll; the Underground Pass tablets | scroll interfaces; `stoneplaque` (748) | `~mesbox` | 220/808/625/741 seen; check each |
| Dragon Slayer: Ned's voyage to Crandor | rev 241 has `dragon_slayer_qip_cr_journey`, not `ship_journey` 3 | teleport + messages | none found yet |
| Fishing Trawler: out and back, the reward net | `ship_journey` 7/8, `trawler_reward` | teleport; fish straight to the pack | not searched |
| Gnome gliders | `glidermap` (138) | `~p_choice` destinations | none |
| Thormac | `thormac` (332) | `~p_choice5` | none |
| Party Room chest | `partydrop_main` / `partydrop_side` | Deposit stubbed | not searched |
| Ranging Guild ticket exchange | `rangingguild_ticketexchange` | "has nothing to trade with yet" | not searched |
| Agility Arena ticket trader | `agilityarena_rewards` | `~p_choice` + mesbox | not searched |
| Karamja -> Ardougne (customs officer at Brimhaven) | clientscript 1131 has no such journey | hop | none |
