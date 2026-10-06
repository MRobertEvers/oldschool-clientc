# Rig pass: zuk (TzKal-Zuk and Jal-MejJak)

Method: docs/minigames/waves_loop/RIG_PASS.md. Documents only; nothing run or rewritten.
Rows: zuk.tsv (18 rows: 6 Zuk seq, 7 healer seq, 5 Zuk spotanims).

## The npcs (cache_npc.txt)
- inferno_tzkalzuk_placeholder 7706 (:510): TzKal-Zuk, size 7, model 33011, readyanim zuk_idle,
  NO walkanim (stationary boss).
- inferno_zuk_healer 7708 (:556): Jal-MejJak, model 33099, readyanim and walkanim
  dagannoth_water_creature_ready / _walk, vislevel 250.

## The rigs
- Zuk: framemap 1691, 6 sequences, all zuk_*. Closed; no name narrowing.
- Healer: framemap 19, 7 sequences, all dagannoth_water_creature_*. Closed by the join, but the
  rig belongs to the dagannoth water creature, borrowed by Jal-MejJak (AV_INVENTORY section on it).

## Candidates by tier: bound 3, rig+name 10, name 5, rig 0 (unknown 0)
Zuk (game ticks):
| id | name | role | tier | ticks |
|---|---|---|---|---|
| 7564 | zuk_idle | ready | bound | 3.0 |
| 7566 | zuk_attack | attack | rig+name | 3.0 |
| 7565 | zuk_defend | defend | rig+name | 3.0 |
| 7562 | zuk_death | death | rig+name | 5.0 |
| 7563 | zuk_spawn | spawn | rig+name | 7.0 |
| 13717 | zuk_spawn_no_rock | spawn | rig+name | 7.0 |
Healer:
| 2867 / 2863 | _ready / _walk | ready / walk | bound | 1.6 / 1.6 |
| 2868 | _attack | attack | rig+name | 0.9 |
| 2869 | _defend | defend | rig+name | 1.0 |
| 2866 | _death | death | rig+name | 7.9 |
| 2864 / 2865 | _spring_up / _go_down | transition | rig+name | 1.4 / 2.9 |
- Zuk has ONE attack seq (single variant); the magic, ranged and melee-to-prayer attack styles are
  not distinct animations on the rig: the rig cannot tell them apart.
- zuk_spawn and zuk_spawn_no_rock: both 70 frames, 7.0 ticks; which plays when (AV_INVENTORY:
  "for re-fights", unsourced) is not settled by the rig.
- No Zuk or healer seq carries a frame sound. Priorities are in the tsv notes.
- Special: none on either rig.

## Graphics (tier name; Jagex name shares "zuk")
- 1375 inferno_zuk_projectile (anim zuk_proj, 12 frames, 2.0 ticks, model 33006), 2261 _small
  (resize 64), 2381 _mid (resize 90), 2382 _mid_short (anim zuk_proj_short, 1.0 tick),
  3294 _gigantic (resize 500). No frame sounds. AV_INVENTORY marks 2261, 2381, 2382 and 3294
  unknown_purpose. 1376 inferno_zek_projectile shares the anim but is Jal-Zek's, not listed.
- Jal-MejJak: no spotanim shares a name word (searched zuk, mej, jak, healer, tzkal).

## Ledger (npc_combat/i/, two files)
- inferno_tzkalzuk_placeholder: death zuk_death, attack zuk_attack, defend zuk_defend, sounds "-".
  All on rig, right monster. Consistent with no frame sounds. The file says NOT COMPILED: the
  authored inferno.npc block wins. Not a disagreement.
- inferno_zuk_healer: death, attack, defend dagannoth_water_creature_*: on rig; right monster only
  in the sense that the cache record itself uses that rig. Sounds "-". Disagreements: 0.

## Only a recording, plugin constant or picture can settle
- Which spawn seq plays when, and whether the single zuk_attack serves every Zuk attack.
- Whether Jal-MejJak plays spring_up/go_down (spawn from and sink into lava) or only the lava-attack.
- Which Zuk projectile spotanim each attack launches.
