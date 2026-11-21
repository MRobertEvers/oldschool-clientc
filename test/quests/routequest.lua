-- In Search of the Myreque, the recorded flow (RSProx index 4632 + 4633, rev 242.1;
-- docs/rsprox-analysis/quests/in_search_of_the_myreque.md), as a 4-leg relay:
--   1. Vanstrom (the No, then the Yes), Cyreg's persuasion, the planks, the boat, the tree, the three rungs;
--   2. Curpile (a round lost to the knock-out, Cyreg, the boat back, a round won), the doors, the hideout
--      (Harold's greeting), Veliaf;
--   3. the five members, Veliaf's weapons, the ambush (in a copy), the hound let loose, the escape;
--   4. back into the hound's copy, the kill, Veliaf, the false wall, the ladder, the Stranger and the scroll.
local function where(t)
    local r, tl = t.world.tile()
    if type(tl) ~= "table" then return tostring(r) end
    return tl.x .. "," .. tl.z .. "," .. tl.level
end

-- Curpile's answers, by question (4632 [1895], [1909], [1925], [2075], [2082], [2090]; the
-- female member and the leader are the wiki's).
local ANSWERS = {
    { q = "boatman", a = "Cyreg Paddlehorn." },
    { q = "scholar", a = "Polmafi Ferdygris." },
    { q = "vampyre family", a = "Drakan." },
    { q = "youngest", a = "Ivan Strom." },
    { q = "female", a = "Sani Piliu." },
    { q = "leader", a = "Veliaf Hurtz." },
}

-- One round of Curpile's three questions, read off each menu's title (the question); `wrong_at`
-- answers that question "I don't know."
local function quiz(t, name, wrong_at)
    local verdicts = { "npc:Okay, interesting answer. That's tha first question done.",
        "npc:Interesting... That's tha second question answered.",
        "npc:Hmm.... Very interesting. That's tha third question" }
    local openers = { nil, "npc:Right then, here's ya second question.", "npc:Okay, time for tha third and final question." }
    for n = 1, 3 do
        if openers[n] then t.exec(name .. "-open" .. n, t.chat.play, { openers[n] }) end
        local title = nil
        for _ = 1, 6 do
            local r, tt = t.chat.options_title()
            if r == "ok" then title = tostring(tt) break end
            t.ticks(1)
        end
        local pick = nil
        for _, e in ipairs(ANSWERS) do if title and title:find(e.q, 1, true) then pick = e.a end end
        t.check(name .. "-q" .. n, pick ~= nil, tostring(title))
        if n == wrong_at or pick == nil then
            t.exec(name .. "-a" .. n, t.chat.play, { "choose:I don't know.", "player:I don't know." })
        else
            t.exec(name .. "-a" .. n, t.chat.play, { "choose:" .. pick, "player:" .. pick })
        end
        t.exec(name .. "-v" .. n, t.chat.play, { verdicts[n] })
    end
end

-- The arrival box after a boat (4632 [1783], [2021]); a ghast's hit in the Hollows can close it
-- before it is read, so its absence is noted, not failed.
local function arrival(t, name, text)
    local k = t.chat.kind()
    if k == "mesbox" then
        t.exec(name, t.chat.play, { "mesbox:" .. text })
        t.ticks(1)
    else
        t.check(name, true, "no box up at the read (chat kind " .. tostring(k) .. "): a ghast's hit closes it")
    end
end

return {
    id = "routequest",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::complete quest_priestinperil", -- the way into Morytania on foot: the Paterdomus trapdoor and holy barrier
        "::complete quest_restlessghost", -- Priest in Peril's own requirement
        "::complete quest_naturespirit", -- the quest's requirement (druidspirit_complete)
        "::give dagger_wolfbane 1", -- Priest in Peril's reward; Drezel's advice branch needs it held
        "::setlevel agility 25", -- the quest's 25 Agility
        "::setlevel attack 70", "::setlevel strength 70", "::setlevel defence 70", "::setlevel hitpoints 80", -- the hound
        "::setlevel prayer 43", -- Protect from Melee for the hound (4633 [23]: the recorder prays it)
        "::give steel_longsword 1", -- the weapons Vanstrom names (4632 [684])
        "::give steel_sword 2",
        "::give steel_mace 1",
        "::give steel_warhammer 1",
        "::give steel_dagger 1",
        "::give nails 225", -- Cyreg's list (4632 [1402]): a hammer, 225 steel nails, six planks, ten gold
        "::give woodplank 6",
        "::give hammer 1",
        "::give druid_pouch 5", -- something against the ghasts
        "::give coins 100", -- the fee, twice (the knock-out sends the player back to Mort'ton)
    },
    bind = {
        varp = "varp387_routequest",
        row = "quest_routequest",
        constants = { not_started = 0, started = 5, spoke_to_boatman = 10, boatman_agreed = 15, boatman_repaired = 20,
            entered_hollowed = 25, found_guard = 52, answered_questions = 55, entered_underground = 60,
            introduced_veliaf = 65, gave_weapons = 67, enter_cutscene = 70, ambush = 80, saved_myreque = 85,
            told_exit_route = 90, discovered_wall = 95, found_exit = 97, spoke_to_stranger = 100, complete = 110 },
        display = "In Search of the Myreque",
        points = 2,
    },

    legs = {
        { name = "vanstrom_to_bridge", run = function(t)
            do local _, d = t.ticklog.start(); t.note("ticklog: " .. tostring(d)) end
            t.ticklog.mark("leg.vanstrom_to_bridge")
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            -- Into Morytania on foot, the way Priest in Peril opened (door rule): the Varrock members' gate, the
            -- Paterdomus trapdoor, Drezel's two gates, his advice, the holy barrier out east of the Salve (copied
            -- from makinghistory / druidspirit, same reach.py readings from the fixture's tile).
            t.exec("goto-enterMorytania.varrockGate", t.player.goto_tile, 3318, 3468, 0)
            t.exec("enterMorytania.varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
                open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
            t.exec("goto-enterMorytania.trapdoor", t.player.goto_tile, 3405, 3506, 0)
            t.exec("enterMorytania.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
            t.await({
                level = function()
                    return t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } }) == "ok"
                end,
                note = "enterMorytania: the trapdoor opens",
            }, 6)
            local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } })
            local tdc_r = t.world.loc_near("trapdoor", 3, { at = { 3405, 3507, 0 } })
            t.check("enterMorytania.trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
                "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. " "
                    .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z .. "," .. tdo.level) or tostring(tdo))
                    .. "; closed trapdoor there -> " .. tostring(tdc_r) .. " (want the open leaf and no closed one)")
            t.exec("enterMorytania.descend", t.player.climb, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
                at = { 3405, 3507, 0 }, src = { 3405, 3506 }, dest = { 3405, 9906, 0 }, slack = 1 })
            t.exec("enterMorytania.gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
                near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
                far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
            t.exec("enterMorytania.gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
                near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
                far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
            t.exec("enterMorytania.talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
            -- Priest in Peril's Drezel (that quest's own port): his advice pages, whatever they say now.
            -- His advice ends on a menu of further questions: stop there and close it.
            t.exec("enterMorytania.talkToDrezel-dialog", t.chat.drain, { max_pages = 30, stop_at = "options" })
            t.chat.close()
            t.exec("enterMorytania.drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
            t.exec("enterMorytania.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
                at = { 3440, 9886, 0 }, near = { 3440, 9887 },
                far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
                far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })
            t.exec("enterMorytania.holyBarrier-msg", t.msg.expect, "You pass through the holy barrier")


            -- Vanstrom (4632 [625..777]): the start offered, refused, offered again and taken.
            t.exec("goto-talkToVanstrom", t.player.goto_tile, 3503, 3478, 0)
            local intro = {
                "npc:Hello there. The name's Vanstrom Klause. How goes it", "player:Quite well, thanks for asking.",
                "npc:Hmm... Well, I am a little concerned", "player:What friends are these?",
                "npc:Well, friends may be the wrong term", "player:What work?",
                "npc:Fighting against the vampyres", "player:The Myreque? Yes, I've heard of them.",
                "npc:Really? Well, then you'll understand", "player:And how is it you want to help them?",
                "npc:As I say, I'm not actually too sure.",
            }
            t.exec("talkToVanstrom.no", t.player.talk_to, "multi_vanstrom_stranger_entity", 1)
            local no = {}
            for _, e in ipairs(intro) do no[#no + 1] = e end
            no[#no + 1] = "choose:No."
            no[#no + 1] = "player:I wish I could help, but I'm busy at the moment."
            no[#no + 1] = "npc:Oh well, I'm sure someone will be along to help soon."
            t.exec("talkToVanstrom.no-dialog", t.chat.play, no)
            t.chat.continue_(true); t.ticks(2)
            t.expect("quest.stage.not_started.after_no", t.quest.expect_stage("not_started"))
            t.exec("talkToVanstrom", t.player.talk_to, "multi_vanstrom_stranger_entity", 1)
            local yes = {}
            for _, e in ipairs(intro) do yes[#yes + 1] = e end
            for _, e in ipairs({ "choose:Yes.",
                "player:Well, perhaps I could deliver some weapons", "npc:Really? You would help? Fantastic!",
                "npc:From what I hear, the weapons needed to slay", "player:So what weapons did you want",
                "npc:Well, vampyres are not the only foes", "player:But you've not actually been able",
                "npc:Unfortunately not.", "player:I'm sure I'll be able to get my hands on some.",
                "npc:They'll need delivering to our Myreque friends.", "player:Sounds like a good place to start then.",
                "npc:How wonderful! Good luck, adventurer.",
                "choose:Where can I find this boatman?", "player:Where can I find this boatman?",
                "npc:I hear he lives in Mort'ton.", "npc:Most of the town has supposedly fallen",
                "player:And you say he's had dealings", "npc:So I hear.",
                "choose:Remind me, what weapons do I need?", "player:Remind me, what weapons do I need?",
                "npc:You've forgotten already?", "npc:I'd suggest a longsword",
                "choose:What else do you know about the Myreque?", "player:What else do you know about the Myreque?",
                "npc:The vampyres are an evil heel", "npc:The Myreque are the only ones brave enough",
                "npc:No one knows for sure", "player:And this group in Mort Myre?", "npc:Yes, indeed!",
                "choose:I'd better be off.", "player:I'd better be off." }) do yes[#yes + 1] = e end
            t.exec("talkToVanstrom-dialog", t.chat.play, yes)
            t.chat.continue_(true); t.ticks(2)
            t.expect("quest.stage.started", t.quest.expect_stage("started"))

            -- Cyreg (4632 [1299..1415], [1632..1669]).
            t.exec("goto-talkToCyreg", t.player.goto_tile, 3522, 3285, 0)
            t.exec("talkToCyreg", t.player.talk_to, "route_cyreg_paddlehorn", 1)
            t.exec("talkToCyreg-dialog", t.chat.play, {
                "npc:Hello there, friend.", "player:Hello there. I'm guessing you're the boatman?",
                "npc:Well I am 'a' boatman.", "player:I hear you have dealings with the Myreque.",
                "npc:Hmm... I don't think I know", "player:Come on, I know you've helped them",
                "npc:Well... let's say I had worked for them before", "player:Do I look like a vampyre?",
                "npc:Maybe! You know they can shapeshift", "player:Surely if I was a vampyre",
                "npc:Hmm... Perhaps... But I'm sorry",
                "choose:Oh come on, you can tell me!", "player:Oh come on, you can tell me!", "npc:I'm sorry, I just can't...",
                "choose:Well, I guess they'll just die without weapons.", "player:Well, I guess they'll just die",
                "npc:Well, you don't seem too concerned",
                "choose:Resourceful enough to get their own steel weapons?", "player:Resourceful enough",
                "npc:Maybe they are...",
                "choose:If you don't tell me, their deaths are on your head!", "player:If you don't tell me",
                "npc:There's death aplenty in this forsaken place...",
                "choose:Why do you say that this place is forsaken?", "player:Why do you say that this place is forsaken?",
                "npc:All of these lands are forsaken",
                "choose:What kind of a man are you to say that you don't care?", "player:What kind of a man are you",
                "npc:Don't dare to judge me", "player:You're risking death either way.", "npc:Hmm...",
                "npc:I suppose you do make a point.", "player:So you'll help me?",
                "npc:No, I won't take you, but if you're happy to pay", "player:What for?",
                "npc:The boat took some damage", "npc:You should also bring a hammer", "npc:Oh, and don't forget the weapons",
                "choose:Here are some planks for you.", "player:Here are some planks for you.",
                "objbox:The boatman takes three wooden planks", "npc:Right. When you're ready",
                "npc:Keep heading north on foot", "npc:Don't forget, you'll want to take three more planks",
                "npc:Oh, and don't forget the weapons",
                "choose:Tell me about yourself.", "player:Tell me about yourself.", "npc:I'm just a humble boatman.",
                "choose:Tell me more about the Myreque.", "player:Tell me more about the Myreque.", "npc:What do you want to know?",
                "choose:Tell me about the members of the Myreque.", "player:Tell me about the members of the Myreque.",
                "npc:Well you have Radigad Ponfit.", "npc:Then there's Veliaf Hurtz", "npc:Next there's Ivan Strom.",
                "npc:You've also got Sani Piliu", "npc:There's Harold Evans.", "npc:Finally, you have Polmafi Ferdygris.",
                "player:And that's all of them?", "npc:In Mort Myre it is.",
                "choose:Let's talk about something else.", "player:Let's talk about something else.",
                "choose:Tell me about the Drakans.", "player:Tell me more about the vampyres.",
                "npc:They're led by the Drakans.", "npc:No one out here ever sees the Drakans",
                "choose:Could I use your boat?", "player:Could I use your boat?", "npc:You can, but I'll have to charge you",
                "choose:Yes, I'll pay the ten gold.", "player:Yes, I'll pay the ten gold.",
            })
            t.chat.continue_(true)
            t.ticks(20)
            arrival(t, "boat-arrive", "You arrive at the Hollows.")
            t.expect("quest.stage.entered_hollowed", t.quest.expect_stage("entered_hollowed"))
            local _, landed = t.world.tile()
            t.check("boat-landed", type(landed) == "table" and landed.x == 3498 and landed.z == 3380,
                "landed at " .. where(t) .. " (4632 [2020]: 3498,3380)")

            -- The tree and the three rungs (4632 [1805..1831]).
            t.exec("goto-climbTree", t.player.goto_tile, 3502, 3425, 0)
            t.exec("climbTree", t.player.click_loc, "spooky_tree_base_forbridge", 2, { at = { 3502, 3426, 0 } })
            t.ticks(4)
            local _, up = t.world.tile()
            t.check("climbTree-where", type(up) == "table" and up.x == 3502 and up.z == 3427 and up.level == 0,
                "on the bridge at " .. where(t) .. " (4632 [1812]: 3502,3427,0)")
            for n = 1, 3 do
                t.exec("repairBridge" .. n, t.player.click_loc, "route_swampbridge_" .. n, 1)
                t.ticks(5)
                local _, np = t.inv.count("woodplank")
                local _, nn = t.inv.count("nails")
                t.check("repairBridge" .. n .. "-after", tonumber(nn) == 225 - 75 * n,
                    where(t) .. " planks " .. tostring(np) .. " nails " .. tostring(nn))
            end
            t.exec("climbDown", t.player.click_loc, "spooky_tree_base_forbridge", 1, { at = { 3502, 3431, 0 } })
            t.ticks(3)
            local _, down = t.world.tile()
            t.check("climbDown-where", type(down) == "table" and down.x == 3502 and down.z == 3432,
                "off the bridge at " .. where(t) .. " (4632 [1831]: 3502,3432)")
            t.exec("leg.1.goto-camp", t.player.goto_tile, 3507, 3438, 0)
            t.ticks(4)
            t.check("leg.1.state", true, "ends at " .. where(t) .. ", stage entered_hollowed (25)")
        end },
        { name = "curpile_to_veliaf", run = function(t)
            t.ticklog.mark("leg.curpile_to_veliaf")
            t.ticks(2)
            -- The doors before Curpile's questions (a guessed line: no recording tries them).
            t.exec("doorsLocked", t.player.click_loc, "freedomfighterentrancer", 1, { at = { 3509, 3447, 0 } })
            t.exec("doorsLocked-box", t.chat.play, { "mesbox:The doors are locked." })
            t.exec("goto-curpile1", t.player.goto_tile, 3507, 3438, 0)
            -- Curpile (4632 [1836..1935]): the punch, the questions, a round lost.
            t.exec("talkToCurpile.punch", t.player.talk_to, "route_curpile_fyod_child", 1)
            t.exec("talkToCurpile.punch-dialog", t.chat.play, {
                "npc:Hey... What're you doin' here?", "choose:I don't have to answer to you!",
                "player:I don't have to answer to you!", "npc:You do if you know what's good for ya!" })
            t.chat.continue_(true); t.ticks(2)
            t.exec("talkToCurpile.punch-msg", t.msg.expect, "Curpile punches you.")
            t.exec("talkToCurpile", t.player.talk_to, "route_curpile_fyod_child", 1)
            t.exec("talkToCurpile-dialog", t.chat.play, {
                "npc:Hey... What're you doin' here?", "choose:No reason!", "player:No reason!",
                "npc:Well, you's gotta know that you're tresspassin'",
                "choose:I'm lost. Can you show me the way out?", "player:I'm lost. Can you show me the way out?",
                "npc:You's got yourself here...",
                "choose:I've come to help the Myreque. I've brought weapons.", "player:I've come to help the Myreque.",
                "npc:Okay, I see ya got some weapons", "npc:Now, you seems all sincered up to me...",
                "player:But I just want to deliver these weapons.", "npc:Okay, I's got an idea.",
                "player:Sounds fine to me... Go ahead, shoot!", "npc:Hey... Don't tempt me!", "npc:Okay, here's ya first question." })
            t.expect("quest.stage.found_guard", t.quest.expect_stage("found_guard"))
            quiz(t, "round1", 3)
            t.exec("round1-lost", t.chat.play, { "npc:I'm sorry, but you's only got two questions correct!" })
            t.ticks(14)
            -- The knock-out (4632 [1935..1950]): back beside Cyreg.
            t.exec("knockout", t.chat.play, { "mesbox:Curpile knocks you out and takes you back to Mort'ton.",
                "player:Ow... my head...", "npc:So your hunt for the Myreque is going well then?",
                "choose:Could I use your boat?", "player:Could I use your boat?", "npc:You can, but I'll have to charge you",
                "choose:Yes, I'll pay the ten gold.", "player:Yes, I'll pay the ten gold." })
            t.chat.continue_(true)
            t.ticks(20)
            arrival(t, "boat2-arrive", "You arrive at the Hollows.")
            -- Back over the mended bridge (4632 [2058..2067]): up the south tree, along the rungs, down the north tree.
            t.exec("goto-crossBridge", t.player.goto_tile, 3502, 3425, 0)
            t.exec("crossBridge.up", t.player.click_loc, "spooky_tree_base_forbridge", 1, { at = { 3502, 3426, 0 } })
            t.ticks(4)
            t.exec("crossBridge.down", t.player.click_loc, "spooky_tree_base_forbridge", 1, { at = { 3502, 3431, 0 } })
            t.ticks(8)
            local _, over = t.world.tile()
            t.check("crossBridge-where", type(over) == "table" and over.x == 3502 and over.z == 3432,
                "over the bridge at " .. where(t) .. " (4632 [2067]: 3502,3432)")
            t.exec("goto-curpile2", t.player.goto_tile, 3508, 3439, 0)
            t.exec("talkToCurpile2", t.player.talk_to, "route_curpile_fyod_child", 1)
            t.exec("talkToCurpile2-dialog", t.chat.play, { "npc:Back again? You's ain't getting in",
                "player:Well, let's hear them then.", "npc:Okay, here's ya first question." })
            quiz(t, "round2", 0)
            t.exec("round2-won", t.chat.play, { "npc:Okay, I believes ya..." })
            t.ticks(2)
            t.expect("quest.stage.answered_questions", t.quest.expect_stage("answered_questions"))

            -- The doors (4632 [2111..2123]) and the first cave on the right (4632 [2184..2203]).
            t.exec("enterDoors", t.player.click_loc, "freedomfighterentrancer", 1, { at = { 3509, 3447, 0 } })
            t.ticks(8)
            t.expect("quest.stage.entered_underground", t.quest.expect_stage("entered_underground"))
            local _, under = t.world.tile()
            t.check("enterDoors-where", type(under) == "table" and under.x == 3500 and under.z == 9811,
                "below at " .. where(t) .. " (4632 [2122]: 3500,9811)")
            t.player.walk_to(3492, 9824, 60)
            t.exec("enterCave", t.player.click_loc, "route_cavewalltunnel", 1, { at = { 3492, 9823, 0 } })
            t.ticks(7)
            t.exec("enterCave-harold", t.chat.play, { "npc:Hey! Who are you and why are you here?",
                "player:It's okay. I'm a friend.", "npc:Weapons? You'd better talk to Veliaf then." })
            t.ticks(1)
            local _, inside = t.world.tile()
            t.check("enterCave-where", type(inside) == "table" and inside.x == 3505 and inside.z == 9832,
                "in the hideout at " .. where(t) .. " (4632 [2190]: 3505,9832)")

            -- Veliaf (4632 [2252..2288]).
            t.exec("talkToVeliaf", t.player.talk_to, "route_veliaf_hurtz_parent", 1)
            t.exec("talkToVeliaf-dialog", t.chat.play, { "player:Hello there.", "npc:You... I saw you come in.",
                "player:I'm ", "npc:I see... And why should we trust you?", "player:Because I already gained the trust",
                "npc:A good start I suppose", "player:True, but someone I wouldn't have been able to fool is Drezel.",
                "npc:Drezel? You know him?", "player:Yes, and he told me to seek you out.",
                "npc:Hmm... Very well.", "npc:In the meantime, given you're here to help" })
            t.chat.continue_(true); t.ticks(2)
            t.expect("quest.stage.introduced_veliaf", t.quest.expect_stage("introduced_veliaf"))
        end },
        { name = "myreque_to_cutscene", run = function(t)
            t.ticklog.mark("leg.myreque_to_cutscene")
            t.ticks(2)
            -- The five members (4632 [2302..2422]), each met once, each topic asked.
            local function member(step, sym, opening, job, myreque)
                t.exec(step, t.player.talk_to, sym, 1)
                local list = {}
                for _, e in ipairs(opening) do list[#list + 1] = e end
                list[#list + 1] = "choose:What's your job here?"
                list[#list + 1] = "player:What's your job here?"
                for _, e in ipairs(job) do list[#list + 1] = e end
                list[#list + 1] = "choose:Could you tell me more about the Myreque?"
                list[#list + 1] = "player:Could you tell me more about the Myreque?"
                for _, e in ipairs(myreque) do list[#list + 1] = e end
                list[#list + 1] = "choose:Okay, thanks."
                list[#list + 1] = "player:Okay, thanks."
                t.exec(step .. "-dialog", t.chat.play, list)
                t.chat.continue_(true); t.ticks(2)
            end
            member("talkToSani", "route_sani_piliu", { "npc:Hello. Very nice to meet you!", "player:I'm ",
                "npc:The pleasure's all mine!", "player:Well, perhaps a little", "npc:Sure, go ahead." },
                { "npc:Well, it's fair to say", "player:What skills?", "npc:Before I joined the Myreque",
                  "player:That's horrible!", "npc:It's okay.", "npc:Anyway, what it meant was", "npc:He saw that I would be an asset" },
                { "npc:Well, Veliaf is the leader", "npc:Anyway, our hope is", "npc:That's the long term goal though.",
                  "player:You make it sound easy!", "npc:With determination" })
            member("talkToPolmafi", "route_polmafi_ferdygris_parent", { "npc:Ah, I see you've introduced yourself to Veliaf. Now",
                "player:Do you mind if I ask a few questions?", "npc:Not at all." },
                { "npc:I'm a key advisor to Veliaf.", "npc:We don't give up though." },
                { "npc:We're the last hope", "npc:There are multiple groups of us.", "npc:Veliaf leads this group." })
            member("talkToIvan", "route_ivan_strom_parent", { "player:Hi there. Veliaf said", "npc:Okay. Well, my name is Ivan.",
                "player:Would you mind if I asked you a few questions?", "npc:That's fine." },
                { "npc:To be honest, I shouldn't actually be here", "player:What were you meant to be learning", "npc:I'm meant to be training as a priest." },
                { "npc:We're the only ones brave enough" })
            member("talkToRadigad", "route_radigad_ponfit_parent", { "npc:Ah, I see you've introduced yourself to Veliaf. Are you",
                "player:Maybe, but I'd like to ask", "npc:Sure, go ahead." },
                { "npc:I help with anything I can really." },
                { "npc:We're the only resistance", "npc:Hopefully we can find more people", "player:So there aren't other members",
                  "npc:There are, but I hear", "npc:I'm not sure how true that is" })
            member("talkToHarold", "route_harold_evans", { "npc:Hello there. Forgive me", "player:It's fine.", "npc:Sure. I'll do my best" },
                { "npc:Cheating death seems to be my main role." },
                { "npc:We're a good team", "npc:Many of the others", "npc:Truth be told though", "player:Juves?", "npc:Juveniles," })
            local _, bits = t.var.server("varb2496_thsfm_spoken_allmyreque")
            t.check("talkToMembers", bits == 31, "thsfm_spoken_allmyreque=" .. tostring(bits) .. " (4632 [2405]: 31)")

            -- In Aid of the Myreque's garlic chest (4632 [2230..2239]): open it, search it twice.
            t.exec("garlicChest.open", t.player.click_loc, "burgh_chest_closed", 1, { at = { 3515, 9837, 0 } })
            t.ticks(4)
            for n = 1, 2 do
                t.exec("garlicChest.search" .. n, t.player.click_loc, "burgh_chest_open", 1, { at = { 3515, 9837, 0 } })
                t.ticks(4)
            end
            t.exec("garlicChest.msg", t.msg.expect, "...you find a bulb of garlic which you add to your inventory.")
            local _, garlic = t.inv.count("garlic")
            local _, gc = t.var.server("varb2009_myq2_garlic_chest")
            t.check("garlicChest", tonumber(garlic) == 2 and gc == 2,
                "garlic " .. tostring(garlic) .. ", myq2_garlic_chest " .. tostring(gc) .. " (4632 [2239]: 2 and 2)")

            -- Veliaf and the weapons (4632 [2436..2487]), then the ambush in a copy ([2487..2553]).
            t.exec("talkToVeliafAgain", t.player.talk_to, "route_veliaf_hurtz_parent", 1)
            t.exec("talkToVeliafAgain-dialog", t.chat.play, { "npc:Well met. I see you've introduced yourself to everyone.",
                "choose:So you're the leader of this group?", "player:So you're the leader of this group?", "npc:That's right.",
                "choose:Can you tell me about your organisation?", "player:Can you tell me about your organisation?",
                "npc:Well I wouldn't say we're an organisation.", "npc:Anyway though",
                "choose:What can you tell me about Morytania?", "player:What can you tell me about Morytania?",
                "npc:If legend is to be believed", "player:Who is Lowerniel Drakan?", "npc:Lowerniel Vergidiyad Drakan",
                "player:You sound very bitter.", "npc:So would you",
                "choose:What are your plans for the future?", "player:What are your plans for the future?",
                "npc:At the moment we're maintaining a low profile", "npc:Looking further ahead",
                "choose:Let's talk about the weapons.", "player:Let's talk about the weapons.",
                "npc:Alright, let's see what you have for us...", "objbox:You give the weapons to Veliaf.",
                "npc:We'll be able to make good use of these." })
            local mark = t.cutscene.mark()
            t.ticks(8)
            t.exec("ambush-dialog1", t.chat.play, { "npc:What the...", "npc:Vampyre!" })
            t.ticks(6)
            t.exec("ambush-dialog2", t.chat.play, { "npc:It's Vanstrom!", "player:Wait... What?",
                "npc:Well done, adventurer!", "npc:The Drakans have been wanting you lot dealt with", "npc:We're dead!" })
            t.ticks(8)
            t.exec("ambush-dialog3", t.chat.play, { "npc:Quite right.", "npc:No!" })
            t.ticks(8)
            t.exec("ambush-dialog4", t.chat.play, { "npc:And now... A little gift for the rest of you." })
            t.exec("ambush.cutscene", t.cutscene.await, "ambush", { since = mark,
                expect = { { op = "moveto", height = 555 }, { op = "lookat", height = 85 }, { op = "reset" } } })
            t.ticks(2)
            t.expect("quest.stage.ambush", t.quest.expect_stage("ambush"))
            local _, lsw = t.inv.count("steel_longsword")
            t.check("weapons-handed-over", tonumber(lsw) == 0, "steel_longsword " .. tostring(lsw) .. " left in the backpack")
            -- The escape (4632 [2566..2577]): out of the copy at the passage mouth.
            t.exec("escape", t.player.click_loc, "route_cavewalltunnel_escape", 1)
            t.exec("escape-confirm", t.chat.play, { "choose:Yes." })
            t.ticks(6)
            local _, out = t.world.tile()
            t.check("escape-where", type(out) == "table" and out.x == 3492 and out.z == 9824,
                "out at " .. where(t) .. " (4632 [2574]: 3492,9824)")
        end },
        { name = "hellhound_to_stranger", run = function(t)
            t.ticklog.mark("leg.hellhound_to_stranger")
            t.ticks(2)
            t.expect("quest.stage.ambush.again", t.quest.expect_stage("ambush"))
            t.cheat("::give rune_scimitar 1")
            t.cheat("::give lobster 12")
            t.ticks(2)
            t.exec("equip.scimitar", t.player.equip, "rune_scimitar")
            -- Back in through the passage mouth (4633 [12..18]): a fresh copy, the hound set on the player.
            t.exec("enterCaveHellhound", t.player.click_loc, "route_cavewalltunnel", 1, { at = { 3492, 9823, 0 } })
            t.ticks(6)
            t.exec("prayMelee", t.prayer.set, "protectfrommelee", true)
            t.exec("killHellhound", t.player.attack, "skeleton_hellhound", 2, 30)
            t.exec("killHellhound.dead", t.npc.await_dead_engaged, 300, 50, { eat = { item = "lobster", below = 40 } })
            t.ticks(6)
            t.expect("quest.stage.saved_myreque", t.quest.expect_stage("saved_myreque"))
            t.ticks(6)
            -- Veliaf speaks as the black lifts (4633 [60..92]).
            t.exec("veliafMourns", t.chat.play, { "npc:Sani... Harold... That murderer killed them!",
                "player:I'm so sorry. This is all my fault.", "npc:I don't understand...", "player:He was the one that asked me",
                "npc:I see...", "npc:I'm sorry, friend, but he clearly took advantage of you.", "player:No, but I'm still responsible.",
                "npc:Yes... The burden of what happened here", "player:That's not enough.", "npc:He will almost certainly have moved on now.",
                "player:Maybe, but I have to try.", "npc:Well, I won't stop you", "npc:If you head back into the main cave",
                "player:That's very useful. Thanks.", "npc:Once you are done", "choose:Okay, thanks.", "player:Okay, thanks." })
            t.chat.continue_(true); t.ticks(2)
            t.expect("quest.stage.told_exit_route", t.quest.expect_stage("told_exit_route"))
            local _, back = t.world.tile()
            t.check("houndDead-where", type(back) == "table" and back.x == 3506 and back.z == 9837,
                "out of the copy at " .. where(t) .. " (4633 [56]: 3506,9837)")
            -- The way out (4633 [150..222]): the cave mouth, the false wall, the ladder.
            t.exec("leaveCave", t.player.click_loc, "route_cavewalltunnel", 1, { at = { 3505, 9831, 0 } })
            t.ticks(6)
            t.exec("falseWall", t.player.click_loc, "thrttavernbasementfalsewall", 1, { at = { 3480, 9837, 0 } })
            t.ticks(4)
            t.expect("quest.stage.discovered_wall", t.quest.expect_stage("discovered_wall"))
            t.exec("goUpToCanifis", t.player.click_loc, "thrttavernbasementladder", 1, { at = { 3477, 9846, 0 } })
            t.ticks(4)
            t.expect("quest.stage.found_exit", t.quest.expect_stage("found_exit"))
            local _, top = t.world.tile()
            t.check("goUpToCanifis-where", type(top) == "table" and top.x == 3495 and top.z == 3465,
                "up at " .. where(t) .. " (4633 [222]: 3495,3465)")
            -- The Stranger (4633 [258..268]) and the scroll.
            local snap_r, snap = t.skill.snapshot()
            t.check("snapshot", snap_r == "ok", "xp before the hand-in")
            t.exec("goto-talkToStranger", t.player.goto_tile, 3503, 3478, 0)
            t.exec("talkToStranger", t.player.talk_to, "multi_vanstrom_stranger_entity", 1)
            t.exec("talkToStranger-dialog", t.chat.play, { "npc:Hello, friend. What can I do for you?",
                "player:Oh, sorry. I thought you were someone else.", "npc:Who did you think I was?",
                "player:I thought... Well, I guess it doesn't matter now.", "npc:If you say so, friend." })
            t.ticks(3)
            t.quest.expect_complete()
            for _, sk in ipairs({ "attack", "defence", "strength", "hitpoints", "crafting" }) do
                t.expect("reward." .. sk .. "_xp", t.skill.expect_gain(sk, 600, snap))
            end
        end },
    },
}
