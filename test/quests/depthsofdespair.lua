return {
    id = "depthsofdespair",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::complete quest_clientofkourend", "::complete quest_xmarksthespot", "::depthsofdespair", "::setlevel agility 30", "::setlevel attack 60", "::setlevel strength 60", "::setlevel hitpoints 60", "::give lobster 10" },
    run = function(t)
        t.quest.bind({
            varp = "varb6027_hosidiusquest",
            constants = { not_started = 0, olivia = 1, galana = 2, envoy = 3, caves = 4, navigate = 6, artur = 7, snake = 8, chest = 9, ret = 10, complete = 11 },
            display = "The Depths of Despair", points = 1,
        })
        t.ticks(3)
        t.exec("talkToKandur", t.player.talk_to, "hosidiusquest_lord", 1)
        t.exec("kandur-menu", t.chat.drain, { stop_at = "options" })
        t.exec("kandur-yes", t.chat.choose, "Yes.")
        t.exec("kandur-done", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.olivia", t.quest.expect_stage("olivia"))
        t.exec("talkToOlivia", t.player.talk_to, "hosidiusquest_chef", 1)
        t.exec("olivia-done", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.galana", t.quest.expect_stage("galana"))
        t.exec("goto-galana", t.player.goto_tile, 1650, 3824, 0)
        t.exec("talkToGalana", t.player.talk_to, "hosidiusquest_librarian", 1)
        t.exec("galana-done", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.envoy", t.quest.expect_stage("envoy"))
        t.exec("galana-again", t.player.talk_to, "hosidiusquest_librarian", 1)
        t.exec("galana-again-done", t.chat.drain, {})
        local ok, n = t.inv.count("hosidiusquest_book")
        t.check("galana.grants_no_book", ok == "ok" and n == 0, "book count " .. tostring(n))
        t.cheat("::dodshelf")
        t.ticks(4)
        local _, list = t.msg.last(8)
        local sx, sz, sl, sid
        for _, line in ipairs(list or {}) do
            local text = type(line) == "table" and (line.text or line[1]) or line
            local a, b, c, d = tostring(text):match("dodshelf (%d+),(%d+),(%d+) type=(%d+)")
            if a then sx, sz, sl, sid = tonumber(a), tonumber(b), tonumber(c), tonumber(d) end
        end
        t.check("shelf.found", sx ~= nil, "shelf " .. tostring(sx) .. "," .. tostring(sz) .. "," .. tostring(sl) .. " id " .. tostring(sid))
        local sym = "archeuus_library_bookcase_g1_end_right_door_03_red"
        t.check("shelf.sym", sid == 28006, "uid-picked shelf type " .. tostring(sid))
        t.exec("goto-shelf", t.player.goto_tile, sx, sz - 3, 1)
        t.exec("searchShelf", t.player.click_loc, sym, 1, { at = { sx, sz, 1 } })
        t.inv.await("hosidiusquest_book", 1, 15)
        local bok, bn = t.inv.count("hosidiusquest_book")
        t.check("findEnvoy.book", bok == "ok" and bn == 1, "book count " .. tostring(bn))
        t.exec("readEnvoy", t.player.inv_op, "hosidiusquest_book", 1)
        t.ticks(2)
        t.expect("quest.stage.caves", t.quest.expect_stage("caves"))
        t.exec("goto-caves", t.player.goto_tile, 1645, 3452, 0)
        t.exec("enterCrabclawCaves", t.player.click_loc, "hosidiusquest_cave_entrance", 1)
        t.ticks(3)
        t.expect("quest.stage.navigate", t.quest.expect_stage("navigate"))
        t.exec("goto-crevice", t.player.goto_tile, 1711, 9824, 0)
        t.exec("goThroughCrevice", t.player.click_loc, "hosidiusquest_crackin", 1)
        t.ticks(4)
        t.exec("goto-stones", t.player.goto_tile, 1706, 9801, 0)
        local tries = 0
        repeat
            tries = tries + 1
            t.exec("stepOverSteppingStones" .. tries, t.player.click_loc, "hosidiusquest_stone", 1)
            t.ticks(6)
        until (function() local _, q = t.world.tile(); return q ~= nil and q.x <= 1703 end)() or tries >= 8
        local _, tl1 = t.world.tile(); local sxx, szz = tl1 and tl1.x, tl1 and tl1.z
        t.check("stones.crossed", (function() local _, q = t.world.tile(); return q ~= nil and q.x <= 1703 end)(), "after " .. tries .. " attempt(s) at " .. tostring(sxx) .. "," .. tostring(szz))
        tries = 0
        repeat
            tries = tries + 1
            t.exec("climbPastRocks" .. tries, t.player.click_loc, "hosidiusquest_rock", 1)
            t.ticks(6)
        until (function() local _, q = t.world.tile(); return q ~= nil and q.x <= 1685 end)() or tries >= 8
        local _, tl2 = t.world.tile(); local rx, rz = tl2 and tl2.x, tl2 and tl2.z
        t.check("rocks.crossed", (function() local _, q = t.world.tile(); return q ~= nil and q.x <= 1685 end)(), "after " .. tries .. " attempt(s) at " .. tostring(rx) .. "," .. tostring(rz))
        t.exec("enterTunnelEntrance", t.player.click_loc, "hosidiusquest_rope_top", 1)
        t.ticks(4)
        t.expect("quest.stage.artur", t.quest.expect_stage("artur"))
        t.exec("talkToArturHosidius", t.player.talk_to, "hosidiusquest_son_caves", 1)
        t.exec("artur-done", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.snake", t.quest.expect_stage("snake"))
        local rt = 0
        repeat
            rt = rt + 1
            t.exec("climbRubble" .. rt, t.player.click_loc, "hosidiusquest_rock_snake", 1)
            t.ticks(6)
            local _, tr = t.world.tile()
            if tr and tr.x >= 1688 then break end
        until rt >= 8
        local ra, da = t.player.attack("hosidiusquest_snake", 2, 10)
        t.check("snake-attack", ra == "ok" or ra == "timeout", tostring(ra) .. " " .. tostring(da))
        t.exec("snake-dead", t.npc.await_dead_engaged, 300, 12, { eat = { item = "lobster", below = 55 } })
        t.ticks(2)
        t.expect("quest.stage.chest", t.quest.expect_stage("chest"))
        t.exec("searchChest", t.player.click_loc, "hosidiusquest_chest", 1)
        t.inv.await("hosidiusquest_accord", 1, 15)
        t.expect("quest.stage.ret", t.quest.expect_stage("ret"))
        t.exec("goto-kandur", t.player.goto_tile, 1782, 3569, 0)
        local _, snap = t.skill.snapshot()
        local _, coins0 = t.inv.count("coins")
        t.exec("talkToLordKandurAgain", t.player.talk_to, "hosidiusquest_lord_vis", 1)
        t.exec("kandur-return-done", t.chat.drain, {})
        t.ticks(3)
        local gr, gd = t.skill.expect_gain("agility", 1500, snap)
        t.check("agility.gain", gr == "ok", tostring(gr) .. " " .. tostring(gd))
        local _, coins1 = t.inv.count("coins")
        t.check("reward.coins", (coins1 or 0) - (coins0 or 0) == 4000, "coins " .. tostring(coins0) .. " -> " .. tostring(coins1))
        local pr, pn = t.inv.count("veos_memoirs_hos_page")
        t.check("reward.page", pr == "ok" and pn == 1, "memoir page count " .. tostring(pn))
        t.quest.expect_complete()
        t.finish(0)
    end,
}
