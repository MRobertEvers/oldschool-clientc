# Passenger boats and ferries — how Jagex sends them (rev 236-242)

Audit of 2026-11-17, after RuneLite (Jagex's own 242 client) went black on the
Port Sarim ship: our content still sent the 2023 `ship_journey` shape for that
route, and the level-1 hold it parks the player in made the server re-send the
login GPI init mid-session (fixed separately in the engine, bd85d146d).

Tool: `tools/rsprox/rsprox_transport_shape.py <transcripts> [--npc NAME]` —
every long move of the recorder with the click that started it and the shape
(journey interface 299, fade overlay 174 / clientscript 948, mesboxes,
jingles, landing level, op -> move ticks). Sessions: every rev 235+ session
holding one of the travel npcs (index search by npc id), transcribed.

## Two shapes

| shape | routes | wire |
| --- | --- | --- |
| **ship_journey** (interface 299) | Entrana monks (index 4257 [99..113] rev 239, 4435 [1299] rev 241); Pest Control squires (index 3990 [470..501] rev 237) | boarding: the player parked at 3064,3208,1, song 147, jingle 172 `sailing_journey`, the modal; arrival N ticks later on the deck (level 1) + mesbox "The ship arrives at ..." |
| **fade** (overlay 174, cs 948) | Port Sarim <-> Musa Point (4378 [92], [237]; 4420 [690], rev 241); Captain Barnaby, all three docks (19 trips 4360..4580, rev 241-242); Torfinn to Ungael (4112 [129], 3941 [1412], rev 236-239) | below |

### Port Sarim <-> Musa Point (seamen / customs officer)

F = the fare tick: "You pay the boarding charge of 30 coins.", "You board the
ship and sail to <place>.", song 147, jingle 171 `quick_sail` (4500 ms at
241/242), fade out. F+1 minimap off, `busy` 1. F+5 teleport onto the QUAY at
level 0 (Musa Point 2956,3146 facing north; Port Sarim 3029,3217 facing west).
F+6 fade in, minimap on, `busy` 0. F+8 overlay closed. No 299, no mesbox, no
deck. Content: `transport_ship/scripts/quick_sail.rs2` (`~fade_move`, hold 3).

### Captain Barnaby

Fare line, song 147, jingle 171, fade out on F; "You board the ship and sail
to X." and the move onto the deck (level 1) three ticks later (3..12 when the
player walks to him first); fade in the tick after; overlay closed two later.
No minimap toggle. Content: `captain_barnaby_ferry.rs2` (song and jingle added
2026-11-17).

### Torfinn (Rellekka -> Ungael)

op3 on his travel/collect form. F: mes_spam "You board the boat and travel to
Ungael.", fade out. F+1 minimap off. F+3 teleport 2277,4034,0 facing north.
F+5 fade in (two ticks after the move, not one), minimap on. F+7 closed. No
jingle. Before 2026-11-17 nothing bound his ops. Content:
`minigame_vorkath/scripts/torfinn_ferry.rs2`; the way back is unrecorded.

## Shared rules found on the way

- Every fade writes varp 5991 `hpbar_hud_show_extra_bar` = 0; login sets it
  -1, so only a session's first fade puts it on the wire (`~fade_screen`).
- `busy` (varbit 12393) goes 0 on the fade-in tick: the delay ends there and
  the overlay is closed by a queue two ticks later (`~fade_move`, 4444
  [242..246] quetzals, 4378, 4420, 4112).

## Not recorded (no rev 235+ session with the trip)

Captain Shanks, Veos, the Rellekka/Miscellania/Lunar/Fremennik Isles ferries,
Klarense, Cabin Fever's boats, Seaman Morris (routed through `~quick_sail`,
[unverified]), the Ungael boat loc. Re-run the tool when one is recorded.
