-- Monkey Madness I: the 10th squad sigil and the Jungle Demon, in the order
-- RSProx index 4565 recorded it ([1741]..[2179]): the sigil worn as a monkey,
-- the fight, out by the NE trapdoor and the centre crack, Bonzara, the sigil
-- again ("... again!"), the kill, Zooknock, Garkor's report, Zooknock's
-- teleport. mm_demon.rs2 has the recorded ticks;
-- docs/rsprox-analysis/quests/monkey_madness_i_demon.md the tables.
--
--     python3 tools/quest_gate/run.py --script test/quests/_mm_demon.lua --name mm_demon --fixture fresh_lumbridge.ini
return {
    id = "mm_demon",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::setvar varp365_mm_main 5",
        "::setvar varb126_mm_garkor 6",
        "::setlevel attack 58",
        "::setlevel defence 44",
        "::setlevel strength 46",
        "::setlevel hitpoints 99",
        "::setlevel ranged 70",
        "::setlevel prayer 99",
        "::setlevel magic 50",
        "::clearinv",
        "::give mm_sigil",
        "::give adamant_knife 1000",
        "::give mm_monkey_greegree_for_normal_monkey",
        "::give shark 12",
        "::tele 0_43_43_53_9",
    },

    run = function(t)
        t.ticklog.start()
        local function tile()
            local r, w = t.world.tile()
            if r ~= "ok" or w == nil then return nil end
            return w
        end
        local function at(name, x, z, level, slack)
            local w = tile()
            local ok = w ~= nil and math.abs(w.x - x) <= (slack or 0) and math.abs(w.z - z) <= (slack or 0) and w.level == level
            t.check(name, ok, "at " .. (w and (w.x .. "," .. w.z .. "," .. w.level) or "?") .. " want " .. x .. "," .. z .. "," .. level)
        end
        local function stage(name, var, want)
            local _, v = t.var.server(var)
            t.check(name, v == want, var .. " " .. tostring(v) .. " want " .. tostring(want))
        end
        local function await_page(n)
            for _ = 1, n or 15 do
                if t.chat.kind() ~= "none" then return end
                t.ticks(1)
            end
        end
        -- Protect from Magic, pressed only when it is off (the button toggles)
        local function protect()
            local _, on = t.var.varbit("varb4116_prayer_protectfrommagic")
            if on == 1 then return end
            t.ui.tab("prayer")
            t.ticks(2)
            local r, w = t.ui.widget("prayerbook:prayer13")
            if r == "ok" then t.ui.invoke(w, 1) end
            t.ticks(3)
        end
        local function sigil(leg)
            t.exec(leg .. "-wear", t.player.inv_op, "mm_sigil", 2)
            await_page()
            t.exec(leg .. "-yes", t.chat.choose, "Yes")
            -- C+27: the squad, then (as a monkey) the greegree's mesbox, then Garkor
            t.ticks(30)
            await_page(20)
        end

        -- the first visit, worn as a monkey ([1741]; the greegree is taken at [1772])
        t.ticks(2)
        t.exec("hold-greegree", t.player.inv_op, "mm_monkey_greegree_for_normal_monkey", 2)
        t.ticks(3)
        t.ticklog.mark("leg.sigil1")
        sigil("sigil1")
        t.exec("sigil1-pages", t.chat.play, {
            "mesbox:The Monkey Greegree wrenches itself from your hand as its power begins to fade...",
            "npc:Ready yourself, human: the final battle begins!",
        })
        at("sigil1.arena", 2702, 9173, 1)
        local _, gg = t.inv.count("mm_monkey_greegree_for_normal_monkey")
        t.check("sigil1.greegree", gg == 1, "the greegree back in the backpack: " .. tostring(gg))
        local _, sg = t.inv.count("mm_sigil")
        t.check("sigil1.not-worn", sg == 1, "the sigil stays in the backpack: " .. tostring(sg))

        -- the fight ([1778]..[1839]): knives from range, Protect from Magic
        t.ticklog.mark("leg.fight1")
        t.exec("fight1-wield", t.player.inv_op, "adamant_knife", 2)
        t.ticks(2)
        protect()
        t.player.attack("mm_demon", 2, 20)
        t.ticks(60)

        -- out by the NE trapdoor ([1859]..[1870]) and the centre crack ([1890]..[1905])
        t.ticklog.mark("leg.exit")
        t.player.walk_to(2741, 9206, 60)
        t.exec("trapdoor-open", t.player.click_loc, "mm_trapdoor_ne", 1, { at = { 2741, 9205 } })
        t.ticks(4)
        t.exec("trapdoor-down", t.player.click_loc, "mm_trapdoor_ne_open", 1, { at = { 2741, 9205 } })
        t.ticks(12)
        at("trapdoor.landing", 2739, 9205, 0)
        t.player.walk_to(2715, 9184, 60)
        t.exec("crack", t.player.click_loc, "mm_wallcrack_center", 1, { at = { 2715, 9185 } })
        t.ticks(12)
        at("crack.landing", 2415, 9908, 0)

        -- Bonzara ([1914]..[1931])
        t.ticklog.mark("leg.bonzara")
        t.exec("bonzara", t.player.talk_to, "mm_bonzara", 1)
        await_page()
        t.exec("bonzara-dialog", t.chat.play, {
            "npc:It looks like you're trying to escape. Would you like some help?",
            "choose:Yes",
            "player:I ... uh ... yes.",
            "npc:Right you are.",
        })
        t.ticks(10)
        at("bonzara.landing", 2705, 2782, 0)

        -- the second visit ([1951]..[1983]): no greegree, Garkor's "again"
        t.ticklog.mark("leg.sigil2")
        sigil("sigil2")
        t.exec("sigil2-pages", t.chat.play, { "npc:Ready yourself, human: the final battle begins... again!" })
        at("sigil2.arena", 2702, 9173, 1)

        -- the squad's would-be kill ([2079]..[2082]): the demon left on 2, the
        -- player holding off, the gnomes' 1s take it to 0 and it stands at 35
        t.ticklog.mark("leg.restore")
        protect()
        t.cheat("::setvar varp172_option_nodef 1") -- auto retaliate off: the knives must not finish it
        t.cheat("::mm_demon_hp 2")
        t.ticks(60)
        t.cheat("::setvar varp172_option_nodef 0")

        -- the kill ([1983]..[2140])
        t.ticklog.mark("leg.fight2")
        protect()
        t.player.attack("mm_demon", 2, 20)
        local _, detail = t.exec("fight2.dead", t.npc.await_dead_engaged, 900, 40, { eat = { item = "shark", below = 40 } })
        t.note("fight2: " .. tostring(detail))
        t.ticks(8)
        t.chat.drain({})
        stage("kill.stage", "varp365_mm_main", 6)

        -- the pages after it ([2144]..[2179])
        t.ticklog.mark("leg.after")
        t.exec("zooknock1", t.player.talk_to, "mm_zooknock_final_battle", 1)
        await_page()
        t.exec("zooknock1-dialog", t.chat.play, { "npc:You should speak to the Sergeant." })
        t.ticks(2)
        t.exec("garkor", t.player.talk_to, "mm_garkor_final_battle", 1)
        await_page()
        t.exec("garkor-dialog", t.chat.play, {
            "npc:Well done, human! That was a most impressive display of skill.",
            "player:Thank you.",
            "npc:You should report to King Narnode immediately. Tell him that the 10th squad still survives and has suffered no casualties.",
            "player:Rest assured, I will do so. How do I leave this place?",
            "npc:Speak to Zooknock. He will arrange for you to leave.",
        })
        stage("garkor.varbit", "varb126_mm_garkor", 7)
        t.ticks(2)
        t.exec("zooknock2", t.player.talk_to, "mm_zooknock_final_battle", 1)
        await_page()
        t.exec("zooknock2-dialog", t.chat.play, { "npc:Well done, human. Bear with me now." })
        t.ticks(8)
        at("zooknock.landing", 2715, 2786, 0)
    end,
}
