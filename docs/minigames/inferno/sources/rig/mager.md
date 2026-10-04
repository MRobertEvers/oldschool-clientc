# Rig pass: mager (inferno_creature_mager 7699, inferno_mager_finalwave 7703; Jal-Zek)

Method: docs/minigames/waves_loop/RIG_PASS.md. Documents only; nothing run or rewritten.

## The npcs
- Both are Jal-Zek, size 4, model1 33000, readyanim jalakxil_ready, walkanim jalakxil_walk
  (cache_npc.txt:266 and :406). Same record content; two ids.

## The rig
- One framemap, 1686, for ready and walk and for both npcs. It carries exactly 6 sequences,
  all jalakxil_*. Closed; no name narrowing needed.

## Candidates (6 seq + 1 spotanim per npc = 14 rows in mager.tsv)
| id | name | frames | game ticks | role | tier |
|---|---|---|---|---|---|
| 7609 | jalakxil_ready | 12 | 1.0 | ready | bound |
| 7608 | jalakxil_walk | 30 | 2.0 | walk | bound |
| 7610 | jalakxil_attack_magic | 30 | 2.0 | attack (magic) | rig+name |
| 7612 | jalakxil_attack_melee | 30 | 2.0 | attack (melee) | rig+name |
| 7611 | jalakxil_resurrect | 72 | 6.0 | special | rig+name |
| 7613 | jalakxil_death | 30 | 2.0 | death (forcedpriority 10) | rig+name |

- Special: resurrect is classified special on its Jagex name; whether it is the mager's
  revive-the-dead cast is a candidate reading, not a fact.
- No spawn, defend or transition candidate on the rig. No frame sounds on any sequence.
- Two attack variants (magic, melee); which fires when is not on the rig.

## Graphics (tier name)
- spotanim 1376 inferno_zek_projectile: model 33007, anim zuk_proj (12 frames, 2.0 game ticks),
  resize 90. Named for Jal-Zek by Jagex name and referenced by inferno_adds.rs2:353; the anim is
  Zuk-named and its rig is not joined, so candidate only.

## Unknown
- Nothing on the rig is left unclassified. No defend animation exists on the rig.

## Ledger (OSRS-Content/osrs239-content/npc_combat/i/inferno_creature_mager.combat; finalwave same)
- death jalakxil_death (a2), attack jalakxil_attack_magic (a3, ranked 2): on rig, right monster.
- defend null (a5): consistent, rig has none. All sounds "-": consistent.
- Both files say NOT COMPILED: the authored inferno.npc block wins.
- Disagreements counted: 0.

## Only a recording, plugin constant or picture can settle
- Which attack id fires at which range/style; resurrect's use; projectile timing and offsets.
