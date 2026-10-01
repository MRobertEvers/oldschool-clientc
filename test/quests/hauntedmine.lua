return {
    id = "hauntedmine",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_priestinperil",
        "::setlevel crafting 35",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel ranged 99",
        "::give magic_shortbow 1",
        "::give rune_arrow 400",
        "::give shark 23",
    },
    run = function(t)
        t.quest.bind({
            varp = "varp382_hauntedmine",
            constants = { not_started = 0, started = 1, dayth_killed = 9, key_collected = 10, complete = 11 },
            row = "quest_hauntedmine",
            display = "Haunted Mine",
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("goto-zealot", t.player.goto_tile, 3443, 3258, 0)
        t.exec("talkToZealot", t.player.talk_to, "saradominist_zealot", 1)
        t.exec("talkToZealot-dialog", t.chat.play, {
            "npc:State thy allegiance",
            "choose:/Saradomin/",
            "player:I follow",
            "npc:Ah, a wise",
            "choose:/challenges/",
            "player:I come seeking",
            "npc:A noble cause",
            "choose:What quest is that then?",
            "player:What quest",
            "npc:I seek to reclaim",
            "npc:*",
            "npc:*",
            "choose:/other way/",
            "player:Is there any other",
            "npc:Indeed I have",
            "npc:*",
            "choose:/borrow/",
            "player:Can I borrow",
            "npc:*",
            "options",
        })
        t.key("escape")
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        local hk, hv = t.var.server("varb2397_hauntedmine_heardaboutkey")
        t.check("heardaboutkey", hk, tostring(hv))
        t.exec("pickpocketZealot", t.player.press, "saradominist_zealot", 3, 8)
        t.check("zealotkey", t.inv.await("hauntedmine_lift_key", 1, 6))
        t.exec("wield", t.player.equip, "magic_shortbow")
        t.exec("wield-arrows", t.player.equip, "rune_arrow")

        local function click(name, sym, x, z, op)
            t.exec(name, t.player.click_loc, sym, op or 1, { at = { x, z } })
            t.ticks(3)
        end
        local function at(name, x, z, lvl) t.exec(name, t.player.goto_tile, x, z, lvl or 0) end
        local function tile(name, ex, ez)
            local ok, w = t.world.tile()
            t.check(name, ok == "ok" and math.abs(w.x - ex) <= 6 and math.abs(w.z - ez) <= 6, w.x .. "," .. w.z .. "," .. w.level)
        end

        -- south tunnel: fungus -> cart -> levers
        at("goto-enterMine", 3429, 3226)
        click("enterMine", "hauntedmine_back_entrance2", 3429, 3225)
        tile("inLevel1South", 3420, 9620)
        click("goDownFromLevel1South", "hauntedmine_laddertop", 3422, 9625)
        click("goDownFromLevel2South", "hauntedmine_laddertop_1sw", 2798, 4567)
        click("goDownToFungusRoom", "hauntedmine_laddertop_1e", 2725, 4486)
        tile("inCartRoom", 2789, 4488)
        click("pickFungus", "glowing_mushroom2", 2793, 4493)
        t.check("fungus-held", t.inv.await("glowing_fungus", 1, 6))
        click("putFungusInCart", "hauntedmine_puzzle_cart", 2778, 4506)
        t.check("begincart", t.msg.expect("You place the glowing fungus"))
        -- wrong first: panel with levers unset must sink/return, not send
        click("pullLeverA", "hauntedmine_point_lever1", 2785, 4517)
        click("pullLeverB", "hauntedmine_point_lever2", 2784, 4517)
        click("pullLeverE", "hauntedmine_point_lever5", 2785, 4515)
        click("pullLeverF", "hauntedmine_point_lever6", 2768, 4533)
        click("readPanel", "hauntedmine_points_info", 2769, 4522)
        local _, m1 = t.msg.last(4)
        t.check("endcart", t.msg.expect("mine cart trundles"), tostring(m1))
        t.check("fungus-gone-from-pack", t.inv.await("glowing_fungus", 0, 4))
        -- back out the south tunnel
        at("goto-ladder1w", 2789, 4487)
        click("goUpFromFungusRoom", "hauntedmine_ladder_1w", 2789, 4486)
        tile("level3south", 2733, 4510)
        click("goUpFromLevel3South", "hauntedmine_ladder_1ne", 2734, 4503)
        click("goUpFromLevel2South", "hauntedmine_ladder", 2782, 4569)
        click("leaveLevel1South", "lotr_back_entrance1_inside", 3408, 9623)
        tile("outside", 3430, 3225)
        -- north tunnel
        at("goto-enterMineNorth", 3430, 3234)
        click("enterMineNorth", "hauntedmine_back_entrance1", 3430, 3233)
        click("goDownLevel1North", "hauntedmine_laddertop", 3413, 9633)
        click("goDownLevel2North", "hauntedmine_laddertop_1sw", 2797, 4599)
        click("goDownToCollectFungus", "hauntedmine_laddertop_1e", 2710, 4540)
        tile("collectRoom", 2774, 4538)
        click("collectFungus", "hauntedmine_puzzle_cart", 2774, 4537)
        t.exec("collectFungus-dialog", t.chat.play, { "player:Take it" })
        t.check("fungus-collected", t.inv.await("glowing_fungus", 1, 6))
        click("goUpFromCollectRoom", "hauntedmine_ladder_1w", 2774, 4540)
        tile("back-on-level3north", 2710, 4538)
        at("goto-liftladder", 2731, 4528)
        click("goDownFromLevel3NorthEast", "hauntedmine_laddertop_1e", 2732, 4529)
        tile("liftroom", 2797, 4529)
        at("goto-chisel", 2800, 4501)
        t.exec("pickUpChisel", t.player.click_obj, "chisel", 3)
        t.check("chisel", t.inv.await("chisel", 1, 6))
        at("goto-valve", 2808, 4497)
        t.drive.camera(0, 383, 300)
        click("useKeyOnValve", "hauntedmine_lift_valve", 2808, 4496)
        local _, m2 = t.msg.last(4)
        t.check("valve-open", t.msg.expect("run for the lift"), tostring(m2))
        t.exec("goDownLift", t.player.click_loc, "lift_side_r", 1)
        local function below() local ok, w = t.world.tile(); return ok == "ok" and w.x < 2760 end
        t.check("lift-descended", t.await({ level = below, note = "lift descent" }, 20))
        t.check("lift-line", t.msg.expect("You take the lift down"))
        tile("floodedRoom", 2726, 4455)
        -- Dayth
        at("goto-flooded-east1", 2745, 4440)
        t.drive.camera(0, 383, 300)
        click("goDownToDayth", "hauntedmine_dark_stairs_top", 2746, 4436)
        tile("daythRoom", 2810, 4453)
        at("goto-key", 2795, 4456)
        t.drive.camera(0, 383, 400)
        t.exec("tryToPickUpKey", t.player.press, "hauntedmine_boss_key", 1, 8)
        local rr, rd = t.msg.expect("Treus Dayth rises")
        if rr ~= "ok" then rr, rd = t.msg.await("Treus Dayth rises", 40) end
        t.check("dayth-rises", rr, rd)
        t.player.walk_to(2799, 4455, 12)
        t.exec("attackDayth", t.player.attack, "hauntedmine_boss_ghost", 2, 20)
        t.exec("killDayth", t.npc.await_dead_engaged, 300, 30, { eat = { item = "shark", below = 75 } })
        t.check("dayth-killed", t.var.await("varp382_hauntedmine", 9, 10))
        local pkr, pkd = t.player.press("hauntedmine_boss_key", 1, 8)
        local kr, kd = t.inv.await("hauntedmine_reward_key", 1, 8)
        t.check("pickUpKey", kr, "press " .. tostring(pkr) .. " " .. tostring(pkd) .. "; key in pack: " .. tostring(kd))
        t.drive.camera(1024, 383, 300)
        at("goto-dayth-stair", 2809, 4453)
        click("goUpFromDayth", "hauntedmine_light_stairs_bottom", 2812, 4452)
        tile("floodedAgain", 2746, 4439)
        at("goto-flooded-west", 2693, 4440)
        click("goDownToCrystals", "hauntedmine_dark_stairs_top", 2692, 4436)
        tile("crystalEntrance", 2758, 4453)
        click("openRewardDoor", "hauntedmine_rewarddoor_l", 2773, 4450)
        local _, xpsnap = t.skill.snapshot()
        click("cutCrystal", "crystalcorner", 2787, 4428)
        t.check("stage-complete", t.var.await("varp382_hauntedmine", 11, 10))
        t.quest.expect_complete()
        t.check("reward.strength_xp", t.skill.expect_gain("strength", 22000, xpsnap))
        -- dark rooms
        local dr, dd = t.player.drop("glowing_fungus")
        t.check("drop-fungus", tostring(dd):find("crumbles to ashes", 1, true) ~= nil, tostring(dr) .. " " .. tostring(dd))
        at("goto-flooded", 2726, 4455)
        at("goto-flooded-east", 2745, 4440)
        click("dark-dayth", "hauntedmine_dark_stairs_top", 2746, 4436)
        tile("inDarkDaythRoom", 2732, 4562)
        click("leaveDarkDaythRoom", "hauntedmine_dark_stairs_bottom", 2731, 4561)
        tile("flooded-after-dark-dayth", 2746, 4440)
        at("goto-flooded-west2", 2693, 4440)
        click("dark-crystal", "hauntedmine_dark_stairs_top", 2692, 4436)
        tile("inDarkCrystalRoom", 2711, 4591)
        click("leaveDarkCrystalRoom", "hauntedmine_dark_stairs_bottom", 2709, 4591)
        tile("flooded-after-dark-crystal", 2692, 4440)
        t.finish(0)
    end,
}
