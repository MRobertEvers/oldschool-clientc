-- Quest journals against what Jagex sends (2026-10-27): each state is set
-- with ::setvar, the journal opened through the quest list, and its rows read.
--   The Ides of Milk, complete: the 33 rows of RSProx index 4381 [870], exactly.
--   Pirate's Treasure, stage 1: the 6 rows of RSProx index 4378 [74], exactly.
--   Observatory Quest, stages 2, 3, 4 and 6: RSProx index 4412, exactly.
--   Children of the Sun, stages 2, 6 and 12: RSProx index 4408, exactly.
--   Tale of the Righteous, stages 5, 6, 14 and 16: RSProx index 4425, exactly.
--   Misthalin Mystery and Vampyre Slayer (the wiki's journal transcripts): every
--   state opens with its current step.
-- The driver reads a row's text without its tags, so rows are compared as plain
-- text (the wrap points); the <str> and colour tags are checked on a
-- TORIRSSERVER_TRACE_OUT=2 run.
local function plain(s)
    return (string.gsub(s, "<[^>]*>", ""))
end
-- The driver reads a row trimmed; Jagex's rows can open or close with a space
-- (Dwarf Cannon's "<col=000080> in the dwarves...", "...the Captain. "), which
-- our wire keeps (a TORIRSSERVER_TRACE_OUT=2 run), so the compare trims too.
local function plain_read(s)
    return (string.gsub(plain(s), "^%s*(.-)%s*$", "%1"))
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

-- Fishing Contest, RSProx index 4404: stage 2 [677], stage 4 [1252..1261].
local FC_4404_2 = {
    "<str>The Dwarves will let me use the tunnel through White Wolf",
    "<str><str>Mountain if I can win the Hemenster Fishing Competition",
    "<col=000080>My <col=800000>fishing spot<col=000080> in the contest is <col=800000>next to the willow tree",
    "<col=000080>I need to catch the <col=800000>biggest fish<col=000080> to win!",
}
local FC_4404_4 = {
    "<str>The Dwarves will let me use the tunnel through White Wolf",
    "<str><str>Mountain if I can win the Hemenster Fishing Competition.",
    "<str>I easily won the contest by catching some Giant Carp.",
    "<col=000080>I should take the <col=800000>Trophy<col=000080> back to the <col=800000>Dwarf<col=000080> at the side of <col=800000>White",
    "<col=800000>Wolf Mountain<col=000080> and claim my <col=800000>reward",
}

-- Recruitment Drive, stage 1: index 4413 [132] (before Sir Tiffy took the
-- player in) and index 4414 [134] (rd_spoke_to_tiffy).
local RD_STRUCK = {
    "<str>Sir Amik Varze told me that he had put my name forward as a",
    "<str>potential member of some mysterious organisation.",
}
local RD_4413 = {
    RD_STRUCK[1], RD_STRUCK[2],
    "<col=000080>I should head to <col=800000>Falador Park<col=000080> to meet my <col=800000>Contact<col=000080>, so that I",
    "<col=000080>can begin my <col=800000>testing for the job<col=000080>.",
}
local RD_4414 = {
    RD_STRUCK[1], RD_STRUCK[2],
    "<col=000080>A man named <col=800000>Tiffy<col=000080> brought me <col=800000>here, to the secret training",
    "<col=800000>grounds<col=000080> so that I could be tested for the job.",
    "",
    "<col=000080>I should <col=800000>work out<col=000080> what I am supposed to do to complete these",
    "<col=000080>rooms...",
}

-- Dwarf Cannon, RSProx index 4409: stage 1 [219] and with the six railings
-- fixed [561], 2 [573], 4 [640], 6 [943], 7 [1083], 8 [1140], 10 [1476].
local MC_START = {
    "<str>I have spoken to Captain Lawgof, he recruited me into the",
    "<str>Black Guard and asked me to help the dwarves.",
    "",
}
local function mc_rows(...)
    local rows = {}
    for _, row in ipairs(MC_START) do rows[#rows + 1] = row end
    for _, row in ipairs({ ... }) do rows[#rows + 1] = row end
    return rows
end
local MC_4409 = {
    { 1, false, mc_rows("<col=000080>My first task is to <col=800000>fix the broken railings",
        "<col=000080> in the dwarves defensive perimeter.") },
    { 1, true, mc_rows("<str>I have repaired all the broken railings,",
        "<col=000080>I should report back to Captain Lawgof.") },
    { 2, true, mc_rows("<str>I have repaired all the broken railings,",
        "<col=000080> Captain Lawgof has asked me to check up on his guards at",
        "<col=000080><col=800000>the watchtower <col=000080> to the South of his camp.") },
    { 4, true, mc_rows("<str>I gave the remains to Captain Lawgof.",
        "<col=000080>He sent me to find the <col=800000>Goblin base<col=000080>, South-east of the camp.") },
    { 6, true, mc_rows("<str>I have rescued Lollk and sent him back to the Captain. ",
        "<col=000080>I need to <col=800000>speak to Captain Lawgof<col=000080> again.") },
    { 7, true, mc_rows("<col=000080>Captain Lawgof has asked me to <col=800000>fix the multicannon<col=000080>.") },
    { 8, true, mc_rows("<str>I've fixed the broken multicannon,",
        "<col=000080>I need to <col=800000>speak to Captain Lawgof<col=000080> again.") },
    { 10, true, mc_rows("<str>I've spoken to Nulodion,", "<str>He gave me an ammo mould and notes,",
        "<col=000080>I need to <col=800000>speak to Captain Lawgof<col=000080> again.") },
}

-- Observatory Quest, RSProx index 4412: stage 2 [607] (the bronze bar not
-- carried), stage 3 [634] (the glass carried), stage 4 [674] (no mould),
-- stage 6 [1180].
local OBS_START = {
    "<str>I can start this quest by talking to the professor, in the",
    "<str>Observatory reception, south-west of Ardougne.",
}
local OBS_LIST = {
    "<col=000080>Seems the Observatory telescope needs repairing, due to the",
    "<col=000080>nearby goblins. The <col=800000>professor<col=000080> wants me to help by getting the",
    "<col=000080>following, with the help of his <col=800000>assistant<col=000080>:",
    "",
}
local function obs_rows(...)
    local rows = {}
    for _, part in ipairs({ ... }) do
        for _, row in ipairs(part) do rows[#rows + 1] = row end
    end
    return rows
end
local OBS_4412_2 = obs_rows(OBS_START, OBS_LIST, { "<str>3 plain wooden planks", "<col=800000>1 bronze bar" })
local OBS_4412_3 = obs_rows(OBS_START, OBS_LIST, { "<str>3 plain wooden planks", "<str>1 bronze bar", "<str>1 molten glass" })
local OBS_4412_4 = obs_rows(OBS_START, {
    "<col=000080>Seems the observatory telescope needs repairing due to the",
    "<col=000080>nearby goblins. The <col=800000>professor<col=000080> wants me to help by getting the",
    "<col=000080>following with the help of his <col=800000>assistant<col=000080>:",
    "",
}, { "<str>3 plain wooden planks", "<str>1 bronze bar", "<str>1 molten glass", "<col=800000>1 lens mould" })
local OBS_4412_6 = obs_rows(OBS_START, {
    "<str>Seems the Observatory telescope needs repairing, due to the",
    "<str>nearby goblins. The professor wants me to help by getting the",
    "<str>following, with the help of his assistant:",
    "",
    "<str>3 plain wooden planks",
    "<str>1 bronze bar",
    "<str>1 molten glass",
    "<str>1 lens mould",
    "<str>The professor was pleased to have all the pieces needed to",
    "<str>fix the telescope. Apparently, the professor's last attempt",
    "<str>at Crafting ended in disaster. So, he wants me to create the",
    "<str>lens by using the molten glass with the mould. Fine by me!",
    "<col=000080>The <col=800000>professor<col=000080> has gone ahead to the <col=800000>Observatory<col=000080>. He wants",
    "<col=000080>me to meet him there by travelling through the <col=800000>dungeon<col=000080> below",
    "<col=000080>it.",
    -- and a blank row [1180] qj18, which the reader cannot tell from the end
})

-- Children of the Sun, RSProx index 4408: stage 2 [209], 6 [365], 12 [824]
-- (and the same 14 rows at [889], [899]).
local COTS_STRUCK_1 = {
    "<str>I spoke to Alina and Noah in Varrock Square. They explained",
    "<str>that a Delegation from the Kingdom of Varlamore would soon",
    "<str>be arriving to finalise the opening up of the kingdom to",
    "<str>outsiders.",
}
local COTS_4408_2 = {
    "<col=000080>I spoke to <col=800000>Alina<col=000080> and <col=800000>Noah<col=000080> in <col=800000>Varrock Square<col=000080>. They seemed",
    "<col=000080>quite excited about something. I should talk to them again and",
    "<col=000080>see what it is.",
}
local COTS_4408_6 = {
    COTS_STRUCK_1[1], COTS_STRUCK_1[2], COTS_STRUCK_1[3], COTS_STRUCK_1[4],
    "<col=000080>The <col=800000>Delegation<col=000080> from <col=800000>Varlamore<col=000080> arrived and entered the",
    "<col=000080><col=800000>Palace<col=000080>. However, as they did, I spotted a <col=800000>Guard<col=000080> acting",
    "<col=000080>suspicious. I should follow this <col=800000>Guard<col=000080> and see what they're up",
    "<col=000080>to.",
}
local COTS_4408_12 = {
    COTS_STRUCK_1[1], COTS_STRUCK_1[2], COTS_STRUCK_1[3], COTS_STRUCK_1[4],
    "<str>The Delegation from Varlamore arrived and entered the",
    "<str>Palace. However, as they did, I spotted a Guard acting",
    "<str>suspicious. I followed this Guard to a meeting with some",
    "<str>Bandits. The Guard gave the Bandits some disguises for them",
    "<str>to use to attack the Delegation.",
    "<col=000080>I reported what I'd discovered to <col=800000>Sergeant Tobyn<col=000080> in <col=800000>Varrock",
    "<col=800000>Square<col=000080>. He asked me to point out the <col=800000>Bandits<col=000080> disguised as",
    "<col=000080><col=800000>Guards<col=000080>. There are four for me to find, with all four either",
    "<col=000080>being in <col=800000>Varrock Square<col=000080> or on the road between the <col=800000>Square<col=000080>",
    "<col=000080>and the <col=800000>Eastern Bank<col=000080>.",
}

-- Tale of the Righteous, RSProx index 4425: stage 5 [894], 6 [914], 14 [1975], 16 [2074].
local TOR_4425_5 = {
    "<str>I spoke to Phileas Rimor in Shayzien. He recently received a",
    "<str>parcel containing evidence to suggest that his ancestor,",
    "<str>Magnus Rimor, was murdered during an expedition to Mount",
    "<str>Quidamortem many years ago. He asked me to visit the",
    "<str>Library Historical Archive to learn more about this expedition.",
    "<str>I spoke to Pagida in the Library Historical Archive. He didn't",
    "<str>have any information on the expedition but did agree to let",
    "<str>me access the Tower of Magic Prison which was built around",
    "<str>the time the expedition took place.",
    "<col=000080>I entered the <col=800000>Tower of Magic Prison<col=000080> and managed to unlock",
    "<col=000080>one of the cells. Inside, I found a skeleton and a strange",
    "<col=000080>message about <col=800000>Lizards<col=000080> carved into the floor. I should let",
    "<col=000080><col=800000>Phileas<col=000080> know about this.",
}
local TOR_4425_6 = {
    "<str>I spoke to Phileas Rimor in Shayzien. He recently received a",
    "<str>parcel containing evidence to suggest that his ancestor,",
    "<str>Magnus Rimor, was murdered during an expedition to Mount",
    "<str>Quidamortem many years ago. He asked me to visit the",
    "<str>Library Historical Archive to learn more about this expedition.",
    "<str>I spoke to Pagida in the Library Historical Archive. He didn't",
    "<str>have any information on the expedition but did agree to let",
    "<str>me access the Tower of Magic Prison which was built around",
    "<str>the time the expedition took place.",
    "<str>I entered the Tower of Magic Prison and managed to unlock",
    "<str>one of the cells. Inside, I found a skeleton and a strange",
    "<str>message about Lizards carved into the floor.",
    "<col=000080>I told <col=800000>Phileas<col=000080> about the message and he's now convinced that",
    "<col=000080>someone survived the expedition. He'd like me to conduct an",
    "<col=000080>expedition of my own to try and find out what the original",
    "<col=000080>team discovered. I'll need to get permission from <col=800000>Lord Shiro",
    "<col=800000>Shayzien<col=000080> to conduct research on <col=800000>Mount Quidamortem<col=000080>. I can",
    "<col=000080>find him in the <col=800000>War Tent<col=000080> in the <col=800000>Shayzien Encampment<col=000080>.",
}
local TOR_4425_14 = {
    "<str>I spoke to Phileas Rimor in Shayzien. He recently received a",
    "<str>parcel containing evidence to suggest that his ancestor,",
    "<str>Magnus Rimor, was murdered during an expedition to Mount",
    "<str>Quidamortem many years ago. He asked me to visit the",
    "<str>Library Historical Archive to learn more about this expedition.",
    "<str>I spoke to Pagida in the Library Historical Archive. He didn't",
    "<str>have any information on the expedition but did agree to let",
    "<str>me access the Tower of Magic Prison which was built around",
    "<str>the time the expedition took place.",
    "<str>I entered the Tower of Magic Prison and managed to unlock",
    "<str>one of the cells. Inside, I found a skeleton and a strange",
    "<str>message about Lizards carved into the floor.",
    "<str>I told Phileas about the message which convinced him that",
    "<str>someone survived the expedition. He asked me to conduct an",
    "<str>expedition of my own to try and find out what the original",
    "<str>team discovered.",
    "<col=000080>Following the advice of <col=800000>Historian Duffy<col=000080>, I entered a cave on",
    "<col=000080>the western face of <col=800000>Mount Quidamortem<col=000080>. Inside, I made some",
    "<col=000080>interesting discoveries. I found out that the cave was the",
    "<col=000080>origin point of the <col=800000>Lizardman<col=000080> species and that they were",
    "<col=000080>created during the time of <col=800000>King Shayzien VII<col=000080>. As well as this, I",
    "<col=000080>discovered a <col=800000>Strange Altar<col=000080> which seemed to be acting as an",
    "<col=000080>amplifier for the <col=800000>Dark Altar<col=000080>. I should return to <col=800000>Lord Shayzien<col=000080>",
    "<col=000080>and let him know what I've discovered.",
}
local TOR_4425_16 = {
    "<str>I spoke to Phileas Rimor in Shayzien. He recently received a",
    "<str>parcel containing evidence to suggest that his ancestor,",
    "<str>Magnus Rimor, was murdered during an expedition to Mount",
    "<str>Quidamortem many years ago. He asked me to visit the",
    "<str>Library Historical Archive to learn more about this expedition.",
    "<str>I spoke to Pagida in the Library Historical Archive. He didn't",
    "<str>have any information on the expedition but did agree to let",
    "<str>me access the Tower of Magic Prison which was built around",
    "<str>the time the expedition took place.",
    "<str>I entered the Tower of Magic Prison and managed to unlock",
    "<str>one of the cells. Inside, I found a skeleton and a strange",
    "<str>message about Lizards carved into the floor.",
    "<str>I told Phileas about the message which convinced him that",
    "<str>someone survived the expedition. He asked me to conduct an",
    "<str>expedition of my own to try and find out what the original",
    "<str>team discovered.",
    "<str>Following the advice of Historian Duffy, I entered a cave on",
    "<str>the western face of Mount Quidamortem. Inside, I made some",
    "<str>interesting discoveries. I found out that the cave was the",
    "<str>origin point of the Lizardman species and that they were",
    "<str>created during the time of King Shayzien VII. As well as this, I",
    "<str>discovered an altar which seemed to be acting as an amplifier",
    "<str>for the Dark Altar.",
    "<str>I told Lord Shayzien about my discoveries on Mount",
    "<str>Quidamortem. He wanted to learn more about the parcel that",
    "<str>Phileas Rimor received and asked me to go and retrieve it.",
    "<col=000080>I went to the home of <col=800000>Phileas Rimor<col=000080> but I found the place",
    "<col=000080>trashed and <col=800000>Phileas<col=000080> gone. I should tell <col=800000>Lord Shayzien<col=000080> about",
    "<col=000080>this.",
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
                if lines[i] ~= (want[i] and plain_read(want[i])) then
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

        set("::setvar varp11_fishingcompo 2")
        exact("journal.fishingcontest.willow", "Fishing Contest", FC_4404_2)
        set("::setvar varp11_fishingcompo 4")
        exact("journal.fishingcontest.won", "Fishing Contest", FC_4404_4)

        set("::setvar varb657_rd_main 1")
        set("::setvar varb692_rd_spoke_to_tiffy 0")
        exact("journal.recruitmentdrive.park", "Recruitment Drive", RD_4413)
        set("::setvar varb692_rd_spoke_to_tiffy 1")
        exact("journal.recruitmentdrive.grounds", "Recruitment Drive", RD_4414)

        for _, st in ipairs(MC_4409) do
            local fixed = st[2] and 1 or 0
            for b = 2240, 2245 do
                set("::setvar varb" .. b .. "_mcannon_railing" .. (b - 2239) .. "_fixed " .. fixed)
            end
            set("::setvar varp0_mcannon " .. st[1])
            exact("journal.dwarfcannon." .. st[1] .. (st[2] and "" or ".railings"), "Dwarf Cannon", st[3])
        end

        set("::setvar varp112_itgronigen 2")
        exact("journal.observatory.bronze", "Observatory Quest", OBS_4412_2)
        set("::give molten_glass 1")
        set("::setvar varp112_itgronigen 3")
        exact("journal.observatory.glass", "Observatory Quest", OBS_4412_3)
        set("::clearinv")
        set("::setvar varp112_itgronigen 4")
        exact("journal.observatory.mould", "Observatory Quest", OBS_4412_4)
        set("::setvar varp112_itgronigen 6")
        exact("journal.observatory.gone_ahead", "Observatory Quest", OBS_4412_6)

        set("::setvar varb9632_vmq1 2")
        exact("journal.childrenofthesun.2", "Children of the Sun", COTS_4408_2)
        set("::setvar varb9632_vmq1 6")
        exact("journal.childrenofthesun.6", "Children of the Sun", COTS_4408_6)
        set("::setvar varb9632_vmq1 12")
        exact("journal.childrenofthesun.12", "Children of the Sun", COTS_4408_12)

        for _, st in ipairs({ { 5, TOR_4425_5 }, { 6, TOR_4425_6 }, { 14, TOR_4425_14 }, { 16, TOR_4425_16 } }) do
            set("::setvar varb6358_shayzienquest " .. st[1])
            exact("journal.taleoftherighteous." .. st[1], "Tale of the Righteous", st[2])
        end

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
