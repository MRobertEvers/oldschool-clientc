# Below Ice Mountain: what the ladder cannot say

Source: wiki transcript + Quest Helper (LostCity has no copy). End state is 120, not 45.

Where things really stand
- Willow at the entrance is the type bim_willow (talk_to "bim_willow"); the spawn symbol bim_willow_outside_entrace is not in the client pool.
- Crew, Atlas, Marley: spawn tiles in the *.spawn rows match the guide within 1 tile.
- Checkal at Barbarian Village has the multi parent bim_checkal_barb; ~bim_emote_performed looks for that type within 4 tiles.

Barriers and gates
- Entrance (2999,3493): rock for states 0..29, door "Enter" from 30. State 30..40 asks Yes/No; 120 teleports straight in.
- The hall (m46_90) is public. Landing 2952,5764; exit loc at 2951,5761 returns to 2996,3494.
- Pillars ship broken in the map. A fight raises the four intact ones (bim_pillar_set); undermined ones are tracked in varp7213 bits 0..3 per player.
- Mining pillars needs Mining 10 (boosts count) and any pickaxe, carried or wielded.

Dialogue options that gate progress
- Willow: "Yes." writes 10. Below 16 QP: mesbox, nothing written.
- Marley must be talked to first (5); only then does the cook give the recipe (10). Sandwich in the pack on the next Marley talk writes 35, finishing the same talk writes 40.
- Cook: "I was wondering if you'd be able to make me a Steak sandwich?"
- Atlas "Yes." trains (checkal 15, counter+1, Barbarian Workout bit). Checkal then asks for a Flex (20); the Flex emote beside him recruits him (40).
- Burntof: drink first (5 -> 10), talk again for the best-of-three; any signs win.
- Willow at 20/25: "Yes." runs the scene (30 at the blast, 35 inside with a guardian).

Fights
- Ancient Guardian: level 25, 40 hp, max hit 7. Melee with food works; or four pillars. It is spawned per player and removed when the player leaves the hall.

Differences from the guide
- No instance, no camera, no fades (cutscenes pending, docs/quests/cutscenes/). Dialogue of the three scenes is chat.
- Bag (2950,5769) completes the quest: coins, music, scroll, then Ramarno's chat (varb12068 = 1).
- Flex is usable by everyone (no emote lock in this world).
