# Rig pass: modifiers_and_props (Colosseum)

Documents only. Rows: `modifiers_and_props.tsv` (69 rows: 5 bound, 28 rig+name, 11 rig, 24 name, 1 none; the closer added the doom scorpion's 20).

## The npcs and their rigs
- colosseum_modifier_bees (12823, Bee Swarm): ready = walk = npc_colosseum_bees_idle_01. Framemap 1815, 7 seqs.
- colosseum_beam_crystal (12824, no name): ready = walk = npc_colosseum_crystal_idle_01. Framemap 2227, 17 seqs.
- colosseum_healing_totem (12825, Healing totem): ready = walk = npc_colosseum_totem_idle_01. Framemap 2231, 2 seqs.
- colosseum_doom_scorpion (12822, Doom Scorpion): CLOSER CORRECTION. The worker read no anims; cache_npc.txt binds readyanim
  scorpion_update_ready (6252) and walkanim scorpion_update_walk (6253), framemap 1426, 18 seqs (generic scorpion, small
  scorpion, lobster, nature spirit scorpion). Candidates rig+name: attack1 scorpion_update_attack_tail 6254, defend 6255,
  death 6256 (the bound family); small_scorpion_* 6257-6261 and nature_spirit spawn/despawn 12519/12520 are other npcs'.
  Spotanims (name): 2711 vfx_doom_scorpion_01_player_death_01, 2712 vfx_doom_scorpion_attack_impact_01.
- colosseum_safespot_dying (12820, Pillar, size 3): no animations, no rig. No candidate.
All three rigs are private to the Colosseum except the bee rig, which also carries tob_bloat_flies_* and nightmare_flies_projectile.

## Attack, spawn and death candidates
- Bees: attack npc_colosseum_bees_attack_01 (10823, 2 game ticks, sound bee_swarm_attack06) rig+name; spawn _spawn_01 (10821) rig+name; death _despawn_01 (10824) rig+name (removal, not a kill).
- Crystal: attack npc_colosseum_crystal_attack_01 (10802, 1 tick, no sound) rig+name; charge_01 (10801) transition rig+name (windup by name only); spawn_01 (10798) with proj_spawn sounds; despawn_01 (10800) death rig+name.
- Totem: attack npc_colosseum_totem_attack_01 (10828, 4 ticks, sound modifier_totem_attack) rig+name. No spawn or death seq on its rig.
- No rig+sound rows: the sounds present only repeat what the name states.

## Graphics (tier name)
- Totem: spotanims 2687 projectile (3 ticks), 2688 impact (1 tick).
- Crystal: 2689-2692 charge beams 1-4, 2693-2696 attack beams 1-4, 2697 impact, plus echo copies 3147-3155 (model 55662). Their seqs 10803-10811 sit on the crystal rig as role other.
- Bees: 2707 jar impact (anim bees_jar_impact_01, 0 ticks), 2708 jar travel (anim projectile_zebak_pitcher01, a Zebak sequence; role from name alone).

## Stays unknown
- 10803-10806 charge beams and 10812 sunfire_lightning are tier rig/name only; the beam seqs 10807-10811 are graphics seqs, not body animations.
- Rig-only strangers: tob_bloat_flies_large/small (8083, 8084), nightmare_flies_projectile (8628) on the bee rig; spotanim_colossi_finalboss_01_triple_attack(_shorter) (10907, 10908) on the crystal rig, which name Sol Heredit.
- The pillar: no animation exists in the cache record. Doom scorpion: lobster rows 6263-6267 are rig, unknown.

## Ledger disagreements (0 after the closer)
- Closer: NONE. colosseum_doom_scorpion.combat gives scorpion_update_death/attack_tail/defend (a1); all three are on
  the npc's rig 1426 and in its bound family. The worker's "no rig" was wrong, so this is not a disagreement.
- The other four ledgers agree with the rig: bees attack/death, totem attack, crystal attack/death (a4 death = despawn). Defend and the totem death stay null.

## What only a recording, plugin constant or picture can settle
- Which crystal beam seq pairs with which spotanim, and the charge order against attack.
- Whether the Doom scorpion uses the generic scorpion set (6254-6256) or another (Blert or a plugin table).
- Whether the totem has any death or despawn motion; its rig offers none.
