# Shadows of Custodia - what the ladder cannot say

Port form differs from the guide:
- Stage 2 to 4 moves on the FIRST citizen asked (wiki: "at least one"), not all four
  (shadowsofcustodia.rs2 soc_citizen_asked). Aemilia/Francis only help once Ictus was
  heard (%varb16639_soc_sillyman), then the stage jumps to 5 and the wall loc turns to
  "Inspect" (wall_state 1). Wall state 2 = Reinforce (set on the boys scene), 3 = done.
- Injured boy: talking teleports you home (stage 14); the long scene plays when you
  talk to Aemilia/Francis (stage 14 -> 15), not as an automatic cutscene.
- Captain and wall are order-free; stage 16 is written by whichever finishes last.
- Antos: first talk spawns the 3 creatures (owned, level 93, 100 hp); last kill writes 20.
  Talking to Antos again takes you to the Captain (stage 22); she completes the quest.

Where things stand:
- Bartender (1391,3354) is behind the bar counter; the public side says "I can't reach
  that". Stand at 1391,3353 (behind the counter) to talk to him. Optional: one citizen is enough.
- Cave entrance (1295,3373) is approached from the NORTH (1295,3375); the south side
  cannot reach it. Cave exit loc is at 1295,9768; it returns you to 1296,3371.
- Arrival tile in the cave is 1298,9757 (the injured boy's tile). Antos's chamber is
  1337,9753, about 40 tiles east; creatures spawn 2 tiles west/north/south of him.
- House ladder (1380,3357) is reached from inside the house (1381,3358); outside is walled.
  Upstairs: Etz 1381,3360, Shas 1382,3360. Before stage 16 Aemilia refuses the ladder.
- Fishing rod ground spawn 1345,3352 and hammer 1378,3371 (furnace building) are
  placed by us; the wiki gives no tile. Maple logs: chop mapletree 1383,3375 (114 ticks).
- The plank takes "use fishing rod on plank" (oplocu on the BASE loc soc_log); the
  Inspect op alone only tells you a rod might work.
- Etz and Shas are multinpc shells: triggers live on soc_etz_multi / soc_injured_person_intown.

Fight: 3 strange creatures, level 93, crush, hit about 5-10; 99 combat melee clears them
in about 200 ticks with 10 lobsters. Protect from Melee is not required by the port
(bleed is not implemented). The cave is not flagged multi-combat.

Open: shops are stubs (shopkeeper/bartender Trade say they have nothing yet).
