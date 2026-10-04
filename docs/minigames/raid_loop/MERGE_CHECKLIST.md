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

### 1a. Parent files where v3 and the raid branch both changed the same thing (2026-10-03)

The quest orchestrator's fixer merged this branch (`e6ac51434` / content `7936c59bf9`)
with v3 in a throwaway worktree and hit conflicts in five parent files, because v3 had
landed its own version of the same fixes: `src/torirsserver/torirs_server_combat.c` (the
LostCity addXp rule), `src/torirsserver/torirs_server_world_selftest.c`,
`src/app/app_world_rebuild.c` (one ground row per OBJ_ADD), `test/quests/_conformance.lua`
and `tools/wiki_droptable.py`. Taking v3's side for all five built and ran for their
proof, but that is NOT the merge to make: `_conformance.lua` and the selftest carry this
branch's rows and stanzas (113 seam rows, the tick log, the eat delay, the raid stanzas)
and must be merged by hand, v3's rows kept and ours added; for the three C/tool files
compare the two fixes line by line and keep one, with the other side's tests passing.
The content conflict was `pack/varp.alloc` only; renumbering the consume varps to
7223/7224/7225 in `varp.alloc`, `consume_delay.varp` and `consume_shared.rs2` compiled
(re-check the next free id at merge time).

## 2. Quest tests the raid branch turns RED

By the owner's decision (2026-10-03) the eat-delay port landed: an eat no longer holds a
queued npc hit. On the raid branch the quest suite reads 116 green and three RED:
`troll` and `regicide` (the player dies without the held hits) and `deserttreasure`
(since seam1's LostCity addXp rule: a drained stat stays drained). The quest orchestrator
(session Haiku Quests) took all three on 2026-10-03: it re-authors their food, prayer,
antipoison and restore staging after its batch b55 closes and proves each against this
branch's content, so they are green on both sides of the merge. The raid loop edits none
of them. Before merging, confirm with that session that the three are ready.

**Done on the quest side (2026-10-03):** the three tests are hardened and on v3 (parent
`ac4ab4478`, OSRS-Content `a221108613`), proven green twice against this branch merged
with v3 and green on v3 itself (troll 57/0, regicide 477/0, deserttreasure 261/0); report
`test/quests/orchestrator/matthew-mbp-m4/reports/raid_hardening_2026-10-03.md` on v3.
Correction to this loop's note: with the eat-delay port deserttreasure's old test also
died to Damis's true form and to an unprayed Kamil, not only to the Magic level; the
hardened test prays Protect from Melee for both. If one of the three goes red after the
merge, send that session the first failing row.

**forgettabletale (2026-10-03, seam8):** RED on the raid branch because seam8 fixed
`ToriRSServer_MusicEnterRegion` (it wrote a music row's variable index as a varp id). The
test's setup `::complete quest_fishingcompo` names a dbrow with no ::complete arm (the arm
is `quest_fishingcontest`) and was green only through that stray write to varp 11. The
quest orchestrator was told with the one-line fix; a corrected copy ran 292/292.
**Fixed on v3 (2026-10-03, parent `8eae3e3ff`, OSRS-Content `e3722b0292`):**
forgettabletale stages `::complete quest_fishingcontest` (292/0), and a sweep fixed the
same no-arm mistake in ghostsahoy, mortton (`quest_priestinperil`) and mourningsendpartii
(`quest_mourningsendpart1`); `lint_quest.py` now refuses a setup `::complete` with no arm.
Those three tests' prerequisites are really set now: after merging v3 into this branch,
run the four and send that session the first failing row of any that moves.

## 2a. The content worktree is not clean (2026-10-03, seam8)

Two fixer runs without `--no-publish` rewrote published evidence in
`OSRS-Content/osrs239-content/server/scripts/selftest/quests/quest_cook/play` and
`quest_druid/play` (113 PNGs deleted, 113 untracked, 2 ledgers modified, all uncommitted).
The seam8 closer's restore was refused by the permission check and the raid loop did not
route around it: the owner restores those two directories from HEAD (or says to). Closers
commit by explicit path, so nothing of it is in a commit; the tree must be clean before
the merge.

## 3. Shared tooling the raid branch changed

- `tools/quest_gate/quest_list.py`, `run.py`, `gate.py`: `TORIRS_QUEST_TESTS_DIR` and
  `TORIRS_QUEST_PUBLISH_DIR` overrides (unset = unchanged behaviour).
- `tools/quest_gate/conformance.py`: `MAX_FRAMES` 60000 -> 80000 (the eat rows).
- `test/quests/_conformance.lua`, `tools/quest_gate/verb_list.py`: 162 verbs, 105 seam
  rows on the branch; v3's counts will differ and the merge must add, not replace.
- `tools/wiki_droptable.py`: `MINIGAME_DEATH_QUEUES`.
- `tools/toa_fetch_wiki.py`: never overwrites a pinned page.
- seam17 (2026-10-04), the party run. A one-client run is untouched: cooks_assistant and
  druid ledgers are byte-identical to the branch base (closer, final tree), and the suite
  holds its buckets.
  - `tools/quest_gate/run.py`: `run_party`, `PARTY_RE`/`read_party_size`, `party_accounts`,
    `free_loopback_port`, `--party N`; `client_env` and `launch_client` take `extra_env`;
    `write_wrapper_script` takes `party` (it prepends `QD_PARTY`); `--all` runs a file that
    declares `party = N,` on its own, one at a time.
  - `tools/quest_gate/gate.py`: `PARTY_MARKER`, `party_seats`, `party_union`; `check_quest`
    rebuilds the union for a party run; duplicate-MD5 is grouped per raider in a party union;
    minimum-shape strips `p<n>:`. A run directory without `party.tsv` is never touched.
  - `tools/raid_gate/gate.py`: an id starting with `_` skips raid_coverage.
  - `test/quests/_conformance.lua`, `tools/quest_gate/verb_list.py` (unchanged): 177 verbs
    (12 new `party.*` rows after `raid.leave`), 159 seam rows on the branch.
  - `script/plugins/plugin_api.meta.lua`: `api_drive.barrier_mark`, `barrier_present`,
    `players`.

## 4. The owner's main checkout

Nine files there carry a stray copy of seam1's client edits (uncommitted, written by a
fixer to the wrong path on 2026-10-02): `src/app/app_plugin_drive_events.c`,
`src/app/app_world_spawn.c`, `src/game/task_exec_entity_info.c`,
`src/plugin/torirs_plugin_drive.h`, `src/plugin/torirs_plugin_drive_ui.c`,
`src/world/entity_npc.h`, `src/world/entity_projectile.h`, `src/world/entity_spotanim.h`,
`src/world/world.c`. The owner restores them; the raid loop was not permitted to.

## The eat-delay port is also landing on the waves branch (noted 2026-10-03)

The waves loop (branch `matthew-mbp-m4-waves-b1`) carried content commit `7936c59bf9` over
byte-for-byte (37 files) with the three `consume_delay.varp` ids renumbered to 7223-7225,
and re-sourced its comments to pinned OSRS wiki pages. Whichever branch merges second must
take the first one's varp numbers, not allocate three more, and must expect the 37 files to
conflict only in those ids and comments. On the waves branch the port also reddened the
quest test `contact` (row 214, the Giant Scarab), which stayed green here: tell the quest
session if it is red after either merge. Eat paths the port does not cover, found by the
waves loop: `minigame_toa/scripts/toa_supplies.rs2` (CONTENT_BUGS.md row, the ToA seams'),
`minigame_gauntlet/scripts/gauntlet_craft.rs2` (paddlefish) and `kebab.rs2:20`.

## A prayer fix is landing on the waves branch (noted 2026-10-03)

By the owner's decision ("Prayer does not regenerate") the waves loop changes shared files
`player/scripts/stat_restore.rs2`, `skill_prayer/scripts/prayer.rs2` and `player/death.rs2`:
the restore timer no longer restores prayer, there is no drain on the activation tick, the
drain counter is kept when prayers go off and zeroed on death, and each prayer's drain is
checked against the wiki's table (Chivalry drained at twice the rate). Every raid room
prays for most of its fight, so once both branches are on v3 re-run every kept room
(`tools/raid_gate/suite.py`) and expect prayer-point and potion counts to move: a room that
runs out of prayer is re-authored with more restores, the content is not bent back. The
waves session will send the commit when its seam pass 4 lands.

## Landed on the waves branch (2026-10-04): parent 457041eec, content c93c574f20

The eat port (34 of 36 files byte-equal to `7936c59bf9`; the two that differ are listed in
the waves loop's `docs/minigames/waves_loop/FORKED_FROM.md`), `consume_delay` varps
7223-7225, the Gauntlet paddlefish on the same rule, the prayer fix (no restore over time,
no drain on the activation tick, the drain counter zeroed on death, wiki drain rates), and
the default retaliation honouring an npc record's `retaliate=no`. This branch changed the
same retaliation path in its own seam (the tree-wide retaliation fix for the ToB bosses):
expect a hand merge there and keep both sides' behaviour. `toa_supplies.rs2` is untouched
on both branches and still open here.

Two rows that concern this loop's own work:
- `seam.eat_does_not_hold_queued_hit` (this loop's conformance row for the eat port) also
  passes on the OLD content, so it does not tell old from new (the waves loop's ENG-58).
  Open: make the row fail on the pre-port content (an eat inside the hit's delay window,
  asserting the hit's landing tick), in the next seam pass that touches conformance.
- `tools/check_gauntlet_contract.py` pins the old Gauntlet eat lines and is red on the
  waves branch until its pin is updated (the waves loop's ENG-56); not this loop's file.

## v3 after quest batch b56 (noted 2026-10-04): parent f2e91ffaa, content 4b277a1d8a

From the quest session, for the day this branch meets v3:
- `pack/varp.alloc` will conflict: b56 added varps 7240-7243, 7270-7272, 7290-7291 and
  7330-7335. This branch's three colliding varps take the waves branch's 7223-7225 for the
  eat port; any other new raid varp is renumbered above v3's highest at merge time.
- New quest-gate rules on v3 that the raid tests run under (they use the quest gate): the
  coverage grader reads the map's walls and grades a goto into or out of a closed space as
  a cheat, and lint refuses a mid-run `::bankgive` or an unmarked mid-run `::give`. The raid
  room tests arrive by the bring-along `t.raid.enter` (an instance teleport): after the
  merge run `tools/raid_gate/suite.py` and, if the wall grader flags the arrival, declare
  `t.raid.enter` to the grader as the raid's bring-along (a tooling row on this side) --
  never rewrite the tests to dodge it.
- The driver gained bank verbs on v3 (`t.bank.open / withdraw / deposit / count / close`):
  `test/quests/_conformance.lua` and `verb_list.py` conflicts keep both sides' rows.
- Fight-heavy quests new on v3 and not yet run under the eat port: `dreammentor` and the
  reworked `contact`. If either is red after this branch merges, send the quest session the
  first failing row; it fixes them on its side.

## The super restore no longer heals Hitpoints (seam15, 2026-10-04)

The owner confirmed on 2026-10-04 that the super restore must be fixed:
`[proc,super_restore_effect]` (prayer_potion.rs2) no longer heals Hitpoints (wiki [Super
restore] oldid 15183989 line 53, pinned under docs/minigames/theater_of_blood/sources/). It
reaches every quest and room that drinks `4dose2restore`, `br_4dose2restore` or a Castlewars
brew.
- Quest suite on this branch after the fix: 115 green, and deserttreasure, forgettabletale,
  regicide and troll RED with the same first failing rows as seam14 (the baseline, fixed on
  v3). No quest went red because its fight healed from restores.
- v3's quests that are not on this branch (b56 onwards, `dreammentor`, the reworked `contact`)
  have not been run without the heal. After the merge, any of them that goes red in a fight
  that drinks a super restore is the quest session's: send it the first failing row.
- Raid rooms that moved (SEAM_LEDGER.md, seam15): tob_verzik (P3 out of food at tick 636)
  and tob_maiden (one sound-range row on a changed walk). Both must be re-authored on this
  branch; neither is a merge blocker for v3.

## The Saradomin brew raises Defence (seam18, 2026-10-04)

The owner confirmed on 2026-10-04 that the brew must be fixed: `[label,consume_effect_sara_brew]`
(sara_brew.rs2) no longer drains Defence; a dose raises it by 2 + 20% of base (wiki [Saradomin
brew] revid 15322175 lines 56 and 89, pinned under docs/minigames/theater_of_blood/sources/). It
reaches every quest and room that drinks `4dosepotionofsaradomin` and its smaller doses. The ToA
supply brew (br_potion.rs2:80) still drains Defence; that is an open content row, not fixed here.
- Quest suite on this branch after the fix: 115 green, and deserttreasure, forgettabletale,
  regicide and troll RED with the same first failing steps as seam16 (the baseline, fixed on
  v3). No quest went red because its fight drank brews.
- v3's quests that are not on this branch (b56 onwards) have not been run with the fixed brew.
  After the merge, any of them that goes red in a fight that drinks a Saradomin brew is the
  quest session's: send it the first failing row.
- Raid rooms that moved (SEAM_LEDGER.md, seam18): tob_nylocas (the last support falls before
  Vasilias spawns at 490 ticks; 91 PASS / 24 FAIL). It must be re-authored on this branch; it is
  not a merge blocker for v3.
