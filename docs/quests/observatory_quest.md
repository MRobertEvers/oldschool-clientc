# Observatory Quest -- parity brief

Source of truth for behaviour: LostCity_Content2 `scripts/quests/quest_itgronigen/`
(`quest_itgronigen.rs2`, `observatory_professor.rs2`, `observatory_assistant.rs2`,
`goblin_guard.rs2`, `book_of_astrology.rs2`, `spirit_of_scorpius.rs2`,
`grave_of_scorpius.rs2`, `ghost_grave_of_scorpius.rs2`) plus
`skill_crafting/scripts/glass/glass.rs2`. Where OSRS changed something, the OSRS
form wins. Wiki: [Observatory Quest](https://oldschool.runescape.wiki/w/Observatory_Quest),
[Transcript:Observatory Quest](https://oldschool.runescape.wiki/w/Transcript:Observatory_Quest)
(revision not pinned: no network in the parity pass; the Transcript text was read from
the copy the earlier audit saved). Quest Helper: `observatoryquest`. Ported by the
parity3c pass, 2026-09-29.

Requirements: 10 Crafting (not boostable for the lens), 3 planks, 1 bronze bar, 1 molten
glass; a sleeping goblin guard must be killed. Rewards: 2 Quest
Points, 2,250 Crafting XP, 1 uncut sapphire, a jug of wine from the assistant, access to
the telescope, plus one sign reward (below). Stage var `%itgronigen`: 0 not started,
1 started, 2 planks, 3 bronze, 4 glass, 5 mould, 6 sent telescope, 7 complete, 8 wine.

## Where OSRS wins over LostCity (named for the report)

| Detail | LostCity (RSC) | OSRS form kept |
| --- | --- | --- |
| Telescope | viewing queues `constellation_dialogue`; the professor announces the sign | you view the telescope (`observatory_starsign` rolled once, varbit on `itkeepgatelock`) and later tell the professor which of the 12 signs you saw, four choice pages with next/previous (Transcript "After viewing the telescope") |
| Completion | professor dialogue ends the quest | the right sign gives `That's exactly it!` / `Yes! Woo hoo!`, a per-sign flavour pair (12, verbatim), then the reward; a wrong sign gets `I'm afraid not` |
| Sign rewards | RSC reward set | wiki table: Aquarius 25 water runes, Capricorn 875 Strength XP, Sagittarius maple longbow, Scorpio weapon poison, Libra 3 law runes, Virgo 875 Defence XP, Leo 875 Hitpoints XP, Cancer amulet of defence, Gemini black 2h sword, Taurus super strength(1), Aries 875 Attack XP, Pisces 3 tuna; every sign also 2,250 Crafting XP and an uncut sapphire. Nov 2018: a combat-XP sign is never rolled for a player whose matching stat is level 1 (`observatory_sign_is_combat_protected`) |
| Lens mould | `craft_telescope_disc` deletes only the glass | same; the wiki recipe lists the mould with `mat2cost = No`. The professor takes the mould with the lens ("You may as well take this mould too") |
| Rewards / journal | LostCity text | shared `~quest_complete_rewards` scroll and the QUEST COMPLETE journal banner |
| Cutscene | RSC has none | dialogue-only approximation (no `cam_*` opcodes hosted in this client) |
| Treasure Trails chart lesson | not in RSC | not ported (needs the trail system); the topic returns a stub `~mesbox` |

## Legs

| Leg | LostCity | Port |
| --- | --- | --- |
| Professor start, 3 planks / bronze bar / molten glass hand-ins | professor stages 1-4, each item deleted from the backpack | `observatory_professor.rs2` |
| Assistant hint per stage; `telescope repairman` at stage 6; wine at stage 7 | `observatory_assistant.rs2` | `observatory_assistant.rs2` |
| Dungeon chest choice, wrong chest `The chest is empty.`, right chest `You find a kitchen key.` | `quest_itgronigen.rs2` | `observatory_dungeon.rs2` |
| Gate: `If you open the gate, the guard will hear you. You need to get rid of him.` while the sleeping guard is within 8 tiles; prod the guard, it wakes, kill it, the gate opens | `goblin_guard.rs2` | `goblin_guard.rs2` + `observatory_dungeon.rs2` (gate) |
| Stove search leaves the mould | mesbox, `lens_mould` | `observatory_dungeon.rs2` |
| Use molten glass on the lens mould (Crafting 10) at stage 5 | `craft_telescope_disc` | `glass.rs2` `observatory_cast_lens` (mould kept, glass consumed) |
| Both dungeon stairs climb back up | (LostCity maplink) | `observatory_stairs.rs2` (cache op text `Climb up` was rejected by the maplink import, so the stairs did nothing) |
| Book of astrology | four spreads, `book_flip_page(0, 0, 3)` | `book_of_astrology.rs2` on interface 392 |
| Spirit of Scorpius | wisdom gives the unholy-symbol mould once; blessing; second mould | `spirit_of_scorpius.rs2` (already verbatim) |

Proof: `parity_itgronigen2` (98 ledger rows, 0 fail) drives the whole quest through the
real client with a real goblin-guard fight; `parity_itgronigen_side3` drives the book
(all four spreads) and the spirit; `::itgronigenrun` reports OK.
