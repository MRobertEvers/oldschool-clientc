# Rig pass: ranger (inferno_creature_ranger 7698, inferno_ranger_finalwave 7702; Jal-Xil)

Method: docs/minigames/waves_loop/RIG_PASS.md. Documents only; nothing run or rewritten.

## The npcs
- Both are Jal-Xil, size 3, model 33014, readyanim jalxil_ready, walkanim jalxil_walk
  (cache_npc.txt:232 and :374). Same cache record content; two ids.

## The rig
- One framemap, 1693, for both ready and walk and for both npcs. It carries exactly 6
  sequences, all jalxil_*. Closed; no name narrowing needed.

## Candidates (6 seq + 1 spotanim per npc = 14 rows in ranger.tsv)
| id | name | frames | game ticks | role | tier |
|---|---|---|---|---|---|
| 7602 | jalxil_ready | 18 | 3.0 | ready | bound |
| 7603 | jalxil_walk | 20 | 2.0 | walk | bound |
| 7604 | jalxil_attack_melee | 21 | 2.0 | attack (melee) | rig+name |
| 7605 | jalxil_attack_ranged | 31 | 3.0 | attack (ranged) | rig+name |
| 7606 | jalxil_death | 30 | 3.0 | death (forcedpriority 10) | rig+name |
| 7607 | jalxil_defend | 8 | 1.0 | defend | rig+name |

- No special, spawn or transition candidate. No frame sounds on any of the six.
- Two attack variants: melee and ranged. Which one the monster uses when is not on the rig.

## Graphics (tier name)
- spotanim 1377 inferno_xil_projectile: model 33013, no anim field (static). Named for Jal-Xil by Jagex name and referenced by inferno_adds.rs2:278; probably the ranged projectile, candidate only.

## Unknown
- Nothing on the rig is left unclassified.

## Ledger (OSRS-Content/osrs239-content/npc_combat/i/inferno_creature_ranger.combat; finalwave identical)
- death jalxil_death (a2), defend jalxil_defend (a2): on rig, right monster. No disagreement.
- attack jalxil_attack_melee (a3, ranked 2): on rig, right monster, but a3 is a rank, not evidence;
  the ranged variant (7605) is the rig's other attack. Not a rig/ownership disagreement; flagged.
- attack/defend/death sounds all "-": consistent, no frame sounds exist.
- Disagreements counted: 0.

## Only a recording, plugin constant or picture can settle
- Which attack id fires at which range/style, and the animation length-to-hit timing.
- Projectile model/offsets (the spotanim carries its own rig).
