# The Garden of Death -- wiki pin

Source of truth for the port (no LostCity version). Fetched 2026-10-03 through api.php with the
project User-Agent (the plain site is behind Cloudflare).

| page | oldid |
| --- | --- |
| The Garden of Death | 15316642 |
| Transcript:The Garden of Death | 14359937 |
| Transcript:Stone tablet (The Garden of Death) | 14350657 |
| Transcript:Word translations | 15097326 |
| Transcript:Dirty note / Warning note / Kasonde's journal | 14347583 / 15097986 / 14347155 |
| Transcript:Stone table / Stone chest / Vase / Rubble / Vines / Tent | 14472019 / 14472020 / 14472021 / 14472023 / 14471737 / 14471722 |
| Boaty | 15301509 |

Requirements: Farming 20 (not boostable). Items: secateurs (obtainable). Rewards: 1 QP, 10,000
Farming XP. Four dungeons (Mount Quidamortem camp, Molch Island, Xeric's Shrine, Ruins of Morra),
one stone tablet each, a word-translation interface (Attempt Translation) and carvings, chest
labels and a compass that teach the words. No npcs, no combat, no dialogue.

Implementation: `quest_gardenofdeath/scripts/gardenofdeath.rs2` (state ladder in its header),
`gardenofdeath_words.rs2` (32 words, 77 meanings, four tablets). Stage word sets are Quest Helper's
inputWords1..4. Walkthrough: docs/quests/ladders/gardenofdeath.notes.md.
