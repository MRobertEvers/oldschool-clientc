# Theatre of Blood: rig audit

Pass: matthew-mbp-m4-raid-b1-rig-tob, closer, 2026-10-03. Documents only.

The per-room joins are in `sources/rig/<room>.tsv`, and each has a `.md` with the worker's findings and the closer's strikes. The rig catalog is `out/osrs239_anims/framemap_seqs.csv` and `npc_rigs.csv`, read with the loaders in `tools/gen_npc_combat.py`.

Paths below are short forms:
- `npc_combat/` means `OSRS-Content/osrs239-content/npc_combat/`.
- `gen.npc` means `OSRS-Content/osrs239-content/server/scripts/npc/configs/npc_anims.generated.npc`.
- `tob.npc` and the `.rs2` files are under `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/`.

**STALE (seam11): the paragraph below is wrong.** The server loads every `*.generated.npc` first and every authored `.npc` second (`torirs_server_content.c` `load_npc_generated_config`, then `load_npc_authored_config`; cachepack ranks generated 1 and authored 2), so an authored value, `null` included, always wins; lines 2030-2041 are the per-header seed. The Athanatos null held in all three modes before any edit. The L rows were real for a different reason: `tob.npc` never stated an attack for those records. Seam11 authored the nulls (CONTENT_BUGS.md, seam11).

**Why a generated row reaches the game.** The server reads every `[gameval]` block for an npc and lays each one over the same record, in directory order (`src/torirsserver/torirs_server_content.c:2030-2041`). `minigames/` sorts before `npc/`, so `gen.npc` is applied after `tob.npc`. A generated `attack_anim` therefore replaces anything `tob.npc` states, including an authored `null`. The `NOT COMPILED` banner on `tob_bloat.combat:13` describes an older loader.

The three lists:
- **LEDGER**: a generated or authored combat record names an animation the npc cannot play in that role.
- **SPEC**: an `.av.` row names a sequence that is off the npc's rig, or grades it above its tier.
- **UNUSED**: a rig animation whose name states a role that our content never plays.

## Maiden

**Rigs:**
- 1812, the Maiden, all bodies: 7 sequences.
- 1821, the Matomenos: 4 sequences.
- 1814, the blood spawn: 4 sequences.
- `maiden_transmog` has no rig.

All three rigs are closed sets.

**LEDGER**
- L1. maiden_blood_slug (+_story, _hard) `attack_anim` = projectile_muspah_attack_ranged_01 (9944). The sequence is on 1814 but carries another monster's name. The rig offers no attack, so the field should be null. Evidence: npc_combat/m/maiden_blood_slug.combat:16, gen.npc:24470, sources/rig/maiden.tsv:16.
- L2. maiden_elemental (+_story, _hard) `attack_anim` = elemental_spawn (8098), which is the spawn sequence. The rig offers no attack, so the field should be null. Evidence: npc_combat/m/maiden_elemental.combat:16, gen.npc:24491, maiden.tsv:12.

**SPEC:** none. Every sequence the rows name is on the right rig, and each grade is at or below its tier.

**UNUSED**
- U1. 8098 elemental_spawn, the Matomenos spawn (rig+name). We spawn the Matomenos with no animation (encounters/maiden.tsv:100: "no source binds them"). The rig is that source. Evidence: maiden.tsv:12.
- U2. 14399 maiden_spawn, the Maiden spawn (rig+name). It is probably the quest's version, because it clusters with myq6_maiden_death_b 14401 and the vfx_myq6 graphic. Not filed. Evidence: maiden.tsv:7.

## Bloat

**Rigs:**
- 1823, Bloat in all three modes: 5 sequences. Closed set.

**LEDGER**
- L3. tob_bloat (+_story, _hard) `attack_anim` = tob_bloat_sleep (8082). This is the 33-tick down sequence (forcedpriority 8), not an attack. The rig offers no attack, so the field should be null. Evidence: npc_combat/t/tob_bloat.combat:20, gen.npc:44393, combat_stats.generated.npc:27273, bloat.tsv:4.

**SPEC:** none. bloat.av.down.seq C (encounters/bloat.tsv:116) is defensible.

**UNUSED:** none in the room. 9031 tob_bloat_walk_pet is the pet's walk.

## Nylocas

**Rigs:**
- 1800, the melee form: shared, 38 sequences, 9 of them top_spider_*.
- 1799, the magic form: closed, 9 sequences.
- 1801, the ranged form: closed, 9 sequences.
- The support npc has no rig.

**LEDGER:** none (checked across 55 files).

**SPEC:** none.

**UNUSED**
- U3. 7990 top_spider_magic_meleeattack: the magic form's own melee swing (rig+name). Evidence: nylocas.tsv:68.
- U4. 8001 top_spider_ranged_meleeattack: the ranged form's own melee swing (rig+name). Evidence: nylocas.tsv:53.
- U5. The `_quiet` attacks 14387 and 14390-14393 (rig+name). What "quiet" means is unknown: these may belong to a quest or a mode. Not filed. Evidence: nylocas.tsv:38, nylocas.tsv:55.
- U6. The `_quiet` deaths 14388, 14389 and 14394 (rig+name). Same unknown. Not filed. Evidence: nylocas.tsv:39.

## Sotetseg

**Rigs:**
- 1822, the boss in all six records: 6 sequences.
- 1818, the creeper: 4 sequences.

Both are closed sets.

**LEDGER**
- L4. tob_sotetseg_creeper (+_story, _hard) `attack_anim` = tob_shadow_projectile_spawn (9004), which is the spawn sequence. The rig offers no attack, so the field should be null. Evidence: npc_combat/t/tob_sotetseg_creeper.combat:16, gen.npc:44934, sotetseg.tsv:63.

**SPEC**
- S1. sotetseg.av.maze.boss_seq names 8142 tob_sotetseg_shadow_portal. That sequence is on framemap 1830, the exit loc 33037's animation, not on the boss rig 1822. Do not claim it as his sequence: re-scope it to the exit loc, or mark the boss's maze animation unknown. Our script plays it on the boss at tob_sotetseg.rs2:1020. Evidence: encounters/sotetseg.tsv:79, sotetseg.scope.tsv:84, sotetseg.tsv:9.

**UNUSED**
- U7. 9004 tob_shadow_projectile_spawn: the creeper's appear (rig+name). It is only "played" as an attack, which the creeper never makes. Evidence: sotetseg.tsv:63.
- U8. 9005 tob_shadow_projectile_despawn: the creeper's vanish (rig+name). Our creepers leave by `npc_del` (tob_sotetseg.rs2:1594-1684), so it never plays. Evidence: sotetseg.tsv:64.

## Xarpus

**Rigs:**
- 1811, every form (static, feeding, combat, the dead body) and the pets: 7 sequences. Closed set.
- tob_stranger_xarpus is on the shared human rig.

**LEDGER:** none.
- x/xarpus_death `death_anim` is death_a, while its record binds death_b. Both are on 1811, and the script plays death_b on the changed type itself (tob_xarpus.rs2:1660), so this is not a rig fault. Evidence: npc_combat/x/xarpus_death.combat:15.

**SPEC:** none above tier. xarpus.av.p2_start.seq D (encounters/xarpus.tsv:77) holds.

**UNUSED:** none in the room. 9033 is the pet's idle, and 8062 death_a plays through the ledger.

## Verzik

**Rigs:**
- 1796, P1 and the seated form: 5 sequences.
- 1808, the P1 to P2 transition and P2: 6 sequences.
- 1810, the P2 to P3 transition, P3 and Lil' Zik: 19 sequences, 7 of them nylocas_queen_*.
- 1809, the death bat: 1 sequence.
- The spiders use 1800, 1801 and 1799. The Athanatos uses 1798, the Matomenos 1821, and the creeper 1818.
- 1807 is the throne loc's rig.

**LEDGER**
- L5. tob_verzik_phase2_bloodnylocas (+_story, _hard) `attack_anim` = elemental_spawn (8098), which is the spawn sequence. The rig offers no attack, so the field should be null. Evidence: gen.npc:45013, verzik.tsv:123.
- L6. tob_verzik_creeper (+_story, _hard) `attack_anim` = tob_shadow_projectile_spawn (9004), which is the spawn sequence. The rig offers no attack, so the field should be null. Evidence: gen.npc:44973, verzik.tsv:125.
- L7. tob_verzik_phase2_armourednylocas (all three modes) `attack_anim` = tob_spider_tank_spawn (8079), which is the emerge sequence. The rig offers no attack. The authored null in the ledger and in tob.npc:2484 is overlaid by the stale gen.npc:44992. Evidence: npc_combat/t/tob_verzik_phase2_armourednylocas.combat:16, npc_combat/t/tob_verzik_phase2_armourednylocas_story.combat:16, verzik.tsv:119.
- L8. verzik_death_bat (+_story, _hard) `attack_anim` = verzik_phase3_death_b (8129). That is the bat's only sequence (117 frames), its ready and walk animation. The rig offers no attack, so the field should be null. Evidence: npc_combat/v/verzik_death_bat.combat:16, gen.npc:49093, verzik.tsv:81.

**SPEC**
- S2. verzik.av.p3_death.throne_seq is graded A for 8053 and 8108. 8053 is bound (the loc's anim=). 8108 verzik_throne_transform is on the same loc rig 1807, but its role comes from the name only. Grade 8108 C, or split it from 8053. Evidence: encounters/verzik.tsv:203, verzik.tsv:88.

**UNUSED**
- U9. 8098 elemental_spawn on the P2 Matomenos (8385), its spawn (rig+name). No script plays it. Evidence: verzik.tsv:123.
- U10. 9004 spawn and 9005 despawn on the Verzik creeper (8386) (rig+name). No script plays either. Evidence: verzik.tsv:125, verzik.tsv:126.
- U11. On verzik_nylocas_{ranged,magic,melee}: 8001 and 7990 meleeattack, plus the `_quiet` 14387-14394 (rig+name). These are the same sequences as U3-U6. Evidence: verzik.tsv:103, verzik.tsv:110, verzik.tsv:96.

## Notes (not counted)

- **Rows graded below their tier, which may be raised:** sotetseg ball.seq 8139, melee.seq 8138 and death.seq 8140 are rig+name but graded C/C/D. xarpus.av.spit.seq 8059 is rig+name and rig+sound but graded D.
- **verzik.av.reds_summon.seq (D):** 8117 is named verzik_phase2_heal. Only the plugins and the tick log make it the summon.
- **The same ledger faults outside the raid:** tobquest_bloat, tobquest_athanatos, myq6_maiden_*, the Bloat pets, deadman_breach_bloat, leviathan_tornado and toa_zebak_blood_cloud* carry the same a4 or "only attack" picks (gen.npc:6943, 22412, 28715, 44294, 45105, 45119).
- **Generator wording bug:** the a4 string always says "forcedpriority=6", for any value in FP_LIVE {6,7,8} (tools/gen_npc_combat.py:832). The `NOT COMPILED` banner logic (:591-600) contradicts the overlay loader.

Counts: LEDGER 8, SPEC 2, UNUSED 11.
