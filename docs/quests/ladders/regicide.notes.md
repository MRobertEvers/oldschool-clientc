# Regicide notes (what the ladder cannot know)

Source is LostCity_Server quest_regicide (parity3g), wiki wording where OSRS changed it.
Stage = varp regicide_quest 0..15.

The log to Iorwerth's camp runs EAST-WEST at z=3237, not north: click
regicide_logbalance1_start at 2201,3237, you walk to 2196,3237 (and back from the camp side).
Needs Agility 45. regicide_traps.rs2 regicide_logbalance (LostCity :341-386), real forcemoves.
Log 2 (centre x 2261) and log 3 (z, centre 3235) are the same shape.

Dense forest: click from the near edge, the port crosses by the loc's maplink row
(regicide_route.rs2 regicide_forest_cross). Agility 56, stage >= 8.
cross_over2_tyras_camp (2188,3171) BEFORE the guard is dead (stage < 9): "I remember you!"
and the camp guard attacks (route.rs2, LostCity :393-401). At stage 9 the same click crosses
and writes stage 10 silently (no narration).
East forest cross_over3 (2238,3148) at stage 8 summons the owner-private level 110 guard.
Either guard kill sets stage 9. Tracker 2257,3149 answers "pendant" only once held.

Footprints (2241,3150) are a Follow only from stage 6 to 8: one line per stage (LostCity
:483-493); stage 6 -> 7 on first Follow, then talk to the tracker for 8.

Camp items, loom (4 wool, Crafting 10), furnace gloves, pestle needs a pot: as in the earlier
notes in regicide_bombcraft.rs2. Cooked rabbit to the guard at 2181,3184 first.

Catapult: use the fused bomb on regicide_catapult_right (2184,3183); a tinderbox is required
here (not in LostCity). You vanish, the tent square burns (full LostCity loc set), you land at
2183,3185 with stage 12. No camera ops exist in LostCity for this scene.

Arianwyn: walk into the zone at 2584,3296 holding the message at stage 13, or talk to him
(opnpc1 resumes the scene; LostCity has the zone/queue path only).
Elena's advice before the Chemist is not written (no transcript lines).
