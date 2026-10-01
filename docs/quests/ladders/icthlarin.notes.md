icthlarin -- driven notes (wiki source)

Stand tiles. Wanderer 3315,2849: stand 3315,2850 (2851 is walled off). Rock 3324,2858: click from
3322,2855, lands 3320,2796; ics_wall_crack leads back out.
Temple door 3295,2779 (stand 3295,2781) lands on 3277,9173 beside the ladder, the only pocket of
the pyramid joined to the pit. Mummies roam the path.
Pit = blocked 3291-3296 x 9194-9196. Jump north: stand 3292,9193, ics_little_pit_to at 3292,9194
(op 2) lands z9197. Back: stand 3292,9197, ics_little_pit_from at 3292,9196. Needs and costs 20% run
energy (fresh characters have 10%: ::icthlarinenergy). Agility roll stat_random(130,255): 99 never
falls; a fall is 1-3 damage, you stay put.
West door 3280,9199: stand 3280,9201 outside, 3280,9198 inside; the door tile counts as inside.
East door 3306,9199: stand 3306,9201 outside, 3306,9198 inside.
Sphinx 3300,2784 (stand 3301,2786), answer "9.". Town High Priest 3281,2772 is drawn only at states
0-15 and 25-26 (cache multinpc); at 16-24 he is in the east room.

Tile game (interface 147): 25 tiles ics_1..25 row-major, a click turns the 3x3 block around it, solved
when all 25 varbits are set (ics_tilechecker 33554431). Buttons are IF1: t.ui.invoke(widget, 0); the
server trigger is [if_button,icthalarins_tile_game:ics_N] (unnumbered). Solve by Gauss over GF(2) on
the ics_tileN readings. The golden bird (ics_tilegame_picture) re-deals.

Jar: pots at x3286: liver/Het z9194, lungs/Crondis 9195, stomach/Scarabas 9196, intestines/Apmeken
9193. Hypnosis picks ics_little_jar_multi (1 Het, 2 Scarabas, 3 Apmeken; 4 Crondis is legacy, never
picked); only that pot lifts. First reach raises its Apparition (owner npc, melee for Het/Scarabas,
magic max 12 for Apmeken/Crondis); reaching with none standing raises a fresh one. Take the jar (11),
jump the pit south (12), north (13), the west door asks the tile game again, then Drop (inv op 5)
within 1 tile of the pot (14); a drop elsewhere is refused.

Prep: Embalmer 3287,2755 (stand 3287,2757). Raetul ics_little_linen1 3311,2787 (stand 3311,2789)
sells linen for 30 only after the Embalmer was met. Lake: stand 3286,2839 (2840 is water), press
icthalarins_waters_edge with an empty bucket; suntrap 3305,2756 (stand 3305,2758) takes the saltwater
bucket by use. Sap: knife on an evergreen with an empty bucket, only at state 15; nearest is 3018,3458.
Carpenter 3313,2771: stand 3313,2770; he waits for the Embalmer to finish,
takes the log on one talk and gives the holy symbol on the next (a lost one is carved again).

Third memory: the east door turns the holy symbol into the unholy one (17); use it on the
multiloc ics_sarcophigi_door_2_op (3312,9195, stand 3311,9195): the plain deserttreasure_sarcophigi_wall
has no ops. Leave and re-enter (19), talk to the ceremony High Priest, the Possessed Priest spawns
3306,9195 (4 spells, max 1/2/4/6).

Differs: the cat is an inventory item; the jar pick is random(3); the ceremony and fourth memory
are dialogue pages, not cutscenes; no hostile first-memory Sophanem, Apparition prayer immunity
or Air weakness.
