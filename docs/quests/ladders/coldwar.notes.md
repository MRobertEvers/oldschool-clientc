Cold War -- driven notes (2026-10-02, vm-parity1; start to scroll)

Stand tiles: Larry zoo 2597,3266 (stand 2599,3268). Enclosure gate 2594,3266: click it
from the south, 2594,3264. Zoo penguin 2596,3270. Lumbridge Larry is the same npc
(peng_larry_zoo) at 3212,3263, only there once state 45 is reached. Sheep wanders 3194-3201,
3266-3272. Fred 3188,3274. Dairy cow 3172,3317: click Steal-cowbell from the north, 3172,3319
(the south side is fenced). Steal rolls 54-78%: loop it.

Iceberg: Larry's teleport lands 2658,3987,1. Larry 2661,3990, hide 2664,3990, boat 2654,3985.
Outer KGP 2640,4007 and Noodle 2644,4003 only exist from state 70. Avalanche 2638,4011 (enter
as a penguin; a person is thrown back beside Larry). Inside you land 2658,10373.

Suit: Tuxedo-time is op3 on any Larry; empty weapon, shield and cape slots; the suit moves
to the cape slot. Talk-to as a penguin turns you back. It also ends when you leave the zoo,
the sheep farm, the iceberg or the outpost. Removing it by hand outside the outpost says
"stuck"; inside it throws you to Larry (2670,3988,1).

Greeting (zoo 35, sheep 45, KGP 70): talk, the emote panel replaces the emote tab (IF1
buttons, invoke op 0, peng_emote:peng_emote_<name>), one wrong emote fails at once. The
three emotes are varbits 3300-3302 (1 shiver 2 spin 3 clap 4 bow 5 cheer 6 wave 7 preen 8 flap).

Debrief: KGP in the first room west, 2648,10384. All three reports needed. Noodle needs 2
free slots. Corridor door 2633,10404 puts you at the course start 2643,4034,1 and sets 100.
Course (vm-b1-seam1): the instructor refuses 100 -> 105 until %varb3305_peng_agility_state reads
3 (1 stone 7, 2 the last icicle pillar x=2662, 3 the ice); a goto into the finish no longer works.
The water leg is drivable since vm-b1-seam2 (gaps-world, Penguin Agility Course): walk_to
2636,4054 (the ledge climbs you down to 2634,4054,0), walk_to 2630,4055 (wade past the crushers,
no op), click_loc peng_agility_crushcourse_stepstone01 (lands 2630,4057,1, +55 xp; the approach
retry may need a second press). No goto_tile onto the first stone. Retry the first icicle press
(a pick flake). Talk to the Thing (sheep_shearer_the_thing) with op 3 (Talk-to). Op 1 Shear
makes it walk away with no wool, or says you need shears (vm-b1-seam3 coldwar_thing_talk_op).
Lumbridge Larry (peng_multi_larry_lumb) is placed only while varb3298_peng_multi_larry=1, so a
scratch that stages state 45 sets it too. Stones 1-7 jump from two tiles
(stone 7 is an aploc). The ice ends with a slide to the finish 2657,4039,1; talk to the instructor
there, then the fence gate (west edge of 2652,4039) takes you west to 2651,4039, and the door
2643,4032 leads back to the corridor.

Suit (vm-b1-seam1): setup ::coldwarpoh (Rimmington house + Workshop + Crafting table 3, player at
the portal 2953,3224,0) with Construction 34 / Crafting 30, steel bar, plank, silk. enterPoh =
click_loc poh_rimmington_portal op 2 (Home); bench poh_clockmaking_3 op 1 -> Clockwork, then
Clockwork toys -> Clockwork penguin.

Ping and Pong room via the door 2662,10396. Bongos only with the suit OFF and Crafting 30.
Booth guard 2655,10408, panel 2655,10407, blast door 2656,10409 (opens 50 ticks). War room door
2671,10418, pen 2645,10425, pen door 2639,10424, chasm 2657,10423. Icelord kills are real
combat (level 51): door opens after 1-3 kills (1/3, then 1/2, then sure), 40 Attack XP each.
Differences from the guide: the cutscenes are text only (spec pending); the hide shows the
three emote names in chat; teleports use a plain fade.
