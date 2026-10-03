# Theatre of Blood: audiovisual and presentation inventory

`AV_INVENTORY.tsv` (989 rows) has one row for each rev-239 cache asset this raid ships, and records whether our content uses it. The assets come from `sources/cache_{seq,spotanim,sounds,locs,vars,npc_*}.txt`, plus the 14 music tracks, the Verzik's Defeat jingle and the 7 `tob_*` interfaces. Paths below are relative to the worktree. `MT` stands for `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/scripts/`.

**How "used" was decided.** Each asset was matched whole-symbol against every non-comment line of `minigame_tob/` (scripts and configs) and the 19 other server files that name ToB symbols. Numeric forms (`varb6447_*`, `seq_N`, ...) were matched separately. No script uses a bare numeric id for a seq, spotanim, sound, loc or npc; the varbits are all written in the `varbNNNN_name` form. An asset also counts as used when it is reached through other content:
- a seq that is the `readyanim`/`walkanim` (cache record) of an npc we spawn;
- a seq that is the `death_anim`/`defend_anim` of such an npc, after the `tob.npc` override on top of `npc_anims.generated.npc`. `attack_anim` never counts, because every boss is `defaultmode=none`;
- a seq that is the `anim=` of a spotanim we spawn or a loc that is actually placed;
- a sound that is a frame sound of a seq we play. The client does emit these (`src/world/world_cycle.c:623` `World_EmitAnimFrameSound`);
- a loc that the cache map places in a square we actually instance.

Hits in `tob_selftest.rs2` and `test/raids/*.lua` go in the `tests:` part of the `detail` column and do not count as used.

**Purpose.** It comes from plugin constants (blert, advancedraidtracker `TobIDs.java`, openosrs `TheatreConstant.java`, tob-qol, tobmistaketracker), the plan, ENCOUNTER_TIMING, the wiki and the cache configs themselves (`anim=`, `multiloc`, `multivarbit`, seq frame sounds), and the cache clientscripts that read a var. Anything else is `unsourced`. An unused asset with no sourced purpose has the status `unknown_purpose`.

## Counts (used / unused / used_wrong_place / unknown_purpose)

| kind | maiden | bloat | nylocas | sotetseg | xarpus | verzik | lobby | raid-wide | unknown | total |
|---|---|---|---|---|---|---|---|---|---|---|
| seq (130) | 9/1/0/0 | 9/1/0/1 | 17/1/0/17 | 8/0/0/1 | 11/1/0/0 | 31/2/1/4 | . | 0/7/0/3 | 2/0/0/3 | 87/13/1/29 |
| spotanim (63) | 3/0/0/3 | 6/1/1/0 | 6/3/0/1 | 5/1/0/0 | 5/0/0/4 | 20/2/0/2 | . | . | . | 45/7/1/10 |
| sound (113) | 8/1/0/1 | 10/2/0/3 | 14/3/0/3 | 9/0/0/6 | 13/0/0/4 | 25/0/1/6 | . | 0/1/0/0 | 3/0/0/0 | 82/7/1/23 |
| loc (456) | 15/0/0/2 | 29/1/0/3 | 76/0/0/3 | 43/3/0/5 | 11/0/0/3 | 44/2/0/12 | 34/3/0/3 | 96/36/0/31 | 1/0/0/0 | 349/45/0/62 |
| npc (162) | 24/0/0/1 | 3/0/0/0 | 51/0/0/0 | 9/0/0/0 | 12/0/0/0 | 41/13/0/2 | . | 0/6/0/0 | . | 140/19/0/3 |
| varbit (38) | . | . | . | 0/1/0/0 | . | 1/0/0/0 | 0/2/0/0 | 10/12/0/2 | 2/6/0/2 | 13/21/0/4 |
| varp (6) | . | . | . | . | . | . | . | 2/3/0/0 | 0/0/0/1 | 2/3/0/1 |
| music (15) | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/1/0/0 | 0/1/0/0 | 0/1/0/0 | . | 12/3/0/0 |
| interface (7) | . | . | . | . | . | . | 0/2/0/1 | 1/2/0/1 | . | 1/4/0/2 |

The `unknown` room covers assets two rooms share (`tob_shadow_projectile*` is the creeper of both Sotetseg and Verzik, `tob_pillar_*`) and the `tobquest*` varbits, which belong to A Night at the Theatre.

## 0. Wrong-place findings (3)

- **seq 8132 `verzik_lightning_impact`** is passed to `spotanim_pl` at `MT/tob_verzik.rs2:1569` (P2 zap hit). That symbol is a seq. The cache has no spotanim of that name (`configs/all.spotanim`), so the zap impact graphic is not what the cache defines.
- **sound 3542 `tob_verzik_vampire_death`** is played at `MT/tob_verzik.rs2:3058` on the P3 death (`npc_anim(verzik_phase3_death_a)`, line 3055). The cache carries it only as a frame sound of `verzik_phase2_death` (`configs/all.seq:182835`), which plays it already. P3 death_a carries no sound (`OSRS-Content/osrs239-content/npc_combat/v/verzik_phase3.combat:21`).
- **spotanim 1570 `tob_bloat_falling_flesh1`** is used for Verzik hard-mode pillar debris (`MT/tob_verzik.rs2:819`). blert names 1570-1573 `BLOAT_HANDS_GRAPHICS` (`sources/blert_plugin/BloatDataTracker.java:59`).

**Sounds played twice.** At 15 sites a script calls `sound_synth` for a sound that the seq it just played already carries in-band, so the client hears it twice: `MT/tob_maiden.rs2:654`, `MT/tob_nylocas.rs2:1082,1084,1171`, `MT/tob_sotetseg.rs2:245`, `MT/tob_bloat.rs2:646,697`, `MT/tob_verzik.rs2:853,1433,1453,2480,2520,2526,2737,2855`. The `detail` column marks each one `DOUBLE:`.

## 1. Animations the cache has and we never play

- **Maiden.** `maiden_spawn` 14399 (her entrance; frame sound 11863, `configs/all.seq:336655`).
- **Bloat.**
  - `tob_bloat_stunned_short` 8089 (the anim of spotanim `tob_bloat_stunned` 1575, `configs/all.spotanim:10078`). It is deliberately replaced by the generic `stunned_shove` at `MT/tob_bloat.rs2:629`.
  - `bloated_toad_stay` 1020: unknown purpose.
- **Nylocas.**
  - `top_spider_melee_spawn` 8075 and `_noloop` 9030, the web-spawn emergence: nylos appear with no spawn anim and no `tob_nylocas_web_spawn` 3546.
  - `top_spider_magic_meleeattack` 7990 and `top_spider_ranged_meleeattack` 8001. These carry 3982 and 3993, so the mage and ranged nylos have no adjacent-melee swing.
  - Eight `*_quiet` variants (14387-14394) and `nylocas_queen_*` (14381-14386) have unknown purpose. The queen seqs are probably the quest, not the raid.
- **Sotetseg.** `tob_sotetseg_wall_float` 8141: unknown purpose.
- **Xarpus.** `tob_xarpus_acid_splat_end` 8069, the anim of `tob_xarpus_acidpool_end_0..3` 1551-1554 (`configs/all.spotanim:9944`). Acid pools vanish with no end graphic.
- **Verzik.**
  - **Phase-0 to phase-1 transform:** `verzik_throne_transform_initial` 8053 (the anim of loc `tob_dungeon_verzik_throne_transforming` 32737, `configs/all.loc:372645`, never placed). `verzik_throne_transform` 8108 was removed because it loops (`MT/tob_verzik.rs2:112-126`), and nothing replaced the throne transformation.
  - `verzik_chathead_talk/laugh` 8054/8055 and `verzik_human_idle` 8051: unknown purpose.
  - `tob_spider_tank_spawn` 8079: its siblings 8076-8078 are the armoured nylocas' ready, walk and death anims (`sources/cache_npc_verzik.txt:331`).
- **Raid-wide.**
  - `tob_throne_room_arrow_float` 8106, the anim of `tob_treasureroom_chest_loc0..4` (`configs/all.loc:375254`): the "your chest" arrow.
  - `tob_treasure_room_teleport_crystal` 8105.
  - `tob_purgatory_stance` 8070: unknown, likely the spectator pose.
  - Pet anims (8122, 9031-9033, 13135, 9142/9143).
- **Silent animation.** Only one seq our scripts play has no frame sound and no `sound_synth` in its proc: `verzik_pillar_fade` 8104 (`MT/tob_verzik.rs2:866`).

## 2. Graphics and projectiles never spawned (17)

- **Nylocas.** `tob_nylocas_death_{melee,ranged,magic}_standard` 1562-1564 are the small-nylo despawn graphics (`sources/tobqol/NylocasConstants.java:134-136`, `MELEE/RANGE/MAGIC_SMALL_DESPAWN_GRAPHIC`). Our nylos die through their `death_anim` only. `tob_nylocas_rangedprojectile_sizemid` 1560 is also never spawned: mid-size range nylos borrow size1 or size2 (`MT/tob_nylocas.rs2:1127-1129`).
- **Xarpus.** The acid-pool end graphics 1551-1554.
- **Maiden.** The directional pool variants `maiden_lingering_blood_sw/nw/ne` 3982-3984: only the base 1579 is used (`MT/tob_maiden.rs2:1244`).
- **Bloat.** `tob_bloat_stunned` 1575 (see section 1).
- **Verzik.**
  - `verzik_phase2_poisonglobule` 1588 (only the blood globule 1587 is used).
  - `verzik_acidbomb_small_impact` 1599.
  - `verzik_powerblast_protection` 1597 and `verzik_powerblast_safezone_quick` 3028.
- **Sotetseg.** `tob_sotetseg_zap` 1603.

## 3. Sound effects

- **Coverage.** Of 113 sounds, a script plays 37 (by `sound_synth` or as a proc argument). 64 are heard as frame sounds of a seq we play, and 46 of those are heard only that way. That leaves **83 heard and 30 never heard**. No test checks a sound: `test/raids/*.lua` and `tob_selftest.rs2` contain no `sound` or `synth` assertion.
- **Missing sounds, by mechanic.** Each is a TSV row with status `unused` or `unknown_purpose`.
  - **Boss hit and defend sounds (8).** None is ever played: `tob_maiden_hit` 3999, `tob_nylocas_hit` 4020, `tob_sotetseg_hit` 4019, `tob_xarpus_hit` 4018, `tob_verzik_human_hit` 4009, `tob_verzik_vampire_hit` 4021, `tob_verzik_spider_hit` 4022 and `tob_verzik_human_defend` 3977. The hook exists but is empty: `defend_sound = -  // s3 no source` in every `npc_combat/t/tob_*.combat`.
  - **Maiden.** `tob_maiden_blood_hit` 3989, the blood landing on a player.
  - **Bloat.**
    - `tob_bloat_jar_bgsound_loop` 3288, the room ambience loop.
    - `tob_bloat_flies_attack_1/4/6` 3544, 3951 and 3983. The fly attack picks only variants 2, 3 and 5 (`MT/tob_bloat.rs2:412-414`).
  - **Nylocas.**
    - `tob_nylocas_web_spawn` 3546 (spawn seq unused).
    - `tob_magic_melee_attack` 3982 and `tob_nylocas_range_melee_attack` 3993 (their seqs are unused).
    - `tob_nylocas_deathdetonate_spinning_2` 3946 and `_sizzle` 3959.
  - **Sotetseg.**
    - `tob_sotetseg_large_attack_ranged` 3994 and `tob_sotetseg_large_fireball_player_hit` 3947: the death-ball cast and impact. The ball plays `tob_sotetseg_red_spiral_hum` (`MT/tob_sotetseg.rs2:443`).
    - `tob_sotetseg_redspiral_grid_playercollision` 3233 and `tob_sotetseg_player_tile_explosion_impact(_2)` 3985/3970: the maze tile hits.
  - **Xarpus.**
    - `tob_xarpus_exhume_health_shots` 3231, the exhumed heal orb flight. Projectile 1550 flies silently (`MT/tob_xarpus.rs2:578`).
    - `tob_xarpus_player_acid_burn` 3944, a player standing in acid.
    - `tob_xarpus_attack_ranged_wing_flap` 3949.
  - **Verzik.** `tob_verzik_spark_2` 3988 and `tob_verzik_spider_death_1` 4008.
  - **Raid-wide.** `tob_transition_card` 3952: the room title card plays silently (`MT/tob_title.rs2:89`).
  - **Unknown.** `noa_parasite_bloat` 4241.

## 4. Locs never placed or never changed

- **Treasure vault (the biggest gap).** The cache map puts the vault in square 50_67. `^tob_template_loot` is defined (`minigame_tob/configs/tob.constant:67`) but `~tob_room_alloc` never instances it (`MT/tob_raid.rs2:58-62`). Verzik's death calls `~tob_open_vault` (`MT/tob_raid.rs2:1436`), which drops the loot straight into the inventory (`MT/tob_rewards.rs2:51-68`). The consequences:
  - The per-player chests are never placed: `tob_treasureroom_chest_loc0..4` 33086-33090 (each with a `multivarbit` on 6450-6454, `configs/all.loc:375257`) and their states 32990-32994 and 41746. There is no arrow and no purple aura.
  - The exit crystal 32996, stairs 32995, bookcases 33000-33003, small chests 33016 and spectator walls 33021-33025 never reach a player.
  - The oploc handlers for the war table and library table (`MT/tob_board.rs2:63-69`) can never fire.
- **Throne.**
  - `tob_dungeon_verzik_throne_visible` is placed (`MT/tob_verzik.rs2:1017`).
  - `throne_transforming` 32737 (anim 8053) is never placed.
  - `throne_door_opened` 32738 (op `Enter`), the trapdoor to the vault, is never placed.
- **Barriers.** `tob_arena_barrier` has a handler (`MT/tob_party.rs2:121`). Its `_noop` 42516 and the Verzik walkway `tob_walkway_verzik_barrier_op` 47336 (op `Resign`) are never used. The spectator barriers are placed by the map, and the middle one is only a door (`MT/tob_party.rs2:269`).
- **Cages.** `tob_dungeon_verzik_death_cage` comes from the map. `death_cage3` 32716, `death_cage_top` 32718 and `tob_bloat_cage_wall` 32956 are never placed.
- **Spectator furniture and cameras.** No bench locs exist. The camera locs 2739 and 8718 come from the map. Their `_op/_noop` variants 42517-42520, `tob_spectator_misthalin_easel/teddybear` 32685/32986 and `tob_scoreboard_noop` 32988 are never placed.
- **Lobby.**
  - `tob_surface_gravestone_chest` 32656 (op `Claim`) and `tob_surface_deposit_box` 32665 are placed with no handler.
  - The blood well states 32654/32985 are never placed, and its varbit 6455 is never set.
  - `tob_rewards_chest_lobby_open/closed` 41435/41436 are never placed.
  - `tob_female_orator` 32757 is never used. `tob_male_orator` has handlers (`MT/tob.rs2:20,35`) but no scanned map or script places it.
  - `tob_announcement_leaflet` 32533 is never used.
- **Party board.** `tob_surface_notice_board` is a dialogue only (`MT/tob_board.rs2:93-113`). There is no party list: see section 5.
- **In-room set dressing.**
  - Sotetseg hard-mode tiles 41748-41753 and their varbit 12269: no hard-mode maze split.
  - `tob_maiden_initial` 32972 and `tob_maiden_dead_remains` 32973 (no corpse loc).
  - `tob_bloat_corpse_active` 32969.
  - `verzik_webspin_loc` 32734 and `verzik_acid_pool` 41747. Webs and acid are done with npcs and spotanims instead.
  - `tob_midway_chest_open` 32759.

## 5. HUD and client vars the cache clientscripts read that no script writes

- **Written** (`MT/tob_hud.rs2:67,135-137,226-248`):
  - 6440 party status;
  - 6441-6446, the orb slot and five orbs;
  - 6447-6449, the top health bar's type, value and max;
  - 6400, throne visibility (`MT/tob_verzik.rs2:1140`).

  These are tested at `tob_selftest.rs2:3871` and onward (15 references).
- **Read, never written:**
  - varp 1740 `tob_mycontroller` and varp 3052 `tob_temp_transmit_3`, read by `tob_partylist_init.cs2:36-37`: the party list.
  - varp 1746 `tob_perm_transmit_1`, read by `tob_scoreboard_init.cs2:3`.
  - varbit 12270 `tob_scoreboard_tab`, read by `tob_scoreboard_tab_select.cs2:3`.
  - varbit 12988 `tob_lobby_friends_filter`, read by `tob_partylist_addline.cs2:40`.
- **Never written, where the cache uses them as loc switches:**
  - 6450-6454: the vault chests.
  - 6455: the blood well.
  - 11958 `tob_should_have_loot`, the multivarbit of `tob_rewards_chest_lobby_multi` (`configs/all.loc:490869`).
  - 12269: the Sotetseg hard-mode tiles.
- **Never written, plan-named:**
  - 6460/6461/12271/12272, the midway-chest store UI. That interface is never opened.
  - 11469 `tob_damage_taken`, 12971 `tob_progress` and 14980 `tob_crystal_antispam`.
- **Death markers.** The orb code "left the raid" (`^tob_hud_orb_gone` = 31, `tob.constant:3432`) is never written. Only "no hitpoints" (30) is, at `MT/tob_hud.rs2:267`.
- **Room timer.** There is no server varbit for it. The HUD timer is client-side (varc 219/220 in `tob_hud_draw_2297.cs2`).

## 6. Raid-wide presentation (plan sections 5 and 12)

| Feature | Implemented? | Exercised by a test? |
|---|---|---|
| Lobby party board and party list | **Partly.** The board is a dialogue (`MT/tob_board.rs2:99-113`) and "anyone who enters with you joins" (line 113). The `tob_partylist`/`tob_partydetails`/`tob_infoboard` interfaces (364/50/459) are never opened. | Party procs: `tob_selftest.rs2:1332` and onward. No board or interface test. |
| Mysterious Stranger, escape crystal, gravestone chest | **No.** No `tob_stranger` reference anywhere. The gravestone chest has no handler. | No |
| Raid entry | Yes: `[oploc1,tob_surface_raid_entrance]` (`MT/tob_party.rs2:77`) and walk-in queue `MT/tob_raid.rs2:113`. | Via the room drivers (`test/raids/*.lua`) |
| Room title card / transition | Yes, visually: the `tob_hud` portal and fade (`MT/tob_title.rs2:89`), called at `MT/tob_raid.rs2:387,882,942`. **Silent:** `tob_transition_card` 3952 is never played. | `tob_selftest.rs2:4776` (3 references) |
| Top health bar and orbs | Yes (section 5). | `tob_selftest.rs2:3871` and onward |
| Death and spectate flow | **No.** `~tob_board_death` (`MT/tob_chest.rs2:207`) has no caller. `[playerdeath,_]` (`player/death.rs2:21`) has no ToB hook (the Inferno has one, line 75). A raider who dies takes an ordinary death. `tob_purgatory_stance` 8070 is unused. Logout is hooked (`player/logout.rs2:29` to `MT/tob_raid.rs2:824`). | 1 spectator reference (`tob_selftest.rs2:2614`). No death test. |
| Supply (midway) chests | Partly: a dialogue store (`MT/tob_chest.rs2:147`). The `tob_midway_stores` interface 405 and its varbits are unused. | `test/raids/tob_bloat.lua:1413`, `tob_selftest.rs2:2211` |
| Treasure room, chests, loot beams | **No.** The vault is never built and loot is added to the inventory (`MT/tob_rewards.rs2:51-68`). There are no chests, no arrow, no purple aura, no `tob_chests` interface 23, and no "Curtain Closes" music. | None (`tob_open_vault`: 0 test references) |
| Performance board and scoreboard | Chat lines only (`MT/tob_chest.rs2:214`, `MT/tob_board.rs2:72`). The `tob_scoreboard` interface 363 is never opened. | `tob_selftest.rs2:2276` (board) |
| Music per room | 12 of 14 tracks, room and fight (`MT/tob_music.rs2:30-45`, called from `MT/tob_raid.rs2:382,941` and `MT/tob_party.rs2:225`). "Welcome to the Theatre" (556) and "The Curtain Closes" (582) are defined at `tob.constant:3351,3358` and never played. The jingle `verzik_s_defeat` (250) is unused. No `music_unlock` call exists in `minigame_tob`, so no track ever unlocks in the music tab. | None |
| Combat Achievements | Partly: 18 `ca_task_complete` calls (`MT/tob_rewards.rs2:99-166`): KC tiers, the 7 perfect rooms/theatre, and 7 speed tiers, out of 46 wiki tasks (`sources/wiki_combat_achievements_tob.tsv`). | None (`tob_ca_`: 0 test references) |

## Method notes and limits

- **NPC rows** list the record's own `readyanim`/`walkanim` (cache) and the effective `attack`/`defend`/`death` anim (generated block with the `tob.npc` override), then `tob.npc overrides:` and whether a script also plays that death or ready seq by `npc_anim`. Of 162 records, 140 are referenced. The 19 unused ones are the story and hard Verzik webs, pillars, rubble and death bats, `verzik_throne_npc` and the pets.
- **"used" for an npc** means a script references it, whether spawning it or handling a trigger. Eight records (story/hard Verzik and Sotetseg minions, `verzik_initial_base/quickstart`) are referenced only in handlers.
- **Region music** handled outside `minigame_tob` (an engine area table) was not checked.
- **`unknown_purpose`** is not a guess. For 23 sounds the only evidence is the cache name, which the TSV keeps.
- **Scripts.** Built with `$SCRATCH/av_inventory.py` and `av_write.py`. They are not committed. Re-running them over the same inputs reproduces both files.
