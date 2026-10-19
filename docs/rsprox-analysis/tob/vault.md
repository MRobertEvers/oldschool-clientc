# Verzik's vault: the chests and the purple

This file records what Jagex's server sends in the treasure room. It is read
from 25 vault visits in 9 sessions: 1833 (7), 1764 (3), 1779 (3), 1852 (2),
553 (3), 1837 (3), 101, 1758 (2) and 582. They hold **13 purples**: 4 of the
recorder's own and 9 of teammates'. 1852 has a Hard raid in which all five
raiders got one. Every session is from a Leagues V or Grid Master world.
They were found with varbits 6450-6454 on the local index (`type` 6).

**Ours:** `tob_vault.rs2`, `tob_rewards.rs2`.

## The chest varbits

Each raider's own varbits 6450..6454, one per chest, are set when they enter
the vault (the tick of clientscript 2306 "Verzik Vitur's Vault"):

| Value | Chest | Seen |
|---|---|---|
| 0 | a teammate's, no unique | every raid |
| 1 | a teammate's, **unique** | 9 of 9 teammate purples |
| 2 | **mine**, no unique | every raid without my purple |
| 3 | **mine, unique** | 4 of 4 of my purples |

The multiloc list in `all.loc` prints as `multiloc1..5`, but the varbit
indexes it from 0. Value 1 is `notmine_rare`, not `notmine_standard`. Ours
already sets 0-3 this way (`~tob_vault_chest_state`). **match**

## Opening a chest

O is the tick the server handles the open (the client's `oploc1` is on O − 1).

| | Recorded | Ours before | Status |
|---|---|---|---|
| O | the raider plays `human_pickuptable` 832 (delay 10); `sound_area chest_open` 52 at the raider's tile, range 5; the chest becomes `tob_treasureroom_chest_open` 32994, or `_open_rare` 41746 for a unique | the loc change only | **fixed** |
| O + 1 | for a unique, "`<col=ff0000>Name</col> found something special: <col=ff0000>Item</col>`" to every raider in the vault (a teammate's arrives on their O + 1 too); "New item added to your collection log" (the raider's chat option); inv 612; synth `found_gem` 2655; interface 23 | the interface on O, no announcement, no synth | **fixed** (`[queue,tob_vault_reveal]`) |
| The collection log | credited at the open (the line comes on O + 1, 3 of 3, and nothing at her death) | credited at her death | **fixed**. A chest never opened in the vault is credited at the Ver Sinhaza chest instead. That case is unrecorded |
| At her death | no loot line | "`<col=aa00ff>You find: Item`" | **fixed** (removed) |

`test/raids/_tob_vault_purple.lua` forces it: `::tobuniqueme` hands the
raider the unique, `::tobvzskip` ends the three phases, then it goes down the
trapdoor and opens the chest. The run gives 832 + `chest_open` + 41746 on
O = 99, `found_gem` and the log on O + 1, and "va_p1 found something special:
Justiciar chestguard".

## Open

- "The throne collapses, revealing a staircase." (`~tob_vault_roll`) is in no
  recording. The recorded death tick has the wave and completion lines and the
  completion count instead. Not changed here.
- The recorded collection-log line colours the item `ff0000`; ours uses
  `ef1020` (`collection.rs2`, every collection log source).
