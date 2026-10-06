# Rig pass: javelin_colossus (colosseum)

Documents only; nothing built or run. Table: `javelin_colossus.tsv` (33 rows). Method: `docs/minigames/waves_loop/RIG_PASS.md`.

## The npcs
- colosseum_javelin_colossus, npc 12817, "Javelin Colossus", size 3, cache_npc.txt:330.
  readyanim npc_colossi_javelin_01_idle (seq 10889, 60 frames); walkanim npc_colossi_javelin_walk (seq 10879, 90 frames).
  No other animation field is set.
- colosseum_colossi_gib, npc 12831, no name, model 51241, size 3, not interactable. No readyanim or walkanim.

## The rig
- Javelin colossus: ready and walk both sit on framemap 2245, one rig. It carries 24 sequences and is SHARED by all the colossi
  (Sol Heredit's finalboss set, the shockwave colossus, the javelin colossus). The join does not close it to one monster.
- Gib: no rig. Nothing to join, so no candidate (row `none`).

## Counts
33 rows: 24 sequences on 2245, 1 gib row, 8 spotanims.
Tiers: bound 2, rig+name 4, rig+sound 1, rig 17, name 8, none 1.

## Candidates by role
- bound: npc_colossi_javelin_01_idle (ready), npc_colossi_javelin_walk (walk).
- attack1 rig+name: npc_colossi_javelin_01_range_attack (10892), 90 frames, 3 game ticks. Sounds: range_attack_grab, spear swoosh.
- attack2 rig+name: npc_colossi_javelin_01_artillery_attack (10893), 90 frames, 3 game ticks. Sounds: artillery_attack_grab, spear swoosh.
- death rig+name: npc_colossi_javelin_01_death (10894), 120 frames, 4 game ticks. Sounds: spear_drop, growl, body_impact.
- spawn rig+sound: npc_colossi_javelin_01_walkfade (10891), walk with a fade; sound javelin_colossi_appear_01.
  The name says walk+fade; only the frame sound states the spawn (closer: was rig+name).
- walk alternate rig+name: npc_colossi_javelin_01_walk (10890), not the bound walk.
- special: none. defend: none (no javelin-named block sequence on the rig).

## Unknown (tier rig, 17 rows)
Every other sequence on 2245 is named for finalboss (Sol) or shockwave. They share the rig but belong to other monsters by name,
so they are `unknown` and are not offered as javelin roles (including the shockwave death 10895, which the human-name rule excludes).

## Graphics and projectiles (tier name, role unknown)
- Javelin named: 2673 spearhead (anim 30 client ticks), 2674 artillery_slow (60), 2675 artillery_fast (30), 2676 artillery_fire (30),
  2677 spearhead_fire_slow (60), 2678 spearhead_fire_fast (30). Models 52586 and 52587.
- Generic colossi word only: 2666 colossi_01_land (anim is finalboss_01_land, 90; probably Sol's), 2722 colosseum_colossi_explosion_01 (60).
- Which spotanim goes with range or artillery attack is not stated: the names suggest slow/fast variants of one projectile.

## Ledger disagreements (npc_combat/c/*.combat)
- javelin_colossus: death npc_colossi_javelin_01_death and attack npc_colossi_javelin_01_range_attack agree with the rig and the name (a1).
  defend is null (agrees: no candidate). Sounds are `-` with in-band sounds named in the comment. The ledger does NOT list
  artillery_attack as a second attack: 0 wrong rows, 1 omission.
- colossi_gib: all slots `-`, matches the no-rig finding. 0 disagreements.

## Only a recording, plugin constant or picture can settle
- When the range attack versus the artillery attack is used, and the projectile id for each.
- Whether the walkfade is the spawn entrance.
- Attack speed and range (not in the cache). Defend animation, if any.
- The spotanim for the javelin projectile and its speed. The gib's purpose (death debris, likely shared with sol_gib).
