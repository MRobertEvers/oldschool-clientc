# Rig pass: jad (JalTok-Jad and Yt-HurKot, wave 67 and final wave)

Method: docs/minigames/waves_loop/RIG_PASS.md. Documents only; nothing run or rewritten.
Rows: jad.tsv (30 rows: 9 per Jad npc, 6 per healer npc; no spotanim rows, see Graphics).

## The npcs (cache_npc.txt)
- inferno_jad 7700 (:301) and inferno_jad_finalwave 7704 (:441): JalTok-Jad, size 5, model 33012,
  readyanim jaltokjad_ready, walkanim jaltokjad_walk.
- inferno_jad_healer 7701 (:337) and inferno_jad_healer_finalwave 7705 (:475): Yt-HurKot, models
  9326/9328/9327, readyanim lizard_cleric_ready, walkanim lizard_cleric_walk (no size field).

## The rigs (ready and walk agree for each npc)
- Jad: framemap 1692, 9 sequences, all jaltokjad_*. Closed; no name narrowing.
- Healer: framemap 163, 6 sequences, all lizard_cleric_*. Closed; no name narrowing.

## Candidates by tier: bound 8, rig+name 18, rig 4, name 0
Jad (both npcs; game ticks):
| id | name | role | tier | ticks |
|---|---|---|---|---|
| 7589 / 7588 | jaltokjad_ready / walk | ready / walk | bound | 2.2 / 3.2 |
| 7590 | jaltokjad_attack_melee | attack (melee) | rig+name | 2.4 |
| 7592 | jaltokjad_attack_magic | attack (magic) | rig+name | 5.3 |
| 7593 | jaltokjad_attack_ranged | attack (ranged) | rig+name | 1.8 |
| 7591 | jaltokjad_defend | defend | rig+name | 1.8 |
| 7594 | jaltokjad_death | death | rig+name | 5.1 |
| 8857, 8858 | jaltokjad_walk_pet, _chathead_pet | unknown | rig | 1.1, 5.0 |
Healer (both npcs):
| 2636 / 2634 | lizard_cleric_ready / walk | ready / walk | bound | 4.4 / 4.3 |
| 2637 | lizard_cleric_attack | attack | rig+name | 1.3 |
| 2635 | lizard_cleric_defend | defend | rig+name | 2.0 |
| 2638 | lizard_cleric_death | death | rig+name | 669.7 (see below) |
| 2639 | lizard_cleric_heal | other (heal) | rig+name | 1.3 |

- Special, spawn, transition: none on either rig.
- Frame sounds: none on any of the 15 sequences.
- Attack priorities: magic and ranged carry forcedpriority 10 and priority 2, melee forcedpriority 9;
  which is used when is not on the rig.
- lizard_cleric_death: last frame is about 20,000 client ticks, so the sequence totals 669.7 game
  ticks (20,090 client). A death that plays to its end needs a cut-off by the engine.

## Unknown
- 8857 jaltokjad_walk_pet and 8858 jaltokjad_chathead_pet: on Jad's rig, but the "pet" name words
  point at the JalRek-Jad pet, not the combat npc (tier rig, role unknown).

## Graphics (tier name)
- No spotanim in cache_spotanim.txt shares a name word with jad, jaltok, hurkot or lizard cleric
  (searched jad, tok, hur, kot, lizard, cleric, healer; hits were unrelated). Tier name: none.
- Not name-matched, so NOT candidates by the method: the Jad set that AV_INVENTORY.md:128 takes from
  the code: tzhaar_fire_spit_launch 447 (anim tzhaar_fire_spit_launch, 8 frames, 1.1 ticks),
  tzhaar_fire_spit_travel 448, _follow_travel 449, _end_travel 450 (anim tzhaar_fire_spit, 8 frames,
  1.1 ticks), tzhaar_rock_smash 451 (16 frames, 2.2 ticks), firewave_impact 157 (anim wave_impact,
  8 frames, 0.8 ticks). None has a frame sound. Settled only by a picture or a plugin constant.

## Ledger (OSRS-Content/osrs239-content/npc_combat/i/, four files)
- inferno_jad and _finalwave: death jaltokjad_death (a2), defend jaltokjad_defend (a2): on rig,
  right monster. attack jaltokjad_attack_melee (a3, ranked 3): on rig, right monster, but a3 is a
  rank, not evidence; magic (7592) and ranged (7593) are the boss's other attacks, so a melee
  default is a weak pick for a monster known for magic and ranged. Flagged, not a disagreement.
- inferno_jad_healer and _finalwave: death, attack, defend lizard_cleric_* (a2): on rig, right monster.
- All six sounds "-": consistent with no frame sounds.
- Disagreements counted: 0.

## Only a recording, plugin constant or picture can settle
- Jad's attack-to-style mapping and the animation-to-hit timing (the 9-tick rate is not on the rig).
- Which spotanims Jad fires and their offsets; the healer's heal graphic, if any.
- The healer death cut-off length.
