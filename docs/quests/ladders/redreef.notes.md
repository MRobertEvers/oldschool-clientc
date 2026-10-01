Red Reef notes (driven 2026-10-01, source = wiki + Quest Helper; LostCity has no quest).

Stage numbers: the port uses its own %trr values (redreef.constant), 0..42. 18 = sea crew, 20 = dock,
22 = Last Light shore, 24 = Bethel, 26 = return, 28 = Paxton done, 30 = gear, 38 = east, 40 = plans.

Cache multinpc/multiloc are indexed by the %trr VALUE (index = value+1): Paxton shows 0..28,
Spencer (Zenith) 28..40, diving_a 30, diving_b 32..36, floopa_grove 6..10. Triggers bind the
multinpc BASE symbol, not the child (trr_floopa_grove, trr_spencer_brentwood_diving_a, ...).

Sailing (Conch summer shore -> Red Rock): legs (3176,2332) (2844,2488) (2816,2508), then Disembark
sailing_gangplank_red_rock. Red Rock -> sea crew: (2816,2508) (2832,2384) (2833,2361). Hull must
avoid rocks. Last Light: (2840,2336) (2844,2331); disembark row is "Mooring point", not Gangplank.
After a long trip the hull can jam on the dock: repeat sail_to a nearby tile until it moves.
Disembarking at Red Rock needs the camera to draw the plank; the default pose works when the hull
arrives heading ~6 (angle 768) from the east leg (2844,2488).

Sea crew (redreef.rs2:397-470): the Zenith and Bethel's ships have no hull entities; the 8 crew are
npcs spawned at sea by a hull-proximity timer. Shoot them from the deck (shortbow + arrows, ranged
70). "I can't reach that" means melee is selected: keep the bow wielded for this leg.

Last Light: the door last_light_doorway is scripted (open it at 2856,2323). 6 pirates inside; stairs
last_light_spiralstairs_base then _middle reach Bethel. Her spawn is 2865,2323 (the centre cog is
unreachable). Bethel hits hard (configs/redreef.npc): bring rune armour, an abyssal whip, 16 sharks.
Without hitpoints= stats in the .npc she died in one hit (the combat engine ignores cache stats).

Diving: Spencer talk -> "Let's go." needs the helmet and apparatus worn (wear both first). Teleport
to the root first or Spencer is unpickable from the vessel. Dredger repair (trr_coral_dredger_2)
twice around the giant lobster (112), then Spencer diving_b to the east, surface, sail home.

Form differences from the guide: ship combat is crew-on-npc (no cannons/hull hp); redreef.rs2:397.
