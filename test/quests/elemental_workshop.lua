-- Elemental Workshop I (quest_elemental_workshop). Resumed from a rejected
-- attempt whose blocker turned out to be stale: it reported `covered` on
-- elemental_workshop_bookcase and "no verb for OPHELDU" for the book's spine
-- cut, both against a driver that has since grown the fix for each --
-- click_loc's own walk-to-another-side retry (QD.player._far_side_step,
-- pointer.lua) and t.player.use_item_on_item for a backpack item used on
-- another. This file drives the walkthrough for real.
--
-- Three symbol corrections the scaffold got wrong, checked against
-- quest_elemental_workshop.rs2 rather than guessed:
--   * elemental_workshop_box_1's own [oploc1,...] handler (rs2:142-152)
--     grants the STONE BOWL, and elemental_workshop_box_4's (rs2:166-176)
--     grants LEATHER -- backwards from the scaffold's step names. box_2/
--     box_4 are needle/leather FALLBACKS only (their own conditions require
--     the player to be carrying none already), moot here since setup
--     carries both already.
--   * elemental_workshop_workbench, elemental_workshop_furnace and
--     elemental_workshop_trough_2 are all OPLOCU (use-item-on-loc) targets
--     (elem2_helm.rs2:62, quest_elemental_workshop.rs2:185-201,259-297) --
--     t.player.use_on, never click_loc.
--   * [opheldu,elemental_workshop_shield_book] additionally requires
--     %elemental_workshop_book = 1, set only by READING the book first
--     ([opheld1,...], two mesbox pages) -- the scaffold skipped that step.
--
-- Door rule (b64 re-drive, owner rulings 2026-10-03 / 2026-10-05). Every
-- crossing is pressed, nothing is goto'd into or out of a closed space, and
-- no goto lands on a loc:
--   * Lumbridge -> Seers' Village is a real Camelot Teleport (the walk crosses
--     the Taverley members' gate), cast from the spellbook by click; the hop
--     from the Camelot landing to the house is open overland travel
--     (reach.py REACH closed-doors len=50).
--   * The bookcase room is walled in with ONE door, kr_poordoor 2713,3483
--     (reach.py NEEDS-DOOR) -- pass_door in and out.
--   * The odd wall 2709,3495 is the only way into the stairwell
--     (elem1_walk_wall, rs2:236-250, a p_teleport through the wall with no
--     loc change): a walk-through, cross_gate graded on the tile.
--   * The spiral stairs (rs2:252-254) p_teleport to 2716,9888 in the
--     underground's map frame: a climb.
--   * The workshop below is one walkable floor (reach.py REACH between every
--     pair of standing tiles below): each machine is pressed from an open
--     tile beside it that a walk_to row reaches, never from its own footprint.
return {
    id = "elemental_workshop",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so requirements fit
        "::give knife 1", -- cuts the battered book's spine
        "::give bronze_pickaxe 1", -- mines the elemental rock
        "::give needle 1", -- repairs the bellows
        "::give thread 1", -- repairs the bellows
        "::give leather 1", -- repairs the bellows
        "::give hammer 1", -- smiths the elemental bar into a shield
        "::give coal 4", -- smelts the elemental bar
        "::give rune_scimitar 1", -- the fixture's own gear cannot land a hit on a level-35 elemental (S8)
        "::give lobster 4", -- food for the level-35 earth elemental fight (margin row)
        "::setlevel magic 45", -- Camelot Teleport (magic_spells.dbrow [magic_spell_teleport_camelot]: level 45, 5 air + 1 law)
        "::give airrune 5", -- one Camelot Teleport, Lumbridge -> Seers' Village
        "::give lawrune 1",
        "::setlevel mining 20", -- elem2_gather.rs2:19's own floor
        "::setlevel smithing 20", -- quest_elemental_workshop.rs2:283,304's own floor
        "::setlevel crafting 20", -- quest_elemental_workshop.rs2:111's own floor
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel defence 60",
        "::setlevel hitpoints 60",
    },

    run = function(t)
        -- The quest's own varp is a 21-bit flag accumulator
        -- (configs/all.varbit's elemental_workshop_book/key/gate1/gate2/
        -- switch/bellows/fire/bellows_switch/boxes/stairs/leather/finished
        -- subfields on basevar varp299_elemental_workshop_bits), so there is
        -- no single linear "stage" integer to bind -- bind the completion
        -- varbit itself instead (S2's Prying Times precedent).
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb2067_elemental_workshop_finished",
            constants = {
                not_started = 0,
                complete = 1,
            },
            row = "quest_elementalworkshop1",
            display = "Elemental Workshop I",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("equipScimitar", t.player.equip, "rune_scimitar")

        -- A quest varbit read as a graded row (the machinery's own flags,
        -- quest_elemental_workshop.rs2): the effect of each press.
        local function flag(name, varbit, value)
            t.exec(name, t.var.await_server, varbit, value, 10)
        end
        -- A walk on one floor, graded on the exact tile it reaches.
        local function walk(name, x, z)
            t.exec(name, t.player.walk_to, x, z, 60)
        end

        -- Seers' Village: Camelot Teleport from Lumbridge (lands 2757,3478),
        -- then over open ground to the house's door.
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "searchBookcase.camelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot, tele_coord 0_43_54_5_22" })
        t.exec("goto-searchBookcase", t.player.goto_tile, 2713, 3485, 0)
        t.exec("searchBookcase.doorIn", t.player.pass_door, { closed = "kr_poordoor", open = "kr_poordooropen",
            at = { 2713, 3483, 0 }, near = { 2713, 3483 }, far = { 2713, 3481 } })

        -- Search the marked bookcase (oploc1, rs2:28-60) from the open tile
        -- beside it (the bookcase fills 2716,3481-3482).
        -- S8: a bare inv read right after a granting chat line can still see
        -- the OLD count -- poll with inv.await, not expect_has.
        walk("walk-searchBookcase", 2715, 3481)
        t.exec("searchBookcase", t.player.click_loc, "elemental_workshop_bookcase", 1)
        local book_await_result, book_await_detail = t.inv.await("elemental_workshop_shield_book", 1, 10)
        local book_count_result, book_count = t.inv.count("elemental_workshop_shield_book")
        t.check("gotBook", book_await_result == "ok",
            "inv.await(elemental_workshop_shield_book,1) -> " .. tostring(book_await_result) .. " " .. tostring(book_await_detail)
                .. "; count=" .. tostring(book_count_result == "ok" and book_count or book_count_result))

        -- Read it (opheld1) -- two mesbox pages, and the read is what sets
        -- %elemental_workshop_book = 1 that the spine cut needs. The read
        -- keeps the book; gotBookBack proves it is still held for the knife.
        t.exec("readBook", t.player.inv_op, "elemental_workshop_shield_book", 1)
        t.exec("readBook.dismiss", t.chat.drain, {})
        flag("readBook.var", "varb2056_elemental_workshop_book", 1)
        local book2_await_result, book2_await_detail = t.inv.await("elemental_workshop_shield_book", 1, 10)
        local book2_count_result, book2_count = t.inv.count("elemental_workshop_shield_book")
        t.check("gotBookBack", book2_await_result == "ok" and book2_count == 1,
            "inv.await(elemental_workshop_shield_book,1) after the read -> " .. tostring(book2_await_result) .. " " .. tostring(book2_await_detail)
                .. "; count=" .. tostring(book2_count_result == "ok" and book2_count or book2_count_result))

        -- Cut the spine with the knife (opheldu) -- item-on-item: the book
        -- leaves the pack, the slashed book and the battered key arrive.
        t.exec("cutSpine", t.player.use_item_on_item, "knife", "elemental_workshop_shield_book")
        local key_await_result, key_await_detail = t.inv.await("elemental_workshop_key", 1, 10)
        local key_count_result, key_count = t.inv.count("elemental_workshop_key")
        local intact_result, intact_count = t.inv.count("elemental_workshop_shield_book")
        local slashed_result, slashed_count = t.inv.count("elemental_workshop_shield_book_slashed")
        t.check("gotKey", key_await_result == "ok" and intact_result == "ok" and intact_count == 0
                and slashed_result == "ok" and slashed_count == 1,
            "inv.await(elemental_workshop_key,1) -> " .. tostring(key_await_result) .. " " .. tostring(key_await_detail)
                .. "; key count=" .. tostring(key_count_result == "ok" and key_count or key_count_result)
                .. "; battered book " .. tostring(intact_count) .. " (want 0), slashed book "
                .. tostring(slashed_count) .. " (want 1)")
        flag("cutSpine.var", "varb2057_elemental_workshop_key", 1)

        -- Out of the house by its door, then north on foot through the open
        -- double doors (reach.py REACH closed-doors len=17) to the odd wall.
        t.exec("openOddWall.doorOut", t.player.pass_door, { closed = "kr_poordoor", open = "kr_poordooropen",
            at = { 2713, 3483, 0 }, near = { 2713, 3482 }, far = { 2713, 3485 } })
        walk("walk-openOddWall", 2709, 3494)

        -- Open the odd wall (oploc1, rs2:203-250): the key lets the player
        -- through, a p_teleport with no loc change -- a walk-through, graded
        -- on the tile it lands in the stairwell (2709-2711,3496-3498).
        t.exec("openOddWall", t.player.cross_gate, { loc = "elemental_workshop_oddwall_l", at = { 2709, 3495, 0 },
            near = { 2709, 3494 },
            far_ok = function(tile) return tile.z >= 3496 and tile.z <= 3498 and tile.x >= 2709 and tile.x <= 2711 end,
            far_desc = "in the stairwell north of the odd wall (2709-2711,3496-3498)" })

        -- Climb down the spiral stairs (oploc1, rs2:252-254): p_teleport to
        -- 0_42_154_28_32 = 2716,9888 in the underground's map frame.
        t.exec("goDownStairs", t.player.climb, { loc = "elemental_workshop_spiralstairstop", op = 1,
            op_name = "Climb-down", at = { 2710, 3497, 0 }, dest = { 2716, 9888, 0 } })
        flag("goDownStairs.var", "varb2065_elemental_workshop_stairs", 1)

        -- North room: turn the water controls. East (x > 2719, rs2:73-81)
        -- must open FIRST -- it refuses while the west gate is already open.
        walk("walk-turnEastControl", 2726, 9907)
        t.exec("turnEastControl", t.player.click_loc, "elemental_workshop_valve_1", 1)
        t.chat.close()
        flag("turnEastControl.var", "varb2059_elemental_workshop_gate2", 1)

        walk("walk-turnWestControl", 2713, 9907)
        t.exec("turnWestControl", t.player.click_loc, "elemental_workshop_valve_2", 1)
        t.chat.close()
        flag("turnWestControl.var", "varb2058_elemental_workshop_gate1", 1)

        -- Pull the lever -- both gates open, so this starts the wheel
        -- (rs2:85-104).
        walk("walk-pullLever", 2723, 9906)
        t.exec("pullLever", t.player.click_loc, "elemental_workshop_water_lever", 1)
        t.chat.close()
        flag("pullLever.var", "varb2060_elemental_workshop_switch", 1)

        -- Central room's north-east crate: box_1 grants the stone bowl
        -- (rs2:142-152). Pressed from the open tile south of it.
        walk("walk-getStoneBowl", 2717, 9893)
        t.exec("getStoneBowl", t.player.click_loc, "elemental_workshop_box_1", 1)
        local bowl_await_result, bowl_await_detail = t.inv.await("elemental_workshop_lava_bowl", 1, 10)
        local bowl_count_result, bowl_count = t.inv.count("elemental_workshop_lava_bowl")
        t.check("gotBowl", bowl_await_result == "ok",
            "inv.await(elemental_workshop_lava_bowl,1) -> " .. tostring(bowl_await_result) .. " " .. tostring(bowl_await_detail)
                .. "; count=" .. tostring(bowl_count_result == "ok" and bowl_count or bowl_count_result))

        -- East room: repair the bellows (rs2:106-122) with the needle/thread/
        -- leather from setup (thread and leather are used up), then pull the
        -- air lever to start pumping (rs2:124-138) -- needs the wheel running.
        walk("walk-fixBellows", 2733, 9884)
        t.exec("fixBellows", t.player.click_loc, "elemental_workshop_bellows_multiloc", 1)
        t.chat.close()
        flag("fixBellows.var", "varb2061_elemental_workshop_bellows", 1)
        -- The backpack's copy can trail the server's var by a tick or two
        -- (S8): poll up to 6 ticks for the thread and leather to leave.
        local thread_result, thread_count, leather_result, leather_count
        local polled = 0
        repeat
            thread_result, thread_count = t.inv.count("thread")
            leather_result, leather_count = t.inv.count("leather")
            if thread_count == 0 and leather_count == 0 then break end
            polled = polled + 1
            t.ticks(1)
        until polled > 6
        t.check("fixBellows.materials", thread_result == "ok" and thread_count == 0
                and leather_result == "ok" and leather_count == 0,
            "after the repair: thread " .. tostring(thread_count) .. " (want 0, 1 staged), leather "
                .. tostring(leather_count) .. " (want 0, 1 staged) after " .. polled
                .. " tick(s) -- rs2:119-120 inv_del")

        walk("walk-pullBellowsLever", 2734, 9888)
        t.exec("pullBellowsLever", t.player.click_loc, "elemental_workshop_air_lever", 1)
        t.chat.close()
        flag("pullBellowsLever.var", "varb2063_elemental_workshop_bellows_switch", 1)

        -- South room: fill the stone bowl from the lava trough (OPLOCU,
        -- rs2:185-196), then empty it into the furnace to light it (OPLOCU,
        -- proc elem1_furnace, rs2:259-269).
        walk("walk-useBowlOnLava", 2717, 9872)
        local trough_target, trough_target_result, trough_target_name = t.player.by_symbol("loc", "elemental_workshop_trough_2")
        t.check("foundTrough", trough_target ~= nil,
            "by_symbol(loc, elemental_workshop_trough_2) -> " .. tostring(trough_target_result) .. " " .. tostring(trough_target_name))
        t.exec("useBowlOnLava", t.player.use_on, "elemental_workshop_lava_bowl", trough_target)
        local full_await_result, full_await_detail = t.inv.await("elemental_workshop_lava_bowl_full", 1, 10)
        local empty_after_fill_result, empty_after_fill = t.inv.count("elemental_workshop_lava_bowl")
        t.check("gotFullBowl", full_await_result == "ok" and empty_after_fill_result == "ok" and empty_after_fill == 0,
            "inv.await(elemental_workshop_lava_bowl_full,1) -> " .. tostring(full_await_result) .. " " .. tostring(full_await_detail)
                .. "; empty bowl left the pack: " .. tostring(empty_after_fill) .. " (want 0)")

        walk("walk-useLavaOnFurnace", 2724, 9875)
        local furnace_target, furnace_target_result, furnace_target_name = t.player.by_symbol("loc", "elemental_workshop_furnace")
        t.check("foundFurnace", furnace_target ~= nil,
            "by_symbol(loc, elemental_workshop_furnace) -> " .. tostring(furnace_target_result) .. " " .. tostring(furnace_target_name))
        t.exec("useLavaOnFurnace", t.player.use_on, "elemental_workshop_lava_bowl_full", furnace_target)
        local empty_await_result, empty_await_detail = t.inv.await("elemental_workshop_lava_bowl", 1, 10)
        local full_after_result, full_after = t.inv.count("elemental_workshop_lava_bowl_full")
        t.check("gotEmptyBowl", empty_await_result == "ok" and full_after_result == "ok" and full_after == 0,
            "inv.await(elemental_workshop_lava_bowl,1) -> " .. tostring(empty_await_result) .. " " .. tostring(empty_await_detail)
                .. "; full bowl left the pack: " .. tostring(full_after) .. " (want 0)")
        flag("useLavaOnFurnace.var", "varb2062_elemental_workshop_fire", 1)

        -- West room: mine an elemental rock (opnpc1, elem2_gather.rs2:18-35)
        -- -- it spawns and awakens the earth elemental -- then fight it down
        -- and collect its ore drop.
        walk("walk-mineRock", 2703, 9893)
        t.exec("mineRock", t.player.talk_to, "elem1_qip_earth_elemental_rock_version_rock", 1)
        t.exec("mineRock.dismiss", t.chat.drain, {})

        -- npc_add runs server-side in the same tick as the mine click, but
        -- the client's entity pool sync can lag a beat behind it.
        t.settle()
        t.exec("elementalPresent", t.npc.await_present, "elem1_qip_earth_elemental_rock_version", 10, 10)

        -- The fight: lobsters eaten under 35/60 inside the press and the
        -- kill wait; the margin row needs the lowest reading >= 15 (a
        -- quarter of 60) AND a lobster left.
        local EAT = { eat = { item = "lobster", below = 35 } }
        local lowest = nil
        local function note_low(detail)
            local low = tonumber(string.match(tostring(detail), "lowest hp (%d+)/") or "")
            if low ~= nil and (lowest == nil or low < lowest) then
                lowest = low
            end
        end
        local attack_result, attack_detail = t.player.attack("elem1_qip_earth_elemental_rock_version", 2, 20, EAT)
        note_low(attack_detail)
        t.note("attackElemental: " .. tostring(attack_result) .. " " .. tostring(attack_detail))
        local _, kill_detail = t.exec("killElemental", t.npc.await_dead, "elem1_qip_earth_elemental_rock_version", 90, 10, 6, EAT)
        note_low(kill_detail)
        local food_result, food_left = t.inv.count("lobster")
        t.check("killElemental.margin", lowest ~= nil and lowest >= 15 and food_result == "ok" and food_left >= 1,
            "earth elemental (level 35): lowest hp " .. tostring(lowest) .. "/60, lobsters left "
                .. tostring(food_left) .. " of 4 staged (" .. tostring(food_result)
                .. ") (margin: lowest hp >= 15, a quarter of 60, AND food left)")

        -- The ore drop (ai_queue3, elemental_drops.rs2:105-110) is a queued
        -- private ground item -- give the server a beat to place it.
        t.settle()
        t.ticks(3)
        local ore_near_result, ore_near = t.world.obj_near("elemental_workshop_ore", 10)
        t.check("foundOre", ore_near_result == "ok",
            "obj_near(elemental_workshop_ore,10) -> " .. tostring(ore_near_result) .. " " .. tostring(ore_near))
        local ore_take_result, ore_take_detail = t.player.click_obj("elemental_workshop_ore", 3)
        t.note("takeOre: click_obj(elemental_workshop_ore,3) -> " .. tostring(ore_take_result) .. " " .. tostring(ore_take_detail))
        local ore_await_result, ore_await_detail = t.inv.await("elemental_workshop_ore", 1, 10)
        local ore_count_result, ore_count = t.inv.count("elemental_workshop_ore")
        t.check("gotOre", ore_await_result == "ok",
            "inv.await(elemental_workshop_ore,1) -> " .. tostring(ore_await_result) .. " " .. tostring(ore_await_detail)
                .. "; count=" .. tostring(ore_count_result == "ok" and ore_count or ore_count_result))

        -- South room again: smelt the ore into a bar (proc elem1_furnace,
        -- rs2:271-297 -- needs the bellows pumping and the furnace lit, both
        -- already true): ore and four coal leave the pack, the bar arrives,
        -- and smithing gains its literal 8xp (stat_advance(smithing, 80)).
        walk("walk-forgeBar", 2724, 9875)
        local forge_snapshot_result, forge_before = t.skill.snapshot()
        local forge_furnace_target = t.player.by_symbol("loc", "elemental_workshop_furnace")
        t.exec("forgeBar", t.player.use_on, "elemental_workshop_ore", forge_furnace_target)
        local bar_await_result, bar_await_detail = t.inv.await("elemental_workshop_bar", 1, 10)
        local coal_result, coal_count = t.inv.count("coal")
        local ore_left_result, ore_left = t.inv.count("elemental_workshop_ore")
        t.check("gotBar", bar_await_result == "ok" and coal_result == "ok" and coal_count == 0
                and ore_left_result == "ok" and ore_left == 0,
            "inv.await(elemental_workshop_bar,1) -> " .. tostring(bar_await_result) .. " " .. tostring(bar_await_detail)
                .. "; coal " .. tostring(coal_count) .. " (want 0 of 4), ore " .. tostring(ore_left) .. " (want 0)")
        if forge_snapshot_result == "ok" then
            t.check("forgeBar.smithing", t.skill.expect_gain("smithing", 8, forge_before))
        else
            t.check("forgeBar.smithing", false, "skill.snapshot before the smelt -> " .. tostring(forge_snapshot_result))
        end

        -- Central room: use the bar on the workbench (OPLOCU, elem2_helm.rs2
        -- :62-69 dispatches to proc elem1_make_shield, rs2:299-323) from the
        -- open tile west of it, to smith the shield and complete the quest.
        walk("walk-smithShield", 2716, 9888)
        local workbench_target = t.player.by_symbol("loc", "elemental_workshop_workbench")

        -- Reward snapshot before the hand-in.
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        local reward_shield_before_result, reward_shield_before = t.inv.count("elemental_shield")

        t.exec("smithShield", t.player.use_on, "elemental_workshop_bar", workbench_target)
        local bar_after_result, bar_after = t.inv.count("elemental_workshop_bar")
        t.check("smithShield.barUsed", bar_after_result == "ok" and bar_after == 0,
            "elemental_workshop_bar after the use -> " .. tostring(bar_after) .. " (want 0; " .. tostring(bar_after_result) .. ")")

        t.quest.expect_complete()

        -- Reward checks against the LITERAL numbers quest_elemental_workshop
        -- .rs2:316-322 grants, not a number read back from the scroll.
        -- Crafting has one contributor (stat_advance(crafting, 50000),
        -- rs2:320): 5000xp. Smithing: the SAME click that grants the 5000xp
        -- completion bonus (stat_advance(smithing, 50000), rs2:321) also
        -- grants the shield-crafting action's own 20xp (stat_advance
        -- (smithing, 200), rs2:316) in the same tick, so the observable
        -- delta is exactly 5020xp. Quest points +1 is quest.points above.
        t.check("reward.crafting", t.skill.expect_gain("crafting", 5000, reward_before))
        t.check("reward.smithing", t.skill.expect_gain("smithing", 5020, reward_before))

        local reward_shield_after_result, reward_shield_after = t.inv.count("elemental_shield")
        t.check("reward.elemental_shield",
            reward_shield_before_result == "ok" and reward_shield_after_result == "ok"
                and reward_shield_after == reward_shield_before + 1,
            string.format("elemental_shield %s -> %s (want +1), reads %s/%s",
                tostring(reward_shield_before), tostring(reward_shield_after),
                tostring(reward_shield_before_result), tostring(reward_shield_after_result)))

        t.finish(0)
    end,
}
