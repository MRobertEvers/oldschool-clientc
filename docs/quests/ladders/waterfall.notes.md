# Waterfall Quest notes (what the ladder cannot know)

Positions (x,z)
- Almera 2522,3498. Raft loc 2509-2510,3494. Hudon on the island 2511,3484.
- Rock 2512,3468. Tree/ledge 2511,3463. Hadley 2516,3428, stairs 2518,3430, bookcase 2520,3427 level 1.
- Gnome ladder 2533,3155 (Tree Gnome Village). Cellar crate 2548,9565. Golrie gate 2515,9575, Golrie 2515,9581.
- Tombstone 2559,3445. Tomb entry 2554,9844, chest 2530,9844, coffin 2542,9811, tomb exit ladder 2557,9844.
- Falls entry 2575,9861. Key crate 2589,9888. West door 2566,9901. Pillars x 2562/2569, z 9910/9912/9914. Statue 2565,9916.
- Raised room: chalice 2603,9910, entry 2603,9914.

Doors, gates, barriers
- Raft: the Hudon dialogue arrives ~8-10 ticks after the click, inside the raft loc. Wait for "crash into a small land mound".
- Rope on rock and on tree only works from the island zone; rock lands 2513,3468, tree 2511,3463.
- Barrel at the ledge washes you up at 2527,3413 (Gerald/Hadley side).
- Gnome ladder: approach from the east end of the z=3155-3156 corridor (2536,3155). z=3154 and z>=3157 are hedge/wall rows.
- Cellar crate gives the key only from stage 3 (book read) on. Gate opens with the key used on it (2515,9573 side); Golrie takes the key back.
- Bookcase gives the book only at stage 2+; read it for stage 3.
- Tombstone: use Glarial's pebble. Tomb refuses weapons, armour, runes, ammo. Enter clean, bring runes after.
- Tomb exit: the ladder at 2557,9844 (maplink 0_39_153_61_52_up) lands 2557,3444.
- Ledge door needs the amulet in pack or worn, else it floods you back to 2527,3413 (8 damage).
- Falls crate: stand south of it (2589,9887); the north side is walled.
- West door: stand south (2566,9899); the two-tile pocket at 2568,9901 has no route to it.
- Statue with fewer than 18 runes placed drops a 20 damage boulder (kills a level-3 hitpoints account).
- Chalice needs 5 free slots.

Dialogue gates
- Almera: "How can I help?", then agree to look. Hudon and Golrie are chat-only.
- Golrie: pebble page follows a mes and 2 ticks; wait for a dialogue before chat.play.

Wandering: none.
Fights: none.

Deviation from the guide
- Gnome ladder: shared table dropped it as ambiguous (ladders_stairs/scripts/maplink_shared.rs2:20). Added quest_waterfall_locs.rs2:25-30, +6400 z as LostCity loc_1754.
- Rune loadout restriction is the OSRS wiki form, quest_waterfall_locs.rs2:132.
- Hudon dialogue lives in the raft loc script, quest_waterfall_locs.rs2:207.
