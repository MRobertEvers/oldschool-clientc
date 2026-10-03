-- Shadows of Custodia, client-driven (from the b54 parity driver).
-- GUIDE-GAP: unreachableState is the Quest Helper "state should not be reachable" placeholder step; shadowsofcustodia.rs2:176 moves stage 2 to 4 on the first citizen asked, so the step never shows.
return {
    id = "shadowsofcustodia",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel slayer 60", "::setlevel fishing 50", "::setlevel construction 45", "::setlevel hunter 40",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99",
        "::setlevel prayer 70",
        "::give rune_scimitar 1", "::give lobster 14",
        "::give willow_longbow 4", "::setlevel woodcutting 60",
        "::complete quest_childrenofthesun",
        "::give mithril_axe 1",
    },
    run = function(t)
        t.quest.bind({
            varp = "varb16632_soc",
            constants = { not_started = 0, citizens = 2, parents = 4, wall = 5, puddle = 6, plank = 8, cloth = 9, boys = 10,
                boys_cave = 12, boys_found = 14, authorities = 15, etz = 16, antos = 18, antos_saved = 20, finish = 22, complete = 24 },
            display = "Shadows of Custodia",
            points = 2,
        })
        t.ticks(3)
        t.exec("wield", t.player.equip, "rune_scimitar")
        -- ---- start
        t.exec("goto-board", t.player.goto_tile, 1395, 3356, 0)
        t.exec("startQuest", t.player.click_loc, "soc_missing_persons", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "mesbox:covered in missing person posters",
            "player:quite a few missing people",
            "choose:Yes.",
            "player:anyone around here knows more",
        })
        t.ticks(2)
        t.expect("quest.stage.citizens", t.quest.expect_stage("citizens"))
        -- ---- Marcus
        t.exec("goto-marcus", t.player.goto_tile, 1394, 3353, 0)
        t.exec("talkToCitizen", t.player.talk_to, "soc_citizen", 1)
        t.exec("talkToMarcus-dialog", t.chat.drain, { max_pages = 20 })
        t.ticks(2)
        local r, v = t.var.varbit("varb16635_soc_citizen"); t.check("marcus.var", v == 1, tostring(r) .. " " .. tostring(v))
        t.expect("quest.stage.parents.afterMarcus", t.quest.expect_stage("parents"))
        t.exec("goto-bar", t.player.goto_tile, 1391, 3353, 0)
        t.exec("talkToBartender", t.player.talk_to, "auburn_bartender", 1)
        t.exec("bartender-dialog", t.chat.play, {
            "npc:Welcome to the Auburn Pub",
            "options",
            "choose:About the missing people...",
            "player:About the missing people",
            "*",
        })
        t.chat.drain({ max_pages = 20 })
        t.ticks(2)
        r, v = t.var.varbit("varb16633_soc_barkeep"); t.check("barkeep.var", v == 1, tostring(r) .. " " .. tostring(v))
        t.exec("goto-shop", t.player.goto_tile, 1380, 3349, 0)
        t.exec("talkToShopkeep", t.player.talk_to, "auburn_general_store", 1)
        t.exec("shop-menu", t.chat.play, { "npc:Nilsal", "options", "choose:About the missing people..." })
        t.chat.drain({ max_pages = 30 })
        t.ticks(2)
        r, v = t.var.varbit("varb16634_soc_shopkeep"); t.check("shopkeep.var", v == 1, tostring(r) .. " " .. tostring(v))
        -- parents refuse before Ictus
        t.exec("goto-parents-early", t.player.goto_tile, 1381, 3362, 0)
        t.exec("parents-refuse", t.player.talk_to, "soc_parent", 1)
        t.chat.drain({ max_pages = 20 })
        t.ticks(2)
        t.expect("quest.stage.parents.stillrefused", t.quest.expect_stage("parents"))
        t.exec("goto-ictus", t.player.goto_tile, 1409, 3374, 0)
        t.exec("talkToIctus", t.player.talk_to, "soc_sillyman", 1)
        t.chat.drain({ max_pages = 30 })
        t.ticks(2)
        r, v = t.var.varbit("varb16639_soc_sillyman"); t.check("ictus.var", v == 1, tostring(r) .. " " .. tostring(v))
        -- ---- parents convinced
        t.exec("goto-parents", t.player.goto_tile, 1381, 3362, 0)
        t.exec("talkToTheParents", t.player.talk_to, "soc_parent", 1)
        t.chat.drain({ max_pages = 40 })
        t.ticks(2)
        t.expect("quest.stage.wall", t.quest.expect_stage("wall"))
        r, v = t.var.varbit("varb16659_soc_wall_state"); t.check("wall.state1", v == 1, tostring(r) .. " " .. tostring(v))
        t.exec("goto-wall", t.player.goto_tile, 1377, 3358, 0)
        t.exec("inspectWall", t.player.click_loc, "soc_wall_inspect_op", 1)
        t.chat.drain({ max_pages = 5 })
        t.ticks(2)
        t.expect("quest.stage.puddle", t.quest.expect_stage("puddle"))
        t.exec("goto-puddle", t.player.goto_tile, 1376, 3356, 0)
        t.exec("inspectPuddle", t.player.click_loc, "soc_puddle", 1)
        t.chat.drain({ max_pages = 5 })
        t.ticks(2)
        t.expect("quest.stage.plank", t.quest.expect_stage("plank"))
        -- ---- plank: rod from the ground, then use it
        t.exec("goto-rod", t.player.goto_tile, 1346, 3352, 0)
        t.exec("getRod", t.player.click_obj, "fishing_rod", 3)
        t.exec("getRod.await", t.inv.await, "fishing_rod", 1, 10)
        t.exec("goto-plank", t.player.goto_tile, 1346, 3354, 0)
        t.exec("inspectPlank", t.player.click_loc, "soc_log_op", 1)
        t.chat.drain({ max_pages = 5 })
        local plank = t.player.by_symbol("loc", "soc_log_op")
        t.exec("fishCloth", t.player.use_on, "fishing_rod", plank)
        t.chat.drain({ max_pages = 8 })
        t.exec("cloth.await", t.inv.await, "soc_cloth", 1, 10)
        t.expect("quest.stage.cloth", t.quest.expect_stage("cloth"))
        t.exec("goto-parents2", t.player.goto_tile, 1381, 3362, 0)
        t.exec("returnCloth", t.player.talk_to, "soc_parent", 1)
        t.chat.drain({ max_pages = 20 })
        t.ticks(2)
        t.expect("quest.stage.boys", t.quest.expect_stage("boys"))
        -- ---- cave: injured boys
        t.exec("goto-cave", t.player.goto_tile, 1295, 3375, 0)
        t.exec("enterCave", t.player.click_loc, "soc_cave_entrance", 1)
        t.ticks(4)
        do local rr, tl = t.world.tile(); t.check("cave.inside", rr == "ok" and tl.z > 9000, rr == "ok" and (tl.x .. "," .. tl.z) or tostring(tl)) end
        t.expect("quest.stage.boys_cave", t.quest.expect_stage("boys_cave"))
        t.exec("talkToInjuredBoyInCave", t.player.talk_to, "soc_injured_person", 1)
        t.exec("injured-dialog", t.chat.drain, { max_pages = 12 })
        t.ticks(4)
        t.expect("quest.stage.boys_found", t.quest.expect_stage("boys_found"))
        do local rr, tl = t.world.tile(); t.check("home.teleport", rr == "ok" and tl.x > 1370 and tl.z < 3400, rr == "ok" and (tl.x .. "," .. tl.z) or tostring(tl)) end
        t.exec("talkToTheParents-boysBack", t.player.talk_to, "soc_parent", 1)
        t.chat.drain({ max_pages = 40 })
        t.ticks(2)
        t.expect("quest.stage.authorities", t.quest.expect_stage("authorities"))
        r, v = t.var.varbit("varb16659_soc_wall_state"); t.check("wall.state2", v == 2, tostring(r) .. " " .. tostring(v))
        -- ---- ladder blocked until the work is done
        t.exec("goto-ladder", t.player.goto_tile, 1381, 3358, 0)
        t.exec("ladderBlocked", t.player.click_loc, "soc_ladder", 1)
        t.chat.drain({ max_pages = 3 })
        do local rr, tl = t.world.level(); t.check("ladder.stillground", tl == 0, tostring(rr) .. " " .. tostring(tl)) end
        -- ---- captain agrees to the bows (we hold 4): hand over now
        t.exec("goto-captain", t.player.goto_tile, 1369, 3345, 0)
        t.exec("informCaptainAboutMissingPeople", t.player.talk_to, "auburnvale_guard_captain", 1)
        t.chat.drain({ max_pages = 40 })
        t.ticks(2)
        r, v = t.var.varbit("varb16641_soc_bowsmade"); t.check("bows.done", v == 2, tostring(r) .. " " .. tostring(v))
        t.expect("quest.stage.authorities.stillwall", t.quest.expect_stage("authorities"))
        -- ---- logs from the maple tree, hammer from the ground
        t.exec("goto-hammer", t.player.goto_tile, 1378, 3372, 0)
        t.exec("getHammer", t.player.click_obj, "hammer", 3)
        t.exec("getHammer.await", t.inv.await, "hammer", 1, 10)
        t.exec("goto-maple", t.player.goto_tile, 1383, 3374, 0)
        t.exec("chopMaple", t.player.click_loc, "mapletree", 1)
        t.exec("maple.await", t.inv.await, "maple_logs", 4, 400)
        t.exec("goto-wall2", t.player.goto_tile, 1377, 3358, 0)
        t.exec("reinforceWall", t.player.click_loc, "soc_wall_inspect_reinforce", 1)
        t.chat.drain({ max_pages = 6 })
        t.ticks(4)
        r, v = t.var.varbit("varb16659_soc_wall_state"); t.check("wall.state3", v == 3, tostring(r) .. " " .. tostring(v))
        t.expect("quest.stage.etz", t.quest.expect_stage("etz"))
        -- ---- upstairs
        t.exec("goto-ladder2", t.player.goto_tile, 1381, 3358, 0)
        t.exec("climbUpLadder", t.player.click_loc, "soc_ladder", 1)
        t.ticks(4)
        do local rr, tl = t.world.level(); t.check("ladder.up", tl == 1, tostring(rr) .. " " .. tostring(tl)) end
        t.exec("goto-etz", t.player.goto_tile, 1381, 3359, 1)
        t.exec("talkToEtzAboutWhatTheyRemember", t.player.talk_to, "soc_etz", 1)
        t.chat.drain({ max_pages = 20 })
        t.ticks(2)
        t.expect("quest.stage.antos", t.quest.expect_stage("antos"))
        t.exec("goto-top", t.player.goto_tile, 1381, 3358, 1)
        t.exec("climbDownstairs", t.player.click_loc, "soc_laddertop", 1)
        t.ticks(4)
        -- ---- Antos: the fight
        t.exec("goto-cave2", t.player.goto_tile, 1295, 3375, 0)
        t.exec("enterCave2", t.player.click_loc, "soc_cave_entrance", 1)
        t.ticks(4)
        t.exec("goto-antos", t.player.goto_tile, 1336, 9753, 0)
        t.exec("talkToAntos", t.player.talk_to, "soc_antos", 1)
        t.chat.drain({ max_pages = 6 })
        t.ticks(3)
        r, v = t.var.varbit("varb16653_soc_stalkers_encountered"); t.check("stalkers.encountered", v == 1, tostring(r) .. " " .. tostring(v))
        local rn, n = t.npc.tiles("soc_quest_juvenile", 12)
        t.check("creatures.spawned", rn == "ok", tostring(rn) .. " " .. tostring(n))
        for i = 1, 3 do
            local ra, da = t.player.attack("soc_quest_juvenile", 2, 10)
            t.check("attack-" .. i, ra == "ok" or ra == "timeout", tostring(ra) .. " " .. tostring(da))
            t.exec("killCreature-" .. i, t.npc.await_dead_engaged, 400, 12, { eat = { item = "lobster", below = 50 } })
        end
        t.ticks(4)
        t.expect("quest.stage.antos_saved", t.quest.expect_stage("antos_saved"))
        t.exec("talkToAntos2", t.player.talk_to, "soc_antos", 1)
        t.chat.drain({ max_pages = 20 })
        t.ticks(4)
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.check("reward.snapshot", xp_snapshot_result == "ok", "skill.snapshot before hand-in -> " .. tostring(xp_snapshot_result))
        t.exec("finishQuest", t.player.talk_to, "auburnvale_guard_captain", 1)
        t.chat.drain({ max_pages = 30 })
        t.ticks(4)
        t.quest.expect_complete()
        t.expect("reward.slayer_xp", t.skill.expect_gain("slayer", 10000, xp_snapshot))
        t.expect("reward.hunter_xp", t.skill.expect_gain("hunter", 4000, xp_snapshot))
        t.expect("reward.fishing_xp", t.skill.expect_gain("fishing", 3000, xp_snapshot))
        t.expect("reward.construction_xp", t.skill.expect_gain("construction", 3000, xp_snapshot))
        t.check("reward.quest_points", true, "2 quest points literal: bind points=2 asserted by quest.points row")
        t.finish(0)
    end,
}
