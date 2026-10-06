# Sleeping Giants brief (matthew-mbp-m4-b56-parity)

Source: OSRS wiki + Quest Helper. LostCity has no copy of this quest (2022, Giants' Foundry update).

## Pinned references

| Reference | Revision | Use |
| --- | --- | --- |
| [Sleeping Giants](https://oldschool.runescape.wiki/w/Sleeping_Giants?oldid=15241064) | 15241064 | Requirements, walkthrough, rewards |
| [Transcript:Sleeping Giants](https://oldschool.runescape.wiki/w/Transcript:Sleeping_Giants?oldid=15263403) | 15263403 | Every dialogue of the port |
| [Giants' Foundry](https://oldschool.runescape.wiki/w/Giants%27_Foundry?oldid=15337299) | 15337299 | Crucible, moulds, quality, tool tick rates, heat |
| Quest Helper `helpers/quests/sleepinggiants/SleepingGiants.java` | repo checkout | Stage ladder and varbit thresholds |

## Facts the port is built on

- Start: Smithing 15 (not boostable). Strike the Hill Giant (Kovac) at 3361,3147 east of Al Kharid, Yes. A cutscene shows the foundry (not ported, spec pending).
- Needs 3 oak logs, 1 wool, 10 nails, hammer or Imcando hammer, chisel; a bucket of water or ice gloves; 20 empty slots for the crate. The foundry's initial room is an instance (wiki), the big chest is the bank.
- Repairs: trip hammer 1 oak log + 5 nails, grindstone chisel, polishing wheel 2 oak logs + 5 nails + wool. Stage 10, then 15 when all three are done, 20 after Kovac's talk, 25 for the commission, 30 complete.
- Commission "Flat Broad": crate, crucible (28 bars' worth of bronze / iron), mould (forte, blade, tip), pour, bucket of water, preform (two-handed, equips itself, only comes off in the preform storage).
- Refinement: heat in lava, trip hammer (hot, every 5th tick +2%, -2.5% heat), cool, grindstone (medium, every 2nd tick +1%, +1.5% heat), cool, polishing wheel (cold, every 2nd tick +1%, -1.7% heat). Wrong tool or heat costs 10 quality; Kovac takes any sword.
- Rewards: 1 quest point, 6,000 Smithing XP, access to the Giants' Foundry.

Tutorial bits (varb13903): 10 commission, 15 crate, 25 crucible full, 30 mould talk, 35 mould set, 40 pour talk, 45 poured, 50 preform, 55 handed in.

Port: `OSRS-Content/osrs239-content/server/scripts/quests/quest_sleepinggiants/`. Notes from driving: `docs/quests/ladders/sleepinggiants.notes.md`.
