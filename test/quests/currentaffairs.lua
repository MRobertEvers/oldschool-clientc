-- Current Affairs.
-- content: OSRS-Content/osrs239-content/server/scripts/quests/quest_currentaffairs/scripts/currentaffairs.rs2
-- Arhein's opnpc1 delegates to `ca_arhein_talk` via
-- areas/area_catherby/scripts/arhein.rs2 (op 1, confirmed). Harry's
-- opnpc1 delegates to `ca_harry_talk` the same way, only inside the
-- quest's ^ca_get_mayor..^ca_show_mayor window (harry.rs2).
--
-- RE-AUTHOR (queue last_failure, after OSRS-Content 70487ebee8 /
-- [parity:parity1o]): the sea leg is SAILED for real now, not narrated or
-- telejumped, and the committed file this replaces pre-dates that pass --
-- it broke at row 52 talkToArheinNews-dialog (content opens with Arhein's
-- verbatim "Hello again!" four-row menu now, not the old two-line stub).
-- Three shapes changed, all in currentaffairs.rs2:
--   * `ca_arhein_talk` wraps every Going-with-the-Flow visit (news/duck/
--     charted) in `ca_arhein_hello` (currentaffairs.rs2:238): "Hello
--     again!" then a p_choice4 ("What do you have for sale?" / the quest
--     row / "Where abouts do you make deliveries to?" / "I'd best be
--     off.") -- choose the quest row (its text depends on stage: "The
--     by-law has been changed!" at ^ca_news, "I've charted the currents!"
--     once the chart bit is set) to fall into the branch below.
--   * No `ca_board_ship`/p_telejump at all any more. Boarding is a REAL
--     sail: the player's own boat (setup's sailing_boat_1_* vars, the
--     Catherby gangplank, doc section 3's sail recipe), helmed east, and
--     the ripple is the sea zone [zone,0_44_53_16_24] (currentaffairs.rs2:
--     634) the engine queues on every rider when the HULL (never the
--     rider's feet) crosses in -- `ca_own_boat_here`/`ca_boat_on_ripple`
--     (currentaffairs.rs2:127-146) gate release and collection on the hull
--     alone, so a `t.world.tile()` read of the RIDER means nothing here;
--     `t.sail.state()` is the channel.
--   * Releasing the duck ([opheld1,sailing_charting_current_duck],
--     currentaffairs.rs2:154, only once the hull is on the ripple) spawns a
--     REAL moving npc (sailing_charting_current_duck_moving) that swims
--     five ocean-checked current legs to the wiki's shore pin near Holgart
--     (^ca_duck_end, 2802,3322) over an [ai_timer], then becomes the
--     stopped collectible form and prints the wiki's blue stop message.
--     Collection is an [apnpc1] press from the OWNER's own boat
--     (currentaffairs.rs2:654, "The duck recognises your boat and climbs
--     aboard...") -- the guide's own "collect the duck" step, driven for
--     real through the sailing side panel's deck-row press
--     (`t.sail._press_deck_row`), never a goto/talk_to.
-- Proved route (sail legs, furl points, the heading press before
-- re-setting sail, the deck-row press coordinates):
-- build/parity_state/parity1o/ca_scripts/ca_sea_leg.lua.
--
-- Rewards actually granted (section 8's "what THIS pack grants" rule): both
-- stat_advance calls are real -- ^ca_fish_xp (10000 tenths = 1000
-- Fishing XP) AND ^ca_sailing_xp (14000 tenths = 1400 Sailing XP) -- plus
-- the unconditional inv_add(inv, sawmill_coupon_oak, 25). The duck and the
-- Mayor are already held by the time Arhein's completion branch runs (this
-- playthrough never loses either), so ~ca_quest_complete's own conditional
-- inv_add for them never fires -- asserted as already-held items, not as
-- fresh grants.

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
        -- Own a personal boat at the Catherby berth (doc section 3's sail
        -- recipe) -- a Sailing-skill prerequisite, not this quest's own
        -- work, so it belongs in setup like the coins/levels above.
        "::setvar sailing_boat_1_owned 1",
        "::setvar sailing_boat_1_type 1", -- skiff
        "::setvar sailing_boat_1_port 6", -- Catherby
        "::setvar sailing_last_personal_boat_boarded 1",
        "::setvar sailing_boat_1_hotspot_6 1", -- a hold
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

        -- ---- Tell Arhein the by-law changed, receive the Current duck.
        -- ca_arhein_talk wraps every ^ca_news/^ca_duck visit in
        -- ca_arhein_hello's "Hello again!" + p_choice4 menu; choose the
        -- quest row ("The by-law has been changed!" at this stage) to fall
        -- into the ^ca_news branch, verbatim Transcript:Current_Affairs
        -- "Going with the Flow" / "Talking to Arhein". ----
        t.exec("goto-talkToArheinNews", t.player.goto_tile, 2803, 3430, 0)
        t.exec("talkToArheinNews", t.player.talk_to, "arhein", 1)
        t.exec("talkToArheinNews-dialog", t.chat.play, {
            "npc:Hello again!",
            "choose:The by-law has been changed!",
            "player:The by-law has been changed!",
            "npc:Excellent! Thank you for sorting that out for me.",
            "player:What is it?",
            "npc:I have some repairs I need to do on my ship.",
            "player:Of course. How do I do it?",
            "npc:From what I've been told, you just need to release the duck",
            "player:Sounds good. Where should I release him?",
            "npc:I saw a good spot next to that island east of the docks.",
            "player:Alright, I'll sail over there right away.",
            "*", -- ~objbox(sailing_charting_current_duck, "Arhein hands you the duck.")
            "npc:Have fun!",
        })
        t.expect("quest.stage.duck", t.quest.expect_stage("duck"))
        local duck_result, duck_detail = t.inv.await("sailing_charting_current_duck", 1, 10)
        t.check("duck.received", duck_result == "ok",
            "inv.await(sailing_charting_current_duck,1,10) -> " .. tostring(duck_result) .. " " .. tostring(duck_detail))

        -- ---- Board the player's own boat at the Catherby gangplank and
        -- sail east: since OSRS-Content 70487ebee8 there is no ca_board_ship
        -- dialogue/p_telejump at all -- the ripple is the hull-queued sea
        -- zone [zone,0_44_53_16_24] (currentaffairs.rs2:634), gated on the
        -- player's OWN hull (ca_own_boat_here/ca_boat_on_ripple,
        -- currentaffairs.rs2:127-146), never the rider's tile. Proved route:
        -- build/parity_state/parity1o/ca_scripts/ca_sea_leg.lua. ----
        t.exec("goto-gangplank", t.player.goto_tile, 2797, 3413, 0)
        t.exec("board", t.sail.board, "sailing_gangplank_catherby")
        t.exec("helm", t.sail.helm, "Helm")
        t.exec("sails", t.sail.sails, true)
        t.exec("sail.leg1", t.sail.sail_to, 2800, 3400, 2, 200)
        t.exec("sail.leg2", t.sail.sail_to, 2835, 3402, 2, 300)
        t.exec("sail.leg3", t.sail.sail_to, 2835, 3412, 1, 200)
        -- The ripple sits against the obelisk island's shore: furl on
        -- arrival instead of sailing on into it. (Quest Helper's own
        -- SailStep, "sailToStart" -- named verbatim, trap 32.)
        t.exec("sailToStart", t.sail.sail_to, 2835, 3417, 1, 200)
        t.exec("sailToStart.furl", t.sail.sails, false)
        t.ticks(4)
        local ripple_msg_result, ripple_msg_detail = t.msg.expect("You find a small ripple in the water by the Obelisk of Water.")
        t.check("ripple.zone.message", ripple_msg_result == "ok",
            "msg.expect(...) -> " .. tostring(ripple_msg_result) .. " " .. tostring(ripple_msg_detail))

        -- ---- Release the duck on the ripple: ca_release_duck
        -- (currentaffairs.rs2:154-179) spawns a REAL moving npc
        -- (sailing_charting_current_duck_moving) that swims five
        -- ocean-checked current legs to the wiki's shore pin near Holgart
        -- (^ca_duck_end, 2802,3322) over an [ai_timer], then becomes the
        -- stopped collectible form with the wiki's blue stop message --
        -- the guide's own followThatDuck step, driven for real. (Quest
        -- Helper's own DetailedQuestStep, "releaseDuck" -- named
        -- verbatim, trap 32.) ----
        t.exec("releaseDuck", t.player.inv_op, "sailing_charting_current_duck", 1)
        t.exec("releaseDuck.dialog", t.chat.play, {
            "player:There he goes! Now to follow him and see where these currents flow.",
        })
        local released_result, released_count = t.inv.count("sailing_charting_current_duck")
        t.check("release.consumed", released_result == "ok" and released_count == 0,
            "inv.count(sailing_charting_current_duck) -> " .. tostring(released_count))

        -- The inventory press can land on the sailing side panel's own sail
        -- toggle first; bring the panel back and re-set course.
        t.check("tab.sailing", t.ui.tab(0) == "ok", "sailing side panel back after the inventory press")
        t.ticks(1)
        t.check("turn.west", t.sail._press_heading(4) == "ok", "Set heading west (off the island before the sails fill)")
        t.ticks(8)
        t.exec("sails.reset", t.sail.sails, true)
        -- Back up the channel the hull came down, then west along it after
        -- the duck, rounding the point in open water to come at the duck's
        -- pin from the east (the mainland shore runs south of it).
        t.exec("follow.leg0", t.sail.sail_to, 2826, 3417, 2, 200)
        t.exec("follow.leg1", t.sail.sail_to, 2801, 3401, 2, 300)
        t.exec("follow.leg2", t.sail.sail_to, 2792, 3380, 2, 300)
        t.exec("follow.leg3", t.sail.sail_to, 2792, 3342, 2, 300)
        t.exec("follow.leg4", t.sail.sail_to, 2795, 3329, 2, 300)
        t.exec("follow.leg5", t.sail.sail_to, 2809, 3328, 2, 200)
        -- The leg that actually closes on the duck's stop pin (Quest
        -- Helper's own DetailedQuestStep, "followThatDuck" -- named
        -- verbatim, trap 32).
        t.exec("followThatDuck", t.sail.sail_to, 2809, 3323, 1, 200)
        t.exec("followThatDuck.furl", t.sail.sails, false)
        t.ticks(4)
        local stop_msg_result, stop_msg_detail = t.msg.expect("Your current duck comes to a stop.")
        t.check("duck.stop.message", stop_msg_result == "ok",
            "msg.expect(...) -> " .. tostring(stop_msg_result) .. " " .. tostring(stop_msg_detail))

        -- ---- Collect: an [apnpc1] press from the own boat's deck menu
        -- (currentaffairs.rs2:654), gated on npc_owner/ca_duck_released/
        -- stage -- the guide's own "collect the duck" step, driven through
        -- the sailing side panel's deck-row press. ----
        t.exec("collect.helm_off", t.sail._press_deck_row, "Navigate", "Helm")
        t.ticks(2)
        t.exec("collect", t.sail._press_deck_row, "Collect", "Current duck", 16, 12)
        t.exec("collect.box", t.chat.play, { "*" }) -- ~objbox(duck, "The duck recognises your boat...")
        local chart_bit_result, chart_bit_value = t.var.varbit("sailing_charting_current_duck_catherby_bay_complete")
        t.check("duck.charted", chart_bit_result == "ok" and chart_bit_value == 1,
            "var.varbit(sailing_charting_current_duck_catherby_bay_complete) -> " .. tostring(chart_bit_result) .. " " .. tostring(chart_bit_value))
        local duck_back_result, duck_back_detail = t.inv.await("sailing_charting_current_duck", 1, 10)
        t.check("duck.recollected", duck_back_result == "ok",
            "inv.await(sailing_charting_current_duck,1,10) -> " .. tostring(duck_back_result) .. " " .. tostring(duck_back_detail))

        -- ---- Home to Catherby and disembark before telling Arhein. ----
        t.check("tab.sailing2", t.ui.tab(0) == "ok", "sailing side panel")
        t.exec("home.helm", t.sail.helm, "Helm")
        t.check("home.turn", t.sail._press_heading(8) == "ok", "Set heading north")
        t.ticks(8)
        t.exec("home.sails", t.sail.sails, true)
        t.exec("home.leg1", t.sail.sail_to, 2809, 3328, 2, 200)
        t.exec("home.leg2", t.sail.sail_to, 2795, 3331, 2, 200)
        t.exec("home.leg3", t.sail.sail_to, 2792, 3344, 2, 200)
        t.exec("home.leg4", t.sail.sail_to, 2792, 3380, 2, 300)
        t.exec("home.leg5", t.sail.sail_to, 2793, 3396, 2, 300)
        -- The berth leg can undershoot by a tile or two (driver steering);
        -- disembark's own camera-aim + pick reaches the gangplank from
        -- wherever the hull ends up, so this is a recorded reading, not the
        -- leg's own proof -- home.disembark below is.
        local berth_result, berth_detail = t.sail.sail_to(2793, 3407, 1, 200)
        t.check("home.berth", berth_result == "ok" or berth_result == "timeout",
            "sail_to(2793,3407,1,200) -> " .. tostring(berth_result) .. " " .. tostring(berth_detail))
        t.exec("home.furl", t.sail.sails, false)
        t.ticks(4)
        t.exec("home.disembark", t.sail.disembark, "sailing_gangplank_catherby")

        -- Snapshot skills BEFORE the hand-in that grants Fishing + Sailing XP.
        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("preCompletion.snapshot", snapshot_result == "ok",
            "skill.snapshot() -> " .. tostring(snapshot_result))

        -- ---- Tell Arhein, complete the quest. ca_arhein_talk's hello menu
        -- again (quest row now "I've charted the currents!", the chart bit
        -- being set), falling to [label,ca_charted_currents]: a mesbox, then
        -- verbatim Transcript:Current_Affairs "Talking to Arhein after
        -- charting the current" -- the mayor is already held, so its
        -- lost-mayor branch never fires. ----
        t.exec("goto-talkToArheinComplete", t.player.goto_tile, 2803, 3430, 0)
        t.exec("talkToArheinComplete", t.player.talk_to, "arhein", 1)
        t.exec("talkToArheinComplete-dialog", t.chat.play, {
            "npc:Hello again!",
            "choose:I've charted the currents!",
            "player:I've charted the currents!",
            "mesbox:You share details on the currents around Catherby with Arhein.",
            "npc:Thank you so much, friend! I'm sorry for all the nonsense",
            "npc:In fact, keep the mayor as well.",
            "player:Thank you!",
            "npc:Don't mention it. All the best!",
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
