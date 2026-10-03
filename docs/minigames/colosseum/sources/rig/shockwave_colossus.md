# Rig pass: shockwave_colossus (colosseum)

Documents only; nothing built or run. Table: `shockwave_colossus.tsv` (27 rows). Method: `docs/minigames/waves_loop/RIG_PASS.md`.

## The npc
- colosseum_shockwave_colossus, npc 12819, "Shockwave Colossus", size 3, cache_npc.txt:406.
  readyanim npc_colossi_shockwave_01_idle (seq 10902, 90 frames, 3 game ticks);
  walkanim npc_colossi_shockwave_01_walk (seq 10881, 90 frames). No other animation field is set.

## The rig
- Ready and walk both sit on framemap 2245, one rig, 24 sequences, SHARED by every colossus (Sol Heredit's finalboss set,
  the javelin colossus, this one). The join does not close it; the Jagex names separate them.

## Counts
27 rows: 24 sequences, 3 spotanims. Tiers: bound 2, rig+name 3, rig 19, name 3.

## Candidates
- bound: npc_colossi_shockwave_01_idle (ready), npc_colossi_shockwave_01_walk (walk).
- attack1 rig+name: npc_colossi_shockwave_01_clapattack (10903), 90 frames, 3 game ticks. Sounds: open_arms, charge, whoosh, clap, projectile.
- death rig+name: npc_colossi_shockwave_01_death (10895), 120 frames, 4 game ticks. Sounds: shockwave_death_impact x2, growl.
- walk alternate rig+name: npc_colossi_shockwave_01_walk_nosound (10880): silent walk. The cache hands it to
  poh_solheredit_pet as its walkanim, so it is probably the pet's, not an arena variant.
- special, spawn, defend: none. No shockwave-named sequence states them. Colossi spawn is unstated (the javelin
  colossus has a walkfade; this one has none).

## Unknown (tier rig, 19 rows)
Every other sequence on 2245 is named for finalboss (Sol) or the javelin colossus; they stay `unknown` and are not offered.

## Graphics (tier name, role unknown)
- 2679 npc_colossi_shockwave_01_clapattack: the only shockwave-named spotanim; see tsv for its anim and length. A clap
  projectile or ground wave is suggested by the sound name "projectile" but not stated.
- 2666 colossi_01_land (anim is finalboss land, probably Sol's) and 2722 colosseum_colossi_explosion_01: generic colossi word only.

## Ledger (npc_combat/c/colosseum_shockwave_colossus.combat)
No disagreement. attack_anim = clapattack, death_anim = shockwave_01_death (both on the rig, both its own name).
defend_anim = null (nothing on the rig). Sounds are in-band.

## Only a recording, plugin constant or picture can settle
- How many ticks the clap takes to land, and which spotanim is the shockwave projectile and its speed.
- Whether any hit reaction exists (none on the rig).
- Death graphic, spawn style.
