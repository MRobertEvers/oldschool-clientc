-- Current Affairs.
-- content: OSRS-Content/osrs239-content/server/scripts/quests/quest_currentaffairs/scripts/currentaffairs.rs2
-- Arhein's opnpc1 delegates to `ca_arhein_talk` via
-- areas/area_catherby/scripts/arhein.rs2 (op 1, confirmed). Harry's
-- opnpc1 delegates to `ca_harry_talk` the same way, only inside the
-- quest's ^ca_get_mayor..^ca_show_mayor window (harry.rs2).
--
-- RETRY (queue last_failure): Councillor Catherine is now spawned --
-- quest_currentaffairs/configs/currentaffairs.spawn adds
-- current_affairs_councillor at 2825,3454,0, her own ^ca_councillor_coord
-- tile -- so the organic route plays through. The prior blocked file's bug
-- was its own bind: `%current_affairs` is a VARBIT (all.varbit.compack
-- 18282) packed into the `current_affairs_main` container varp; binding the
-- container itself reads the whole packed value (527 once other bits are
-- set) instead of the 0/5/10/.../45 stage ladder. Bind the varbit symbol
-- directly -- `t.quest.bind` resolves a varp-or-varbit symbol transparently
-- either way (QUEST_AUTHORING.md section 3, `quest.stage`).
--
-- Tiles, decoded from currentaffairs.constant's own `^*_coord` constants
-- (level_regionX_regionY_localX_localY, QUEST_AUTHORING.md section 8):
--   ^ca_arhein_coord     = 0_43_53_51_38 -> 2803,3430,0 (matches arhein.spawn)
--   ^ca_councillor_coord = 0_44_53_9_62  -> 2825,3454,0 (matches the new
--                                            currentaffairs.spawn row)
--   ^ca_cabinet_coord    = 0_44_53_11_61 -> 2827,3453,0
--   ^ca_harry_coord      = 0_44_53_15_52 -> 2831,3444,0 (harry.spawn has him
--                                            one tile off, at 2834,3445,0 --
--                                            goto that live spawn tile, not
--                                            the constant)
--
-- Rewards actually granted (section 8's "what THIS pack grants" rule, not
-- the scroll string): `~ca_quest_complete` calls stat_advance(fishing,
-- ^ca_fish_xp=10000 tenths = 1000 XP) and unconditionally inv_add(inv,
-- sawmill_coupon_oak, 25); Sailing XP is a documented soft-skip (a bare
-- mes() line, no stat_advance call at all) so it is not asserted here. The
-- duck and the Mayor are already held by the time Arhein's branch runs (this
-- playthrough never loses either), so `~ca_quest_complete`'s own
-- conditional inv_add for them never fires -- asserted as already-held
-- items, not as fresh grants.

return {
    id = "currentaffairs",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::currentaffairs", -- debugproc: resets the quest varp, teleports to Arhein
        "::give coins 50", -- Harry's mayoral-fishbowl-and-net purchase, a real currency cost
        "::setlevel sailing 22",
        "::setlevel fishing 10",
        "::complete quest_pandemonium",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "current_affairs", -- the STAGE varbit (all.varbit.compack 18282), not the
                                       -- packed container "current_affairs_main" (trap: the
                                       -- container reads 527, not the 0..45 ladder)
            constants = {
                not_started = 0,
                councillor = 5,
                form = 10,
                arhein_mayor = 15,
                get_mayor = 20,
                show_mayor = 25,
                sign = 30,
                news = 35,
                duck = 40,
                complete = 45,
            },
            row = "quest_currentaffairs", -- all.dbrow.compack 7105
            display = "Current Affairs", -- all.dbrow [quest_currentaffairs] displayname
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- the ::currentaffairs debug reset's effect is not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---- Arhein: start the quest ----
        t.exec("goto-startQuest", t.player.goto_tile, 2803, 3430, 0) -- arhein.spawn (m43_53.spawn)
        t.exec("startQuest", t.player.talk_to, "arhein", 1)
        -- ca_arhein_talk, ^ca_not_started branch: chatplayer, chatnpc, p_choice2(Yes/No), chatnpc.
        t.exec("startQuest-dialog", t.chat.play, {
            "player:What's with the duck?",
            "npc:Red tape. Councillor Catherine won't let me use my Current duck until the by-laws change.",
            "choose:Yes.",
            "npc:Talk to Councillor Catherine in north-east Catherby. Tell her you're my new employee.",
        })
        t.expect("quest.stage.councillor", t.quest.expect_stage("councillor"))

        -- ---- Councillor Catherine: receive form cr-4p ----
        t.exec("goto-talkToCouncillor", t.player.goto_tile, 2825, 3454, 0) -- currentaffairs.spawn row
        t.exec("talkToCouncillor", t.player.talk_to, "current_affairs_councillor", 1)
        -- ca_councillor_talk, ^ca_councillor branch: a single chatnpc page (no chatplayer first).
        t.exec("talkToCouncillor-dialog", t.chat.play, {
            "npc:Arhein's employee? Fill form cr-4p. Charcoal is in the cabinet.",
        })
        t.expect("quest.stage.form", t.quest.expect_stage("form"))

        -- ---- Charcoal from the cabinet, fill the form ----
        t.exec("goto-cabinet", t.player.goto_tile, 2827, 3453, 0)
        t.exec("getCharcoal", t.player.click_loc, "current_affairs_cabinet", 1)
        local charcoal_result, charcoal_detail = t.inv.await("charcoal", 1, 10)
        t.check("charcoal.received", charcoal_result == "ok",
            "inv.await(charcoal,1,10) -> " .. tostring(charcoal_result) .. " " .. tostring(charcoal_detail))

        -- opheldu,current_affairs_form (last_useitem=charcoal) -> ~ca_fill_form_soft;
        -- charcoal is never consumed (docs/quests/current_affairs.md 5.2).
        -- use_item_on_item(a,b) arms a and clicks b; the opheldu TRIGGER here
        -- is keyed on the item clicked second (current_affairs_form), with
        -- last_useitem naming the armed item (charcoal) -- arming the form
        -- first fires [opheldu,charcoal] instead, which this script never
        -- declares, and the client answers its own generic "Nothing
        -- interesting happens." (measured run 1: fillForm FAIL).
        t.exec("fillForm", t.player.use_item_on_item, "charcoal", "current_affairs_form")
        local q1_result, q1_value = t.var.varbit("current_affairs_form_q1")
        t.check("form.filled", q1_result == "ok" and q1_value == 1,
            "var.varbit(current_affairs_form_q1) -> " .. tostring(q1_result) .. " " .. tostring(q1_value))

        -- ---- Hand the filled form back, then re-talk to advance ----
        t.exec("goto-handInForm", t.player.goto_tile, 2825, 3454, 0)
        t.exec("handInForm", t.player.talk_to, "current_affairs_councillor", 1)
        t.exec("handInForm-dialog", t.chat.play, {
            "player:Yes, I have it here.",
            "npc:Thank you. Speak with me again once I've filed it.",
        })
        t.exec("confirmForm", t.player.talk_to, "current_affairs_councillor", 1)
        t.exec("confirmForm-dialog", t.chat.play, {
            "npc:Good. Now find Arhein — he needs a mayor for the by-law change.",
        })
        t.expect("quest.stage.arhein_mayor", t.quest.expect_stage("arhein_mayor"))

        -- ---- Arhein sends the player after a mayor ----
        t.exec("goto-talkToArheinMayor", t.player.goto_tile, 2803, 3430, 0)
        t.exec("talkToArheinMayor", t.player.talk_to, "arhein", 1)
        t.exec("talkToArheinMayor-dialog", t.chat.play, {
            "player:I need to find the Mayor of Catherby.",
            "npc:Funny story... ask Harry about a new mayor.",
        })
        t.expect("quest.stage.get_mayor", t.quest.expect_stage("get_mayor"))

        -- ---- Harry: buy the election kit ----
        t.exec("goto-talkToHarry", t.player.goto_tile, 2834, 3445, 0) -- harry.spawn (m44_53.spawn)
        t.exec("talkToHarry", t.player.talk_to, "harry", 1)
        t.exec("talkToHarry-dialog", t.chat.play, {
            "player:I'm here about the mayor.",
            "npc:I can sell you a mayoral fishbowl and tiny net for 50 coins.",
            "choose:Yes.",
            "npc:Fish in the aquarium for a mayorfish.",
        })
        local coins_result, coins_count = t.inv.count("coins")
        local bowl_result, bowl_count = t.inv.count("current_affairs_mayoral_fishbowl")
        local net_result, net_count = t.inv.count("tiny_net")
        t.check("kit.received", coins_count == 0 and bowl_count == 1 and net_count == 1,
            string.format("coins=%s(%s) fishbowl=%s(%s) tiny_net=%s(%s)",
                tostring(coins_count), tostring(coins_result),
                tostring(bowl_count), tostring(bowl_result),
                tostring(net_count), tostring(net_result)))

        -- ---- Catch the Mayor of Catherby from the aquarium ----
        t.exec("catchMayor", t.player.click_loc, "aquarium", 1)
        local mayor_result, mayor_detail = t.inv.await("current_affairs_mayor_of_catherby", 1, 10)
        t.check("mayor.received", mayor_result == "ok",
            "inv.await(current_affairs_mayor_of_catherby,1,10) -> " .. tostring(mayor_result) .. " " .. tostring(mayor_detail))

        -- ---- Show the mayor to Arhein ----
        t.exec("goto-talkToArheinShowMayor", t.player.goto_tile, 2803, 3430, 0)
        t.exec("talkToArheinShowMayor", t.player.talk_to, "arhein", 1)
        t.exec("talkToArheinShowMayor-dialog", t.chat.play, {
            "player:About the mayor...",
            "npc:He's a fine specimen. Show him to the councillor.",
        })
        t.expect("quest.stage.show_mayor", t.quest.expect_stage("show_mayor"))

        -- ---- Catherine's audit (soft-skipped content, still a real dialogue) ----
        t.exec("goto-talkToCouncillorAudit", t.player.goto_tile, 2825, 3454, 0)
        t.exec("talkToCouncillorAudit", t.player.talk_to, "current_affairs_councillor", 1)
        t.exec("talkToCouncillorAudit-dialog", t.chat.play, {
            "player:Yes, I have it here.",
            "npc:A fish? Very well — we'll begin the audit.",
            "npc:Audit complete. Now take form 7r4-5h for the mayor's signature.",
        })
        t.expect("quest.stage.sign", t.quest.expect_stage("sign"))

        -- ---- A second Catherine talk actually grants unsigned form 7r4-5h ----
        t.exec("talkToCouncillorGetForm2", t.player.talk_to, "current_affairs_councillor", 1)
        t.exec("talkToCouncillorGetForm2-dialog", t.chat.play, {
            "npc:Take form 7r4-5h and have the mayor sign it.",
        })
        local form2_result, form2_detail = t.inv.await("current_affairs_form_2", 1, 10)
        t.check("form2.received", form2_result == "ok",
            "inv.await(current_affairs_form_2,1,10) -> " .. tostring(form2_result) .. " " .. tostring(form2_detail))

        -- ---- The mayor signs form 7r4-5h ----
        t.exec("signForm", t.player.use_item_on_item, "current_affairs_form_2", "current_affairs_mayor_of_catherby")
        local signed_result, signed_detail = t.inv.await("current_affairs_form_2_signed", 1, 10)
        t.check("form.signed", signed_result == "ok",
            "inv.await(current_affairs_form_2_signed,1,10) -> " .. tostring(signed_result) .. " " .. tostring(signed_detail))

        -- ---- Hand the signed form back to Catherine ----
        t.exec("talkToCouncillorSigned", t.player.talk_to, "current_affairs_councillor", 1)
        t.exec("talkToCouncillorSigned-dialog", t.chat.play, {
            "player:Here's the signed form.",
            "npc:Everything appears to be in order. The by-law is changed.",
        })
        t.expect("quest.stage.news", t.quest.expect_stage("news"))

        -- ---- Tell Arhein the by-law changed, receive the Current duck ----
        t.exec("goto-talkToArheinNews", t.player.goto_tile, 2803, 3430, 0)
        t.exec("talkToArheinNews", t.player.talk_to, "arhein", 1)
        t.exec("talkToArheinNews-dialog", t.chat.play, {
            "player:The by-law has been changed!",
            "npc:At last! Take this Current duck and chart the bay for me.",
        })
        t.expect("quest.stage.duck", t.quest.expect_stage("duck"))
        local duck_result, duck_detail = t.inv.await("sailing_charting_current_duck", 1, 10)
        t.check("duck.received", duck_result == "ok",
            "inv.await(sailing_charting_current_duck,1,10) -> " .. tostring(duck_result) .. " " .. tostring(duck_detail))

        -- Snapshot skills BEFORE the hand-in that grants Fishing XP.
        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("preCompletion.snapshot", snapshot_result == "ok",
            "skill.snapshot() -> " .. tostring(snapshot_result))

        -- ---- Chart the currents (opheld1,sailing_charting_current_duck; soft-skip) ----
        local chart_result, chart_detail = t.player.inv_op("sailing_charting_current_duck", 1)
        t.check("chartCurrents", chart_result == "ok",
            "inv_op(sailing_charting_current_duck,1) -> " .. tostring(chart_result) .. " " .. tostring(chart_detail))
        local chart_msg_result, chart_msg_detail = t.msg.expect("You collect the Current duck after charting the bay.")
        t.check("chartCurrents.message", chart_msg_result == "ok",
            "msg.expect(...) -> " .. tostring(chart_msg_result) .. " " .. tostring(chart_msg_detail))

        -- ---- Tell Arhein, complete the quest ----
        t.exec("goto-talkToArheinComplete", t.player.goto_tile, 2803, 3430, 0)
        t.exec("talkToArheinComplete", t.player.talk_to, "arhein", 1)
        t.exec("talkToArheinComplete-dialog", t.chat.play, {
            "player:I've charted the currents!",
            "npc:Brilliant work!",
        })
        t.ticks(3) -- completion is asynchronous -- load-bearing before expect_complete()
        t.quest.expect_complete()

        -- ---- Rewards actually granted (see header note) ----
        local gain_result, gain_detail = t.skill.expect_gain("fishing", 1000, snapshot)
        t.check("reward.fishingXp", gain_result == "ok",
            "skill.expect_gain(fishing,1000) -> " .. tostring(gain_result) .. " " .. tostring(gain_detail))
        local coupon_result, coupon_detail = t.inv.expect_has("sawmill_coupon_oak", 25)
        t.check("reward.coupons", coupon_result == "ok",
            "inv.expect_has(sawmill_coupon_oak,25) -> " .. tostring(coupon_result) .. " " .. tostring(coupon_detail))
        local duck_reward_result, duck_reward_detail = t.inv.expect_has("sailing_charting_current_duck", 1)
        t.check("reward.duck", duck_reward_result == "ok",
            "inv.expect_has(sailing_charting_current_duck,1) -> " .. tostring(duck_reward_result) .. " " .. tostring(duck_reward_detail))
        local mayor_reward_result, mayor_reward_detail = t.inv.expect_has("current_affairs_mayor_of_catherby", 1)
        t.check("reward.mayor", mayor_reward_result == "ok",
            "inv.expect_has(current_affairs_mayor_of_catherby,1) -> " .. tostring(mayor_reward_result) .. " " .. tostring(mayor_reward_detail))

        t.finish(0)
        return
    end,
}
