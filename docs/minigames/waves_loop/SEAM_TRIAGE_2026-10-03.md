# Waves seam pass 1 triage (2026-10-03): the driver port

Pass: `matthew-mbp-m4-waves-b1-seam1`. Written by the waves orchestrator. One seam,
one fixer: the files interlock (the Lua verbs need the C API, the C needs the server's
tick log, the gates need the conformance rows), so the port is done by one agent in
order and proved as a whole. Seam pass 2 adds what waves need beyond the raid loop's
verbs and measures the four engine findings; it is not in this file.

`docs/WAVES_ORCHESTRATOR.md` section 5 lists thirteen driver rows. The raid loop built
the first seven on `origin/matthew-mbp-m4-raid-b1`. Section 2 allows one verbatim copy
of them: in place for driver and engine files, under our own name for tools, each copy
recorded in `docs/minigames/waves_loop/FORKED_FROM.md` with the raid commit. Nothing
is cherry-picked or merged.

Facts the fixer starts from (measured by the orchestrator on 2026-10-03):

- Raid tip `94f55b306`; its merge base with `v3` is `80b58e323`; `v3` has moved 124
  commits since (`d9c86ca89` is our base).
- The raid branch's driver and engine work is 35 files, about 4,800 added lines
  (`git diff --stat 80b58e323 94f55b306 -- script/plugins src tools/quest_gate`),
  plus `test/quests/_conformance.lua` (+1,678) and the `tools/raid_gate/` wrappers.
- Files changed on BOTH sides since the merge base, which cannot be copied as whole
  files: `script/plugins/quest_driver/pointer.lua`, `src/app/app_world_rebuild.c`,
  `src/torirsserver/torirs_server_combat.c`,
  `src/torirsserver/torirs_server_world_selftest.c`, `test/quests/_conformance.lua`.
  (`tools/wiki_droptable.py` is also both-changed and is not part of this seam.)
- In OSRS-Content the raid branch's base is `315ffdff00`; outside the three raids'
  own directories it changed the consumption scripts (the eat-delay port, raid
  content commit `7936c59bf9`), `player/death.rs2`, `general/scripts/food.rs2`,
  `pack/varp.alloc` and a few others. None of that is this seam: it is behaviour,
  it belongs to seam pass 2's engine findings, and it must not be copied here.

## driver: driver_port

Units: every Inferno and Colosseum unit (no wave test or fought spec measurement can
be written without these verbs).

Files: `script/plugins/plugin_api.meta.lua`, `script/plugins/quest_driver.lua`,
`script/plugins/quest_driver/combat.lua`, `script/plugins/quest_driver/core.lua`,
`script/plugins/quest_driver/pointer.lua`, `script/plugins/quest_driver/prayer.lua`,
`script/plugins/quest_driver/spell.lua`, `script/plugins/quest_driver/ticklog.lua`,
`script/plugins/quest_driver/ui.lua`, `script/plugins/quest_driver/world.lua`,
`script/plugins/quest_driver/waves.lua`, `src/app/app_plugin_drive_events.c`,
`src/app/app_world_rebuild.c`, `src/app/app_world_spawn.c`,
`src/game/task_exec_entity_info.c`, `src/makefile`, `src/plugin/torirs_plugin_drive.c`,
`src/plugin/torirs_plugin_drive.h`, `src/plugin/torirs_plugin_drive_ticklog.c`,
`src/plugin/torirs_plugin_drive_ui.c`, `src/torirsserver/torirs_server.h`,
`src/torirsserver/torirs_server_combat.c`, `src/torirsserver/torirs_server_scripts.c`,
`src/torirsserver/torirs_server_ticklog.c`, `src/torirsserver/torirs_server_world.c`,
`src/torirsserver/torirs_server_world_selftest.c`, `src/torirsserver/torirs_server_zone.c`,
`src/world/entity_npc.h`, `src/world/entity_projectile.h`, `src/world/entity_spotanim.h`,
`src/world/world.c`, `tools/quest_gate/conformance.py`, `tools/quest_gate/gate.py`,
`tools/quest_gate/quest_list.py`, `tools/quest_gate/run.py`, `tools/waves_gate/run.py`,
`tools/waves_gate/gate.py`, `tools/waves_gate/suite.py`, `tools/waves_gate/README.md`,
`tools/waves_gate/spec_check.py`, `tools/waves_gate/waves_coverage.py`,
`tools/waves_gate/frame_count.py`, `tools/waves_gate/vtt_to_md.py`,
`test/waves/README.md`, `test/waves/fixtures/README.md`,
`test/waves/fixtures/fresh_lumbridge.ini`, `test/quests/_conformance.lua`,
`tools/quest_gate/verb_list.py`, `docs/minigames/waves_loop/FORKED_FROM.md`

Summary. Bring the raid loop's seven driver rows onto the waves branch and prove them
here: the tests-directory override (`TORIRS_QUEST_TESTS_DIR`, `TORIRS_QUEST_PUBLISH_DIR`
in `tools/quest_gate/quest_list.py` and `run.py`, changing nothing when unset, with
evidence under `selftest/minigames/`), `t.prayer.set / read / points`, `t.npc.state /
await_anim`, `t.world.spotanims / projectiles / hazard_at`, the server tick and the
per-tick server log with `t.ticklog.start / rows / gaps / mark / slot`,
`t.player.step_tick`, and the fast attack press (`t.player.attack` and `t.player.cast`
with a deadline of one or two ticks land or answer `covered` at once, naming the copies
offered). Method, in this order:

1. Read `git show origin/matthew-mbp-m4-raid-b1:docs/minigames/raid_loop/DRIVER_NOTES.md`
   (what each verb does and what its authors tripped on) and the six raid driver commits
   (`git log --oneline 80b58e323..94f55b306 -- script/plugins src tools/quest_gate`).
2. For each file in the list that `v3` has NOT changed since `80b58e323`
   (`git diff --quiet 80b58e323 origin/v3 -- <path>` exits 0): write the raid tip's
   content verbatim (`git show "94f55b306:<path>" > <path>`).
3. For the five both-changed files: a three-way merge with `git merge-file` (ours = the
   file at our HEAD, base = `80b58e323:<path>`, theirs = `94f55b306:<path>`), every
   conflict resolved by reading both sides; `v3`'s side is never dropped.
4. NOT copied, and why. `script/plugins/quest_driver/raid.lua` (`t.raid.*` enters ToB,
   ToA and CoX rooms through debugprocs that exist only in the raid branch's content):
   the loader registers an empty `waves.lua` part in its place, which seam pass 2 fills
   with `t.wave.enter / state`. Every `raid.*` conformance row is left out, with the
   counts adjusted. Any hunk whose only purpose is a behaviour change in combat or
   consumption (the retaliation rule, the magic damage type, the eat delay, the stat
   drain: the four engine findings of section 5) is left out of this seam: list each
   such hunk with file and line in your report, because seam pass 2 measures the
   behaviour on `v3` first and then decides. A hunk that only RECORDS (the tick log's
   hooks in `torirs_server_combat.c`) is in. If a hunk does both, say so and keep the
   recording half.
5. Tools: `tools/raid_gate/run.py`, `gate.py`, `suite.py` and `README.md` are copied to
   `tools/waves_gate/` and then changed to read `test/waves/` and publish under
   `selftest/minigames/<game>/<unit>/play/`; the four tools already copied there
   (`spec_check.py`, `waves_coverage.py`, `frame_count.py`, `vtt_to_md.py`) are changed
   to the waves layout (`docs/minigames/inferno|colosseum/encounters/<unit>.tsv`, test id
   `<game>_<unit>`, e.g. `inferno_nibblers`, `colosseum_sol_heredit`). A wave id must
   never equal a quest id; the wrapper refuses if one does. `test/waves/README.md` and
   the fixture are written from `test/raids/README.md` and `test/raids/fixtures/`.
6. Every copied file gets a row in `docs/minigames/waves_loop/FORKED_FROM.md` (raid
   commit, raid path, our path, verbatim or merged or changed, and what was left out).
7. Prove it on an ordinary npc, never a boss: a scratch script that starts the tick
   log, sets Protect from Melee by the prayer book and reads it back, attacks a
   Lumbridge goblin with the fast press, reads `npc.state` and `await_anim`, measures the
   goblin's attack cadence from `t.ticklog.gaps`, steps one tile with `step_tick` and
   reads the tick it resolved on, and reads `hazard_at`, `spotanims` and `projectiles`
   during a spell cast. Quote the ledger rows.
8. Conformance: `test/quests/_conformance.lua` is yours in this seam (the driver's own
   gate, not a quest test). Three-way merge the raid tip's rows into it, drop the
   `raid.*` rows, keep every row `v3` added since the merge base, set `VERB_COUNT`,
   `SEAM_COUNT` and their `-- @verb-count` / `-- @seam-count` comments to what is
   really there, and bring `tools/quest_gate/verb_list.py` in line if it needs it.
   Run the conformance file through `run.py` yourself and quote its totals; the closer
   runs the make targets.

Evidence. `git diff --stat 80b58e323 94f55b306 -- script/plugins src tools/quest_gate`
(35 files, 4,794 insertions); `docs/WAVES_ORCHESTRATOR.md` section 5's table, rows 1 to
7; on `v3`, `grep -n "prayer\|ticklog\|step_tick" tools/quest_gate/verb_list.py` finds
no such verb.
