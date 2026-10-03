# At First Light: what the ladder cannot know

Source: OSRS wiki + Quest Helper (no LostCity quest). Driven end to end by scratch drivers.

Where things really stand
- Verity (1559,9464) is behind the bar. Talk to her ACROSS the counter from in front of it (about 1559,9461); the tiles behind the bar (1559,9467, 1558,9466) are a sealed pocket (counter at z 9463, flap hg_table_tavern02_door01 at 1556,9463 has no op) and a goto there is a teleport past the bar (b56 sampler, from the map files; the across-the-counter talk tile is not yet proved by a run).
- Wolf (1554,9462): talk from 1554,9459 or 1556,9462.
- Kiko WANDERS near the bed (spawn 1553,9460). Read her tile first, stand
  orthogonal to it; a diagonal tile answers "I can't reach that!".
- Cat bed (1552,9460): click it from 1552,9459. It is a multiloc on the quest var.
- Fox is two spawns on 1623,2982: afl_hunter_fox_multi (states 4-10) and
  afl_hunter_fox_normal (11-12). The trigger binds the SPAWNED wrapper.
- Leafy bush 1618,2979: press from 1617,2979. Rough bush 1673,2992.
- Embertailed jerboas wander 1657-1670, 2998-3009 (6 spawned).
- Atza 1696,3063; pile of equipment 1697,3063; hammer on the floor 1696,3070.

Doors and barriers
- Hunter Guild stairs are the shared maplinks. The Burrow->surface rows are in
  afl_maplink.dbrow (the generated maplink.dbrow had only the down rows).
  Landing tiles: down 1557/1558,9451; up 1557/1558,3047.
- Imia (hg_mixedhide_seller) sells the box trap for 41 gp; Hunter 46 is the shop gate.

Dialogue gates
- Apatura: Yes/No choice. Qualify is five named refusals.
- Fox state 4 talk writes 5; poultice hand-in writes 6; second talk gives the fur (7).
- Wolf at 3 only advances once bedcheck is set (mouse used AND bed checked).

Mechanics
- Winding: Wind (op 1) on the unwound mouse. Wound mouse on Kiko OR on the bed.
- Poultice: one smooth leaf + one sticky leaf + ONE tail (use a tail on a leaf).
  The second tail is consumed by the bed.
- Box trap: needs Hunter 39 (jerboa), Eagles' Peak done. Lay (inv op 1) on a
  tile with no loc in its zone; some tiles answer "You can't lay a trap here."
  A failed trap is dismantled (op 1) and re-laid. Catch rolls are random.
- Pile: "Set-up" with a hammer or Imcando hammer -> housetrapped 2, then talk to Atza.
- Bed: use a jerboa tail on the bed with trimmed fur + needle/costume needle,
  after Verity has taken the report (report bit 1, state stays 10).
- No fights.

Differences from the guide
- Fur and report replacement is free at the valid state (atfirstlight.rs2:327, 458).
- Report Read (op 1) shows the text (atfirstlight.rs2:398). Destroy is not bound.
