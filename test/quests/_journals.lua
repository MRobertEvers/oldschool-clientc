-- Quest journals against what Jagex sends (2026-10-27): each state is set
-- with ::setvar, the journal opened through the quest list, and its rows read.
--   The Ides of Milk, complete: the 33 rows of RSProx index 4381 [870], exactly.
--   Pirate's Treasure, stage 1: the 6 rows of RSProx index 4378 [74], exactly.
--   Misthalin Mystery and Vampyre Slayer (the wiki's journal transcripts): every
--   state opens with its current step.
-- The driver reads a row's text without its tags, so rows are compared as plain
-- text (the wrap points); the <str> and colour tags are checked on a
-- TORIRSSERVER_TRACE_OUT=2 run.
local function plain(s)
    return (string.gsub(s, "<[^>]*>", ""))
end
local IOM_4381 = {
    "<str>I have met an enigmatic man by the name of Cassius lurking",
    "<str>around Farmer Fred's farm in Lumbridge.",
    "<str>Cassius has offered me the chance to share the profits of",
    "<str>his upcoming milk business, in exchange for a bit of help",
    "<str>setting it up.",
    "<str>Having spoken to Seth and Gillie Groats, I found that they",
    "<str>raise their animals following a set of principles that Seth",
    "<str>keeps in a book. I took that book to Cassius as it may have",
    "<str>been useful to his budding milk business.",
    "<str>Having returned Seth's book to Cassius, he then offered me",
    "<str>a sample of the milk we will be selling. It wasn't the most",
    "<str>pleasant thing to drink, but I managed to finish it.",
    "<str>The next step in getting our business set up was to get a",
    "<str>business permit. To that end, Cassius sent me to the Duke of",
    "<str>Lumbridge with another milk sample, aiming to convince him to",
    "<str>let us begin trading.",
    "<str>I spoke to the Duke of Lumbridge about getting a business",
    "<str>permit for Cassius' future milk empire, but unfortunately the",
    "<str>Duke couldn't grant us one without us speaking to the Groats",
    "<str>family first.",
    "<str>I spoke to Gillie at the Duke of Lumbridge's request, and she",
    "<str>asked me to drink the milk sample so that she could get a",
    "<str>sense of the quality of the milk. It wasn't pleasant, but",
    "<str>seemed to bring her around to my cause.",
    "<str>Gillie sent me to deal with the aggressive bull that she had",
    "<str>fenced in her field. It was a tough fight but I brought the",
    "<str>beast down. Unfortunately it turned out the the bull was",
    "<str>Brutus, who I believed to be Cassius business partner.",
    "<str>I have apologised to Cassius for killing Brutus, but",
    "<str>unfortunately this may be a deathblow to his potential milk",
    "<str>empire. It's unlikely he's going to want to see my face again.",
    "",
    "<col=ff0000>QUEST COMPLETE!",
}
local HUNT_4378 = {
    "",
    "<col=000080>I have spoken to <col=800000>Redbeard Frank<col=000080>. He has agreed to tell me the",
    "<col=000080>location of some <col=800000>treasure<col=000080> for some <col=800000>Karamja Rum<col=000080>.",
    "",
    "<col=000080>I need to go to <col=800000>Karamja<col=000080> and buy some <col=800000>rum<col=000080>. I hope it is not too",
    "<col=000080>expensive.",
}

return {
    id = "journals",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv" },
    run = function(t)
        t.ticks(3)
        local function read(display)
            local r, j = t.ui.journal_open(display)
            if r == "ok" then
                t.ui.journal_close()
            end
            return r, j
        end
        local function exact(name, display, want)
            local r, j = read(display)
            local lines = (r == "ok" and j and j.lines) or {}
            local diff = nil
            for i = 1, math.max(#want, #lines) do
                if lines[i] ~= (want[i] and plain(want[i])) then
                    diff = "row " .. i .. ": want '" .. tostring(want[i]) .. "' read '" .. tostring(lines[i]) .. "'"
                    break
                end
            end
            t.check(name, r == "ok" and diff == nil, tostring(r) .. " " .. #lines .. " rows; " .. (diff or "every row matches"))
        end
        local function shape(name, display, struck_min, current)
            local r, j = read(display)
            local lines = (r == "ok" and j and j.lines) or {}
            local found = false
            for _, l in ipairs(lines) do
                if current and string.find(l, current, 1, true) then found = true end
            end
            t.check(name, r == "ok" and #lines > struck_min and (current == nil or found),
                tostring(r) .. " " .. #lines .. " rows; current '" .. tostring(current) .. "' " .. (found and "found" or "MISSING"))
        end
        local function set(cmd)
            t.cheat(cmd, false)
            t.ticks(2)
        end

        set("::setvar varb20106_cowquest 22")
        exact("journal.idesofmilk.complete", "The Ides of Milk", IOM_4381)
        set("::setvar varb20106_cowquest 12")
        shape("journal.idesofmilk.duke", "The Ides of Milk", 10, "I spoke to the Duke of Lumbridge")

        set("::setvar varp71_hunt 1")
        exact("journal.hunt.rum", "Pirate's Treasure", HUNT_4378)

        local MM = {
            { 5, 1, "I agreed to sail to the island" },
            { 20, 3, "I need to stop the killer" },
            { 90, 6, "I must investigate the manor" },
            { 105, 3, "I must confront the killer." },
            { 120, 4, "They turned on one another" },
            { 130, 5, "Mandy left to tend to her wounds" },
            { 135, 5, "QUEST COMPLETE!" },
        }
        for _, s in ipairs(MM) do
            set("::setvar varb3468_mistmyst_progress " .. s[1])
            shape("journal.misthalin." .. s[1], "Misthalin Mystery", s[2], s[3])
        end

        set("::setvar varp178_vampire 1")
        shape("journal.vampire.1", "Vampyre Slayer", 0, "suggested I begin by seeking out")
        set("::setvar varp178_vampire 2")
        shape("journal.vampire.2", "Vampyre Slayer", 3, "seem willing to talk to me")
        set("::setvar varb15609_harlow_given_beer 1")
        shape("journal.vampire.beer", "Vampyre Slayer", 3, "I should see what he can tell me")
        set("::setvar varb15610_harlow_given_stake 1")
        shape("journal.vampire.stake", "Vampyre Slayer", 3, "If I lose it")
        set("::setvar varp178_vampire 3")
        shape("journal.vampire.complete", "Vampyre Slayer", 6, "QUEST COMPLETE!")

        -- The strip icons carry no server events (cache clickmask=2; Jagex arms
        -- none, RSProx index 4289 [0], [509]) and still switch the body.
        t.exec("strip.tab", t.ui.tab, "quests")
        t.ticks(4)
        local _, summary_icon = t.ui.widget("side_journal:summary_list")
        t.expect("strip.summary", t.ui.invoke(summary_icon, 1))
        t.expect("strip.summary.open", t.ui.await_open("account_summary_sidepanel", 5))
        local _, quest_icon = t.ui.widget("side_journal:quest_list")
        t.expect("strip.quests", t.ui.invoke(quest_icon, 1))
        t.expect("strip.quests.open", t.ui.await_open("questlist", 5))
        t.ticks(2)

        -- Row ops on Pandemonium (quest id 206): Show on map opens the world
        -- map (index 3430 [376]); the quick guide prints Jagex's line (3055 [22]).
        local _, row = t.ui.widget("questlist:list", 206)
        t.expect("row.quickguide", t.ui.invoke(row, 5))
        t.ticks(2)
        t.expect("row.quickguide.line", t.msg.expect("A quick guide for Pandemonium has been opened in the Old School RuneScape Wiki."))
        t.expect("row.showonmap", t.ui.invoke(row, 3))
        t.expect("row.showonmap.open", t.ui.await_open("worldmap", 5))
        t.finish(0)
    end,
}
