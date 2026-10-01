# Priest in Peril -- parity brief

Source of truth for behaviour: LostCity_Content2 `scripts/quests/quest_priestperil/`
(+ `areas/area_mausoleum/scripts/{drezel,holy_barrier,gates}.rs2`,
`areas/area_varrock/scripts/king_roald.rs2`). Where OSRS changed something, the OSRS
form wins, and only where this brief says so. Ported by the parity3c pass, 2026-09-29.

Wiki pins (oldid): [Priest in Peril](https://oldschool.runescape.wiki/w/Priest_in_Peril?oldid=15292273),
[Quick guide](https://oldschool.runescape.wiki/w/Priest_in_Peril/Quick_guide?oldid=15266571),
[Monument](https://oldschool.runescape.wiki/w/Monument?oldid=15271167),
[Golden key](https://oldschool.runescape.wiki/w/Golden_key?oldid=15254446),
[Iron key](https://oldschool.runescape.wiki/w/Iron_key?oldid=15254444),
[Temple Guardian](https://oldschool.runescape.wiki/w/Temple_Guardian?oldid=15237248),
[Monk of Zamorak (Paterdomus)](https://oldschool.runescape.wiki/w/Monk_of_Zamorak_(Paterdomus)?oldid=15290091),
[Murky water](https://oldschool.runescape.wiki/w/Murky_water?oldid=15187401).
The wiki cannot be quoted verbatim through the fetch tool: dialogue is LostCity's text, and
OSRS wording differences that could not be pinned are listed as open issues in the pass report.

Requirements: none (Prayer 0). Rewards: 1 Quest Point, 1,406 Prayer XP, Wolfbane dagger,
access to Morytania (the holy barrier). Stage var `%priestperil`:

| value | constant | meaning |
| --- | --- | --- |
| 0 | not_started | |
| 1 | started | Roald agreed to send the player, or the offer was taken |
| 2 | agree_to_kill_dog | temple door knock accepted ("Sure. I'll do it.") |
| 3 | killed_dog | temple guardian dog (cellar under 3405,3507) dead |
| 4 | return_to_drezel | Roald told "YOU DID WHAT???" |
| 5 | find_drezel_key | Drezel spoke through the cell gate and was agreed to |
| 6 | unlocked_drezel | iron key used on the cell gate |
| 7 | poured_blessed_water | blessed water poured on the coffin |
| 8 | meet_in_mausoleum | Drezel freed |
| 10..59 | begin_bring_essence + n | essence handed in so far |
| 60 | complete | 50 essence given; reward |
| 61 | access_holy_barrier | Drezel's warning heard, the barrier lets the player through |

`%priestperil_mausoleum` (not transmitted): bits 0-6 = the seven swapped monuments, bit 20 =
first underground gate unlocked, bit 21 = layout seeded, bits 22-28 = the seed.

## Legs

| Leg | LostCity | OSRS-era difference kept |
| --- | --- | --- |
| Roald offer / refusal / reminder | verbatim (`king_roald.rs2`) | none |
| Temple door Knock-at | 3 questions, "Nope"/"Sure" branches | the cache Large door only has Open, so Open knocks below stage 4 (see report) |
| Dog trapdoor + Temple Guardian | trapdoor 3405,3507, dog fight, stage 3 | instanced guardian per wiki 15237248: fight is real, `npc_findhero` binds the killer |
| Roald "YOU DID WHAT???" | stage 3 -> 4 | none |
| Monk of Zamorak (level 30) | drops the golden key on death | pickup is `Take`, key is per-player |
| Cell gate talk-through | stage 4 story, "Tell me anyway" -> "Yes." -> 5 | Open on the Cell gate talks through (cache has op1 only) |
| Underground gate | golden key unlocks once, bit 20 | none |
| Monuments | Study opens interface 272 with the seeded text; golden key on the key monument swaps for the iron key | none |
| Well | empty bucket -> murky water (fresh after completion) | none |
| Bless water | use murky water on Drezel; every copy in one blessing | wiki 15187401: all copies at once |
| Cell gate + coffin | iron key -> 6; blessed water on the coffin -> 7; talk -> 8 | none |
| East trapdoor | 3422,3485 opens and descends to the mausoleum | shared stub in sinsofthefather removed |
| Mausoleum meet | 17 dialogue pages, stage 10 | none |
| Barrier | blocked (10-59), "speak to me first" (60), passes (61) | none |
| Essence | rune essence only in LostCity | pure essence accepted (wiki: 50 rune or pure essence); noted essence refused with Drezel's line; rune first then pure |
| Completion | IF1 `~send_quest_complete` | `~quest_complete_rewards` scroll: "1406 Prayer XP\|Wolfbane dagger\|Access route to Canifis and Morytania" |
| Dagger reclaim | LostCity: free | needs a free inventory slot (invented guard, not on the wiki) |

Proof: `build/parity_state/parity3c/priestperil_driver.lua` (scratch driver, 148 rows, every
fight real, cheats only for levels, food and essence supply).
