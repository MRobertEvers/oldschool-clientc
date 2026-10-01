Monkey Madness I notes (parity pass 2026-10-01, source = LostCity quest_mm)

Ape Atoll dungeon ladder (leg 3, enterDungeonForAmuletRun):
- loc mm_bamboo_ladder_dungeon_entrance, south Ape Atoll, stand at 2765,2704.
- Its one op is Climb-down (op1). The handler is bound in
  quest_monkeymadnessii/scripts/monkeymadnessii.rs2:335 (one trigger per loc),
  body = LostCity ape_atoll_dungeon.rs2:1-6 (+6400 z), lands 2764,9103.
- The maplink row keys on the exact tile 2763,2703, so a click from the
  adjacent tile never matched it. Do not rely on maplink here.
- MM2's soft-skip stage write rides the same handler.

Ferry chain back to the atoll (leg 3): Daero (Grand Tree 1st floor) blindfold
-> Waydar (hangar) -> Lumdo (Crash Island) -> Ape Atoll at ~2802,2707.

Sigil / final battle: mm_sigil op2 needs mm_main >= completed_ch3 and
mm_garkor = garkor_joined_10th_squad, not in the wilderness; it asks
"Let the sigil teleport you". The camera sequence (mm_demon.rs2:51-58) is
LostCity mm_demon.rs2:96-99 verbatim: shake, moveto 27_31, lookat 27_34 at
232/232, lookat 27_34 at 10/10, moveto 27_25, then reset. The battle itself
is this pack's owner-private design (squad of 9, demon 170 hp), not LostCity's.

Puzzle: the 5x5 slide board is shuffled by 255 random moves each run; read it
with ::mmpuzzle and solve by BFS (rejected/mm.lua has a sliced solver).

Fights: Ape Atoll ravine archers shoot ranged; wear Protect from Missiles
(prayer 52 in setup) and bring food. Left unproved: legs 4-8 of the relay.
