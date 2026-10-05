# Rig pass: sol_heredit (Colosseum)

Documents only. Table: `sol_heredit.tsv` (68 rows). Method: `docs/minigames/waves_loop/RIG_PASS.md`.

## The npcs (cache_npc.txt)
- colosseum_sol_p1 (12821, size 5): ready `npc_colossi_finalboss_01_idle`, walk `..._01_walk`. Rig framemap 2245.
- colosseum_boss_seated (12827, size 4): ready and walk `npc_colossi_finalboss_01_sitting_idle`. Rig 2245.
- colosseum_solar_flare (12826): ready and walk `npc_colosseum_solar_flare_crystal_idle_01`. Rig 2226 (one sequence).
- colosseum_sol_gib (12832): no ready, walk, rig or sequence. Nothing to join; no candidate.

## The rig
Framemap 2245 carries 24 sequences: all skeletal (length from mayarange, 30 client ticks per game tick). It is
shared by the three Varlamore colossi (Sol, javelin, shockwave), so the join narrows by name words.
10 sequences state Sol (`finalboss`, `tripleattack`), 14 state the javelin or shockwave colossus (tier rig, role
unknown, not Sol's).

## Candidates (rows per npc: p1 and seated 24 each, flare 1, gib 0; spotanims 9 each for p1 and seated)
Tier counts: bound 4, rig+name 19, rig 26, name 18, none 1.
- attack1 `npc_colossi_finalboss_01_melee_attack` (10882, 4.0 ticks, no sound), rig+name.
- attack2 `..._01_melee_attack_telegraph` (10883, 6.0 ticks), rig+name.
- attack3 `..._01_grapple_attack_telegraph` (10884, 4.0 ticks), rig+name.
- attack4 `..._01_shieldslam_telegraph` (10885, 4.0), attack5 `npc_colossi_finalboss_tripleattack` (10886, 12.0) and
  attack6 `_tripleattack_shorter` (10887, 11.0), rig+name. Closer: these were `special`; the names state an attack and
  nothing states special, so they are attack variants until Blert or a plugin says which are specials.
- transition `..._01_arena_jump` (10876, 3.0), `..._01_arena_land` (10877, 3.0), rig+name.
- death `npc_colossi_finalboss_01_death` (10888, 8.33 ticks, boss death sounds), rig+name.
- spawn: none found. defend/flinch: none on the rig names one. A seated-to-standing change has no named sequence.

## Graphics (tier name; own model and rig)
- Triple attack: spotanims 2667/2668 `npc_colossi_colossi_tripleattack_01_telegraph{,_shorter}` play seqs of 12.0 and
  11.0 ticks, equal to Sol's tripleattack lengths: strong hint they pair, still only a name tier.
- 2666 colossi_01_land (3.0 ticks, equals arena_land); 2669-2672 finalboss_01..04_melee (explosion seq, 1.0 tick;
  four variants, likely the four melee hits); 2680 finalboss_01_death (8.33 ticks, equals the death); 2724
  finalboss_explosion_01 (2.0 ticks).
- Solar flare: no spotanim name shares "solar" or "flare". The crystal beam spotanims (2689-2697) share only the word
  crystal from its idle name, so are not listed.

## Ledger disagreements (npc_combat/c/*.combat), 2 npcs, 2 rows each
- colosseum_sol_p1 and colosseum_boss_seated: death_anim = `npc_colossi_javelin_01_death` (a3), attack_anim =
  `npc_colossi_javelin_01_range_attack` (a3). Both ARE on rig 2245 but belong to the javelin colossus by name; the
  rig offers Sol's own death (10888) and melee attack (10882). attack_sound/death_sound inherit the javelin ones.
  defend_anim null: consistent, nothing on the rig names a defend.
- sol_gib and solar_flare: ledger nulls, no disagreement.

## Stays unknown
- Which of attack1-3 and the specials fire in which phase, the grapple vs shield slam damage and timing, and how
  the four melee explosion graphics map to hits: need Blert/plugin constants or a recording.
- Whether 10875 sitting idle is the only seated animation, and what plays on standing up.
- Whether sol_gib has any animation (it has no rig): likely a model-only spotanim-like npc.
- The solar flare's attack visual is a graphic, not a sequence; not found by name.
