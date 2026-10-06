# Tale of the Righteous -- what the ladder cannot know (learned by driving)

Stages are Quest Helper's. The cache hides Phileas from 15 and Duffy's tent copy from 12.

Where things really stand
- Phileas: 1543,3571 inside his house. The door (1540,3570) is a CLOSED wallkit door: click it, then walk in.
- The ladder to Lord Shiro is in the War Tent (1481,3633) and the tent's only way in is the EAST side,
  z 3635-3636. goto_tile to 1492,3635, then click the ladder. Shiro stands upstairs at 1486,3635.
- Archeio 1624,3808 (Talk-to, or op3 Teleport). He lands you at 1551,10211 in the archive; Pagida is at
  1554,10221 (stage 1 to 4 adds "I have a question about King Shayzien VII."). Istoria 1552,10224 leaves.
- Duffy (tent) 1277,3562; the crevice 1214,3558, step off to 1213,3559.

Private copies (tiles are NOT world tiles inside them)
- Pagida's "Yes please" builds your own copy of the prison (m24_159); the portal at the east end
  (1590,10199 world-equivalent) leaves. Read your tile on arrival and offset every walk by it.
- The crevice (rope, "Yes.") builds your own copy of the cave (m18_155); the rope (1168,9973) climbs out.

Prison puzzle (tor_archive.rs2:312 push, :416 hit)
- Device starts 1578,10200. Push slides it to the far end away from you: x 1575 or 1581 (E/W), one tile N/S.
- West end: attack from the NORTH with a combat spell (magic), from the SOUTH with a wielded melee weapon.
  East end: attack from the SOUTH with a ranged weapon plus ammo. Fists do nothing. A spell can splash.
- Never cast the device through the shared spell-fail proc: it retaliates and walks at the player.
- All four crystals white opens the north gate (stage 4). Skeleton 1576,10213 is stage 5.

Cave (tor_cave.rs2)
- Rockfall SW 1180,9972 (Mine, any pickaxe, ~1 in 3 per swing) and boulder SW 1200,9959 (Push) really block
  the passage; both are gone in a copy built at stage 10 or later.
- The magic gate (1171,9946) blocks walking at every stage: click it to pass. Stage 9 calls the Corrupt
  Lizardman (50 hp, poisons, 3 Xerician fabric): kill it for stage 10. Duffy, Gnosi and the team stand
  in the SOUTH room (z < 9945), so a stage 12 visit passes the gate first.

Phileas's house (taleoftherighteous.rs2:335)
- Stage 15 -> 16 is a timer that sees you inside x 1540-1545, z 3568-3572; no click is needed.
