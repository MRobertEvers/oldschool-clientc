-- The Ribbiting Tale of a Lily Pad Labour Dispute. Spec: Quest Helper ladder + ribbitingtale.rs2.
return {
    id = "ribbitingtale",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel woodcutting 15",
        "::complete quest_childrenofthesun",
        -- Brought along for Cuthbert, Lord of Dread (Quest Helper
        -- getCombatRequirements, TheRibbitingTaleOfALilyPadLabourDispute.java:178-181).
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::setlevel defence 40",
        "::setlevel hitpoints 40",
        "::give mithril_scimitar 1",
        "::wield mithril_scimitar",
        "::give lobster 5",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb9844_frog_quest",
            constants = {
                not_started = 0, talk_blue = 2, marcellus2 = 4, gary_leader = 6, yellow = 8,
                chop = 10, sabotage = 12, hopoff = 14, after_hop = 16, marcellus3 = 18,
                blame = 20, chest = 22, plant = 24, cuthbert = 26, marcellus_end = 28,
                gary_end = 30, complete = 32,
            },
            display = "The Ribbiting Tale of a Lily Pad Labour Dispute",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("goto-preQuestFrogs", t.player.goto_tile, 1694, 2993, 0)
        for _, e in ipairs({ { "frog_quest_gary", "Frog", 12937 }, { "frog_quest_sue", "Frog", 12945 },
            { "frog_quest_dave", "Frog", 12941 }, { "frog_quest_jane", "Frog", 12949 } }) do
            local r, row = t.npc.nearest(e[1], 20)
            local ok = r == "ok" and type(row) == "table" and row.name == e[2] and row.npc_id == e[3]
            local d = tostring(r) .. " " .. tostring(row)
            if type(row) == "table" then
                d = string.format("%s -> name=%s npc_id=%s at %s,%s (want %s/%d)", e[1],
                    tostring(row.name), tostring(row.npc_id), tostring(row.x), tostring(row.z), e[2], e[3])
            end
            t.check("names.stage0." .. e[1], ok, d)
        end
        t.exec("goto-talkToMarcellus", t.player.goto_tile, 1683, 2973, 0)
        for _, e in ipairs({ { "frog_quest_marcellus", "Marcellus", 12935 } }) do
            local r, row = t.npc.nearest(e[1], 20)
            local ok = r == "ok" and type(row) == "table" and row.name == e[2] and row.npc_id == e[3]
            local d = tostring(r) .. " " .. tostring(row)
            if type(row) == "table" then
                d = string.format("%s -> name=%s npc_id=%s at %s,%s (want %s/%d)", e[1],
                    tostring(row.name), tostring(row.npc_id), tostring(row.x), tostring(row.z), e[2], e[3])
            end
            t.check("names.stage0." .. e[1], ok, d)
        end
        t.exec("talkToMarcellus", t.player.talk_to, "frog_quest_marcellus")
        t.exec("talkToMarcellus.chat", t.chat.play, {
            "player:Frogs? What are you on about?",
            "npc:play dumb",
            "player:I promise I have no idea",
            "npc:those frogs on the other side",
            "options",
            "choose:I see... How about I head over there and take a look?",
            "player:I see... How about I head over there",
            "npc:You want to go near them?",
            "player:Okay then...",
        })
        t.expect("quest.stage.talk_blue", t.quest.expect_stage("talk_blue"))

        t.exec("goto-talkToBlueFrogs", t.player.goto_tile, 1694, 2996, 0)
        for _, e in ipairs({ { "frog_quest_gary", "Frog", 12938 }, { "frog_quest_sue", "Frog", 12946 } }) do
            local r, row = t.npc.nearest(e[1], 20)
            local ok = r == "ok" and type(row) == "table" and row.name == e[2] and row.npc_id == e[3]
            local d = tostring(r) .. " " .. tostring(row)
            if type(row) == "table" then
                d = string.format("%s -> name=%s npc_id=%s at %s,%s (want %s/%d)", e[1],
                    tostring(row.name), tostring(row.npc_id), tostring(row.x), tostring(row.z), e[2], e[3])
            end
            t.check("names.stage2." .. e[1], ok, d)
        end
        t.exec("talkToBlueFrogs", t.player.talk_to, "frog_quest_gary")
        t.exec("talkToBlueFrogs.chat", t.chat.play, {
            "npc:Oh, hello there.",
            "player:Hang on... You can talk?",
            "npc:Well of course we can talk",
            "npc:I'm Sue and this is Gary.",
            "player:Pleased to meet you.",
            "npc:what can we do for you?",
            "player:there's a guy over there",
            "npc:We're just on strike.",
            "player:He seems to think",
            "npc:What a load of nonsense!",
            "player:something about killing you all",
            "npc:He wants to what?!",
            "player:Wait! How about peace?",
            "npc:I suppose peace could be fun.",
            "player:how about I go and ask him",
            "npc:Well that just sounds swell!",
            "player:Okay, I'll be back shortly.",
        })
        t.expect("quest.stage.marcellus2", t.quest.expect_stage("marcellus2"))

        t.exec("goto-talkToMarcellus2", t.player.goto_tile, 1683, 2973, 0)
        t.exec("talkToMarcellus2", t.player.talk_to, "frog_quest_marcellus")
        t.exec("talkToMarcellus2.chat", t.chat.play, {
            "npc:The frogs didn't kill you?",
            "player:open to engaging in some peace talks",
            "npc:The hypocrisy!",
            "player:But what exactly have they done",
            "npc:Look, I can't remember.",
            "npc:My brain!",
            "player:So you'd be willing to negotiate",
            "npc:I don't negotiate with monsters!",
            "player:I don't think they're monsters.",
            "npc:Well do they have a leader?",
            "player:I'm not sure.",
            "npc:I'll only negotiate with their leader.",
            "player:Fine. I'll go and ask them about their leader.",
        })
        t.expect("quest.stage.gary_leader", t.quest.expect_stage("gary_leader"))

        t.exec("goto-talkToGary", t.player.goto_tile, 1694, 2996, 0)
        for _, e in ipairs({ { "frog_quest_gary", "Gary", 12939 }, { "frog_quest_sue", "Sue", 12947 } }) do
            local r, row = t.npc.nearest(e[1], 20)
            local ok = r == "ok" and type(row) == "table" and row.name == e[2] and row.npc_id == e[3]
            local d = tostring(r) .. " " .. tostring(row)
            if type(row) == "table" then
                d = string.format("%s -> name=%s npc_id=%s at %s,%s (want %s/%d)", e[1],
                    tostring(row.name), tostring(row.npc_id), tostring(row.x), tostring(row.z), e[2], e[3])
            end
            t.check("names.stage6." .. e[1], ok, d)
        end
        t.exec("talkToGary", t.player.talk_to, "frog_quest_gary")
        t.exec("talkToGary.chat", t.chat.play, {
            "npc:Have you found out what peace conditions",
            "player:Sort of.",
            "npc:We're more about collective leadership.",
            "player:Well would you be interested in having a leader?",
            "npc:There would need to be an election",
            "npc:the small matter of Cuthbert",
            "player:So you're saying Cuthbert can't win",
            "npc:The hop-off will see the candidates",
            "player:I guess I'm off to sabotage a lily pad.",
            "npc:Just be careful of Dave and Jane.",
        })
        t.expect("quest.stage.yellow", t.quest.expect_stage("yellow"))

        t.exec("goto-talkToYellowFrogs", t.player.goto_tile, 1696, 2982, 0)
        for _, e in ipairs({ { "frog_quest_dave", "Dave", 12943 }, { "frog_quest_jane", "Jane", 12951 } }) do
            local r, row = t.npc.nearest(e[1], 20)
            local ok = r == "ok" and type(row) == "table" and row.name == e[2] and row.npc_id == e[3]
            local d = tostring(r) .. " " .. tostring(row)
            if type(row) == "table" then
                d = string.format("%s -> name=%s npc_id=%s at %s,%s (want %s/%d)", e[1],
                    tostring(row.name), tostring(row.npc_id), tostring(row.x), tostring(row.z), e[2], e[3])
            end
            t.check("names.stage8." .. e[1], ok, d)
        end
        t.exec("talkToYellowFrogs", t.player.talk_to, "frog_quest_dave")
        t.exec("talkToYellowFrogs.chat", t.chat.play, {
            "npc:Have you heard about the election?",
            "player:I need to inspect the lily pads.",
            "npc:No can do.",
            "player:don't you think it would be wise for a third party",
            "npc:I really could do with a bit of a rest",
            "player:You said you were hungry?",
            "npc:Oh I'd kill for a nice tasty orange!",
            "player:An orange?",
            "npc:most people think we just eat flies.",
            "player:Where do you get your oranges?",
            "npc:They grow on trees around here",
            "npc:Just a shame we can't reach those ones...",
            "player:Hmm...",
        })
        t.expect("quest.stage.chop", t.quest.expect_stage("chop"))

        -- re-talk at 10 (wiki "Talking to Dave or Jane again"), from Jane's side
        t.exec("talkToYellowFrogs.again", t.player.talk_to, "frog_quest_jane")
        t.exec("talkToYellowFrogs.again.chat", t.chat.play, {
            "player:Remind me, where do you get your oranges?",
            "npc:They grow on trees around here",
            "npc:Just a shame we can't reach those ones...",
            "player:Hmm...",
        })
        t.expect("quest.stage.chop.held", t.quest.expect_stage("chop"))

        t.exec("goto-pickUpAxe", t.player.goto_tile, 1684, 2976, 0)
        t.exec("pickUpAxe", t.player.click_loc, "log_withaxe", 1)
        t.exec("pickUpAxe.held", t.inv.await, "bronze_axe", 1, 10)
        t.exec("goto-chopOrangeTree", t.player.goto_tile, 1695, 2981, 0)
        t.exec("chopOrangeTree", t.player.click_loc, "frog_quest_tree_op", 1)
        t.exec("chopOrangeTree.stage", t.var.await_server, "varb9844_frog_quest", 12, 20)
        t.expect("quest.stage.sabotage", t.quest.expect_stage("sabotage"))
        t.exec("talkToYellowFrogs.oranges", t.player.talk_to, "frog_quest_dave")
        t.exec("talkToYellowFrogs.oranges.chat", t.chat.play, { "npc:Can't stop to talk!" })

        t.exec("goto-sabotageLilyPad", t.player.goto_tile, 1692, 2984, 0)
        t.exec("sabotageLilyPad", t.player.click_loc, "frog_quest_lily_pad_destroyable_op", 1)
        t.exec("sabotageLilyPad.stage", t.var.await_server, "varb9844_frog_quest", 14, 20)
        t.expect("quest.stage.hopoff", t.quest.expect_stage("hopoff"))

        t.exec("goto-talkToGary2", t.player.goto_tile, 1694, 2996, 0)
        t.exec("talkToGary2", t.player.talk_to, "frog_quest_gary")
        t.exec("talkToGary2.chat", t.chat.drain, { max_pages = 20 })
        t.expect("quest.stage.marcellus3", t.quest.expect_stage("marcellus3"))
        t.exec("goto-yellowAfterHop", t.player.goto_tile, 1696, 2982, 0)
        t.exec("talkToYellowFrogs.afterHop", t.player.talk_to, "frog_quest_dave")
        t.exec("talkToYellowFrogs.afterHop.chat", t.chat.play, {
            "npc:Can you believe Cuthbert died?",
            "npc:Sue will be a great leader though.",
        })

        t.exec("goto-talkToMarcellus3", t.player.goto_tile, 1683, 2973, 0)
        t.exec("talkToMarcellus3", t.player.talk_to, "frog_quest_marcellus")
        t.exec("talkToMarcellus3.chat", t.chat.drain, { max_pages = 20 })
        t.expect("quest.stage.blame", t.quest.expect_stage("blame"))

        t.exec("goto-talkToGaryToBlame", t.player.goto_tile, 1694, 2996, 0)
        t.exec("talkToGaryToBlame", t.player.talk_to, "frog_quest_gary")
        t.exec("talkToGaryToBlame.chat", t.chat.drain, { max_pages = 10 })
        t.expect("quest.stage.chest", t.quest.expect_stage("chest"))

        t.exec("goto-openChest", t.player.goto_tile, 1676, 2975, 0)
        t.exec("openChest", t.player.click_loc, "frog_quest_chest", 1)
        t.exec("enterCode.open", t.ui.await_open, "combination_lock", 10)
        t.ticks(3)
        local TARGET = { "N", "A", "L", "I", "A" }
        local log = {}
        for i = 1, 5 do
            local _, btn = t.ui.widget("combination_lock:lock", 1 + 7 * (i - 1) + 3)
            for n = 1, 11 do
                local _, r = t.ui.text("combination_lock:lock", 1 + 7 * (i - 1) + 1)
                if r == TARGET[i] then break end
                t.ui.invoke(btn, 1)
                t.ticks(1)
            end
            local _, r2 = t.ui.text("combination_lock:lock", 1 + 7 * (i - 1) + 1)
            log[#log + 1] = tostring(r2)
        end
        t.check("enterCode.dials", table.concat(log, ",") == "N,A,L,I,A", "dials read " .. table.concat(log, ","))
        local _, conf = t.ui.widget("combination_lock:confirm_button")
        t.ui.invoke(conf, 1)
        t.exec("enterCode.plushy", t.inv.await, "frog_quest_plushy", 1, 10)
        t.expect("quest.stage.plant", t.quest.expect_stage("plant"))

        t.exec("goto-plantPlushy", t.player.goto_tile, 1694, 2977, 0)
        t.exec("plantPlushy", t.player.click_loc, "frog_quest_poo_plant_op", 1)
        t.exec("plantPlushy.stage", t.var.await_server, "varb9844_frog_quest", 26, 20)
        t.expect("quest.stage.cuthbert", t.quest.expect_stage("cuthbert"))
        t.exec("defeatCuthbert.present", t.npc.await_present, "frog_quest_cuthbert_combat", 15, 10)
        t.exec("defeatCuthbert", t.player.attack, "frog_quest_cuthbert_combat", 2, 10)
        t.exec("defeatCuthbert.dead", t.npc.await_dead_engaged, 300, 6, { eat = { item = "lobster", below = 20 } })
        t.exec("defeatCuthbert.stage", t.var.await_server, "varb9844_frog_quest", 28, 20)
        t.expect("quest.stage.marcellus_end", t.quest.expect_stage("marcellus_end"))

        t.exec("goto-talkToMarcellusEnd", t.player.goto_tile, 1683, 2973, 0)
        t.exec("talkToMarcellusEnd", t.player.talk_to, "frog_quest_marcellus")
        t.exec("talkToMarcellusEnd.chat", t.chat.drain, { max_pages = 10 })
        t.expect("quest.stage.gary_end", t.quest.expect_stage("gary_end"))

        t.exec("goto-yellowGaryEnd", t.player.goto_tile, 1696, 2982, 0)
        t.exec("talkToYellowFrogs.garyEnd", t.player.talk_to, "frog_quest_jane")
        t.exec("talkToYellowFrogs.garyEnd.chat", t.chat.play, {
            "npc:I hear the peace talks are going well.",
            "npc:I do hope so.",
        })

        t.exec("goto-talkToGaryEnd", t.player.goto_tile, 1694, 2996, 0)
        local _, snap = t.skill.snapshot()
        t.exec("talkToGaryEnd", t.player.talk_to, "frog_quest_gary")
        t.exec("talkToGaryEnd.chat", t.chat.drain, { max_pages = 20 })
        t.quest.expect_complete()
        t.check("reward.woodcutting", t.skill.expect_gain("woodcutting", 2000, snap))
        for _, e in ipairs({ { "frog_quest_gary", "Gary", 12939 } }) do
            local r, row = t.npc.nearest(e[1], 20)
            local ok = r == "ok" and type(row) == "table" and row.name == e[2] and row.npc_id == e[3]
            local d = tostring(r) .. " " .. tostring(row)
            if type(row) == "table" then
                d = string.format("%s -> name=%s npc_id=%s at %s,%s (want %s/%d)", e[1],
                    tostring(row.name), tostring(row.npc_id), tostring(row.x), tostring(row.z), e[2], e[3])
            end
            t.check("names.complete." .. e[1], ok, d)
        end
        t.exec("goto-marcellusFarmer", t.player.goto_tile, 1683, 2973, 0)
        for _, e in ipairs({ { "frog_quest_marcellus", "Marcellus", 12936 } }) do
            local r, row = t.npc.nearest(e[1], 20)
            local ok = r == "ok" and type(row) == "table" and row.name == e[2] and row.npc_id == e[3]
            local d = tostring(r) .. " " .. tostring(row)
            if type(row) == "table" then
                d = string.format("%s -> name=%s npc_id=%s at %s,%s (want %s/%d)", e[1],
                    tostring(row.name), tostring(row.npc_id), tostring(row.x), tostring(row.z), e[2], e[3])
            end
            t.check("names.complete." .. e[1], ok, d)
        end
        t.finish(0)
    end,
}
