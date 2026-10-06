# Maiden rig join (ToB room "maiden"), 2026-10-03

Scratch script: build/rig_state/matthew-mbp-m4-raid-b1-rig-tob/maiden_rig.py (imports tools/gen_npc_combat.py loaders; catalog out/osrs239_anims). Table: maiden.tsv (23 rows).

## NPCs (cache_npc_maiden.txt, 25 records, 3 modes)
- Body, 6 per mode: tob_maiden_100/70/50/30/dying_a/dying_b = ids 8360-8365, _story 10814-10819, _hard 10822-10827. Name "The Maiden of Sugadinti", size 6, ready=walk=maiden_idle. Framemap 1812.
- maiden_elemental 8366 / story 10820 / hard 10828 (cache name Nylocas Matomenos). ready elemental_idle, walk elemental_walk. Framemap 1821.
- maiden_blood_slug 8367 / story 10821 / hard 10829 (Blood spawn). ready tob_blood_spawn_idle, walk tob_blood_spawn_walk. Framemap 1814.
- maiden_transmog 11958: no readyanim, no rig (nothing to join).
- Ready and walk of each npc sit on the same framemap. All three rigs are tiny and room-specific (7, 4, 4 sequences), so the join is closed, not name-narrowed.

## Candidates: 15 sequences on rigs, 8 spotanims by name
Maiden, framemap 1812 (7):
- 8090 maiden_idle (20f, 4 t) ready+walk, bound.
- 8091 maiden_attack_blood (3 t) attack 1, rig+name; frame sound 4002 tob_maiden_blood_attack_part_2.
- 8092 maiden_attack_special (5 t) special/attack 2, rig+name; sounds 3293, 3234 (tob_maiden_shadow_attack*). Its name says "special"; that it is the blackstorm is by name only.
- 8093 maiden_death_a (3 t) death, rig+name; sounds 3955, 3942, 3958.
- 8094 maiden_death_b (4 t) death, rig+name; sound 3958 f1.
- 14399 maiden_spawn (5 t) spawn, rig+name; sound 11863 has no Jagex name (synth_11863).
- 14401 myq6_maiden_death_b (3.07 t) death, rig+name, a quest (MYQ6) variant.
Matomenos, framemap 1821 (4): 8095 walk and 8096 ready (bound); 8097 elemental_death (1.97 t) death rig+name; 8098 elemental_spawn (1.80 t) spawn rig+name.
Blood spawn, framemap 1814 (4): 8101 walk, 8102 ready (bound); 8103 tob_blood_spawn_death (3 t) death rig+name; 9944 projectile_muspah_attack_ranged_01 (5.33 t) role unknown, tier rig (name is another monster's).
Spotanims (tier name, role unknown): maiden_shadow_proj 1577 (anim tob_shadow_projectile, 7 t), maiden_blood_proj 1578 (anim blood_blitz_travel, 0.37 t: a spell's travel anim), maiden_lingering_blood 1579 and _sw/_nw/_ne 3982-3984 (anim tob_blood_splat 8099, 10 t), vfx_maiden_spawn 3985 (3.4 t), vfx_myq6_tob_maiden_pool_well_spawn_01 3986 (2.7 t, quest).

## Attack, special, spawn, death tiers
- Attack: 8091 rig+name, 8092 rig+name (special by name only). Nothing else on her rig.
- Spawn: maiden_spawn 14399, elemental_spawn 8098: rig+name.
- Death: maiden_death_a/b, elemental_death, tob_blood_spawn_death: rig+name. Which of the two Maiden deaths plays on which body is a script/recording fact, not a rig fact.
- No defend sequence on any of the three rigs (no block/defend name); ledger correctly says null.

## Stays unknown
- Whether maiden_spawn plays at all (the Maiden appears; the spec says nothing, and the script never plays it).
- Whether elemental_spawn plays on a Matomenos (spec crab_spawn.npc: "appear with no animation").
- Whether projectile_muspah_attack_ranged_01 is ever played by a blood spawn (spec: no attack).
- 8103 blood spawn death: plays when? (no spec row).
- Sound names on shared frame sounds are borrowed families: shadow projectile seq carries leagues_5_thermy_tornado_wind_*; elemental_death carries tob_nylocas_melee_death_shriek_2 (3980). Name-borrowing, so a sound's Jagex name here states nothing about the Maiden.

## Ledger disagreements (npc_combat, all three modes, 3 files each)
- maiden_blood_slug(+_hard,_story): attack_anim = projectile_muspah_attack_ranged_01. On its rig but the name belongs to another monster (Muspah projectile); "rig's only attack seq" is resemblance. The blood spawn has no attack in the spec. Should be null.
- maiden_elemental(+_hard,_story): attack_anim = elemental_spawn. The seq is a spawn animation (name), chosen by "only forcedpriority=6 seq". Should be null.
- tob_maiden_dying_a/_dying_b (all modes): ledger gives death_anim maiden_death_a to both, while the room script plays maiden_death_b on 8365 itself; and every living body 100/70/50/30 also carries death_anim maiden_death_a/attack maiden_attack_blood, which is fine by rig but attack_special is not represented (script plays it directly).
- maiden_transmog: all "-"; correct (no rig).
- Every other ledger name (maiden_attack_blood, maiden_death_a, tob_blood_spawn_death, elemental_death) is on the correct rig.

## Spec disagreements (encounters/maiden.tsv .av. rows)
Every sequence named by a row is on the right npc's rig: 8090 (A) on 1812, 8091 and 8092 (C), 8093 (C), 8094 (D) on 1812, 8097 (D) on 1821, 8101/8102 (A) on 1814. No off-rig row. Tier notes, not disagreements:
- maiden.av.blackstorm.seq (grade C) and death_b (D): the "special = blackstorm" and "which death" bindings rest on rig+name plus plugin/recordings; the rig alone only gives tier rig+name.
- maiden.av.idle.seq and slug.walk_idle_seq grade A are bound (record names them): correct.
- maiden.av.blood_throw.pool_gfx note says tob_blood_splat is seq 8099: matches cache_seq.txt line 60.
- maiden.av.crab_death.seq 8097: grade D matches rig+name.

## Unused candidates (no spec row, no room script id/name)
- 14399 maiden_spawn (spawn, rig+name). [closer: holds as unused, but the id sits in the 14399-14401 cluster with myq6_maiden_death_b and the vfx_myq6_tob_maiden_pool_well_spawn_01 graphic, so it is most likely the quest's rising, not the raid's; not filed as a content bug.]
- 8098 elemental_spawn (spawn, rig+name)
- ~~8103 tob_blood_spawn_death (death, rig+name)~~ [closer: struck -- it is played: it is the ledger's death_anim (npc_combat/m/maiden_blood_slug.combat:15), compiled into npc_anims.generated.npc [maiden_blood_slug], and the server overlays that block onto tob.npc's (torirs_server_content.c:2030-2041).]
- 14401 myq6_maiden_death_b (quest variant)
- 9944 projectile_muspah_attack_ranged_01 (role unknown)
Scripts use maiden_attack_special, maiden_attack_blood, maiden_death_b, elemental_death; death_a via the body's death_anim.

## Only a recording or plugin constant can settle
Whether the Maiden plays maiden_spawn on entry, whether Matomenos and blood spawns play elemental_spawn/death 8103, which death sequence runs on which body, and the real tick offsets of 8092's frame sounds against the blackstorm.

## Closer audit (rig-tob closer, 2026-10-03)
Three checks against out/osrs239_anims/framemap_seqs.csv, the ledger and the script: (1) 9944 projectile_muspah_attack_ranged_01 is on 1814, the blood spawn's rig, and maiden_blood_slug.combat:16 names it as the attack: holds. (2) 8098 elemental_spawn is on 1821, and maiden_elemental.combat:16 names it as the attack: holds. (3) tob.rs2:560-561 changes the type to tob_maiden_dying_b and then plays maiden_death_b: holds. That one is not a rig fault, because both deaths are on 1812. One strike (8103, above).
