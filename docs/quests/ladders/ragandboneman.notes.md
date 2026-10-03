Rag and Bone Man I - what the ladder cannot know (driven 2026-10-02)

Source: wiki (no LostCity quest). Brief: wiki Rag_and_Bone_Man_I + its Transcript page.

Places
- Odd Old Man stands at 3361,3505 (m52_54.spawn); the boiler is the next tile north (3360,3505).
- Fortunato: 3085,3251 (m48_50.spawn). First talk is the refusal then "did HE send you?" and sets
  %varb2047_rag_wine; later talks offer Yes / Not today, then 1 or 8 jugs at 1gp (the wine shop
  interface is blocked in this pack, so the sale is a dialogue buy).
- The client sends the boiler's BASE id (rag_multi_potboiler) for every click and use. All boiler
  ops dispatch on %varb2046_rag_boiler in ragandboneman_vinegar.rs2 (rag_boiler_use).
- Boiler values are 0 nologs, 1 logs in, 2 pot on, 3 lit, 4 boiled (multiloc value k = child k+1;
  Quest Helper's RAG_BOILER 1..4). Boiling is 20 ticks. Remove-Pot works while the pot is cold.

Bones
- Each of the 8 quest bones is a guaranteed drop while collecting (wiki: JagexAsh 2019), unless
  that bone was already handed in. The swamp frog the world spawns is medium_frog_nodrops
  (level 10); it had no death hook until now. Lumbridge goblins are goblin_unarmed_melee_1..8
  (wiki_goblin.rs2 carries the bone call); rats are giantrat1_2/1_3 as well as giantrat.
- Lumbridge is single-way combat: aggressive goblins/frogs make Attack answer "I'm already under
  attack." A driver needs ::passive (16 types max) or a ::spawn in a quiet place.

Dialogue
- Starting the quest needs "Anything I can do to help?" then "Yes". "Where is that mumbling coming
  from?" loops back to the menu. The sack lines are mesboxes ("Sack: Mumblemumble").
- Hand-in: polished bones are taken; raw or in-vinegar bones answer "just clean them up in the
  pot-boiler"; with nothing the Old Man lists a hint for every bone still owed.
- Using the jug on the pot or the pot on the jug both work (the jug-on-pot order is a branch in
  quest_swansong/scripts/swansong_army.rs2 [opheldu,pot_empty]).

Different from the guide
- Fortunato sells by dialogue, not a shop interface (ragandboneman_fortunato.rs2).
- The submitted-bones mask is the soft %varp6208_rag_submit, not the real 2045 counter.
- Karamja bat / monkey need a boat; goto_tile is plain travel.
