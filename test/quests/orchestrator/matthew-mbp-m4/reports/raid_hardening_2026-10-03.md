# Quest hardening for the raid branch's eat-delay port and addXp change

Hardened copies (not committed, not in test/quests/):
`build/hardening/troll.lua`, `build/hardening/regicide.lua`, `build/hardening/deserttreasure.lua`.
Each has a unified diff beside it (`<id>.diff`, against `HEAD:test/quests/<id>.lua`, saved as `<id>.committed.lua`).
Saved ledgers for every run cited here are in `build/hardening/evidence/`, because the proof tree has been removed.

## Verdict

| quest | proof tree (raid + v3 merged) | v3 side (main checkout, `--no-build`) | lint | helper_coverage (committed -> hardened) |
|---|---|---|---|---|
| troll | GREEN x2 (hp_troll_5 57/0, hp_troll_6 57/0) | GREEN (harden_troll_v3 57/0) | clean | FULL 24 (23 DRIVEN, 1 TRAVEL) -> same |
| deserttreasure | GREEN x2 (hp_dt_17 261/0, hp_dt_18 261/0) | GREEN (harden_deserttreasure_v3 261/0) | clean | FULL 59 DRIVEN -> same |
| regicide | GREEN x2 under the account `regicide` (477/0 twice) | **RED** under the account `harden_regicide_v3`: 468/2. The 2 failures are `crossLogFromCamp` and `crossLogFromCamp-again` | clean | FULL 64 DRIVEN -> same |

**Regicide is NOT proven green on both sides.**
- The only red rows are `crossLogFromCamp` and `crossLogFromCamp-again`, at the start of leg 4, before any of my edits. Both are `settle_after_click`: the click got no map_flag or chat evidence within 30 ticks. The next row shows the log was crossed.
- These two rows depend on the account name. The **committed** file fails them the same way under any other account name:
  - proof tree: `hp_regicide_c1`
  - v3 side: `harden_regicide_committed_v3`, which also fails `leg6.pack` (coal 8<9) because 6 sharks were left over
  - three more runs of my copy under other names fail them too
- They pass under the account `regicide`, which is the name the real gate logs in as.
- Every regicide row I added or changed passes in every run.
- The two proof-tree greens are under the same account and match tick for tick (8343 / 8343), so they are copies of one run, not independent samples.

## The player's random stream is seeded from the account name

`torirs_server_save.c:268`: "a character re-seeds from its name". `--name X` logs in as X, so X picks the dice. Two runs under one name are nearly deterministic: the regicide proof runs `regicide_final_a` and `regicide_final_b` had identical ticks.

This decides how a "green" should be read:
- The committed troll, regicide and deserttreasure tests are green partly because their own account names roll well.
- Under other names I found several flakes that were there before this change. They are listed under "Flakes found that were there before" and, where cheap, hardened.
- Troll and deserttreasure were proven under fresh names, so each run is a separate sample.

## How the proof tree was built (merged)

- Parent worktree `build/orchestrator/worktrees/questproof` at `origin/matthew-mbp-m4-raid-b1` = `e6ac51434`, then `git merge --no-commit origin/v3` (`d9c86ca89`).
  - Content worktree at `7936c59bf9`, then `git merge --no-commit origin/v3` (content `c70422bdf6`).
  - `cache.osrs239` was symlinked to the main checkout. Nothing else was needed: run.py builds its own binary into `src/build_questtest` and its own script pack.
- **Parent conflicts (5).** In all five, both sides had landed the same fix independently: the addXp rule, the one-ground-row-per-OBJ_ADD client change and the selftest stanza. Each was resolved by taking v3's side (`git checkout --theirs`):
  - `src/torirsserver/torirs_server_combat.c` (addXp)
  - `src/torirsserver/torirs_server_world_selftest.c`
  - `src/app/app_world_rebuild.c` (v3's `app_obj_stack_land` is non-static, matching the raid `app_placeholder.c` call)
  - `test/quests/_conformance.lua`
  - `tools/wiki_droptable.py`
  - The `OSRS-Content` gitlink was staged at the content worktree's HEAD.
- **Content conflict (1): `osrs239-content/pack/varp.alloc`.** The raid branch's consume varps collided with v3's 7218-7222. They were renumbered in the scratch merge only:
  - `varp7218_consume_combo_delay` -> `varp7223_consume_combo_delay`
  - `varp7219_consume_food_delay` -> `varp7224_consume_food_delay`
  - `varp7220_consume_potion_delay` -> `varp7225_consume_potion_delay`
  - The three names appear only in `varp.alloc`, `player/configs/consumption/consume_delay.varp` and `player/scripts/consumption/consume_shared.rs2`; all three were edited.
  - A `var_prefix_names.py` dry run reported only a pre-existing `%action_delay` inside a LostCity quote comment.
  - The pack compiled (`compiled 42321 scripts`, fingerprint 27b80ba6).
  - Resolution patch: `evidence/proof_tree_content_merge_resolution.patch`.
- **Environment check.** The unmodified `troll` died at row 31 `player.died`, tick 465, at 2824,10077 L2, 26 sharks eaten, general at 1/30. That is exactly the raid note.
  - The unmodified `regicide` died at the end of leg 4: 12/12 sharks eaten, lowest 20/70, poisoned at the tripwire, first FAIL row 205 `goKillGuardAtSecondForest-walk-toForests`, 204/35, tick 4793. Also exactly the note.
- **Cleanup.** Both worktrees were removed after the symlink was deleted (`git worktree remove --force` for the content worktree first, then the parent, then `prune`). Nothing was committed, pushed or stashed.
  - In the main checkout I created only `build/hardening/` and the run directories `build/quest_gate/harden_troll_v3`, `harden_regicide_v3`, `harden_deserttreasure_v3` and `harden_regicide_committed_v3`.
- The raid branch has moved on since (now `d6e48b9b0`). The proof is against `e6ac51434` + v3 `d9c86ca89`.

## troll

**Before (proof tree):** row 31 `player.died` at tick 465, inside killGeneral's `await_dead_engaged`. All 26 sharks eaten (eat below 90), general at 1/30.

**Edits** (`troll.diff`, 72 lines):
- Setup `::setlevel prayer 99`.
  - Source: Protect from Melee needs Prayer 43 (wiki).
  - Quest Helper `TrollStronghold.java:133` lists "Food + prayer potions".
  - 99 points cover about 495 ticks at Protect from Melee's 1 point per 5 ticks at +0 prayer bonus (wiki Prayer). killGeneral took 204 ticks before the port.
- Protect from Melee is turned on through the prayer tab before `goto-killGeneral`, using the verbs-combat.md recipe. New row `killGeneral-protectMelee` reads `varb4118`.
  - Source: `troll_general*` is damagetype 1 (slash), attackrate 4, strength 140 + strengthbonus 100 (`combat_stats.generated.npc`). A prayed npc melee hit is 0 (`combat_stats.rs2 [proc,playerhit_n_melee_apply]`).
- Eat threshold 90 -> 75: a shark heals 20, so eating below 90 wasted food.
- New margin row `killGeneral-margin`: staged / at the generals / eaten / left / lowest hp / ticks.
- Setup `::setlevel agility 40` -> `70`. **This flake was there before.** `troll_climbingrocks` is `[label,rockslide_obstacle]` `stat_random(agility,160,300)`:
  - at 40 it slips about 16% of the time; `climbOverRocks` FAILed in `hp_troll_2`
  - at 68 and above it never slips

**Runs:**

| run | sharks staged / at generals / eaten / left | lowest hp | general fight (ticks; killGeneral row) | result |
|---|---|---|---|---|
| proof hp_troll_5 | 26 / 26 / 1 / 25 | 59/99 | 95 (138) | 57/0 green |
| proof hp_troll_6 | 26 / 25 / 0 / 25 | 80/99 | 120 (163) | 57/0 green |
| v3 harden_troll_v3 | 26 / 26 / 1 / 25 | 72/99 | 95 (138) | 57/0 green |

## regicide

**Before (proof tree):**
- `killGuard-dead` took 268 ticks and ate all 12 sharks (eat below 35, lowest 20/70).
- `crossTripwire` snagged ("You have been poisoned!"), `leg.4.end` read shark x0, and the character died. Tick 4793, 204/35.

**Edits** (`regicide.diff`, 80 lines). Every other row is byte-identical.
- Setup `::setlevel prayer 70`.
  - Source: the guard is pure melee (`regicide_tyras_guard.rs2 [ai_applayer2,regicide_old_camp_guard] ~npc_meleeattack`).
  - 70 points at 1 per 5 ticks last about 350 ticks; the slowest prayed fight was 235 ticks and used 45 points.
- Leg 4 turns Protect from Melee on before `killGuard` (row `killGuard-protectMelee`) and off after the kill (row `killGuard-prayerOff`). The local helper uses the same recipe.
- `killGuard-dead` now captures its detail (same row, same arguments). New row `killGuard-margin`.
- Setup `::give 4doseantipoison 1`.
  - Source: Quest Helper `Regicide.java:260` recommends antidotes/antipoisons (`ItemCollections.ANTIPOISONS`).
  - The tripwire is `regicide_traps.rs2:19-28` (`queue(poison_player, 0, 10)` + 2x5 damage).
  - Antipoison is `anti_poison.rs2` (`%varp102_poison = min(poison,-5)`).
- New row `crossTripwire-poison`: if `varp102_poison > 0`, drink the antipoison. It asserts poison <= 0 and hp > 25 before `leg.4.end`.
- Setup `::give shark 12` -> `2`. This is fewer sharks, not more, and it is forced by the pack:
  - Setup must stay at 26 slots or fewer. With 27, leg 1's cloth wrap had no free slot ("You don't have space to do that.", `lightArrow` FAIL in hp_regicide_1).
  - Leg 6 adds 10 slots (cloth, rabbit, 8 coal) to a pack of 15 + the antipoison. Leg 5 eats 2 sharks at its start.
  - The committed run reached leg 4's end with 0 sharks. Prayed, the guard eats almost none:
    - 12 staged broke `leg6.pack`
    - 4 staged filled leg 6 to 28/28
    - 2 staged leaves 26/28
  - The committed file itself overflows leg 6 when sharks survive: `harden_regicide_committed_v3`, `leg6.pack` short coal 8<9, 6 sharks left.

**Runs:**

| run | account | guard ticks | sharks staged / at guard / eaten / left | lowest hp in fight | prayer after | tripwire | result |
|---|---|---|---|---|---|---|---|
| proof regicide_final_a | regicide | 235 | 2 / 2 / 1 / 1 | 32/70 (margin holds on hp > 25) | 25/70 | poisoned 10 -> -5, hp 45/70 | 477/0 green |
| proof regicide_final_b | regicide | 235 | 2 / 2 / 1 / 1 | 32/70 | 25/70 | 10 -> -5, hp 45/70 | 477/0 green (identical replica) |
| proof hp_regicide_3 | foreign | 136 | 2 / 2 / 0 / 2 | 39/70 | 43/70 | 10 -> -5, hp 31/70 | 468/2: crossLogFromCamp x2 only |
| v3 harden_regicide_v3 | foreign (mandated) | 196 | 2 / 2 / 0 / 2 | 70/70 | 32/70 | not poisoned, hp 70/70 | 468/2: crossLogFromCamp x2 only, gate RED |

- Control: the committed file under foreign accounts also fails crossLogFromCamp x2:
  - `hp_regicide_c1` (proof tree): 464/2
  - `harden_regicide_committed_v3` (v3 side): 463/3, plus `leg6.pack`
- Margin note: the guard fight enters at about 32 hp after the leg's traps. With 2 sharks the margin row holds on "lowest > 25" or "left >= 2". It stays thin by design; the pack has no room for more food.

## deserttreasure

**Before (proof tree):**
- The unmodified file was GREEN as `deserttreasure` (v3 already carries the leg-5 restore fix and `killKamil` passed). But:
  - Damis' true form ended at 9/99, OUT OF shark
  - Kamil ate 13 sharks and ran OUT
  - `breakIce1` started at 12/99
- Under other accounts the committed behaviour failed:
  - `pickChestLocks` missed 14/14 (hp_dt_1)
  - Damis' true form killed the character 2/2 (hp_dt_3, hp_dt_4; Damis code unchanged there)
  - unprayed Kamil killed hp_dt_12
  - `killIceTrolls` reached only 3-4 of 5 in its 12 rounds (hp_dt_9, 10, 13, 14), because the cold now really drains
  - a one-shot ice block failed `breakIce1-dead` (hp_dt_14)
  - `reward.magic_xp` read 20006 (hp_dt_13, 14)
  - Water Blast ran out of death runes at Dessous (hp_dt_15)

**Edits** (`deserttreasure.diff`, 274 lines):
- **Magic for Kamil.** The leg-5 restores already existed. Added row `killKamil-magic`, which asserts `t.skill.read("magic").level >= 59` right before `killKamil-engage`. All proof and v3 runs read 98-99/99.
- **Prayer 99 + Protect from Melee for Damis, both forms.**
  - Turned on before `waitForDamis-goto`. Turning it on after his spawn let a wanderer claim the player: "I'm already under attack", hp_dt_5 and hp_dt_6.
  - Turned off after the kill. New row `killDamis-margin` covers both forms.
  - Source: `fd_damis_normal` / `fd_damis_tougher` are damagetype 2 (crush) in `deserttreasure.npc`.
  - His aura drains `stat(prayer)/20 + 1` per attack (`[proc,dt_damis_prayer_drain]`), so Protect from Melee covers only about 90 ticks of the true form, and Prayer reaches 0.
- **Super restores for prayer at Kamil.** Leg-5 restore potions changed from `4dosestatrestore` to `4dose2restore`.
  - Source: the same Quest Helper `RESTORE_POTIONS` list, `ItemCollections.java:532-540`.
  - `[proc,super_restore_effect]` heals 8+25% including Prayer. Two doses before Kamil give about 75 prayer.
- **Protect from Melee at Kamil**, on before `goto-killKamil` and off after.
  - Source: Quest Helper `DesertTreasure.java:540`, "protect from melee".
  - `icediamond_icewarrior` slashes for up to 22. His freeze is magic, max 5 (`^dt_kamil_freeze_maxhit`).
- **Ice trolls.** Inside the troll loop, drink a restore dose once the cold has taken 30 levels (raid addXp change). New margin row `killIceTrolls-margin`.
- **Pack flow.** Sharks now survive the prayed fights, and the kits used to rely on them being eaten.
  - `leg4.kit` now gives garlic, spice and cake first, then tops sharks up to 11 instead of 15 more.
    - The green run reached leg 5 with 11.
    - Before: garlic/spice "did not fit" (hp_dt_7, hp_dt_8) and leg 5's restores did not fit (hp_dt_9, hp_dt_10).
  - The pick loop gives `lockpick 1`, not 6, so left-over picks no longer take leg 3's shark slots.
- **Flakes that were there before, hardened** (see "Flakes found that were there before"):
  - Setup thieving 53 -> 99
  - Pick loop 14 -> 40 attempts, eating a shark below 30 hp
  - `leg2.kit-items` death runes 100 -> 150
  - `breakIce1-dead` accepts a one-cast shatter (checks `varb380_fd_icewarrior_dadfree == 1`)
  - `reward.magic_xp` accepts 20006 or 20007
- Note: like the committed file, this one tops up food and kit with mid-leg `::give` (leg kits). I added no new `::give` row. I only changed amounts or items in existing kit rows: deathrune 150, `4dose2restore`, the shark top-up, `lockpick 1`.

**Runs:**

| run | pickChest tries | Damis at / eaten / left / lowest | Damis2 ticks | trolls at / eaten / left / lowest, kill ticks | Kamil at / eaten / left / lowest | Kamil row ticks | result |
|---|---|---|---|---|---|---|---|
| proof hp_dt_17 | 6 | 20 / 15 / 5 / 32 | 341 | 13 / 2 / 11 / 74, 205 | 14 / 2 / 12 / 88 | 218 | 261/0 green |
| proof hp_dt_18 | ~11 | 20 / 14 / 6 / 36 | 289 | 13 / 2 / 11 / 68, 180 | 14 / 0 / 14 / 91 | 68 | 261/0 green |
| v3 harden_deserttreasure_v3 | many (209 ticks) | 17 / 15 / 2 / 33 | 348 | 13 / 2 / 11 / 74, 81 | 14 / 1 / 13 / 89 | 148 | 261/0 green |

- Damis is the thinnest fight. Over all 13 prayed runs that reached it (hp_dt_7 to hp_dt_18 and the v3 run), lowest hp was 21-41 and sharks left were 1-13; every margin row passed, two of them only on one leg (hp_dt_11: 2 left, lowest 21; hp_dt_15: 1 left, lowest 28).
- The pack caps food at about 20 sharks there, so "a quarter more than the worst run" cannot be staged. Prayer is the lever.

## Flakes found that were there before (not caused by the raid changes)

- **regicide `crossLogFromCamp` / `-again`.** Not hardened: changing these rows was out of scope, since every other row must stay byte-identical.
  - Fails `settle_after_click` under every account but `regicide`, with the committed file too.
  - The crossing happens; only the click's settle evidence is missing.
  - This is a driver or settle problem for whoever owns the regicide test.
- **regicide `leg6.pack`.** The committed file overflows when sharks survive leg 4 (`harden_regicide_committed_v3`). My copy fixes it with the 2-shark setup.
- **troll `climbOverRocks`.** Agility 40 slips about 16% of the time. Hardened.
- **deserttreasure `pickChestLocks`.** About 51% miss at thieving 53 over 14 tries. Hardened. Also, the port's `stat_random(thieving, 52, 128)` gives at most 50% a lock even at 99, which may be worth a content check against the wiki.
- **deserttreasure death-rune budget.** The green run left 7. Hardened.
- **deserttreasure `breakIce1-dead`.** One-shot shatter. Hardened.
- **deserttreasure `reward.magic_xp`.** Half-point parity. Hardened.

## For the raid orchestrator (reported, not fixed)

- The CONTENT_BUGS "Quest loop impact" entry for deserttreasure lists only Kamil's Magic. The eat-delay port also makes **Damis' true form** lethal: 2/2 deaths under other accounts, and the green run ended at 9/99 OUT. **Unprayed Kamil** is also lethal: 1 death, and 13 sharks eaten in the green run.
- Varp collision at merge time: the consume delay varps `7218/7219/7220` are already `varp7218_ft_jugs`, `varp7219_ft_fluid_seed` and `varp7220..7222_bv_voy_*` on content v3. They need renumbering, for example to 7223-7225 as above, in `varp.alloc`, `consume_delay.varp` and `consume_shared.rs2`.
- The parent merge of the raid branch with v3 conflicts in 5 files where both sides landed the same fix. Taking v3's side compiled and ran.

## Unified diffs

### troll

```diff
--- a/test/quests/troll.lua
+++ b/test/quests/troll.lua
@@ -16,8 +16,14 @@
         "::setlevel strength 99",
         "::setlevel defence 99",
         "::setlevel hitpoints 99",
-        "::setlevel agility 40",
+        -- Agility 70, not 40: the rocks are upass_obstacles.rs2 [label,rockslide_obstacle] stat_random(agility, 160, 300),
+        -- which slips back ~16% of the time at 40 (climbOverRocks FAILed in run hp_troll_2) and never at 68+.
+        "::setlevel agility 70",
         "::setlevel thieving 60",
+        -- Protect from Melee for the Troll General (needs Prayer 43; wiki Protect from Melee). Since the eat-delay port
+        -- (raid branch, OSRS-Content 7936c59bf9) an eat no longer holds his hits: 26 sharks ran out with him at 1/30.
+        -- 99 points cover ~495 ticks of the prayer's 1 point / 5 ticks drain at +0 prayer bonus (wiki Prayer).
+        "::setlevel prayer 99",
         "::give rune_scimitar 1",
         "::wield rune_scimitar",
         "::give shark 26",
@@ -231,18 +237,37 @@
         -- killGeneral: the prison key is a ground drop
         ------------------------------------------------------------------
         t.ticks(8) -- the scene the door teleport built settles before any npc slot is trusted
+        -- Protect from Melee before the generals' hall (recipe: verbs-combat.md "Turning on a protection prayer"): the
+        -- three generals are aggressive slash fighters (troll_general*: damagetype 1, attackrate 4, strength 140 +
+        -- strengthbonus 100, combat_stats.generated.npc), and a prayed npc melee hit is 0 (combat_stats.rs2 playerhit_n_melee_apply).
+        do
+            local tab_result = t.ui.tab("prayer")
+            t.ticks(2)
+            local wr, w = t.ui.widget("prayerbook:prayer15")
+            t.ui.invoke(w, 1)
+            t.ticks(2)
+            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
+            local _, pr = t.skill.read("prayer")
+            t.check("killGeneral-protectMelee", tab_result == "ok" and wr == "ok" and on == 1, "varb4118_prayer_protectfrommelee " .. tostring(on) .. "; prayer skill " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
+        end
+        local _, sharks_at_general = t.inv.count("shark")
         t.exec("goto-killGeneral", t.player.goto_tile, 2835, 10088, 2)
         t.ticks(4)
         local have_prison_key = false
         local general_symbols = { "troll_general", "troll_general2", "troll_general3" }
         local general_tries = 0
         local general_notes = {}
+        local general_lowest, general_ticks = nil, 0 -- margin row below: lowest hp and ticks over every general fought
         while (not have_prison_key) and general_tries < 6 do
             general_tries = general_tries + 1
             local sym = general_symbols[((general_tries - 1) % 3) + 1]
             local attack_result = t.player.attack(sym, 2, 4)
             if attack_result == "ok" then
-                local dead_result = t.npc.await_dead_engaged(240, 40, { eat = { item = "shark", below = 90 } })
+                -- eat below 75, not 90: a shark heals 20, so below 90 spent food on hits the prayer already stops
+                local dead_result, dead_detail = t.npc.await_dead_engaged(240, 40, { eat = { item = "shark", below = 75 } })
+                local lowest_here = tonumber(tostring(dead_detail):match("lowest hp (%d+)/"))
+                if lowest_here and (general_lowest == nil or lowest_here < general_lowest) then general_lowest = lowest_here end
+                general_ticks = general_ticks + (tonumber(tostring(dead_detail):match("dead after (%d+) tick")) or 0)
                 general_notes[#general_notes + 1] = sym .. " attack=" .. tostring(attack_result) .. " dead=" .. tostring(dead_result)
                 if dead_result == "ok" then
                     t.ticks(4)
@@ -257,6 +282,14 @@
         end
         t.check("killGeneral", have_prison_key, "troll_key_prison in the backpack after " .. general_tries
             .. " general(s): " .. table.concat(general_notes, "; "))
+        local _, sharks_after_general = t.inv.count("shark")
+        local _, hp_after_general = t.skill.read("hitpoints")
+        t.check("killGeneral-margin", (sharks_after_general or 0) >= 2 or (general_lowest or 0) > 25,
+            "sharks staged 26, at the generals " .. tostring(sharks_at_general) .. ", eaten in the fight "
+            .. tostring((sharks_at_general or 0) - (sharks_after_general or 0)) .. ", left " .. tostring(sharks_after_general)
+            .. ", lowest hp in the fight " .. tostring(general_lowest) .. "/99, hp after "
+            .. tostring(type(hp_after_general) == "table" and (hp_after_general.current or hp_after_general.level) or hp_after_general)
+            .. ", " .. general_ticks .. " tick(s) of general fighting (margin: sharks left >= 2 or lowest hp > 25)")
 
         ------------------------------------------------------------------
         -- goDownInStronghold, goThroughPrisonDoor, goDownToPrison
```

### regicide

```diff
--- a/test/quests/regicide.lua
+++ b/test/quests/regicide.lua
@@ -33,7 +33,19 @@
         "::setlevel defence 40",
         "::give magic_shortbow 1",
         "::give rune_arrow 150",
-        "::give shark 12",
+        -- Since the eat-delay port (raid branch, OSRS-Content 7936c59bf9) an eat no longer holds the Tyras guard's hits: the
+        -- 12 sharks ran out (lowest 20/70) and the tripwire's poison finished the character. The guard is pure melee
+        -- (regicide_tyras_guard.rs2 [ai_applayer2,regicide_old_camp_guard] ~npc_meleeattack), so leg 4 prays Protect from
+        -- Melee (needs Prayer 43; 70 points last ~350 ticks at 1 point / 5 ticks, wiki Prayer; the fight took 268).
+        "::setlevel prayer 70",
+        -- 2 sharks, not 12: prayed, the guard fight eats none, and the pack has no room for more. The run used to end
+        -- leg 4 with 0 sharks; leg 6 adds 10 slots (cloth, rabbit, 8 coal) to a pack of 15 + this antipoison, and leg 5
+        -- eats 2 at its start, so 4 staged reached leg 6 as 2 and filled it to 28 of 28 (harden_regicide_v3). Leg 1's
+        -- cloth wrap also needs a free slot: setup must stay <= 26 slots (27 broke lightArrow, hp_regicide_1).
+        "::give shark 2",
+        -- Quest Helper Regicide.java:260 recommends antidotes/antipoisons (ItemCollections.ANTIPOISONS) for the
+        -- tripwire's poison (regicide_traps.rs2:23 queue(poison_player, 0, 10)).
+        "::give 4doseantipoison 1",
     },
     bind = {
         varp = "varp328_regicide_quest",
@@ -877,10 +889,34 @@
             local _, me = t.world.tile()
             t.check("climbThroughForest-o1-tile", me.x == 2231 and me.z == 3149, "standing at " .. me.x .. "," .. me.z .. " :: " .. last_lines(3))
             t.exec("guard.arrived", t.npc.await_present, "regicide_old_camp_guard", 12, 10)
+
+            -- Protect from Melee for the guard (recipe: verbs-combat.md "Turning on a protection prayer"); a prayed npc
+            -- melee hit is 0 (combat_stats.rs2 playerhit_n_melee_apply). Turned off after the kill.
+            local function protect_melee(name, want)
+                local tab_result = t.ui.tab("prayer")
+                t.ticks(2)
+                local wr, pw = t.ui.widget("prayerbook:prayer15")
+                t.ui.invoke(pw, 1)
+                t.ticks(2)
+                local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
+                local _, pr = t.skill.read("prayer")
+                t.check(name, tab_result == "ok" and wr == "ok" and on == want, "varb4118_prayer_protectfrommelee " .. tostring(on)
+                    .. " (want " .. want .. "); prayer " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
+            end
+            protect_melee("killGuard-protectMelee", 1)
+            local _, sharks_at_guard = t.inv.count("shark")
 
             -- killGuard: a real fight (lvl 110); the quest queues regicide_quest_guard_defeated on its death
             t.exec("killGuard", t.player.attack, "regicide_old_camp_guard", 2, 30)
-            t.exec("killGuard-dead", t.npc.await_dead_engaged, 400, 3, { eat = { item = "shark", below = 35 } })
+            local _, guard_detail = t.exec("killGuard-dead", t.npc.await_dead_engaged, 400, 3, { eat = { item = "shark", below = 35 } })
+            local guard_lowest = tonumber(tostring(guard_detail):match("lowest hp (%d+)/"))
+            local _, sharks_after_guard = t.inv.count("shark")
+            t.check("killGuard-margin", (sharks_after_guard or 0) >= 2 or (guard_lowest or 0) > 25,
+                "sharks staged 2, at the guard " .. tostring(sharks_at_guard) .. ", eaten in the fight "
+                .. tostring((sharks_at_guard or 0) - (sharks_after_guard or 0)) .. ", left " .. tostring(sharks_after_guard)
+                .. ", lowest hp in the fight " .. tostring(guard_lowest) .. "/70, guard dead after "
+                .. tostring(tostring(guard_detail):match("dead after (%d+) tick")) .. " ticks (margin: sharks left >= 2 or lowest hp > 25)")
+            protect_melee("killGuard-prayerOff", 0)
             t.ticks(3)
             t.expect("quest.stage.defeated_guard", t.quest.expect_stage("defeated_guard"))
 
@@ -891,6 +927,20 @@
             t.ticks(6)
             local _, tw = t.world.tile()
             t.check("crossTripwire-tile", tw.z >= 3155, "player " .. tw.x .. "," .. tw.z .. " :: " .. last_lines(3))
+            -- A snag poisons at severity 10 (regicide_traps.rs2:23; poison.rs2 [queue,poison_player]): drink the
+            -- brought-along antipoison when it did (anti_poison.rs2: %varp102_poison = min(poison, -5) cures it).
+            local _, poison_at_wire = t.var.server("varp102_poison")
+            local antipoison_note = "not poisoned"
+            if (poison_at_wire or 0) > 0 then
+                antipoison_note = "drank antipoison: " .. tostring(t.player.inv_op("4doseantipoison", 1))
+                t.ticks(3)
+            end
+            local _, poison_after_wire = t.var.server("varp102_poison")
+            local _, hp_wire = t.skill.read("hitpoints")
+            local hp_after_wire = type(hp_wire) == "table" and hp_wire.level or nil
+            t.check("crossTripwire-poison", (poison_after_wire or 1) <= 0 and (hp_after_wire or 0) > 25,
+                "varp102_poison " .. tostring(poison_at_wire) .. " -> " .. tostring(poison_after_wire) .. " (" .. antipoison_note
+                .. "), hitpoints " .. tostring(hp_after_wire) .. "/70 (floor 25)")
 
             local _, w = t.world.tile()
             local _, stage = t.quest.stage()
```

### deserttreasure

```diff
--- a/test/quests/deserttreasure.lua
+++ b/test/quests/deserttreasure.lua
@@ -7,10 +7,20 @@
     setup = {
         "::clearinv",
         "::give coins 1000",
-        "::setlevel thieving 53",
+        -- Thieving 99, not 53: each of the chest's three locks is stat_random(thieving, 52, 128) (deserttreasure.rs2:1565-1581,
+        -- deserttreasure.constant:56-57; value > random(256), torirs_server_scripts.c SS_OP_STAT_RANDOM): 36% a lock at 53
+        -- (4.6% an attempt, so 14 attempts miss 51% of the time) and 50% at 99 (12.8%). The player's random stream is seeded
+        -- from its name (torirs_server_save.c:268): 53 passed as "deserttreasure" and missed 14 of 14 as hp_dt_1.
+        "::setlevel thieving 99",
         "::setlevel magic 50",
         "::setlevel firemaking 50",
         "::setlevel slayer 10",
+        -- Prayer 99 for Protect from Melee at Damis (leg 3; needs 43; prayer potions are on Quest Helper's list,
+        -- DesertTreasure.java:302/:607). Since the eat-delay port (raid branch, OSRS-Content 7936c59bf9) an eat no longer
+        -- holds his hits: unprayed, the true form killed the character in 2 of 2 runs (hp_dt_3, hp_dt_4) and the green
+        -- one ended at 9/99, OUT OF shark. His aura drains Prayer (deserttreasure.rs2 [proc,dt_damis_prayer_drain]); leg 5's
+        -- super restores give it back for Kamil.
+        "::setlevel prayer 99",
         "::complete quest_digsite",
         "::complete quest_templeofikov",
         "::complete quest_touristtrap",
@@ -234,8 +244,10 @@
             and t.cheat("::setlevel strength 99") == "ok" and t.cheat("::setlevel defence 99") == "ok"
             and t.cheat("::setlevel hitpoints 99") == "ok",
             "magic 70 (water blast), attack/strength/defence/hitpoints 99 given: the guide lists water spells or melee gear for Fareed")
+        -- 150 death runes, not 100: Water Blast takes one a cast through Fareed, both Damis forms and Dessous; the green run
+        -- left 7 and hp_dt_15 ran out at Dessous ("You do not have enough Death Runes") after a longer true-form fight.
         t.check("leg2.kit-items", t.cheat("::give airrune 400") == "ok" and t.cheat("::give waterrune 400") == "ok"
-            and t.cheat("::give deathrune 100") == "ok" and t.cheat("::give tinderbox 1") == "ok"
+            and t.cheat("::give deathrune 150") == "ok" and t.cheat("::give tinderbox 1") == "ok"
             and t.cheat("::give gasmask 1") == "ok" and t.cheat("::give ice_gloves 1") == "ok"
             and t.cheat("::give rune_scimitar 1") == "ok" and t.cheat("::give rune_chainbody 1") == "ok"
             and t.cheat("::give rune_platelegs 1") == "ok" and t.cheat("::give rune_kiteshield 1") == "ok"
@@ -321,10 +333,16 @@
         t.exec("goto-getCross", t.player.goto_tile, 3169, 2965, 0)
         local picked = false
         local pick_tries = 0
-        for attempt = 1, 14 do
+        for attempt = 1, 40 do -- 40 at 12.8% an attempt: 0.4% to miss them all
             pick_tries = attempt
+            -- a miss costs 3 hitpoints (deserttreasure.rs2:1552 dt_shadow_pick_fail) and the chest is reached at ~64/99: eat first
+            local _, hp_pick = t.skill.read("hitpoints")
+            if type(hp_pick) == "table" and (hp_pick.level or 99) < 30 and (select(2, t.inv.count("shark")) or 0) > 0 then
+                t.player.inv_op("shark", 1)
+                t.ticks(3)
+            end
             if select(2, t.inv.count("lockpick")) == 0 then
-                t.cheat("::give lockpick 6")
+                t.cheat("::give lockpick 1") -- one, not six: a miss snaps one, and the picks left over took leg 3's shark slots
                 t.ticks(2)
             end
             t.player.click_loc("fd_bandit_shutchest", 1)
@@ -376,10 +394,33 @@
         -- Brought-along food (Quest Helper: combat gear): the kit gives hit "did not fit"; top up to 20 sharks now that the pack has room.
         t.check("leg3.food", t.cheat("::give shark 10") == "ok", "10 more sharks given as brought-along food; sharks now " .. tostring(select(2, t.inv.count("shark"))))
         t.ticks(2)
+        -- Protect from Melee for both forms (recipe: verbs-combat.md "Turning on a protection prayer"): Damis is a crush
+        -- fighter (fd_damis_normal / fd_damis_tougher damagetype 2, deserttreasure.npc) and a prayed npc melee hit is 0
+        -- (combat_stats.rs2 playerhit_n_melee_apply). The true form's aura drains it within ~90 ticks; then food. On before the
+        -- room: a prayer-tab detour between his spawn and the Attack let a wanderer claim the player first (hp_dt_5, hp_dt_6).
+        local function protect_melee(name, want)
+            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
+            local tab_result, wr = "ok", "ok"
+            if now ~= want then
+                tab_result = t.ui.tab("prayer")
+                t.ticks(2)
+                local pw
+                wr, pw = t.ui.widget("prayerbook:prayer15")
+                t.ui.invoke(pw, 1)
+                t.ticks(2)
+            end
+            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
+            local _, pr = t.skill.read("prayer")
+            t.check(name, tab_result == "ok" and wr == "ok" and on == want, "varb4118_prayer_protectfrommelee " .. tostring(now) .. " -> " .. tostring(on)
+                .. " (want " .. want .. "); prayer " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
+        end
+        protect_melee("killDamis-protectMelee", 1)
         t.exec("waitForDamis-goto", t.player.goto_tile, 2738, 5088, 0)
         t.exec("waitForDamis", t.npc.await_present, "fd_damis_normal", 15, 20)
+        local sharks_at_damis = select(2, t.inv.count("shark"))
         t.exec("killDamis1-engage", t.player.attack, "fd_damis_normal", 2, 20)
-        t.exec("killDamis1", t.npc.await_dead_engaged, 300, 40, { eat = { item = "shark", below = 60 } })
+        local _, damis1_detail = t.exec("killDamis1", t.npc.await_dead_engaged, 300, 40, { eat = { item = "shark", below = 60 } })
+        local damis_lowest = tonumber(tostring(damis1_detail):match("lowest hp (%d+)/"))
         t.exec("killDamis2-present", t.npc.await_present, "fd_damis_tougher", 15, 20)
         -- Single-way combat: the true form claims the player on spawn and every Attack answers "I'm already under attack."
         -- (docs/quest_authoring/gaps-combat.md ::passive); the type is held passive so the swing lands, Damis still dies for real.
@@ -401,11 +442,18 @@
         for round = 1, 60 do
             local cast_result = t.player.cast("water_blast", "fd_damis_tougher", 8)
             damis2_result, damis2_detail = t.npc.await_dead_engaged(8, 1, { eat = { item = "shark", below = 65 } })
+            local low = tonumber(tostring(damis2_detail):match("lowest hp (%d+)/"))
+            if low and (damis_lowest == nil or low < damis_lowest) then damis_lowest = low end
             if damis2_result == "ok" or damis2_result == "no_row" then
                 if damis2_result == "ok" then break end
             end
         end
         t.check("killDamis2", damis2_result == "ok", "killed the true form of Damis with water_blast casts and melee: " .. tostring(damis2_result) .. " " .. tostring(damis2_detail))
+        local sharks_after_damis = select(2, t.inv.count("shark"))
+        t.check("killDamis-margin", (sharks_after_damis or 0) >= 2 or (damis_lowest or 0) > 25,
+            "sharks at Damis " .. tostring(sharks_at_damis) .. ", eaten over both forms " .. tostring((sharks_at_damis or 0) - (sharks_after_damis or 0))
+            .. ", left " .. tostring(sharks_after_damis) .. ", lowest hp " .. tostring(damis_lowest) .. "/99 (margin: sharks left >= 2 or lowest hp > 25)")
+        protect_melee("killDamis-prayerOff", 0)
         t.ticks(2)
         t.check("killDamis-stage", select(2, t.var.server("varp5947_dt_shadow_stage")) == 100,
             "dt_shadow_stage = " .. tostring(select(2, t.var.server("varp5947_dt_shadow_stage"))) .. " a tick after the corpse (was 3 = ring, dt_shadow_complete = 100)")
@@ -473,9 +521,14 @@
             return tostring(type(tile) == "table" and (tostring(tile.x) .. "," .. tostring(tile.z)) or tile) .. " level " .. tostring(level)
         end
         -- Brought-along food and ingredients (Quest Helper: combat gear, garlic powder, spice, cake for leg 4).
-        t.check("leg4.kit", t.cheat("::give shark 15") == "ok" and t.cheat("::give fd_crushed_garlic 1") == "ok"
-            and t.cheat("::give spicespot 1") == "ok" and t.cheat("::give cake 1") == "ok",
-            "15 sharks, garlic powder, spice and a cake given as brought-along items for the blood diamond and the troll child; sharks " .. tostring(select(2, t.inv.count("shark"))))
+        -- The quest items first, then sharks topped up to 11 rather than 15 more: prayed, Damis leaves 5-13 sharks in the pack
+        -- (it used to leave none), so 15 more filled it -- the garlic and spice "did not fit" (hp_dt_7, hp_dt_8) and then
+        -- leg 5's kit had no room for its restore potions (hp_dt_9, hp_dt_10). The green run reached leg 5 with 11.
+        local leg4_sharks_had = select(2, t.inv.count("shark")) or 0
+        local leg4_sharks_give = math.max(0, 11 - leg4_sharks_had)
+        t.check("leg4.kit", t.cheat("::give fd_crushed_garlic 1") == "ok" and t.cheat("::give spicespot 1") == "ok"
+            and t.cheat("::give cake 1") == "ok" and (leg4_sharks_give == 0 or t.cheat("::give shark " .. leg4_sharks_give) == "ok"),
+            leg4_sharks_give .. " sharks (" .. leg4_sharks_had .. " carried), garlic powder, spice and a cake given as brought-along items for the blood diamond and the troll child")
         t.ticks(2)
         t.exec("goto-enterEntrana", t.player.goto_tile, 3045, 3236, 0)
         t.exec("enterEntrana", t.player.talk_to, "shipmonk", 1)
@@ -603,11 +656,13 @@
         -- (deserttreasure.rs2:1225 [softtimer,dt_ice_cold], ^dt_cold_interval = 10), and an xp drop no longer undoes it
         -- (LostCity Player.ts:1841-1851 addXp). Quest Helper's Ice diamond panel brings restore potions for it
         -- (quest-helper DesertTreasure.java:685 restorePotions = ItemCollections.RESTORE_POTIONS, which lists
-        -- _4DOSESTATRESTORE); a dose heals the five combat stats by 10 + 30% (restore_potion.rs2:35
-        -- [label,consume_effect_restore_potion]). Partial potions are drunk first; a dose swaps the obj, so the
+        -- _4DOSESTATRESTORE and _4DOSE2RESTORE); a super restore dose heals the combat stats by 8 + 25% (prayer_potion.rs2
+        -- [proc,super_restore_effect]). Partial potions are drunk first; a dose swaps the obj, so the
         -- verb's own settle is not the evidence (verbs-inventory-shops: sack Fill) -- the stat reading is.
         local cold_stats = { "attack", "strength", "defence", "magic" }
-        local restore_doses = { "1dosestatrestore", "2dosestatrestore", "3dosestatrestore", "4dosestatrestore" }
+        -- Super restores (the same RESTORE_POTIONS list, ItemCollections.java:532-540 _4DOSE2RESTORE): a dose also gives back
+        -- 8 + 25% Prayer (prayer_potion.rs2 [proc,super_restore_effect]), which Damis' aura took, for Protect from Melee at Kamil.
+        local restore_doses = { "1dose2restore", "2dose2restore", "3dose2restore", "4dose2restore" }
         local function stat_reading()
             local parts = {}
             for _, s in ipairs(cold_stats) do
@@ -643,7 +698,7 @@
         end
         -- Brought-along gear (Quest Helper: fire spells, spiked boots, restore potions) and food for the Ice Path.
         t.check("leg5.kit", t.cheat("::give death_spikedboots 1") == "ok" and t.cheat("::give firerune 400") == "ok" and t.cheat("::give deathrune 100") == "ok"
-            and t.cheat("::give abyssal_whip 1") == "ok" and t.cheat("::give 4dosestatrestore 2") == "ok" and t.cheat("::setlevel magic 99") == "ok",
+            and t.cheat("::give abyssal_whip 1") == "ok" and t.cheat("::give 4dose2restore 2") == "ok" and t.cheat("::setlevel magic 99") == "ok",
             "spiked boots, 400 fire runes and 100 death runes (fire blast), an abyssal whip for the ice trolls and two restore potions given as brought-along items, magic set to 99 (the Ice Path's cold drains a level per ten ticks); sharks " .. tostring(count("shark")))
         t.ticks(2)
         t.exec("wear-whip", t.player.equip, "abyssal_whip")
@@ -663,19 +718,33 @@
             if t.cheat("::passive trollrescue_icetroll_melee" .. index) ~= "ok" then troll_passive = false end
         end
         t.check("killIceTrolls-passive", troll_passive, "::passive on the seven ice troll types: they no longer swarm the player but still take hits and die (test affordance, gaps-combat)")
+        -- Food margin over the troll fights (the cold now really drains, so they run longer: raid branch addXp change).
+        local sharks_at_trolls = count("shark")
+        local trolls_lowest, trolls_ticks = nil, 0
         for round = 1, 12 do
             if select(2, t.var.server("varb378_fd_icewarrior_trollskilled")) >= 5 then break end
+            -- Since the raid branch's addXp change the cold's drain is not undone by the kills' xp: at 35-40 Attack the 12
+            -- rounds killed 3-4 of 5 (hp_dt_9, hp_dt_10, hp_dt_13, hp_dt_14). A restore dose once it has taken 30 levels.
+            if cold_deficit() >= 30 then drink_restores("drinkRestore-trolls" .. round) end
             local engaged = "no_row"
             for _, sym in ipairs({ "trollrescue_icetroll_melee1", "trollrescue_icetroll_melee2", "trollrescue_icetroll_melee3",
                 "trollrescue_icetroll_melee4", "trollrescue_icetroll_melee5", "trollrescue_icetroll_melee6", "trollrescue_icetroll_melee7" }) do
                 engaged = t.player.attack(sym, 2, 20)
                 if engaged == "ok" then break end
             end
-            t.npc.await_dead_engaged(60, 4, { eat = { item = "shark", below = 75 } })
+            local _, troll_detail = t.npc.await_dead_engaged(60, 4, { eat = { item = "shark", below = 75 } })
+            local low = tonumber(tostring(troll_detail):match("lowest hp (%d+)/"))
+            if low and (trolls_lowest == nil or low < trolls_lowest) then trolls_lowest = low end
+            trolls_ticks = trolls_ticks + (tonumber(tostring(troll_detail):match("dead after (%d+) tick")) or 0)
         end
         t.ticks(2)
         t.check("killIceTrolls", select(2, t.var.server("varb378_fd_icewarrior_trollskilled")) >= 5,
             "ice trolls killed with the scimitar: fd_icewarrior_trollskilled = " .. tostring(select(2, t.var.server("varb378_fd_icewarrior_trollskilled"))) .. " (needs 5)")
+        local sharks_after_trolls = count("shark")
+        t.check("killIceTrolls-margin", (sharks_after_trolls or 0) >= 2 or (trolls_lowest or 0) > 25,
+            "sharks at the trolls " .. tostring(sharks_at_trolls) .. ", eaten " .. tostring((sharks_at_trolls or 0) - (sharks_after_trolls or 0))
+            .. ", left " .. tostring(sharks_after_trolls) .. ", lowest hp " .. tostring(trolls_lowest) .. "/99, "
+            .. trolls_ticks .. " tick(s) to kills (margin: sharks left >= 2 or lowest hp > 25)")
         t.exec("goto-enterTrollCave", t.player.goto_tile, 2866, 3720, 0)
         t.exec("enterTrollCave", t.player.click_loc, "trollrescue_troll_cave_entrance", 1)
         t.ticks(4)
@@ -684,15 +753,51 @@
         t.ticks(2)
         -- Fire blast needs 59 Magic, and the walk and the troll fights have left it in the fifties.
         drink_restores("drinkRestore-killKamil")
+        -- Protect from Melee at Kamil (Quest Helper DesertTreasure.java:540 "Get into melee distance and protect from melee"):
+        -- he swings slash for up to 22 (icediamond_icewarrior strength 80 + 100, deserttreasure.npc), a prayed npc melee hit
+        -- is 0 (combat_stats.rs2 playerhit_n_melee_apply), and his freeze is max 5 (^dt_kamil_freeze_maxhit). Since the
+        -- eat-delay port (raid branch, OSRS-Content 7936c59bf9) an eat no longer holds his hits: unprayed he killed hp_dt_12.
+        local function protect_melee(name, want)
+            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
+            local tab_result, wr = "ok", "ok"
+            if now ~= want then
+                tab_result = t.ui.tab("prayer")
+                t.ticks(2)
+                local pw
+                wr, pw = t.ui.widget("prayerbook:prayer15")
+                t.ui.invoke(pw, 1)
+                t.ticks(2)
+            end
+            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
+            local _, pr = t.skill.read("prayer")
+            t.check(name, tab_result == "ok" and wr == "ok" and on == want, "varb4118_prayer_protectfrommelee " .. tostring(now) .. " -> " .. tostring(on)
+                .. " (want " .. want .. "); prayer " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
+        end
+        protect_melee("killKamil-protectMelee", 1)
+        local sharks_at_kamil = count("shark")
         t.exec("goto-killKamil", t.player.goto_tile, 2863, 3753, 0)
+        -- Fire Blast needs Magic 59 (the cold drains a level per ten ticks, deserttreasure.rs2:1225 [softtimer,dt_ice_cold]).
+        local _, magic_at_kamil = t.skill.read("magic")
+        local magic_now = type(magic_at_kamil) == "table" and magic_at_kamil.level or nil
+        t.check("killKamil-magic", (magic_now or 0) >= 59, "magic " .. tostring(magic_now) .. "/"
+            .. tostring(type(magic_at_kamil) == "table" and magic_at_kamil.base_level) .. " before the first Fire Blast (needs 59)")
         t.exec("killKamil-engage", t.player.cast, "fire_blast", "icediamond_icewarrior", 8)
         local kamil_result, kamil_detail = "timeout", ""
+        local kamil_lowest = nil
         for round = 1, 40 do
             kamil_result, kamil_detail = t.npc.await_dead_engaged(60, 2, { eat = { item = "shark", below = 90 } })
+            local low = tonumber(tostring(kamil_detail):match("lowest hp (%d+)/"))
+            if low and (kamil_lowest == nil or low < kamil_lowest) then kamil_lowest = low end
             if kamil_result == "ok" then break end
             t.player.cast("fire_blast", "icediamond_icewarrior", 8)
         end
         t.check("killKamil", kamil_result == "ok", "killed Kamil with fire_blast: " .. tostring(kamil_result) .. " " .. tostring(kamil_detail))
+        local sharks_after_kamil = count("shark")
+        t.check("killKamil-margin", (sharks_after_kamil or 0) >= 2 or (kamil_lowest or 0) > 25,
+            "sharks at Kamil " .. tostring(sharks_at_kamil) .. ", eaten " .. tostring((sharks_at_kamil or 0) - (sharks_after_kamil or 0))
+            .. ", left " .. tostring(sharks_after_kamil) .. ", lowest hp " .. tostring(kamil_lowest) .. "/99 over every wait"
+            .. " (margin: sharks left >= 2 or lowest hp > 25)")
+        protect_melee("killKamil-prayerOff", 0)
         t.ticks(2)
         t.check("killKamil-stage", select(2, t.var.server("varb382_fd_icewarrior_subquest")) == 3,
             "fd_icewarrior_subquest = " .. tostring(select(2, t.var.server("varb382_fd_icewarrior_subquest"))) .. " a tick after the corpse (3 = Kamil dead), dt_ice_stage " .. tostring(select(2, t.var.server("varp5943_dt_ice_stage"))))
@@ -713,8 +818,14 @@
         -- The long walk to the blocks drains again; drink only if the cold has taken twenty levels since Kamil.
         if cold_deficit() >= 20 then drink_restores("drinkRestore-breakIce") end
         t.exec("goto-breakIce1", t.player.goto_tile, 2828, 3808, 2)
-        t.exec("breakIce1", t.player.cast, "fire_blast", "troll_block_1", 8)
-        t.exec("breakIce1-dead", t.npc.await_dead_engaged, 60, 3, { eat = { item = "shark", below = 60 } })
+        local _, ice1_detail = t.exec("breakIce1", t.player.cast, "fire_blast", "troll_block_1", 8)
+        if string.find(tostring(ice1_detail), "left the pool inside the settle", 1, true) then
+            -- one Fire Blast at restored Magic can shatter the block inside the cast's own settle (hp_dt_14): no fight to await
+            t.check("breakIce1-dead", select(2, t.var.server("varb380_fd_icewarrior_dadfree")) == 1,
+                "the block left the pool inside the cast's settle: dadfree " .. tostring(select(2, t.var.server("varb380_fd_icewarrior_dadfree"))))
+        else
+            t.exec("breakIce1-dead", t.npc.await_dead_engaged, 60, 3, { eat = { item = "shark", below = 60 } })
+        end
         t.exec("breakIce2", t.player.cast, "fire_blast", "troll_block_2", 8)
         for round = 1, 6 do
             t.ticks(3)
@@ -849,8 +960,14 @@
         })
         t.ticks(3)
         t.quest.expect_complete()
-        -- documented 20,006.9 Magic XP (dt_magic_reward_xp = 200069 tenths); the whole-unit readings of before/after differ by 20007
-        t.exec("reward.magic_xp", t.skill.expect_gain, "magic", 20007, snapshot)
+        -- documented 20,006.9 Magic XP (dt_magic_reward_xp = 200069 tenths). The whole-unit readings differ by 20007 or 20006
+        -- with the half point the run's spells left in the total (Fire Blast 34.5, Water Blast 28.5): the prayed fights cast
+        -- a different number of them, and hp_dt_13 / hp_dt_14 read 20006 for the full grant.
+        local _, magic_after = t.skill.read("magic")
+        local magic_before = snapshot and snapshot.magic and snapshot.magic.experience
+        local magic_gain = (type(magic_after) == "table" and magic_after.experience or 0) - (magic_before or 0)
+        t.check("reward.magic_xp", magic_before ~= nil and (magic_gain == 20007 or magic_gain == 20006),
+            "magic: before=" .. tostring(magic_before) .. " after=" .. tostring(type(magic_after) == "table" and magic_after.experience) .. " delta=" .. magic_gain .. " (20,006.9 documented)")
         t.finish(0)
         return
         -- LEG 6 END
```
