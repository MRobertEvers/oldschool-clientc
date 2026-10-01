# Regicide notes (what the ladder cannot know)

Source is the wiki + Quest Helper (no LostCity quest). Stage = varp regicide_quest 0..15.

Start: King Lathas (2578,3293,1) answers at stage 0 or 1 with a menu: "I assume you
have a plan?" then "Start the Regicide quest? Yes." The messenger is optional.
lord_iorwerth.rs2 / king_lathas.rs2 follow Transcript:Regicide line by line.

Pass: the second Underground Pass trip is the quest_upass content, not re-driven.
The Well of Voyage is in Iban's room (2009,4712,1); it lands at 2343,9622. The exit loc
(2313,9624) puts you at 2312,3216 and the zone fires Idris, Morvran and Essyllt (stage 2->3).

Dense forest (regicide_route.rs2:67-125): click it from anywhere on its near edge, the
port crosses by the loc's own maplink row (proc regicide_forest_cross; plain
~maplink_agility needs the exact src tile). Needs Agility 56 and stage >= 8.
The east forest at 2238,3148 (cross_over3) summons the private Tyras guard on the FAR side
(stage 8). It is level 110 (30 hp bar), lasts 500 ticks. With 99 stats, a rune scimitar and
12 sharks it takes ~150 ticks. The static guard at 2188,3171 is the other kill, same stage.
Either kill sets stage 9. Clicking cross_over2_tyras_camp (2188,3171) at stage 9 sets 10.

Tracker stands at 2257,3149; talk from 2256. He only answers "pendant" once you hold it.
Iorwerth removes the crystal pendant whenever you talk to him after stage 6.

Camp items: take the two barrels with Take (op 3) at 2190,3144 and 2189,3140. Fill them at
regicide_tar_collection (2263,3127), sulphur at regicide_sulphar2 (2261,3130). Both work
from stage 10 to 11. Keep inventory slots free: coal is not stacked.
Iorwerth's ingredient menu has two pages ("More options..." / "Previous options..."), each
answer sets a *_chat varbit (varp regicide_bits now transmits).
Fuse: 4 balls of wool on the camp loom (2198,3249), Crafting 10. Quicklime: limestone on any
furnace (smelting.rs2 case limestone) or the camp's own (2193,3146), gloves worn or it
hurts 8. Pestle needs a pot in the pack. Ground sulphur has no Druidic Ritual gate.

Chemist (2933,3210): the first menu has "Your quest." Still (2927,3212): use the tar barrel,
turn the tar valve up twice, pressure up once when flow is high, coal when heat is low.
Use quicklime pot, then sulphur dust, on the naphtha; then the cloth on the fire oil.

Catapult: use the fused bomb on regicide_catapult_right (2184,3183), not the frame. Feed the
guard at 2181,3184 first (cooked rabbit via talk or use). Raw rabbit cooks on any fire (row
added in cooking_generic.dbrow). A tinderbox is required. The tent cutscene is the old port's
(not rewritten; real scene is in docs/quests/cutscenes).

Arianwyn appears when you walk into the zone at 2584,3296 holding the letter at stage 13.
Differences from the guide: none known. Elena's advice before the Chemist is not written
(no transcript lines).
