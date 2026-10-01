return {
    id = "losttribe",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::complete quest_goblindiplomacy", "::complete quest_runemysteries", "::setlevel mining 17", "::setlevel agility 13", "::setlevel thieving 13", "::give bronze_pickaxe 1", "::give candle_lantern_lit 1" },
    run = function(t)
        local br, bd = t.quest.bind({
            varp = "varb532_lost_tribe_quest",
            constants = { not_started = 0, started = 1, tunnel = 4, book = 5, symbol = 6, generals = 7, mistag = 8, ham_hunt = 9, treaty = 10, complete = 11 },
            row = "quest_losttribe", display = "The Lost Tribe", points = 1,
        })
        t.step("quest.bind", br == "ok" and "PASS" or "FAIL", bd)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        local snap_r, snap = t.skill.snapshot()
        local r, d, x, z, tr, tl

        -- start: Sigmund
        t.exec("goto-talkToSigmund", t.player.goto_tile, 3210, 3221, 1)
        t.exec("talkToSigmund", t.player.talk_to, "lost_tribe_sigmund", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("sigmund.opts", r, d)
        t.exec("sigmund.quests", t.chat.choose, "Do you have any quests for me?")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("sigmund.end", r, d)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- the Cook is the red herring (guide: talkToAllAboutCellar)
        t.exec("goto-talkToAllAboutCellar", t.player.goto_tile, 3209, 3213, 0)
        t.exec("talkToAllAboutCellar", t.player.talk_to, "cook", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("cook.end", r, d)
        t.exec("cook.nothing", t.var.await, "varb537_lost_tribe_contact", 0, 3)

        -- witness: Bob
        t.exec("goto-talkToBob", t.player.goto_tile, 3231, 3204, 0)
        t.exec("talkToBob", t.player.talk_to, "bob", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("bob.end", r, d)
        t.exec("bob.knows", t.var.await, "varb537_lost_tribe_contact", 1, 5)
        t.exec("goto-talkToDuke", t.player.goto_tile, 3210, 3221, 1)
        t.exec("talkToDuke", t.player.talk_to, "duke_of_lumbridge", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("duke.permission", r, d)
        t.exec("duke.permits", t.var.await, "varb537_lost_tribe_contact", 2, 5)

        -- dig the rubble, squeeze through, trip the maze's floor trap, take the brooch
        t.exec("goto-basement", t.player.goto_tile, 3209, 3217, 0)
        t.exec("goDownIntoBasement", t.player.click_loc, "qip_cook_trapdoor_open", 1)
        t.ticks(3)
        t.exec("usePickaxeOnRubble", t.player.use_on, "bronze_pickaxe", t.player.by_symbol("loc", "lost_tribe_cellar_wall"))
        t.exec("rubble.dug", t.var.await, "varb532_lost_tribe_quest", 4, 12)
        t.exec("climbThroughHole", t.player.click_loc, "lost_tribe_cavewall_hole_walldecor", 1)
        t.ticks(3)
        tr, tl = t.world.tile(); x, z = tl.x, tl.z
        t.step("hole.inside", (x and x >= 3220) and "PASS" or "FAIL", tostring(x) .. "," .. tostring(z))
        t.exec("hole.back", t.player.click_loc, "lost_tribe_cavewall_hole_walldecor", 1)
        t.ticks(3)
        tr, tl = t.world.tile(); x, z = tl.x, tl.z
        t.step("hole.outside", (x and x < 3220) and "PASS" or "FAIL", tostring(x) .. "," .. tostring(z))
        t.exec("climbThroughHole2", t.player.click_loc, "lost_tribe_cavewall_hole_walldecor", 1)
        t.ticks(3)
        local first = { {3222,9618},{3224,9618},{3229,9610} }
        for i, p in ipairs(first) do t.player.walk_to(p[1], p[2], 12) end
        t.exec("grabBrooch", t.player.click_obj, "lost_tribe_brooch", 3)
        t.exec("brooch.have", t.inv.await, "lost_tribe_brooch", 1, 6)
        -- the maze: a floor trap drops the player into the swamp caves and puts the light out
        t.exec("trap.step", t.player.goto_tile, 3238, 9622, 0)
        t.ticks(4)
        tr, tl = t.world.tile(); x, z = tl.x, tl.z
        t.step("trap.fell", (z and z < 9600) and "PASS" or "FAIL", tostring(x) .. "," .. tostring(z))
        t.exec("trap.light.out", t.inv.await, "candle_lantern_unlit", 1, 3)
        t.cheat("::give candle_lantern_lit 1")
        t.exec("relight", t.inv.await, "candle_lantern_lit", 1, 5)
        t.exec("goto-cellar-back", t.player.goto_tile, 3218, 9618, 0)

        -- showBroochToDuke
        t.exec("goto-showBroochToDuke", t.player.goto_tile, 3210, 3221, 1)
        t.exec("showBroochToDuke", t.player.talk_to, "duke_of_lumbridge", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("brooch.opts", r, d)
        t.exec("brooch.rubble", t.chat.choose, "I dug through the rubble...")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("brooch.end", r, d)
        t.exec("brooch.contact", t.var.await, "varb537_lost_tribe_contact", 3, 5)
        t.expect("quest.stage.book", t.quest.expect_stage("book"))

        -- Reldo hints at the book; the bookcase; read every spread
        t.exec("goto-talkToReldo", t.player.goto_tile, 3210, 3494, 0)
        t.exec("talkToReldo", t.player.talk_to, "reldo", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("reldo.opts", r, d)
        t.exec("reldo.brooch", t.chat.choose, "What can you tell me about this brooch?")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("reldo.end", r, d)
        t.exec("goto-searchBookcase", t.player.goto_tile, 3208, 3496, 0)
        t.exec("searchBookcase", t.player.click_loc, "lost_tribe_bookcase", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("bookcase.chat", r, d)
        t.exec("book.have", t.inv.await, "lost_tribe_book", 1, 5)
        t.exec("readBook", t.player.inv_op, "lost_tribe_book", 1)
        t.exec("book.open", t.ui.await_open, "lost_tribe_symbol_book", 10)
        t.ticks(2)
        local wr, w = t.ui.widget("lost_tribe_symbol_book:lost_tribe_right_arrow")
        t.step("book.arrow", wr == "ok" and "PASS" or "FAIL", tostring(wr) .. " " .. tostring(w))
        t.exec("book.page0", t.ui.expect_text, "lost_tribe_symbol_book:com_32", "History of the Goblin Race")
        local comps = { "com_34", "com_39", "com_43", "com_23", "com_28" }
        local wants = { "war-god", "command", "twelve distinct", "Dorgeshuun", "Rekeshuun" }
        for i = 1, 5 do
            t.ui.invoke(w, 1)
            t.ticks(2)
            t.exec("book.page" .. i, t.ui.expect_text, "lost_tribe_symbol_book:" .. comps[i], wants[i])
            if i < 5 then t.expect("book.unfinished" .. i, t.quest.expect_stage("book")) end
        end
        t.ticks(3)
        r, d = t.chat.drain({ stop_at = "none" })
        t.ticks(3)
        t.exec("readBook.done", t.var.await, "varb532_lost_tribe_quest", 6, 8)
        t.key("escape")
        t.ticks(2)

        -- talkToGenerals
        t.exec("goto-generals-gate", t.player.goto_tile, 2957, 3507, 0)
        t.exec("generals.door", t.player.click_loc, "goblin_outpost_poordoor_double_inner", 1)
        t.ticks(3)
        t.exec("goto-generals", t.player.goto_tile, 2957, 3510, 0)
        t.ticks(2)
        t.exec("talkToGenerals", t.player.talk_to, "general_wartface_green", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("generals.opts", r, d)
        t.exec("generals.matter", t.chat.choose, "It doesn't really matter.")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("generals.end", r, d)
        t.expect("quest.stage.generals", t.quest.expect_stage("generals"))

        -- the marked path to Mistag
        t.exec("goto-cellar", t.player.goto_tile, 3218, 9618, 0)
        t.exec("enterTunnels", t.player.click_loc, "lost_tribe_cavewall_hole_walldecor", 1)
        t.ticks(3)
        local line = { {3222,9618},{3224,9618},{3229,9610},{3238,9610},{3241,9612},{3246,9612},{3249,9619},{3254,9625},{3252,9631},{3234,9631},{3230,9634},{3230,9643},{3237,9648},{3244,9648},{3246,9645},{3240,9641},{3244,9637},{3252,9642},{3252,9646},{3257,9656},{3269,9656},{3277,9652},{3277,9647},{3267,9643},{3267,9638},{3276,9637},{3290,9645},{3300,9641},{3307,9631},{3297,9627},{3295,9622},{3290,9617},{3297,9606},{3303,9606},{3309,9612},{3317,9612} }
        for i, p in ipairs(line) do t.player.walk_to(p[1], p[2], 12) end
        tr, tl = t.world.tile(); x, z = tl.x, tl.z
        t.step("walkToMistag", (x and x >= 3309) and "PASS" or "FAIL", tostring(x) .. "," .. tostring(z))
        t.player.emote("Goblin Bow")
        t.ticks(2)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("emoteAtMistag.chat", r, d)
        t.exec("emoteAtMistag", t.var.await, "varb532_lost_tribe_quest", 8, 6)

        -- Mistag shows the way out; the Duke
        t.exec("mistag.talk", t.player.talk_to, "lost_tribe_mistag", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("mistag.opts", r, d)
        t.exec("mistag.out", t.chat.choose, "Can you show me the way out of the mine?")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("mistag.out.end", r, d)
        t.ticks(4)
        tr, tl = t.world.tile(); x, z = tl.x, tl.z
        t.step("mistag.landed", (x and x < 3240) and "PASS" or "FAIL", tostring(x) .. "," .. tostring(z))
        t.exec("goto-goTalkToDukeAfterEmote", t.player.goto_tile, 3210, 3221, 1)
        t.exec("goTalkToDukeAfterEmote", t.player.talk_to, "duke_of_lumbridge", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("contact.opts", r, d)
        t.exec("contact.made", t.chat.choose, "I've made contact with the cave goblins...")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("contact.end", r, d)
        t.exec("contact.stage", t.var.await, "varb532_lost_tribe_quest", 9, 6)

        -- Sigmund's key, the chest, the HAM hideout crate
        t.exec("pickpocketSigmund", t.player.press, "lost_tribe_sigmund", 3, 8)
        t.exec("key.have", t.inv.await, "lost_tribe_chest_key", 1, 8)
        t.exec("goto-unlockChest", t.player.goto_tile, 3209, 3216, 1)
        t.exec("unlockChest", t.player.click_loc, "lost_tribe_chest", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("chest.chat", r, d)
        t.exec("robes.have", t.inv.await, "ham_robe", 1, 6)
        t.exec("robes.state", t.var.await, "varb534_lost_tribe_ham", 1, 4)
        t.exec("goto-ham-trapdoor", t.player.goto_tile, 3166, 3253, 0)
        t.exec("ham.picklock", t.player.click_loc, "osf_trapdoor_closed", 5)
        t.ticks(4)
        r, d = t.player.click_loc("osf_trapdoor_open", 1)
        t.step("enterHamLair.click", (r == "refused") and "PASS" or "FAIL", tostring(r) .. " -- " .. tostring(d))
        t.ticks(2)
        t.blocked("content_bug: enterHamLair -- the HAM trapdoor Climb-down (losttribe_ham.rs2:54 [oploc1,osf_trapdoor_open] -> ~climb_ladder(-1)) answers 'You can't go any further.' after the Pick-Lock: ~climb -> ~maplink_try finds no maplink row for src 0_49_50_30_52 (ladders_stairs/configs/maplink.dbrow has no down row for the HAM lair, dest 0_49_150_*), so the +/-1 plane default at level 0 is blocked (ladders.rs2:69-78). The lair is unreachable without a goto_tile cheat past the guide's trapdoor step.")
        return
    end,
}
