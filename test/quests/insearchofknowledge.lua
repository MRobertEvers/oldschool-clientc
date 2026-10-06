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

        -- Forthos route: webs (bigweb_slashable) sit across the passage; cut each with the knife by click, then walk on
        local reached, cuts = false, 0
        local sr, sd = t.player.walk_to(1841, 9945, 60)
        t.check("walkToFirstWeb", sr == "ok" or sr == "timeout", "walk_to 1841,9945 -> " .. tostring(sr) .. " " .. tostring(sd))
        for leg = 1, 8 do
            local wr, wd = t.player.walk_to(1842, 9928, 60)
            local _, wt = t.world.tile()
            t.check("walkToAimeri" .. leg, true, "walk_to 1842,9928 -> " .. tostring(wr) .. " now " .. tostring(wt and wt.x) .. "," .. tostring(wt and wt.z))
            if wr == "ok" then reached = true; break end
            for try = 1, 6 do
                if t.world.loc_near("bigweb_slashable", 12) ~= "ok" then break end
                cuts = cuts + 1
                t.exec("cutWeb" .. cuts, t.player.click_loc, "bigweb_slashable", 1)
                t.ticks(3)
            end
        end
        t.check("walkToAimeri.arrived", reached, "reached Aimeri's room after cutting " .. cuts .. " web click(s)")
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

        -- back to the exit ladder ON FOOT: the webs cut on the way in respawn (general_use/scripts/web.rs2), so cut
        -- any that stand across the passage again, by click, the way the inbound leg does (b52 round 3: a goto here
        -- teleported past them)
        -- the way out runs back through Aimeri's room (probe build/quest_gate/isok_exit_probe3: 1816,9939 -> 1820,9930
        -- -> 1843,9926 on foot); from there north to the ladder the webs stand again
        for i, hop in ipairs({ { 1820, 9930 }, { 1843, 9926 } }) do
            local hr = t.player.walk_to(hop[1], hop[2], 60)
            local _, ht = t.world.tile()
            t.check("walkBackToAimeri" .. i, hr == "ok", "walk_to " .. hop[1] .. "," .. hop[2] .. " -> " .. tostring(hr) .. " now " .. tostring(ht and ht.x) .. "," .. tostring(ht and ht.z))
        end
        -- The two webs on the way out, in route order (maps/m28_155.jl2 bigweb_slashable): the pair across the passage
        -- north of Aimeri at 1841-1842,9933 (wall on the tile's north edge) and the one at 1833,9944 (south edge) on
        -- the way north to the ladder. A cut succeeds 1 in 2 and opens the web for 100 ticks (general_use/scripts/
        -- web.rs2, LostCity's), so each web is cut and then stepped through at once.
        local function cut_through(name, web_x, web_z, past_x, past_z)
            local note = ""
            for attempt = 1, 10 do
                local wr = t.player.walk_to(past_x, past_z, 15)
                local _, w = t.world.tile()
                note = note .. "[" .. attempt .. " walk " .. tostring(wr) .. " -> " .. tostring(w and w.x) .. "," .. tostring(w and w.z) .. "] "
                if wr == "ok" then
                    t.check(name, true, "through the web at " .. web_x .. "," .. web_z .. " to " .. past_x .. "," .. past_z .. " :: " .. note)
                    return true
                end
                t.exec(name .. "-cut" .. attempt, t.player.click_loc, "bigweb_slashable", 1, { at = { web_x, web_z } })
                t.ticks(3)
            end
            t.check(name, false, "never got through the web at " .. web_x .. "," .. web_z .. " :: " .. note)
            return false
        end
        cut_through("cutWebOut-aimeri", 1842, 9933, 1842, 9934)
        cut_through("cutWebOut-north", 1833, 9944, 1833, 9945)
        local lr = t.player.walk_to(1830, 9972, 60)
        local _, lt = t.world.tile()
        t.check("walkToExitLadder.arrived", lr == "ok", "walk_to 1830,9972 -> " .. tostring(lr) .. " now " .. tostring(lt and lt.x) .. "," .. tostring(lt and lt.z))
        t.exec("leaveDungeon", t.player.click_loc, "hosdun_entrance_ladder", 1)
        t.ticks(4)
        local _, ot = t.world.tile()
        t.check("leaveDungeon.surface", ot and ot.z < 9000, "tile " .. tostring(ot and ot.x) .. "," .. tostring(ot and ot.z))
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
