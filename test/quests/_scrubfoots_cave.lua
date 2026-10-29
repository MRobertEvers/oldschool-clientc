-- Scrubfoot's cave, Sled Racing and the Goblin Village rows of RSProx index
-- 4402 (2026-10-28, rev 241; docs/rsprox-analysis/quests/scrubfoots_cave.md):
--   * a soldier's Talk-to while the RFD generals are at 5 (four pages, [436]);
--   * the village ladder up (same tile, level 2) and down (2955,3497,0) [637..662];
--   * the single and the double door [456], [665];
--   * the cave mouth to 1975,6220,3 [681..694];
--   * the slope without a sled in hand: the page [721]; the sled pile [705];
--     Ride [726]; "Sled down the slope?" Yes. [741]: the copy, the clock;
--   * the course walked along 4402's own clicks to the jump [912], the landing
--     at 1942,6226,0 [920], the best and the sled back in the pack;
--   * Scrubfoot's menu [936..1001]; the crevice to 1963,6221,3 [1018]; the
--     exit to 2949,3508,0 [1034];
--   * the percy sacks' Prod and Open [1541], [1569].
--
--     python3 tools/quest_gate/run.py --script test/quests/_scrubfoots_cave.lua --name scrubfoots_cave --no-publish
return {
    id = "scrubfoots_cave",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv",
        "::setvar varb2378_gobdip_main 6",
        "::setvar varb1867_100goblin 5",
        "::goto 2957 3505 0",
    },

    run = function(t)
        t.note(select(2, t.ticklog.start()))
        t.ticks(3)
        local function count(name)
            local _, n = t.inv.count(name)
            return n or 0
        end
        local function at(name, x, z, level)
            local r, w = t.world.tile()
            t.check(name, r == "ok" and w.x == x and w.z == z and (level == nil or w.level == level),
                tostring(r) .. " " .. tostring(w and w.x) .. "," .. tostring(w and w.z) .. "," .. tostring(w and w.level)
                .. " want " .. x .. "," .. z .. "," .. tostring(level))
        end
        local function varbit(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or -1
        end

        -- [436]..[440]
        t.ticklog.mark("leg.soldier")
        t.exec("soldier.talk", t.player.talk_to, "goblin_green_soldier_2", 1)
        t.expect("soldier.pages", t.chat.play({
            "player:Hey, I need your help. Wartface and Bentnoze have",
            "npc:What that got to do with me?",
            "player:I need to find a meal they'd both eat.",
            "npc:Me no chef, ask village cook instead.",
            "end" }))

        -- [637]..[662]
        t.ticklog.mark("leg.ladder")
        t.exec("ladder.up", t.player.click_loc, "goblin_ladder_bottom", 1)
        t.ticks(5)
        at("ladder.up.landed", 2955, 3497, 2)
        t.exec("ladder.down", t.player.click_loc, "goblin_ladder_top", 1)
        t.ticks(5)
        at("ladder.down.landed", 2955, 3497, 0)

        -- [665]..[667], [456]..[459]
        t.ticklog.mark("leg.doors")
        t.exec("door.single", t.player.click_loc, "goblin_outpost_poordoor", 1)
        t.ticks(3)
        t.exec("door.double", t.player.click_loc, "goblin_outpost_poordoor_double_inner", 1)
        t.ticks(3)

        -- [681]..[694]
        t.ticklog.mark("leg.cave")
        t.exec("cave.enter", t.player.click_loc, "xmas20_goblin_cave_entrance", 1)
        t.ticks(10)
        at("cave.landed", 1975, 6220, 3)

        -- [720]..[725]: no sled in hand.
        t.ticklog.mark("leg.slope_nosled")
        t.exec("slope.nosled", t.player.click_loc, "xmas20_slope_start", 1)
        t.expect("slope.nosled.page", t.chat.play({
            "player:It looks too steep to go down by foot. Maybe I could", "end" }))

        -- [702]..[705]
        t.ticklog.mark("leg.sled")
        t.exec("sled.take", t.player.click_loc, "xmas20_sled_loc", 1)
        t.exec("sled.inv", t.inv.await, "xmas20_sled", 1, 8)
        t.expect("sled.line", t.msg.expect("You pick up a sled. There seems to be more available"))

        -- [726]..[727]
        t.player.inv_op("xmas20_sled", 2)
        t.ticks(2)
        t.check("sled.worn", count("xmas20_sled") == 0 and "ok" or "hollow", "inv sleds " .. count("xmas20_sled"))

        -- [737]..[746]
        t.ticklog.mark("leg.start")
        t.exec("slope.start", t.player.click_loc, "xmas20_slope_start", 1)
        t.expect("slope.menu", t.chat.play({ "options", "choose:Yes." }))
        t.ticks(6)
        local sr, s = t.world.tile()
        local ox, oz = 0, 0
        if sr == "ok" then
            ox, oz = s.x - 1959, s.z - 6228
        end
        t.check("course.copy", sr == "ok" and s.x > 6000 and s.level == 2,
            tostring(sr) .. " " .. tostring(s and s.x) .. "," .. tostring(s and s.z) .. "," .. tostring(s and s.level))
        t.check("course.clock", varbit("varb11731_xmas20_time") >= 4 and "ok" or "hollow",
            "xmas20_time " .. varbit("varb11731_xmas20_time"))

        -- 4402's clicks down the course ([746]..[862]), in the copy.
        t.ticklog.mark("leg.course")
        local route = {
            { 1955, 6233 }, { 1955, 6236 }, { 1957, 6237 }, { 1959, 6238 }, { 1962, 6237 }, { 1969, 6238 },
            { 1974, 6240 }, { 1976, 6242 }, { 1978, 6245 }, { 1980, 6248 }, { 1979, 6252 }, { 1974, 6251 },
            { 1970, 6251 }, { 1967, 6253 }, { 1968, 6258 }, { 1968, 6260 }, { 1970, 6264 }, { 1969, 6267 },
            { 1964, 6269 }, { 1961, 6269 }, { 1954, 6261 }, { 1953, 6258 }, { 1951, 6256 }, { 1948, 6249 },
            { 1950, 6246 }, { 1951, 6245 }, { 1950, 6237 }, { 1934, 6233 }, { 1928, 6240 }, { 1928, 6244 },
            { 1933, 6248 }, { 1938, 6247 }, { 1943, 6241 },
        }
        for i, p in ipairs(route) do
            local wr, wd = t.player.walk_to(p[1] + ox, p[2] + oz, 40)
            if wr ~= "ok" then
                t.note("course.walk." .. i .. " " .. tostring(wr) .. " " .. tostring(wd))
            end
        end
        local pb0 = varbit("varb11732_xmas20_time_pb")
        t.ticklog.mark("leg.jump")
        t.exec("jump", t.player.click_loc, "xmas20_slope_end", 1)
        t.ticks(14)
        at("jump.landed", 1942, 6226, 0)
        local pb = varbit("varb11732_xmas20_time_pb")
        t.check("jump.best", pb > 0 and pb0 == 0 and "ok" or "hollow", "xmas20_time_pb " .. pb0 .. " -> " .. pb)
        t.expect("jump.land.line", t.msg.expect("You land softly in a pile of snow at the bottom of the slope."))
        t.expect("jump.best.line", t.msg.expect("New personal best:"))
        t.check("jump.sled.back", count("xmas20_sled") == 1 and "ok" or "hollow", "inv sleds " .. count("xmas20_sled"))

        -- [932]..[1005]
        t.ticklog.mark("leg.scrubfoot")
        t.exec("scrubfoot.talk", t.player.talk_to, "xmas20_scrubfoot_crashed", 1)
        t.expect("scrubfoot.pages", t.chat.play({
            "options", "choose:Scrubfoot, are you okay?",
            "player:Scrubfoot, are you okay?",
            "npc:Ugh... no. Scrubfoot can't move.",
            "player:I'm sorry. Sledding is dangerous business.",
            "npc:That it is. It also dangerous business going back to",
            "options", "choose:What were you doing here?",
            "player:What were you doing here?",
            "npc:General Wartface send Scrubfoot to get green glowy",
            "player:I guess you won't be able to get the green ones now.",
            "npc:It not matter. If Scrubfoot get green fireflies, General",
            "player:Seems like you can't win with those generals. What if",
            "npc:Scrubfoot never tried that.",
            "options", "choose:How do I get out of here?",
            "player:How do I get out of here?",
            "npc:Over by water there is crack in the wall.",
            "player:Thanks. Will you make it out by yourself?",
            "npc:Scrubfoot needs a minute before moving.",
            "player:Good luck with that.",
            "options" }))
        t.ui.close_modal()
        t.ticks(2)

        -- [1009]..[1021]
        t.ticklog.mark("leg.crevice")
        t.exec("crevice", t.player.click_loc, "xmas20_crevice", 1)
        t.ticks(10)
        at("crevice.landed", 1963, 6221, 3)

        -- [1028]..[1037]
        t.ticklog.mark("leg.exit")
        t.exec("cave.exit", t.player.click_loc, "xmas20_goblin_cave_exit", 1)
        t.ticks(10)
        at("exit.landed", 2949, 3508, 0)

        -- [1541]..[1544], [1569]..[1570]
        t.ticklog.mark("leg.sacks")
        t.cheat("::goto 2961 3506 0")
        t.ticks(3)
        t.exec("sacks.prod", t.player.click_loc, "percy_sacks", 1)
        t.expect("sacks.prod.box", t.chat.play({ "mesbox:You hear a muffled groan. The sack wriggles slightly.", "end" }))
        t.exec("sacks.open", t.player.click_loc, "percy_sacks", 2)
        t.ticks(2)
        t.expect("sacks.open.line", t.msg.expect("I have no reason to do that."))
        t.finish(0)
    end,
}
