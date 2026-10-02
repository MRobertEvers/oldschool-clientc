# The Forsaken Tower -- what the ladder cannot tell you

Stages: 0/1 Vulcana, 2 Undor, 3 enter, 4+n after n puzzles, 8 case open, 9 hammer, 10 Vulcana.
Source is the wiki + guide (LostCity has none); ft_puzzles.rs2 header lists each puzzle var.

Doors and ways between steps
- Undor will not take the job until Ignisia (1634,3948) has been spoken to once (%varp5801_wint_ignisia).
- Entry door (1382,3817) opens only from stage 3 up (forsakentower.rs2:~200, then the generic door swing).
- Basement ladder down (1382,3825,0) and up (1382,10229) are hand handlers: the cache has no maplink row.
- First-floor ladder (1382,3827,1) sits in the north room: open lovaquest_inner_door at (1384,3826,1) first.
- Top ladder lands you on the SOUTH side (z 3826); the generic climb put you behind it, unable to reach the pylons.
- Crank crate (1387,10228) is in the NE cell: open the prisondoor at (1386,10227) first.

Puzzles (all real clicks, no soft-skip)
- Jugs: shelves (1378,3826) give both jugs, (1381,3829) the tinderbox. Inspect furnace, then the coolant tank,
  then 5/8-gallon pours (use jug on jug; Check on a jug offers Empty); use the 4-gallon jug on the tank; Light.
  Lighting before the tank is full burns 40% of base Hitpoints.
- Power: crank, Start the generator, Inspect grid (Yes) shuffles the 36 tiles and opens interface 624.
  The server must arm the tiles with if_setevents(grid,0,35,6) or the client sends nothing.
  Tile op1 turns right, op2 left; 4 in PowerPuzzle.solvedPositions means 0 or 2 both fit.
- Refinery: Inspect (Yes) -> clogged; notes (cupboard 1387,3820,1) rows 5-7 name the vial order; take the
  Cleansing fluid from the table (wrong vial: 25-30 damage + poison), use it on the refinery, Activate.
  Which vial is right is rolled once per player (%varp7219_ft_fluid_seed).
- Pylons: Rebalance takes the smallest disk or puts the held one down (never on a smaller disk); Reset
  restarts. Tower of Hanoi, west -> centre, 15 moves; solved shows value 16 (no-op locs).

Fights: none. Bring 100+ Hitpoints headroom for the wrong-vial test (level 1 dies).
Invented text: the fluid names (Acidic/Caustic/Volatile/Inert) and the riddle prose; the wiki transcript
was not copyable. The ancient-letter crate (lovaquest_tower_crate_note) is not wired.
