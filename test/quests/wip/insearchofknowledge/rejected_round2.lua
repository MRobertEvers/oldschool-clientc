-- In Search of Knowledge. Spec: docs/quests/ladders/insearchofknowledge.notes.md, parity script isok.lua.
return {
    id = "insearchofknowledge",
    max_frames = 480000,
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99",
        "::give osmumtens_fang 1", "::wield osmumtens_fang", "::give dragonfire_shield 1", "::wield dragonfire_shield",
        "::give lobster 20", "::give bread 1", "::give knife 1",
        "::passive red_dragon", "::passive red_dragon2", "::passive red_dragon3", "::passive red_dragon4",
        "::passive babyreddragon", "::passive hosdun_druid", "::passive hosdun_spider",
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

        -- getPages: real kills; each red dragon rolls a 1/10 page (isok_page_drop, insearchofknowledge_locs.rs2:166)
        t.exec("goto-dragons", t.player.goto_tile, 1815, 9934, 0)
        t.ticks(7) -- an idle beat moves the run onto another roll stream
        local pages = {
            { page = "hosdun_sun_page", tome = "hosdun_sun_tome", var = "varb8399_hosdun_sun_pages", name = "sun" },
            { page = "hosdun_moon_page", tome = "hosdun_moon_tome", var = "varb8400_hosdun_moon_pages", name = "moon" },
            { page = "hosdun_temple_page", tome = "hosdun_temple_tome", var = "varb8401_hosdun_temple_pages", name = "temple" },
        }
        local targets = { "red_dragon", "red_dragon2", "red_dragon3", "red_dragon4" }
        local kills, misses, attackfails, lastmiss, lastattack = 0, 0, 0, "", ""
        local done = false
        for k = 1, 400 do
            local tgt = targets[(k % #targets) + 1]
            local ra, rad = t.player.attack(tgt, 2, 10)
            if ra == "ok" then
                local rd, dd = t.npc.await_dead_engaged(300, 12, { eat = { item = "lobster", below = 55 } })
                if rd == "ok" then kills = kills + 1 else misses = misses + 1; lastmiss = tostring(rd) .. " " .. tostring(dd):sub(1, 200) end
            else
                attackfails = attackfails + 1; lastattack = tgt .. " " .. tostring(ra) .. " " .. tostring(rad):sub(-220)
                t.ticks(3)
            end
            done = true
            for _, p in ipairs(pages) do
                if t.world.obj_near(p.page, 14) == "ok" then
                    local before = select(2, t.inv.count(p.page))
                    t.exec("getPages." .. p.name .. ".pickup" .. kills, t.player.click_obj, p.page, 3)
                    t.ticks(2)
                end
                local _, held = t.inv.count(p.page)
                local _, ins = t.var.varbit(p.var)
                if held and held > 0 and ins and ins < 4 then
                    t.exec("insertPage." .. p.name .. kills, t.player.use_item_on_item, p.page, p.tome)
                    t.ticks(2)
                    _, ins = t.var.varbit(p.var)
                end
                if not ins or ins < 4 then done = false end
            end
            if done then break end
        end
        for _, p in ipairs(pages) do
            t.expect("getPages." .. p.name .. ".full", t.var.await(p.var, 4, 6))
        end
        t.check("getPages.kills", done, "red dragon kills=" .. kills .. " waitmiss=" .. misses .. " (" .. lastmiss .. ") attackfail=" .. attackfails .. " (" .. lastattack .. ")")

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
