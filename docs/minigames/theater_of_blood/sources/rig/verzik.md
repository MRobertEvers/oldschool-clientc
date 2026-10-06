# Verzik Vitur rig join (rig pass 1, 2026-10-03)

Source: out/osrs239_anims catalog via tools/gen_npc_combat.py loaders (read only), cache_npc_verzik.txt, all.seq, all.spotanim.
Table: verzik.tsv (150 rows: 126 seq rows over 75 distinct sequences, 24 spotanims). Normal-mode npc ids get rows;
Entry (_story) and Hard (_hard) records were checked and have identical ready/walk/size, so they share the rows
(ids listed in each row's note). Lil' Maiden/Bloat/Nylo/Sot/Xarp pets (10870-10874) belong to other rooms; not rowed.

## Rigs (framemap = ready and walk both on it unless stated)
- 1796: P1 and seated Verzik (8369, 8370): 5 seqs. - 1808: P1->P2 transition (8371) and P2 (8372): 6 seqs.
- 1810: P2->P3 transition (8373), P3 (8374), Lil' Zik (8337): 19 seqs; 7 are nylocas_queen_* (another monster) -> tier rig.
- 1809: death bat (8375): 1 seq (8129, its readyanim and walkanim). - 1800: melee spider 8381 (shared with hosdun, araxyte,
  crystal and sote spiders; narrowed to top_spider_melee_*, tier name). 1801 ranged 8382, 1799 magic 8383: own rigs.
- 1798 Athanatos 8384 (4 seqs); 1821 Matomenos 8385 (4); 1818 creeper 8386 (4, readyanim is tob_shadow_projectile).
- No readyanim, no rig: web 8376, pillars 8377-8379, throne 8380; their rows are name-only (verzik_webspin 8130 fm 1817,
  verzik_pillar_collapse 8052 / fade 8104 fm 1802, throne_transform 8108 / _initial 8053 fm 1807). Bases 14795-14798: no anims.

## Attack, special, spawn, death candidates (tier)
- P1 (1796): attack 8109 phase1_attack_magic, defend 8110, death 8111 (rig+name). 8051 verzik_human_idle: ready by name, use unknown.
- P2 (1808): spawn 8112 (bound: transition record's ready+walk), attack 8114 magic, 8116 melee, special 8117 heal, death 8118 (rig+name).
- P3 (1810): spawn 8119; attack 8123 melee, 8124 magic, 8125 ranged; special 8126 powerblast, 8127 webspin, 14406 summon;
  death 8128 death_a (all rig+name). Bat 8129 death_b is bound as ready only; the bat has no death or attack seq.
- Spiders: melee 8004 attack, 8005/8006 death, 8075/9030 spawn (name tier, shared rig); ranged and magic spiders on own rigs:
  attack, meleeattack, death, death_detonate, _quiet variants (rig+name). Athanatos 8078 death, 8079 spawn; Matomenos 8097 death, 8098 spawn.
- Creeper 1818: 9004 spawn, 9005 despawn, 13134 idle-named, 8100 (bound ready/walk).
- Totals by tier: bound 22, rig+name 72, name 14, rig 18, rig+sound 0.

## Stays unknown
- 18 rig-only rows: the 7 nylocas_queen_* seqs on 1810 (x2 for pet/transition rows share), 8130 on the web npc.
- Which sequence the Athanatos cast, the Matomenos summon and the P3 ball launch play: no name states them.
- Who plays 8051, 8135 (pet chathead), 8122/13135 (pet idle/walk) inside the fight.
- Priorities: only forcedpriority=6 on death_b; no hand-item hints on this rig.

## Spotanims (tier name; graphics have their own rigs)
24 rows. Seq lengths per row. Not Verzik's own anims: 1580 and 1594 and 1596 use zuk_proj (7571); 1586 lotr_tarn_mutant_proj_spot;
1587 and 1588 ds2_galvek_water_launched; 1589 and 1595 tol_homonculus_magic_spotanim; 1590 and 1597 druid shield; 1598 fossil lava bomb;
1599 strike_impact; 1600 blast_impact. 1583 and 1593 (ranged projectiles) carry no anim=. Verzik-named anims: 1581/1582 verzik_lightning_impact 8132,
1584 8131, 1585 8115, 1591 8133, 1592 8134, 1601 8130 (webspin), 1602 8100.

## Ledger disagreements (npc_combat/<v|t>/*.combat; no row names an off-rig animation; none uses a human default)
- verzik_death_bat 8375 (and _story, _hard): attack_anim = verzik_phase3_death_b (a4 guess, forcedpriority); it is the bat's ready anim. death_anim is the same seq.
- tob_verzik_phase2_bloodnylocas 8385 (+variants): attack_anim = elemental_spawn (a spawn seq as attack, a4 guess); the rig has no attack.
- tob_verzik_creeper 8386 (+variants): attack_anim = tob_shadow_projectile_spawn, death_anim = _despawn (a4 guesses; roles by name are spawn and despawn).
- tob_verzik_phase2_armourednylocas_story 10844 and _hard 10861: attack_anim = tob_spider_tank_spawn; ~~the Normal 8384 record is authored null (no attack).~~ [closer: struck as stated -- the null is authored in the ledger (t/tob_verzik_phase2_armourednylocas.combat:16) and in tob.npc:2484, but npc_anims.generated.npc:44992 still carries attack_anim tob_spider_tank_spawn for the Normal record, and that file loads after tob.npc and overlays it (torirs_server_content.c:2030-2041). All three modes get the spawn as their attack.]
- Phase transition and P3 records give one attack (P3: melee) of three; defend_anim null on P2/P3 (P1 only has 8110): consistent with the rig.

## Spec disagreements (encounters/verzik.tsv .av. rows)
All 66 .av. rows' quoted lengths and frame sounds match the rig table; every named sequence exists. Only tier issues:
- verzik.av.p3_death.throne_seq (A): ~~8108 is on framemap 1807, bound to no npc, role by name only (tier name); the cache gives only 8053 on the loc. Deserves D.~~ [closer: struck in part -- framemap 1807 is the throne loc's own rig: it holds exactly 8053 (the loc's bound anim=) and 8108 (out/osrs239_anims/framemap_seqs.csv). So 8108 is rig+name for the loc, not name only: the row's A holds for 8053 and is one step high for 8108, which deserves C, not D.]
- verzik.av.reds_summon.seq (D): 8117 is named verzik_phase2_heal; "summon" is not stated by the rig (rig+name says heal).
- verzik.av.p2_purple.seq (C): 8114 is named attack_magic on rig 1808; that the Athanatos cast uses it rests on plugin tags only (D without them).
- verzik.av.p3_death.bat (A): fine for readyanim 8129 (bound), not for any death claim.
- verzik.av.p2_spawn.seq (A): bound as ready/walk of 8371; "transition" is by name.

## Unused candidates (no spec row, no script in minigame_tob)
- Spiders: 14387/14388 melee _quiet; ranged ~~7998 death~~, 8001 meleeattack, 14389-14391; magic 7990, ~~7991~~, 14392-14394. [closer: 7998 and 7991 struck -- they are the ledger death_anims of verzik_nylocas_ranged / verzik_nylocas_magic (top_spider_ranged_death / top_spider_magic_death), so they play.]
- 8098 elemental_spawn (Matomenos spawn), 9004/9005 creeper spawn/despawn. ~~8110 phase1_defend only appears in a spec text, no script.~~ [closer: struck -- 8110 is the ledger defend_anim of verzik_initial and verzik_phase1 (v/verzik_phase1.combat, npc_anims.generated.npc:49248), so the engine plays it on a hit.]

## Only a recording or plugin constant settles
Athanatos/Matomenos cast seqs, whether spiders play meleeattack/_quiet variants, Matomenos and creeper spawn seqs,
whether 8117 is the summon, and 8051/8135.

## Closer audit (rig-tob closer, 2026-10-03)
(1) verzik_death_bat.combat:15-16 gives verzik_phase3_death_b (8129, the only seq on 1809) as both death and attack: holds. (2) The throne tier: struck in part (above). (3) armourednylocas: amended (above). Unused: 7998, 7991 and 8110 struck (above). Also checked: 8109/8110/8111 on 1796, 8112/8114/8117/8118 on 1808, and 8119/8126/8127/8128/14406 on 1810.
