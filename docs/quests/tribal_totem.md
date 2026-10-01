# Tribal Totem -- parity brief

Source of truth for behaviour: LostCity_Content2 `scripts/quests/quest_totem/`
(+ `areas/area_ardougne_east/{horacio,rdpt_employee,wizard_cromperty,guide_book}.rs2`,
`areas/area_brimhaven/kangai_mau.rs2`). Where OSRS changed something, the OSRS
form wins. Wiki: [Tribal Totem](https://oldschool.runescape.wiki/w/Tribal_Totem)
(revision not pinned: no network in the parity pass; the Quest Helper
`TribalTotem.java` was read for step text). Ported by the parity3c pass, 2026-09-29.

Requirements: Thieving 21 (boostable). Rewards: 1 Quest Point, 1,775 Thieving XP,
5 swordfish. Stage var `%totemquest` (0 not started .. 5 complete), plus
`%handelmort_traps_disabled` bit 0 = combination door solved, bit 1 = stairs trap found.

## Legs (port file: `quests/quest_totem/scripts/totem_mansion.rs2`)

| Leg | LostCity | OSRS-era difference kept |
| --- | --- | --- |
| Horn crate (GPDT Wizards' Tower) Investigate | label only once started; text "There is a label on this crate..."; crate never opens | none |
| Use label on the teleport crate | mesbox, `inv_del`, stage 2, "Now I just need someone to deliver it for me." | none |
| Read the teleport crate label | after the swap shows the mansion address | none |
| GPDT employee / Cromperty | stage 2 -> 3, teleport -> 4, `curse_all` sound | GPDT wording (was R.P.D.T., 11 June 2025 wiki change) |
| Mansion front door | securely locked from the street, opens from the inside | walked through, not swung |
| Combination door (`combodoor`, interface 369) | KURT, A..Z wrap, Enter closes the lock either way, "The combination seems correct!" / "This combination is incorrect." | cache `tribal_door` per-wheel varbits instead of LostCity's `tribal_door2` bitfield |
| Stairs Investigate | Thieving 21, repeatable, sets bit 1 | 3-line mesbox text |
| Stairs Climb | untrapped: up; trapped: click, fall to the sewers, 20% hitpoints + 1 damage | none |
| Chest | Open (animation), Search gives the totem once (inventory + bank), "The chest is empty." otherwise, swings shut | full backpack refuses |
| Kangai Mau hand-in | 1 QP, 1,775 Thieving XP, 5 swordfish | shared `~quest_complete_rewards` scroll |
| Tourist Guide to Ardougne | four spreads on the book interface; spread 2 names Lord Francis Kurt Handelmort (the password clue) | rev-239 book interface |

Proof: `::totemrun` (debugproc + C-side `[if_button]` dispatch in
`torirs_server_world_selftest.c`) and `tools/quest_gate/run.py --script ... --name parity_totem`.
