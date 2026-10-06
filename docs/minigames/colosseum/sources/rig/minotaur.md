# Rig pass: minotaur (colosseum)

Documents only; nothing built or run. Table: `minotaur.tsv` (51 rows). Method: `docs/minigames/waves_loop/RIG_PASS.md`.

## The npcs
- colosseum_minotaur, npc 12812, "Minotaur", size 3, model 52497, cache_npc.txt:151. readyanim npc_minotaur_boss_idle (10840), walkanim npc_minotaur_boss_walk (10842).
- colosseum_minotaur_routefind, npc 12813, same record fields (ready, walk, model, size), cache_npc.txt:185. Presumably the same monster used for pathing; nothing in the record says more.
- colosseum_minotaur_gib, npc 12830, no name, model 51250, not interactable, no readyanim or walkanim: no rig, no candidate.

## The rig
- Ready and walk sit on one framemap, 1758. It carries 24 sequences, shared with the zamorak demon boss set (7 rows, other monster by name, tier rig, role unknown).
- 17 sequences are minotaur by name: 7 npc_minotaur_boss_*, 7 mag_minotaur_*, 3 louder/fast variants.

## Counts (per npc, 24 rows each, x2, plus one spotanim each, plus the gib row = 51)
- bound 4, rig+name 26, rig+sound 4, rig 14, name 2, none 1.

## Candidates (tier rig+name unless stated)
- attack1 melee: npc_minotaur_boss_attack_melee 10843 (22 frames, 2 ticks; sound attack_melee_01).
- attack2 magic: npc_minotaur_boss_attack_magic 10844 (sounds heal_charge, heal_cast: a healing cast by sound name).
- attack3: npc_minotaur_boss_attack_melee_louder 11747 (same frames and sounds).
- mag_minotaur_melee 11584 and mag_minotaur_magic 11585: rig+sound (name alone is not enough); forcedpriority 7.
- spawn: npc_minotaur_boss_spawn 10845 (38 frames, 5 ticks; spawn_magic, spawn_growl, footsteps).
- death: npc_minotaur_boss_death 10846 (30 frames, 3 ticks), death_louder 11748, mag_minotaur_death 11587 (forcedpriority 10).
- defend: npc_minotaur_boss_defend 10841 (forcedpriority 4).
- walk variants: walk_fast 11746, mag_minotaur_walk 11582, mag_minotaur_run 11583 (32 client ticks).
- mag_minotaur_ramp 11586: transition by name only; its sole sound is cow_death, a stray reference.

## Graphics
- spotanim 2723 vfx_colosseum_minotaur_explosion_01, tier name, anim vfx_scarab_explosion01, model 46421. Role unknown.
- No other spotanim names the minotaur. The magic attack heal cast graphic is not identifiable.

## Unknown
- 7 zamorak_demon_boss_* rows (other monster). Gib: nothing.
- Which of attack_melee, attack_melee_louder, mag_minotaur_melee is used when; the mag_ set vs npc_ set.

## Ledger disagreements
- None. Both ledgers (colosseum_minotaur, _routefind) give death/attack/defend = npc_minotaur_boss_death / attack_melee / defend, all on the rig and minotaur by name. Gib ledger is empty, correct.
- The ledger attack never uses attack_magic, so the magic attack is absent from the ledger (not a disagreement).

## Needs a recording, plugin constant or picture
- What the magic (heal) attack does and which graphic it casts; when spawn plays; whether the mag_ and louder variants are used at all; the explosion's trigger.
