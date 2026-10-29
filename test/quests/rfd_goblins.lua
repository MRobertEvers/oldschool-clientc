-- Recipe for Disaster: Freeing the Goblin generals, as RSProx index 4402
-- (2026-10-29, rev 241) played it: the frozen generals and Aris [135]..[156],
-- the generals in the Goblin Village [461]..[565], the kitchen ladder [1040],
-- Mudknuckles before the charcoal [1054]..[1110] and with it [1149]..[1165],
-- the explosion scene [1165]..[1259], his charred self [1270]..[1342], the
-- ingredients [1362]..[1496], the hand-in [1498]..[1512], the ladder up
-- [1524], and the slop on the generals with the scroll and Aris [1750]..[1762].
--
-- Every page is the recorded one on the recorded choices ("kind:fragment" is
-- the page's first row). The journal is read at every stage 4402 opens it
-- (0, 5, 10, 15, 30, 35, 40), row for row as plain text. The run keeps
-- build/quest_gate/<run>/ticklog.tsv for the wire audit in
-- docs/rsprox-analysis/quests/rfd_goblins.md.
local JOURNAL = {
    [0] = {
        "I have to free Wartface and Bentnoze! I can start by",
        "examining their frozen selves in Lumbridge Castle.",
        "",
        "This sub-quest has the following requirements:",
        "Goblin Diplomacy",
    },
}
local P1 = { "I have to protect the goblins Wartface and Bentnoze!",
    "I must have completed the Goblin Diplomacy Quest.",
    "I should inspect the goblins' frozen selves in the Lumbridge",
    "Castle Dining room." }
local ARIS = { "Aris admitted that nobody was sure which goblin was",
    "supposed to be on the council. Therefore I need to find a dish",
    "that both Wartface and Bentnoze would like. I should speak to",
    "the goblins in their village North of Falador to find out what",
    "might work." }
local COOK_NOW = { "I met a cook named Mudknuckles in the village who may be able",
    "to help me, but he insisted that he had to complete his",
    "experiment first. To do this he needs some charcoal." }
local COOK_DONE = { "I met a cook in the village who may be able to help me, but he",
    "insisted that he had to complete his experiment first. To do",
    "this he needs some charcoal." }
local BOOM = { "I got Mudknuckles the charcoal he required. His experiment",
    "had interesting results. Much to my surprise Mudknuckles",
    "survived, maybe now he will help me." }
local NEED = { "Mudknuckles told me that he has tried to create a dish that",
    "both Wartface and Bentnoze would like before. However he",
    "ran into some problems. I need...",
    "Orange slices which aren't orange.",
    "Maggots that aren't bland.",
    "Bread which isn't crunchy." }
local SLOP_NOW = { "I managed to devise all the ingredients that Mudknuckles",
    "required. He made me a goblin slop of compromise which I",
    "should use to protect Wartface and Bentnoze from the",
    "culinaromancer's spell." }
local SLOP_DONE = { "I managed to devise all the ingredients that Mudknuckles",
    "required. He made me a goblin slop of compromise which I",
    "should use to free Wartface and Bentnoze back in Lumbridge",
    "castle.",
    "The slop of compromise worked and I was able to free the",
    "goblins.",
    "",
    "QUEST COMPLETE!" }
local function cat(...)
    local out = {}
    for _, list in ipairs({ ... }) do
        for _, row in ipairs(list) do
            out[#out + 1] = row
        end
    end
    return out
end
JOURNAL[5] = cat(P1, ARIS)
JOURNAL[10] = cat(P1, ARIS, COOK_NOW)
JOURNAL[15] = cat(P1, ARIS, COOK_DONE, BOOM)
JOURNAL[30] = cat(P1, ARIS, COOK_DONE, BOOM, NEED)
JOURNAL[35] = cat(P1, ARIS, COOK_DONE, BOOM, NEED, SLOP_NOW)
JOURNAL[40] = cat(P1, ARIS, COOK_DONE, BOOM, NEED, SLOP_DONE)

local COOK_OPEN = { "npc:Go away human, me's busy here!", "player:Busy doing what?", "npc:Me's working on new way of cooking." }
local CHARRED_OPEN = { "npc:You the human who helped me.", "npc:I thinking of calling it fast food" }

return {
    id = "rfd_goblins",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::questmark quest_cooksassistant",
        "::questmark quest_goblindiplomacy",
        "::setvar varb1850_hundred_main_quest_var 3",
        "::setvar varb1859_100goblinsvisible 1",
        "::setlevel cooking 10",
        -- What the recorder bought at the Grand Exchange before setting out
        -- (4402 [248]..[346]); the charcoal is not offered until he asks.
        "::give charcoal 1",
        "::give bread 1",
        "::give bucket_water 1",
        "::give knife 1",
        "::give orange 1",
        "::give bluedye 1",
        "::give fishing_bait 1",
        "::give gnome_spice 1",
        "::goto 3208 3214 0",
    },

    run = function(t)
        t.note(select(2, t.ticklog.start()))
        t.ticks(3)
        local function var(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or nil
        end
        local function stage(name, want)
            local v = var("varb1867_100goblin")
            t.check(name, v == want, "100goblin " .. tostring(v) .. ", want " .. want)
        end
        local function talk(name, npc, list)
            t.exec(name, t.player.talk_to, npc, 1)
            t.expect(name .. ".pages", t.chat.play(list))
        end
        local function with(head, list)
            return cat(head, list)
        end
        local function in_copy_at(name, dx, dz)
            local r, w = t.world.tile()
            t.check(name, r == "ok" and w.x > 6000 and w.x % 64 == dx and w.z % 64 == dz,
                tostring(r) .. " " .. tostring(w and w.x) .. "," .. tostring(w and w.z) .. " want copy " .. dx .. "," .. dz)
        end
        local function landed(name, x, z)
            local r, w = t.world.tile()
            t.check(name, r == "ok" and w.x == x and w.z == z, tostring(r) .. " " .. tostring(w and w.x) .. "," .. tostring(w and w.z))
        end
        -- The subquest's journal through the Recipe for Disaster menu: the
        -- goblins' row is textlayer child 11 (rfd_start.lua).
        local function journal(st)
            local name = "journal." .. st
            local mr = t.ui.journal_open("Recipe for Disaster")
            if mr ~= "ok" then
                t.check(name, false, "menu " .. tostring(mr))
                return
            end
            local rr, row = t.ui.widget("questjournal:textlayer", 11)
            if rr ~= "ok" or row == nil then
                t.check(name, false, "child 11: " .. tostring(rr))
                t.ui.journal_close()
                return
            end
            t.ui.invoke(row, 1)
            t.ticks(2)
            local jr, j = t.ui.journal_open()
            local want = JOURNAL[st]
            local lines = (jr == "ok" and j and j.lines) or {}
            local diff = nil
            for i = 1, math.max(#want, #lines) do
                if lines[i] ~= want[i] then
                    diff = "row " .. i .. ": want '" .. tostring(want[i]) .. "' read '" .. tostring(lines[i]) .. "'"
                    break
                end
            end
            t.check(name, jr == "ok" and diff == nil and j.title ~= nil and string.find(j.title, "Wartface", 1, true) ~= nil,
                tostring(jr) .. " " .. tostring(j and j.title) .. ", " .. #lines .. " rows; " .. (diff or "every row matches"))
            if jr == "ok" then
                t.ui.journal_close()
            end
            t.ticks(1)
        end

        stage("stage.0", 0)
        journal(0)

        -- [1701]: into the frozen room by the Cook's door.
        t.exec("frozen.in", t.player.click_loc, "hundred_lumbridge_door", 1)
        t.ticks(6)
        in_copy_at("frozen.walked_in", 5, 5)

        -- [135]..[156]
        local inspect = { "player:Which one of these am I actually", "npc:Hmmm... neither of them were at the pre",
            "npc:The goblins get through leaders so fast", "npc:Well, no worries. I'm sure if you just",
            "player:Remembering the last time I dealt with", "npc:Well you could try asking some of the o", "end" }
        t.exec("inspect", t.player.click_loc, "hundred_goblin2_base", 1)
        t.expect("inspect.pages", t.chat.play(inspect))
        stage("stage.5", 5)
        journal(5)
        t.exec("frozen.out", t.player.click_loc, "hundred_portal_door1", 1)
        t.ticks(5)
        landed("frozen.out.landed", 3207, 3217)

        -- [461]..[565]: the generals at stage 5, each recorded answer.
        t.cheat("::goto 2958 3510 0")
        t.ticks(4)
        talk("generals.market", "general_bentnoze_red", { "options", "choose:Can I ask about what food you like?",
            "player:Can I ask about what food you like?", "npc:What? Why?", "options", "choose:Market research.",
            "player:Market research.", "npc:But us never use your markets.", "player:Exactly. That's why we need to find",
            "npc:Go away human, we busy.", "player:But it'll only take a minute of your", "npc:I said we busy!", "end" })
        talk("generals.life", "general_bentnoze_red", { "options", "choose:So how is life for the goblins?",
            "player:So how is life for the goblins?", "npc:Now you've solved our argument, we gott", "npc:Yep, we bored now.", "end" })
        talk("generals.reason", "general_wartface_green", { "options", "choose:Can I ask about what food you like?",
            "player:Can I ask about what food you like?", "npc:What? Why?", "options", "choose:Erm... no particular reason.",
            "player:Erm... no particular reason.", "npc:Me see no particular reason to answer yo", "end" })
        talk("generals.spell", "general_bentnoze_red", { "options", "choose:Can I ask about what food you like?",
            "player:Can I ask about what food you like?", "npc:What? Why?", "options", "choose:I need to save you from the Culinaromancer's spell.",
            "player:I need to save you from the Culinaroman", "npc:From who's what?", "player:At the council meeting in Lumbridge, th",
            "npc:Lumbridge...?", "npc:Oh yeah. Me remember. Me was goblin rep", "npc:No you weren't! That was me!",
            "player:Never mind that, don't you remember the", "npc:All me remember is Wartface stealin' ch",
            "npc:Weren't your chair. You shouldn' even've", "player:But...", "npc:I there before you were. So I get chair",
            "player:Never mind.", "end" })
        stage("stage.5.after.generals", 5)

        -- [1040]..[1044]: down the ladder into a copy of the kitchen.
        t.cheat("::goto 2960 3506 0")
        t.ticks(3)
        t.exec("ladder.down", t.player.click_loc, "100_goblin_ladder_down", 1)
        t.ticks(5)
        in_copy_at("kitchen.landed", 37, 59)

        -- [1054]..[1110]
        talk("cook.newway", "goblin_cook", with(COOK_OPEN, { "options", "choose:What is this new way of cooking?",
            "player:What is this new way of cooking?", "npc:Got the idea from the dwarves.", "npc:Wouldn't tell Mudknuckles how it worked",
            "options", "choose:Well, good luck with that.", "player:Well, good luck with that.", "npc:No luck. Will work cos Mudknuckles", "end" }))
        talk("cook.leave", "goblin_cook", with(COOK_OPEN, { "options", "choose:Well, I guess I'll leave you to it.",
            "player:Well, I guess I'll leave you to it.", "npc:Yes, Mudknuckles too busy to talk.", "end" }))
        talk("cook.later", "goblin_cook", with(COOK_OPEN, { "options", "choose:I need your help...",
            "player:I need your help. Wartface and Bentnoze", "npc:Oh, maybe that why Mudknuckles not been",
            "npc:Hmmm, s'pose me should help.", "options", "choose:I guess I'll come back later then.",
            "player:I guess I'll come back later then.", "end" }))
        stage("stage.5.after.cook", 5)
        talk("cook.charcoal", "goblin_cook", with(COOK_OPEN, { "options", "choose:I need your help...",
            "player:I need your help. Wartface and Bentnoze", "npc:Oh, maybe that why Mudknuckles not been",
            "npc:Hmmm, s'pose me should help.", "options", "choose:What do you need? Maybe I can get it for you.",
            "player:What do you need? Maybe I can get it", "npc:Is called charcoal.", "npc:Burnt trees, they good too.",
            "player:Ok, I'll see what I can do.", "end" }))
        stage("stage.10", 10)
        journal(10)

        -- [1149]..[1259]: the charcoal and the explosion.
        local mark = t.cutscene.mark()
        talk("cook.explosion", "goblin_cook", with(COOK_OPEN, { "options", "choose:I've got the charcoal you were after.",
            "player:I've got the charcoal you were after.", "npc:Really? Then no time to lose.", "player:Sulphur? Er...",
            "npc:Must be moving. Follow me.", "end" }))
        t.ticks(3)
        stage("stage.15", 15)
        local cr, coal = t.inv.count("charcoal")
        t.check("charcoal.taken", cr == "ok" and coal == 0, "charcoal " .. tostring(coal))
        t.ticks(100)
        t.check("explosion.varbit", var("varb1868_100goblin_explosion") == 2, "100goblin_explosion " .. tostring(var("varb1868_100goblin_explosion")))
        t.check("explosion.unlocked", var("varb542_cutscene_status") == 0, "cutscene_status " .. tostring(var("varb542_cutscene_status")))
        in_copy_at("explosion.charred_kitchen", 43, 15)
        t.note(select(2, t.cutscene.await("explosion", { since = mark, quiet = 20,
            expect = { { op = "moveto", height = 325 }, { op = "moveto", height = 475 }, { op = "moveto", height = 1000 },
                { op = "moveto", height = 600 }, { op = "moveto", height = 525 }, { op = "reset" } } })))
        journal(15)

        -- [1270]..[1342]
        talk("charred.idea", "goblin_cook_charred", with(CHARRED_OPEN, { "options", "choose:That sounds like a great idea.",
            "player:That sounds like a great idea.", "npc:You think so? Everyone else think", "npc:Can't see why. Not like you gonna get",
            "player:Er... yes... I'm sure food poisoning", "end" }))
        talk("charred.sell", "goblin_cook_charred", with(CHARRED_OPEN, { "options", "choose:Do you think that will sell?",
            "player:Do you think that will sell? Like you", "npc:So maybe some imperfections, but fast", "end" }))
        talk("charred.leave", "goblin_cook_charred", with(CHARRED_OPEN, { "options", "choose:I'll leave you to your experimentation then.",
            "player:I'll leave you to your experimentation", "end" }))
        stage("stage.15.after.talks", 15)
        talk("charred.demands", "goblin_cook_charred", with(CHARRED_OPEN, { "options", "choose:I need your help...",
            "player:I need your help...", "npc:Yes, yes, Mudknuckles very grateful", "player:Your leaders, Wartface and Bentnoze",
            "npc:So that's why Mudknuckles not been", "npc:Tried that before, Mudknuckles had", "player:I need to create a dish that both of",
            "npc:Oh...", "player:What?", "npc:Is impossible. Mudknuckles try it", "player:Why, what did they want?",
            "npc:Well Bentnoze wanted fruit in the dish.", "player:That doesn't sound too bad.", "npc:Maybe, but Bentnoze objects to the",
            "player:Anything else?", "npc:Wartface, he want some nice juicy", "player:Where can I get maggots?",
            "npc:Humans use them all the time for", "player:Each to their own indeed, but what's",
            "npc:Bentnoze not like maggots, say they're", "player:Ok, is there anything other than",
            "npc:Bentnoze asked for some bread in the mix.", "player:Let me guess, Wartface complained right?",
            "npc:Yep, Wartface think bread too crunchy.", "player:So let me get this straight.",
            "npc:Yep, 'bout sums it up.", "player:Wonderful. Well, I guess I'll have to", "end" }))
        stage("stage.30", 30)
        journal(30)
        talk("charred.maggots", "goblin_cook_charred", with(CHARRED_OPEN, { "options", "choose:I need your help...",
            "player:I need your help. About that dish that", "options", "choose:Why did Bentnoze object to the maggots?",
            "player:Why did Bentnoze object to the maggots?", "npc:Bentnoze not like maggots, say they're", "end" }))
        talk("charred.bread", "goblin_cook_charred", with(CHARRED_OPEN, { "options", "choose:I need your help...",
            "player:I need your help. About that dish that", "options", "choose:What did Wartface have against bread?",
            "player:What did Wartface have against bread?", "npc:Wartface think bread too crunchy.", "end" }))
        talk("charred.oranges", "goblin_cook_charred", with(CHARRED_OPEN, { "options", "choose:I need your help...",
            "player:I need your help. About that dish that", "options", "choose:What was the problem with the orange slices?",
            "player:What was the problem with the orange", "npc:Well Bentnoze wanted fruit in the dish.", "end" }))

        -- [1362]..[1496]: the three ingredients.
        local function count(obj)
            local _, n = t.inv.count(obj)
            return n or -1
        end
        t.player.use_item_on_item("bucket_water", "bread")
        t.ticks(2)
        t.check("soggy.bread", count("100goblin_soggy_bread") == 1 and count("bread") == 0 and count("bucket_empty") == 1,
            "soggy " .. count("100goblin_soggy_bread") .. ", bread " .. count("bread") .. ", bucket " .. count("bucket_empty"))
        t.expect("soggy.line", t.msg.expect("You soak the nice crispy bread until it becomes slimy to the touch."))
        t.player.use_item_on_item("knife", "orange")
        t.exec("orange.menu", t.ui.make, "a")
        t.exec("orange.slices", t.inv.await, "orange_slices", 1, 6)
        t.player.use_item_on_item("bluedye", "orange_slices")
        t.ticks(2)
        t.check("dyed.oranges", count("100goblin_dyed_oranges") == 1 and count("orange_slices") == 0 and count("bluedye") == 0,
            "dyed " .. count("100goblin_dyed_oranges") .. ", slices " .. count("orange_slices") .. ", dye " .. count("bluedye"))
        t.expect("dyed.line", t.msg.expect("You add the dye to the oranges. They turn an appealing greyish brown colour."))
        t.player.use_item_on_item("gnome_spice", "fishing_bait")
        t.ticks(2)
        t.check("spicy.maggots", count("100goblin_spicey_maggots") == 1 and count("fishing_bait") == 0 and count("gnome_spice") == 1,
            "maggots " .. count("100goblin_spicey_maggots") .. ", bait " .. count("fishing_bait") .. ", spice " .. count("gnome_spice"))
        t.expect("spicy.line", t.msg.expect("You add a light dusting of spices to the maggots."))

        -- [1498]..[1512]
        talk("charred.handin", "goblin_cook_charred", with(CHARRED_OPEN, { "options", "choose:I've got the ingredients we need...",
            "player:I've got the ingredients we need to make", "npc:Mudknuckles is confused.", "npc:There you go, but don't go blaming",
            "objbox:The goblin hands you a bowl", "player:Great, thanks!", "end" }))
        stage("stage.35", 35)
        t.check("slop.held", count("100goblin_compromise_mush") == 1 and count("100goblin_dyed_oranges") == 0
            and count("100goblin_soggy_bread") == 0 and count("100goblin_spicey_maggots") == 0,
            "slop " .. count("100goblin_compromise_mush"))
        journal(35)
        talk("charred.unlikely", "goblin_cook_charred", with(CHARRED_OPEN, { "options", "choose:I need your help...",
            "player:I need your help. Are you sure Wartface", "npc:Highly unlikely, but is best", "end" }))

        -- [1524]..[1527]: up the charred kitchen's ladder.
        t.exec("ladder.up", t.player.click_loc, "100_goblin_ladder_up_charred", 1)
        t.ticks(5)
        landed("ladder.up.landed", 2960, 3506)
        -- [1532]..[1535]: down again, to the charred kitchen now.
        t.exec("ladder.down.charred", t.player.click_loc, "100_goblin_ladder_down", 1)
        t.ticks(5)
        in_copy_at("ladder.down.charred.landed", 37, 19)
        t.exec("ladder.up.again", t.player.click_loc, "100_goblin_ladder_up_charred", 1)
        t.ticks(5)
        landed("ladder.up.again.landed", 2960, 3506)

        -- [1701]..[1762]: back to the generals with the slop.
        t.cheat("::goto 3208 3214 0")
        t.ticks(4)
        t.exec("frozen.in.again", t.player.click_loc, "hundred_lumbridge_door", 1)
        t.ticks(6)
        local _, snap = t.skill.snapshot()
        local qr, qp0 = t.var.server("varp101_qp")
        t.exec("slop.use", t.player.use_on, "100goblin_compromise_mush", t.player.by_symbol("loc", "hundred_goblin1_base"))
        t.expect("scroll.open", t.ui.await_open("questscroll", 10))
        stage("stage.40", 40)
        t.check("generals.gone", var("varb1859_100goblinsvisible") == 0, "100goblinsvisible " .. tostring(var("varb1859_100goblinsvisible")))
        local qr1, qp1 = t.var.server("varp101_qp")
        t.check("qp", qr == "ok" and qr1 == "ok" and qp1 == qp0 + 1, "qp " .. tostring(qp0) .. " -> " .. tostring(qp1))
        local wr, close = t.ui.widget("questscroll:close_button")
        if wr ~= "ok" or close == nil then
            t.step("scroll.close", "FAIL", "questscroll:close_button " .. tostring(wr))
            t.finish(1)
            return
        end
        t.expect("scroll.close", t.ui.invoke(close, 1))
        -- [1758]: the server answers the close with Aris's page; our client
        -- then sends CLOSE_MODAL for the scroll's "Close" and the page goes with
        -- it (rfd_start.md), so the echo is what the test can read.
        t.ticks(2)
        t.expect("aris.page", t.msg.expect("Aris|Good work adventurer, I've teleported them away to safety! You only have 7 council members left to protect!"))
        t.ticks(3)
        t.check("tally", var("varb1852_hundred_subquest_tally") == 1, "hundred_subquest_tally " .. tostring(var("varb1852_hundred_subquest_tally")))
        t.check("xp.cooking", t.skill.expect_gain("cooking", 1000, snap))
        t.check("xp.farming", t.skill.expect_gain("farming", 1000, snap))
        t.check("xp.crafting", t.skill.expect_gain("crafting", 1000, snap))
        t.ticks(2)
        journal(40)
        t.finish(0)
    end,
}
