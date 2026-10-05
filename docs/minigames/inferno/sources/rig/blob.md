# Rig pass: blob (Jal-Ak and the three Jal-AkRek)

Method: docs/minigames/waves_loop/RIG_PASS.md. Documents only; nothing run or rewritten.

## The npcs (cache_npc.txt)
- 7693 inferno_creature_splitter, Jal-Ak, size 3, model 33001 (line 103)
- 7694 ..._splitter_mage, Jal-AkRek-Mej, model 33002 (line 137)
- 7695 ..._splitter_range, Jal-AkRek-Xil, model 33004 (line 156)
- 7696 ..._splitter_melee, Jal-AkRek-Ket, model 33003 (line 174)
- All four: readyanim jalak_ready, walkanim jalak_walk.

## The rig
- One framemap, 1687, for all four. It carries exactly 7 sequences, all jalak_*. Closed; no name narrowing.

## Candidates: 7 seq x 4 npcs + 4 spotanim = 32 rows in blob.tsv
| id | name | frames | game ticks | role | tier |
|---|---|---|---|---|---|
| 7581 | jalak_attack_magic | 11 | 1.0 | attack 1 | rig+name |
| 7582 | jalak_attack_melee | 12 | 1.0 | attack 2 | rig+name |
| 7583 | jalak_attack_ranged | 11 | 1.0 | attack 3 | rig+name |
| 7584 | jalak_death | 9 | 1.5 (forcedpriority 10) | death | rig+name |
| 7585 | jalak_defend | 12 | 1.0 | defend | rig+name |
| 7586 | jalak_ready | 12 | 1.0 | ready | bound |
| 7587 | jalak_walk | 12 | 1.0 | walk | bound |
- No special, spawn or transition candidate. No frame sounds on any sequence.
- Which attack belongs to which npc is not stated by the cache: the name says the style of the sequence, not who uses it. Attack variants are listed for every npc; a bat-style one-to-one is not claimed.

## Graphics (tier name; model and anim from all.spotanim)
- 1380 inferno_splitter_mage: model 33008, anim inferno_splitter_proj, 24 frames, 1.6 ticks
- 1381 inferno_babysplitter_mage: model 33009, same anim
- 1378 inferno_splitter_range: model 33015, anim inferno_basic_projectile_02, 15 frames, 1.0 tick
- 1379 inferno_babysplitter_range: model 33016, anim inferno_basic_projectile, 15 frames, 1.0 tick
- Role attack-projectile is from the Jagex name only. No spotanim for the melee npc. The slug2_blob_* spotanims share the word blob, not the monster; not listed.

## Unknown
- Nothing on the rig is left unclassified (role unknown: 0).

## Ledger (OSRS-Content/osrs239-content/npc_combat/i/inferno_creature_splitter*.combat)
- All four files give death jalak_death, attack jalak_attack_magic, defend jalak_defend, sounds "-". All on the rig and the right monster.
- Disagreement 1: _melee (7696) is given jalak_attack_magic although jalak_attack_melee is on its rig.
- Disagreement 2: _range (7695) is given jalak_attack_magic although jalak_attack_ranged is on its rig.
- The magic variants (7693, 7694) agree at rig+name. Each file is NOT COMPILED (inferno.npc authored block wins).

## Only a recording, plugin constant or picture can settle
- Which attack sequence Jal-Ak (7693) plays per style, and that each Jal-AkRek plays its namesake attack.
- Projectile-to-npc pairing, launch and impact timing; whether splitter and babysplitter projectiles differ in use.
- Hit sounds: the cache carries none.
