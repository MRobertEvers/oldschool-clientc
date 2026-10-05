# Rig pass: bat (inferno_creature_harpie, Jal-MejRah)

Method: docs/minigames/waves_loop/RIG_PASS.md. Documents only; nothing run or rewritten.

## The npc
- inferno_creature_harpie, npc 7692, size 2, model 33018 (cache_npc.txt:69).
- readyanim = walkanim = jalmejrah_ready (seq 7577): one animation serves both.

## The rig
- One framemap: 1695, carrying exactly 4 sequences, all jalmejrah_*. Closed; no name narrowing needed.

## Candidates (4 seq + 2 spotanim = 6 rows in bat.tsv)
| id | name | frames | game ticks | role | tier |
|---|---|---|---|---|---|
| 7577 | jalmejrah_ready | 7 | 1.0 | ready and walk | bound |
| 7578 | jalmejrah_attack | 6 | 1.0 | attack | rig+name |
| 7579 | jalmejrah_defend | 6 | 1.0 | defend | rig+name |
| 7580 | jalmejrah_death | 12 | 2.0 | death (forcedpriority 10) | rig+name |

- No special, spawn or transition candidate. No frame sounds on any of the four.
- Single attack variant only; the bat has no melee/ranged pair on this rig.

## Graphics (tier name)
- spotanim 1382 inferno_harpie_proj: model 33017, anim inferno_basic_projectile (15 frames, 1.0 tick). Probably the bat's ranged projectile; named by the repo's inferno_ai.rs2:139, the cache name only says harpie.
- spotanim 541 slayer_harpie_splat: shares the word harpie, anim watersplash_small. Almost certainly another monster's; listed, not a candidate.

## Unknown
- Nothing on the rig is left unclassified.

## Ledger (OSRS-Content/osrs239-content/npc_combat/i/inferno_creature_harpie.combat)
- death jalmejrah_death, attack jalmejrah_attack, defend jalmejrah_defend: all on the rig and Jal-MejRah by name. 0 disagreements.
- attack/defend/death sounds are "-" (none carried): agrees with the cache.
- File says NOT COMPILED: inferno.npc has its own authored block that wins.

## Only a recording, plugin constant or picture can settle
- That attack 7578 is the ranged attack and that spotanim 1382 is its projectile (and its launch/impact timing).
- Hit sounds: the cache carries none, so they must come from a recording or plugin constant.
