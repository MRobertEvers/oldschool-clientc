-- Tribal Totem: Kangai Mau (Brimhaven) -> GPDT depot label swap -> Wizard Cromperty teleport ->
-- KURT combination door -> trapped stairs -> chest -> hand-in. Rows named after the guide steps
-- (tools/quest_gate/ladder.py totem). Adapted from the parity3c scratch driver.
return {
    id = "totem",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel thieving 30",
    },

    run = function(t)
        local function stage(want, label)
            local r, v = t.var.server("totemquest")
            t.check(label, r == "ok" and v == want, "%totemquest = " .. tostring(v) .. ", want " .. want)
        end
        local function bits(want_bit0, want_bit1, label)
            local r, v = t.var.server("handelmort_traps_disabled")
            local b0 = (v or 0) % 2
            local b1 = math.floor((v or 0) / 2) % 2
            t.check(label, r == "ok" and b0 == want_bit0 and b1 == want_bit1,
                "handelmort_traps_disabled = " .. tostring(v) .. " (bit0 " .. b0 .. ", bit1 " .. b1 .. ")")
        end
        local names = { "a", "b", "c", "d" }

        t.quest.bind({
            varp = "totemquest",
            constants = { not_started = 0, started = 1, crate_marked = 2, crate_delivered = 3, teleported = 4, complete = 5 },
            row = "quest_tribaltotem",
            display = "Tribal Totem",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- 1.1 talkToKangaiMau
        t.exec("goto-talkToKangaiMau", t.player.goto_tile, 2791, 3182, 0)
        t.exec("talkToKangaiMau", t.player.talk_to, "kangai_mau", 1)
        t.exec("talkToKangaiMau-dialog", t.chat.play, {
            "npc:Hello. I Kangai Mau",
            "choose:I'm in search of adventure!",
            "player:I'm in search of adventure!",
            "npc:Adventure is something",
            "npc:I need someone to go on a mission",
            "npc:We need it back.",
            "choose:Ok, I will get it back.",
            "player:Ok, I will get it back.",
            "npc:Best of luck",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- 1.2 investigateCrate
        t.exec("goto-investigateCrate", t.player.goto_tile, 2649, 3271, 0)
        t.exec("investigateCrate", t.player.click_loc, "horncrate", 2)
        t.exec("investigateCrate.text", t.chat.expect_text, "There is a label on this crate")
        t.exec("investigateCrate.drain", t.chat.drain, {})
        t.exec("investigateCrate.label", t.inv.await, "tribal_totem_label", 1, 6)

        -- 1.3 useLabel
        local crate = t.player.by_symbol("loc", "teleportcrate")
        t.exec("useLabel", t.player.use_on, "tribal_totem_label", crate)
        t.exec("useLabel.drain", t.chat.drain, {})
        t.ticks(2)
        stage(2, "quest.stage.crate_marked")
        t.exec("useLabel.consumed", t.inv.expect_absent, "tribal_totem_label")

        -- 1.4 talkToEmployee
        t.exec("talkToEmployee", t.player.talk_to, "rpdt_employee", 1)
        t.exec("talkToEmployee-dialog", t.chat.play, {
            "npc:Welcome to RPDT!",
            "options",
            "choose:So, when are you going to deliver this crate?",
            "player:So, when are you going to deliver this crate?",
            "npc:Well... I guess we could do it now...",
        })
        stage(3, "quest.stage.crate_delivered")

        -- 1.5 talkToCromperty
        t.exec("goto-talkToCromperty", t.player.goto_tile, 2683, 3324, 0)
        t.exec("talkToCromperty", t.player.talk_to, "ardounge_wizard", 1)
        t.exec("talkToCromperty.d1", t.chat.drain, { stop_at = "options" })
        t.exec("talkToCromperty.c1", t.chat.choose, "So what have you invented?")
        t.exec("talkToCromperty.d2", t.chat.drain, { stop_at = "options" })
        t.exec("talkToCromperty.c2", t.chat.choose, "Can I be teleported please?")
        t.exec("talkToCromperty.d3", t.chat.drain, { stop_at = "options" })
        t.exec("talkToCromperty.c3", t.chat.choose, "Yes, that sounds good. Teleport me!")
        t.exec("talkToCromperty.d4", t.chat.drain, {})
        t.ticks(8)
        local tr, tile = t.world.tile()
        t.check("talkToCromperty.landed", tr == "ok" and tile.x == 2638 and tile.z == 3321 and tile.level == 0,
            "tile " .. tostring(tile and (tile.x .. "," .. tile.z .. "," .. tile.level)))
        stage(4, "quest.stage.teleported")

        -- 1.6 enterPassword / 1.7 solvePassword (KURT: K=10 right, U=6 left, R=9 left, T=7 left)
        t.exec("goto-enterPassword", t.player.goto_tile, 2633, 3323, 0)
        t.exec("enterPassword", t.player.click_loc, "combodoor", 1)
        t.exec("enterPassword.await", t.ui.await_open, "tribal_door")
        for i = 1, 4 do
            t.exec("enterPassword.start." .. names[i], t.ui.expect_text, "tribal_door:tribal" .. names[i], "A")
        end
        local _, enter = t.ui.widget("tribal_door:tribalenter")
        t.ui.invoke(enter, 1)
        t.exec("solvePassword.wrong.msg", t.msg.await, "This combination is incorrect.")
        bits(0, 0, "solvePassword.wrong.bit0_clear")
        t.exec("solvePassword.reopen", t.player.click_loc, "combodoor", 1)
        t.exec("solvePassword.reopen.await", t.ui.await_open, "tribal_door")
        for i = 1, 4 do
            local target = string.byte("KURT", i) - string.byte("A")
            local _, w = t.ui.widget("tribal_door:tribal" .. names[i] .. "_right")
            for _ = 1, target do t.ui.invoke(w, 1); t.ticks(1) end
        end
        t.exec("solvePassword.k", t.ui.expect_text, "tribal_door:tribala", "K")
        t.exec("solvePassword.u", t.ui.expect_text, "tribal_door:tribalb", "U")
        t.exec("solvePassword.r", t.ui.expect_text, "tribal_door:tribalc", "R")
        t.exec("solvePassword.t", t.ui.expect_text, "tribal_door:tribald", "T")
        local _, enter2 = t.ui.widget("tribal_door:tribalenter")
        t.ui.invoke(enter2, 1)
        t.exec("solvePassword.right.msg", t.msg.await, "The combination seems correct!")
        bits(1, 0, "solvePassword.right.bit0_set")
        t.exec("solvePassword.walk", t.player.click_loc, "combodoor", 1)
        t.ticks(4)
        local _, kt = t.world.tile()
        t.check("solvePassword.inside", kt and kt.x >= 2634 and kt.level == 0, "tile " .. tostring(kt and (kt.x .. "," .. kt.z)))

        -- 1.8 climbStairs: the trap first, then investigate (op 2), then climb
        t.exec("goto-climbStairs", t.player.goto_tile, 2631, 3325, 0)
        t.exec("climbStairs.trap", t.player.click_loc, "totemtrapstairs", 1)
        t.exec("climbStairs.trap.click", t.msg.await, "you hear a click")
        t.exec("climbStairs.trap.fall", t.msg.await, "You have fallen through a trap!")
        t.ticks(6)
        local _, ft = t.world.tile()
        t.check("climbStairs.trap.landed", ft and ft.x == 2640 and ft.z == 9719 and ft.level == 0,
            "tile " .. tostring(ft and (ft.x .. "," .. ft.z .. "," .. ft.level)))
        t.cheat("::goto 2635 3320")
        t.ticks(3)
        t.exec("goto-climbStairs2", t.player.goto_tile, 2631, 3325, 0)
        t.exec("climbStairs.investigate", t.player.click_loc, "totemtrapstairs", 2)
        t.exec("climbStairs.investigate.text", t.chat.expect_text, "Your trained senses as a thief")
        t.exec("climbStairs.investigate.drain", t.chat.drain, {})
        bits(1, 1, "climbStairs.bit1_set")
        t.exec("climbStairs.climb", t.player.click_loc, "totemtrapstairs", 1)
        t.exec("climbStairs.climb.msg", t.msg.await, "You climb up the stairs.")
        t.ticks(3)
        local _, ut = t.world.tile()
        t.check("climbStairs.landed", ut and ut.x == 2631 and ut.z == 3321 and ut.level == 1,
            "tile " .. tostring(ut and (ut.x .. "," .. ut.z .. "," .. ut.level)))

        -- 1.9 searchChest
        t.exec("goto-searchChest", t.player.goto_tile, 2638, 3323, 1)
        t.exec("searchChest.open", t.player.click_loc, "totemshutchest", 1)
        t.exec("searchChest.open.msg", t.msg.expect, "You open the chest.")
        t.exec("searchChest", t.player.click_loc, "totemopenchest", 1)
        t.exec("searchChest.text", t.chat.expect_text, "Inside the chest you find the tribal totem.")
        t.exec("searchChest.drain", t.chat.drain, {})
        t.exec("searchChest.totem", t.inv.await, "tribal_totem", 1, 6)

        -- 1.10 talkToKangaiMauAgain
        t.cheat("::goto 2791 3180")
        t.ticks(10)
        local ktr, kt2 = t.world.tile()
        t.check("goto-talkToKangaiMauAgain", ktr == "ok" and kt2 and kt2.x == 2791, "at " .. tostring(kt2 and (kt2.x .. "," .. kt2.z)))
        local _, snap = t.skill.snapshot()
        t.exec("talkToKangaiMauAgain", t.player.talk_to, "kangai_mau", 1)
        t.exec("talkToKangaiMauAgain-dialog", t.chat.drain, {})
        t.ticks(2)
        t.exec("reward.totem_gone", t.inv.expect_absent, "tribal_totem")
        t.exec("reward.swordfish", t.inv.await, "swordfish", 5, 6)
        t.exec("reward.thieving_xp", t.skill.expect_gain, "thieving", 1775, snap)
        stage(5, "quest.stage.complete")
        t.quest.expect_complete()
        t.finish(0)
    end,
}
