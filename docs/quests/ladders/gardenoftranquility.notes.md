# Garden of Tranquillity -- what the ladder cannot tell you

Primary %garden_quest: 10 Ellamaria, 20/30 Wise Old Man (ring asked / test failed), 40 the whole garden chapter, 50 Roald, 60 done. "Planted", "grown", "statues placed" are derived from the secondary varbits, not stages.

Gates
- Ellamaria needs Fenkenstrain done (`%fenk_quest` OR `%creatureoffenkenstrain`; ::complete writes only the first) and BASE Farming 25. Choose the first option to accept.
- Wise Old Man test: answers are C, A, C, B, B, B ("Ask me nicely..."), then any. A wrong answer sets 30; ask "Can I retake" and repeat.
- Every villager (Elstan, Lyra, Kragen, Dantaera, Althric, Bernald) answers "Do I know you?" unless the ACTIVATED ring is WORN.
- Alain (Taverley gardener) needs the ring UNEQUIPPED.
- Well: the ring must be unequipped and used from the backpack (garden_althric.rs2:28). Rose seeds (4 of each colour) only after that. Fish it back with a fishing rod, 13%..37% by Fishing (garden_althric.rs2:52).

Farming
- Onions: Lyra's patches are the real Port Phasmatys allotments (3601,3529). Cabbages: Ardougne allotments. Plant AFTER Lyra/Kragen ask (marker set in garden_lyra.rs2:73). Rake, plant 3 seeds, one stage per 10 minutes; `t.clock.skip(11)` then talk to the NPC (the talk runs the catch-up) -- four times, then the crop is grown.
- Elstan's marigold uses Falador flower patch 1 (3055,3308); skip 6 minutes per stage and talk to Elstan.
- Dantaera shoot: secateurs on the dead white tree (3008,3498), pot it with plantpot_compost, water it (spends a can charge); sprouts in 5 minutes (catch-up on login/softtimer).

Ellamaria's garden (3226..3233, 3472..3487)
- Rake each patch three times, then plant: 4 seeds each for delphinium, snowdrop, vine and each rose colour, 3 for each orchid, sapling + spade for the White Tree. Orchid pots take a compost bucket first (empty bucket returns); the two pots are told apart by x (3229 pink, 3231 yellow). Plain patches mature in 3 stages of 4 minutes (White Tree 4); the catch-up runs when you talk to Ellamaria.

Statues (garden_statues.rs2)
- Ask Ellamaria for the trolley only after everything is planted. Use it on the Lumbridge king statue (3231,3217) or Falador's Saradomin statue (2965,3381): the trolley npc appears at (3232,3218) / (2965,3383).
- Push moves it one tile away from you (you follow), Pull one towards, Big-push up to four; "will not go any further" = walled. Lumbridge: east to x 3235, north to z 3226, east over the bridge to x 3252 -> jumps to Varrock (3215,3500). Falador: north to z 3402 -> same jump.
- Varrock: east to 3226, south to 3495, east to 3230, then south to 3487 and east to 3231 (king) or south to 3484, east 1, south to 3479 (Saradomin). Place (op 5) within 2 tiles of the plinth (3233,3487) / (3230,3479).
- A lost trolley: tell Ellamaria "I have lost the trolley" (garden_shared.rs2:91).
- The Falador guard cutscene is NOT built (cutscene spec pending).

Finale
- Ring worn, talk to King Roald (3221,3473), accept the long chat. Needs four free slots for the reward.
