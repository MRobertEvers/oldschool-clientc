# Before the raid branch merges to v3

The raid branch (`matthew-mbp-m4-raid-b1`, parent and OSRS-Content) reaches v3 by one PR
per repo, content first, merge commits, when the owner asks. These must be done first.
Found is not fixed: each item stays here until the commit that settles it is named.

## 1. Var id collisions (found 2026-10-03, by the quest orchestrator)

The raid branch's content merge base with v3 is `315ffdff00`. Since then v3 allocated
`7218=varp7218_ft_jugs`, `7219=varp7219_ft_fluid_seed` (Forsaken Tower),
`7220=varp7220_bv_voy_bearing`, `7221`, `7222` (Bone Voyage). The eat-delay port
(OSRS-Content `7936c59bf9`) allocated `7218=varp7218_consume_combo_delay`,
`7219=varp7219_consume_food_delay`, `7220=varp7220_consume_potion_delay` on the branch.
Names carry their id, so a collision is a rename, not only a renumber.

At merge time, after `git -C OSRS-Content merge origin/v3` (taking v3's `pack/*.alloc`):

1. List every var the raid branch added: `git -C OSRS-Content diff $(git -C OSRS-Content
   merge-base HEAD origin/v3) HEAD -- osrs239-content/pack/varp.alloc
   osrs239-content/pack/varbit.alloc osrs239-content/pack/varc.alloc` (later seams may
   have added more than the three above).
2. For each whose id v3 now uses for another name, pick the next free id on v3 (7223 on
   2026-10-03; re-check), and rename it everywhere in BOTH repos by explicit path: the
   `.varp`/`.varbit` config header, the alloc line, every `.rs2` and `.constant` that
   names it (`player/scripts/consumption/consume_shared.rs2`,
   `player/configs/consumption/consume_delay.varp` and the food and potion scripts for
   the three above), `test/quests/_conformance.lua` and any `test/raids/*.lua`, C if it
   names one. Search scoped, never the whole content tree.
3. `make -C src torirsserver-scripts`, then `python3 OSRS-Content/tools/var_prefix_names.py`
   (dry run) must print `0 name(s) in 0 file(s)`.

## 2. Quest tests the raid branch turns RED

By the owner's decision (2026-10-03) the eat-delay port landed: an eat no longer holds a
queued npc hit. On the raid branch the quest suite reads 116 green and three RED:
`troll` and `regicide` (the player dies without the held hits) and `deserttreasure`
(since seam1's LostCity addXp rule: a drained stat stays drained). The quest orchestrator
(session Haiku Quests) took all three on 2026-10-03: it re-authors their food, prayer,
antipoison and restore staging after its batch b55 closes and proves each against this
branch's content, so they are green on both sides of the merge. The raid loop edits none
of them. Before merging, confirm with that session that the three are ready.

## 3. Shared tooling the raid branch changed

- `tools/quest_gate/quest_list.py`, `run.py`, `gate.py`: `TORIRS_QUEST_TESTS_DIR` and
  `TORIRS_QUEST_PUBLISH_DIR` overrides (unset = unchanged behaviour).
- `tools/quest_gate/conformance.py`: `MAX_FRAMES` 60000 -> 80000 (the eat rows).
- `test/quests/_conformance.lua`, `tools/quest_gate/verb_list.py`: 162 verbs, 105 seam
  rows on the branch; v3's counts will differ and the merge must add, not replace.
- `tools/wiki_droptable.py`: `MINIGAME_DEATH_QUEUES`.
- `tools/toa_fetch_wiki.py`: never overwrites a pinned page.

## 4. The owner's main checkout

Nine files there carry a stray copy of seam1's client edits (uncommitted, written by a
fixer to the wrong path on 2026-10-02): `src/app/app_plugin_drive_events.c`,
`src/app/app_world_spawn.c`, `src/game/task_exec_entity_info.c`,
`src/plugin/torirs_plugin_drive.h`, `src/plugin/torirs_plugin_drive_ui.c`,
`src/world/entity_npc.h`, `src/world/entity_projectile.h`, `src/world/entity_spotanim.h`,
`src/world/world.c`. The owner restores them; the raid loop was not permitted to.
