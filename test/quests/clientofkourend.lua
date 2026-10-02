-- Client of Kourend: Veos (Piscarilius docks) sends you to five general stores
-- for house interviews, then an orb to the Dark Altar. Source of truth for the
-- dialogue: quest_clientofkourend/scripts/clientofkourend.rs2.
-- Veos stands at 1825,3691; 1825,3694 is a ship deck, so stand at 1824,3689.
return {
    id = "clientofkourend",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_xmarksthespot", -- prerequisite; also shows Veos on the docks
        "::clientofkourend",
        "::give feather 1", -- the guide brings a feather along
    },

    run = function(t)
        t.quest.bind({
            varp = "varb5619_veos_progress",
            constants = { not_started = 0, houses = 1, returnv = 2, altar = 4, finish = 5, complete = 7 },
            row = "quest_clientofkourend",
            display = "Client of Kourend",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("goto-talkToVeos", t.player.goto_tile, 1824, 3689, 0)
        t.exec("talkToVeos", t.player.talk_to, "veos_vis_amulet", 1)
        t.exec("talkToVeos-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToVeos-quests", t.chat.choose, "Have you got any quests for me?")
        t.exec("talkToVeos-offer", t.chat.drain, { stop_at = "options" })
        t.exec("talkToVeos-yes", t.chat.choose, "Yes.")
        t.exec("talkToVeos-accepted", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.houses", t.quest.expect_stage("houses"))
        local scroll_ok, scroll_n = t.inv.count("veos_scroll")
        t.check("talkToVeos.scroll", scroll_ok == "ok" and scroll_n == 1, "veos_scroll count " .. tostring(scroll_n))

        t.exec("useFeatherOnScroll", t.player.use_item_on_item, "feather", "veos_scroll")
        t.exec("useFeatherOnScroll-closed", t.chat.drain, {})
        t.inv.await("veos_quill", 1, 10)
        local quill_ok, quill_n = t.inv.count("veos_quill")
        t.check("useFeatherOnScroll.quill", quill_ok == "ok" and quill_n == 1, "veos_quill count " .. tostring(quill_n))

        t.exec("goto-talkToLeenz", t.player.goto_tile, 1807, 3725, 0)
        t.exec("talkToLeenz", t.player.talk_to, "piscarilius_generalstore_keeper", 1)
        t.exec("talkToLeenz-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToLeenz-ask", t.chat.choose, "Can I ask you about Port Piscarilius?")
        t.exec("talkToLeenz-topics", t.chat.drain, { stop_at = "options" })
        t.exec("talkToLeenz-topic1", t.chat.choose, 1)
        t.exec("talkToLeenz-topics2", t.chat.drain, { stop_at = "options" })
        t.exec("talkToLeenz-topic2", t.chat.choose, 2)
        t.exec("talkToLeenz-closed", t.chat.drain, {})
        t.ticks(2)
        t.expect("talkToLeenz.recorded", t.var.expect("varb5620_veos_piscarilius", 1))

        t.exec("goto-talkToRegath", t.player.goto_tile, 1720, 3726, 0)
        t.exec("talkToRegath", t.player.talk_to, "arceuus_generalstore", 1)
        t.exec("talkToRegath-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToRegath-ask", t.chat.choose, "Can I ask you about Arceuus?")
        t.exec("talkToRegath-topics", t.chat.drain, { stop_at = "options" })
        t.exec("talkToRegath-topic1", t.chat.choose, 1)
        t.exec("talkToRegath-topics2", t.chat.drain, { stop_at = "options" })
        t.exec("talkToRegath-topic2", t.chat.choose, 2)
        t.exec("talkToRegath-closed", t.chat.drain, {})
        t.ticks(2)
        t.expect("talkToRegath.recorded", t.var.expect("varb5621_veos_arceuus", 1))

        t.exec("goto-talkToMunty", t.player.goto_tile, 1551, 3751, 0)
        t.exec("talkToMunty", t.player.talk_to, "lovakengj_generalstore", 1)
        t.exec("talkToMunty-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToMunty-ask", t.chat.choose, "Can I ask you about Lovakengj?")
        t.exec("talkToMunty-topics", t.chat.drain, { stop_at = "options" })
        t.exec("talkToMunty-topic1", t.chat.choose, 1)
        t.exec("talkToMunty-topics2", t.chat.drain, { stop_at = "options" })
        t.exec("talkToMunty-topic2", t.chat.choose, 2)
        t.exec("talkToMunty-closed", t.chat.drain, {})
        t.ticks(2)
        t.expect("talkToMunty.recorded", t.var.expect("varb5622_veos_lovakengj", 1))

        t.exec("goto-talkToJennifer", t.player.goto_tile, 1519, 3589, 0)
        t.exec("talkToJennifer", t.player.talk_to, "shayzien_generalstore", 1)
        t.exec("talkToJennifer-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToJennifer-ask", t.chat.choose, "Can I ask you about Shayzien?")
        t.exec("talkToJennifer-topics", t.chat.drain, { stop_at = "options" })
        t.exec("talkToJennifer-topic1", t.chat.choose, 1)
        t.exec("talkToJennifer-topics2", t.chat.drain, { stop_at = "options" })
        t.exec("talkToJennifer-topic2", t.chat.choose, 2)
        t.exec("talkToJennifer-closed", t.chat.drain, {})
        t.ticks(2)
        t.expect("talkToJennifer.recorded", t.var.expect("varb5623_veos_shayzien", 1))
        t.expect("quest.stage.houses_still", t.quest.expect_stage("houses"))

        t.exec("goto-talkToHorace", t.player.goto_tile, 1773, 3590, 0)
        t.exec("talkToHorace", t.player.talk_to, "hosidius_generalstore", 1)
        t.exec("talkToHorace-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToHorace-ask", t.chat.choose, "Can I ask you about Hosidius?")
        t.exec("talkToHorace-topics", t.chat.drain, { stop_at = "options" })
        t.exec("talkToHorace-topic1", t.chat.choose, 1)
        t.exec("talkToHorace-topics2", t.chat.drain, { stop_at = "options" })
        t.exec("talkToHorace-topic2", t.chat.choose, 2)
        t.exec("talkToHorace-closed", t.chat.drain, {})
        t.ticks(2)
        t.expect("talkToHorace.recorded", t.var.expect("varb5624_veos_hosidius", 1))
        t.expect("quest.stage.returnv", t.quest.expect_stage("returnv"))

        t.exec("goto-returnToVeos", t.player.goto_tile, 1824, 3689, 0)
        t.exec("returnToVeos", t.player.talk_to, "veos_vis_amulet", 1)
        t.exec("returnToVeos-menu", t.chat.drain, { stop_at = "options" })
        t.exec("returnToVeos-client", t.chat.choose, "Let's talk about your client...")
        t.exec("returnToVeos-orb", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.altar", t.quest.expect_stage("altar"))
        local orb_ok, orb_n = t.inv.count("veos_orb")
        t.check("returnToVeos.orb", orb_ok == "ok" and orb_n == 1, "veos_orb count " .. tostring(orb_n))

        t.exec("goto-goToAltar", t.player.goto_tile, 1712, 3880, 0)
        t.exec("goToAltar", t.player.inv_op, "veos_orb", 1)
        t.exec("goToAltar-closed", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))

        t.exec("goto-finishQuest", t.player.goto_tile, 1824, 3689, 0)
        local lamp_before_ok, lamp_before = t.inv.count("veos_lamp")
        local memoirs_before_ok, memoirs_before = t.inv.count("veos_kharedsts_memoirs")
        t.exec("finishQuest", t.player.talk_to, "veos_vis_amulet", 1)
        t.exec("finishQuest-menu", t.chat.drain, { stop_at = "options" })
        t.exec("finishQuest-client", t.chat.choose, "Let's talk about your client...")
        t.exec("finishQuest-possession", t.chat.drain, {})
        t.ticks(2)

        t.quest.expect_complete()
        local lamp_after_ok, lamp_after = t.inv.count("veos_lamp")
        local memoirs_after_ok, memoirs_after = t.inv.count("veos_kharedsts_memoirs")
        t.check("reward.veos_lamp", lamp_before_ok == "ok" and lamp_after_ok == "ok" and lamp_after == lamp_before + 2,
            "veos_lamp " .. tostring(lamp_before) .. " -> " .. tostring(lamp_after))
        t.check("reward.veos_kharedsts_memoirs", memoirs_before_ok == "ok" and memoirs_after_ok == "ok" and memoirs_after == memoirs_before + 1,
            "memoirs " .. tostring(memoirs_before) .. " -> " .. tostring(memoirs_after))
        t.finish(0)
    end,
}
