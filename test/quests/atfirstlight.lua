-- At First Light. Guide: Quest Helper atfirstlight; content: quest_atfirstlight/scripts/atfirstlight.rs2.
-- Prerequisites staged by setup: Children of the Sun + Eagles' Peak (::complete), Hunter 46,
-- Herblore 30, Construction 27. Coins are brought for the box trap (Imia, 41gp each).
-- Everything else is obtained in-quest: toy mouse (Wolf), leaves (bushes), tails (box traps),
-- hammer and needle (ground), fur/report by dialogue.

return {
    id = "atfirstlight",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::atfirstlight",
        "::setlevel hunter 46",
        "::setlevel herblore 30",
        "::setlevel construction 27",
        "::complete quest_childrenofthesun",
        "::complete quest_eaglespeak",
        "::give coins 200",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb9835_afl",
            constants = {
                not_started = 0, verity = 1, wolf = 2, cat = 3, fox = 4, poultice = 5,
                fox2 = 6, atza = 7, repair = 8, trim = 9, report = 10, finish = 11,
                complete = 12,
            },
            row = "quest_atfirstlight",
            display = "At First Light",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- 1.1 talkToApatura
        t.exec("goto-talkToApatura", t.player.goto_tile, 1555, 3033, 0)
        t.exec("talkToApatura", t.player.talk_to, "hg_apatura", 1)
        t.exec("talkToApatura-dialog", t.chat.play, {
            "player:Can I help you with anything?",
            "npc:I'm not sure if you have the time",
            "choose:Yes.",
            "player:Great! Point me in her direction",
            "npc:Speak to Verity in the guild hall below",
        })
        t.expect("quest.stage.verity", t.quest.expect_stage("verity"))

        -- 1.2 goDownTree
        t.exec("goto-goDownTree", t.player.goto_tile, 1557, 3046, 0)
        t.exec("goDownTree", t.player.click_loc, "hunterguild_stairs_down01_combined", 1)
        t.ticks(4)
        local _, below = t.world.tile()
        t.check("goDownTree-landed", below and below.z > 9000, "burrow tile " .. tostring(below and below.x) .. "," .. tostring(below and below.z))

        -- 1.3 talkToVerity
        t.exec("goto-talkToVerity", t.player.goto_tile, 1559, 9462, 0)
        local vr, vrow = t.npc.nearest("hg_verity", 10)
        t.check("talkToVerity-where", vr == "ok", "verity row " .. tostring(vr) .. " x=" .. tostring(vrow and vrow.x) .. " z=" .. tostring(vrow and vrow.z))
        local tv_result, tv_detail = t.player.talk_to("hg_verity", 1)
        do
            t.blocked("content_bug: talkToVerity -- Verity stands at 1559,9464 behind the bar counter (floor-blocked tiles z 9463, x 1556-1560, flap hg_table_tavern02_door01 has no op); talk_to from 1559,9462 straight across answers " .. tostring(tv_result) .. " '" .. tostring(tv_detail) .. "' and from 1561,9463 and 1559,9461 the same: every tile adjacent to her is unreachable, so the only landing is the sealed pocket behind the bar (a goto past the counter, rejected). The port lacks the across-the-counter reach OSRS has; all later legs unverified.")
            return
        end
        t.exec("talkToVerity-dialog", t.chat.play, {
            "player:Apatura sent me",
            "npc:Yes. Fox's report on his trapping trip vanished",
        })
        t.expect("quest.stage.wolf", t.quest.expect_stage("wolf"))

        -- 1.4 talkToWolf
        t.exec("goto-talkToWolf", t.player.goto_tile, 1554, 9459, 0)
        t.exec("talkToWolf", t.player.talk_to, "hg_wolf", 1)
        t.exec("talkToWolf-dialog", t.chat.play, {
            "player:Verity says you were minding the hall",
            "npc:Aye. Kiko, that cat",
            "npc:Take this toy mouse",
        })
        t.expect("quest.stage.cat", t.quest.expect_stage("cat"))
        t.inv.await("poh_toy_mouse_unwound", 1, 5)

        -- 1.5 windUpToy
        t.exec("windUpToy", t.player.inv_op, "poh_toy_mouse_unwound", 1)
        local wound_result, wound_total = t.inv.await("poh_toy_mouse_wound", 1, 5)
        t.check("windUpToy-wound", wound_result, "wound toy mice in backpack: " .. tostring(wound_total))

        -- 1.6 useToyOnKiko
        local kiko = t.player.by_symbol("npc", "afl_kiko")
        t.exec("useToyOnKiko", t.player.use_on, "poh_toy_mouse_wound", kiko)
        t.var.await("varb9839_afl_catdistract", 1, 8)
        t.expect("useToyOnKiko-distracted", t.var.expect("varb9839_afl_catdistract", 1))

        -- 1.7 checkBed
        t.exec("goto-checkBed", t.player.goto_tile, 1552, 9459, 0)
        t.exec("checkBed", t.player.click_loc, "afl_catbed_op", 1)
        t.var.await("varb9837_afl_bedcheck", 1, 8)
        t.expect("checkBed-bedcheck", t.var.expect("varb9837_afl_bedcheck", 1))

        -- 1.8 returnToWolf
        t.exec("goto-returnToWolf", t.player.goto_tile, 1554, 9459, 0)
        t.exec("returnToWolf", t.player.talk_to, "hg_wolf", 1)
        t.exec("returnToWolf-dialog", t.chat.play, {
            "player:I checked Kiko's bed",
            "npc:Cut fur? That sounds like Fox",
        })
        t.expect("quest.stage.fox", t.quest.expect_stage("fox"))

        -- 1.9 goUpTree
        t.exec("goto-goUpTree", t.player.goto_tile, 1557, 9451, 0)
        t.exec("goUpTree", t.player.click_loc, "hunterguild_stairs_up01", 1)
        t.ticks(4)
        local _, above = t.world.tile()
        t.check("goUpTree-landed", above and above.z < 4000, "surface tile " .. tostring(above and above.x) .. "," .. tostring(above and above.z))

        -- 1.10 buyBoxTrap (Imia, two traps so both jerboas can be caught together)
        t.exec("goto-buyBoxTrap", t.player.goto_tile, 1562, 3058, 0)
        t.exec("buyBoxTrap-open", t.shop.open, "hg_mixedhide_seller", 3, "imia_supplies")
        t.exec("buyBoxTrap", t.shop.buy, "hunting_box_trap", 2)
        local close_result, close_detail = t.shop.close()
        t.check("buyBoxTrap-close", close_result == "ok", "shop.close -> " .. tostring(close_result) .. " " .. tostring(close_detail))
        local trap_result, trap_total = t.inv.await("hunting_box_trap", 2, 5)
        t.check("buyBoxTrap-have", trap_result, "box traps in backpack: " .. tostring(trap_total))

        -- 1.11 talkToFox
        t.exec("goto-talkToFox", t.player.goto_tile, 1623, 2980, 0)
        t.exec("talkToFox", t.player.talk_to, "afl_hunter_fox_multi", 1)
        t.exec("talkToFox-dialog", t.chat.play, {
            "player:Wolf says you might know",
            "npc:Ugh... I took a nasty fall",
            "npc:I tore my report to scraps",
            "npc:Pick a smooth leaf",
        })
        t.expect("quest.stage.poultice", t.quest.expect_stage("poultice"))

        -- 1.12 takeLeaf
        t.exec("goto-takeLeaf", t.player.goto_tile, 1617, 2979, 0)
        t.exec("takeLeaf", t.player.click_loc, "afl_bush1", 1)
        local leaf1_result, leaf1_total = t.inv.await("afl_leaf1", 1, 8)
        t.check("takeLeaf-have", leaf1_result, "smooth leaves: " .. tostring(leaf1_total))

        -- 1.13 takeSecondLeaf
        t.exec("goto-takeSecondLeaf", t.player.goto_tile, 1673, 2991, 0)
        t.exec("takeSecondLeaf", t.player.click_loc, "afl_bush2", 1)
        local leaf2_result, leaf2_total = t.inv.await("afl_leaf2", 1, 8)
        t.check("takeSecondLeaf-have", leaf2_result, "sticky leaves: " .. tostring(leaf2_total))

        -- 1.14 catchJerboa: lay both traps by the oasis, collect the catches (random rolls retry)
        t.exec("goto-catchJerboa", t.player.goto_tile, 1664, 3000, 0)
        local tails = 0
        for round = 1, 14 do
            local _, tail_total = t.inv.count("hunting_jerboa_tail")
            tails = tail_total or 0
            if tails >= 2 then break end
            local _, held = t.inv.count("hunting_box_trap")
            local el, _ = t.world.loc_near("hunting_boxtrap_empty", 14)
            local fl, _ = t.world.loc_near("hunting_boxtrap_full_jerboa", 14)
            local bl, _ = t.world.loc_near("hunting_boxtrap_failed", 14)
            if (held or 0) >= 1 and el ~= "ok" and fl ~= "ok" and bl ~= "ok" then
                t.exec("catchJerboa-goto-at" .. round, t.player.goto_tile, 1664, 3002, 0)
                local lay_result, lay_detail = t.player.inv_op("hunting_box_trap", 1)
                t.ticks(6)
                local _, held_after = t.inv.count("hunting_box_trap")
                local laid, _ = t.world.loc_near("hunting_boxtrap_empty", 6)
                t.check("catchJerboa-lay" .. round, lay_result == "ok" and (held_after or 0) < (held or 0) and laid == "ok", "lay -> " .. tostring(lay_result) .. " " .. tostring(lay_detail) .. "; box traps held " .. tostring(held) .. " -> " .. tostring(held_after) .. "; trap loc " .. tostring(laid))
            end
            t.ticks(40)
            local fok = t.world.loc_near("hunting_boxtrap_full_jerboa", 14)
            if fok == "ok" then
                t.exec("catchJerboa-take" .. round, t.player.click_loc, "hunting_boxtrap_full_jerboa", 1)
                t.ticks(3)
            end
            local bok = t.world.loc_near("hunting_boxtrap_failed", 14)
            if bok == "ok" then
                t.exec("catchJerboa-redo" .. round, t.player.click_loc, "hunting_boxtrap_failed", 1)
                t.ticks(3)
            end
        end
        local _, tail_final = t.inv.count("hunting_jerboa_tail")
        t.check("catchJerboa-tails", (tail_final or 0) >= 2, "jerboa tails: " .. tostring(tail_final))

        -- 1.15 useTailOnLeaves
        t.exec("useTailOnLeaves", t.player.use_item_on_item, "hunting_jerboa_tail", "afl_leaf1")
        local pout_result, pout_total = t.inv.await("afl_poultice", 1, 8)
        t.check("useTailOnLeaves-poultice", pout_result, "poultices: " .. tostring(pout_total))

        -- 1.16 returnToFox
        t.exec("goto-returnToFox", t.player.goto_tile, 1623, 2980, 0)
        t.exec("returnToFox", t.player.talk_to, "afl_hunter_fox_multi", 1)
        t.exec("returnToFox-dialog", t.chat.play, {
            "player:I've made the poultice",
            "npc:Ahh, that is much better",
        })
        t.expect("quest.stage.fox2", t.quest.expect_stage("fox2"))

        -- 1.17 talkToFoxAfterPoultice
        t.exec("talkToFoxAfterPoultice", t.player.talk_to, "afl_hunter_fox_multi", 1)
        t.exec("talkToFoxAfterPoultice-dialog", t.chat.play, {
            "npc:I can stand again",
            "npc:Atza, outside the city's south wall",
        })
        t.expect("quest.stage.atza", t.quest.expect_stage("atza"))
        local fur_result, fur_total = t.inv.await("afl_fur", 1, 5)
        t.check("talkToFoxAfterPoultice-fur", fur_result, "fur samples: " .. tostring(fur_total))

        -- 1.18/1.19 talkToAtza (fur in hand)
        t.exec("goto-talkToAtza", t.player.goto_tile, 1698, 3066, 0)
        t.exec("talkToAtza-door", t.player.click_loc, "fortis_door_l", 1, { at = { 1698, 3064 } })
        t.exec("talkToAtza", t.player.talk_to, "afl_atza", 1)
        t.exec("talkToAtza-dialog", t.chat.play, {
            "player:Fox sent me with a fur sample",
            "npc:I can, but my tools are scattered",
            "npc:If you can set it up with a hammer",
        })
        t.expect("quest.stage.repair", t.quest.expect_stage("repair"))

        -- 1.20 takeHammer
        t.player.walk_to(1698, 3066, 12)
        local _, out1 = t.world.tile()
        t.check("takeHammer-leaveAtza", out1 and out1.z >= 3065, "walked out through Atza's door to " .. tostring(out1 and out1.x) .. "," .. tostring(out1 and out1.z))
        t.exec("goto-takeHammerOutside", t.player.goto_tile, 1694, 3068, 0)
        t.exec("takeHammer-door", t.player.click_loc, "fortis_door_l", 1, { at = { 1695, 3068 } })
        t.exec("takeHammer", t.player.click_obj, "hammer")
        local hammer_result, hammer_total = t.inv.await("hammer", 1, 8)
        t.check("takeHammer-have", hammer_result, "hammers: " .. tostring(hammer_total))

        -- 1.21 makeEquipmentPile
        t.player.walk_to(1694, 3068, 12)
        local _, out2 = t.world.tile()
        t.check("makeEquipmentPile-leaveHammerHouse", out2 and out2.x <= 1694, "walked out through the hammer house door to " .. tostring(out2 and out2.x) .. "," .. tostring(out2 and out2.z))
        t.exec("goto-makeEquipmentPile", t.player.goto_tile, 1698, 3066, 0)
        t.exec("makeEquipmentPile-door", t.player.click_loc, "fortis_door_l", 1, { at = { 1698, 3064 } })
        t.exec("makeEquipmentPile", t.player.click_loc, "afl_housetrap_multi", 1)
        t.var.await("varb9840_afl_housetrapped", 2, 10)
        t.expect("makeEquipmentPile-set", t.var.expect("varb9840_afl_housetrapped", 2))

        -- 1.22 talkToAtzaForTrim
        t.exec("talkToAtzaForTrim", t.player.talk_to, "afl_atza", 1)
        t.exec("talkToAtzaForTrim-dialog", t.chat.play, {
            "player:The equipment is set up",
            "npc:Perfect. Here is your trimmed fur",
        })
        t.expect("quest.stage.trim", t.quest.expect_stage("trim"))

        -- 1.23/1.24 returnToFoxAfterTrim
        t.exec("goto-returnToFoxAfterTrim", t.player.goto_tile, 1623, 2980, 0)
        t.exec("returnToFoxAfterTrim", t.player.talk_to, "afl_hunter_fox_multi", 1)
        t.exec("returnToFoxAfterTrim-dialog", t.chat.play, {
            "player:Atza has trimmed the fur",
            "npc:Fine work. Keep it",
        })
        t.expect("quest.stage.report", t.quest.expect_stage("report"))

        -- 1.25 takeNeedle
        t.exec("goto-takeNeedle", t.player.goto_tile, 1566, 3034, 0)
        t.exec("takeNeedle", t.player.click_obj, "needle")
        local needle_result, needle_total = t.inv.await("needle", 1, 8)
        t.check("takeNeedle-have", needle_result, "needles: " .. tostring(needle_total))

        -- 1.26 goDownTreeEnd
        t.exec("goto-goDownTreeEnd", t.player.goto_tile, 1557, 3046, 0)
        t.exec("goDownTreeEnd", t.player.click_loc, "hunterguild_stairs_down01_combined", 1)
        t.ticks(4)

        -- 1.27 talkToVerityEnd
        t.exec("goto-talkToVerityEnd", t.player.goto_tile, 1559, 9462, 0)
        t.exec("talkToVerityEnd", t.player.talk_to, "hg_verity", 1)
        t.exec("talkToVerityEnd-dialog", t.chat.play, {
            "player:Fox gave me this report",
            "npc:So it was Fox's all along",
            "npc:Kiko's bed needs mending",
        })
        t.expect("talkToVerityEnd-report", t.var.expect("varb9836_afl_report", 1))

        -- 1.28 useJerboaTailOnBed
        t.exec("goto-useJerboaTailOnBed", t.player.goto_tile, 1552, 9459, 0)
        local bed = t.player.by_symbol("loc", "afl_catbed_op")
        t.exec("useJerboaTailOnBed", t.player.use_on, "hunting_jerboa_tail", bed)
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))

        -- 1.29/1.30 goUpTreeToFinishQuest, talkToApaturaToFinishQuest
        t.exec("goto-goUpTreeToFinishQuest", t.player.goto_tile, 1557, 9451, 0)
        t.exec("goUpTreeToFinishQuest", t.player.click_loc, "hunterguild_stairs_up01", 1)
        t.ticks(4)
        t.exec("goto-talkToApaturaToFinishQuest", t.player.goto_tile, 1555, 3033, 0)

        local _, reward_before = t.skill.snapshot()
        t.exec("talkToApaturaToFinishQuest", t.player.talk_to, "hg_apatura", 1)
        t.exec("talkToApaturaToFinishQuest-dialog", t.chat.play, {
            "player:Everything's sorted downstairs",
            "npc:Excellent work",
        })
        t.quest.expect_complete()
        t.check("reward.hunter", t.skill.expect_gain("hunter", 4500, reward_before), "4500 Hunter XP")
        t.check("reward.construction", t.skill.expect_gain("construction", 800, reward_before), "800 Construction XP")
        t.check("reward.herblore", t.skill.expect_gain("herblore", 500, reward_before), "500 Herblore XP")

        t.finish(0)
    end,
}
