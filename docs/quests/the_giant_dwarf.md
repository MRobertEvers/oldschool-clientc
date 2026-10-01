# The Giant Dwarf -- pinned brief

Source: https://oldschool.runescape.wiki/w/The_Giant_Dwarf?oldid=15346380
(pinned 2026-09-30 by the parity3e closer; LostCity has no quest_giantdwarf, so
the port is constructed from this page and Quest Helper `TheGiantDwarf.java`).

## Requirements

200 coins, logs, tinderbox, coal, iron bar, 3 cut sapphires, redberry pie;
Crafting 12, Firemaking 16, Magic 33, Thieving 14 (all boostable).

## Boots fit for a king

- Left boot: 14 Thieving. Steal it from the table inside Dromund's house
  "when Dromund is not looking towards you."
- Right boot: 33 Magic. Stand outside by the window and cast Telekinetic Grab
  on it on another table, again "when Dromund is not looking."

## Joining the consortium

- Secretary tasks: 3-5 un-noted materials (clay; copper, tin, iron, silver,
  gold or mithril ore; coal), 20 points per delivery, 2 points subtracted for
  failing or cancelling, time limit 10 minutes plus one minute per item.
  75 points lets the player speak to the director.
- Director tasks: 2-4 bars (bronze, iron, steel, silver, gold, mithril),
  12 points per delivery, time limit 10 minutes plus 2 minutes per item.
  100 points lets the player join.
- Joinable companies: Green Gemstone, White Chisel, Silver Cog, Brown Engine,
  Yellow Fortune, Blue Opal, Purple Pewter (Red Axe cannot be joined).

## Rewards

2 quest points; 2,500 Mining, Smithing and Crafting XP; 1,500 Magic,
Thieving and Firemaking XP.

## Port status (parity3e)

Boots and the consortium points game follow the sections above
(`gdwarf_boots.rs2`, `gdwarf_consortium.rs2`, telegrab hook in
`skill_magic/scripts/spells/telegrab.rs2`). Not modelled: the task time
limits; the strict any-order of
clothes/boots/axe (the ladder is linear); the cutscenes (spec pending, docs/quests/cutscenes/). Driving notes:
docs/quests/ladders/giantdwarf.notes.md.
