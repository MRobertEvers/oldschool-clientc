# Rig pass: nibbler (inferno), documents only

## npc
- inferno_nibbler, npc 7691, name Jal-Nib, cache_npc.txt line 48 (configs/all.npc:210596).
- readyanim jalnib_ready (seq 7573), walkanim jalnib_walk (seq 7572). Size not stated in the dump.
- No other animation field is stated by the record.

## rig
- One rig: framemap 1688 (out/osrs239_anims/npc_rigs.csv, seeds 7573 7572, strict_covers true).
- Ready and walk share it, so the rig is a single closed set of 5 sequences. It is not a shared
  human-style rig, so no name narrowing was needed; every row is on the rig itself.

## candidates: 5 sequences, 0 spotanims
| id | name | frames | game ticks | role | tier |
|---|---|---|---|---|---|
| 7572 | jalnib_walk | 9 | 0.9 | walk | bound |
| 7573 | jalnib_ready | 16 | 1.6 | ready | bound |
| 7574 | jalnib_attack | 12 | 2.0 | attack (only one) | rig+name |
| 7575 | jalnib_defend | 5 | 0.8 | defend | rig+name |
| 7576 | jalnib_death | 10 | 2.0 | death | rig+name |

(game ticks = client ticks / 30; 27, 48, 60, 25, 60 client ticks.)
- Attack: one candidate, 7574, rig+name. No special, spawn or transition sequence exists on the rig.
- Death: 7576, rig+name; forcedpriority 10. Defend 7575 has priority 2.
- No sequence on the rig carries a frame sound, so no rig+sound row exists and no sound can be
  named from the cache for this monster.

## stays unknown
- Nothing on the rig is left `unknown`; all five rows have a role.
- Spawn: no spawn sequence on the rig. If the Nibblers appear with a spawn visual it is not
  on framemap 1688 and not named Jal-Nib.

## graphics and projectiles
- cache_spotanim.txt (and all.spotanim names) hold no spotanim named with nib or jal. The
  nibbler is a melee biter; no projectile or hit graphic is claimed.

## ledger comparison
- OSRS-Content/osrs239-content/npc_combat/i/inferno_nibbler.combat: death jalnib_death,
  attack jalnib_attack, defend jalnib_defend (all layer a2, "rig's only ... seq"); sounds `-`.
- Disagreements: 0. All three are on the rig and named Jal-Nib. The file is marked NOT COMPILED
  because inferno.npc authors its own [inferno_nibbler] block; that block is what the game reads.

## what only a recording, plugin constant or picture can settle
- That 7574 is the attack the live nibbler plays (the corpus's Blert/plugin constants carry
  the id; this pass did not read them) and that 7576 is what plays on death.
- The attack and death sounds: the rig carries none in-band, so they are a server or
  synth-name question.
- Whether any nibbler spawn or pillar-contact visual exists, and on what rig.
