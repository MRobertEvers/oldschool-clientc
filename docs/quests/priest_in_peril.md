# Priest in Peril

Rebuilt from RSProx index 4459 (rev 241, 2026-11-02), Jagex's rewritten quest; the
LostCity (2004) port this brief used to describe is gone. The analysis, a verdict per
recorded action, the per-section tables and the open list are in
[docs/rsprox-analysis/quests/priest_in_peril.md](../rsprox-analysis/quests/priest_in_peril.md).

Requirements: none. Rewards: 1 Quest Point, 1,406 Prayer XP, the Wolfbane dagger, access
to Morytania. Stage var `%varp302_priestperil` (perm, transmitted):

| value | constant | meaning |
| --- | --- | --- |
| 0 | not_started | |
| 1 | started | "Yes." to "Start the Priest in Peril quest?" |
| 2 | agree_to_kill_dog | the fake Drezel's errand, through the temple door |
| 3 | killed_dog | the guardian killed in the player's copy of the cellar |
| 4 | return_to_drezel | King Roald's anger; the monks start hunting |
| 5 | find_drezel_key | Drezel met through the bars |
| 6 | unlocked_drezel | the iron key in the cell gate |
| 7 | poured_blessed_water | the blessed water on the coffin |
| 8 | meet_in_mausoleum | Drezel told |
| 10..59 | begin_bring_essence + n | essence given, one write each |
| 60 | complete | the dagger page's click: the reward and the scroll |
| 61 | access_holy_barrier | Drezel's blessing: the barrier lets the player through |

Varbits on `%varp5662_vampire_secondary` (perm, transmitted): `returned_to_roald` (15612),
`returned_to_fake_drezel` (15613), `priestperilevilmonk_hunt` (15614), `evilmonk_dead`
(15615, hides the leader), `tried_fake_key` (15616), `ending_dialogue` (15617),
`salve_dialogue` (15618). `%varp7408_priestperil_mausoleum` (ours, not transmitted): bits
0-6 the seven swapped monuments, bit 20 the north gate unlocked with the golden key.

Test: `test/quests/priestperil.lua`.
