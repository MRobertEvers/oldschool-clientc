-- random_events: the random events added 2026-11-21, each started with its :: hook and
-- driven to its end, for the tick log, to diff against the RSProx sessions named in each
-- script's header (docs/rsprox-analysis/randoms/random_events.md).
--
--   python3 tools/quest_gate/run.py --script test/randoms/random_events.lua --no-publish
local function near(t, sym)
    local r, _, rows = t.npc.tiles(sym, 20)
    return r == "ok" and rows and rows[1] ~= nil
end

local function at(t, x, z, slack)
    local r, tile = t.world.tile()
    local s = slack or 0
    return r == "ok" and tile and math.abs(tile.x - x) <= s and math.abs(tile.z - z) <= s,
        tostring(tile and tile.x) .. "," .. tostring(tile and tile.z)
end

local function server(t, name)
    local r, v = t.var.server(name)
    if r ~= "ok" then return nil end
    return v
end

local function press(t, name)
    local wr, w = t.ui.widget(name)
    if wr ~= "ok" or w == nil then return "not_found", name end
    return t.ui.invoke(w, 1)
end

local function arrive(t, cheat, sym)
    t.cheat(cheat, false)
    t.ticks(3)
    t.check(sym .. ".here", near(t, sym), sym .. " within 20")
    t.ticks(9)
end

return {
    id = "random_events",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = {
        "::clearinv",
    },
    run = function(t)
        t.ticklog.start()

        -- The drunken dwarf: a beer and a kebab, gone three ticks after the page.
        t.ticklog.mark("dwarf")
        arrive(t, "::drunkendwarf", "macro_dwarf")
        local r, d = t.player.talk_to("macro_dwarf", 1)
        t.check("dwarf.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("dwarf.page", t.chat.play, { "npc:I 'new it were you matey!" })
        t.expect("dwarf.reward", t.msg.expect("Your reward is: <col=ff0000>1</col> x <col=ff0000>Kebab</col>."))
        local _, beers = t.inv.count("beer")
        t.check("dwarf.beer", beers == 1, "beer " .. tostring(beers))
        t.ticks(5)
        t.check("dwarf.gone", not near(t, "macro_dwarf"), "the dwarf left")

        -- The Mysterious Old Man: a present.
        t.ticklog.mark("oldman")
        arrive(t, "::oldman", "macro_mysterious_old_man")
        r, d = t.player.talk_to("macro_mysterious_old_man", 1)
        t.check("oldman.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("oldman.page", t.chat.play, { "npc:Ah, so you are there. I hoped you would talk to me" })
        t.expect("oldman.reward", t.msg.expect("Your reward is:"))
        t.ticks(5)
        t.check("oldman.gone", not near(t, "macro_mysterious_old_man"), "the old man left")

        -- Count Check, no Bank PIN: the fail.
        t.ticklog.mark("count")
        arrive(t, "::countcheck", "macro_countcheck_surface")
        r, d = t.player.talk_to("macro_countcheck_surface", 1)
        t.check("count.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("count.pages", t.chat.play, {
            "npc:I, Count Check, have checked you, but alas, you have failed!",
            "mesbox:You do not have a Bank PIN, so you fail Count Check's checks!",
        })
        t.ticks(4)
        t.check("count.gone", not near(t, "macro_countcheck_surface"), "the Count left")

        -- The sandwich lady: the tray; the first slot is pressed, right or wrong.
        t.ticklog.mark("sandwich")
        arrive(t, "::sandwichlady", "macro_sandwich_lady_npc")
        r, d = t.player.talk_to("macro_sandwich_lady_npc", 1)
        t.check("sandwich.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("sandwich.page", t.chat.play, { "npc:You look hungry to me. I tell you what - have a" })
        t.ticks(2)
        t.exec("sandwich.tray", press, t, "sandwich_tray:sandwich_tray_0")
        t.ticks(2)
        t.exec("sandwich.answer", t.chat.drain, {})
        t.ticks(12)
        t.check("sandwich.gone", not near(t, "macro_sandwich_lady_npc"), "the lady left")

        -- The pillory: arrested, three right keys read off the server, home with a reward.
        t.ticklog.mark("pillory")
        local _, home = t.world.tile()
        arrive(t, "::pillory", "macro_pillory_guard")
        r, d = t.player.talk_to("macro_pillory_guard", 1)
        t.check("pillory.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("pillory.pages", t.chat.play, {
            "npc:You're under arrest! I've come to lock you up.",
            "choose:/Okay, guv/",
            "player:Okay, guv, I'll come quietly.",
        })
        t.ticks(4)
        local ok, where = at(t, 3228, 3407)
        t.check("pillory.cage", ok, "in the cage " .. where)
        for lock = 1, 3 do
            local w = server(t, "varp9837_macro_event_want") or 0
            local shape = w % 4
            local button = 1
            for b = 1, 3 do
                if math.floor(w / (4 ^ b)) % 4 == shape then button = b end
            end
            t.exec("pillory.key" .. lock, press, t, "pillory:button" .. button)
            t.ticks(2)
        end
        t.ticks(4)
        ok, where = at(t, home.x, home.z, 8)
        t.check("pillory.home", ok, "back home " .. where)
        t.expect("pillory.reward", t.msg.expect("Your reward is:"))

        -- The mime: four right copies, read off the server, then home.
        t.ticklog.mark("mime")
        arrive(t, "::mime", "macro_mime_invitation")
        r, d = t.player.talk_to("macro_mime_invitation", 1)
        t.check("mime.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("mime.pages", t.chat.play, {
            "npc:would you like to come and perform in a mime show?",
            "choose:/love to do a mime show/",
            "player:Yeah, I'd love to do a mime show.",
            "npc:You need to copy the mime's performance",
        })
        ok, where = at(t, 2008, 4764)
        t.check("mime.stage", ok, "on stage " .. where)
        for round = 1, 4 do
            local want = 0
            for _ = 1, 30 do
                t.ticks(1)
                want = server(t, "varp9837_macro_event_want") or 0
                if want > 0 then break end
            end
            t.check("mime.round" .. round, want > 0, "the panel is up")
            t.ticks(2)
            t.exec("mime.copy" .. round, press, t, "macro_mime_emotes:button_" .. (want - 1))
            t.ticks(8)
        end
        t.ticks(3)
        ok, where = at(t, home.x, home.z, 8)
        t.check("mime.home", ok, "back home " .. where)
        t.expect("mime.reward", t.msg.expect("Your reward is: <col=ff0000>1</col> x <col=ff0000>Mime mask</col>."))

        -- The Drill Demon: five orders, each done on the mat whose sign shows it.
        t.ticklog.mark("drill")
        arrive(t, "::drilldemon", "macro_drilldemon_invitation")
        r, d = t.player.talk_to("macro_drilldemon_invitation", 1)
        t.check("drill.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("drill.pages", t.chat.play, {
            "npc:You've been recommended for my corps.",
            "choose:/Sir, yes sir/",
            "player:Sir, yes sir!",
        })
        t.ticks(2)
        ok, where = at(t, 3163, 4820)
        t.check("drill.camp", ok, "in the camp " .. where)
        t.exec("drill.first", t.chat.play, { "npc:Move yourself, Private!" })
        for round = 1, 5 do
            t.exec("drill.order" .. round, t.chat.play, { "npc:private!", "objbox:Go to this mat" })
            local want = server(t, "varp9837_macro_event_want") or 0
            local mat = 1
            for m = 1, 4 do
                local vr, v = t.var.varbit("varb" .. (1334 + m) .. "_macro_drilldemon_post_" .. m)
                if vr == "ok" and v == want then mat = m end
            end
            t.exec("drill.mat" .. round, t.player.click_loc, "barrack_mat_" .. mat, 1)
            t.ticks(9)
        end
        t.exec("drill.done", t.chat.play, { "npc:Well I'll be, you actually did it, Private." })
        t.ticks(2)
        ok, where = at(t, home.x, home.z, 8)
        t.check("drill.home", ok, "back home " .. where)
        t.expect("drill.reward", t.msg.expect("Your reward is: <col=ff0000>1</col> x <col=ff0000>Camo top</col>."))

        -- Evil Bob (fishing): the island, the servant, the net, the right spot, the cold fire,
        -- the fish fed, the portal home with 650 xp.
        t.ticklog.mark("bob")
        arrive(t, "::evilbob", "macro_evil_bob_outside")
        r, d = t.player.talk_to("macro_evil_bob_outside", 1)
        t.check("bob.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("bob.pages", t.chat.play, {
            "npc:come with me if you like fishing!",
            "choose:/good idea/",
        })
        t.ticks(2)
        t.exec("bob.arrival", t.chat.play, {
            "player:Huh?",
            "player:Where am I?",
            "npc:On my island.",
            "player:Take me to your leader!",
            "npc:I am your leader, you are but a slave!",
            "player:I am not a slave! I am a free person!",
            "npc:Ah-ha-ha-ha-ha-ha!",
        })
        r, d = t.player.talk_to("macro_evil_bob_female_servant", 1)
        t.check("bob.servant", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("bob.servant.pages", t.chat.play, {
            "player:I need help, I've been kidnapped by an evil cat!",
            "npc:He'll kill us all!",
            "player:He's just a little cat!",
            "npc:give him the f-f-f-fish he likes",
            "npc:That fishing spot c-c-contains",
            "mesbox:Evil Bob's servant points at a fishing spot.",
        })
        t.exec("bob.net", t.player.click_obj, "evil_bob_net", 3)
        t.exec("bob.fish", t.player.click_loc, "macro_evil_bob_fishing_spot", 1, { at = { 2526, 4791, 0 } })
        t.ticks(10)
        t.exec("bob.catch", t.chat.play, { "objbox:You catch a... what is this??" })
        local _, fish = t.inv.count("evil_bob_cooked_fish_correct")
        t.check("bob.right_fish", fish == 1, "right fish " .. tostring(fish))
        t.exec("bob.uncook", t.player.use_on, "evil_bob_cooked_fish_correct", t.player.by_symbol("loc", "macro_evil_bob_uncook_pot"))
        t.ticks(4)
        t.exec("bob.feed", t.player.use_on, "evil_bob_uncooked_fish_correct", t.player.by_symbol("npc", "macro_evil_bob_island"))
        t.exec("bob.fed", t.chat.play, {
            "npc:Mmm, mmm... that's... mmm... delicious...",
            "npc:Now, let me take... a little... catnap...",
        })
        t.exec("bob.portal", t.player.click_loc, "macro_evil_bob_portal", 1)
        t.ticks(2)
        t.exec("bob.home.pages", t.chat.play, { "player:", "mesbox:You feel somehow that you've become better at" })
        ok, where = at(t, home.x, home.z, 8)
        t.check("bob.home", ok, "back home " .. where)

        -- The Freaky Forester: any pheasant's carcass, the hand-in, the exit tile, lederhosen.
        t.ticklog.mark("forester")
        arrive(t, "::freakyforester", "macro_forester_invitation")
        r, d = t.player.talk_to("macro_forester_invitation", 1)
        t.check("forester.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("forester.pages", t.chat.play, {
            "npc:can you come and help me with some pheasants?",
            "choose:/help with your pheasants/",
            "player:Okay, I'll help with your pheasants.",
        })
        t.ticks(2)
        t.exec("forester.order", t.chat.play, { "npc:Could you kill a pheasant with" })
        t.exec("forester.kill", t.player.attack, "macro_pheasant_onetail", 2, 60)
        t.exec("forester.dead", t.npc.await_dead_engaged, 120, 6)
        t.ticks(3)
        t.exec("forester.carcass.page", t.chat.drain, {})
        t.exec("forester.take", t.player.click_obj, "raw_macro_pheasant", 3)
        r, d = t.player.talk_to("macro_forester_m", 1)
        t.check("forester.handin", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("forester.thanks", t.chat.play, { "npc:you may use the portal to leave the area now." })
        t.exec("forester.exit", t.player.walk_to, 2594, 4775, 30)
        t.ticks(4)
        t.exec("forester.reward.page", t.chat.play, { "npc:for your help, many thanks!" })
        ok, where = at(t, home.x, home.z, 8)
        t.check("forester.home", ok, "back home " .. where)
        t.expect("forester.reward", t.msg.expect("Your reward is: <col=ff0000>1</col> x <col=ff0000>Lederhosen hat</col>."))

        -- The maze: in at a corner, the percent counting down, out by the old man's page.
        t.ticklog.mark("maze")
        arrive(t, "::maze", "macro_maze_invitation")
        r, d = t.player.talk_to("macro_maze_invitation", 1)
        t.check("maze.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("maze.pages", t.chat.play, {
            "npc:would you like to come and solve a maze for me?",
            "choose:/I like exploring mazes/",
            "player:Sure, I like exploring mazes.",
            "npc:Reach the maze centre as fast as possible.",
        })
        local p0 = server(t, "varp531_macro_transmit")
        t.ticks(11)
        local p1 = server(t, "varp531_macro_transmit")
        t.check("maze.clock", p0 == 100 and p1 == 98, "percent " .. tostring(p0) .. " -> " .. tostring(p1))

        t.finish(0)
    end,
}
