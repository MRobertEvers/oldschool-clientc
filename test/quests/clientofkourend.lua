-- Client of Kourend as RSProx index 4415 played it (2026-10-29): Veos in Port
-- Sarim, the crossing, the first arrival's offer, the quill, the five
-- interviews in the recorded order, the report, the orb lost and replaced, the
-- Dark Altar, the possession, the scroll and both lamps. Every page below is
-- that recording's; docs/rsprox-analysis/quests/client_of_kourend.md has the
-- tick-by-tick audit.
--
-- Door rule (b71): the player sails with Veos from Port Sarim, and every walled
-- general store is entered and left through its door (pass_door).

-- The journal rows Jagex sent ([318], [345], [1086]), compared as plain text.
local function plain(s) return (string.gsub(s, "<[^>]*>", "")) end
local J_SCROLL = {
    "I have agreed to help Veos and his Mysterious Client gather",
    "information on the five cities of Great Kourend. To gather the",
    "information, Veos gave me an Enchanted Scroll to write it on.",
    "To do this I'll also need an Enchanted Quill. I can make one by",
    "using a Feather on the Enchanted Scroll.",
}
local J_QUILL = {
    "I have agreed to help Veos and his Mysterious Client gather",
    "information on the five cities of Great Kourend. To gather the",
    "information, Veos gave me an Enchanted Scroll and taught me",
    "how to make an Enchanted Quill. Veos suggested I gather the",
    "information by talking to the General Store Owners of each",
    "city.",
    "",
    "I should speak to the General Store Owners of:",
    "Arceuus",
    "Hosidius",
    "Lovakengj",
    "Port Piscarilius",
    "Shayzien",
}
local J_ORB = {
    "I agreed to help Veos and his Mysterious Client gather",
    "information on the five cities of Great Kourend. To gather the",
    "information, Veos gave me an Enchanted Scroll and taught me",
    "how to make an Enchanted Quill. Veos suggested I gather the",
    "information by talking to the General Store Owners of each",
    "city.",
    "I spoke to all of the General Store Owners. While speaking with",
    "the Arceuus Store Owner, the Dark Altar was mentioned. The",
    "mention of this caused the Enchanted Quill to glow and",
    "damage my hand.",
    "I returned to Veos who informed me that his Mysterious Client",
    "had another task for me. Veos gave me a Mysterious Orb and",
    "asked me to activate it next to the Dark Altar.",
}

local function journal(t, name, want)
    -- Needs the client (run.py): scriptrun has no quest list to press. There,
    -- ::cokjournal puts the same rows on the wire for TORIRSSERVER_TRACE_OUT.
    local r, j = t.ui.journal_open("Client of Kourend")
    if r == "ok" then t.ui.journal_close() end
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

local function count(t, obj) local _, n = t.inv.count(obj) return tonumber(n) or 0 end

-- One interview: the store owner's greeting, the ask, "What is there to do",
-- and the pages after, all from the recording.
local function interview(t, name, npc, pages)
    t.ticklog.mark("leg." .. name)
    t.exec(name, t.player.talk_to, npc, 1)
    t.exec(name .. ".chat", t.chat.play, pages)
    t.ticks(2)
end

return {
    id = "clientofkourend",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_xmarksthespot", -- prerequisite; also shows Veos on the docks
        "::give feather 1", -- the guide brings a feather along
        "::give coins 100",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb5619_veos_progress",
            constants = { not_started = 0, houses = 1, returnv = 2, altar = 4, finish = 5, complete = 7 },
            row = "quest_clientofkourend",
            display = "Client of Kourend",
            points = 1,
        })
        t.ticks(3)
        t.ticklog.start()
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- [273..286] Port Sarim, never in Kourend: "Tell me more", then the crossing.
        t.ticklog.mark("leg.sarim")
        t.exec("goto-veosSarim", t.player.goto_tile, 3054, 3246, 0)
        t.exec("sarim", t.player.talk_to, "veos_sarim", 1)
        t.exec("sarim.chat", t.chat.play, {
            "player:Hello Veos.", "npc:Hello there. What can I do for you?", "options",
            "choose:Tell me more about Great Kourend.", "player:Tell me more about Great Kourend.",
            "npc:Great Kourend is a magnificent kingdom", "npc:At one time, the kingdom",
            "npc:However, since the last King died", "options",
            "choose:That's great, can you take me there please?", "player:That's great, can you take me there please?",
            "npc:Certainly! I hope you don't get seasick, it's a long voyage.",
        })
        -- [289] the deck, [292] Veos speaks (the click + 6); the offer sits in
        -- "tell me more" [299].
        t.ticks(7)
        t.ticklog.mark("leg.arrival")
        t.exec("arrival.chat", t.chat.play, {
            "npc:Well here we are friend. Welcome to the Kingdom of Great Kourend.",
            "npc:Now Kourend is a big place", "options",
            "choose:Could you tell me more about Kourend?", "player:Could you tell me more about Kourend?",
            "npc:Certainly. The Kingdom of Great Kourend", "npc:At one time", "npc:However, since our last King",
            "npc:If you're looking for a good way", "npc:I have a client from the mainland",
            "npc:I was just going to do it myself", "npc:So what do you say?", "options",
            "choose:Yes.", "player:Sounds interesting! How can I help?",
            "objbox:Veos hands you an enchanted scroll.", "npc:Take this scroll.",
            "npc:My client is interested in the hierarchy", "npc:That scroll is special.",
            "player:Wait, wait! I'm not spying for somebody am I?", "npc:Not exactly...",
            "player:That seems fair.", "npc:There's many a man and woman", "npc:They should know enough",
            "player:Good call.", "npc:Simply touching a normal feather", "player:Right, I'll return to you",
            "npc:See you soon.", "end",
        })
        do
            local _, dock = t.world.tile()
            t.check("arrival.deck", dock ~= nil and dock.x == 1824 and dock.z == 3691,
                "landed at " .. tostring(dock and (dock.x .. "," .. dock.z)) .. ", recorded 1824,3691")
        end
        t.expect("quest.stage.houses", t.quest.expect_stage("houses"))
        t.expect("arrival.visited", t.var.expect("varb4897_zeah_playerhasvisited", 1))
        t.expect("arrival.sarim_travel", t.var.expect("varb12148_veos_sarim_vis", 2))
        t.check("start.scroll", count(t, "veos_scroll") == 1, "veos_scroll " .. count(t, "veos_scroll"))
        journal(t, "journal.scroll", J_SCROLL)

        -- [325] the quill.
        t.exec("quill", t.player.use_item_on_item, "feather", "veos_scroll")
        t.exec("quill.chat", t.chat.play, {
            "objbox:You attune the feather to the enchanted scroll, creating an enchanted quill.", "end",
        })
        t.check("quill.made", count(t, "veos_quill") == 1, "veos_quill " .. count(t, "veos_quill"))
        journal(t, "journal.quill", J_QUILL)

        -- [330..341] "I've lost something" with the scroll in the pack.
        t.ticklog.mark("leg.veos_lost_scroll")
        t.exec("goto-veos", t.player.goto_tile, 1824, 3690, 0)
        t.exec("veosLost", t.player.talk_to, "veos", 1)
        t.exec("veosLost.chat", t.chat.play, {
            "npc:Hello again,", "options", "choose:Let's talk about your client...",
            "player:Let's talk about your client...", "options", "choose:I've lost something you've given me.",
            "player:I've lost something you've given me.", "npc:You already have your scroll,", "end",
        })

        t.exec("goto-leenz", t.player.goto_tile, 1807, 3725, 0)
        interview(t, "leenz", "piscarilius_generalstore_keeper", {
            "npc:A great adventurer, from afar?", "options", "choose:Can I ask you about Port Piscarilius?",
            "player:Can I ask you about Port Piscarilius?", "npc:If it's Piscarilius information you seek,", "options",
            "choose:What is there to do in Port Piscarilius?", "player:What is there to do in Port Piscarilius?",
            "npc:Our shore is brimming,",
            "objbox:You jot down all of the information Leenz had to say about Port Piscarilius... With slightly less poetry.",
            "player:Hang on a second...", "npc:Listen here pal,", "player:Happy rhyming Leenz, please!",
            "npc:Anytime, my adventuring friend,", "end",
        })
        t.expect("leenz.recorded", t.var.expect("varb5620_veos_piscarilius", 1))

        -- Arceuus general store: double door on the west edge of 1718,3727.
        t.exec("goto-regath", t.player.goto_tile, 1717, 3727, 0)
        t.exec("regath.doorIn", t.player.pass_door, { closed = "archeuus_door_double_left_blue", open = "archeuus_door_double_left_blue_open",
            at = { 1718, 3727, 0 }, near = { 1717, 3727 }, far = { 1718, 3727 } })
        interview(t, "regath", "arceuus_generalstore", {
            "npc:What do you require, traveller?", "options", "choose:Can I ask you about Arceuus?",
            "player:Can I ask you about Arceuus?", "npc:This city holds knowledge", "player:Well, I'm a...",
            "player:Uhm...", "player:A journalist!", "npc:Someone willing to share knowledge",
            "npc:Very well, then. The rest of the world", "options", "choose:What is there to do in Arceuus?",
            "player:What is there to do in Arceuus?", "npc:Our library is something to behold, but the millennia of knowledge found there - runic,",
            "npc:Through its power, we have great knowledge of life and death.",
            "objbox:Your quill and scroll glow, causing a seething pain to run through your hand and arm.",
            "npc:What is the matter, journalist?", "player:I... I don't know.", "player:I must have caught my finger",
            "npc:Very well. As mentioned,", "objbox:You jot down all of the information Regath had to say about Arceuus.",
            "player:Thanks for your help!", "npc:You're welcome.", "end",
        })
        t.expect("regath.recorded", t.var.expect("varb5621_veos_arceuus", 1))
        t.exec("regath.doorOut", t.player.pass_door, { closed = "archeuus_door_double_left_blue", open = "archeuus_door_double_left_blue_open",
            at = { 1718, 3727, 0 }, near = { 1718, 3727 }, far = { 1717, 3727 } })

        -- Lovakengj general store: door on the north edge of 1551,3754.
        t.exec("goto-munty", t.player.goto_tile, 1551, 3755, 0)
        t.exec("munty.doorIn", t.player.pass_door, { closed = "lova_wall_door_lower", open = "lova_wall_door_open_lower",
            at = { 1551, 3754, 0 }, near = { 1551, 3755 }, far = { 1551, 3754 } })
        interview(t, "munty", "lovakengj_generalstore", {
            "npc:Can I help you, human?", "options", "choose:Can I ask you about Lovakengj?",
            "player:Can I ask you about Lovakengj?", "npc:Why I'd be happy to have a chat!", "options",
            "choose:What is there to do in Lovakengj?", "player:What is there to do in Lovakengj?",
            "npc:Well creating dynamite", "npc:We also make the armour", "npc:The rewards able to be reaped",
            "objbox:You jot down all of the information Munty had to say about Lovakengj.",
            "player:Thanks for your help!", "npc:Anytime! Come back soon!", "end",
        })
        t.expect("munty.recorded", t.var.expect("varb5622_veos_lovakengj", 1))
        t.exec("munty.doorOut", t.player.pass_door, { closed = "lova_wall_door_lower", open = "lova_wall_door_open_lower",
            at = { 1551, 3754, 0 }, near = { 1551, 3754 }, far = { 1551, 3755 } })

        -- Shayzien general store: door on the north edge of 1519,3587.
        t.exec("goto-jennifer", t.player.goto_tile, 1519, 3587, 0)
        t.exec("jennifer.doorIn", t.player.pass_door, { closed = "wallkit_shayzien_door01_l", open = "wallkit_shayzien_door01_l_open",
            at = { 1519, 3587, 0 }, near = { 1519, 3587 }, far = { 1519, 3588 } })
        interview(t, "jennifer", "shayzien_generalstore", {
            "npc:What're yer looking for, me luv'?", "options", "choose:Can I ask you about Shayzien?",
            "player:Can I ask you about Shayzien?", "npc:Well I could give you some info", "options",
            "choose:What is there to do in Shayzien?", "player:What is there to do in Shayzien?",
            "npc:It ain't all pretty 'ere,",
            "objbox:You jot down all of the information Jennifer had to say about Shayzien.",
            "player:Thanks for your help!", "npc:That's alright petal,", "end",
        })
        t.expect("jennifer.recorded", t.var.expect("varb5623_veos_shayzien", 1))
        t.expect("quest.stage.houses_still", t.quest.expect_stage("houses"))
        t.exec("jennifer.doorOut", t.player.pass_door, { closed = "wallkit_shayzien_door01_l", open = "wallkit_shayzien_door01_l_open",
            at = { 1519, 3587, 0 }, near = { 1519, 3588 }, far = { 1519, 3587 } })

        -- Hosidius general store: the west door on the south edge of 1770,3590.
        t.exec("goto-horace", t.player.goto_tile, 1770, 3590, 0)
        t.exec("horace.doorIn", t.player.pass_door, { closed = "kore2_hos_door_closed", open = "kore2_hos_door_open",
            at = { 1770, 3590, 0 }, near = { 1770, 3590 }, far = { 1770, 3589 } })
        interview(t, "horace", "hosidius_generalstore", {
            "npc:Hello, I'm Horace.", "options", "choose:Can I ask you about Hosidius?",
            "player:Can I ask you about Hosidius?", "npc:Well, well my friend!", "options",
            "choose:What is there to do in Hosidius?", "player:What is there to do in Hosidius?",
            "npc:Our personal kitchens and farming patches are lovely.",
            "mesbox:As you jot down the final information on Hosidius, you hear a mysterious whisper in your ear.",
            "player:Thanks for your help, Horace!", "npc:Anytime, friend!", "end",
        })
        t.expect("horace.recorded", t.var.expect("varb5624_veos_hosidius", 1))
        t.expect("quest.stage.returnv", t.quest.expect_stage("returnv"))
        t.exec("horace.doorOut", t.player.pass_door, { closed = "kore2_hos_door_closed", open = "kore2_hos_door_open",
            at = { 1770, 3590, 0 }, near = { 1770, 3589 }, far = { 1770, 3590 } })

        -- [1051..1068] the report and the orb.
        t.ticklog.mark("leg.report")
        t.exec("goto-report", t.player.goto_tile, 1824, 3690, 0)
        t.exec("report", t.player.talk_to, "veos", 1)
        t.exec("report.chat", t.chat.play, {
            "npc:Hello again,", "options", "choose:Let's talk about your client...",
            "player:Let's talk about your client...", "player:I have gathered information from all of the cities.",
            "player:I could hear a voice, Veos...", "npc:Yes, I heard it too... They mentioned",
            "npc:Let me take those items off your hands.", "objbox:Veos removes the items he gave you from your backpack.",
            "player:That reminds me, Veos!", "npc:If it makes you feel any better", "npc:Namely...",
            "npc:The Dark Altar...", "player:If I knew mentioning the Dark Altar", "npc:Well that's just the thing,",
            "player:Well, I don't necessarily believe", "player:What should I do now?",
            "npc:I'm certainly glad you're able to assist once again.", "npc:My client will be most pleased.",
            "npc:My client requires... another simple task.", "player:Can I ask why I would do that?",
            "npc:It's best you don't.", "player:Well, can't argue with that!", "end",
        })
        t.expect("quest.stage.altar", t.quest.expect_stage("altar"))
        t.check("report.orb", count(t, "veos_orb") == 1, "veos_orb " .. count(t, "veos_orb"))
        t.check("report.tools_taken", count(t, "veos_scroll") + count(t, "veos_quill") == 0,
            "scroll " .. count(t, "veos_scroll") .. ", quill " .. count(t, "veos_quill"))
        journal(t, "journal.orb", J_ORB)

        -- [1071..1084] the orb destroyed and handed back.
        t.ticklog.mark("leg.orb_lost")
        t.player.inv_op("veos_orb", 5)
        t.ticks(1)
        t.exec("orbDestroy.yes", t.ui.press_pause, "confirmdestroy:universe", 1, "confirmdestroy:universe")
        t.ticks(2)
        t.check("orb.destroyed", count(t, "veos_orb") == 0, "veos_orb " .. count(t, "veos_orb"))
        t.exec("orbAgain", t.player.talk_to, "veos", 1)
        t.exec("orbAgain.chat", t.chat.play, {
            "npc:Hello again,", "options", "choose:Let's talk about your client...",
            "player:Let's talk about your client...", "options", "choose:I've lost something you've given me.",
            "player:I've lost something you've given me.", "objbox:Veos hands you another mysterious orb.",
            "npc:Please be more careful,", "player:Thanks, Veos!", "end",
        })
        t.check("orb.again", count(t, "veos_orb") == 1, "veos_orb " .. count(t, "veos_orb"))

        -- [1451..1459] the Dark Altar.
        t.ticklog.mark("leg.altar")
        t.exec("goto-altar", t.player.goto_tile, 1713, 3883, 0)
        t.exec("altar", t.player.inv_op, "veos_orb", 1)
        t.exec("altar.chat", t.chat.play, {
            "objbox:The orb shatters after being held in the air.",
            "mesbox:You hear a whisper in your ear.", "player:I really wish I would be warned", "end",
        })
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
        t.check("altar.glass", count(t, "broken_glass") == 1, "broken_glass " .. count(t, "broken_glass"))

        -- [1581..1600] the possession and the completion.
        t.ticklog.mark("leg.finish")
        t.exec("goto-finish", t.player.goto_tile, 1825, 3690, 0)
        t.exec("finish", t.player.talk_to, "veos", 1)
        t.exec("finish.chat", t.chat.play, {
            "npc:Hello again,", "options", "choose:Let's talk about your client...",
            "player:Let's talk about your client...", "player:Veos, enough is enough!",
            "npc:What are you talking about?", "player:You know darn well", "npc:I haven't spoken with you",
            "npc:ARGH!", "player:Veos, this isn't funny anymore...", "npc:Indeed you have.", "player:Uhm... Veos?",
            "npc:You have done well.", "player:Who are you?", "npc:You need not fear for him.",
            "npc:As for who I am...", "player:I have a right to know!", "npc:Knowledge can be a curse.",
        })
        t.ticks(3)
        t.quest.expect_complete()
        t.check("reward.veos_lamp", count(t, "veos_lamp") == 2, "veos_lamp " .. count(t, "veos_lamp"))
        t.check("reward.memoirs", count(t, "veos_kharedsts_memoirs") == 1, "memoirs " .. count(t, "veos_kharedsts_memoirs"))

        -- [1608..1625] both lamps on Agility (xpreward child 7): 7 -> 8 -> 9.
        t.ticklog.mark("leg.lamps")
        for i = 1, 2 do
            t.exec("lamp" .. i, t.player.inv_op, "veos_lamp", 1)
            t.ticks(3)
            t.exec("lamp" .. i .. ".agility", t.ui.press_pause, "xpreward:universe", 7, "xpreward:confirm")
            t.ticks(2)
            t.expect("lamp" .. i .. ".granted", t.msg.expect("500 Agility experience"))
            t.exec("lamp" .. i .. ".box", t.chat.play, { "mesbox:Your wish has been granted!" })
        end
        t.expect("lamp.counted", t.var.expect("varb5619_veos_progress", 9))
        t.ticklog.mark("end")
        t.finish(0)
    end,
}
