-- Current Affairs.
-- content: OSRS-Content/osrs239-content/server/scripts/quests/quest_currentaffairs/scripts/currentaffairs.rs2
-- Arhein's opnpc1 delegates to `ca_arhein_talk` via
-- areas/area_catherby/scripts/arhein.rs2 (op 1, confirmed). Harry's
-- opnpc1 delegates to `ca_harry_talk` the same way, only inside the
-- quest's ^ca_get_mayor..^ca_show_mayor window (harry.rs2).
--
-- RE-AUTHOR (queue last_failure, after OSRS-Content 15c39665ce /
-- aa38f602a3 / c8b3e2fede): three legs that used to be soft-skips are real
-- content now, and the committed file that went green as 51be935b7
-- (2026-09-21) pre-dates all three -- it broke at row 15 form.filled:
--   * c8b3e2fede -- form cr-4p is a real interactive 8-question chatmenu
--     (~ca_fill_form: eight back-to-back ~p_choice3_header pages, no
--     preceding npc/player line -- each opens directly as chat kind
--     "options"). ANY of the 3 answers is valid (the wiki says so, quoted
--     in the content's own header comment: "the answers you provide do not
--     matter"), so every question is answered by its first row here. The
--     old use_item_on_item(charcoal, form) press only opened question 1's
--     menu and never answered it -- q1 stayed 0.
--   * c8b3e2fede -- the audit at ^ca_show_mayor is real too:
--     ~ca_audit_ask_unmatched re-asks (also via ~p_choice3_header, same
--     "options" shape, no preceding line) every question not yet marked
--     correct, in the same fixed order, and compares the answer against the
--     SAVED %current_affairs_form_qN. Answering each question's first row
--     again (by exact row text -- chat.play's "choose:" shorthand never
--     converts a numeric-looking arg to an index, only exact text or a
--     /pattern/ match against the row) reproduces what was saved during
--     filling, so the audit passes on the first pass.
--   * aa38f602a3 -- Arhein's ^ca_duck branch, once the player already holds
--     the duck and it is not in transit / not charted, falls to
--     `@ca_board_ship`: a real dialogue (verbatim Transcript:Current_Affairs
--     "Talking to Arhein again"), a p_choice2 ("Board your boat and sail
--     east to the ripple." / "Not yet."), then p_delay(2) + p_telejump to
--     ^ca_island_coord (2835,3418,0) inside the SAME script call, closing on
--     a ~mesbox ("You find a small ripple..."). No general Sailing/boat
--     vehicle subsystem exists in this pack (a soft-skip sourced in
--     ca_board_ship's own comment: the player's own boat and the helm
--     steering east are not modelled, the same shape Pandemonium's own
--     board/sail and sailors.rs2's ferry use) -- driven here as the
--     dialogue+choice the content actually offers, never goto_tile-skipped.
--   * 15c39665ce -- releasing the duck ([opheld1,sailing_charting_current_duck]
--     while standing exactly on ^ca_island_coord) spawns a REAL moving npc
--     (sailing_charting_current_duck_moving) that walks the 97-tile hop to
--     ^ca_duck_end (2801,3321,0, re-pinned one tile off the wiki's literal
--     Holgart shore pin -- see the constant file's own probe) over
--     ^ca_duck_swim_ticks (150) server ticks, then the script (still
--     suspended in that same p_delay) spawns the STOPPED collectible duck
--     there. This file goto_tiles to the landing tile right after releasing
--     and awaits the stopped duck's presence for up to 170 ticks (150 +
--     margin) -- the guide's own "follow the duck ... and collect it" step,
--     driven for real.
--
-- Rewards actually granted (section 8's "what THIS pack grants" rule): both
-- stat_advance calls are now real -- ^ca_fish_xp (10000 tenths = 1000
-- Fishing XP) AND, new since this content pass, ^ca_sailing_xp (14000
-- tenths = 1400 Sailing XP; the "Sailing stat is absent from stat.pack"
-- source comment was stale -- quest_troubledtortugans already calls it) --
-- plus the unconditional inv_add(inv, sawmill_coupon_oak, 25). The duck and
-- the Mayor are already held by the time Arhein's completion branch runs
-- (this playthrough never loses either), so ~ca_quest_complete's own
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

        -- ---- Charcoal from the cabinet ----
        t.exec("goto-cabinet", t.player.goto_tile, 2827, 3453, 0)
        t.exec("getCharcoal", t.player.click_loc, "current_affairs_cabinet", 1)
        local charcoal_result, charcoal_detail = t.inv.await("charcoal", 1, 10)
        t.check("charcoal.received", charcoal_result == "ok",
            "inv.await(charcoal,1,10) -> " .. tostring(charcoal_result) .. " " .. tostring(charcoal_detail))

        -- ---- Fill form cr-4p: a REAL 8-question chatmenu (opheld1,
        -- current_affairs_form -> ~ca_fill_form, currentaffairs.rs2:56-65).
        -- Each ~p_choice3_header opens directly as chat kind "options" with
        -- no preceding npc/player line -- eight back-to-back option pages,
        -- one per question, in the fixed order docs/quests/current_affairs.md
        -- 5.2 gives. Any of the 3 answers is valid ("the answers you
        -- provide do not matter" -- the wiki, quoted in the content's own
        -- header comment), so every question is answered by its first row;
        -- the audit below answers the same first row again, so every answer
        -- matches on the first pass. The closing "You fill out form cr-4p"
        -- line is a bare mes() (only chatnpc/chatplayer/mesbox/objbox/
        -- p_choice* open a PAGE), so the chat.play list ends at the eighth
        -- choose and the mes() is read back with t.msg.expect.
        t.exec("fillForm", t.player.inv_op, "current_affairs_form", 1)
        t.exec("fillForm-questions", t.chat.play, {
            "choose:Pleasure", -- Q1 "Main reason for making port at Catherby?"
            "choose:0", -- Q2 "How many hulls does the ship have?"
            "choose:Cargo spillage", -- Q3 "Insured against cargo spillage and theft?"
            "choose:No", -- Q4 "Does the first mate have first aid training?"
            "choose:Partial", -- Q5 "Does the second mate have second aid training?"
            "choose:Yes", -- Q6 "Any plague symptoms in the last two weeks?"
            "choose:Less than a month", -- Q7 "Sailing experience?"
            "choose:Varrock", -- Q8 "Home port?"
        })
        local fillform_msg_result, fillform_msg_detail = t.msg.expect("You fill out form cr-4p, answering Catherine's eight questions.")
        t.check("fillForm.message", fillform_msg_result == "ok",
            "msg.expect(...) -> " .. tostring(fillform_msg_result) .. " " .. tostring(fillform_msg_detail))
        local q1r, q1v = t.var.varbit("current_affairs_form_q1")
        local q8r, q8v = t.var.varbit("current_affairs_form_q8")
        t.check("form.filled", q1r == "ok" and q1v == 1 and q8r == "ok" and q8v == 1,
            string.format("q1: %s %s, q8: %s %s", tostring(q1r), tostring(q1v), tostring(q8r), tostring(q8v)))

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

        -- ---- Catherine's audit: re-asks all 8 questions (same "options"
        -- shape, no preceding line, ca_audit_ask_unmatched) and compares
        -- each answer to the saved %current_affairs_form_qN -- answering the
        -- same first-row text again matches every one of them, so
        -- ~ca_audit_all_correct is true on this first pass and Catherine's
        -- closing line lands in the SAME press. ----
        t.exec("goto-talkToCouncillorAudit", t.player.goto_tile, 2825, 3454, 0)
        t.exec("talkToCouncillorAudit", t.player.talk_to, "current_affairs_councillor", 1)
        t.exec("talkToCouncillorAudit-dialog", t.chat.play, {
            "player:Yes, I have it here.",
            "npc:A fish? Very well",
            "choose:Pleasure", -- audit Q1 (must match the saved answer)
            "choose:0", -- audit Q2
            "choose:Cargo spillage", -- audit Q3
            "choose:No", -- audit Q4
            "choose:Partial", -- audit Q5
            "choose:Yes", -- audit Q6
            "choose:Less than a month", -- audit Q7
            "choose:Varrock", -- audit Q8
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

        -- ---- Board the boat: a real dialogue + choice (ca_board_ship,
        -- Transcript:Current_Affairs "Talking to Arhein again"), then a
        -- p_delay(2) + p_telejump to the ripple inside the same script
        -- call, closing on a ~mesbox page. Sourced in the content's own
        -- comment (ca_board_ship, currentaffairs.rs2): no general
        -- Sailing/boat vehicle subsystem exists in this pack, so "board
        -- your own boat and steer east" is this pack's soft-skip idiom
        -- (the same shape Pandemonium's board/sail and sailors.rs2's ferry
        -- use) rather than a modelled boat the player pilots. ----
        t.exec("talkToArheinBoard", t.player.talk_to, "arhein", 1)
        t.exec("talkToArheinBoard-dialog", t.chat.play, {
            "player:About that duck...",
            "npc:Yes?",
            "player:What do I need to do with him?",
            "npc:From what I've been told",
            "player:Sounds good. Where should I release him?",
            "npc:I saw a good spot next to that island",
            "player:Alright, I'll sail over there right away.",
            "choose:Board your boat and sail east to the ripple.", -- (vs "Not yet.")
            "mesbox:You find a small ripple in the water by the Obelisk of Water.",
        })
        local ripple_tile_result, ripple_tile = t.world.tile()
        t.check("board.arrivedAtRipple", ripple_tile_result == "ok" and ripple_tile
            and ripple_tile.x == 2835 and ripple_tile.z == 3418 and ripple_tile.level == 0,
            "world.tile() -> " .. tostring(ripple_tile_result) .. " " .. tostring(ripple_tile and
                (ripple_tile.x .. "," .. ripple_tile.z .. "," .. ripple_tile.level) or "nil"))

        -- ---- Release the duck onto the ripple: a real moving npc
        -- (sailing_charting_current_duck_moving) walks the 97-tile hop to
        -- the shore near Holgart over ^ca_duck_swim_ticks (150) server
        -- ticks (ca_release_duck, currentaffairs.rs2:157-179), then the
        -- STOPPED collectible duck is placed there. releaseDuck/goto-
        -- duckShore/duck.landedAtShore/collectDuck below drive the guide's
        -- followThatDuck step for real (release, wait out the swim, walk to
        -- the shore, collect it) -- everything except piloting the boat
        -- itself, which this content pack does not model at all (the same
        -- gap ca_board_ship's own dialogue narrates above).
        -- GUIDE-GAP: followThatDuck the player's own boat and helm are not modelled, currentaffairs.rs2:314 -- release/swim-wait/walk/collect below IS driven for real.
        -- ----
        t.exec("releaseDuck", t.player.inv_op, "sailing_charting_current_duck", 1)
        local release_msg_result, release_msg_detail = t.msg.expect("You release the Current duck onto the ripple. Track-current!")
        t.check("releaseDuck.message", release_msg_result == "ok",
            "msg.expect(...) -> " .. tostring(release_msg_result) .. " " .. tostring(release_msg_detail))
        t.ticks(1) -- trap 24: read a container one tick after a click verb's ok, never on the same line
        local duck_gone_result, duck_gone_count = t.inv.count("sailing_charting_current_duck")
        t.check("releaseDuck.consumed", duck_gone_result == "ok" and duck_gone_count == 0,
            "inv.count(sailing_charting_current_duck) -> " .. tostring(duck_gone_result) .. " " .. tostring(duck_gone_count))

        -- ---- Follow: walk to the shore the duck is swimming toward and
        -- wait out its 150-tick crossing (^ca_duck_swim_ticks). ----
        t.exec("goto-duckShore", t.player.goto_tile, 2801, 3321, 0, 20) -- 15c39665ce's re-pinned landing tile
        local duck_present_result, duck_present_detail = t.npc.await_present("sailing_charting_current_duck_stopped", 10, 170)
        t.check("duck.landedAtShore", duck_present_result == "ok",
            "npc.await_present(sailing_charting_current_duck_stopped,10,170) -> " .. tostring(duck_present_result) .. " " .. tostring(duck_present_detail))

        -- ---- Collect: [opnpc1,sailing_charting_current_duck_stopped]
        -- answers with a bare mes() and two varp writes, no dialogue page --
        -- talk_to's chat_message settle arm catches the mes() and its
        -- detail carries the content line (QUEST_AUTHORING.md section 3's
        -- talk_owes_a_page note). ----
        t.exec("collectDuck", t.player.talk_to, "sailing_charting_current_duck_stopped", 1)
        local chart_bit_result, chart_bit_value = t.var.varbit("sailing_charting_current_duck_catherby_bay_complete")
        t.check("duck.charted", chart_bit_result == "ok" and chart_bit_value == 1,
            "var.varbit(sailing_charting_current_duck_catherby_bay_complete) -> " .. tostring(chart_bit_result) .. " " .. tostring(chart_bit_value))
        local duck_back_result, duck_back_detail = t.inv.await("sailing_charting_current_duck", 1, 10)
        t.check("duck.recollected", duck_back_result == "ok",
            "inv.await(sailing_charting_current_duck,1,10) -> " .. tostring(duck_back_result) .. " " .. tostring(duck_back_detail))

        -- Snapshot skills BEFORE the hand-in that grants Fishing + Sailing XP.
        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("preCompletion.snapshot", snapshot_result == "ok",
            "skill.snapshot() -> " .. tostring(snapshot_result))

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
        local fish_gain_result, fish_gain_detail = t.skill.expect_gain("fishing", 1000, snapshot)
        t.check("reward.fishingXp", fish_gain_result == "ok",
            "skill.expect_gain(fishing,1000) -> " .. tostring(fish_gain_result) .. " " .. tostring(fish_gain_detail))
        local sailing_gain_result, sailing_gain_detail = t.skill.expect_gain("sailing", 1400, snapshot)
        t.check("reward.sailingXp", sailing_gain_result == "ok",
            "skill.expect_gain(sailing,1400) -> " .. tostring(sailing_gain_result) .. " " .. tostring(sailing_gain_detail))
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
