-- Children of the Sun, played the way RSProx index 4408 (rev 241, 2026-10-29)
-- played it: Alina's start and topics, the delegation scene, one failed tail
-- (walked onto the guard, [318]..[336]) and Noah's restart, the tail followed
-- from the recorder's five hiding places ([456]..[616]), the bandits' house,
-- Tobyn, the marks (four wrong, the report refused, unmarked, the four right),
-- the arrest and the roof, the interrogation, the scroll, Tobyn afterwards and
-- Regulus Cento's first flight to Fortis and back.
-- docs/rsprox-analysis/quests/children_of_the_sun.md has the audit.

-- The tail's copy: the player starts it on the template tile 3226,3428.
local TAIL_START = { x = 3226, z = 3428 }
-- The guard's five stops and where the recorder stood through each look.
local STOPS = {
    { x = 3239, z = 3429, hide = { x = 3233, z = 3427 } },
    { x = 3243, z = 3412, hide = { x = 3240, z = 3417 } },
    { x = 3239, z = 3401, hide = { x = 3241, z = 3403 } },
    { x = 3243, z = 3389, hide = { x = 3236, z = 3392 } },
    { x = 3253, z = 3397, hide = { x = 3247, z = 3397 } },
}

-- A scene page comes after the cast has moved: wait it out (the blank
-- chatbox between pages is a mesbox with nothing in it).
local function wait_page(t, kind, n)
    for i = 1, n do
        if t.chat.kind() == kind then return true end
        t.ticks(1)
    end
    return t.chat.kind() == kind
end

-- Each guard wanders round his own post; the marks are ap from 3 tiles.
local POSTS = {
    [1] = { 3208, 3422 }, [2] = { 3221, 3430 }, [3] = { 3246, 3429 }, [4] = { 3237, 3427 },
    [5] = { 3227, 3424 }, [6] = { 3218, 3424 }, [8] = { 3206, 3431 }, [10] = { 3218, 3433 },
}

local function guard_marks(t, which, want, label)
    local post = POSTS[which]
    if post then t.player.walk_to(post[1] + 2, post[2], 30) end
    local sym = "vmq1_guard_" .. which
    local vb = ({ "varb9633", "varb9634", "varb9635", "varb9636", "varb9637",
        "varb9640", "varb9641", "varb9642", "varb9643", "varb9644" })[which] .. "_vmq1_guard_" .. which
    local r, d
    for attempt = 1, 4 do
        r, d = t.player.press(sym, 1, 12)
        t.ticks(2)
        local _, now = t.var.varbit(vb)
        if now == want then break end
    end
    t.check(label, select(1, t.var.expect(vb, want)) == "ok", "press " .. tostring(r) .. " " .. tostring(d) .. "; " .. vb .. " expected " .. want)
end

return {
    id = "childrenofthesun",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::childrenofthesun" },
    run = function(t)
        t.quest.bind({
            varp = "varb9632_vmq1",
            constants = { not_started = 0, alina2 = 2, alina4 = 4, follow = 6, house = 8, tobyn = 10, mark = 12, report = 14, finish = 16, finish2 = 18, finish3 = 20, finish4 = 22, complete = 24 },
            display = "Children of the Sun",
            points = 1,
        })
        -- the wire audit against RSProx 4408 reads this (build/quest_gate/<run>/ticklog.tsv)
        t.ticklog.start()
        t.ticks(3)
        t.expect("cots.reset", t.quest.expect_stage("not_started"))
        t.exec("goto-alina", t.player.goto_tile, 3224, 3426, 0)
        t.ticklog.mark("leg.alina")
        local deleg_mark = t.cutscene.mark()
        t.exec("talkToAlina", t.player.talk_to, "vmq1_alina")
        t.exec("alina.start", t.chat.play, {
            "npc:So they're from the west?",
            "npc:Yes! They call it Varlamore, the Shining Kingdom!",
            "player:What are you talking about?",
            "npc:Oh, hello! Are you here to see the delegation?",
            "player:Delegation?",
            "npc:See! It's not just me who doesn't know",
            "npc:The delegation from Varlamore, of course!",
            "npc:Why would I be excited about a place",
            "npc:A whole land locked off for hundreds of years!",
            "npc:Adventure? Sounds dangerous.",
            "npc:Bah! Boring! What about you, stranger?",
            "choose:Yes.",
            "player:Well, I'm actually something of an adventurer myself.",
            "npc:That's the spirit!",
            "choose:What can you tell me about Varlamore?",
            "player:What can you tell me about Varlamore?",
            "npc:It's far across the Western Ocean",
            "npc:Sounds like a prison.",
            "npc:Quite the opposite!",
            "player:The Sun Queen?",
            "npc:Yes! She rules from her palace",
            "npc:What... the sun?",
            "npc:That's right! They have their own religion",
            "npc:I don't know... It all sounds a bit odd to me.",
            "choose:How come you know so much about Varlamore?",
            "player:How come you know so much about Varlamore?",
            "npc:A while back, a group of travellers",
            "npc:College of Bards?",
            "npc:Yes! It's found in the floating city of Tempestus!",
            "npc:They stayed the night in the Blue Moon Inn",
            "choose:When will this delegation arrive?",
            "player:When will this delegation arrive?",
            "npc:Well, it should be any time now!",
            "npc:Hang on... what's going on over there?",
            "npc:Wait! That's them! They're here!",
        })
        t.ticklog.mark("leg.delegation")
        t.expect("quest.stage.alina4", t.quest.expect_stage("alina4"))
        wait_page(t, "npc", 30)
        t.exec("delegation.chat", t.chat.play, {
            "npc:There they are: The children of the sun!",
            "npc:Teokan? What's a Teokan?",
            "npc:It means High Priest.",
            "npc:Huh...",
        })
        wait_page(t, "npc", 15)
        t.exec("delegation.bag", t.chat.play, {
            "npc:What are you doing with that bag?",
            "npc:Oh... Er... Just delivering some supplies",
            "npc:Well, hurry back.",
        })
        wait_page(t, "npc", 15)
        t.exec("delegation.end", t.chat.play, {
            "npc:That's an unusually big bag he's carrying.",
            "player:Hmm... Now where might you be going with that...?",
        })
        -- the camera rows need the live client (scriptrun has no camera)
        if deleg_mark ~= nil then
            t.exec("delegation.cutscene", t.cutscene.await, "delegation", { since = deleg_mark, quiet = 3, expect = {
                { op = "moveto", height = 460 }, { op = "lookat", height = 260 },
                { op = "moveto", height = 390 }, { op = "lookat", height = 190 },
                { op = "moveto", height = 390 }, { op = "lookat", height = 190 },
                { op = "reset" },
            } })
        end
        t.ticks(3)
        t.expect("quest.stage.follow", t.quest.expect_stage("follow"))

        -- The first try, as the recording's: straight after him until he
        -- notices ([318]..[336]).
        t.ticklog.mark("leg.tail.spotted")
        for round = 1, 60 do
            local r, g = t.npc.by_symbol("vmq1_bag_guard")
            if r ~= "ok" or type(g) ~= "table" or not g.x then break end
            t.player.walk_to(g.x, g.z, 1)
        end
        wait_page(t, "mesbox", 12)
        local spotted = (t.chat.play({ "mesbox:You failed to stay hidden from the guard." }) == "ok")
        t.check("tail.spotted", spotted, "the spotted box after walking onto the guard")
        t.expect("tail.spotted.stage", t.quest.expect_stage("follow"))
        local _, back = t.world.tile()
        t.check("tail.spotted.back", back and back.x == 3225 and back.z == 3427, "back at " .. tostring(back and back.x) .. "," .. tostring(back and back.z))

        -- Noah's restart, then the tail from the recorder's hiding places.
        t.ticklog.mark("leg.tail")
        t.exec("talkToAlina.again", t.player.talk_to, "vmq1_alina")
        t.exec("noah.restart", t.chat.play, { "npc:That was quite a large bag that guard was carrying." })
        t.ticks(5)
        local _, start = t.world.tile()
        local ox, oz = start.x - TAIL_START.x, start.z - TAIL_START.z
        t.check("tail.copy", ox ~= 0 or oz ~= 0, "the tail runs in a copy: start " .. start.x .. "," .. start.z)
        local house_mark = t.cutscene.mark()
        local k, at_stop, stage = 1, false, 6
        for round = 1, 400 do
            local _, st = t.quest.stage()
            stage = st
            if st ~= 6 then break end
            local r, g = t.npc.by_symbol("vmq1_bag_guard")
            if r ~= "ok" or type(g) ~= "table" or not g.x then t.ticks(1) else
                local gx, gz = g.x - ox, g.z - oz
                local s = STOPS[k]
                if s and gx == s.x and gz == s.z then
                    at_stop = true
                elseif s and at_stop then
                    at_stop = false
                    k = k + 1
                end
                -- Behind him until he is nearly at his next stop, as the
                -- recorder kept; then into that stop's hiding place.
                s = STOPS[k]
                local h = s and s.hide
                local near = s and math.max(math.abs(gx - s.x), math.abs(gz - s.z)) <= 4
                local _, me = t.world.tile()
                if h and near and (me.x - ox ~= h.x or me.z - oz ~= h.z) then
                    t.player.walk_to(h.x + ox, h.z + oz, 1)
                else
                    t.ticks(1)
                end
            end
        end
        t.check("tail.followed", stage == 8 or stage == 10, "stage " .. tostring(stage) .. " after the tail (stop " .. k .. ")")

        t.ticklog.mark("leg.house")
        wait_page(t, "player", 20)
        t.exec("house.chat", t.chat.play, {
            "player:What are you up to in there...?",
            "npc:You're late.",
            "npc:I'm here aren't I?",
            "npc:Enough! We don't have much time.",
            "npc:Mostly.",
            "npc:Mostly?",
            "npc:I grabbed what I could!",
            "npc:We're just going to have to make do.",
            "npc:Indeed. You all know the target.",
            "npc:Good luck. You're going to need it.",
            "npc:You lot panic too much.",
            "player:This doesn't sound good.",
        })
        -- the camera rows need the live client (scriptrun has no camera)
        if house_mark ~= nil then
            t.exec("house.cutscene", t.cutscene.await, "house", { since = house_mark, quiet = 3, expect = {
                { op = "moveto", height = 490 }, { op = "lookat", height = 290 },
                { op = "moveto", height = 340 }, { op = "lookat", height = 300 },
                { op = "moveto", height = 490 }, { op = "lookat", height = 290 },
                { op = "reset" },
            } })
        end
        t.ticks(4)
        t.expect("quest.stage.tobyn", t.quest.expect_stage("tobyn"))
        local _, out = t.world.tile()
        t.check("house.out", out and out.x == 3259 and out.z == 3398, "out at " .. tostring(out and out.x) .. "," .. tostring(out and out.z))

        -- The house door, now a walk-through door ([651]).
        t.exec("banditDoor.in", t.player.click_loc, "vmq1_bandit_door")
        t.ticks(3)
        local _, inside = t.world.tile()
        t.check("banditDoor.through", inside and inside.x == 3260 and inside.z == 3400, "after the door " .. tostring(inside and inside.x) .. "," .. tostring(inside and inside.z))

        -- Upstairs in the empty house: the ladder, the shelf, down again
        -- ([657]..[677]), and out through the door ([679]).
        t.exec("banditHouse.ladder", t.player.click_loc, "fai_varrock_ladder", 1)
        t.ticks(4)
        t.check("banditHouse.up", select(2, t.world.level()) == 1, "level " .. tostring(select(2, t.world.level())))
        t.exec("banditHouse.shelf", t.player.click_loc, "fai_varrock_shelf_books", 1)
        t.ticks(1)
        t.exec("banditHouse.shelf.line", t.msg.expect, "None of them look very interesting.")
        t.exec("banditHouse.laddertop", t.player.click_loc, "fai_varrock_laddertop", 1)
        t.ticks(4)
        t.check("banditHouse.down", select(2, t.world.level()) == 0, "level " .. tostring(select(2, t.world.level())))
        t.player.click_loc("vmq1_bandit_door", 1)
        t.ticks(3)
        local _, outside = t.world.tile()
        t.check("banditDoor.out", outside and outside.x == 3259 and outside.z == 3400, "after the door " .. tostring(outside and outside.x) .. "," .. tostring(outside and outside.z))

        t.ticklog.mark("leg.tobyn")
        -- Nevet, on the way ([722]..[736]).
        t.exec("goto-nevet", t.player.goto_tile, 3210, 3435, 0)
        t.exec("talkToNevet", t.player.talk_to, "events_shop")
        t.exec("nevet.chat", t.chat.play, {
            "npc:Hello! Looking to buy?",
            "player:What are you selling?",
            "npc:A selection of items for those who partake in variety!",
            "player:Right...",
            "mesbox:You can only purchase items from the Events Rewards Shop",
            "npc:So, looking to buy?",
            "choose:Yes please.",
            "player:Yes please.",
        })
        t.expect("nevet.shop", t.ui.await_open("event_rewards", 6))
        t.cheat("::closeinterfaces", false)
        t.ticks(2)
        t.exec("goto-tobyn", t.player.goto_tile, 3211, 3436, 0)
        t.exec("talkToTobyn", t.player.talk_to, "vmq1_guard_sergeant")
        t.exec("tobyn.briefing", t.chat.play, {
            "npc:Move along, citizen.",
            "player:Wait! I have some information",
            "npc:What is it?",
            "player:There are some bandits disguising themselves",
            "npc:Surely you can't be serious?",
            "player:It's true!",
            "npc:The one with the large bag?",
            "player:So what do we do?",
            "npc:And risk a panic?",
            "player:Four in total.",
            "npc:Right, we can work with that.",
            "npc:Don't go too far.",
            "npc:Return to me once you've found all four",
            "player:Alright, I'll get to it.",
        })
        t.ticks(2)
        t.expect("quest.stage.mark", t.quest.expect_stage("mark"))

        -- Four wrong marks, the report refused, unmarked ([766]..[862]).
        t.ticklog.mark("leg.marks")
        guard_marks(t, 8, 2, "mark.guard8")
        guard_marks(t, 10, 2, "mark.guard10")
        guard_marks(t, 6, 2, "mark.guard6")
        guard_marks(t, 5, 2, "mark.guard5")
        t.exec("goto-tobyn2", t.player.goto_tile, 3211, 3436, 0)
        t.exec("tobyn.wrong", t.player.talk_to, "vmq1_guard_sergeant")
        t.exec("tobyn.wrong.chat", t.chat.play, {
            "player:Alright, I've pointed out all the bandits.",
            "npc:Are you sure? I definitely recognise",
        })
        t.expect("quest.stage.mark.still", t.quest.expect_stage("mark"))
        guard_marks(t, 10, 1, "unmark.guard10")
        guard_marks(t, 5, 1, "unmark.guard5")
        guard_marks(t, 6, 1, "unmark.guard6")
        guard_marks(t, 8, 1, "unmark.guard8")
        guard_marks(t, 1, 2, "mark.guard1")
        guard_marks(t, 2, 2, "mark.guard2")
        guard_marks(t, 4, 2, "mark.guard4")
        guard_marks(t, 3, 2, "mark.guard3")
        t.expect("quest.stage.mark.four", t.quest.expect_stage("mark"))

        t.ticklog.mark("leg.arrest")
        t.exec("goto-tobyn3", t.player.goto_tile, 3211, 3436, 0)
        t.exec("tobyn.report", t.player.talk_to, "vmq1_guard_sergeant")
        t.exec("tobyn.arrest", t.chat.play, {
            "player:Alright, I've pointed out all the bandits.",
            "npc:Good work. I'll signal my guards",
        })
        -- the box under the fade asks for no click; its twin comes 4 ticks on
        t.ticks(5)
        t.exec("tobyn.arrested", t.chat.play, {
            "mesbox:Sergeant Tobyn signals to his guards",
            "npc:That could have ended up quite nasty",
            "player:So what happens now?",
            "npc:The bandits will be interrogated.",
            "npc:Speaking of which...",
            "player:Of course! Where do you need me?",
            "npc:Come with me.",
        })
        t.ticks(5)
        t.exec("tobyn.escort", t.chat.play, { "mesbox:Sergeant Tobyn escorts you to the palace roof." })
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
        local _, roof = t.world.tile()
        t.check("roof.tile", roof and roof.x == 3203 and roof.z == 3474, "on the roof at " .. tostring(roof and roof.x) .. "," .. tostring(roof and roof.z))
        local _, g1 = t.var.varbit("varb9633_vmq1_guard_1")
        local _, g5 = t.var.varbit("varb9637_vmq1_guard_5")
        t.check("guards.gone", g1 == 0 and g5 == 0, "guard 1 " .. tostring(g1) .. ", guard 5 " .. tostring(g5))

        t.ticklog.mark("leg.roof")
        local cell_mark = t.cutscene.mark()
        t.exec("roof.chat", t.chat.play, {
            "npc:Ah, nilsal, sergeant.",
            "npc:Prince Itzla! I... I'm sorry",
            "npc:Well, all that politics downstairs",
            "npc:Well, no, it's just...",
            "npc:Wonderful! And you! Who are you?",
            "player:Hello there. I'm",
            "npc:Kuani! Pleased to meet you.",
            -- "would-be" wraps at its hyphen for a short name (x_p1); match before it
            "npc:are you the one who caught these",
            "player:That's right.",
            "npc:Well, then why don't you and I",
            "npc:Not at all, but...",
            "npc:Excellent! Let's get started then.",
        })
        t.expect("quest.stage.finish2", t.quest.expect_stage("finish2"))
        t.ticklog.mark("leg.cell")
        wait_page(t, "npc", 15)
        t.exec("cell.chat", t.chat.play, {
            "npc:Nilsal to you, iknami.",
            "npc:I'm not telling you anything.",
            "npc:Ah, tetamo!",
            "npc:Now, you'll need to forgive me",
            "npc:You're asking me how you should interrogate me?",
            "npc:Well, it just seemed polite to ask.",
            "npc:If I want?",
            "npc:Absolutely! Now, what are your thoughts on chicken?",
            "npc:Chicken?",
            "npc:do you not have chickens here?",
            "player:Yes, we have chickens.",
            "npc:Well, then why the confusion?",
            "npc:I know what a chicken is!",
            "npc:Well, why didn't you say so?",
            "npc:I don't know what game you're playing",
            "npc:No game at all, iknami.",
            "npc:You're going to cook me?",
            "npc:Cook? No, not at all!",
            "npc:Alright, enough! I'll tell you what you want!",
            "npc:Kuani!",
            "npc:So who are you working for?",
            "npc:I don't know their name.",
            "npc:Why did they want you to attack the delegation?",
            "npc:They didn't say, but that priest",
            "npc:So a Varlamorian paid you",
            "npc:That's all I know!",
            "npc:Hmm...",
            "npc:Well, I think we're done here.",
        })
        -- the camera rows need the live client (scriptrun has no camera)
        if cell_mark ~= nil then
            t.exec("cell.cutscene", t.cutscene.await, "cell", { since = cell_mark, quiet = 3, expect = {
                { op = "moveto", height = 311 }, { op = "lookat", height = 300 },
                { op = "reset" },
            } })
        end
        t.ticklog.mark("leg.aftermath")
        wait_page(t, "npc", 15)
        t.exec("aftermath.chat", t.chat.play, {
            "npc:I think we got what we needed there.",
            "npc:You're sure he told you everything?",
            "npc:Oh yes. The fear of being cooked",
            "npc:We just cut out their heart",
            "player:I can't tell if you're joking or not...",
            "npc:Anyway, sounds like there'll be some work",
            "npc:once we've got these papers signed",
            "npc:Now, it's entirely up to you of course",
            "npc:No need to decide now",
            "npc:Now, they'll be missing me downstairs.",
        })
        t.ticks(5)
        t.exec("itzla.departs", t.chat.play, {
            "mesbox:Itzla departs.",
            "npc:Well, that was interesting...",
            "player:Not every day the heir",
            "npc:Oddly enough, no.",
        })
        t.ticks(4)
        t.quest.expect_complete()
        local _, ft = t.var.varbit("varb9652_vmq2_first_travel")
        t.check("firstTravel.armed", ft == 1, "varb9652_vmq2_first_travel = " .. tostring(ft))
        local sc, sd = t.scroll.close()
        t.check("scroll.close", sc == "ok", tostring(sd))

        t.ticklog.mark("leg.tobyn.after")
        t.exec("tobyn.after", t.player.talk_to, "vmq1_guard_sergeant_roof")
        t.exec("tobyn.after.chat", t.chat.play, {
            "npc:Good to see you, adventurer.",
            "player:Not bad, thanks.",
            "npc:Glad to hear it. Did you end up travelling to Varlamore?",
            "player:Not yet.",
            "npc:Prince Itzla said that one of his people",
            "player:I might just do that. Thanks!",
        })

        -- Regulus Cento outside the east gate: the first flight ([1048]..[1081])
        -- and the way home ([1085]..[1098]).
        t.ticklog.mark("leg.regulus")
        t.cheat("::tele 0_51_53_17_24")
        t.ticks(3)
        t.exec("regulus.talk", t.player.talk_to, "vmq2_quetzal_keeper_varrock")
        t.exec("regulus.chat", t.chat.play, {
            "npc:Nilsal, adventurer. The name is Regulus Cento",
            "player:Quetzal? What's a quetzal?",
            "npc:Well, just look here to my dear companion, Primio",
            "player:Sounds exciting!",
            "npc:You're not wrong there.",
            "npc:Speaking of which, Crown Prince Itzla Arkan",
            "player:Of course!",
            "npc:Excellent! Now, I know Prince Itzla",
            "player:Sounds good.",
            "npc:So, ready for your first journey to Varlamore?",
            "choose:Let's do it!",
            "player:Let's do it!",
            "npc:Kuani! We will depart at once.",
        })
        wait_page(t, "npc", 15)
        t.exec("regulus.fortis.chat", t.chat.play, {
            "npc:Well, here we are! Welcome to Fortis!",
            "player:It's beautiful!",
            "npc:I'm glad you think so!",
            "player:You mentioned he'd be sending some people",
            "npc:Yes, the Tullus twins.",
        })
        local _, fortis = t.world.tile()
        t.check("regulus.fortis", fortis and fortis.x == 1700 and fortis.z == 3141, "landed at " .. tostring(fortis and fortis.x) .. "," .. tostring(fortis and fortis.z))
        local _, ft4 = t.var.varbit("varb9652_vmq2_first_travel")
        t.check("firstTravel.done", ft4 == 4, "varb9652_vmq2_first_travel = " .. tostring(ft4))
        t.ticks(2)
        t.exec("regulus.home", t.player.talk_to, "vmq2_quetzal_keeper_fortis")
        t.exec("regulus.home.chat", t.chat.play, {
            "npc:Nilsal, adventurer. Do you wish to return to Varrock?",
            "choose:Yes please.",
            "player:Yes please.",
            "npc:Then we will depart at once.",
        })
        t.ticks(8)
        local _, home = t.world.tile()
        t.check("regulus.varrock", home and home.x == 3280 and home.z == 3412, "landed at " .. tostring(home and home.x) .. "," .. tostring(home and home.z))
        t.finish(0)
    end,
}
