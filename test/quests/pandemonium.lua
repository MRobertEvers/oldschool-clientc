-- Pandemonium, end to end in RSProx index 4407's order (2026-10-29, rev 241).
-- content: OSRS-Content/osrs-content/server/scripts/quests/quest_pandemonium/
-- audit:   docs/rsprox-analysis/quests/pandemonium.md
--
-- Every page row below is the recording's text (a name-bearing page is
-- matched on the text before the name). The sea legs: Will and Anne's boat
-- from Port Sarim to the wreck (stopped where 4407 stopped it, 3038,3029),
-- then the raft both ways on Prying Times' measured route.

local STAGE = "varb18314_sailing_intro"

return {
    id = "pandemonium",
    fixture = "fresh_lumbridge.ini",
    max_frames = 250000,
    setup = {
        "::clearinv",
        "::pandemonium",
        "::goto 3024 3207 0",
    },

    run = function(t)
        t.ticklog.start()

        local function stage(name, want)
            local r, v = t.var.server(STAGE)
            t.check("stage." .. name, r == "ok" and v == want,
                STAGE .. " = " .. tostring(v) .. " (" .. tostring(r) .. "), want " .. want)
        end
        local function aboard(name, want)
            local r, s = t.sail.state()
            local is = r == "ok" and type(s) == "table" and s.aboard == true
            t.check("aboard." .. name, is == want, "aboard=" .. tostring(is) .. " want " .. tostring(want))
        end

        -- ---- Port Sarim: the interview [200]..[325] ----
        t.exec("will", t.player.talk_to, "sailing_intro_will_sarim", 1)
        t.exec("interview", t.chat.play, {
            "npc:Ah, look what we have here, Will!",
            "player:What?",
            "npc:Goodness, Anne, I think you're right!",
            "player:But I don't need a...",
            "npc:Well, let's not waste any more time! Stranger, are you ready for your interview?",
            "choose:Yes.",
            "player:I guess so...?",
            "npc:Let's get you started with a nice easy question: What is your name?",
            "*",
            "npc:Pleased to meet you, ",
            "npc:Next question: Do you have any experience captaining a ship?",
            "player:Not really...",
            "npc:Oh...",
            "npc:Will, it's important we not be too picky!",
            "npc:Okay, final question:",
            "player:Is that likely to happen?",
            "npc:Not at all!",
            "player:I'm not sure if I...",
            "npc:Excellent! Well then, ",
            "npc:Congratulations!",
            "player:Thanks... only, you didn't actually mention what the role was...",
            "npc:Ah, how forgetful of us!",
            "player:But I didn't accept anything...",
            "npc:Just let us know when you're ready, and we'll hop aboard!",
            "choose:I guess I'm ready...",
            "player:I guess I'm ready...",
            "npc:Then let us away!",
        })
        local function await_page(name, ticks)
            local waited = t.await({
                level = function() return t.chat.kind() ~= "none" end,
                note = "the next page",
            }, ticks or 20)
            t.check(name .. ".page", waited, "a page opened: " .. tostring(waited))
        end
        -- Aboard on T+1, the black lifts on T+2, Will speaks on T+3 ([327]).
        await_page("briefing")
        t.exec("briefing", t.chat.play, {
            "npc:Lovely! The open sea!",
            "player:It's not really that open...",
            "npc:Well, good news, ",
            "player:Helm?",
            "npc:It means you're in charge of the ship's helm!",
            "player:It's more of a boat really...",
            "npc:Well, we all have to start somewhere!",
            "player:Right... So if I'm manning the helm, what will you two be doing?",
            "npc:Well, it's simple, isn't it? We're in charge!",
            "player:Have you ever actually done this before?",
            "npc:Many times!",
            "npc:Just never on our own ship before!",
            "player:And what does this business do?",
            "npc:We salvage shipwrecks of course!",
            "player:Of course. Alright then,",
            "npc:We've got a good one to get us started!",
            "player:The Pandemonium?",
            "npc:Have you never been?",
            "npc:But first, let's collect some salvage!",
            "npc:Over to you, ",
        })
        stage("take_helm", 8)
        aboard("intro_boat", true)

        -- ---- The helm and the sea [473]..[826] ----
        t.exec("helm", t.sail.helm, "Helm")
        stage("sailing", 10)
        -- The sidepanel lists no sail rows for config 4 yet (sailing agent):
        -- the deck loc's own "Set" (cache op2) sets them.
        t.exec("sails", t.sail._press_deck_row, "Set", "Sails")
        t.ticks(3)
        -- Working the sails walks the player off the helm: take it again.
        t.exec("helm.again", t.sail.helm, "Helm")
        t.drive.camera(1024, 383, 900)
        -- Out past the pier post at 3048..3049,3186..3187 first (Prying
        -- Times' leg7a, reversed), then south.
        t.exec("wreck.leg0", t.sail.sail_to, 3052, 3181, 2, 200)
        t.exec("wreck.leg1", t.sail.sail_to, 3044, 3181, 3, 300)
        t.exec("wreck.leg2", t.sail.sail_to, 3043, 3158, 3, 300)
        t.exec("wreck.leg3", t.sail.sail_to, 3037, 3105, 3, 400)
        t.exec("wreck.leg4", t.sail.sail_to, 3043, 3051, 3, 400)
        t.exec("wreck.leg5", t.sail.sail_to, 3038, 3032, 3, 300)
        t.exec("wreck.stop", t.sail._press_deck_row, "Un-set", "Sails")
        await_page("wreck")
        t.exec("wreck", t.chat.play, {
            "npc:Right, now that we're here, I think it's time to do some salvaging!",
            "npc:When you're ready, ",
            "player:Hang on... why am I lowering the hook?",
            "npc:Yes! The helm... and the hook!",
            "player:Of course. I'll just do everything while you two reap the rewards.",
            "npc:Don't go getting grumpy on us now, ",
            "npc:That's right! Think of what you'll learn today!",
            "player:*sigh*",
        })
        stage("hook", 14)

        -- ---- Overboard [854]..[929] ----
        t.drive.camera(0, 128, 600)
        t.exec("hook", t.sail._press_deck_row, "Deploy", "Salvaging hook")
        -- The scene holds the chatbox with empty, continue-less messageboxes
        -- between its pages ([855], [863], [876], [885], [903], [905]): wait
        -- each one out before the next page.
        local function await_scene_page(name, ticks)
            local waited = t.await({
                level = function()
                    local k = t.chat.kind()
                    return k ~= "none" and k ~= "mesbox"
                end,
                note = name,
            }, ticks)
            t.check(name .. ".page", waited, "a page after the scene's hold: " .. tostring(waited))
        end
        await_scene_page("overboard.hook", 30)
        t.exec("overboard.hook", t.chat.play, { "player:Right then, let's give this a go." })
        await_scene_page("overboard.found", 20)
        t.exec("overboard.found", t.chat.play, { "*", "npc:Well?" })
        await_scene_page("overboard.betrayal", 20)
        t.exec("overboard.betrayal", t.chat.play, {
            "player:What do you make of these?",
            "*",
            "npc:The medallion! Anne, we've found it!",
            "player:You were looking for this?",
            "npc:That's right! And now that we've found it,",
            "player:What?",
            "npc:Sorry, ",
        })
        await_scene_page("overboard.beach", 40)
        t.exec("overboard.beach", t.chat.play, { "player:Eugh...", "player:Where am I?" })
        await_scene_page("overboard.steve", 40)
        t.exec("overboard.steve", t.chat.play, {
            "npc:Yo ho ho and a Jolly Roger to you, me hearty!",
            "player:I think I must be having some sort of fever dream...",
            "npc:Ha! Far from it, matey!",
            "player:Okay... You said words there,",
            "npc:Ah, me wee hearty!",
            "player:Pandemonium?",
            "npc:Aye! To be sure!",
            "player:If this is the Pandemonium, do you know of a pair of salvagers called Will and Anne?",
            "npc:Avast ye! You name a pair of pinching looting plundering seadogs!",
            "player:I see. So you do know them.",
            "npc:Purchased?! Purchased?!",
            "player:Well, I had quite the fun run in with them myself.",
            "npc:Arrr, you poor scallywag.",
            "player:Thank you. It was the map they stole",
            "npc:Not a lot, I'm afraid.",
        })
        stage("see_ribs", 20)
        aboard("ashore", false)
        t.expect("cup", t.inv.await("sailing_intro_cup", 1, 5))

        -- ---- Ribs [1061]..[1082] ----
        t.exec("ribs", t.player.talk_to, "sailing_intro_ribs", 1)
        t.exec("ribs-dialog", t.chat.play, {
            "npc:Ahoy, young whippersnapper. Have you come to hear a tale from old Ribs?",
            "player:Kind of, I guess.",
            "npc:A dark day. A very dark day.",
            "player:That being?",
            "npc:A curse! A curse most foul!",
            "player:Barnacle Barnaby? Who's that then?",
            "npc:You have not heard the tale of Barnacle Barnaby?",
            "choose:Now's not really the time for tales.",
            "player:Now's not really the time for tales.",
            "npc:You are a fool for thinking that way,",
            "player:I see. Well, I'm afraid Will and Anne found and looted the Kraken.",
            "npc:I feared as much,",
            "player:They did. It was actually the only thing they were interested in.",
            "npc:Then the foul curse is now free once more. They must be stopped!",
            "player:Well, I do have my own personal reasons",
            "npc:You are a brave soul indeed,",
            "npc:You should talk to Steve.",
        })
        stage("ribs_told", 22)

        -- ---- Steve again [1094]..[1122] ----
        t.exec("walk-steve", t.player.walk_to, 3050, 2968, 40)
        t.exec("steve", t.player.talk_to, "steve_beanie", 1)
        t.exec("steve-dialog", t.chat.play, {
            "npc:Yarr! What will it be?",
            "choose:About Will and Anne...",
            "player:About Will and Anne...",
            "npc:Those plundering seadogs! Did you speak to Ribs about them?",
            "player:I did. He seems to think they might have unleashed some sort of curse.",
            "npc:Ah, the curse of Barnacle Barnaby!",
            "player:Well, myths aside,",
            "npc:Yo ho ho! A mighty good decision!",
            "choose:What exactly is this one rule you keep mentioning?",
            "player:What exactly is this one rule you keep mentioning?",
            "npc:Avast! The mainland kingdoms have no authority here.",
            "npc:We don't care who you are.",
            "player:So anyone is welcome here",
            "npc:Yarr! You've got it!",
            "player:I see. So, back to Will and Anne.",
            "npc:Shiver me timbers! I'm afraid not.",
            "player:And where would I find Old Grog?",
            "npc:Ah, the old scallywag left not that long ago,",
            "player:But I don't have a vessel...",
            "npc:What? But then how did you get here?",
            "player:Will and Anne threw me overboard",
            "npc:Blistering barnacles!",
            "player:Alright, I'll go and see him.",
            "npc:Jolly Roger!",
        })
        stage("see_jim", 24)

        -- ---- Junior Jim and the Shipyard [1132]..[1290] ----
        t.exec("jim", t.player.talk_to, "junior_jim", 1)
        t.exec("jim-dialog", t.chat.play, {
            "npc:Can I help you, mate?",
            "player:Are you Junior Jim?",
            "npc:That's me. What do you need then?",
            "player:Steve said you might be able to sort me out with a boat.",
            "npc:You don't have a boat?",
            "player:No.",
            "npc:Ha! We've got ourselves a Billy-no-boats!",
            "player:How much?",
            "npc:Well, what do you have?",
            "player:Er... I do have this cup I found.",
            "npc:Alright, let's have a gander at it.",
            "*",
            "npc:Hmm... Yeah, yeah, I can work with this.",
            "player:Sorry, I think I must have misheard you.",
            "npc:That's right! The best one I have!",
            "player:I was kind of hoping for a bit more than a raft.",
            "npc:Sorry, mate, but even if you could,",
            "player:Okay...",
            "npc:Lighten up, mate.",
            "npc:Now, about your raft...",
            "player:But I won't get anywhere without sails!",
            "npc:Don't panic, you silly plonker.",
            "player:What favour?",
            "npc:I've got some cargo I need picking up from Port Sarim",
            "npc:Oh, and I tell you what,",
            "player:Thanks!",
            "npc:Now, hand over that cup and we can get cracking.",
            "*",
            "npc:Right, time to sort you out with a cargo hold.",
            "npc:You'll want to grab yourself some tools from the crates over there, mate.",
        })
        stage("cup_given", 28)
        t.exec("saws", t.player.click_loc, "crate_saws", 1)
        t.exec("saws.box", t.chat.play, { "*" })
        t.exec("hammers", t.player.click_loc, "crate_hammers", 1)
        t.exec("hammers.box", t.chat.play, { "*" })
        t.exec("yard.board", t.player.click_loc, "sailing_gangplank_shipyard", 1)
        aboard("raft_in_yard", true)
        t.exec("yard.hotspot", t.sail.open_hotspot, 0, "sailing_boat_facility_placeholder_raft_0")
        t.expect("yard.catalogue", t.ui.await_open("sailing_customisation", 5))
        -- The build is graded by what it did (the stage and Jim's page): the
        -- driver reads Jim's page arriving as the build's own answer.
        local build_r, build_d = t.sail.build("sailing_boat_facility_cargo_hold_regular_raft", "Cargo hold")
        t.note("yard.build -> " .. tostring(build_r) .. " " .. tostring(build_d))
        await_page("yard.built")
        t.exec("yard.built", t.chat.play, { "npc:Very nice! Now, use the gangplank to hop back up here" })
        stage("hold_built", 30)
        t.exec("yard.off", t.sail.disembark, "sailing_gangplank_shipyard_disembark")
        aboard("yard_pier", false)
        t.exec("jim.yard", t.player.talk_to, "junior_jim_shipyard", 1)
        -- [1276]..[1279]: his page closes for the fade home; the next opens there.
        t.exec("jim-home", t.chat.play, { "npc:Smashing job on that cargo hold, mate." })
        await_page("jim-home", 20)
        t.exec("jim-log", t.chat.play, {
            "npc:Okay, looks like that's you all sorted, mate.",
            "player:How exciting!",
            "npc:Why don't you take your new raft out for a spin?",
            "*",
            "npc:Your very own captain's log!",
            "player:Like collecting your cargo?",
            "npc:Right on, mate!",
            "*",
            "npc:And that's you all ready to go, mate!",
            "npc:Now, unless you fancy a cheeky dip,",
            "*",
        })
        stage("task_given", 34)

        -- ---- The raft: Jim's cargo [1313]..[1783] ----
        t.exec("raft.toPlank", t.player.walk_to, 3068, 2987, 60)
        t.exec("raft.board", t.sail.board, "sailing_gangplank_the_pandemonium")
        t.ticks(2)
        stage("raft_boarded", 36)
        t.exec("raft.helm", t.sail.helm, "Helm")
        t.exec("raft.sails", t.sail.sails, true)
        t.drive.camera(0, 383, 900)
        t.exec("out.leg1", t.sail.sail_to, 3082, 2984, 2, 200)
        t.exec("out.leg2", t.sail.sail_to, 3082, 3012, 2, 300)
        t.exec("out.leg3", t.sail.sail_to, 3043, 3051, 3, 400)
        t.exec("out.leg4", t.sail.sail_to, 3037, 3105, 3, 400)
        t.exec("out.leg5", t.sail.sail_to, 3043, 3158, 3, 400)
        t.exec("out.leg6", t.sail.sail_to, 3044, 3181, 3, 300)
        t.exec("out.leg7a", t.sail.sail_to, 3052, 3181, 2, 200)
        t.exec("out.leg7", t.sail.sail_to, 3057, 3189, 2, 300)
        t.exec("out.furl", t.sail.sails, false)
        t.exec("out.disembark", t.sail.disembark, "sailing_gangplank_port_sarim")
        t.ticks(2)
        stage("at_sarim", 38)
        t.exec("cargo.take", t.sail.cargo_take, "dock_loading_bay_ledger_table_port_sarim", 3)
        t.exec("cargo.board", t.sail.board, "sailing_gangplank_port_sarim")
        t.ticks(2)
        stage("cargo_aboard", 40)
        t.exec("cargo.load", t.sail.cargo_load, "cargo hold")
        t.exec("back.helm", t.sail.helm, "Helm")
        t.exec("back.sails", t.sail.sails, true)
        t.drive.camera(1024, 383, 900)
        t.exec("back.leg1", t.sail.sail_to, 3058, 3170, 3, 300)
        t.exec("back.leg2", t.sail.sail_to, 3043, 3158, 3, 300)
        t.exec("back.leg3", t.sail.sail_to, 3037, 3105, 3, 400)
        t.exec("back.leg4", t.sail.sail_to, 3043, 3051, 3, 400)
        t.exec("back.leg5", t.sail.sail_to, 3082, 3012, 3, 400)
        t.exec("back.leg6", t.sail.sail_to, 3082, 2986, 2, 300)
        t.exec("back.leg7", t.sail.sail_to, 3074, 2984, 1, 200)
        t.exec("back.furl", t.sail.sails, false)
        t.exec("cargo.withdraw", t.sail.cargo_withdraw, "cargo hold")
        t.exec("back.disembark", t.sail.disembark, "sailing_gangplank_the_pandemonium")
        t.ticks(2)
        stage("back", 42)
        t.drive.camera(0, 128, 600)
        t.exec("cargo.deliver", t.sail.cargo_deliver, "dock_loading_bay_ledger_table_pandemonium", 10)
        await_page("cargo.delivered")
        t.exec("cargo.delivered", t.chat.play, { "*" })
        stage("delivered", 44)

        -- ---- Jim, Old Grog, Steve [1790]..[1875] ----
        t.exec("jim.paid", t.player.talk_to, "junior_jim", 1)
        t.exec("jim-paid", t.chat.play, {
            "npc:Alright, mate.",
            "player:I collected your cargo and left it with the port master.",
            "npc:Very nice! That's us all squared up then.",
            "*",
            "player:I might just do that.",
            "npc:To be honest, mate,",
            "*",
            "player:Sounds good. Anything else?",
            "npc:Well, now that you're officially a big cheese captain,",
            "npc:Oh, and while we're on about charting,",
            "npc:First up, I've left you a couple of repair kits in there.",
            "npc:The other thing in there is a spyglass.",
            "npc:There's a chap called Charles",
            "*",
            "player:Great! Thanks, Jim.",
            "npc:Don't mention it, mate.",
        })
        stage("see_steve", 46)
        t.exec("walk-grog", t.player.walk_to, 3048, 2970, 40)
        t.exec("grog", t.player.talk_to, "sailing_intro_grog", 1)
        t.exec("grog-dialog", t.chat.play, {
            "npc:Is this the one?",
            "npc:Aye! This Jolly Roger here",
            "*",
            "*",
            "player:Pleased to meet you.",
            "npc:Steve tells me you're keen",
            "player:I won't let you down!",
            "npc:We'll see.",
            "npc:It will take me some time to track Will and Anne down,",
            "player:You have my word.",
            "npc:Good. Now, that's me done drinking.",
            "mesbox:Old Grog departs.",
            "mesbox:Old Grog departs.",
            "npc:Yo ho ho, ",
            "player:Will do!",
            "npc:To help you on your way,",
            "npc:Oh, and don't forget to pop back for a drink",
        })
        stage("complete", 50)
        t.expect("scroll", t.ui.await_open("questscroll", 5))
        t.expect("coupons", t.inv.await("sawmill_coupon", 25, 5))
    end,
}
