-- Recipe for Disaster's start, as RSProx index 4400 (2026-10-28, rev 241)
-- played it: the Cook's two talks [165], [181]..[378], the hand-in [1609]
-- and its scroll, the stage-2 talk [1631], the journal [1624], the dining
-- room door and the banquet [1656]..[2003], Aris's five topics [2008]..[2119],
-- the subquest menu [2126] and the portal door out [2209].
--
-- Every page is the recorded one, in the recorded order, on the recorded
-- choices (each "kind:fragment" is the page's first row). The run keeps
-- build/quest_gate/<run>/ticklog.tsv for the wire audit in
-- docs/rsprox-analysis/quests/rfd_start.md.
return {
    id = "rfd_start",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setvar varp29_cookquest 2",
        "::setlevel cooking 4",
        "::goto 3208 3214 0",
    },

    run = function(t)
        t.note(select(2, t.ticklog.start()))
        t.ticks(3)
        local function stage(name, want)
            local r, v = t.var.server("varb1850_hundred_main_quest_var")
            t.check(name, r == "ok" and v == want, "hundred_main_quest_var " .. tostring(v) .. ", want " .. want)
        end
        local function talk(name, npc, list)
            t.exec(name, t.player.talk_to, npc, 1)
            t.expect(name .. ".pages", t.chat.play(list))
        end
        stage("stage.0", 0)

        -- [165]..[175]
        talk("cook.dying", "cook", { "npc:How is the adventuring going, my friend?", "options",
            "choose:I keep on dying.", "player:I keep on dying.", "npc:Ah well, at least you keep coming back t", "end" })

        -- [178]..[378]
        talk("cook.start", "cook", { "npc:How is the adventuring going, my friend?", "options",
            "choose:Do you have any other quests for me?", "player:Do you have any other quests for me? Tha", "npc:Ooooh dear, I might have, but it's all v", "player:Embarrassing?", "npc:Weeellll...", "player:Yup, that's why I had to go and collect", "npc:Yeeeesssss...", "npc:It isn't.", "options",
            "choose:Angry! It makes me angry!", "player:GRRRR!", "npc:I needed to test you and see if I could", "npc:I am in a terrible pickle, and I despera", "options",
            "choose:How do you still have a job?", "player:I'm curious...", "npc:I'm sorry?", "player:It's just surprising that you would stil", "npc:WHAT?", "player:Well, things you would expect every kitc", "player:And what is worse, you don't even go off", "npc:I know I'm such a failure...", "player:Help again?", "player:You need help AGAIN?", "npc:Well, you're obviously trustworthy, I gu", "player:Please do.", "npc:Well...", "npc:Did you ever hear of Franizzard Van Lumb", "player:Erm...", "player:And with a name like that, I'm sure I wo", "npc:Well, he was a great chef, and also my a", "npc:One hundred years ago there was a very i", "npc:of his descendants.", "player:So you have your job because a hundred y", "player:I don't see how that's much of a problem", "player:In fact I would say you are a VERY LUCKY", "npc:Oooooh no....", "npc:This big important secret meeting that F", "player:Still not seeing the problem...", "npc:Well, I have always felt like a failure", "npc:I want to prove to the world that I am a", "npc:But to do that...", "player:What won't?", "npc:(sniff)", "npc:everybody so much...", "player:Let me take a wild guess:", "npc:Yes!", "player:Call it a hunch.", "player:Okay, tell me what you're after and then", "npc:Well, they are common enough ingredients", "npc:I will pay you upfront for any expenses", "npc:Please can you help me?",
            "mesbox:Before starting this quest", "options", "choose:Yes.", "player:I would love to help you!", "npc:Well, I have an incredibly important ban", "npc:Most of the ingredients I was able to ob", "player:Like what?", "npc:I need the following ingredients as soon", "npc:One Eye Of Newt", "npc:And a glass of greenman's ale.", "npc:I have given you 100 coins to cover any", "player:I'll go look for those for you then!", "end" })
        stage("stage.1", 1)
        local cr0, coins = t.inv.count("coins")
        t.check("coins.100", cr0 == "ok" and coins == 100, "coins " .. tostring(coins))

        -- [1609]..[1621]
        for _, obj in ipairs({ "eye_of_newt", "greenmans_ale", "rotten_tomato", "hundred_fruit_blast" }) do
            t.cheat("::give " .. obj .. " 1", false)
        end
        t.ticks(2)
        talk("cook.handin", "cook", { "player:About those ingredients you wanted for t", "npc:Did you bring them? Remember, I need an", "player:You will be happy to know...", "end" })
        t.expect("scroll.open", t.ui.await_open("questscroll", 10))
        stage("stage.2", 2)
        local wr, close = t.ui.widget("questscroll:close_button")
        if wr ~= "ok" or close == nil then
            t.step("scroll.close", "FAIL", "questscroll:close_button " .. tostring(wr))
            t.finish(1)
            return
        end
        t.expect("scroll.close", t.ui.invoke(close, 1))
        t.ticks(2)
        -- [1621]: the server answers the close with the line and the Cook's
        -- page (TORIRSSERVER_TRACE_OUT shows both). Our client then sends
        -- CLOSE_MODAL for an op named "Close", which Jagex's does not, and
        -- the page goes with it (open in docs/rsprox-analysis/quests/rfd_start.md).
        t.expect("cook.after.scroll", t.msg.expect("Honestly, you must go and look at the amazing feast that's been prepared!"))

        -- [1631]..[1635]
        talk("cook.banquet", "cook", { "player:How's that banquet going?", "npc:Why haven't you gone and looked at the f", "player:I don't know... I'm just... quite busy,", "npc:No, honestly, you MUST go in there!", "npc:GO IN THERE!", "end" })

        -- [1624]: the stage-2 journal, row for row.
        local jr, j = t.ui.journal_open("Recipe for Disaster")
        local want = { "I spoke to the Cook in Lumbridge, who asked for my help again",
            "in preparing a feast for some very important visitors to the", "castle.",
            "He needed a 'Dirty Blast', a greenman's ale, a rotten tomato", "and an eye of newt for the feast.",
            "I brought all of the ingredients to him and he rewarded me with", "some cooking experience!",
            "I should go into the Dining Room next to the kitchen and see",
            "what this Feast he was telling me about is like!" }
        local diff = nil
        local lines = (jr == "ok" and j and j.lines) or {}
        for i = 1, math.max(#want, #lines) do
            if lines[i] ~= want[i] then
                diff = "row " .. i .. ": want '" .. tostring(want[i]) .. "' read '" .. tostring(lines[i]) .. "'"
                break
            end
        end
        t.check("journal.stage2", jr == "ok" and diff == nil, tostring(jr) .. " " .. #lines .. " rows; " .. (diff or "every row matches"))
        if jr == "ok" then
            t.ui.journal_close()
        end

        -- [1656]..[2003]: the door, the banquet, the frozen room.
        local mark = t.cutscene.mark()
        t.exec("door.banquet", t.player.click_loc, "hundred_lumbridge_door", 1)
        t.ticks(8)
        stage("stage.3", 3)
        t.ticks(350)
        local cr, cv = t.var.server("varb542_cutscene_status")
        t.check("banquet.unlocked", cr == "ok" and cv == 0, "cutscene_status " .. tostring(cv))
        t.note(select(2, t.cutscene.await("banquet", { since = mark, quiet = 20 })))

        -- [2008]..[2119]
        talk("aris", "hundred_aris", { "player:Hi.", "npc:Hello there adventurer.", "options",
            "choose:Who are you?", "player:Who are you?", "player:You look kind of familiar...", "npc:I should do, you came to see me about de", "player:Ooooh yeah...", "npc:I am the fortune-teller.", "player:Okay then Aris...", "npc:Was there something you wanted to ask me", "options",
            "choose:What just happened?", "player:I do have a question.", "npc:Oh yes?", "player:Just what in the flipping hippo is going", "npc:...flipping hippo?", "player:Yeeeess....", "npc:No, no I'm quite sure that I never have.", "player:Well, that's not the point!", "player:Please, tell me what just happened!", "npc:Certainly.", "npc:A hundred years ago, he threatened the s", "npc:As a result of that action, the assistan", "npc:Now, apparently the current head cook of", "npc:inadvertently freed the evil Culinaroman", "npc:Naturally, when I saw what was happening", "npc:once and for all.", "player:Um... what?", "npc:You want me to explain it again?", "player:Yes please.", "npc:(sigh)", "npc:Because of you and the Lumbridge cook, h", "npc:He was about to slaughter all of these p", "player:You stopped time?", "npc:Yes I stopped time.", "player:Isn't stopping time kind of...", "npc:Oh, unbelievably so.", "player:Huh?", "npc:Everything outside this room continues t", "npc:members of the secret council.", "npc:Inside this room however you have not.", "player:Uh... what?", "npc:Where time has been frozen in this room,", "npc:outside, the more removed from normal ti", "player:Wait... so I've already saved all of the", "npc:Yes.", "npc:Otherwise there will be increasing tempo", "player:Not the whole universe!", "npc:Yes.", "player:But...", "npc:Yes. You have.", "player:My brain hurts...", "npc:Yes, time travel tends to do that to peo", "npc:I suggest you don't worry about it too m", "player:How would I do that?", "npc:Each council member will have their own", "npc:Inspect the one you wish to save, and I", "npc:Was there anything else you wanted to as", "options",
            "choose:Who's that guy?", "player:Who's that guy over there?", "npc:He calls himself the Culinaromancer.", "player:Culinaromancer?", "npc:Yes, that's correct.", "npc:food as his source of magical power.", "player:Food?", "npc:Do not underestimate the latent power of", "npc:Not only are they a fundamental life-for", "npc:Do not underestimate him, part of his st", "player:Okay, so he's powerful, but I don't get", "npc:Ah, it is a very old story indeed.", "npc:A hundred years ago, he worked here in L", "npc:This was all a pretence, of course. As p", "npc:Building his power was just the first st", "player:How do you mean?", "npc:Knowing of the regular meetings of the s", "player:Would that have worked?", "npc:Possibly, we might never know.", "npc:be useless.", "player:Then how did he escape?", "npc:Well, the current cook of Lumbridge obvi", "npc:Which obviously seems to have opened the", "player:Oh.", "npc:Yes, those ingredients.", "player:Yeah...", "npc:Are you clear on what you have to do, or", "options",
            "choose:Who are these people?", "player:Who are these people?", "npc:The people you see assembled here are th", "player:'Self-styled' council?", "npc:Many centuries ago, a small group from a", "npc:They decided that every 10 years they wo", "player:So they're pretty influential people?", "npc:No, not really.", "player:I see...", "npc:I don't really know what he thinks.", "npc:Either way I'm not prepared to let him g", "npc:And given that this is partly your fault", "player:MY fault??", "npc:Yes.", "player:I just don't understand any of this...", "npc:What exactly is it that you don't unders", "options",
            "choose:Did this room get bigger?", "player:Uh...", "npc:Ah, that would be the time dilation effe", "player:The... time dilation... effect?", "npc:Yes.", "npc:than normal.", "player:Erm... what?", "npc:As everybody knows, time flows from nort", "player:Um... does that even make sense?", "npc:Don't let it worry you too much.", "npc:Anything else you wanted to know?", "options",
            "choose:Nothing thanks.", "player:*", "npc:*", "end" })

        -- [2126]: the menu, and the goblins' row (sub 1).
        local mr, m = t.ui.journal_open("Recipe for Disaster")
        t.check("journal.menu", mr == "ok" and m and m.title ~= nil and m.line_count == 0,
            tostring(mr) .. " title " .. tostring(m and m.title) .. ", " .. tostring(m and m.line_count) .. " rows")
        -- 3757 builds 8 hidden rows, then per guest a row, its head(s) and its
        -- name; the goblins (member 1) have two heads: their row is child 11.
        local rr, row = t.ui.widget("questjournal:textlayer", 11)
        t.check("journal.row.goblins.found", rr == "ok" and row ~= nil, "child 11: " .. tostring(rr))
        if rr == "ok" and row ~= nil then
            t.expect("journal.row.goblins", t.ui.invoke(row, 1))
        end
        t.ticks(2)
        local gr, g = t.ui.journal_open()
        t.check("journal.goblins", gr == "ok" and g and string.find(g.title, "Wartface", 1, true) ~= nil,
            tostring(gr) .. " title " .. tostring(g and g.title))
        if gr == "ok" then
            t.ui.journal_close()
        end

        -- [2209]..[2213]
        t.exec("portal.out", t.player.click_loc, "hundred_portal_door2", 1)
        t.ticks(5)
        local tr, tt = t.world.tile()
        t.check("portal.landed", tr == "ok" and tt.x == 3213 and tt.z == 3222, tostring(tr) .. " " .. tostring(tt and tt.x) .. "," .. tostring(tt and tt.z))

        -- Back in through each entrance and out through each barrier: the
        -- double doors walk the player in to the room's (10,11) (4402 [114]..
        -- [118]), the Cook's door to (5,5) through barrier 1 (4402 [1701]..
        -- [1704]); barrier 2 puts them at 3213,3222, barrier 1 at 3207,3217.
        local function in_copy_at(name, dx, dz)
            local r, w = t.world.tile()
            t.check(name, r == "ok" and w.x > 6000 and w.x % 64 == dx and w.z % 64 == dz,
                tostring(r) .. " " .. tostring(w and w.x) .. "," .. tostring(w and w.z) .. " want copy " .. dx .. "," .. dz)
        end
        local function landed(name, x, z)
            local r, w = t.world.tile()
            t.check(name, r == "ok" and w.x == x and w.z == z, tostring(r) .. " " .. tostring(w and w.x) .. "," .. tostring(w and w.z))
        end
        t.exec("doubledoor.in", t.player.click_loc, "hundred_lumbridge_doubledoorl", 1)
        t.ticks(8)
        in_copy_at("doubledoor.walked_in", 10, 11)
        t.exec("barrier2.out", t.player.click_loc, "hundred_portal_door2", 1)
        t.ticks(5)
        landed("barrier2.landed", 3213, 3222)
        t.exec("cookdoor.in", t.player.click_loc, "hundred_lumbridge_door", 1)
        t.ticks(6)
        in_copy_at("cookdoor.walked_in", 5, 5)
        t.exec("barrier1.out", t.player.click_loc, "hundred_portal_door1", 1)
        t.ticks(5)
        landed("barrier1.landed", 3207, 3217)
        t.finish(0)
    end,
}
