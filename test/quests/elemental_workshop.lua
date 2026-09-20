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
--   * elemental_workshop_box_1's own [oploc1,...] handler (rs2:118-127)
--     grants the STONE BOWL, and elemental_workshop_box_4's (rs2:142-152)
--     grants LEATHER -- backwards from the scaffold's step names. box_2/
--     box_4 are needle/leather FALLBACKS only (their own conditions require
--     the player to be carrying none already), moot here since setup
--     carries both already.
--   * elemental_workshop_workbench, elemental_workshop_furnace and
--     elemental_workshop_trough_2 are all OPLOCU (use-item-on-loc) targets
--     (elem2_helm.rs2:62, quest_elemental_workshop.rs2:161-176,232-243) --
--     t.player.use_on, never click_loc. The rejected attempt's own banner
--     had already caught the trough and furnace; the workbench (smithShield)
--     was still wrong there too.
--   * [opheldu,elemental_workshop_shield_book] (elemental_workshop_shield_
--     book.rs2:19-34) additionally requires %elemental_workshop_book = 1,
--     set only by READING the book first ([opheld1,...], rs2:10-17, two
--     mesbox pages) -- the scaffold skipped that step outright.
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
        "::setlevel mining 20", -- elem2_gather.rs2:19's own floor
        "::setlevel smithing 20", -- quest_elemental_workshop.rs2:88,256,277's own floor
        "::setlevel crafting 20", -- quest_elemental_workshop.rs2:88's own floor
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel defence 60",
        "::setlevel hitpoints 60",
    },

    run = function(t)
        -- The quest's own varp is a 21-bit flag accumulator
        -- (configs/all.varbit's elemental_workshop_book/key/gate1/gate2/
        -- switch/bellows/fire/bellows_switch/boxes/stairs/leather/finished
        -- subfields on basevar elemental_workshop_bits), so there is no
        -- single linear "stage" integer to bind -- bind the completion
        -- varbit itself instead (S2's Prying Times precedent: a pure-varbit
        -- quest binds and reads the same as a varp-tracked one).
        local bind_result, bind_detail = t.quest.bind({
            varp = "elemental_workshop_finished",
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

        -- Seers' Village: search the marked bookcase for the battered book.
        -- S8: a bare inv read right after a granting chat line can still see
        -- the OLD count -- poll with inv.await, not expect_has, and write
        -- the counts back by hand (inv.await's own detail is not itself
        -- named after the item, trap 12's habit).
        t.exec("goto-searchBookcase", t.player.goto_tile, 2716, 3482, 0)
        t.settle()
        t.exec("searchBookcase", t.player.click_loc, "elemental_workshop_bookcase", 1)
        local book_await_result, book_await_detail = t.inv.await("elemental_workshop_shield_book", 1, 10)
        local book_count_result, book_count = t.inv.count("elemental_workshop_shield_book")
        t.check("gotBook", book_await_result == "ok",
            "inv.await(elemental_workshop_shield_book,1) -> " .. tostring(book_await_result) .. " " .. tostring(book_await_detail)
                .. "; count=" .. tostring(book_count_result == "ok" and book_count or book_count_result))

        -- Read it (opheld1, rs2:10-17) -- two mesbox pages, and the read is
        -- what sets %elemental_workshop_book = 1 that the spine cut needs.
        -- inv_op's OWN op-1 dispatch ALSO fires this backpack cell's on_op
        -- flash unconditionally (app_minimenu.c:1241's app_inv_cell_op_flash,
        -- called for every OPHELD1..5 press) and that hook is the shift-
        -- click-drop chain for op_index 1 specifically (app_minimenu.c:1176-
        -- 1182's own comment, confirmed independently by
        -- test/quests/_conformance.lua's player.inv_op probe) -- so the read
        -- itself lands (the mesbox pages below are its proof) but the same
        -- click also drops the book at the player's feet. Pick it back up.
        t.exec("readBook", t.player.inv_op, "elemental_workshop_shield_book", 1)
        t.exec("readBook.dismiss", t.chat.drain, {})
        -- click_obj answers `ok` with a nil detail (QUEST_AUTHORING.md
        -- section 8's fourth hollow verb) -- called directly, graded by hand.
        local pickup_result, pickup_detail = t.player.click_obj("elemental_workshop_shield_book", 3)
        t.check("pickBookBackUp", pickup_result == "ok", "click_obj(elemental_workshop_shield_book,3) -> "
            .. tostring(pickup_result) .. " " .. tostring(pickup_detail))
        local book2_await_result, book2_await_detail = t.inv.await("elemental_workshop_shield_book", 1, 10)
        local book2_count_result, book2_count = t.inv.count("elemental_workshop_shield_book")
        t.check("gotBookBack", book2_await_result == "ok",
            "inv.await(elemental_workshop_shield_book,1) after pickup -> " .. tostring(book2_await_result) .. " " .. tostring(book2_await_detail)
                .. "; count=" .. tostring(book2_count_result == "ok" and book2_count or book2_count_result))

        -- Cut the spine with the knife (opheldu, rs2:19-34) -- item-on-item,
        -- the seam the rejected attempt reported as undrivable.
        t.exec("cutSpine", t.player.use_item_on_item, "knife", "elemental_workshop_shield_book")
        local key_await_result, key_await_detail = t.inv.await("elemental_workshop_key", 1, 10)
        local key_count_result, key_count = t.inv.count("elemental_workshop_key")
        t.check("gotKey", key_await_result == "ok",
            "inv.await(elemental_workshop_key,1) -> " .. tostring(key_await_result) .. " " .. tostring(key_await_detail)
                .. "; count=" .. tostring(key_count_result == "ok" and key_count or key_count_result))

        -- Open the odd wall (oploc1, rs2:178-190) -- a scripted p_teleport
        -- through the wall, not a loc_change, so it mounts no dialogue and no
        -- route: the verb's own settle has no arm for that (its own banner,
        -- pointer.lua:1593-1618) and deliberately keeps `timeout` once the
        -- player's tile has moved rather than guessing. Called directly and
        -- graded on the tile, not the raw result word.
        t.exec("goto-openOddWall", t.player.goto_tile, 2709, 3495, 0)
        t.settle()
        local wall_before_result, wall_before_tile = t.world.tile()
        local wall_result, wall_detail = t.player.click_loc("elemental_workshop_oddwall_l", 1)
        local wall_after_result, wall_after_tile = t.world.tile()
        local wall_moved = wall_before_result == "ok" and wall_after_result == "ok"
            and (wall_after_tile.x ~= wall_before_tile.x
                or wall_after_tile.z ~= wall_before_tile.z
                or wall_after_tile.level ~= wall_before_tile.level)
        t.check("openOddWall", wall_result == "ok" or wall_moved,
            string.format("click_loc(elemental_workshop_oddwall_l,1) -> %s %s; tile %s -> %s",
                tostring(wall_result), tostring(wall_detail),
                wall_before_result == "ok" and string.format("%d,%d,%d", wall_before_tile.x, wall_before_tile.z, wall_before_tile.level) or tostring(wall_before_result),
                wall_after_result == "ok" and string.format("%d,%d,%d", wall_after_tile.x, wall_after_tile.z, wall_after_tile.level) or tostring(wall_after_result)))

        -- North room: turn the water controls. East (valve_1, gated on
        -- %elemental_workshop_gate2) must open FIRST -- rs2:48-63's own east
        -- branch refuses while gate1 (west) is already open, resetting both
        -- if pressed out of order.
        t.exec("goto-turnEastControl", t.player.goto_tile, 2726, 9908, 0)
        t.settle()
        t.exec("turnEastControl", t.player.click_loc, "elemental_workshop_valve_1", 1)
        t.chat.close()

        t.exec("goto-turnWestControl", t.player.goto_tile, 2713, 9908, 0)
        t.settle()
        t.exec("turnWestControl", t.player.click_loc, "elemental_workshop_valve_2", 1)
        t.chat.close()

        -- Pull the lever -- both gates open, so this starts the wheel
        -- (rs2:65-81).
        t.exec("pullLever", t.player.click_loc, "elemental_workshop_water_lever", 1)
        t.chat.close()

        -- North-east boxes: box_1 grants the stone bowl (rs2:118-127). Its
        -- own click_loc from the lever's tile settled on a bare `map_flag`
        -- (a route finishing, no chat line at all) on the rejected run --
        -- no *.loc placement file exists to read its tile from (trap 20), so
        -- find it with world.loc_near and goto_tile there directly rather
        -- than trust click_loc's own short-range walk_near to close what may
        -- be a longer gap.
        local box1_near_result, box1_near = t.world.loc_near("elemental_workshop_box_1", 20)
        t.check("foundBox1", box1_near_result == "ok",
            "loc_near(elemental_workshop_box_1,20) -> " .. tostring(box1_near_result) .. " " .. tostring(box1_near))
        if box1_near_result == "ok" then
            t.exec("goto-getStoneBowl", t.player.goto_tile, box1_near.tile_x, box1_near.tile_z, box1_near.level)
        end
        t.exec("getStoneBowl", t.player.click_loc, "elemental_workshop_box_1", 1)
        local bowl_await_result, bowl_await_detail = t.inv.await("elemental_workshop_lava_bowl", 1, 10)
        local bowl_count_result, bowl_count = t.inv.count("elemental_workshop_lava_bowl")
        t.check("gotBowl", bowl_await_result == "ok",
            "inv.await(elemental_workshop_lava_bowl,1) -> " .. tostring(bowl_await_result) .. " " .. tostring(bowl_await_detail)
                .. "; count=" .. tostring(bowl_count_result == "ok" and bowl_count or bowl_count_result))

        -- East room: repair the bellows (rs2:83-99) with the needle/thread/
        -- leather from setup, then pull the air lever to start pumping
        -- (rs2:101-114) -- needs the wheel already running. The air lever
        -- answered `refused -- I can't reach that!` from the tile fixBellows
        -- left the player on, on the rejected run -- same loc_near ->
        -- goto_tile fix as box_1, rather than trust click_loc's own walk.
        t.exec("goto-fixBellows", t.player.goto_tile, 2735, 9884, 0)
        t.settle()
        t.exec("fixBellows", t.player.click_loc, "elemental_workshop_bellows_multiloc", 1)
        t.chat.close()
        local air_near_result, air_near = t.world.loc_near("elemental_workshop_air_lever", 20)
        t.check("foundAirLever", air_near_result == "ok",
            "loc_near(elemental_workshop_air_lever,20) -> " .. tostring(air_near_result) .. " " .. tostring(air_near))
        if air_near_result == "ok" then
            t.exec("goto-pullBellowsLever", t.player.goto_tile, air_near.tile_x, air_near.tile_z, air_near.level)
        end
        t.exec("pullBellowsLever", t.player.click_loc, "elemental_workshop_air_lever", 1)
        t.chat.close()

        -- South room: fill the stone bowl from the lava trough (OPLOCU,
        -- rs2:161-176), then empty it into the furnace to light it (OPLOCU,
        -- proc elem1_furnace, rs2:232-247).
        t.exec("goto-useBowlOnLava", t.player.goto_tile, 2717, 9871, 0)
        t.settle()
        local trough_target, trough_target_result, trough_target_name = t.player.by_symbol("loc", "elemental_workshop_trough_2")
        t.check("foundTrough", trough_target ~= nil,
            "by_symbol(loc, elemental_workshop_trough_2) -> " .. tostring(trough_target_result) .. " " .. tostring(trough_target_name))
        t.exec("useBowlOnLava", t.player.use_on, "elemental_workshop_lava_bowl", trough_target)
        local full_await_result, full_await_detail = t.inv.await("elemental_workshop_lava_bowl_full", 1, 10)
        local full_count_result, full_count = t.inv.count("elemental_workshop_lava_bowl_full")
        t.check("gotFullBowl", full_await_result == "ok",
            "inv.await(elemental_workshop_lava_bowl_full,1) -> " .. tostring(full_await_result) .. " " .. tostring(full_await_detail)
                .. "; count=" .. tostring(full_count_result == "ok" and full_count or full_count_result))

        local furnace_target, furnace_target_result, furnace_target_name = t.player.by_symbol("loc", "elemental_workshop_furnace")
        t.check("foundFurnace", furnace_target ~= nil,
            "by_symbol(loc, elemental_workshop_furnace) -> " .. tostring(furnace_target_result) .. " " .. tostring(furnace_target_name))
        t.exec("useLavaOnFurnace", t.player.use_on, "elemental_workshop_lava_bowl_full", furnace_target)
        local empty_await_result, empty_await_detail = t.inv.await("elemental_workshop_lava_bowl", 1, 10)
        local empty_count_result, empty_count = t.inv.count("elemental_workshop_lava_bowl")
        t.check("gotEmptyBowl", empty_await_result == "ok",
            "inv.await(elemental_workshop_lava_bowl,1) -> " .. tostring(empty_await_result) .. " " .. tostring(empty_await_detail)
                .. "; count=" .. tostring(empty_count_result == "ok" and empty_count or empty_count_result))

        -- West room: mine an elemental rock (opnpc1, elem2_gather.rs2:18-35)
        -- -- it spawns and awakens the earth elemental (npc_add, retaliate
        -- off) -- then fight it down and collect its ore drop.
        t.exec("goto-mineRock", t.player.goto_tile, 2703, 9894, 0)
        t.settle()
        t.exec("mineRock", t.player.talk_to, "elem1_qip_earth_elemental_rock_version_rock", 1)
        t.exec("mineRock.dismiss", t.chat.drain, {})

        -- npc_add (elem2_gather.rs2:30) runs server-side in the same tick as
        -- the mine click, but the client's own entity pool sync can still
        -- lag a beat behind it (the same class of trap as the ore drop
        -- below) -- confirm the awakened elemental is actually in the
        -- client's npc pool before attack()'s own radius-0 nearest lookup
        -- needs it there.
        t.settle()
        local elemental_present_result = t.npc.await_present("elem1_qip_earth_elemental_rock_version", 10, 10)
        t.check("elementalPresent", elemental_present_result == "ok",
            "npc.await_present(elem1_qip_earth_elemental_rock_version,10,10) -> " .. tostring(elemental_present_result))

        local attack_result, attack_detail = t.player.attack("elem1_qip_earth_elemental_rock_version", 2, 20)
        t.check("attackElemental", attack_result == "ok" or attack_result == "timeout", attack_detail)
        t.exec("killElemental", t.npc.await_dead, "elem1_qip_earth_elemental_rock_version", 60)

        -- The ore drop (ai_queue3, elemental_drops.rs2:105-110) is a queued
        -- private ground item, not an immediate one -- give the server a
        -- beat to place it and confirm it is actually in the world pool
        -- before clicking, rather than let click_obj's own screen_position
        -- read guess why nothing is there.
        t.settle()
        t.ticks(3)
        local ore_near_result, ore_near = t.world.obj_near("elemental_workshop_ore", 10)
        t.check("foundOre", ore_near_result == "ok",
            "obj_near(elemental_workshop_ore,10) -> " .. tostring(ore_near_result) .. " " .. tostring(ore_near))
        -- click_obj answers `ok` with a nil detail (QUEST_AUTHORING.md
        -- section 8's fourth hollow verb) -- called directly, graded by hand.
        local ore_take_result, ore_take_detail = t.player.click_obj("elemental_workshop_ore", 3)
        t.check("takeOre", ore_take_result == "ok", "click_obj(elemental_workshop_ore,3) -> "
            .. tostring(ore_take_result) .. " " .. tostring(ore_take_detail))
        local ore_await_result, ore_await_detail = t.inv.await("elemental_workshop_ore", 1, 10)
        local ore_count_result, ore_count = t.inv.count("elemental_workshop_ore")
        t.check("gotOre", ore_await_result == "ok",
            "inv.await(elemental_workshop_ore,1) -> " .. tostring(ore_await_result) .. " " .. tostring(ore_await_detail)
                .. "; count=" .. tostring(ore_count_result == "ok" and ore_count or ore_count_result))

        -- South room again: smelt the ore into a bar (proc elem1_furnace,
        -- rs2:244-270 -- needs the bellows pumping and the furnace lit, both
        -- already true).
        t.exec("goto-forgeBar", t.player.goto_tile, 2726, 9875, 0)
        t.settle()
        local forge_furnace_target = t.player.by_symbol("loc", "elemental_workshop_furnace")
        t.exec("forgeBar", t.player.use_on, "elemental_workshop_ore", forge_furnace_target)
        local bar_await_result, bar_await_detail = t.inv.await("elemental_workshop_bar", 1, 10)
        local bar_count_result, bar_count = t.inv.count("elemental_workshop_bar")
        t.check("gotBar", bar_await_result == "ok",
            "inv.await(elemental_workshop_bar,1) -> " .. tostring(bar_await_result) .. " " .. tostring(bar_await_detail)
                .. "; count=" .. tostring(bar_count_result == "ok" and bar_count or bar_count_result))

        -- Central room: use the bar on the workbench (OPLOCU, elem2_helm.rs2
        -- :62-69 dispatches to proc elem1_make_shield since the crane-claw
        -- gate is never satisfied here) to smith and complete the quest.
        t.exec("goto-smithShield", t.player.goto_tile, 2717, 9888, 0)
        t.settle()
        local workbench_target = t.player.by_symbol("loc", "elemental_workshop_workbench")

        -- Reward snapshot before the hand-in.
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        local reward_shield_before_result, reward_shield_before = t.inv.count("elemental_shield")

        t.exec("smithShield", t.player.use_on, "elemental_workshop_bar", workbench_target)

        t.quest.expect_complete()

        -- Reward checks against the LITERAL numbers quest_elemental_workshop
        -- .rs2:288-295 grants, not a number read back from the scroll.
        -- Crafting has one contributor (stat_advance(crafting, 50000),
        -- rs2:293) so the plain 5000xp/50000-tenths check applies cleanly.
        -- Smithing does not: the SAME click that grants the 5000xp
        -- completion bonus (stat_advance(smithing, 50000), rs2:294) also
        -- grants the shield-crafting action's own 20xp (stat_advance
        -- (smithing, 200), rs2:289) in the same tick, so the observable
        -- delta is 5020xp, not 5000 -- checked against that precomputed sum
        -- rather than either half alone.
        t.check("reward.crafting", t.skill.expect_gain("crafting", 5000, reward_before))
        local smithing_before = reward_before.smithing and reward_before.smithing.experience
        local smithing_after_result, smithing_after = t.skill.read("smithing")
        local smithing_delta = (smithing_after_result == "ok" and type(smithing_before) == "number")
            and (smithing_after.experience - smithing_before) or nil
        t.check("reward.smithing", smithing_delta == 5020 or smithing_delta == 50200,
            string.format("smithing before=%s after=%s delta=%s (expected 5020xp or 50200 tenths: "
                .. "200 [rs2:289, the shield-craft itself] + 50000 [rs2:294, the quest reward] raw)",
                tostring(smithing_before),
                tostring(smithing_after_result == "ok" and smithing_after.experience or smithing_after_result),
                tostring(smithing_delta)))

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
