# Enlightened Journey notes (what the ladder cannot know)

Auguste stands at 2808,3355 on Entrana (the guide says 2809,3354; stand there).
At Taverley he is the multinpc zep_multi_piccard (2938,3422): talk to that symbol.

Dialogue gates (ej_shared.rs2 zep_piccard_talk):
- Stage 0: needs 20 qp, Firemaking 20, Farming 30, Crafting 36. Yes! Sign me up -> 5.
- Stage 5: "Umm, yes. What's your point?" -> 6. Stage 6: "Yes." -> 10, then 20 in the same talk.
- Stage 20 with the origami balloon: Auguste takes it (consumed). Stage 40 takes 2 papyrus + the potato sack.
- Stage 70: talk once per item, pick Give dye / sandbags / silk / bowl (a talk each).
  The last item gives a basket of apples + Auguste's sapling (needs 2 free slots).

Item flow: the 8 sack fills are use sack on loc sandpit (glass.rs2:19). Sandbags are stored
8 at most, extra ones stay in the pack.
Branches: use 12 willow branches on zep_multi_basket_entrana (ej_crafting.rs2). Grow Auguste's
sapling in a tree patch and cut its branches with secateurs (skill_farming/farming_tree.rs2,
seam pass matthew-mbp-m4-b50-seam1); every hand-in's source and route is in
test/quests/wip/enlightenedjourney/relay.md. Never ::give a hand-in.

The flight (ej_flight.rs2): Auguste needs 10 logs + a tinderbox in the pack and weight <= 40 kg.
He takes the 10 logs when you say Okay. Interface 470 (grid) + 471 (controls) open; each
click moves one column right: sandbag +2 rows, log +1, relax 0, tug -1, emergency tug -2.
Twenty columns per screen, three screens. A cell two rows off the helper's corridor is a hazard,
the ground band is rows 0-2, rows 12 is sky. The last click of screen 3 must end at row 5.
Any crash, a bail or closing the panel ends the flight: stage stays 90, logs are gone, bring 10 more.
Route that works (helper tables): screen 1 sandbag, log, relax x9, red rope, relax x2, rope, relax x5;
screen 2 relax, log, relax, log, relax x10, log, relax x5; screen 3 relax x8, red rope, rope,
relax x3, log, relax x4, rope, relax. Corridor data: configs/zep_flight.enum (the helper's tables).

Differences from the guide: the hazard layout is derived from those tables, not from the original
art; the wiki lists the screens one move short, the helper tables (20 moves) are followed.
Cutscenes (first and second test launch, basket weaving) are not ported: spec pending.
Pets are not checked on the flight. Log storage after the basket is not built.
