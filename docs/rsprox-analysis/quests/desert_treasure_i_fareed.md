# Desert Treasure I: Fareed (the smoke diamond)

Source: RSProx index 4623 [1956..2120] (rev 242.1, world 442, normal world), transcribed
`--all-players --bas`. It is the only session on the index holding npc 3456 (or the Nightmare Zone
copies 4115/6348): one life, one roll. Content: `quests/quest_deserttreasure/scripts/dt_fareed.rs2`,
his block in `configs/deserttreasure.npc`. Spawn and hand-off are `dt_smoke.rs2` (smoke section doc).
Fight test: `test/quests/_dt_fareed.lua` (4 private runs; run 4 12/12 PASS; the last edit, the swing
clock, is UNRUN).

## 1. Per-life record (4623)

| R | ours (run 4) | verdict |
|---|---|---|
| [1956] `[add]` (3309,9377) gate + 1, spawnangle south, `[say]` "You dare trespass in my realm?" in the add block, hint arrow on him | tick 40 = gate 39 + 1, same tile, spawnangle, say on the add | matched |
| [1957] first step SW (3308,9376), `[facing] player=` on it; [1958] (3307,9376); [1959] (3306,9376) | 41/42/43 the same three tiles | matched |
| walk 91 rows, 1 tile a tick, chased to (3323,9382): 14 tiles from spawn | `maxrange=20` (generated 7 would have dropped the chase) | fixed |
| facing: 38 `[facing] player=P` (fallbackangle re-sent as the angle changes), never coord or reset until death | 2 `npc_face` rows | **Open** (engine latch) |
| seqs: `human_sword_slash` x26 (delay 0), `human_unarmedblock` x27 (his block: delay = the projectile's endtime, 0 for a punch), `human_death` x1 | sword slash (was `human_unarmedpunch`), unarmedblock, human_death | fixed |
| no transformation, no level change, no `[bas]`, no `[body_customisation]`, no headicon | none | matched |
| headbar id 0 (width 30: 8 damage -> 28) every hit | healthbar_standard_30 | matched |
| death: last splat [2117], [2118] `human_death` + `giant_death` + `[facing] type=reset` + `enabled_ops 0b00000`, [2120] `[del]` | splat 224, death seq + giant_death 225, free 227 | matched (enabled_ops value not compared: Open) |

## 2. Block kinds (`npc_block_inventory.py --values`)

| kind | recorded | ours | verdict |
|---|---|---|---|
| say | 1 (the add) | 1 | matched |
| sequence | 53 | swing + block + death | matched in kind |
| facing | 38 | 2 | Open (engine) |
| hit / headbar | 25 / 25 | every hit | matched |
| spotanim | 13: the recorder's spell impacts (waterblast_impact, failedspell_impact) | same graphics from the same spells | kit |
| enabled_ops | 1 (0b00000 at death) | 1 ops row at death | value unverified |

## 3. Attacks (32 recorded; gaps 4..16, never under 4)

| attack | R | ours | verdict |
|---|---|---|---|
| swing (26) | `human_sword_slash`, synth `stabsword_slash` (2548), the player's block seq + `human_block_1` delay 20, splat on the swing tick | the same (attack_anim/attack_sound params) | fixed (sound was null, seq was a punch) |
| swing damage | 25/25 = 0 under Protect from Melee (on from [1961]); the 26th ([2117]) died with him, no splat | prayed 0s; max 37 with ice gloves, 40 without | **max is wiki only, UNVERIFIED** (no unprayed swing recorded) |
| magic (6) | [1959] [1976] [2005] [2076] [2097] [2109]: no seq of his, no projectile, `failedspell_impact` (124) on the PLAYER + `spellfail` synth, no splat. 6/6 splashed. 6/32 of his attacks | 1 in 5; a splash sends the same two | fitted (one session); a magic HIT (max ~20, no graphic) is wiki only, UNVERIFIED. The wiki's "splashes on himself" is wrong: 6/6 on the player |
| reach | every attack from a side tile after his step; he moves and swings on the same tick ([1964], [1981]) | ours swings the tick after arriving (spawn+4 vs spawn+3) | **Open** (engine npc phase) |
| idle player | he keeps swinging at a player not attacking him ([2009], [2013]) | run 2 stood idle 38 ticks: the mode is one-shot until his first hit; re-armed in the handler with his own 4-tick clock | fixed, the clock guard UNRUN |

## 4. Rules

| rule | R (counts) | ours | verdict |
|---|---|---|---|
| disarm | 10/10 "The heat from the warrior causes you to drop your weapon." while attacking him from a SIDE tile with a weapon: darts x4, staff x6; never from his diagonal ([1991] dart thrown) or 2+ tiles (17 throws/casts); on the tick he steps beside an attacker or the attack click; the weapon goes to the backpack with its unequip synth; the attack stops | `[opnpc2]`/`[apnpc2,firediamond_firewarrior]` (our AP runs first) and `~dt_fareed_spell_disarm` from `~pvm_combat_spell_checks` for an autocast; a clicked spell is not ([2016]) | fixed; run 2/4: disarm on the click tick before any throw, darts back in the pack |
| ice gloves stop it | not recorded (snakeskin vambraces) | checked | wiki, UNVERIFIED |
| water spells only | wind blast 4/4: cast + flight as usual, `failedspell_impact` (124) in place of its graphic, its own `windblast_hit` area sound, his block seq, a 0 splat + giant_hit. Water blast 8/9 hit (2..16), 1 true splash ([2063]: spellfail, no splat) | `~dt_fareed_prepare_hit` (0) + `~dt_fareed_spell_impact` (graphic); run 4: wind blast at 78 -> failedspell_impact delay 56, 0 splat at 80 | fixed |
| ammo becomes ashes | 8/11 darts left `ashes` (private, reveal 100, despawn 200) on his tile at the throw, on the splat tick (throw + 2); 3 left nothing (1 in 5 break); no dart ever on the floor | `~dt_fareed_dropammo` from `~ranged_dropammo_npc`; run 1: ashes on each splat tick (throw + 2) | fixed; ice arrows / Ava's bypass wiki only, UNVERIFIED |
| melee hurts him | punches 0/2 | default | matched in kind |
| weakness var | `last_npc_elemental_weakness` (5536) -> 555 (water rune) on the first cast [2016] | no `elemental_weakness` param stated | **Open** (the percent is in no source here) |

## 5. Hit causes (by hand from the per-tick stream; `rsprox_hit_causes.py` not run: Open)

| side | splats | cause |
|---|---|---|
| player | 25 x 0 | his swing (same tick), all prayed |
| player | 6 splash graphics, no splat | his magic |
| Fareed | 11 darts (10 > 0), 4 wind blasts (0), 9 water blasts (8 > 0, 1 splash), 2 punches (0) | the recorder |
| heals | none on either side | - |

## 6. The kill and the hand-off

[2117] splat, [2118] death seq, [2120] (death + 2): hint off, `fd_killed_firewarrior` 0 -> 1, the diamond
straight into the pack with "You take the Diamond of Smoke from the ashes of the warrior.", no drop.
Ours: `[ai_queue3]` queues `dt_smoke_fareed_slain` (1) on the hero; run 4: varbit 1, diamond held, the line
in chat. The old private `%varp6614_dt_smoke_stage` is no longer read or written here (old 1 -> 100 on
the kill is now `fd_killed_firewarrior` 0 -> 1); no diamond is dropped any more. `dt_smoke_fareed_slain`
now calls `~dt_diamond_gained` (the four-diamond check).

## 7. Open

1. Player death or leaving mid-fight: unrecorded. He stays for `^dt_boss_lifespan`; on re-entry the gate
   finds him and spawns none, and he is not re-targeted (huntmode none).
2. The swing clock guard (`%npc_action_delay`) was written after the fourth run: UNRUN.
3. Facing re-sends (38 vs 2) and move-then-swing on one tick: engine-generic.
4. Max hit (37/40), magic hit damage, ice gloves, ice arrows, Ava's: wiki only.
5. `elemental_weakness` param (water) and `enabled_ops` value at death not compared.
6. A disarm with a full backpack: unrecorded (ours keeps the weapon and attacks on).
