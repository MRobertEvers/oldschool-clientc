# Desert Treasure I: Dessous (the blood diamond fight), both forms

Rebuilt from RSProx index 4629 (rev 242, w442, 2026-11-20, normal world), the only session on the
index holding either form (`search npc 3459` / `3460`: 1 session each). Files:
`quest_deserttreasure/scripts/dt_dessous.rs2`, `configs/dt_dessous.varn` (new),
the two Dessous blocks in `configs/deserttreasure.npc`, `test/quests/_dt_dessous.lua`.
Our side is run 4 of `_dt_dessous.lua` (private packs, 21/21 PASS, tick log
`$DT/dessous/run4/ticklog.tsv`). M = the pour's message tick, K = the killing splat.

The owner's requirement ("the pot is not blessed ... if Dessous dies, he just heals back to full and
keeps fighting") is met: the test takes the unkillable Dessous to 0 hp twice (run 4 t75, t102; run 3
`npc_heal 200` from `[ai_queue3,blooddiamond_vampirewarrior_unkillable]` at t71 and t99), he heals to
200 and keeps fighting, `fdvw_subquest` stays 2 and no diamond appears; then the real one is killed
with the blessed pot (stage 3, both lines).

## Per-life record

| | unkillable (3460), 4629 [1482..1685] | real (3459), 4629 [3292..3404] | ours |
|---|---|---|---|
| raised by | `fd_silver_pot_blood_garlic_spiced` on the tomb [1475] | `..._blessed` [3284] | same; any other blood pot raises the unkillable one (wiki "any part not followed", UNVERIFIED) |
| pot back | `fd_silver_pot`, its own slot | `fd_silver_pot_blessed`, its own slot | same (run 4 t53, t177) |
| M+4 | tomb -> `vampire_big_grave_withblood` (back at M+13), 9 bats `vampire_bats` raw 5->20, 0..70 from the tomb centre (3570,3402) to N+3, E2, S2, W2, NE, SE, SW, NW (2), N+3; synth `vampire_bats_appear` | identical | same, both sessions' order (run 4: loc at M+4, revert M+13) |
| add | M+7 at (3570,3405) = tomb centre + (0,3), spawnangle south, `[facing] coord=` the player's tile, hint arrow | same | same (run 2/3/4: M+7) |
| movement | idle 192, 1 approach step [1495] | idle 48, 18 `teleport` (smoke), 7 walk (back to a cardinal tile) | `defaultmode=none, wanderrange=0` (generated wander 5 removed); approach by mode; `npc_tele` |
| seqs | `vampire_summonbats` 136, `human_unarmed_def` 50 (delay = the arrow's), `vamp_claw` 4 | `vamp_claw` 5, `vampire_summonbats` 5, `human_unarmed_def` 29, `human_death` 1 | defend anim now `human_unarmed_def` (was `human_unarmedblock`) |
| says | "Hssssssssssss" with every volley (19/19) | same (7/7) | same; unkillable at 0 hp: "You will never defeat me" (wiki, UNVERIFIED wording) |
| sounds | area `vampire_summon` (10) EVERY tick between volleys (179 ticks, 1 a tick); synth `vampire_hit` on each hit taken, `vampire_attack` on each claw | `vampire_summon` on volleys only; `smokepuff2` area (10) + synth on each teleport; `vampire_death` synth at K+1 | same (params `attack/defend/death_sound` were null) |
| death | none (the recorder left at 4/30 [1681]) | K [3401]; K+1 seq `human_death`, face reset, `enabled_ops 0b00000`, synth; K+3 del, hint reset, `fdvw_subquest` 2->3, first page | `param=death_script,yes`; run 3/4: K+1 seq/synth/face/ops, K+3 del + stage + page |
| leaving | player teleported [1685]; hint reset [1692], "Dessous returns to his grave, bored of toying with you." [1693] | not recorded | 7 ticks of the owner gone (>15 tiles, other level, offline) -> del + hint_stop, message a tick later; real form UNVERIFIED |

## Attacks

| attack | recorded (4629) | ours (run 2-4) | verdict |
|---|---|---|---|
| claw (both) | `vamp_claw` + synth `vampire_attack`, the splat on the swing tick, every 3 ticks; values real 0,13,3,15,4, unkillable 6,0,10,9 | same; 0..11 in 6 swings; max 19 by the npc formula (str 99, bonus 50) = wiki | matched (max not reached in 9 recorded) |
| bats, real | say + `vampire_summonbats` + area `vampire_summon` + projectile `vampire_bats` raw 10->5, 0..60, angle 0, progress 0 (7/7) from a free tile 2..3 from the player (Chebyshev 2: 6, 3: 1); player block on the launch; 5 + 5 at +3 (7/7 volleys, 14/14 values 5); every 10 ticks | same; 5+5 at +3 (3 volleys, 6/6) | matched |
| bats, unkillable | same packets (19/19 params identical; source 2: 10, 3: 9); magic half then ranged half, each 5..15 (29 unprayed: min 5, max 15, mean 9.9, 0 zeros), landing at +10 (17/17); a half is 0 under its protection prayer at the LAUNCH (ranged 0 under Missiles 6/6, magic 0 under Magic 1/1; [1514]->[1524] Missiles at launch, Melee at landing, still 0) | same; 8 unprayed values 6..15 | matched |

## Rules

| rule | recorded | ours | verdict |
|---|---|---|---|
| real: style from the prayer | bats only under Protect from Melee (7/7), claws otherwise (5/5) | `~dt_dessous_wanted_mode` | matched (run 4) |
| real: switch tick takes no swing | [3299] Melee on -> bats [3300]; [3360] -> [3361]; no swing at [3375] (the bats' 10 still running) | `~dt_dessous_switch` postpones only a due attack | matched (run 2: Melee t183 -> bats t184; off t209 -> claw t214 = volley + 10) |
| real: teleport on Melee -> claws | 18/18 Melee->Missiles switches teleport beside the player (E 6, NW 5, NE 4, W 2, N 1; never its own tile, never the tomb), smoke map anim 124 on both tiles, area + synth `smokepuff2`, facing re-sent | uniform over free ring tiles != own; same sounds and map anims | matched (run 2 t209: (3570,3405) -> (3568,3404)); map anims not in the tick log (unchecked) |
| unkillable: prayer ignored | 17 volleys under Melee, Missiles, Magic and none | bats whatever the prayer | matched |
| unkillable: claws | after the 3rd volley in a row at Missiles ([1624], [1634], [1644]) claws from [1654]; Melee [1665] -> bats [1666] | `%dt_dessous_count >= 3` | ONE SAMPLE; not exercised by the test (Open) |
| unkillable: 0 hp | (wiki) "You will never defeat me", heals to full | `[ai_queue3]` say + `npc_statheal(hitpoints, 0, 100)` | test: 2/2 heals to 200, stage 2, no diamond |
| empty pot on the tomb | "There is nothing in the pot to pour onto the grave." [3416] (blessed) | both empty pots | blessed matched; unblessed UNVERIFIED |

## npc info block kinds (npc_block_inventory.py, run 4)

| type | kind | recorded | ours |
|---|---|---|---|
| real | sequence / hit / headbar / facing / say / enabled_ops | 40 / 32 / 32 / 28 / 7 / 1 | 21 / 12 / 12 / 4 / 3 / 1 |
| unkillable | sequence / hit / headbar / facing / say | 190 / 54 / 54 / 6 / 19 | 100 / 20 / 19 / 3 / 11 (2 "You will never defeat me") |

Kinds recorded and never sent: 0. Counts differ by fight length (our test is shorter, one teleport).

## Hit causes (rsprox_hit_causes.py --by-prayer, 4629 --all-players --bas vs run 4)

| cause | rec | ours | verdict |
|---|---|---|---|
| unkillable volley halves (`vampire_summonbats`/`human_unarmed_def` offsets, adj+near) | 34, 0..15 (0s prayed) | 16, 5..15 (no prayer) | matched |
| unkillable claws | 6, 0..10 (Missiles) | 0 | REC-ONLY: the test never holds Missiles for 3 volleys (Open) |
| real claws | 4, 0..13 | 6, 0..11 | matched |
| real volley halves | 14, 5..5 (Melee) | 6, 5..5 | matched |
| `sweep:blooddiamond_vampirewarrior@1` 15 [3357] | 1 | 0 | the claw after a teleport, attributed to the teleport |
| `none` | 0 | 0 | |

No heal splat recorded or sent (the unkillable never reached 0 in the recording).

## Generic interactions in the windows

Varrock Teleport [1682] (out), sharks and super restores, Protect prayers, Eagle Eye, the rune
arrows/bow are the recorder's kit: generic, not re-checked.

## Open

1. The unkillable form's claw switch (3 volleys in a row at Missiles) is fitted to one sample; the
   test does not exercise it. Its 1-step approach at [1495] is not reproduced (bats mode never approaches).
2. "You will never defeat me" and the heal are the wiki's (no recording reaches 0); no heal splat is sent.
3. The wiki's long-distance teleport ("hit from a long distance, he teleports next to them; three
   times and he gets bored") is not written: unrecorded. The leave rule (7 ticks gone) is one sample and
   assumed for the real form.
4. The real one's first claw: add+1 recorded, add+2 in our test because our player stood diagonal to
   the spawn (one approach step); not a script delay.
5. The silver-weapon +10% (`~dt_dessous_prepare_hit`) is the wiki's, unrecorded.
6. Map anims (`smokepuff` 124) are not in our tick log; the wire was not diffed packet by packet.
