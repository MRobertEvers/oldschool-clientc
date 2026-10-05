# Rig pass: melee (inferno), documents only

## npcs
- inferno_creature_melee, npc 7697, Jal-ImKot, size 4, cache_npc.txt line 198 (configs/all.npc:210740).
- inferno_creature_melee_small, npc 12594, Jal-ImKot, size 2, resizeh/v 64, cache_npc.txt line 702
  (configs/all.npc:410022).
- Both: model1 33010, readyanim jalimkot_ready (7595), walkanim jalimkot_walk (7596). No other
  animation field is stated by either record. Same model, so the small one is a scaled copy.

## rig
- One rig for both: framemap 1690 (ready and walk share it), 7 sequences. Not a shared human-style
  rig, so no name narrowing; every row is on the rig itself.

## candidates: 7 sequences per npc (14 rows), 0 spotanims
| id | name | frames | game ticks | role | tier |
|---|---|---|---|---|---|
| 7595 | jalimkot_ready | 18 | 2.0 | ready | bound |
| 7596 | jalimkot_walk | 18 | 2.0 | walk | bound |
| 7597 | jalimkot_attack | 18 | 2.0 | attack1 (only one) | rig+name |
| 7598 | jalimkot_defend | 18 | 2.0 | defend | rig+name |
| 7599 | jalimkot_death | 34 | 4.6 (139 client) | death | rig+name |
| 7600 | jalimkot_digdown | 22 | 3.7 (112 client) | transition | rig+name |
| 7601 | jalimkot_digup | 21 | 3.5 (105 client) | transition | rig+name |

- Tiers: bound 2, rig+name 5 per npc. rig+sound 0, rig 0, name 0.
- Attack: one candidate, 7597 (forcedpriority 3). Death: 7599 (forcedpriority 10). Defend 7598
  (forcedpriority 1). No special and no spawn sequence on the rig.
- The dig pair is the Jal-ImKot burrow: the Jagex name states dig down and dig up, so the role is
  a transition by name; the pass cannot say when the engine plays it or whether it is also the spawn.
- No sequence on the rig carries a frame sound: no rig+sound row, no sound can be named from the cache.

## stays unknown
- Nothing: all seven rows per npc have a role. The unknown part is use, not role: the dig pair's
  trigger, and whether 12594 (the second record) is spawned as a split product or scaled variant.

## graphics and projectiles
- No spotanim in cache_spotanim.txt or all.spotanim carries imkot or jal-ImKot in its name. The
  melee is a close-range attacker; no projectile or hit graphic is claimed.

## ledger comparison
- OSRS-Content/osrs239-content/npc_combat/i/inferno_creature_melee.combat and ..._melee_small.combat:
  death jalimkot_death, attack jalimkot_attack, defend jalimkot_defend (all layer a2, rig's only
  seq); all three sounds `-`. Disagreements: 0. Both files are NOT COMPILED because inferno.npc
  authors its own block; that block is what the game reads.

## what only a recording, plugin constant or picture can settle
- That 7597 is the attack and 7599 the death the live Jal-ImKot plays (Blert tables carry ids; not read here).
- When digdown/digup play, and their sounds (the inventory notes they play silently).
- What the small record (12594) is used for; no source states it.
- Attack and death sounds: a server or synth-name question.
