# What the waves loop copied from the raid branch

`docs/WAVES_ORCHESTRATOR.md` section 2 allows one verbatim copy of the raid loop's
tooling as a starting point. Every copy is listed here with the raid commit it came
from. After the copy the file is the waves loop's own; later changes are reconciled
by whichever branch reaches `v3` second. Nothing was cherry-picked or merged.

Raid branch: `origin/matthew-mbp-m4-raid-b1`. Its merge base with `v3` is `80b58e323`.

| Date | Raid commit | Raid path | Waves path | Kind | Changed since the copy |
|---|---|---|---|---|---|
| 2026-10-03 | `94f55b306` | `docs/WAVES_ORCHESTRATOR.md` | `docs/WAVES_ORCHESTRATOR.md` | spec, in place | no |
| 2026-10-03 | `94f55b306` | `tools/toa_fetch_wiki.py` | `tools/toa_fetch_wiki.py` | tool, in place (the pin guard: a different text lands as `.rev<revid>`); `v3` had not touched the file since the merge base | no |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/spec_check.py` | `tools/waves_gate/spec_check.py` | tool, renamed directory | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/raid_coverage.py` | `tools/waves_gate/waves_coverage.py` | tool, renamed | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/frame_count.py` | `tools/waves_gate/frame_count.py` | tool, renamed directory | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/vtt_to_md.py` | `tools/waves_gate/vtt_to_md.py` | tool, renamed directory | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `script/plugins/plugin_api.meta.lua` | `script/plugins/plugin_api.meta.lua` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `script/plugins/quest_driver/combat.lua` | `script/plugins/quest_driver/combat.lua` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `script/plugins/quest_driver/prayer.lua` | `script/plugins/quest_driver/prayer.lua` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `script/plugins/quest_driver/spell.lua` | `script/plugins/quest_driver/spell.lua` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `script/plugins/quest_driver/ticklog.lua` | `script/plugins/quest_driver/ticklog.lua` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `script/plugins/quest_driver/ui.lua` | `script/plugins/quest_driver/ui.lua` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `script/plugins/quest_driver/world.lua` | `script/plugins/quest_driver/world.lua` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/app/app_plugin_drive_events.c` | `src/app/app_plugin_drive_events.c` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/app/app_world_spawn.c` | `src/app/app_world_spawn.c` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/game/task_exec_entity_info.c` | `src/game/task_exec_entity_info.c` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/makefile` | `src/makefile` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/plugin/torirs_plugin_drive.h` | `src/plugin/torirs_plugin_drive.h` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/plugin/torirs_plugin_drive_ticklog.c` | `src/plugin/torirs_plugin_drive_ticklog.c` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/plugin/torirs_plugin_drive_ui.c` | `src/plugin/torirs_plugin_drive_ui.c` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/torirsserver/torirs_server.h` | `src/torirsserver/torirs_server.h` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/torirsserver/torirs_server_scripts.c` | `src/torirsserver/torirs_server_scripts.c` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/torirsserver/torirs_server_ticklog.c` | `src/torirsserver/torirs_server_ticklog.c` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/torirsserver/torirs_server_world.c` | `src/torirsserver/torirs_server_world.c` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/torirsserver/torirs_server_zone.c` | `src/torirsserver/torirs_server_zone.c` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/world/entity_npc.h` | `src/world/entity_npc.h` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/world/entity_projectile.h` | `src/world/entity_projectile.h` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/world/entity_spotanim.h` | `src/world/entity_spotanim.h` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `src/world/world.c` | `src/world/world.c` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `tools/quest_gate/conformance.py` | `tools/quest_gate/conformance.py` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `tools/quest_gate/gate.py` | `tools/quest_gate/gate.py` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `tools/quest_gate/quest_list.py` | `tools/quest_gate/quest_list.py` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `tools/quest_gate/run.py` | `tools/quest_gate/run.py` | driver/engine, in place, verbatim (`v3` had not changed it since `80b58e323`) | no |
| 2026-10-03 | `94f55b306` | `script/plugins/quest_driver.lua` | `script/plugins/quest_driver.lua` | driver, in place, verbatim then changed: `PARTS` lists `prayer`, `ticklog` without `raid` (`wave` joins with seam pass 2's first verb) | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `script/plugins/quest_driver/core.lua` | `script/plugins/quest_driver/core.lua` | driver, in place, verbatim then changed: `QD.raid = {}` became `QD.wave = {}` | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `src/plugin/torirs_plugin_drive.c` | `src/plugin/torirs_plugin_drive.c` | driver, in place, verbatim then changed: the loader registers `plugins/quest_driver/waves.lua` where the raid tip registers `raid.lua` | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `script/plugins/quest_driver/raid.lua` | `script/plugins/quest_driver/waves.lua` | NOT copied: an empty part (`QD.wave`, comments only) stands in its place; `t.raid.*` enters raid rooms through debugprocs only the raid branch's content has | n/a |
| 2026-10-03 | `94f55b306` | `script/plugins/quest_driver/pointer.lua` | `script/plugins/quest_driver/pointer.lua` | driver, three-way merge (`git merge-file`, ours `d9c86ca89`, base `80b58e323`, theirs `94f55b306`): clean, no conflict; `v3`'s b53-seam1 ground-row comments kept | no |
| 2026-10-03 | `94f55b306` | `src/torirsserver/torirs_server_combat.c` | `src/torirsserver/torirs_server_combat.c` | engine, three-way merge: the four tick-log hooks (`TicklogNpcAnim`, `TicklogHitNpc`, `TicklogNpcDeath`, `TicklogHitPlayer`) in; both conflicts (the `addXp` drain rule and its comment) resolved to `v3`'s side, which already carries LostCity's rule (b53-seam2) | no |
| 2026-10-03 | `94f55b306` | `src/torirsserver/torirs_server_world_selftest.c` | `src/torirsserver/torirs_server_world_selftest.c` | engine, three-way merge reduced to the tick-log stanza only (its `TicklogEnable` and the "What the tick log saw of that fight" checks); left out: the raid's eat-delay comment and loop, its duplicate drained-stat stanza (`v3` has its own), the ToB seam2 Verzik/Nylocas/Vasilias literals | no |
| 2026-10-03 | `94f55b306` | `src/app/app_world_rebuild.c` | `src/app/app_world_rebuild.c` | NOT taken: every raid hunk is the ground-obj list change (`client_ground_obj_merge`), which `v3` made itself in `5f37f7d87` (b53-seam1); the file stays `v3`'s | n/a |
| 2026-10-03 | `94f55b306` | `test/quests/_conformance.lua` | `test/quests/_conformance.lua` | driver gate, three-way merge: 17 verb rows and 4 seam rows in (158 verbs, 102 seams); the 3 conflicts kept `v3`'s side (`seam.two_copies_one_tile_both_takeable`, `seam.drain_survives_xp_gain`, the counts); left out `raid.enter/state/start_tile/leave`, `seam.nylocas_protect_blocks_wave_hit`, `seam.eat_does_not_hold_queued_hit`, `seam.eat_delay_clocks`, `seam.two_identical_drops_are_two_ground_rows`, `seam.two_cold_identical_piles_both_land`; `seam.attack_exact_copy_on_one_tile`, `seam.npc_state_size`, `seam.attack_fast_path` moved after `seam.drain_survives_xp_gain` | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/run.py` | `tools/waves_gate/run.py` | tool, renamed directory, then changed to `test/waves/` | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/gate.py` | `tools/waves_gate/gate.py` | tool, renamed directory, then changed: runs `waves_coverage.py`, skips it after a refusal | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/suite.py` | `tools/waves_gate/suite.py` | tool, renamed directory, then changed: `test/waves/`, and a second refusal for an id not named `<game>_<unit>` | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `tools/raid_gate/README.md` | `tools/waves_gate/README.md` | tool notes, rewritten for the waves layout; adds the measurement-scratch recipe | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `test/raids/README.md` | `test/waves/README.md` | notes, written from the raid file (ToB lessons kept as the general lesson list) | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `test/raids/fixtures/README.md` | `test/waves/fixtures/README.md` | notes, written from the raid file | yes: waves seam pass 1 `driver_port` (the closer's commit) |
| 2026-10-03 | `94f55b306` | `test/raids/fixtures/fresh_lumbridge.ini` | `test/waves/fixtures/fresh_lumbridge.ini` | fixture, byte-for-byte (also identical to `test/quests/fixtures/fresh_lumbridge.ini` at `d9c86ca89`) | no |

The four tools first copied (`spec_check.py`, `waves_coverage.py`, `frame_count.py`,
`vtt_to_md.py`) were adapted to `tools/waves_gate/`, `test/waves/` and the
`docs/minigames/inferno|colosseum/` layout by waves seam pass 1 `driver_port`;
`vtt_to_md.py` keeps `--raid` and adds `--game`, and `frame_count.py`'s command line is
unchanged.

Driver and engine files (the prayer verb, npc state and hazard reads, the tick log,
the step-on-tick verb, the fast attack press, the tests-directory override) were
copied by waves seam pass 1 `driver_port` (`build/seam_state/matthew-mbp-m4-waves-b1-seam1/`),
one row each above. Nothing behavioural in combat or consumption came with them: the
raid branch's `addXp` rule is already `v3`'s, its ground-obj rows are `v3`'s own, and
its eat-delay port and ToB content rows are content the waves branch does not carry.

The three workflow cards under `tools/waves_gate/workflows/` are written from the
raid cards' shape (`tools/raid_gate/workflows/raid_{spec,seam,author}.workflow.js` at
`94f55b306`) with the worktree path, the nouns and the phases changed; they are not
verbatim copies.

## Content: the eat-delay port (waves seam pass 3, `eat_delay_port`)

**Kept as a patch, not on the branch (seam pass 3 closer, 2026-10-03):** the port reddened green
quest tests (troll, contact, regicide: deaths in fights that eat), so its files were restored from
`HEAD` and the change is `docs/minigames/waves_loop/patches/matthew-mbp-m4-waves-b1-seam3.eat_delay_port.{content,parent}.patch`.
The rows below say what the patch copies; none of these copies is on the branch until the owner
lands it. The `player_magic.rs2` row at the end belongs to `shared_combat_rules` and IS on the branch.

Raid CONTENT commit `7936c59bf9` (OSRS-Content, raid seam6 `eat_delay_port`, parent `2cddff56d5`), content merge base with `v3` `315ffdff00`. Since that base neither side had touched any of these files except `pack/varp.alloc` (`v3`: `a9aefa1206`, `28793800be`), so every other file was written byte-for-byte from `git show 7936c59bf9:<path>` (cmp-checked) and only then changed where the last column says. The behaviours were re-checked against the pinned modern wiki (`docs/minigames/inferno/sources/LEDGER_wiki.md`, eat-delay rows); nothing was left out. Not cherry-picked.

| Date | Raid commit | Path (same in both) | Kind | Changed since the copy |
|---|---|---|---|---|
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/pack/varp.alloc` | content, in place | yes: three-way merge: `v3` had allocated 7218-7222 since the base (`ft_jugs`, `ft_fluid_seed`, `bv_voy_*`), so the three consume varps are appended as 7223-7225 (`MERGE_CHECKLIST.md`) |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/bosses/boss_tormented_demons/scripts/td_consumables.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/general/scripts/food.rs2` | content, in place | yes: copied, then the delay comments re-sourced to Template:Fast foods table, and `[proc,food_pie_fast_half]` added: the half dragonfruit, wild and summer pie keep a 1-tick eat delay (template "1,1"; the port gave every pie 1,2) |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_nightmarezone/scripts/nightmarezone_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/configs/consumption/consume_delay.varp` | content, in place | yes: copied, then the ids renamed 7218/7219/7220 -> 7223/7224/7225 and the header comment re-sourced to the pinned wiki (no 2004 citation) |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/anti_poison.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/antifire_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/ape_atoll_food.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/attack_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/barbarian_mix.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/br_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/castlewars_brew.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/combat_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/consume_shared.rs2` | content, in place | yes: copied, then the varp ids renamed (21 references) and the header comment re-sourced to the pinned wiki pages with quotes; code otherwise verbatim |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/cox_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/deadman_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/defence_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/divine_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/energy_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/god_brew.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/hunter_meat.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/hunter_mix.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/inferno_potions.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/kebab.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/magic_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/misc_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/poison_karambwan.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/prayer_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/ranging_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/restore_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/sara_brew.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/skill_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/snail.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/strange_fruit.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/strength_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/ugthanki_kebab_bad.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/varlamore_potion.rs2` | content, in place, verbatim | no |
| 2026-10-03 | content `7936c59bf9` | `OSRS-Content/osrs239-content/server/scripts/player/scripts/consumption/venom_cure.rs2` | content, in place, verbatim | no |
| 2026-10-03 | `94f55b306` | `src/torirsserver/torirs_server_world_selftest.c` (the eat stanza, raid lines 19624-19653) | engine selftest, hunk only (the rest of the file is `v3`'s) | yes: the varp name is `varp7224_consume_food_delay` and the 2004 citation is replaced by the wiki Food/Fast foods quote |
| 2026-10-03 | content `7936c59bf9` (from `93707f5d60`) | `OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/player/player_magic.rs2` (the two damage-type hunks: `~pvm_spell_success` and `~pvm_spell_fail`, raid lines 470-483 and 506-511) | same path, in place | content, hunks only, verbatim (seam pass 3 `shared_combat_rules`); the third raid hunk (`~player_combat_stat` at the top of `~pvm_spell_cast`, cited to LostCity `changestat.rs2`) was NOT taken | no |

The two conformance rows that go with it (`seam.eat_does_not_hold_queued_hit`, `seam.eat_delay_clocks`, raid `94f55b306` `test/quests/_conformance.lua:9035-9238`) are verbatim in `build/seam_state/matthew-mbp-m4-waves-b1-seam3/conformance.eat_delay_port.lua` for the closer to merge after `seam.attack_fast_path`.

| Date | Raid commit | Path (same in both) | Kind | Changed since the copy |
|---|---|---|---|---|
| 2026-10-03 | `94f55b306` | `test/quests/_conformance.lua` (rows `seam.eat_does_not_hold_queued_hit` and `seam.eat_delay_clocks`; on the raid tip `origin/matthew-mbp-m4-raid-b1` they sit at lines 9253-9456) | driver gate, two seam rows, verbatim; HELD with the patch in `build/seam_state/matthew-mbp-m4-waves-b1-seam3/held/conformance_rows.eat_delay_port.lua` (not in `_conformance.lua` on the branch) | no |
