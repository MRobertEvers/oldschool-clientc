# Sleeping Giants: what the ladder cannot know

Source: OSRS wiki + Quest Helper. LostCity has no copy. Oldids: quest 15241064, transcript 15263403, minigame 15337299.

Kovac and the cave
- Outside Kovac is the shell giants_foundry_kovac_multi_outside at 3361,3147 (Strike giant at 0..4, Kovac 5..9, gone from 10). Trigger binds on the SHELL (sleepinggiants.rs2:201), not on fake_attack / kovac_quest.
- The dump's plain Hill Giant on that tile is excluded in tools/gen_spawns.py (NPC_SPAWN_EXCLUSIONS).
- The cave (giants_foundry_entrance, 3359-3363,3150) before the quest answers with the player's line. Strike dialogue: Smithing 15 is checked after "your help.", then Yes / No. No leaves stage 0.
- The cutscene after "follow." is not ported (spec pending): the player is walked into the copy.

The foundry is an instance
- During the quest the cave leads to a private copy of m52_179 (sleepinggiants.rs2:132, sg_enter_instance), arrival local 38,28, Kovac added at 40,29. Coordinates in the ladder (3363,11485 ...) are the PUBLIC tiles; click by symbol.
- The broken tools are the *_quest_multi locs swapped over the working ones; Kovac's second talk (stage 15 to 20) swaps the working tools back (bits 3). Exit loc frees the copy; logout leaves at the cave mouth.
- After the quest the cave leads to the public foundry (minigame lane).

Order and gates
- Repairs: hammer 1 oak + 5 nails, grindstone chisel, wheel 2 oak + 5 nails + wool; hammer or Imcando hammer. Any nail type.
- Commission talk (stage 20) gives Flat Broad (words 3 / 4). Crate needs 20 free slots (bronze 22 + iron 10 bars' worth in 20 items).
- Crucible takes bronze/iron bars and weapons, 28 bars' worth; Fill takes everything, use-with adds one. Mould jig opens only after Kovac's mould talk (tut 30).
- Mould screen 718: the picks are client-local, the server hears only the ops. Rows are enum indices 6..11 (1..5 are shop rewards), 17 components a row; tabs 9 a piece.
- Pour needs the mould set. Preform: bucket of water in pack, use-with on the jig, or ice gloves; hands must be empty; it equips itself and cannot be taken off except in the storage.

Refinement (sg_refine.rs2)
- Three sections 0..333 hammer, ..666 grindstone, ..1000 wheel. Heat bands from difficulty 15: hot 675+, medium 342..656, cold 9..323.
- A machine acts every 5 (hammer) or 2 ticks; a wrong tool or heat costs 10 quality and Kovac's Stop! (quality is taken first). 0 quality blocks work but Kovac still takes it.
- Lava / waterfall: Heat / Cool slow, Dunk / Quench fast, both ramp the longer they run. Natural cooling 4 per 2 ticks (guess, wiki gives no rate).
- Not modelled: sweet spots, run energy / coin / reputation payout, Obor kill-count line (no kc var; the 0 kc line is used).
