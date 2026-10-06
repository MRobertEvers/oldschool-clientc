The Ascent of Arceuus - what the ladder cannot know (b55 parity, driven through the client).

Source: OSRS wiki only (no LostCity copy). Transcript oldid 14761128.

Tower of Magic (door 1596,3820 and 1596,3819)
- Both door leaves answer Open. At stage 3 a Tower Mage speaks, then Yes/No.
- Yes builds the player's OWN copy of map square m24_59 (all four levels) and puts five
  Tormented Souls in it (four beside mages, one by the door). Coords inside are square-local.
- The stairs refuse until the five are dead. Up releases the copy and lands on the REAL
  floor 1 (1580,3821). Asteros stands at 1579,3818 there; Lord Trobin is in a trance
  (stage 5-12) and only talks at stage 13.
- Souls: 20 hp, hit up to 2. Any weapon. No food needed at 40+ hp.

Mount Karuulm
- The surface elevator (1311,3807) lands in the lobby 1311,10188, NOT on the guide's
  1312,10211: that tile is inside Kaal-Ket-Jor's size-4 footprint and talking from
  there gives "I can't reach that!". Walk north 21 tiles to the Tasakaal (1305-1315,10204-10209).
- Any of the three Tasakaal starts the same talk. Exit loc: brimstone_dungeon_exit 1312,10186.

Grave and trail
- The grave (1347,3736) at stage 8 sets 9. Every bush, plant and stump on the field reads
  Inspect at stage 9, but only five answer, IN ORDER: bush 1335,3743; plant 1317,3750;
  plant 1305,3750 (reachable only from the north, 1305,3752); stump 1287,3750; plant 1285,3737. The last plant 1281,3725 raises the
  Trapped Soul (owner-private, 500 ticks, 30 hp, hits up to 4, Bones). Out of order or a decoy:
  "You search it but find no sign of the trail."
- Stage 10 and the soul gone (timed out): inspect the last plant again.

Rocks (stage 12)
- Kaal's last talk rolls varb7865 (0-3); only that rock holds the device, the others say
  "nothing". Rocks: 1706,3888  1713,3892  1713,3875  1722,3881. The driver only reads the
  first as clickable (the others read "covered" in the dark cavern); setvar device_location 0.

Different from the guide
- Tower fight is a private copy, as the wiki describes (no instance id in the guide).
- Trapped Soul does not crawl back into the bush when out of sight (wiki). ascentofarceuus.rs2 / locs.rs2:raise.
- Favour reward and Graceful recolour UI are not authored (aoa_leftover_*).
