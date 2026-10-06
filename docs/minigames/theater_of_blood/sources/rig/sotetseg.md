# Sotetseg rig join (2026-10-03, rig worker, documents only)

Table: sources/rig/sotetseg.tsv (78 rows; scratch scripts build/rig_state/matthew-mbp-m4-raid-b1-rig-tob/sote_rig.py, sote_write.py).

## NPCs (cache_npc_sotetseg.txt)
- Boss, six records: 8387/8388 (Normal noncombat/combat), 10864/10865 (Entry "_story"), 10867/10868 (Hard). name=Sotetseg, size 5, ready tob_sotetseg_idle, walk tob_sotetseg_walk (lines 1-6, 30-35, 80-85, 109-114, 159-164, 188-193).
- Creeper (tornado), three records: 8389, 10866, 10869. No name, size 3, ready AND walk tob_shadow_projectile (lines 61-67, 140-146, 219-225).

## Rigs
- Boss: framemap 1822, 6 sequences (ready and walk are on the same map). Closed set, not shared.
- Creeper: framemap 1818, 4 sequences. Closed set.
- Other Sotetseg-named sequences sit on three different maps: 8141 wall_float on 1829, 8142 shadow_portal on 1830, 8143 tile_glow on 1831, 8144 shared_projectile on 1819.

## Candidates: 6 (boss rig) + 4 (creeper rig) + 4 (off-rig by name) + 6 spotanims = 20 distinct
Boss rig 1822 (cycles; ticks = cycles/30):
- 8137 idle: ready, bound. 8136 walk: walk, bound (frame sounds walk_1, walk_3 at f7).
- 8138 attack_melee: attack1, rig+name; also rig+sound (frame 4 attack_melee_4). 60 cycles, 2 ticks.
- 8139 attack_ranged: attack2, rig+name. 60 cycles, no frame sound.
- 8140 death: rig+name. 60 cycles, forcedpriority 11.
- 9032 walk_pet: other (pet walk variant), rig+name. 36 cycles.
Creeper rig 1818: 8100 ready/walk bound (210 cycles, 7 ticks, six tornado frame sounds f3/f39); 9004 spawn rig+name; 9005 despawn rig+name (role other: not a death); 13134 idle variant rig+name.
Off-rig by name only (tier name): 8141 wall_float (no role stated), 8142 shadow_portal, 8143 tile_glow, 8144 shared_projectile.
Spotanims 1603-1608 (all tier name): zap anim=qip_watchtower_ogre_spell_travel; sharedattack anim=tob_sotetseg_shared_projectile (8144, 64 cycles); sharedattack_impact anim=verzik_lightning_impact; maging and ranging both anim=inferno_splitter_proj (7616, 48 cycles); drain anim=tob_shadow_projectile (8100, the creeper's seq).

## Stays unknown
- No special, no spawn for the boss, no defend animation exists on its rig. The boss has exactly two attacks, one death.
- wall_float 8141: role unstated, off-rig, used nowhere.

## Ledger disagreements (npc_combat/t/)
- tob_sotetseg_creeper, _story, _hard: attack_anim = tob_shadow_projectile_spawn, comment "rig's only forcedpriority=6 seq" is false (spawn is forcedpriority 8) and a spawn is not an attack; the creeper has no attack. ~~death_anim = tob_shadow_projectile_despawn: despawn, not a death (a4, role by priority guess).~~ [closer: struck as a fault -- the name fact holds, but the rig offers nothing else for a removal, so despawn is the right answer if the creeper ever ran npc_death. It never does: our creepers leave by npc_del (tob_sotetseg.rs2:1594-1684), so 9005 never plays. That is filed under UNUSED in RIG_AUDIT.md, not as a ledger fault.]
- The six boss ledgers: attack melee and death on rig and named correctly; defend null. No disagreement.

## Spec disagreements (encounters/sotetseg.tsv)
- sotetseg.av.maze.boss_seq: seq 8142 tob_sotetseg_shadow_portal is on framemap 1830, NOT the boss rig 1822; tier deserves "name" (grade D with [cache][nr] is generous; it rests on a script call tob_sotetseg.rs2:1020, our own, and a Jagex name that is a portal loc anim). Whether the live boss plays it needs a recording.
- sotetseg.av.death_ball.proj_anim 8144 and maze.lit_path / exit_portal (8143, 8142): these are spotanim/loc anims on their own rigs, correctly not claimed for an npc; fine.
- ball.proj 1606 / ricochet 1606,1607: the spotanims borrow inferno_splitter_proj (Inferno's sequence), so the projectile motion is another monster's; spec keeps them at C [plugin][qol], which is the most they support.
- ball.seq 8139, melee.seq 8138, death.seq 8140: on the rig, role stated by name: deserve at least rig+name; spec grades C/C/D are under, not over.

## Unused candidates (no spec row, no room script)
- 9032 walk_pet, 9004 spawn, 9005 despawn, 13134 idle (creeper), 8141 wall_float. Script uses 8138, 8139, 8140, 8142 (npc_anim lines 279, 315, 498, 1020).
- creeper spawn 9004/despawn 9005 are never played by our script; the creeper takes the generated ledger's attack/death rows only.

## Only a recording or plugin constant can settle
- Whether the boss plays shadow_portal 8142 at the maze proc, and what wall_float 8141 is for.
- Whether the creeper plays spawn/despawn on appearing and vanishing.
- The pet walk is only relevant if a Sotetseg pet is ever rendered.

## Closer audit (rig-tob closer, 2026-10-03)
(1) 8142 is on framemap 1830, and npc_rigs.csv gives 8387 and 8388 only 1822. tob_sotetseg.rs2:1020 plays it on the boss itself, after ~tob_sote_retype: the off-rig claim holds, and it is a script fault as well as a spec fault. (2) The creeper's ledger attack is 9004, a spawn (tob_sotetseg_creeper.combat:16, generated .npc:44934): holds. (3) The death=despawn half: struck as a fault (above).
