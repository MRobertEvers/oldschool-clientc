-- In Search of Knowledge. Spec: docs/quests/ladders/insearchofknowledge.notes.md, parity script isok.lua.
-- GUIDE-GAP: getPages 12 pages are 1/20..1/30 tertiaries, no cheat names them (insearchofknowledge.rs2:18 leftover_forthos_combat_page_drops); one real drop is driven, the rest ::give.
return {
    id = "insearchofknowledge",
    max_frames = 400000,
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99",
        "::give rune_scimitar 1", "::wield rune_scimitar",
        "::give lobster 20", "::give bread 1", "::give knife 1",
        "::godmode",
        "::insearchofknowledge",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb8403_hosdun_knowledge_search",
            constants = { not_started = 0, tomes = 1, logosia = 2, complete = 3 },
            row = "miniquest_insearchofknowledge",
            display = "In Search of Knowledge",
            points = 0,
        })
        t.ticks(4)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("enterDungeon", t.player.click_loc, "hosdun_entrance_ladder_hole", 1)
        t.ticks(4)
        local _, tile = t.world.tile()
        t.check("enterDungeon.landed", tile and tile.z > 9000, "tile " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z))

        t.exec("goto-aimeri", t.player.goto_tile, 1840, 9929, 0)
        local aim = t.player.by_symbol("npc", "hosdun_aimeri_injured")
        t.exec("useFoodOnAimeri.bread", t.player.use_on, "bread", aim)
        t.check("useFoodOnAimeri.breadRefused", t.msg.expect("isn't suitable food"), "bread refused and kept")
        for i = 1, 5 do
            t.exec("useFoodOnAimeri" .. i, t.player.use_on, "lobster", aim)
            t.ticks(2)
        end
        t.expect("aimeri.fed", t.var.expect("varb8393_hosdun_aimeri_status", 5))

        t.exec("talkToAimeri", t.player.talk_to, "hosdun_aimeri_healed")
        t.exec("talkToAimeri.place", t.chat.play, {
            "npc:Thanks for helping me",
            "choose:What is this place?",
            "player:What is this place?",
            "npc:This place probably had a name",
            "npc:It is rumoured",
            "player:Who are Ralos and Ranul?",
            "npc:According to religious texts",
            "npc:If you find any artifacts",
            "player:I'll see what I find.",
            "npc:You need only to look up",
        })
        t.expect("quest.stage.deferred", t.quest.expect_stage("not_started"))
        t.exec("talkToAimeriAgain", t.player.talk_to, "hosdun_aimeri_healed")
        t.exec("talkToAimeriAgain.dialog", t.chat.play, {
            "npc:Thanks for helping me",
            "choose:Who are you?",
            "player:Who are you",
            "npc:My name is Brother Aimeri",
            "npc:I worship Ralos",
            "player:I'll keep my eyes open.",
            "npc:You have my gratitude.",
            "player:The greater good.",
        })
        t.expect("quest.stage.tomes", t.quest.expect_stage("tomes"))

        t.exec("goto-temple", t.player.goto_tile, 1796, 9934, 0)
        t.exec("searchBookcasesForTemple", t.player.click_loc, "hosdun_temple_bookcase", 1)
        t.expect("temple.tome", t.inv.await("hosdun_temple_tome", 1, 10))
        t.exec("goto-sun", t.player.goto_tile, 1805, 9934, 0)
        t.exec("searchBookcasesForSun", t.player.click_loc, "hosdun_sun_bookcase", 1)
        t.expect("sun.tome", t.inv.await("hosdun_sun_tome", 1, 10))
        t.exec("goto-moon", t.player.goto_tile, 1804, 9942, 0)
        t.exec("searchBookcasesForMoon", t.player.click_loc, "hosdun_moon_bookcase", 1)
        t.expect("moon.tome", t.inv.await("hosdun_moon_tome", 1, 10))

        -- getPages: a real kill for a real page drop (1/20..1/30 tertiaries)
        t.exec("goto-dragons", t.player.goto_tile, 1815, 9934, 0)
        t.ticks(7) -- an idle beat moves the run onto another roll stream
        local got = nil
        local kills = 0
        local targets = { "babyreddragon", "babyreddragon", "hosdun_druid" }
        for k = 1, 150 do
            local tgt = targets[(k % #targets) + 1]
            local ra = t.player.attack(tgt, 2, 10)
            if ra == "ok" then
                t.npc.await_dead_engaged(300, 12, { eat = { item = "lobster", below = 55 } })
                kills = kills + 1
            else
                t.ticks(3)
            end
            for _, pg in ipairs({ "hosdun_sun_page", "hosdun_moon_page", "hosdun_temple_page" }) do
                if t.world.obj_near(pg, 14) == "ok" then got = pg end
            end
            if got then break end
        end
        t.check("getPages.drop", got ~= nil, "kills=" .. kills .. " page=" .. tostring(got))
        if got then
            t.exec("getPages.pickup", t.player.click_obj, got, 3)
            t.expect("getPages.held", t.inv.await(got, 1, 10))
        end

        -- grind fast-forward: remaining pages (QUEST_SERVER_CHEATS has no page cheat; labelled ::give)
        t.cheat("::give hosdun_sun_page 4")
        t.cheat("::give hosdun_moon_page 4")
        t.cheat("::give hosdun_temple_page 4")
        t.ticks(3)
        t.exec("sun.insert", t.player.use_item_on_item, "hosdun_sun_page", "hosdun_sun_tome")
        t.expect("sun.pages", t.var.await("varb8399_hosdun_sun_pages", 4, 6))
        t.exec("moon.insert", t.player.use_item_on_item, "hosdun_moon_page", "hosdun_moon_tome")
        t.expect("moon.pages", t.var.await("varb8400_hosdun_moon_pages", 4, 6))
        t.exec("temple.insert", t.player.use_item_on_item, "hosdun_temple_page", "hosdun_temple_tome")
        t.expect("temple.pages", t.var.await("varb8401_hosdun_temple_pages", 4, 6))

        t.exec("goto-logosia", t.player.goto_tile, 1633, 3806, 0)
        local lib = t.player.by_symbol("npc", "arceuus_library_librarian")
        t.exec("useMoonOnLogosia", t.player.use_on, "hosdun_moon_tome", lib)
        t.expect("quest.stage.logosia", t.quest.expect_stage("logosia"))
        t.expect("tomes.handed", t.inv.expect_absent("hosdun_sun_tome"))

        local lamp_result, lamp_before = t.inv.count("thosf_reward_lamp")
        t.exec("talkToLogosia", t.player.talk_to, "arceuus_library_librarian")
        t.exec("talkToLogosia.dialog", t.chat.play, { "npc:Your contributions" })
        t.quest.expect_complete()
        local lamp_result2, lamp_after = t.inv.count("thosf_reward_lamp")
        t.check("reward.thosf_reward_lamp", lamp_result == "ok" and lamp_result2 == "ok" and lamp_after == lamp_before + 1,
            string.format("lamp %s -> %s (want +1)", tostring(lamp_before), tostring(lamp_after)))
        t.finish(0)
    end,
}
