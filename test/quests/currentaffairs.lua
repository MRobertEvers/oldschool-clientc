-- Current Affairs.
-- content: OSRS-Content/osrs239-content/server/scripts/quests/quest_currentaffairs/scripts/currentaffairs.rs2
-- Arhein's opnpc1 delegates to `ca_arhein_talk` via
-- areas/area_catherby/scripts/arhein.rs2 (op 1, confirmed). Harry's
-- opnpc1 delegates to `ca_harry_talk` the same way, only inside the
-- quest's ^ca_get_mayor..^ca_show_mayor window (harry.rs2).
--
-- RE-AUTHOR (queue last_failure, after OSRS-Content 3b41349334
-- [parity:parity1p]): "Starting off" / "Red Tape" / "Long live the Mayor"
-- are verbatim from Transcript:Current_Affairs now, not the old two-line
-- stubs this file used to drive -- it broke at row 5 startQuest-dialog
-- ("got npc 'Hello again!'"), because EVERY Arhein visit now opens with
-- `ca_arhein_hello` (currentaffairs.rs2:316): "Hello again!" then a
-- p_choice4 menu (shop / the stage's own quest row / deliveries / "I'd
-- best be off.") -- choose the quest row to fall into the stage's branch.
-- Two shapes changed beyond the wrapper, both in currentaffairs.rs2:
--   * Catherine, Harry and the mayor chain are fully voiced (~line 597 on):
--     Catherine's first visit, the form hand-in (one continuous
--     conversation all the way to ^ca_arhein_mayor, no second click),
--     Harry's kit sale, and -- new -- showing the freshly-caught mayor to
--     ARHEIN FIRST (ca_arhein_chain_mayor, :510) is what chains him and
--     advances the stage to ^ca_show_mayor; only THEN does Catherine's
--     audit (ca_councillor_audit, :733) accept him. Every answer given to
--     form cr-4p's 8 questions is repeated verbatim for the audit, so the
--     audit passes on its first pass with no retry.
--   * The quest items now carry the wiki's own ops (Inspect/Dismiss on the
--     duck, Consult/Feed/Destroy on the mayor, Destroy on both forms and
--     the fishbowl) -- none of them a guide step, so this file drives none
--     of them; proved standalone in build/parity_state/parity1p/ca_scripts/
--     ca_items.lua (98/98).
-- Proved route for the dialogue rewrite (every page, every choose text, in
-- order, including the requirement refusal and the audit):
-- build/parity_state/parity1p/ca_scripts/ca_early.lua (120/120). The sea
-- leg (sail recipe, furl points, deck-row presses) is untouched by this
-- parity pass -- kept from the previously-green post-parity1o file.
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
            varp = "varb18282_current_affairs", -- the STAGE varbit (all.varbit.compack 18282), not the
                                       -- packed container "varp4956_current_affairs_main" (trap: the
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

        -- ---- Arhein: start the quest. ca_arhein_talk's hello wrapper
        -- (currentaffairs.rs2:316-318): "Hello again!" then a p_choice4 menu
        -- whose row 2 is the stage's own quest row -- here
        -- "What's with the duck?" (^ca_arhein_start, :453). Levels/
        -- Pandemonium are already met (setup), so this is the meets-
        -- requirements=true / Yes branch straight through to ^ca_councillor. ----
        t.exec("goto-startQuest", t.player.goto_tile, 2803, 3430, 0) -- arhein.spawn (m43_53.spawn)
        t.exec("startQuest", t.player.talk_to, "arhein", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "npc:Hello again!",
            "choose:What's with the duck?",
            "player:What's with the duck?",
            "npc:Oh, it's part of a little test",
            "npc:My plan is to track the currents around Catherby",
            "player:What is it?",
            "npc:The local council have a by-law forbidding non-human workers",
            "choose:Yes.",
            "player:Anything I can do to help?",
            "npc:Well, you're welcome to try talking to Councillor Catherine",
            "player:Thanks for the warning. I'll head over and see what I can do.",
        })
        t.expect("quest.stage.councillor", t.quest.expect_stage("councillor"))

        -- ---- Councillor Catherine: "Red Tape" verbatim, ending with form
        -- cr-4p handed over (ca_councillor_first, currentaffairs.rs2:641). ----
        t.exec("goto-talkToCouncillor", t.player.goto_tile, 2825, 3454, 0) -- currentaffairs.spawn row
        t.exec("talkToCouncillor", t.player.talk_to, "current_affairs_councillor", 1)
        t.exec("talkToCouncillor-dialog", t.chat.play, {
            "player:Hello there. Are you Councillor Catherine?",
            "npc:Thank you for enquiring at the Catherby Council Office.",
            "player:I had a question about one of the local by-laws.",
            "npc:That is correct.",
            "player:Doesn't that strike you as a bit... wrong?",
            "npc:If you are referring to the rumours of Humans Against Monsters",
            "player:Impartial?",
            "npc:Completely.",
            "player:I see... So you wouldn't even be willing to let Arhein's duck",
            "npc:Are you a sailor yourself?",
            "player:Well, yes, I suppose you could say that.",
            "npc:All sailors doing business in Catherby must first be registered",
            "player:Wait... What?",
            "*", -- objbox: Councillor Catherine hands you a form.
            "npc:Please ensure when filling in the form that all answers are clearly legible.",
            "player:But why do I need to...",
            "npc:Thanks to recent budget changes",
            "player:Right...",
            "npc:Upon completion, please return the form",
        })
        t.expect("quest.stage.form", t.quest.expect_stage("form"))
        local form_result, form_detail = t.inv.await("current_affairs_form", 1, 10)
        t.check("form.received", form_result == "ok",
            "inv.await(current_affairs_form,1,10) -> " .. tostring(form_result) .. " " .. tostring(form_detail))

        -- ---- Charcoal from the cabinet ([oploc1,current_affairs_cabinet],
        -- currentaffairs.rs2:792) ----
        t.exec("goto-cabinet", t.player.goto_tile, 2827, 3453, 0)
        t.exec("getCharcoal", t.player.click_loc, "current_affairs_cabinet", 1)
        -- A left-open objbox is a suspended [proc,objbox_scaled]; calling
        -- ~objbox again before this one is dismissed makes the engine drop
        -- one of the two suspended procs outright (measured: run 2 dropped
        -- ca_fill_form's own "You start looking through the questions..."
        -- box this way, and nothing chat.play could read ever mounted).
        -- Dismiss every objbox immediately, never leave one hanging.
        t.exec("getCharcoal-box", t.chat.play, { "*" }) -- objbox: You search the cabinet and find some charcoal.
        local charcoal_result, charcoal_detail = t.inv.await("charcoal", 1, 10)
        t.check("charcoal.received", charcoal_result == "ok",
            "inv.await(charcoal,1,10) -> " .. tostring(charcoal_result) .. " " .. tostring(charcoal_detail))

        -- ---- Fill form cr-4p: a real 8-question chatmenu (opheld1,
        -- current_affairs_form -> ~ca_fill_form, currentaffairs.rs2:84-94),
        -- fixed question order, option rows exactly as ca_ask_question
        -- spells them (trailing full stop, except the hull counts). Any
        -- answer is valid; the FIRST row of every question is chosen here
        -- so the audit -- which re-asks the same eight through the same
        -- procs -- matches on its first pass with no retry. ----
        t.exec("fillForm", t.player.inv_op, "current_affairs_form", 1)
        -- The objbox->options transition (leaving ~objbox's interface to
        -- mount the first p_choice3_header) cannot be crossed within one
        -- chat.play list -- ca_early.lua's own proof (ob() called separately
        -- from the questions list) splits here for the same reason
        -- documented in hunt.lua/biohazard.lua/seaslug.lua: a fresh options
        -- page needs its own call boundary right after an objbox.
        t.exec("fillForm-start", t.chat.play, { "*" }) -- objbox: You start looking through the questions on the form...
        t.exec("fillForm-questions", t.chat.play, {
            "choose:Pleasure.", -- Q1 "Main reason for making port at Catherby?"
            "choose:0", -- Q2 "How many hulls does your ship have?"
            "choose:Cargo spillage.", -- Q3 "Insured against cargo spillage and theft?"
            "choose:No.", -- Q4 "Does the first mate have first aid training?"
            "choose:Partial.", -- Q5 "Does the second mate have second aid training?"
            "choose:Yes.", -- Q6 "Any plague symptoms in the last two weeks?"
            "choose:Less than a month.", -- Q7 "Sailing experience?"
            "choose:Varrock.", -- Q8 "Home port?"
        })
        t.exec("fillForm-done", t.chat.play, { "*" }) -- objbox: Mercifully, it looks like the form is finished.
        local q1r, q1v = t.var.varbit("varb18290_current_affairs_form_q1")
        local q8r, q8v = t.var.varbit("varb18297_current_affairs_form_q8")
        t.check("form.filled", q1r == "ok" and q1v == 1 and q8r == "ok" and q8v == 1,
            string.format("q1: %s %s, q8: %s %s", tostring(q1r), tostring(q1v), tostring(q8r), tostring(q8v)))

        -- ---- Hand the filled form back: one continuous conversation, no
        -- second click, straight through to Arhein-mayor (ca_councillor_
        -- form -> ca_councillor_form_taken, currentaffairs.rs2:671-727). The
        -- transcript's "Yes, I have it here." line is printed twice. ----
        t.exec("goto-handInForm", t.player.goto_tile, 2825, 3454, 0)
        t.exec("handInForm", t.player.talk_to, "current_affairs_councillor", 1)
        t.exec("handInForm-dialog", t.chat.play, {
            "player:Hello again.",
            "npc:Have you fully filled in that form?",
            "choose:Yes, I have it here.",
            "player:Yes, I have it here.",
            "*", -- objbox: Councillor Catherine looks over the form.
            "player:Yes, I have it here.",
            "*", -- objbox: Councillor Catherine takes the completed form.
            "npc:Thank you very much. However, I must inform you",
            "player:Okay... I don't actually really care about the form though.",
            "npc:Then what is it I can help you with?",
            "player:That by-law about non-humans... I want to get it changed.",
            "npc:Any change to the by-laws can only occur with approval from the Mayor of Catherby.",
            "player:Right, we're getting somewhere. Where can I find the mayor?",
            "npc:I'm afraid that due to the Catherby Data Protection Regulation",
            "player:Are you serious?",
            "npc:Very serious. Now, if that's all",
            "npc:We hope your experience was a positive one",
            "player:Yeah... I think I'll pass on that.",
            "npc:Then I wish you a very good day.",
        })
        t.expect("quest.stage.arhein_mayor", t.quest.expect_stage("arhein_mayor"))

        -- ---- Arhein: "Long live the Mayor" verbatim story, sends the
        -- player to Harry (ca_arhein_mayor_story, currentaffairs.rs2:476). ----
        t.exec("goto-talkToArheinMayor", t.player.goto_tile, 2803, 3430, 0)
        t.exec("talkToArheinMayor", t.player.talk_to, "arhein", 1)
        t.exec("talkToArheinMayor-dialog", t.chat.play, {
            "npc:Hello again!",
            "choose:I need to find the Mayor of Catherby.",
            "player:I need to find the Mayor of Catherby.",
            "npc:The mayor? Oh no...",
            "player:What?",
            "npc:I'm afraid to say that the mayor died a few days ago.",
            "player:Oh... I'm sorry to hear that.",
            "npc:Don't worry, it happens all the time.",
            "player:It does?",
            "npc:Why do you need the mayor anyway?",
            "player:Apparently only the mayor can approve",
            "npc:Really? I don't recall that ever stopping Catherine before",
            "player:Meaning?",
            "npc:We're going to need another mayor",
            "player:Where on Gielinor will we get another mayor?",
            "npc:Well, the Mayor of Catherby has been, for a long time now, a fish.",
            "player:A fish?",
            "npc:Actually, to be precise, it has been 26 different fish",
            "player:I'm not sure this democracy thing",
            "npc:Well, unfortunately no one in Catherby really wanted the job.",
            "player:Councillor Catherine?",
            "npc:Councillor Catherine.",
            "player:So the answer was a fish?",
            "npc:It seemed like a good idea at the time.",
            "npc:We were lucky that a different by-law",
            "player:Of course. And as a result",
            "npc:Exactly!",
            "player:Right...",
            "player:I'll help you replace the fish.",
            "npc:A wise choice. Go and see Harry at the fishing shop.",
        })
        t.expect("quest.stage.get_mayor", t.quest.expect_stage("get_mayor"))

        -- ---- Harry: buy the mayoral election kit (ca_harry_talk,
        -- currentaffairs.rs2:878-932). ----
        t.exec("goto-talkToHarry", t.player.goto_tile, 2834, 3445, 0) -- harry.spawn (m44_53.spawn)
        t.exec("talkToHarry", t.player.talk_to, "harry", 1)
        t.exec("talkToHarry-dialog", t.chat.play, {
            "npc:Welcome! If you're looking for fishing equipment, you're in the right place.",
            "choose:I'm here about the mayor.",
            "player:I'm here about the mayor.",
            "npc:What about the mayor?",
            "player:Arhein said you could help me get a replacement.",
            "npc:Oh, we've lost another one have we?",
            "player:Afraid so.",
            "npc:Well, it's easy to fix. You just need a special mayoral fishbowl and a tiny net.",
            "player:Where can I get the fishbowl and net?",
            "npc:I can sell you the whole kit for 50 coins.",
            "choose:Yes.",
            "*", -- doubleobjbox: You buy a mayoral election kit from Harry for 50 coins.
            "npc:There you go. Once you've caught the mayor, be sure to take him to Arhein",
        })
        local coins_result, coins_count = t.inv.count("coins")
        local bowl_result, bowl_count = t.inv.count("current_affairs_mayoral_fishbowl")
        local net_result, net_count = t.inv.count("tiny_net")
        t.check("kit.received", coins_count == 0 and bowl_count == 1 and net_count == 1,
            string.format("coins=%s(%s) fishbowl=%s(%s) tiny_net=%s(%s)",
                tostring(coins_count), tostring(coins_result),
                tostring(bowl_count), tostring(bowl_result),
                tostring(net_count), tostring(net_result)))

        -- ---- Catch the Mayor of Catherby from the aquarium
        -- ([oploc1,aquarium], currentaffairs.rs2:955) ----
        t.exec("catchMayor", t.player.click_loc, "aquarium", 1)
        t.exec("catchMayor-box", t.chat.play, { "*" }) -- objbox: You wave the net around and you catch a tiny mayorfish!
        local mayor_result, mayor_detail = t.inv.await("current_affairs_mayor_of_catherby", 1, 10)
        t.check("mayor.received", mayor_result == "ok",
            "inv.await(current_affairs_mayor_of_catherby,1,10) -> " .. tostring(mayor_result) .. " " .. tostring(mayor_detail))

        -- ---- Show the mayor to ARHEIN FIRST: this is what chains him and
        -- advances the stage to ^ca_show_mayor (ca_arhein_chain_mayor,
        -- currentaffairs.rs2:510-524) -- Catherine's audit refuses an
        -- unchained mayor, so this leg is not optional. ----
        t.exec("goto-showArheinMayor", t.player.goto_tile, 2803, 3430, 0)
        t.exec("showArheinMayor", t.player.talk_to, "arhein", 1)
        t.exec("showArheinMayor-dialog", t.chat.play, {
            "npc:Hello again!",
            "choose:About the mayor...",
            "player:About the mayor...",
            "npc:Yes?",
            "*", -- objbox: You show Arhein the mayor.
            "player:The mayor has returned.",
            "npc:Long live the mayor.",
            "player:So what now?",
            "npc:Well, I think I'll start keeping a stock of mayors on standby",
            "player:That seems like a good idea, but I meant about the by-law.",
            "npc:Ah, of course. Let me just give him his mayoral chain...",
            "*", -- objbox: Arhein places a small chain in the bowl.
            "npc:There! Catherine shouldn't be able to stop us getting the by-law changed now.",
            "player:I'll head over there right away.",
        })
        t.expect("quest.stage.show_mayor", t.quest.expect_stage("show_mayor"))

        -- ---- Catherine's audit: the generic "How can I help you today?"
        -- greeting first (currentaffairs.rs2:618, reached for every stage in
        -- [arhein_mayor..sign]), then ca_councillor_audit (:733) re-asks all
        -- 8 questions through the SAME two procs the form used -- answering
        -- the same first row again matches every one, so
        -- ~ca_audit_all_correct is true on this pass and Catherine's
        -- ^ca_sign / form-7r4-5h hand-over lands in the same conversation. ----
        t.exec("goto-doAudit", t.player.goto_tile, 2825, 3454, 0)
        t.exec("doAudit", t.player.talk_to, "current_affairs_councillor", 1)
        t.exec("doAudit-dialog", t.chat.play, {
            "npc:Welcome to the Catherby Council Office. Your views are important to us. How can I help",
            "player:I have the mayor!",
            "*", -- objbox: You show Councillor Catherine the mayor.
            "npc:Oh, how wonderful.",
            "player:So, shall we get to work on getting that by-law changed?",
            "npc:I'm afraid a recent audit has exposed irregularities",
            "player:Here we go...",
            "npc:Unfortunately, as you yourself have recently filed a form",
            "player:What if I've forgotten what I put down?",
            "npc:That seems unlikely.",
            "player:I am shocked that you would suggest otherwise.",
            "npc:Very good. In that case, let us begin.",
            "npc:What is your main reason for making port at Catherby?",
            "choose:Pleasure.",
            "npc:How many hulls does your ship have?",
            "choose:0",
            "npc:Is your ship insured against cargo spillage and theft?",
            "choose:Cargo spillage.",
            "npc:Does your first mate have first aid training?",
            "choose:No.",
            "npc:Does your second mate have second aid training?",
            "choose:Partial.",
            "npc:Have you experienced symptoms of plague in the last two weeks?",
            "choose:Yes.",
            "npc:How much experience do you have sailing?",
            "choose:Less than a month.",
            "npc:Where would you say your home port is?",
            "choose:Varrock.",
            "npc:Hmm, that all seems to be right...",
            "player:Can we finally get on with this by-law change now?",
            "npc:*sigh* Yes, I suppose everything is in order. I just need this form signing by the mayor.",
            "*", -- objbox: Councillor Catherine hands you a form.
            "player:Right... How does the mayor sign forms usually, given his lack of hands?",
            "npc:Simply dip the form into his bowl and he will give it a nibble.",
            "player:Fair enough.",
        })
        t.expect("quest.stage.sign", t.quest.expect_stage("sign"))
        local form2_result, form2_detail = t.inv.await("current_affairs_form_2", 1, 10)
        t.check("form2.received", form2_result == "ok",
            "inv.await(current_affairs_form_2,1,10) -> " .. tostring(form2_result) .. " " .. tostring(form2_detail))

        -- ---- The mayor signs form 7r4-5h ([opheldu,current_affairs_form_2]
        -- -> ca_sign_form, currentaffairs.rs2:856-869) ----
        t.exec("signForm", t.player.use_item_on_item, "current_affairs_form_2", "current_affairs_mayor_of_catherby")
        t.exec("signForm-box", t.chat.play, { "*" }) -- doubleobjbox: The mayor eagerly signs the form for you...
        local signed_result, signed_detail = t.inv.await("current_affairs_form_2_signed", 1, 10)
        t.check("form.signed", signed_result == "ok",
            "inv.await(current_affairs_form_2_signed,1,10) -> " .. tostring(signed_result) .. " " .. tostring(signed_detail))

        -- ---- Hand the signed form back to Catherine: the by-law is
        -- amended (ca_councillor_sign, currentaffairs.rs2:760-767). ----
        t.exec("showCatherineForm", t.player.talk_to, "current_affairs_councillor", 1)
        t.exec("showCatherineForm-dialog", t.chat.play, {
            "npc:Welcome to the Catherby Council Office. Your views are important to us. How can I help",
            "player:I have that signed form.",
            "*", -- objbox: Councillor Catherine takes the completed form and stamps it.
            "npc:The by-law has now been amended. Good day.",
        })
        t.expect("quest.stage.news", t.quest.expect_stage("news"))

        -- ---- Tell Arhein the by-law changed, receive the Current duck
        -- (^ca_news branch, currentaffairs.rs2:429-449 -- unchanged by this
        -- parity pass, already verbatim). ----
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
        -- sail east: no ca_board_ship dialogue/p_telejump at all -- the
        -- ripple is the hull-queued sea zone [zone,0_44_53_16_24]
        -- (currentaffairs.rs2:982), gated on the player's OWN hull
        -- (ca_own_boat_here/ca_boat_on_ripple, currentaffairs.rs2:206-223),
        -- never the rider's tile. Untouched by this parity pass. ----
        t.exec("goto-gangplank", t.player.goto_tile, 2797, 3413, 0)
        t.exec("boardShip", t.sail.board, "sailing_gangplank_catherby")
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
        -- (currentaffairs.rs2:232-251) spawns a REAL moving npc
        -- (sailing_charting_current_duck_moving) that swims five
        -- ocean-checked current legs to the wiki's shore pin near Holgart
        -- (^ca_duck_end, 2802,3322) over an [ai_timer], then becomes the
        -- stopped collectible form with the wiki's blue stop message --
        -- the guide's own releaseDuck step, driven for real. (Quest
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
        -- (currentaffairs.rs2:1101), gated on npc_owner/ca_duck_released/
        -- stage -- the guide's own "collect the duck" step, driven through
        -- the sailing side panel's deck-row press. ----
        t.exec("collect.helm_off", t.sail._press_deck_row, "Navigate", "Helm")
        t.ticks(2)
        t.exec("collectDuck", t.sail._press_deck_row, "Collect", "Current duck", 16, 12)
        t.exec("collectDuck.box", t.chat.play, { "*" }) -- ~objbox(duck, "The duck recognises your boat...")
        local chart_bit_result, chart_bit_value = t.var.varbit("varb18602_sailing_charting_current_duck_catherby_bay_complete")
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
        -- lost-mayor branch never fires. Unchanged by this parity pass. ----
        t.exec("goto-showCurrentsArhein", t.player.goto_tile, 2803, 3430, 0)
        t.exec("showCurrentsArhein", t.player.talk_to, "arhein", 1)
        t.exec("showCurrentsArhein-dialog", t.chat.play, {
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
