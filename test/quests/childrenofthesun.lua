return {
    id = "childrenofthesun",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::childrenofthesun" },
    run = function(t)
        t.quest.bind({
            varp = "varb9632_vmq1",
            constants = { not_started = 0, follow = 6, house = 8, tobyn = 10, mark = 12, report = 14, finish = 16, finish2 = 18, finish3 = 20, finish4 = 22, complete = 24 },
            display = "Children of the Sun",
            points = 1,
        })
        t.ticks(3)
        t.expect("cots.reset", t.quest.expect_stage("not_started"))
        t.exec("goto-talkToAlina", t.player.goto_tile, 3225, 3426, 0)
        t.exec("talkToAlina", t.player.talk_to, "vmq1_alina_vis")
        t.exec("alina.chat", t.chat.play, {
            "npc:So they're from the west?",
            "npc:Yes! They call it Varlamore",
            "player:What are you talking about?",
            "npc:Oh, hello! Are you here",
            "player:Delegation?",
            "npc:See! It's not just me",
            "npc:The delegation from Varlamore",
            "npc:Why would I be excited",
            "npc:A whole land locked off",
            "npc:Adventure? Sounds dangerous",
            "npc:Bah! Boring!",
            "choose:Yes.",
            "player:Well I'm actually something of an adventurer",
            "npc:That's the spirit!",
            "choose:What can you tell me about Varlamore?",
            "player:What can you tell me",
            "npc:It's far across the western sea",
            "npc:Sounds like a prison.",
            "npc:Quite the opposite!",
            "player:The Sun Queen?",
            "npc:Yes! She rules",
            "npc:What... the sun?",
            "npc:That's right!",
            "npc:I don't know...",
            "choose:When will this delegation arrive?",
            "player:When will this delegation",
            "npc:Well it should be any time now!",
            "npc:Hang on...",
            "npc:Wait! That's them!",
            "npc:There they are",
            "npc:Teokan? What's a Teokan?",
            "npc:It means High Priest",
            "npc:Huh...",
            "npc:What are you doing with that bag?",
            "npc:Oh... Er...",
            "npc:Well hurry back",
            "npc:That's an unusually big bag",
            "player:Hmm... Now where might you be going",
        })
        t.ticks(3)
        t.expect("quest.stage.follow", t.quest.expect_stage("follow"))
        -- careless: close behind him, so he spots us at the first stop
        local GT = {}
        local st, rd = 6, 0
        for round = 1, 200 do
            local r, g = t.npc.by_symbol("vmq1_bag_guard")
            local stage = select(2, t.quest.stage())
            rd = round
            if stage ~= 6 then st = stage break end
            if r == "ok" and type(g) == "table" and g.x then
                local _, tl = t.world.tile()
                local d = math.max(math.abs(g.x - tl.x), math.abs(g.z - tl.z))
                GT[#GT + 1] = { x = g.x, z = g.z }
                if d > 3 then t.player.walk_to(g.x, g.z, 1) else t.ticks(1) end
            else
                break
            end
        end
        t.ticks(2)
        local seen = false
        local mbr = t.chat.play({ "mesbox:You failed to stay hidden from the guard." })
        seen = (mbr == "ok")
        t.check("followGuard.spotted", seen and st == 6, "spotted message=" .. tostring(seen) .. " stage=" .. tostring(st) .. " after " .. rd .. " rounds")
        local gr = t.npc.by_symbol("vmq1_bag_guard")
        t.check("followGuard.guardGone", gr ~= "ok", "guard lookup after the failure: " .. tostring(gr))
        t.player.walk_to(3225, 3423, 40)
        t.ticks(2)
        t.exec("alina.retry", t.player.talk_to, "vmq1_alina_vis")
        t.exec("alina.retry.chat", t.chat.play, {
            "npc:That was quite a large bag",
        })
        t.ticks(2)
        GT = {}
        local st2, rd2 = 6, 0
        for round = 1, 500 do
            local r, g = t.npc.by_symbol("vmq1_bag_guard")
            local stage = select(2, t.quest.stage())
            rd2 = round
            if stage ~= 6 then st2 = stage break end
            if r == "ok" and type(g) == "table" and g.x then
                local _, tl = t.world.tile()
                local d = math.max(math.abs(g.x - tl.x), math.abs(g.z - tl.z))
                GT[#GT + 1] = { x = g.x, z = g.z }
                local b = GT[#GT - 8]
                if d > 13 and b then t.player.walk_to(b.x, b.z, 1) else t.player.idle() t.ticks(1) end
            else
                break
            end
        end
        local _, l2 = t.msg.last(14)
        local txt = ""
        for _, e in ipairs(l2 or {}) do txt = txt .. e.text .. " | " end
        t.check("followGuard.arrived", st2 == 8, "stage " .. tostring(st2) .. " after " .. rd2 .. " rounds; msgs " .. txt)
        t.exec("goto-door", t.player.goto_tile, 3257, 3400, 0)
        t.exec("attemptToEnterHouse", t.player.click_loc, "vmq1_bandit_door")
        t.exec("house.scene", t.chat.play, {
            "player:What are you up to in there",
            "npc:You're late.",
            "npc:I'm here aren't I?",
            "npc:Enough! We don't have much time",
            "npc:Mostly.",
            "npc:Mostly?",
            "npc:I grabbed what I could!",
            "npc:We're just going to have to make do",
            "npc:Indeed. You all know the target",
            "npc:Good luck. You're going to need it.",
            "npc:You lot panic too much",
            "player:This doesn't sound good",
        })
        t.ticks(2)
        t.expect("quest.stage.tobyn", t.quest.expect_stage("tobyn"))
        t.exec("goto-tobyn", t.player.goto_tile, 3211, 3440, 0)
        t.exec("talkToTobyn", t.player.talk_to, "vmq1_guard_sergeant")
        t.exec("tobyn.report", t.chat.play, {
            "npc:Move along, citizen.",
            "player:Wait! I have some information",
            "npc:What is it?",
            "player:There are some bandits",
            "npc:Surely you can't be serious?",
            "player:It's true!",
            "npc:The one with the large bag?",
            "player:So what do we do?",
            "npc:And risk a panic?",
            "player:Four in total.",
            "npc:Right, we can work with that.",
            "npc:Don't go too far.",
            "npc:Return to me once",
            "player:Alright, I'll get to it.",
        })
        t.ticks(2)
        t.expect("quest.stage.mark", t.quest.expect_stage("mark"))
        do
            local _, before = t.var.varbit("varb9637_vmq1_guard_5")
            local r, d
            for attempt = 1, 4 do
                r, d = t.player.press("vmq1_guard_5", 1, 10)
                t.ticks(2)
                local _, now = t.var.varbit("varb9637_vmq1_guard_5")
                if now ~= before or now == 2 then break end
                if t.chat.play({ "mesbox:*" }) == "ok" then break end
            end
            t.check("markWrongGuard5", select(1, t.var.expect("varb9637_vmq1_guard_5", 2)) == "ok", "press " .. tostring(r) .. " " .. tostring(d) .. " ; varb9637_vmq1_guard_5 expected 2")
        end
        do
            local _, before = t.var.varbit("varb9633_vmq1_guard_1")
            local r, d
            for attempt = 1, 4 do
                r, d = t.player.press("vmq1_guard_1", 1, 10)
                t.ticks(2)
                local _, now = t.var.varbit("varb9633_vmq1_guard_1")
                if now ~= before or now == 2 then break end
                if t.chat.play({ "mesbox:*" }) == "ok" then break end
            end
            t.check("markGuard1", select(1, t.var.expect("varb9633_vmq1_guard_1", 2)) == "ok", "press " .. tostring(r) .. " " .. tostring(d) .. " ; varb9633_vmq1_guard_1 expected 2")
        end
        do
            local _, before = t.var.varbit("varb9634_vmq1_guard_2")
            local r, d
            for attempt = 1, 4 do
                r, d = t.player.press("vmq1_guard_2", 1, 10)
                t.ticks(2)
                local _, now = t.var.varbit("varb9634_vmq1_guard_2")
                if now ~= before or now == 2 then break end
                if t.chat.play({ "mesbox:*" }) == "ok" then break end
            end
            t.check("markGuard2", select(1, t.var.expect("varb9634_vmq1_guard_2", 2)) == "ok", "press " .. tostring(r) .. " " .. tostring(d) .. " ; varb9634_vmq1_guard_2 expected 2")
        end
        t.exec("goto-guard3", t.player.goto_tile, 3243, 3429, 0)
        do
            local _, before = t.var.varbit("varb9635_vmq1_guard_3")
            local r, d
            for attempt = 1, 4 do
                r, d = t.player.press("vmq1_guard_3", 1, 10)
                t.ticks(2)
                local _, now = t.var.varbit("varb9635_vmq1_guard_3")
                if now ~= before or now == 2 then break end
                if t.chat.play({ "mesbox:*" }) == "ok" then break end
            end
            t.check("markGuard3", select(1, t.var.expect("varb9635_vmq1_guard_3", 2)) == "ok", "press " .. tostring(r) .. " " .. tostring(d) .. " ; varb9635_vmq1_guard_3 expected 2")
        end
        t.exec("goto-guard6", t.player.goto_tile, 3222, 3426, 0)
        -- a fifth mark is refused
        do
            local _, before = t.var.varbit("varb9640_vmq1_guard_6")
            local r, d
            for attempt = 1, 4 do
                r, d = t.player.press("vmq1_guard_6", 1, 10)
                t.ticks(2)
                local _, now = t.var.varbit("varb9640_vmq1_guard_6")
                if now ~= before or now == 1 then break end
                if t.chat.play({ "mesbox:*" }) == "ok" then break end
            end
            t.check("fifthMarkRefused", select(1, t.var.expect("varb9640_vmq1_guard_6", 1)) == "ok", "press " .. tostring(r) .. " " .. tostring(d) .. " ; varb9640_vmq1_guard_6 expected 1")
        end
        t.exec("fifthMark.tbox", t.chat.play, { "mesbox:You've already marked enough guards." })
        -- Tobyn rejects a set with a genuine guard in it
        t.exec("goto-tobyn2", t.player.goto_tile, 3211, 3440, 0)
        t.exec("tobyn.wrongset", t.player.talk_to, "vmq1_guard_sergeant")
        t.exec("tobyn.wrongset.chat", t.chat.play, {
            "player:Alright, I've pointed out all the bandits.",
            "npc:Are you sure? I definitely recognise",
        })
        t.expect("quest.stage.mark.still", t.quest.expect_stage("mark"))
        t.exec("goto-guard5", t.player.goto_tile, 3222, 3427, 0)
        do
            local _, before = t.var.varbit("varb9637_vmq1_guard_5")
            local r, d
            for attempt = 1, 4 do
                r, d = t.player.press("vmq1_guard_5", 1, 10)
                t.ticks(2)
                local _, now = t.var.varbit("varb9637_vmq1_guard_5")
                if now ~= before or now == 1 then break end
                if t.chat.play({ "mesbox:*" }) == "ok" then break end
            end
            t.check("realUnmarkOneOfTheGuards", select(1, t.var.expect("varb9637_vmq1_guard_5", 1)) == "ok", "press " .. tostring(r) .. " " .. tostring(d) .. " ; varb9637_vmq1_guard_5 expected 1")
        end
        t.exec("goto-guard4", t.player.goto_tile, 3235, 3428, 0)
        do
            local _, before = t.var.varbit("varb9636_vmq1_guard_4")
            local r, d
            for attempt = 1, 4 do
                r, d = t.player.press("vmq1_guard_4", 1, 10)
                t.ticks(2)
                local _, now = t.var.varbit("varb9636_vmq1_guard_4")
                if now ~= before or now == 2 then break end
                if t.chat.play({ "mesbox:*" }) == "ok" then break end
            end
            t.check("markGuard4", select(1, t.var.expect("varb9636_vmq1_guard_4", 2)) == "ok", "press " .. tostring(r) .. " " .. tostring(d) .. " ; varb9636_vmq1_guard_4 expected 2")
        end
        t.ticks(2)
        t.expect("quest.stage.report", t.quest.expect_stage("report"))
        t.exec("goto-tobyn3", t.player.goto_tile, 3211, 3440, 0)
        t.exec("reportBackToTobyn", t.player.talk_to, "vmq1_guard_sergeant")
        t.exec("tobyn.arrest", t.chat.play, {
            "player:Alright, I've pointed out all the bandits.",
            "npc:Good work. I'll signal my guards",
            "mesbox:Sergeant Tobyn signals to his guards",
            "npc:That could have ended up quite nasty",
            "player:So what happens now?",
            "npc:The bandits will be interrogated.",
            "npc:Speaking of which...",
            "player:Of course! Where do you need me?",
            "npc:Come with me.",
            "mesbox:Sergeant Tobyn escorts you to the palace roof.",
        })
        t.ticks(3)
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
        t.check("castle.roof", select(2, t.world.level()) == 2, "level " .. tostring(select(2, t.world.level())))
        t.exec("itzla.talk", t.player.talk_to, "vmq1_itzla")
        t.exec("roof.intro.chat", t.chat.play, {
            "npc:Ah, nilsal, sergeant.",
            "npc:Prince Itzla! I...",
            "npc:Well all that politics",
            "npc:Well no, it's just...",
            "npc:Wonderful! And you! Who are you?",
            "player:Hello there. I'm",
            "npc:Kuaini! Pleased to meet you",
            "npc:are you the one who caught",
            "player:That's right.",
            "npc:Well then, why don't you and I",
            "npc:Not at all, but...",
            "npc:Excellent! Let's get started then.",
            "npc:Nilsal to you, iknami.",
            "npc:I'm not telling you anything.",
        })
        t.expect("quest.stage.finish2", t.quest.expect_stage("finish2"))
        t.exec("interrogation.chat", t.chat.play, {
            "npc:Ah, tetamo!",
            "npc:Now, you'll need to forgive me",
            "npc:You're asking me how",
            "npc:Well it just seemed polite",
            "npc:If I want?",
            "npc:Absolutely! Now, what are your thoughts on chicken?",
            "npc:Chicken?",
            "npc:do you not have chickens here?",
            "player:Yes, we have chickens.",
            "npc:Well then, why the confusion?",
            "npc:I know what a chicken is!",
            "npc:Well why didn't you say so?",
            "npc:I don't know what game",
            "npc:No game at all, iknami.",
            "npc:You're going to cook me?",
            "npc:Cook? No, not at all!",
            "npc:Alright, enough!",
            "npc:Kuaini!",
            "npc:So who are you working for?",
            "npc:I don't know their name.",
            "npc:Why did they want you",
            "npc:They didn't say, but that priest",
            "npc:So a Varlamorian paid you",
            "npc:That's all I know!",
            "npc:Hmm...",
            "npc:Well I think we're done here.",
            "npc:I think we got what we needed there.",
        })
        t.expect("quest.stage.finish3", t.quest.expect_stage("finish3"))
        t.exec("aftermath.chat", t.chat.play, {
            "npc:You're sure he told you everything?",
            "npc:Oh yes. The fear of being cooked",
            "npc:We just cut out their heart",
            "player:I can't tell if you're joking",
            "npc:Anyway, sounds like there'll be some work",
            "npc:once we've got these papers signed",
            "npc:Now, it's entirely up to you",
            "npc:just speak to Regulus Cento",
            "npc:Timoiva, and may the sun light your way!",
            "mesbox:Itzla departs.",
            "npc:Well, that was interesting...",
            "player:Not every day the heir",
            "npc:Oddly enough, no.",
        })
        t.ticks(3)
        t.quest.expect_complete()
        local _, ft = t.var.varbit("varb9652_vmq2_first_travel")
        t.check("firstTravel.armed", ft == 1, "varb9652_vmq2_first_travel = " .. tostring(ft))
        t.finish(0)
    end,
}
