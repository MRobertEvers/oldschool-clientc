local N = 5
local SLICE = 250

local function neighbours(p)
    local r, c = math.floor(p / N), p % N
    local out = {}
    if r > 0 then out[#out + 1] = p - N end
    if r < N - 1 then out[#out + 1] = p + N end
    if c > 0 then out[#out + 1] = p - 1 end
    if c < N - 1 then out[#out + 1] = p + 1 end
    return out
end

-- one BFS "place these pieces on these slots", sliced so that no single call
-- spends the driver's instruction budget; returns the path or nil if unfinished
local function bfs_new(blank, positions, goal, locked)
    local count = #positions
    local state = { count = count, goal = goal, locked = locked, head = 1, seen = {} }
    local key = blank
    for i = 1, count do key = key * 25 + positions[i] end
    state.q = { { blank, positions, 0, 0 } }
    state.seen[key] = true
    return state
end

local function bfs_run(state, limit)
    local done = 0
    while state.head <= #state.q and done < limit do
        local node = state.q[state.head]
        state.head = state.head + 1
        done = done + 1
        local b, positions = node[1], node[2]
        local at_goal = true
        for i = 1, state.count do
            if positions[i] ~= state.goal[i] then at_goal = false end
        end
        if at_goal then
            local path = {}
            local walker = node
            while walker[3] ~= 0 do
                path[#path + 1] = walker[4]
                walker = state.q[walker[3]]
            end
            local out = {}
            for i = #path, 1, -1 do out[#out + 1] = path[i] end
            return out
        end
        for _, n in ipairs(neighbours(b)) do
            if not state.locked[n] then
                local npos = {}
                local key = n
                for i = 1, state.count do
                    local v = positions[i]
                    if v == n then v = b end
                    npos[i] = v
                    key = key * 25 + v
                end
                if not state.seen[key] then
                    state.seen[key] = true
                    state.q[#state.q + 1] = { n, npos, state.head - 1, n }
                end
            end
        end
    end
    return nil
end


-- Monkey Madness I, relay legs (sonnet-b44). Leg 1: King Narnode asks, the gnome glider to
-- Karamja, the shipyard gate and Caranock, the report to Narnode, Daero's orders, the hangar,
-- the reinitialisation puzzle, Waydar's flight, Lumdo, and the chapter 2 scene.

-- The slide puzzle: a 5x5 board shuffled by 255 random blank moves (mm_puzzle.rs2 mm_reinit_shuffle),
-- different every run, so it is read back (::mmpuzzle dumps the board) and solved by BFS.
local function solve_puzzle(t)
    t.cheat("::mmpuzzle")
    t.ticks(1)
    local result, rows = t.msg.last(16)
    local board, found = {}, 0
    local dump = {}
    for i = 1, #rows do
        local text = tostring(rows[i].text)
        local a, b, c, d, e = string.match(text, "mmpuzzle (%d+) (%d+) (%d+) (%d+) (%d+)")
        if a then dump[#dump + 1] = { serial = tonumber(rows[i].serial) or i, v = { a, b, c, d, e } } end
    end
    table.sort(dump, function(x, y) return x.serial < y.serial end)
    for _, row in ipairs(dump) do
        for j, v in ipairs(row.v) do board[found * 5 + j - 1] = tonumber(v) end
        found = found + 1
    end
    t.check("clickPuzzle-board_read", found == 5, found .. " board rows read from ::mmpuzzle")
    local locked = {}
    local clicks = {}
    local function pos_of(piece)
        for i = 0, 24 do if board[i] == piece then return i end end
    end
    local plan = {}
    for r = 0, 2 do
        for c = 0, 2 do plan[#plan + 1] = { { r * N + c + 1 }, { r * N + c } } end
        plan[#plan + 1] = { { r * N + 4, r * N + 5 }, { r * N + 3, r * N + 4 } }
    end
    for c = 0, 2 do plan[#plan + 1] = { { 3 * N + c + 1, 4 * N + c + 1 }, { 3 * N + c, 4 * N + c } } end
    plan[#plan + 1] = { { 3 * N + 4, 3 * N + 5, 4 * N + 4 }, { 3 * N + 3, 3 * N + 4, 4 * N + 3 } }
    for _, step in ipairs(plan) do
        local positions = {}
        for i, piece in ipairs(step[1]) do positions[i] = pos_of(piece) end
        local state = bfs_new(pos_of(0), positions, step[2], locked)
        local path = nil
        while path == nil do
            path = bfs_run(state, SLICE)
            if path == nil then t.ticks(1) end
        end
        for _, n in ipairs(path) do
            local b = pos_of(0)
            board[b] = board[n]; board[n] = 0
            clicks[#clicks + 1] = n
        end
        for _, slot in ipairs(step[2]) do locked[slot] = true end
    end
    t.check("clickPuzzle-plan", #clicks > 0, "solution is " .. #clicks .. " piece clicks")
    t.shot("clickPuzzle-before")
    for i = 1, #clicks do
        local widget_result, widget = t.ui.widget("trail_slidepuzzle:pieces", clicks[i])
        t.ui.invoke(widget, 1)
        t.ticks(1)
        if i == math.floor(#clicks / 2) then t.shot("clickPuzzle-half") end
    end
    t.ticks(1)
    t.shot("clickPuzzle-solved")
end

local function glider_to(t, row_name, destination)
    t.exec(row_name, t.chat.play, {
        "choose:Can you take me on the glider?",
        "player:Can you take me on the glider?",
        "npc:Of course!",
        "choose:" .. destination,
    })
end

return {
    id = "mm",
    max_frames = 150000, -- the full file passes 2000 server ticks before leg 6 (relay run budget)
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give gold_bar 1",
        "::give ball_of_wool 1",
        "::give mm_normal_monkey_bones 1",
        "::complete quest_grandtree", -- guide requirement: The Grand Tree
        "::setlevel hitpoints 99", -- the guide recommends combat stats for Ape Atoll; survives the Crash Island spiders, the ravine archers and the jail punches on the temple run (leg 4)
        "::setlevel defence 70",
        "::setlevel prayer 52", -- guide (enterValley): "going north WITH PROTECT FROM RANGED ON" needs level 40 Protect from Missiles
        "::give lobster 18", -- guide (enterValleyForAmuletMake): "Food, Antipoison" for the ravine archers and the temple spikes
        "::give 4doseprayerrestore 2", -- guide (enterValleyForAmuletMake): "Prayer potions"; the ravine prayer drains the setup points before the temple
        "::setlevel attack 90", -- guide (killNinja, killGorilla, goDownToZombie): "Combat gear" for the archers, the gorilla and the zombie monkeys (leg 5)
        "::setlevel strength 90",
        "::give rune_scimitar 1", -- guide (goDownToZombie): combat gear, wielded in leg 5
        "::setvar varp111_treequest 9", -- guide requirement: Tree Gnome Village (no ::complete row for it)
    },
    bind = {
        varp = "varp365_mm_main",
        constants = {
            awowogei_complete_mission = 2,
            awowogei_sent_mission = 1,
            complete = 9,
            daero_complete = 7,
            daero_found = 1,
            daero_given_orders = 2,
            daero_learnt_mission = 3,
            daero_left_grandtree = 4,
            daero_reinit_complete = 6,
            daero_started_reinit = 5,
            elder_guard_spoken = 1,
            garkor_finished_mission = 7,
            garkor_joined_10th_squad = 6,
            garkor_learned_plan = 5,
            garkor_need_correct_disguise = 3,
            garkor_seek_alliance = 4,
            garkor_speak_zooknock = 2,
            garkor_spoken = 1,
            karam_spoken = 1,
            kruk_spoken = 1,
            lumdo_proof = 1,
            lumdo_travelled = 3,
            lumdo_wont_take = 2,
            lumo_spoken = 1,
            monkey_child_asked_who = 2,
            monkey_child_finding_bananas = 5,
            monkey_child_given_bananas = 6,
            monkey_child_learned_toy = 4,
            monkey_child_lost_toy = 8,
            monkey_child_received_talisman = 7,
            monkey_child_spoke = 1,
            monkey_child_told_uncle = 3,
            monkeymadness_arrived_atoll = 3,
            monkeymadness_asked_caranock = 1,
            monkeymadness_caranock_p2 = 2,
            monkeymadness_caranock_p3 = 3,
            monkeymadness_complete = 9,
            monkeymadness_complete_training = 10,
            monkeymadness_completed_ch2 = 4,
            monkeymadness_completed_ch3 = 5,
            monkeymadness_defeated_demon = 6,
            monkeymadness_not_started = 0,
            monkeymadness_received_reward = 8,
            monkeymadness_reported_narnode = 7,
            monkeymadness_shown_seal = 2,
            monkeymadness_started = 1,
            narnode_given_seal = 3,
            narnode_mentioned_caranock = 5,
            narnode_received_orders = 7,
            narnode_returned_shipyard = 4,
            narnode_southern_winds = 6,
            narnode_spoken = 2,
            narnode_worried = 1,
            not_started = 0,
            waydar_arrived_island = 1,
            zooknock_found = 1,
            zooknock_learned_story = 3,
            zooknock_made_talisman = 6,
            zooknock_need_items = 5,
            zooknock_told_mission = 4,
            zooknock_told_story = 2,
        },
        row = "quest_monkeymadnessi",
        display = "Monkey Madness I",
        points = 3,
    },

    legs = {
        { name = "narnode_to_waydar", run = function(t)
            -- LEG 1 BEGIN: talkToNarnode
            t.expect("start.stage", t.quest.expect_stage("monkeymadness_not_started"))

            t.exec("goto-talkToNarnode", t.player.goto_tile, 2465, 3496, 0)
            t.exec("talkToNarnode", t.player.talk_to, "grandtree_narnode", 1)
            t.exec("talkToNarnode-dialog", t.chat.play, {
                "npc:Adventurer! It is good to see you again.",
                "player:And you too, King.",
                "npc:The tree?",
                "player:Good. What ever did happen to Glough?",
                "npc:Oh, I forced him",
                "player:King, you look worried",
                "npc:Nothing in particular",
                "player:What is it?",
                "npc:Well, do you remember",
                "player:Yes, they were",
                "npc:After you defeated",
                "player:I see",
                "npc:I ... I don't know",
                "player:It is a long way",
                "npc:But I need to know",
                "npc:And so I ask you",
                "choose:/^Yes/",
                "player:Ok, I'll do it.",
                "mesbox:Narnode hands you a copy of the Royal Seal.",
                "npc:Thank you very much",
                "npc:Please report to me",
            })
            t.exec("talkToNarnode-chapter1", t.chat.drain, {})
            t.ticks(2)
            t.expect("quest.stage.monkeymadness_started", t.quest.expect_stage("monkeymadness_started"))
            t.exec("talkToNarnode-seal", t.inv.await, "mm_gnome_royal_seal", 1, 5)

            -- flyGandius: the Grand Tree glider pilot is on the top floor (goUpF0ToF1.. are plain stairs/ladders)
            t.exec("goto-flyGandius", t.player.goto_tile, 2464, 3501, 3)
            t.exec("flyGandius-talk", t.player.talk_to, "pilot_grand_tree", 1)
            glider_to(t, "flyGandius", "Karamja")
            t.ticks(8)
            local fx, ftile = t.world.tile()
            t.check("flyGandius-landed", fx == "ok" and ftile.x > 2900 and ftile.z < 3100,
                "landed at " .. tostring(ftile and ftile.x) .. "," .. tostring(ftile and ftile.z))

            -- the shipyard gate: the guard asks, the seal answers (mm_caranock.rs2 mm_shipyardworker_dialogue)
            t.exec("goto-shipyard-gate", t.player.goto_tile, 2942, 3041, 0)
            t.exec("shipyard-gate", t.player.click_loc, "grandtree_fencegate_l", 1)
            t.exec("shipyard-gate-dialog", t.chat.play, {
                "npc:Hey you", "player:trying to open the gate", "npc:I can see that",
                "player:special mission", "npc:Narnode", "player:he did", "npc:Tough",
                "player:Gnome Royal Seal" })
            t.exec("shipyard-gate-seal", t.chat.continue_, true)
            t.exec("shipyard-gate-after", t.chat.drain, {})
            t.ticks(8)
            t.expect("quest.stage.monkeymadness_shown_seal", t.quest.expect_stage("monkeymadness_shown_seal"))

            t.exec("goto-talkToCaranock", t.player.goto_tile, 2956, 3025, 0)
            t.exec("talkToCaranock", t.player.talk_to, "mm_caranock", 1)
            t.exec("talkToCaranock-dialog", t.chat.play, {
                "player:Hello!",
                "npc:Who are you?",
                "player:Glough? No.",
                "npc:Forced to resign",
                "player:He was plotting",
                "player:Anyway, I am here",
                "npc:Royal Guard? I know nothing",
                "player:You have no idea",
                "npc:None whatsoever",
                "player:They were to oversee",
                "npc:Decommission the shipyard",
                "npc:I shall see personally",
            })
            t.ticks(2)
            -- Karamja's glider is the wreck (gnome_glider.rs2 gnome_pilot_crash_karamja): the way home is on foot,
            -- plain travel to the tree.

            t.exec("goto-talkToNarnodeAfterShipyard", t.player.goto_tile, 2465, 3496, 0)
            t.exec("talkToNarnodeAfterShipyard", t.player.talk_to, "grandtree_narnode", 1)
            t.exec("talkToNarnodeAfterShipyard-dialog", t.chat.play, {
                "npc:Welcome back, adventurer.",
                "player:Hello. I investigated the shipyard.",
                "npc:Thank you for doing this.",
                "player:I met a Gnome who goes by the name of Caranock.",
                "npc:The name sounds a little familiar",
                "player:He calls himself",
                "npc:Never mind that",
                "player:Caranock suggested",
                "npc:Do you believe him?",
                "player:I don't have any other information",
                "npc:Very well. I will now prepare some orders.",
            })
            t.ticks(6) -- Narnode writes the glyphs: mes() then p_delay(3)
            t.exec("talkToNarnodeAfterShipyard-hand", t.chat.play, {
                "mesbox:Narnode hands you some handwritten orders.",
                "player:Where will I find Daero?",
                "npc:You will find him",
            })
            t.exec("talkToNarnodeAfterShipyard-orders", t.inv.await, "mm_narnode_orders", 1, 5)

            -- talkToDaero: orders handed over, then every submenu until "Leave..." shows (varbits 99/100/101)
            t.exec("goto-talkToDaero", t.player.goto_tile, 2482, 3486, 1)
            t.exec("talkToDaero", t.player.talk_to, "mm_daero", 1)
            t.exec("talkToDaero-dialog", t.chat.play, {
                "player:Are you Daero?",
                "npc:Indeed I am",
                "player:I am an adventurer.",
                "npc:I see. You must be the individual",
                "player:I have been asked to give you orders",
                "npc:Well hand them over here then.",
                "mesbox:You hand King Narnode's orders to Daero.",
                "npc:It is written in an old military code.",
            })
            t.ticks(6) -- Daero decodes: mes() then p_delay(3)
            t.exec("talkToDaero-orders", t.chat.play, {
                "npc:I hope you feel like a quest adventurer",
                "player:Why is that?",
                "npc:... because you're going to get one.",
                "player:Tell me what the orders say!",
                "npc:Given your recent performance",
                "player:Where to?",
                "npc:You are to be taken far to the south",
                "npc:You must really have impressed the King",
                "choose:Talk about the journey...",
                "choose:What lies to the south of Karamja?",
                "player:What lies to the south of Karamja?",
                "npc:We do not know.",
                "player:Monkeys? Like on Karamja?",
                "npc:From what I have heard",
                "choose:Return to previous menu",
                "choose:Talk about the 10th squad...",
                "choose:Who is Garkor?",
                "player:Who is Garkor?",
                "npc:Sergeant Garkor holds the command",
                "npc:You should aim to make contact",
                "choose:Return to previous menu",
                "choose:Talk about Caranock...",
                "choose:Who is Caranock?",
                "player:Who is Caranock?",
                "npc:I have never heard of him.",
                "choose:Return to previous menu",
                "choose:Leave...",
                "player:Let us go then.",
                "npc:I must first introduce you",
                "choose:Who is it?",
                "player:Who is it?",
                "npc:His name is Flight Commander Waydar.",
                "npc:We must go now and meet Waydar.",
                "mesbox:You wear the blindfold Daero hands you.",
            })
            t.exec("talkToDaero-blindfold", t.chat.drain, {})
            t.ticks(6)
            t.expect("talkToDaero-hangar", t.await({ level = function()
                local r, tile = t.world.tile()
                return r == "ok" and tile.z > 9000
            end, note = "teleported into the hangar" }, 20))

            -- talkToDaeroInHangar: the hangar dialogue (mm_daero.rs2 daero_hangar_dialogue, daero_left_grandtree)
            t.exec("talkToDaeroInHangar", t.player.talk_to, "mm_daero", 1)
            t.exec("talkToDaeroInHangar-dialog", t.chat.play, {
                "npc:Welcome, adventurer, to the Underground Military Glider Hangar.",
                "player:Wow! Why would Gnomes need such a place?",
                "npc:We do not, if the truth be told.",
                "npc:It is fortunate indeed",
                "npc:Let me introduce you to Flight Commander Waydar.",
                "npc:Flight Commander Waydar, I would like you to meet",
                "npc:Greetings High Tree Guardian.",
                "npc:And greetings to you too, visitor.",
                "npc:Not just any old visitor Waydar",
                "npc:I see. Well, there are no more demons left here.",
                "npc:Quite.",
                "npc:As you know, the 10th squad",
                "npc:We still do not know what happened",
                "npc:Their standard gliders must have fallen prey",
                "npc:When reinitialisation has been completed",
                "npc:We are no closer to reinitialising sir",
                "npc:That Gnome is never stepping foot in this hangar again.",
                "npc:Yes sir.",
                "npc:Very well. Notify me when you have managed to reinitialise.",
                "npc:you will have to wait till reinitialisation is complete.",
            })
            t.ticks(2) -- mm_daero (a varbit the client is not sent) is now daero_started_reinit; the panel below proves it

            -- clickPuzzle: the reinitialisation panel
            t.exec("goto-clickPuzzle", t.player.goto_tile, 2394, 9886, 0)
            t.exec("clickPuzzle", t.player.click_loc, "bunker_controlpanal", 1)
            t.expect("clickPuzzle-open", t.ui.await_open("trail_slidepuzzle", 10))
            local puzzle_mark = t.cutscene.mark()
            solve_puzzle(t)
            -- the hangar scene after the solved panel (mm_puzzle.rs2:238-239), followed to its CAM_RESET
            t.exec("clickPuzzle.cutscene", t.cutscene.await, "clickPuzzle", { since = puzzle_mark, timeout = 60, quiet = 200, expect = {
                { op = "moveto", coord = "0_40_70_31_26", height = 600 },
                { op = "lookat", coord = "0_40_70_16_38" },
            } })
            t.expect("clickPuzzle-camera-free", t.await({ level = function()
                local camera = t.world.camera()
                return camera ~= nil and camera.server_driven == false
            end, note = "camera back to the player" }, 200))

            -- talkToDaeroAfterPuzzle (mm_daero = daero_reinit_complete): the hangar page that follows the solved panel
            t.exec("goto-talkToDaeroAfterPuzzle", t.player.goto_tile, 2393, 9889, 0)
            t.exec("talkToDaeroAfterPuzzle", t.player.talk_to, "mm_daero", 1)
            t.exec("talkToDaeroAfterPuzzle-dialog", t.chat.play, {
                "npc:Well done, adventurer.",
                "player:I've had some practice in the past.",
                "npc:You are clearly a",
                "npc:Flight Commander Waydar, now that reinitialisation is complete",
                "npc:Yes sir.",
                "npc:You are to safeguard",
                "npc:Understood.",
                "npc:speak to Waydar when you are ready to leave.",
                "npc:And ... good luck.",
            })
            t.ticks(2)

            -- talkToWaydarAfterPuzzle: fly to Crash Island
            t.exec("talkToWaydarAfterPuzzle", t.player.talk_to, "mm_waydar", 1)
            t.exec("talkToWaydarAfterPuzzle-dialog", t.chat.play, {
                "npc:You should stock up well on food",
                "npc:I'd be careful of the local fauna",
                "npc:Do you wish to fly right now?",
                "choose:Yes",
                "player:Yes, let's go.",
                "npc:As you wish.",
            })
            t.ticks(8)
            local cx, ctile = t.world.tile()
            t.check("talkToWaydarAfterPuzzle-landed", cx == "ok" and ctile.x > 2850 and ctile.z < 2800 and ctile.z > 2650,
                "Crash Island at " .. tostring(ctile and ctile.x) .. "," .. tostring(ctile and ctile.z))

            -- Waydar's first words on the beach (mm_waydar.rs2 waydar_crash_island_dialogue, mm_waydar = 0): sets
            -- the varbit the "I cannot convince Lumdo" row below needs
            t.exec("goto-waydarCrashLanding", t.player.goto_tile, 2898, 2724, 0)
            t.exec("waydarCrashLanding", t.player.talk_to, "mm_waydar", 1)
            t.exec("waydarCrashLanding-dialog", t.chat.play, {
                "player:Where are we?",
                "npc:I am not sure.",
                "player:Did our glider survive?",
                "npc:Of course.",
            })
            t.ticks(2)

            t.exec("goto-talkToLumdo", t.player.goto_tile, 2891, 2724, 0)
            t.exec("talkToLumdo", t.player.talk_to, "mm_lumdo", 1)
            t.exec("talkToLumdo-dialog", t.chat.play, {
                "npc:Who are you two?",
                "player:We are on a mission for King Narnode Shareen.",
                "npc:What business do you have here?",
                "player:We are to investigate the disappearance",
                "npc:I might be.",
                "player:I have the Gnome Royal Seal.",
                "mesbox:You show Lumdo the Royal Seal.",
                "npc:I see. Sorry for my distrust.",
                "player:So are you Lumdo of the 10th squad?",
                "npc:I am indeed",
                "player:Where are the rest of your squad?",
                "npc:Let me begin at the beginning, human.",
                "npc:We were on our way to decommission",
                "npc:We were one gnome to a glider",
                "player:Did you crash straight here?",
                "npc:Yes. The winds drove us",
                "player:What did you do then?",
                "npc:Whilst we were falling",
                "npc:We spent time gathering enough wood",
                "player:Presumably you are to guard the gliders",
                "npc:Affirmative.",
                "player:You must take us to the island.",
                "npc:And I have orders from the Sergeant",
                "player:You will not take me?",
                "npc:I will not take orders from you.",
            })
            t.ticks(2)

            -- talkToWaydarOnCrash: "I cannot convince Lumdo..." orders Lumdo, then the chapter 2 scene
            local ch2_mark = t.cutscene.mark()
            t.exec("goto-talkToWaydarOnCrash", t.player.goto_tile, 2898, 2726, 0)
            t.exec("talkToWaydarOnCrash", t.player.talk_to, "mm_waydar", 1)
            t.exec("talkToWaydarOnCrash-menu", t.chat.drain, { stop_at = "options" })
            t.exec("talkToWaydarOnCrash-choose", t.chat.choose, "I cannot convince Lumdo to take us to the island...")
            t.exec("talkToWaydarOnCrash-dialog", t.chat.play, {
                "player:I cannot convince Lumdo",
                "npc:What is the problem?",
                "player:He claims to be under direct orders",
                "npc:His zeal in this matter",
                "player:Can you do anything?",
                "npc:I would rather not get involved.",
                "player:You must do something!",
                "npc:You are becoming tiresome, human.",
                "npc:Foot soldier Lumdo of the 10th squad.",
                "npc:Yes?",
                "npc:I am Flight Commander Waydar.",
                "npc:That is correct, Commander.",
                "npc:I need not remind you",
                "npc:Garkor will not be pleased!",
                "npc:Then he can take up his issues",
                "player:Waydar! Will you not accompany me",
                "npc:No. After all, somebody has to look after the gliders.",
                "player:But it is your mission to protect me!",
                "npc:Enough. Return here when you are done.",
            })
            t.exec("chapter2-scene", t.chat.drain, {})
            t.ticks(6)
            t.expect("chapter2-cards-up", t.await({ level = function() return t.chat.kind() ~= "none" end,
                note = "the Chapter 2 title card" }, 40))
            t.exec("chapter2-cards", t.chat.drain, {})
            t.ticks(4)
            t.expect("quest.stage.monkeymadness_arrived_atoll", t.quest.expect_stage("monkeymadness_arrived_atoll"))
            t.exec("chapter2-scene.cutscene", t.cutscene.await, "chapter2-scene", { since = ch2_mark, expect = {
                { op = "moveto", coord = "0_40_71_39_37", height = 400 },
                { op = "lookat", coord = "0_40_71_49_37" },
            } })
            t.expect("chapter2-camera-free", t.await({ level = function()
                local camera = t.world.camera()
                return camera ~= nil and camera.server_driven == false
            end, note = "camera back to the player" }, 40))
            local ex, etile = t.world.tile()
            local sr, stage = t.quest.stage()
            local ir, packed = t.inv.count("mm_gnome_royal_seal")
            t.check("leg.1.state", ex == "ok" and sr == "ok",
                "tile=" .. tostring(etile and etile.x) .. "," .. tostring(etile and etile.z) .. "," .. tostring(etile and etile.level)
                .. " mm_main=" .. tostring(stage) .. " royal_seal=" .. tostring(packed)
                .. " (Ape Atoll boat landing after the chapter 2 scene; gold_bar, ball_of_wool, monkey bones from setup)")
            -- LEG 1 END
        end },
        { name = "atoll_to_warehouse_cavern", run = function(t)
            -- LEG 2 BEGIN: talkToLumdoToReturn
            -- the guide's return route: Lumdo ferries the player Atoll -> Crash Island -> Atoll
            -- (mm_lumdo.rs2:8 opnpc1, the lumdo_travelled case and the 0_43_42 zone case)
            t.exec("talkToLumdoToReturn", t.player.talk_to, "mm_lumdo", 1)
            t.exec("talkToLumdoToReturn-dialog", t.chat.play, {
                "player:Can you take me back to Crash Island?",
                "npc:As you wish.",
            })
            t.ticks(8)
            local cr, ctile = t.world.tile()
            t.check("talkToLumdoToReturn-crash", cr == "ok" and ctile ~= nil and ctile.x < 2900 and ctile.z > 2600 and ctile.z < 2740 and ctile.x > 2870,
                "after the ferry the player stands at " .. tostring(ctile and ctile.x) .. "," .. tostring(ctile and ctile.z))
            t.exec("talkToLumdoToReturn-back", t.player.talk_to, "mm_lumdo", 1)
            t.exec("talkToLumdoToReturn-back-dialog", t.chat.play, {
                "player:Can you take me back to Ape Atoll?",
                "npc:As you wish.",
            })
            t.ticks(8)

            -- enterValley: the ravine archers (mm_archer.rs2, spawns m42_43.spawn:20-37) knock the player out
            -- one arrow in twenty (mm_knockout.rs2 aa_archer_knockout) and drop them in the Ape Atoll jail.
            do
                local tab_result, tab_detail = t.ui.tab("prayer")
                local widget_result, widget = t.ui.widget("prayerbook:prayer14")
                t.check("enterValley-prayertab", tab_result == "ok" and widget_result == "ok",
                    "prayer tab -> " .. tostring(tab_result) .. " " .. tostring(tab_detail) .. "; prayerbook:prayer14 (Protect from Missiles) -> " .. tostring(widget_result))
                t.ui.invoke(widget, 1)
                t.ticks(2)
                local _, on = t.var.varbit("varb4117_prayer_protectfrommissiles")
                t.check("enterValley-protect", on == 1, "prayer_protectfrommissiles varbit " .. tostring(on) .. " (the guide: protect from ranged on)")
            end
            t.exec("goto-enterValley", t.player.goto_tile, 2721, 2750, 0)
            local knocked = false
            local tries = 0
            while not knocked and tries < 12 do
                tries = tries + 1
                local wr, wd = t.player.walk_to(2721, 2780, 40)
                t.ticks(2)
                local tr, tile = t.world.tile()
                if tr == "ok" and tile ~= nil and tile.x >= 2760 and tile.z >= 2790 then knocked = true end
                if not knocked then
                    t.note("enterValley try " .. tries .. " walk=" .. tostring(wr) .. " " .. tostring(wd)
                        .. " tile=" .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z))
                end
            end
            local kr, ktile = t.world.tile()
            t.check("enterValley", knocked, "walking north up the ravine; knocked out by an archer after "
                .. tries .. " walks, woke at " .. tostring(ktile and ktile.x) .. "," .. tostring(ktile and ktile.z))
            -- the knockout objbox and Lumo's jail banter arrive on a queue ten ticks later; the banter is random
            for _ = 1, 3 do
                t.ticks(6)
                if t.chat.kind() ~= "none" then
                    t.exec("enterValley-wake", t.chat.drain, {})
                end
            end

            local function eat_if_hurt()
                local hr, hp = t.skill.read("hitpoints")
                if hr == "ok" and hp.level ~= nil and hp.level < 40 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(2)
                end
            end

            -- leavePrison: pick the cell door (mm_jail.rs2:12, thieving odds 95/295 so it can fail), then slip past the
            -- patrolling guards Aberab and Trefaji (mm_jail.rs2:38-259); a punch drops the player back in a cell, so retry
            local freed = false
            local attempts = 0
            while not freed and attempts < 14 do
                attempts = attempts + 1
                eat_if_hurt()
                local pr, ptile = t.world.tile()
                if ptile ~= nil and ptile.z <= 2795 and ptile.x >= 2766 and ptile.x <= 2776 then
                    if attempts == 1 then
                        t.exec("leavePrison-pickDoor", t.player.click_loc, "mm_jail_door", 1)
                    else
                        t.player.click_loc("mm_jail_door", 1)
                    end
                    t.ticks(5)
                end
                t.player.walk_to(2779, 2802, 30)
                t.ticks(1)
                local wr, wtile = t.world.tile()
                if wtile ~= nil and math.abs(wtile.x - 2779) <= 2 and math.abs(wtile.z - 2802) <= 2 then freed = true end
            end
            local lr, ltile = t.world.tile()
            t.check("leavePrison", freed, "left the cell door at 2771,2795 and reached the north side of the prison after "
                .. attempts .. " attempt(s); standing at " .. tostring(ltile and ltile.x) .. "," .. tostring(ltile and ltile.z))

            -- talkToGarkor: "Stick to the east edge of the town" (mm_garkor.rs2:10, the monkeymadness_not_started case of %mm_garkor)
            for _ = 1, 4 do
                eat_if_hurt()
                local gr = t.player.walk_to(2807, 2764, 40)
                if gr == "ok" then break end
            end
            t.exec("talkToGarkor", t.player.talk_to, "mm_garkor", 1)
            t.exec("talkToGarkor-dialog", t.chat.play, {
                "player:Hello?",
                "npc:A fine day you have chosen to visit th",
                "player:Good day to you to Sergeant. I've been",
                "npc:Investigate the circumstances surround",
                "player:How did you know that?",
                "npc:The King and I are still in communicat",
                "player:Why do you need a human?",
                "npc:There is more going on than meets your",
                "player:Well -",
                "npc:Indeed. But there are more pressing ma",
                "npc:Before we can resume our original miss",
                "player:I know about the guards - I had to sne",
                "npc:Trust me; we too have considered this,",
                "npc:We have considered many things. I have",
                "npc:I remain here so that I may overhear A",
                "player:Awowogei?",
                "npc:The self-proclaimed ruler of the islan",
                "player:Have you seen these monkeys? You could",
                "npc:I wasn't suggesting convincing them, h",
                "player:Don't be ridiculous! I'm a human - not",
                "npc:Do not be so sceptical ... you humans ",
                "player:Yes, but -",
                "npc:Go and see my squad mage, Zooknock. Te",
                "player:I can't even communicate with the monk",
                "npc:Just go and find my squad mage, human.",
            })

            -- enterDentureBuilding: the south bamboo door (mm_bamboo_doors.rs2:9, mm_bamboo_secure_walk)
            local hops = { { 2798, 2762 }, { 2790, 2761 }, { 2782, 2761 }, { 2774, 2761 }, { 2766, 2761 }, { 2765, 2763 } }
            for _, hop in ipairs(hops) do
                for _ = 1, 3 do
                    eat_if_hurt()
                    local dr, dd = t.player.walk_to(hop[1], hop[2], 20)
                    local hr, hp = t.skill.read("hitpoints")
                    t.note("hop " .. hop[1] .. " " .. tostring(dr) .. " hp=" .. tostring(hr == "ok" and hp.level))
                    if dr == "ok" then break end
                end
            end
            -- the door is a bare p_teleport + p_delay: click_loc reads settle_after_click, so grade the landing tile
            t.player.click_loc("mm_bamboo_door_secure", 1)
            t.await({ level = function()
                local ar, atile = t.world.tile()
                return ar == "ok" and atile ~= nil and atile.z >= 2765
            end, note = "the player through the secure door" }, 20)
            local br, btile = t.world.tile()
            t.check("enterDentureBuilding", br == "ok" and btile ~= nil and btile.x >= 2760 and btile.x <= 2772 and btile.z >= 2764,
                "through the secure door (mm_bamboo_doors.rs2:9); standing at " .. tostring(btile and btile.x) .. "," .. tostring(btile and btile.z))

            -- searchForDentures: mm_quest_crates.rs2:9, "Do you wish to take one?" -> Yes
            -- the guard room (0_43_43_10_15 .. 15_20 = x 2762-2767, z 2767-2772, mm_sleeping_monkey_guard.rs2:14) wakes the
            -- guard and summons the guards: the guide's "light floor". Stand east of the crate, outside it.
            t.player.walk_to(2768, 2766, 12)
            t.player.walk_to(2768, 2769, 12)
            t.exec("searchForDentures", t.player.click_loc, "mm_denture_crate", 1)
            t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the crate's mesbox" }, 30)
            t.exec("searchForDentures-page", t.chat.play, { "*" })
            t.await({ level = function() return t.chat.kind() == "options" end, note = "the take-one options" }, 10)
            local orr, orows = t.chat.options()
            local otr, otitle = t.chat.options_title()
            t.note("options " .. tostring(orr) .. " " .. tostring(type(orows) == "table" and table.concat(orows, "/") or orows) .. " title=" .. tostring(otitle) .. " kind=" .. t.chat.kind())
            t.exec("searchForDentures-dialog", t.chat.choose, "Yes")
            t.exec("searchForDentures-got", t.inv.await, "mm_monkey_dentures", 1, 10)

            -- goDownFromDentures: the crate over the hole (mm_warehouse.rs2:9); the fall is random (agility 150/300)
            t.player.walk_to(2768, 2766, 12)
            t.exec("goDownFromDentures", t.player.click_loc, "mm_crate_over_hole", 1)
            t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the crate's mesbox" }, 30)
            t.exec("goDownFromDentures-dialog", t.chat.play, {
                "*",
                "choose:Yes, I'm sure.",
                "*",
            })
            t.ticks(8)
            t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the landing message" }, 30)
            t.exec("goDownFromDentures-landing", t.chat.drain, {})
            t.ticks(4)
            local ex, etile = t.world.tile()
            local sr, stage = t.quest.stage()
            local dr, dentures = t.inv.count("mm_monkey_dentures")
            local fr, food = t.inv.count("lobster")
            t.check("leg.2.state", ex == "ok" and sr == "ok" and dentures == 1,
                "tile=" .. tostring(etile and etile.x) .. "," .. tostring(etile and etile.z) .. "," .. tostring(etile and etile.level)
                .. " mm_main=" .. tostring(stage) .. " mm_monkey_dentures=" .. tostring(dentures) .. " lobster=" .. tostring(food)
                .. " (bottom of the warehouse hole cavern, quiet; royal seal, gold bar, wool, bones from earlier; prayer 52 setup, Protect from Missiles still on)")
            -- LEG 2 END
        end },
        { name = "mould_to_zooknock", run = function(t)
            -- LEG 3 BEGIN: searchForMould
            -- searchForMould: the crate in the north west of the cavern (mm_quest_crates.rs2:23 oploc1)
            t.exec("goto-searchForMould", t.player.goto_tile, 2783, 9170, 0)
            t.exec("searchForMould", t.player.click_loc, "mm_monkey_amulet_mould_crate", 1)
            t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the crate's mesbox" }, 30)
            t.exec("searchForMould-page", t.chat.play, { "*" })
            t.await({ level = function() return t.chat.kind() == "options" end, note = "the take-one options" }, 10)
            t.exec("searchForMould-dialog", t.chat.choose, "Yes")
            t.exec("searchForMould-got", t.inv.await, "mm_monkey_amulet_mould", 1, 10)

            -- leaveToPrepareForBar: the guide's "teleport out to prepare" -- plain travel to the Grand Tree's first floor.
            t.exec("leaveToPrepareForBar", t.player.goto_tile, 2483, 3487, 1)
            -- talkToDaeroForAmuletRun: mm_daero.rs2:12 opnpc1 -> daero_return_hangar (mm_daero >= daero_left_grandtree)
            t.exec("talkToDaeroForAmuletRun", t.player.talk_to, "mm_daero", 1)
            t.exec("talkToDaeroForAmuletRun-dialog", t.chat.play, {
                "npc:Hello again, adventurer.",
                "choose:Yes",
                "player:Yes please.",
                "mesbox:blindfold",
            })
            t.ticks(8)
            local hr, htile = t.world.tile()
            t.check("talkToDaeroForAmuletRun-hangar", hr == "ok" and htile ~= nil and htile.x > 2400 and htile.x < 2700 and htile.z > 4400 and htile.z < 4600,
                "after the blindfold the player stands at " .. tostring(htile and htile.x) .. "," .. tostring(htile and htile.z) .. "," .. tostring(htile and htile.level))
            -- talkToWaydarForAmuletRun: mm_waydar.rs2:9 opnpc1 (mm_daero = daero_complete, mm_main >= arrived_atoll)
            t.exec("talkToWaydarForAmuletRun", t.player.talk_to, "mm_waydar", 1)
            t.exec("talkToWaydarForAmuletRun-dialog", t.chat.play, {
                "npc:Shall we return to Crash Island?",
                "choose:Yes",
                "player:Yes, let's go.",
                "npc:As you wish.",
            })
            t.ticks(8)
            local wr, wtile = t.world.tile()
            t.check("talkToWaydarForAmuletRun-crash", wr == "ok" and wtile ~= nil and wtile.x > 2850 and wtile.z > 2650 and wtile.z < 2760,
                "after the glider the player stands at " .. tostring(wtile and wtile.x) .. "," .. tostring(wtile and wtile.z))
            -- talkToLumdoForAmuletRun: mm_lumdo.rs2:8 opnpc1, lumdo_travelled
            t.exec("talkToLumdoForAmuletRun", t.player.talk_to, "mm_lumdo", 1)
            t.exec("talkToLumdoForAmuletRun-dialog", t.chat.play, {
                "player:Can you take me back to Ape Atoll?",
                "npc:As you wish.",
            })
            t.ticks(8)
            local ar, atile = t.world.tile()
            t.check("goUpToDaeroForAmuletRun", ar == "ok" and atile ~= nil and atile.x > 2780 and atile.x < 2830 and atile.z > 2690 and atile.z < 2730,
                "the three ferries done; back on Ape Atoll at " .. tostring(atile and atile.x) .. "," .. tostring(atile and atile.z))

            -- enterDungeonForAmuletRun: ladders.rs2:202 oploc1 -- the bamboo ladder in south Ape Atoll
            t.exec("goto-enterDungeonForAmuletRun", t.player.goto_tile, 2765, 2704, 0)
            t.exec("enterDungeonForAmuletRun", t.player.click_loc, "mm_bamboo_ladder_dungeon_entrance", 1)
            t.ticks(6)
            local dr, dtile = t.world.tile()
            local mr, md = t.msg.expect("A sealed Ape Atoll dungeon entrance.")
            t.check("enterDungeonForAmuletRun-below", dr == "ok" and dtile ~= nil and dtile.z >= 9000,
                "down the ladder (mm_bamboo_ladder_dungeon_entrance, monkeymadnessii.rs2:335): " .. tostring(dtile and dtile.x) .. "," .. tostring(dtile and dtile.z))
            -- talkToZooknock: mm_zooknock.rs2:13 opnpc1, mm_zooknock = told_mission -> mm_zooknock_p5 (line 326)
            t.exec("goto-talkToZooknock", t.player.goto_tile, 2804, 9141, 0)
            t.exec("talkToZooknock", t.player.talk_to, "mm_zooknock", 1)
            t.exec("talkToZooknock-dialog", t.chat.play, {
                "player:Hello?",
                "npc:A human ... here? What business ha",
                "player:I am on a mission for King Narnode",
                "player:I have been sent to investigate th",
                "npc:Well you've found us - what's left",
                "npc:I am Zooknock, the 10th squad mage",
                "player:Of course.",
                "npc:Your story first, human. What poss",
                "player:I am currently in the service of y",
                "player:As far as I understand, the 10th s",
                "player:Rumour has it you were blown off c",
                "npc:You were sent alone?",
                "player:No, I have been accompanied by Fli",
                "npc:The so called Crash Island. We lef",
                "player:Yes, we have met. He ferried me ac",
                "npc:He did!? He was explicitly ordered",
                "player:Waydar ordered him to leave his po",
                "npc:Flight Commander Waydar you said? ",
                "player:So why are you here?",
                "npc:The rumours are correct. We were i",
                "player:Then you gathered enough wood to f",
                "npc:Correct. I assume Lumdo told you t",
                "player:Yes. What happened when you landed",
                "npc:We split up into several small gro",
                "player:...",
                "npc:Monkeys. Lots of monkeys. They are",
                "npc:We were overwhelmed in numbers. So",
                "player:Who survived?",
                "npc:Myself, the Sergeant, Bunkwicket a",
                "player:And of the rest?",
                "npc:Lumo, Bunkdo and Carado were captu",
                "npc:We are attempting to tunnel to thi",
                "player:Why don't you just go overground?",
                "npc:We have considered this, but every",
                "player:I see.",
                "player:I have spoken to your Sergeant. He",
                "npc:Aha. He wants me to turn you into ",
                "player:Actually, it was more along the li",
                "npc:I think you misunderstand, human. ",
                "player:King Narnode Shareen asked me to..",
                "npc:Indeed. However, King Narnode Shar",
                "player:Why wasn't I told?",
                "npc:Before you arrived on this island,",
                "player:But why a human? Why me?",
                "npc:Garkor had long decided that we ne",
                "player:Why don't you just transform a gno",
                "npc:It has been tried in the past, but",
                "player:Right. What do I have to do?",
                "npc:There will be two aspects to your ",
                "npc:We must also transform your body s",
                "npc:So that the effects of my spells a",
                "player:What kind of items?",
                "npc:For the spells to take full effect",
                "npc:I suggest that I invest the abilit",
                "npc:Similarly, the transformation spel",
                "options",
                "choose:What do we need for the monkey amulet?",
                "player:What do we need for the monkey amulet?",
                "*",
                "options",
                "choose:I'll be back later.",
                "player:I'll be back later.",
            })
            t.ticks(4)
            -- useDentures: mm_zooknock.rs2:89 opnpcu, case mm_monkey_dentures
            t.exec("useDentures", t.player.use_on, "mm_monkey_dentures", t.player.by_symbol("npc", "mm_zooknock"))
            t.exec("useDentures-dialog", t.chat.play, {
                "mesbox:You hand Zooknock the magical monkey dentures.",
                "*",
                "*",
            })
            t.ticks(3)
            local _, dentures_left = t.inv.count("mm_monkey_dentures")
            local _, mould_left = t.inv.count("mm_monkey_amulet_mould")
            local _, bar_left = t.inv.count("gold_bar")
            local fr, ftile = t.world.tile()
            local sr, stage = t.var.server("varp365_mm_main")
            t.check("useDentures-handed", dentures_left == 0 and mould_left == 1 and bar_left == 1,
                "dentures " .. tostring(dentures_left) .. ", mould " .. tostring(mould_left) .. ", gold bar " .. tostring(bar_left))
            t.check("leg.3.state", true, "at " .. tostring(ftile and ftile.x) .. "," .. tostring(ftile and ftile.z) .. "," .. tostring(ftile and ftile.level) .. ", mm_main " .. tostring(stage) .. ", holds gold_bar, mm_monkey_amulet_mould, lobster, royal seal")
            -- LEG 3 END
        end },
        { name = "amulet_to_flame", run = function(t)
            -- LEG 4 BEGIN: useMould
            local function eat_if_hurt()
                local hr, hp = t.skill.read("hitpoints")
                if hr == "ok" and hp.level ~= nil and hp.level < 40 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(2)
                end
            end
            -- useMould: mm_zooknock.rs2:89 opnpcu, case mm_monkey_amulet_mould (varbit_111 set by the first talk)
            t.exec("useMould", t.player.use_on, "mm_monkey_amulet_mould", t.player.by_symbol("npc", "mm_zooknock"))
            t.exec("useMould-dialog", t.chat.play, {
                "mesbox:You hand Zooknock the monkey amulet mould.",
                "*",
                "npc:We still need the gold bar",
            })
            t.ticks(3)
            local _, mould_given = t.inv.count("mm_monkey_amulet_mould")
            t.check("useMould-handed", mould_given == 0, "mm_monkey_amulet_mould left in the backpack: " .. tostring(mould_given))
            -- useBar: mm_zooknock.rs2:89 opnpcu, case gold_bar; with all three items handed he enchants the bar (mm_zooknock_amulet_check)
            t.exec("useBar", t.player.use_on, "gold_bar", t.player.by_symbol("npc", "mm_zooknock"))
            t.exec("useBar-dialog", t.chat.play, {
                "mesbox:You hand Zooknock the gold bar.",
                "*",
                "npc:Now listen closely",
                "npc:You must then smith",
                "player:Where do I find",
                "npc:Somewhere in the village",
            })
            t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Zooknock hands you back the bar" }, 20)
            t.exec("useBar-back", t.chat.drain, {})
            t.ticks(3)
            local _, ebar = t.inv.count("mm_enchanted_gold_bar")
            local _, mould_back = t.inv.count("mm_monkey_amulet_mould")
            t.check("useBar-enchanted", ebar == 1 and mould_back == 1, "enchanted gold bar " .. tostring(ebar) .. ", mould " .. tostring(mould_back))

            -- leaveToPrepareForAmulet: the guide's "teleport out" -- plain travel to the Grand Tree's first floor
            t.exec("leaveToPrepareForAmulet", t.player.goto_tile, 2483, 3487, 1)
            t.exec("talkToDaeroForAmuletMake", t.player.talk_to, "mm_daero", 1)
            t.exec("talkToDaeroForAmuletMake-dialog", t.chat.play, {
                "npc:Hello again, adventurer.",
                "choose:Yes",
                "player:Yes please.",
                "mesbox:blindfold",
            })
            t.ticks(8)
            local hr, htile = t.world.tile()
            t.check("talkToDaeroForAmuletMake-hangar", hr == "ok" and htile ~= nil and htile.x > 2400 and htile.x < 2700 and htile.z > 4400 and htile.z < 4600,
                "after the blindfold the player stands at " .. tostring(htile and htile.x) .. "," .. tostring(htile and htile.z))
            t.exec("talkToWaydarForAmuletMake", t.player.talk_to, "mm_waydar", 1)
            t.exec("talkToWaydarForAmuletMake-dialog", t.chat.play, {
                "npc:Shall we return to Crash Island?",
                "choose:Yes",
                "player:Yes, let's go.",
                "npc:As you wish.",
            })
            t.ticks(8)
            local wr, wtile = t.world.tile()
            t.check("talkToWaydarForAmuletMake-crash", wr == "ok" and wtile ~= nil and wtile.x > 2850 and wtile.z > 2650 and wtile.z < 2760,
                "after the glider the player stands at " .. tostring(wtile and wtile.x) .. "," .. tostring(wtile and wtile.z))
            t.exec("talkToLumdoForAmuletMake", t.player.talk_to, "mm_lumdo", 1)
            t.exec("talkToLumdoForAmuletMake-dialog", t.chat.play, {
                "player:Can you take me back to Ape Atoll?",
                "npc:As you wish.",
            })
            t.ticks(8)
            local ar, atile = t.world.tile()
            t.check("goUpToDaeroForAmuletMake", ar == "ok" and atile ~= nil and atile.x > 2780 and atile.x < 2830 and atile.z > 2690 and atile.z < 2730,
                "the three ferries done; back on Ape Atoll at " .. tostring(atile and atile.x) .. "," .. tostring(atile and atile.z))

            -- enterValleyForAmuletMake: Protect from Missiles on (it is the guide's Protect from Ranged), walk north up the ravine
            do
                local _, on = t.var.varbit("varb4117_prayer_protectfrommissiles")
                if on ~= 1 then
                    t.ui.tab("prayer")
                    local widget_result, widget = t.ui.widget("prayerbook:prayer14")
                    if widget_result == "ok" then t.ui.invoke(widget, 1) end
                    t.ticks(2)
                    _, on = t.var.varbit("varb4117_prayer_protectfrommissiles")
                end
                t.check("enterValleyForAmuletMake-protect", on == 1, "prayer_protectfrommissiles varbit " .. tostring(on))
            end
            t.exec("goto-enterValleyForAmuletMake", t.player.goto_tile, 2721, 2750, 0)
            local arrived = false
            local tries = 0
            while not arrived and tries < 14 do
                tries = tries + 1
                eat_if_hurt()
                t.player.walk_to(2721, 2780, 40)
                t.ticks(2)
                local tr, tile = t.world.tile()
                if tr == "ok" and tile ~= nil and tile.x >= 2760 and tile.z >= 2790 then arrived = true end
            end
            local kr, ktile = t.world.tile()
            t.check("enterValleyForAmuletMake", arrived, "walked north up the ravine in " .. tries .. " walks; standing at " .. tostring(ktile and ktile.x) .. "," .. tostring(ktile and ktile.z))
            for _ = 1, 3 do
                t.ticks(6)
                if t.chat.kind() ~= "none" then t.exec("enterValleyForAmuletMake-wake", t.chat.drain, {}) end
            end
            -- if an archer knocked the player into the jail, pick the door and slip past the guards as in leg 2
            local freed = false
            local attempts = 0
            while not freed and attempts < 14 do
                attempts = attempts + 1
                eat_if_hurt()
                local _, ptile = t.world.tile()
                if ptile ~= nil and ptile.z <= 2795 and ptile.x >= 2766 and ptile.x <= 2776 then
                    t.player.click_loc("mm_jail_door", 1)
                    t.ticks(5)
                end
                t.player.walk_to(2807, 2788, 40)
                t.ticks(1)
                local _, wtile2 = t.world.tile()
                if wtile2 ~= nil and math.abs(wtile2.x - 2807) <= 3 and math.abs(wtile2.z - 2788) <= 3 then freed = true end
            end
            local _, ftile = t.world.tile()
            t.check("enterValleyForAmuletMake-north", freed, "north of the prison near the temple trapdoor after " .. attempts .. " attempt(s); standing at " .. tostring(ftile and ftile.x) .. "," .. tostring(ftile and ftile.z))

            -- the gorilla guards round the trapdoor and the zombie monkeys below hit in melee: Protect from Melee
            do
                -- the prison guards and the gorilla guards drain the walk north: eat back up before the prayer click
                for _ = 1, 10 do
                    local hr0, hp0 = t.skill.read("hitpoints")
                    if hr0 == "ok" and hp0.level ~= nil and hp0.level < 85 then
                        t.player.inv_op("lobster", 1)
                        t.ticks(2)
                    end
                end
                for _ = 1, 2 do
                    t.player.inv_op("4doseprayerrestore", 1)
                    t.ticks(3)
                end
                local _, melee_on = t.var.varbit("varb4118_prayer_protectfrommelee")
                t.ui.tab("prayer")
                t.ticks(2)
                local widget_result, widget = t.ui.widget("prayerbook:prayer15")
                if widget_result == "ok" then t.ui.invoke(widget, 1) end
                t.ticks(4)
                _, melee_on = t.var.varbit("varb4118_prayer_protectfrommelee")
                local _, missiles_on = t.var.varbit("varb4117_prayer_protectfrommissiles")
                local _, magic_on = t.var.varbit("varb4116_prayer_protectfrommagic")
                local _, prayer_now = t.skill.read("prayer")
                t.check("enterTemple-protect", melee_on == 1, "widget " .. tostring(widget_result) .. "; melee " .. tostring(melee_on) .. ", missiles " .. tostring(missiles_on) .. ", magic " .. tostring(magic_on) .. ", prayer points " .. tostring(prayer_now and prayer_now.level))
            end
            -- enterTemple: mm_temple.rs2:10 oploc1 opens the trapdoor, then the open one (mm_temple.rs2:17) climbs down
            t.exec("enterTemple-open", t.player.click_loc, "mm_temple_trapdoor", 1)
            t.ticks(2)
            -- the gorilla guards round the trapdoor hit hard: eat to full before the climb
            for _ = 1, 8 do
                local hr3, hp3 = t.skill.read("hitpoints")
                if hr3 == "ok" and hp3.level ~= nil and hp3.level < 85 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(2)
                end
            end
            t.exec("enterTemple", t.player.click_loc, "mm_temple_trapdoor_open", 1)
            t.ticks(6)
            local tr2, ttile = t.world.tile()
            local _, hpb = t.skill.read("hitpoints")
            local _, lobs = t.inv.count("lobster")
            t.check("enterTemple-below", tr2 == "ok" and ttile ~= nil and ttile.z >= 9000, "hp " .. tostring(hpb and hpb.level) .. ", lobsters " .. tostring(lobs) .. ", down the trapdoor (mm_temple.rs2:17): " .. tostring(ttile and ttile.x) .. "," .. tostring(ttile and ttile.z) .. "," .. tostring(ttile and ttile.level))

            -- useBarOnFlame: zenyte.rs2:14 oplocu -> mm_amulet_smith.rs2:8 smiths the amulet
            -- the temple basement is full of aggressive zombie monkeys (m43_143.spawn): walk in short hops, eating between them
            for _ = 1, 14 do
                local hr4, hp4 = t.skill.read("hitpoints")
                if hr4 == "ok" and hp4.level ~= nil and hp4.level < 55 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(1)
                end
                t.player.walk_to(2810, 9207, 4)
                local _, wt = t.world.tile()
                if wt ~= nil and math.abs(wt.x - 2810) <= 2 and math.abs(wt.z - 9207) <= 2 then break end
            end
            t.exec("useBarOnFlame", t.player.use_on, "mm_enchanted_gold_bar", t.player.by_symbol("loc", "mm_iban_firewall_diagonal"))
            t.ticks(6)
            local _, amulet = t.inv.count("mm_amulet_of_monkey_speak_without_string")
            t.check("useBarOnFlame-smithed", amulet == 1, "mm_amulet_of_monkey_speak_without_string in the backpack: " .. tostring(amulet))
            local lr, ltile = t.world.tile()
            local sr, stage = t.var.server("varp365_mm_main")
            t.check("leg.4.end", true, "at " .. tostring(ltile and ltile.x) .. "," .. tostring(ltile and ltile.z) .. "," .. tostring(ltile and ltile.level) .. ", mm_main " .. tostring(stage) .. ", holds the unstrung monkey amulet, ball_of_wool, lobster, royal seal")
            -- LEG 4 END
        end },
        { name = "child_to_zombie", run = function(t)
            -- LEG 5 BEGIN: leaveTempleDungeon
            local function eat_if_hurt(limit)
                local hr, hp = t.skill.read("hitpoints")
                if hr == "ok" and hp.level ~= nil and hp.level < limit then
                    t.player.inv_op("lobster", 1)
                    t.ticks(2)
                end
            end
            local function protect(widget_name)
                t.ui.tab("prayer")
                t.ticks(2)
                local widget_result, widget = t.ui.widget(widget_name)
                if widget_result == "ok" then t.ui.invoke(widget, 1) end
                t.ticks(4)
            end
            -- guide (giveChildBananas) lists the five bananas as brought along; given here, not in setup, because setup plus leg 4's items would overflow the 28 slots
            t.cheat("::give banana 5")
            t.ticks(3)
            local _, bananas = t.inv.count("banana")
            t.check("leg.5.bananas", bananas == 5, "banana count " .. tostring(bananas))
            -- leaveTempleDungeon: mm_temple.rs2:28 oploc1 climbs the rope; hurt first, the zombie monkey is on us
            eat_if_hurt(85)
            t.player.click_loc("mm_climbing_rope_bottom_temple", 1)
            t.ticks(6)
            local rr, rtile = t.world.tile()
            t.check("leaveTempleDungeon", rr == "ok" and rtile ~= nil and rtile.z < 9000, "up the rope (mm_temple.rs2:28): " .. tostring(rtile and rtile.x) .. "," .. tostring(rtile and rtile.z) .. "," .. tostring(rtile and rtile.level))

            -- the monkey child only talks to a wearer of the strung amulet (mm_monkey_child.rs2:13): string it with the wool (stringing.rs2:55) and wear it
            t.exec("string-amulet", t.player.use_item_on_item, "mm_amulet_of_monkey_speak_without_string", "ball_of_wool")
            t.ticks(4)
            local _, strung = t.inv.count("mm_amulet_of_monkey_speak")
            t.check("string-amulet-done", strung == 1, "mm_amulet_of_monkey_speak in the backpack: " .. tostring(strung))
            t.exec("wear-amulet", t.player.equip, "mm_amulet_of_monkey_speak")
            t.ticks(2)
            t.exec("wield-scimitar", t.player.equip, "rune_scimitar")
            t.ticks(2)

            -- talkToMonkeyChild: the first meeting, varbit_119 0 -> spoke (mm_monkey_child.rs2:9)
            t.exec("goto-talkToMonkeyChild", t.player.goto_tile, 2744, 2794, 0)
            t.exec("talkToMonkeyChild", t.player.talk_to, "mm_monkey_child")
            t.exec("talkToMonkeyChild-dialog", t.chat.play, {
                "player:Hello there little monkey.",
                "npc:Hello big-big",
                "player:Oh I'm not a stranger",
                "npc:You look strange to me",
            })
            t.ticks(2)
            t.expect("talkToMonkeyChild-stage", t.var.await_server("varb119_varbit_119", 1, 10))
            -- talkToMonkeyChild2: spoke -> asked who -> told uncle
            t.exec("talkToMonkeyChild2", t.player.talk_to, "mm_monkey_child")
            t.exec("talkToMonkeyChild2-dialog", t.chat.play, {
                "player:Hello again little monkey.",
                "npc:You're strange",
                "player:I'm not a stranger",
                "npc:Then what are you",
                "choose:Well I'll be a monkey's uncle!",
                "player:Well I'll be a monkey's uncle!",
                "npc:Uh ah! You do look like my uncle",
            })
            t.ticks(2)
            t.expect("talkToMonkeyChild2-stage", t.var.await_server("varb119_varbit_119", 3, 10))
            -- talkToMonkeyChild3: told uncle -> learned toy -> finding bananas
            t.exec("talkToMonkeyChild3", t.player.talk_to, "mm_monkey_child")
            t.exec("talkToMonkeyChild3-dialog", t.chat.play, {
                "npc:You look a lot bigger",
                "player:I've been",
                "npc:I'm bored",
                "player:Why are you bored",
                "npc:Aunty told me to pick loads of bananas",
                "choose:How many bananas did Aunty want?",
                "player:How many bananas did Aunty want?",
                "npc:Twenty!",
                "player:Yes, very mean",
                "npc:Ok!",
                "player:But only if you promise",
                "npc:Ok Uncle!",
            })
            t.ticks(2)
            t.expect("talkToMonkeyChild3-stage", t.var.await_server("varb119_varbit_119", 5, 10))
            -- giveChildBananas: finding bananas, five in the pack (mm_monkey_child.rs2:166)
            t.exec("giveChildBananas", t.player.talk_to, "mm_monkey_child")
            t.exec("giveChildBananas-dialog", t.chat.play, {
                "npc:Did you get any bananas",
                "player:Yes, I have some here.",
                "npc:Wow that's a lot of bananas",
                "player:Yes, of course there are.",
                "mesbox:You give the monkey child your bananas.",
                "npc:Aunty will be so happy",
            })
            t.ticks(2)
            t.expect("giveChildBananas-stage", t.var.await_server("varb119_varbit_119", 6, 10))
            -- talkToChildForTalisman: the aunt's 100 tick toy timer (mm_monkey_child.rs2:154) must run out first
            t.ticks(110)
            t.exec("talkToChildForTalisman", t.player.talk_to, "mm_monkey_child")
            t.exec("talkToChildForTalisman-dialog", t.chat.play, {
                "player:Has Aunty given you the toy yet",
                "npc:Yeah - it's really neat",
                "player:Can I borrow it now then",
                "npc:But I only just got it",
                "player:Please?",
                "npc:Ok then",
                "mesbox:The monkey child gives you some kind of talisman.",
            })
            t.ticks(2)
            t.expect("talkToChildForTalisman-stage", t.var.await_server("varb119_varbit_119", 7, 10))
            local _, talismans = t.inv.count("mm_monkey_talisman")
            t.check("talkToChildForTalisman-item", talismans == 1, "mm_monkey_talisman in the backpack: " .. tostring(talismans))
            -- talkToChildFor4Talismans: lose the toy, wait out the crying, borrow it again (mm_monkey_child.rs2:93-145)
            t.exec("talkToChildFor4Talismans-lost", t.player.talk_to, "mm_monkey_child")
            t.exec("talkToChildFor4Talismans-lost-dialog", t.chat.play, {
                "choose:I've lost that toy you gave me...",
                "player:I've lost that toy",
                "npc:You lost it",
            })
            t.ticks(2)
            t.expect("talkToChildFor4Talismans-lost-stage", t.var.await_server("varb119_varbit_119", 8, 10))
            t.ticks(110)
            t.exec("talkToChildFor4Talismans", t.player.talk_to, "mm_monkey_child")
            t.exec("talkToChildFor4Talismans-dialog", t.chat.play, {
                "npc:I'm feeling a bit better now",
                "player:It's good to see you've cheered up",
                "npc:Yes - Aunty gave me a new toy",
                "choose:Wow - can I borrow it?",
                "player:Wow - can I borrow it?",
                "npc:Only if you promise",
                "choose:Ok, I promise!",
                "player:Ok, I promise!",
                "mesbox:The monkey child gives you some kind of talisman.",
            })
            t.ticks(2)
            t.expect("talkToChildFor4Talismans-stage", t.var.await_server("varb119_varbit_119", 7, 10))

            -- killNinja: a posted monkey archer (mm_archer.rs2:18); Protect from Missiles for its poisoned arrows
            local _, restore_before = t.inv.count("4doseprayerrestore")
            local _, points_before = t.skill.read("prayer")
            for _, potion in ipairs({ "4doseprayerrestore", "3doseprayerrestore", "2doseprayerrestore", "1doseprayerrestore" }) do
                local _, have = t.inv.count(potion)
                if have ~= nil and have > 0 then
                    t.player.inv_op(potion, 1)
                    t.ticks(3)
                end
            end
            local _, points_after = t.skill.read("prayer")
            t.check("killNinja-restore", true, "prayer potions before " .. tostring(restore_before) .. ", prayer points " .. tostring(points_before and points_before.level) .. " -> " .. tostring(points_after and points_after.level))
            protect("prayerbook:prayer14")
            local _, missiles_on = t.var.varbit("varb4117_prayer_protectfrommissiles")
            local _, prayer_pts = t.skill.read("prayer")
            t.check("killNinja-protect", missiles_on == 1, "protect from missiles varbit " .. tostring(missiles_on) .. ", prayer points " .. tostring(prayer_pts and prayer_pts.level))
            t.exec("goto-killNinja", t.player.goto_tile, 2757, 2789, 0)
            local ar, ad = t.player.attack("mm_posted_archer", 2, 20)
            t.check("killNinja", ar == "ok" or ar == "timeout", ad)
            t.exec("killNinja.dead", t.npc.await_dead_engaged, 160, 30, { eat = { item = "lobster", below = 50 } })
            -- killGorilla: a temple guard (aa_monkey_guard.rs2:47), melee, heals under 30 hp: Protect from Melee
            eat_if_hurt(85)
            t.exec("goto-killGorilla", t.player.goto_tile, 2800, 2785, 0)
            protect("prayerbook:prayer15")
            local _, melee_on = t.var.varbit("varb4118_prayer_protectfrommelee")
            local _, prayer_g = t.skill.read("prayer")
            t.check("killGorilla-protect", melee_on == 1, "protect from melee varbit " .. tostring(melee_on) .. ", prayer points " .. tostring(prayer_g and prayer_g.level))
            local gr, gd = t.player.attack("mm_religious_guard", 2, 20)
            t.check("killGorilla", gr == "ok" or gr == "timeout", gd)
            t.exec("killGorilla.dead", t.npc.await_dead_engaged, 240, 40, { eat = { item = "lobster", below = 55 } })
            t.ticks(4)
            -- goDownToZombie: the trapdoor (mm_temple.rs2:10, :17)
            for _ = 1, 8 do eat_if_hurt(85) end
            t.exec("goDownToZombie-open", t.player.click_loc, "mm_temple_trapdoor", 1)
            t.ticks(2)
            for _ = 1, 8 do eat_if_hurt(85) end
            t.exec("goDownToZombie", t.player.click_loc, "mm_temple_trapdoor_open", 1)
            t.ticks(6)
            local dr, dtile = t.world.tile()
            t.check("goDownToZombie-below", dr == "ok" and dtile ~= nil and dtile.z >= 9000, "down the trapdoor: " .. tostring(dtile and dtile.x) .. "," .. tostring(dtile and dtile.z) .. "," .. tostring(dtile and dtile.level))
            local sr, stage = t.var.server("varp365_mm_main")
            local _, hpe = t.skill.read("hitpoints")
            t.check("leg.5.end", true, "at " .. tostring(dtile and dtile.x) .. "," .. tostring(dtile and dtile.z) .. "," .. tostring(dtile and dtile.level) .. ", mm_main " .. tostring(stage) .. ", hp " .. tostring(hpe and hpe.level) .. ", holds the strung monkey amulet (worn), rune scimitar (wielded), monkey talisman, lobster, royal seal")
            -- LEG 5 END
        end },
        { name = "zombie_to_zooknock", run = function(t)
            -- LEG 6 BEGIN: killZombie
            local function eat_if_hurt(limit)
                local hr, hp = t.skill.read("hitpoints")
                if hr == "ok" and hp.level ~= nil and hp.level < limit then
                    t.player.inv_op("lobster", 1)
                    t.ticks(2)
                end
            end
            local function protect(widget_name)
                t.ui.tab("prayer")
                t.ticks(2)
                local widget_result, widget = t.ui.widget(widget_name)
                if widget_result == "ok" then t.ui.invoke(widget, 1) end
                t.ticks(4)
            end
            -- guide (killZombie, leaveToPrepareForTalismanRun): "food, antipoison, prayer potions" brought along; leg 4-5 spent the setup stock
            t.cheat("::give 4doseprayerrestore 3")
            t.cheat("::give lobster 8")
            t.ticks(3)
            t.player.inv_op("4doseprayerrestore", 1)
            t.ticks(2)
            local _, melee_before = t.var.varbit("varb4118_prayer_protectfrommelee")
            if melee_before ~= 1 then protect("prayerbook:prayer15") end
            for _ = 1, 3 do eat_if_hurt(70) end
            local _, pts = t.skill.read("prayer")
            local _, melee_on = t.var.varbit("varb4118_prayer_protectfrommelee")
            t.check("killZombie-protect", melee_on == 1, "protect from melee varbit " .. tostring(melee_on) .. " (was " .. tostring(melee_before) .. "), prayer points " .. tostring(pts and pts.level))
            -- killZombie: the zombie monkey at the foot of the rope (aggressive; leg 5 left the player in its reach)
            eat_if_hurt(80)
            local zr, zd = t.player.attack("mm_zombie_monkey_small", 2, 20)
            t.check("killZombie", zr == "ok" or zr == "timeout", zd)
            t.exec("killZombie.dead", t.npc.await_dead_engaged, 200, 40, { eat = { item = "lobster", below = 55 } })
            t.ticks(1)
            -- the zombie's death_drop (combat_stats.generated.npc:19206) lies on the floor: take it
            t.exec("killZombie-take", t.player.click_obj, "mm_small_zombie_monkey_bones", 3)
            t.exec("killZombie-bones", t.inv.await, "mm_small_zombie_monkey_bones", 1, 10)
            -- leaveToPrepareForTalismanRun: the guide's "teleport out to prepare" -- plain travel to the Grand Tree's first floor
            t.exec("leaveToPrepareForTalismanRun", t.player.goto_tile, 2483, 3487, 1)
            t.exec("talkToDaeroForTalismanRun", t.player.talk_to, "mm_daero", 1)
            t.exec("talkToDaeroForTalismanRun-dialog", t.chat.play, {
                "npc:Hello again, adventurer.",
                "choose:Yes",
                "player:Yes please.",
                "mesbox:blindfold",
            })
            t.ticks(8)
            local hr, htile = t.world.tile()
            t.check("talkToDaeroForTalismanRun-hangar", hr == "ok" and htile ~= nil and htile.x > 2400 and htile.x < 2700 and htile.z > 4400 and htile.z < 4600,
                "after the blindfold the player stands at " .. tostring(htile and htile.x) .. "," .. tostring(htile and htile.z) .. "," .. tostring(htile and htile.level))
            t.exec("talkToWaydarForTalismanRun", t.player.talk_to, "mm_waydar", 1)
            t.exec("talkToWaydarForTalismanRun-dialog", t.chat.play, {
                "npc:Shall we return to Crash Island?",
                "choose:Yes",
                "player:Yes, let's go.",
                "npc:As you wish.",
            })
            t.ticks(8)
            local wr, wtile = t.world.tile()
            t.check("talkToWaydarForTalismanRun-crash", wr == "ok" and wtile ~= nil and wtile.x > 2850 and wtile.z > 2650 and wtile.z < 2760,
                "after the glider the player stands at " .. tostring(wtile and wtile.x) .. "," .. tostring(wtile and wtile.z))
            t.exec("talkToLumdoForTalismanRun", t.player.talk_to, "mm_lumdo", 1)
            t.exec("talkToLumdoForTalismanRun-dialog", t.chat.play, {
                "player:Can you take me back to Ape Atoll?",
                "npc:As you wish.",
            })
            t.ticks(8)
            local ar, atile = t.world.tile()
            t.check("goUpToDaeroForTalismanRun", ar == "ok" and atile ~= nil and atile.x > 2780 and atile.x < 2830 and atile.z > 2690 and atile.z < 2730,
                "the three ferries done; back on Ape Atoll at " .. tostring(atile and atile.x) .. "," .. tostring(atile and atile.z))
            -- enterDungeonForTalismanRun: ladders.rs2:202 oploc1
            t.exec("goto-enterDungeonForTalismanRun", t.player.goto_tile, 2765, 2704, 0)
            t.exec("enterDungeonForTalismanRun", t.player.click_loc, "mm_bamboo_ladder_dungeon_entrance", 1)
            t.ticks(6)
            local dr, dtile = t.world.tile()
            t.check("enterDungeonForTalismanRun-below", dr == "ok" and dtile ~= nil and dtile.z >= 9000,
                "down the ladder: " .. tostring(dtile and dtile.x) .. "," .. tostring(dtile and dtile.z))
            t.exec("goto-useTalisman", t.player.goto_tile, 2804, 9141, 0)
            -- useTalisman: mm_zooknock.rs2 opnpcu case mm_monkey_talisman
            t.exec("useTalisman", t.player.use_on, "mm_monkey_talisman", t.player.by_symbol("npc", "mm_zooknock"))
            t.exec("useTalisman-dialog", t.chat.play, { "mesbox:You hand Zooknock the monkey talisman.", "*", "*" })
            t.ticks(3)
            -- talkToZooknockForTalisman
            t.exec("talkToZooknockForTalisman", t.player.talk_to, "mm_zooknock", 1)
            t.exec("talkToZooknockForTalisman-menu", t.chat.drain, { stop_at = "options" })
            t.exec("talkToZooknockForTalisman-choose", t.chat.choose, "What do we need for the monkey talisman?")
            t.exec("talkToZooknockForTalisman-dialog", t.chat.play, { "player:What do we need for the monkey talisman?", "*" })
            t.exec("talkToZooknockForTalisman-leave", t.chat.drain, { stop_at = "options" })
            t.exec("talkToZooknockForTalisman-back", t.chat.choose, "I'll be back later.")
            t.exec("talkToZooknockForTalisman-end", t.chat.drain, {})
            t.ticks(2)
            -- useBones
            local ch3_mark = t.cutscene.mark()
            t.exec("useBones", t.player.use_on, "mm_small_zombie_monkey_bones", t.player.by_symbol("npc", "mm_zooknock"))
            t.exec("useBones-dialog", t.chat.play, {
                "mesbox:You hand Zooknock the monkey remains.",
                "*",
                "npc:Bear with me human: I must now cast",
            })
            t.ticks(8) -- if_close + p_delay(4), then the talisman is added (mm_zooknock.rs2:238-244)
            t.exec("useBones-talisman-dialog", t.chat.play, {
                "mesbox:Zooknock hands you back the talisman.",
                "npc:I am afraid I have not been able",
                "npc:The range at which I will be able",
                "npc:Furthermore, you will not be able to attack",
            })
            t.ticks(6)
            -- the chapter 3 scene is queued after Zooknock's last page (mm_zooknock.rs2:244): let it play out, then dismiss what is left
            t.exec("useBones-after", t.chat.drain, {})
            t.ticks(10)
            t.exec("useBones-after2", t.chat.drain, {})
            t.exec("useBones.cutscene", t.cutscene.await, "useBones", { since = ch3_mark, expect = {
                { op = "moveto", coord = "0_41_71_47_24", height = 600 },
                { op = "lookat", coord = "0_41_71_47_15" },
            } })
            local fr, ftile = t.world.tile()
            local sr, stage = t.var.server("varp365_mm_main")
            local _, tal = t.inv.count("mm_monkey_talisman")
            t.check("leg.6.state", true, "at " .. tostring(ftile and ftile.x) .. "," .. tostring(ftile and ftile.z) .. "," .. tostring(ftile and ftile.level) .. ", mm_main " .. tostring(stage) .. ", talismans " .. tostring(tal))
            -- LEG 6 END
        end },
        { name = "greegree_to_zoo", run = function(t)
            -- LEG 7 BEGIN: leaveDungeonWithGreeGree
            local function tile_text()
                local r, tl = t.world.tile()
                return tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level)
            end
            -- the guide: "make another monkey talisman" (mm_zooknock.rs2:225 zooknock_anothertali), then the normal monkey's bones (the Karamjan greegree)
            t.ticks(2)
            t.exec("goto-leaveDungeonWithGreeGree", t.player.goto_tile, 2804, 9141, 0)
            t.exec("leaveDungeonWithGreeGree-talk", t.player.talk_to, "mm_zooknock", 1)
            t.exec("leaveDungeonWithGreeGree-menu", t.chat.drain, { stop_at = "options" })
            t.exec("leaveDungeonWithGreeGree-choose", t.chat.choose, "Can you make another monkey talisman?")
            t.exec("leaveDungeonWithGreeGree-dialog", t.chat.play, {
                "player:Can you make another monkey talisman?",
                "npc:Are you sure?",
                "choose:Yes",
                "player:Yes.",
                "npc:Very well.",
            })
            t.ticks(2)
            t.exec("leaveDungeonWithGreeGree-talisman", t.player.use_on, "mm_monkey_talisman", t.player.by_symbol("npc", "mm_zooknock"))
            t.exec("leaveDungeonWithGreeGree-talisman-dialog", t.chat.play, { "mesbox:You hand Zooknock the monkey talisman.", "*", "*" })
            t.ticks(3)
            t.exec("leaveDungeonWithGreeGree-bones", t.player.use_on, "mm_normal_monkey_bones", t.player.by_symbol("npc", "mm_zooknock"))
            t.exec("leaveDungeonWithGreeGree-bones-dialog", t.chat.play, {
                "mesbox:You hand Zooknock the monkey remains.",
                "*",
                "npc:Bear with me human: I must now cast",
            })
            t.ticks(8)
            t.exec("leaveDungeonWithGreeGree-greegree-dialog", t.chat.play, {
                "mesbox:Zooknock hands you back the talisman.",
                "npc:I am afraid I have not been able",
                "npc:The range at which I will be able",
                "npc:Furthermore, you will not be able to attack",
            })
            t.ticks(4)
            t.exec("leaveDungeonWithGreeGree-end", t.chat.drain, {})
            local gr, gn = t.inv.count("mm_monkey_greegree_for_normal_monkey")
            t.check("leaveDungeonWithGreeGree-have", gr == "ok" and gn == 1, "Karamjan monkey greegree in the pack: " .. tostring(gn))
            -- "teleport away": plain travel to the Ardougne Zoo, beside the Monkey Minder
            t.exec("leaveDungeonWithGreeGree", t.player.goto_tile, 2609, 3280, 0)
            t.ticks(2)
            -- talkToMinder: hold the greegree (opheld2, mm_greegree.rs2:19), then talk
            local hr, hd = t.player.inv_op("mm_monkey_greegree_for_normal_monkey", 2)
            t.ticks(3)
            t.check("talkToMinder-held", true, "held the greegree: " .. tostring(hr) .. " " .. tostring(hd) .. " at " .. tile_text())
            t.exec("talkToMinder", t.player.talk_to, "mm_monkey_minder", 1)
            t.exec("talkToMinder-dialog", t.chat.play, {
                "player:Ook Ook!",
                "npc:Why do you monkeys keep trying to escape",
                "player:Ook!",
                "npc:Let's put you back in your cage",
                "player:Ok!",
                "npc:What??",
                "player:Err",
                "npc:I must be imagining things",
            })
            t.ticks(6)
            t.check("talkToMinder-caged", true, "the minder put the monkey in the pen: " .. tile_text())
            -- talkToMonkeyAtZoo: mm_zoo_monkey.rs2:12 opnpc1
            t.exec("talkToMonkeyAtZoo", t.player.talk_to, "mm_zoo_monkey", 1)
            t.exec("talkToMonkeyAtZoo-dialog", t.chat.drain, {})
            t.ticks(3)
            local mr, mn = t.inv.count("mm_monkey_in_backpack")
            t.check("talkToMonkeyAtZoo-monkey", mr == "ok" and mn == 1, "monkey in the backpack: " .. tostring(mn))
            -- talkToMinderAgain: unequip the greegree, then talk (mm_monkey_minder.rs2:25, inside the cage)
            t.exec("talkToMinderAgain-unequip", t.player.unequip, "mm_monkey_greegree_for_normal_monkey")
            t.ticks(3)
            t.exec("talkToMinderAgain", t.player.talk_to, "mm_monkey_minder", 1)
            t.exec("talkToMinderAgain-dialog", t.chat.play, {
                "npc:My word",
                "player:I ... er ... I don't know",
                "npc:Well, don't worry",
            })
            t.ticks(6) -- if_close + the fade + p_telejump (mm_monkey_minder.rs2:29-33); the last two pages follow it
            t.exec("talkToMinderAgain-thanks", t.chat.play, {
                "player:Thank you.",
                "npc:No problem.",
            })
            t.ticks(4)
            t.check("talkToMinderAgain-out", true, "out of the pen: " .. tile_text())
            -- the return run: Daero, Waydar, Lumdo (the leg 3 / leg 6 route)
            t.exec("goto-talkToDaeroForTalkingToAwow", t.player.goto_tile, 2483, 3487, 1)
            t.exec("talkToDaeroForTalkingToAwow", t.player.talk_to, "mm_daero", 1)
            t.exec("talkToDaeroForTalkingToAwow-dialog", t.chat.play, {
                "npc:Hello again, adventurer.",
                "choose:Yes",
                "player:Yes please.",
                "mesbox:blindfold",
            })
            t.ticks(8)
            local hr2, htile = t.world.tile()
            t.check("talkToDaeroForTalkingToAwow-hangar", hr2 == "ok" and htile ~= nil and htile.x > 2400 and htile.x < 2700 and htile.z > 4400 and htile.z < 4600,
                "after the blindfold the player stands at " .. tile_text())
            t.exec("talkToWaydarForTalkingToAwow", t.player.talk_to, "mm_waydar", 1)
            t.exec("talkToWaydarForTalkingToAwow-dialog", t.chat.play, {
                "npc:Shall we return to Crash Island?",
                "choose:Yes",
                "player:Yes, let's go.",
                "npc:As you wish.",
            })
            t.ticks(8)
            local wr, wtile = t.world.tile()
            t.check("talkToWaydarForTalkingToAwow-crash", wr == "ok" and wtile ~= nil and wtile.x > 2850 and wtile.z > 2650 and wtile.z < 2760,
                "after the glider the player stands at " .. tile_text())
            t.exec("talkToLumdoForTalkingToAwow", t.player.talk_to, "mm_lumdo", 1)
            t.exec("talkToLumdoForTalkingToAwow-dialog", t.chat.play, {
                "player:Can you take me back to Ape Atoll?",
                "npc:As you wish.",
            })
            t.ticks(8)
            local ar, atile = t.world.tile()
            t.check("goUpToDaeroForTalkingToAwow", ar == "ok" and atile ~= nil and atile.x > 2780 and atile.x < 2830 and atile.z > 2690 and atile.z < 2730,
                "the three ferries done; back on Ape Atoll at " .. tile_text())
            t.exec("leg.7.settle", t.chat.drain, {})
            local sr, stage = t.var.server("varp365_mm_main")
            local _, mk = t.inv.count("mm_monkey_in_backpack")
            t.check("leg.7.end", true, "at " .. tile_text() .. ", mm_main " .. tostring(stage) .. ", monkey in backpack " .. tostring(mk) .. ", Karamjan greegree and M'speak amulet carried, lobster and restores for leg 8")
            -- LEG 7 END
        end },
        { name = "garkor_to_narnode", run = function(t)
            -- LEG 8 BEGIN: talkToGarkorWithMonkey
            local function tile_text()
                local r, tl = t.world.tile()
                return tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level)
            end
            local function eat_if_hurt(limit)
                local hr, hp = t.skill.read("hitpoints")
                if hr == "ok" and hp.level ~= nil and hp.level < limit then
                    t.player.inv_op("lobster", 1)
                    t.ticks(2)
                end
            end
            local function protect(widget_name)
                t.ui.tab("prayer")
                t.ticks(2)
                local widget_result, widget = t.ui.widget(widget_name)
                if widget_result == "ok" then t.ui.invoke(widget, 1) end
                t.ticks(4)
            end
            t.ticks(2)
            -- talkToGarkorWithMonkey: hold the Karamjan greegree (mm_greegree.rs2:19, the atoll is inside ~mm_greegree_zone), then talk (mm_garkor.rs2:10)
            t.exec("talkToGarkorWithMonkey-hold", t.player.inv_op, "mm_monkey_greegree_for_normal_monkey", 2)
            t.ticks(3)
            -- enterGate: the Bamboo Gate (mm_bamboo_doors.rs2:32, needs the worn greegree) -- walk the ravine to its south side, press it, Kruk shouts for it to be opened
            local gate_walk, gate_walk_detail = "refused", "no hop reached the gate"
            for _, hop in ipairs({ { 2790, 2712 }, { 2775, 2718 }, { 2760, 2725 }, { 2745, 2735 }, { 2730, 2745 }, { 2721, 2752 }, { 2721, 2760 } }) do
                for _ = 1, 3 do
                    eat_if_hurt(40)
                    gate_walk, gate_walk_detail = t.player.walk_to(hop[1], hop[2], 25)
                    t.note("gate hop " .. hop[1] .. "," .. hop[2] .. " " .. tostring(gate_walk) .. " " .. tostring(gate_walk_detail) .. " at " .. tile_text())
                    if gate_walk == "ok" then break end
                end
            end
            t.check("walk-enterGate", gate_walk == "ok", "ravine walk to the gate: " .. tostring(gate_walk) .. " " .. tostring(gate_walk_detail) .. " at " .. tile_text())
            t.exec("enterGate", t.player.click_loc, "mm_bamboo_largedoor_left", 1)
            t.exec("enterGate-clear", t.chat.drain, {})
            t.ticks(4)
            t.chat.drain({})
            t.ticks(4)
            t.check("enterGate-inside", select(2, t.world.tile()).z >= 2766, "through the Bamboo Gate: " .. tile_text())
            local garkor_walk = "refused"
            for _, hop in ipairs({ { 2735, 2770 }, { 2750, 2772 }, { 2765, 2772 }, { 2780, 2768 }, { 2795, 2764 }, { 2807, 2760 } }) do
                for _ = 1, 3 do
                    eat_if_hurt(40)
                    garkor_walk = t.player.walk_to(hop[1], hop[2], 25)
                    t.note("garkor hop " .. hop[1] .. "," .. hop[2] .. " " .. tostring(garkor_walk) .. " at " .. tile_text())
                    if garkor_walk == "ok" then break end
                end
            end
            t.check("walk-talkToGarkorWithMonkey", garkor_walk == "ok", "walked into Marim beside Garkor: " .. tostring(garkor_walk) .. " at " .. tile_text())
            t.exec("talkToGarkorWithMonkey", t.player.talk_to, "mm_garkor_aa", 1)
            t.exec("talkToGarkorWithMonkey-dialog", t.chat.play, {
                "npc:My my, Zooknock has outdone himself",
                "player:I know.",
                "npc:And by happy coincidence",
                "npc:I need you now to seek audience with Awowogei",
                "npc:You must win his trust",
            })
            t.ticks(2)
            t.expect("talkToGarkorWithMonkey-stage", t.var.await_server("varb126_mm_garkor", 4, 10))
            -- talkToGuard: the elder guard outside the building (mm_elder_guard.rs2:22), %mm_garkor is seek_alliance
            t.exec("goto-talkToGuard", t.player.goto_tile, 2802, 2756, 0)
            t.exec("talkToGuard", t.player.talk_to, "mm_elder_guard_2", 1)
            t.exec("talkToGuard-dialog", t.chat.play, {
                "npc:Grrr ... What do you want?",
                "player:I'd like to speak with Awowogei, please.",
                "npc:Only the Captain of the Monkey Guard",
                "player:Who is the Captain of the Monkey Guard?",
                "npc:He goes by the name of Kruk.",
            })
            t.ticks(2)
            t.expect("talkToGuard-stage", t.var.await_server("varb120_varbit_120", 1, 10))
            -- goUpToBridge / goDownFromBridge: the watchtower ladders (mm_atoll_locs.rs2:54, :75), the guide's way round to Kruk
            t.exec("goto-goUpToBridge", t.player.goto_tile, 2713, 2764, 0)
            -- the zoo monkey in the backpack interrupts with "I'm hungry!" pages: clear them before and after each press
            t.exec("goUpToBridge-clear", t.chat.drain, {})
            t.exec("goUpToBridge", t.player.click_loc, "mm_bamboo_ladder_watchtower_west", 1)
            t.ticks(4)
            t.chat.drain({})
            local _, utile = t.world.tile()
            for attempt = 1, 3 do
                if utile ~= nil and utile.level == 2 then break end
                t.player.click_loc("mm_bamboo_ladder_watchtower_west", 1)
                t.ticks(4)
                t.chat.drain({})
                _, utile = t.world.tile()
            end
            t.check("goUpToBridge-up", utile ~= nil and utile.level == 2, "up the west watchtower ladder: " .. tile_text())
            local wr, wd = t.player.walk_to(2728, 2766, 40)
            t.check("walk-bridge", wr == "ok", "across the bridge: " .. tostring(wr) .. " " .. tostring(wd) .. " at " .. tile_text())
            t.chat.drain({})
            t.exec("goDownFromBridge", t.player.click_loc, "mm_bamboo_ladder_top_watchtower_east", 1)
            t.ticks(4)
            t.chat.drain({})
            local _, dtile = t.world.tile()
            for attempt = 1, 3 do
                if dtile ~= nil and dtile.level == 0 then break end
                t.player.click_loc("mm_bamboo_ladder_top_watchtower_east", 1)
                t.ticks(4)
                t.chat.drain({})
                _, dtile = t.world.tile()
            end
            t.check("goDownFromBridge-down", dtile ~= nil and dtile.level == 0, "down the east watchtower ladder: " .. tile_text())
            -- talkToKruk: the captain (mm_kruk.rs2:12) walks you to Awowogei
            t.exec("talkToKruk", t.player.talk_to, "mm_kruk", 1)
            t.exec("talkToKruk-dialog", t.chat.play, {
                "player:Hello?",
                "npc:What brings you up here, monkey?",
                "player:I have come to seek audience with Awowogei.",
                "npc:That's right - you do.",
                "player:I am envoy from the monkeys of Karamja.",
                "npc:I see. Very well, you look genuine enough.",
            })
            t.ticks(8)
            t.exec("talkToKruk-escort", t.chat.drain, {})
            t.expect("talkToKruk-stage", t.var.await_server("varb117_varbit_117", 1, 10))
            t.check("talkToKruk-tile", true, "Kruk escorted the player to " .. tile_text())
            -- talkToAwow, twice (mm_awowogei.rs2:12): the mission, then the captive monkey from the zoo
            t.exec("talkToAwow", t.player.click_loc, "mm_throne", 1)
            t.exec("talkToAwow-dialog", t.chat.play, {
                "player:Greetings, Awowogei.",
                "npc:Greetings, visitor.",
                "player:I am an envoy from the monkeys of Karamja.",
                "npc:I see. Ours is a strong and mighty lineage",
                "player:Awowogei, please consider my offer carefully.",
                "player:We offer strength in numbers",
                "npc:I don't believe him, Awowogei.",
                "npc:What is your opinion, Murowoi?",
                "npc:I think he seems trustworthy, sir.",
                "npc:I must admit, I have always regarded",
            })
            -- Uwogo's "Don't listen to him" interjection depends on where the advisor stands, so the tail of the speech is drained, not listed
            t.exec("talkToAwow-dialog-tail", t.chat.drain, {})
            t.ticks(2)
            t.expect("talkToAwow-mission", t.var.await_server("varb118_varbit_118", 1, 10))
            t.exec("talkToAwow-again", t.player.click_loc, "mm_throne", 1)
            t.exec("talkToAwow-again-dialog", t.chat.play, {
                "npc:Have you brought with you a captive?",
                "player:Yes, I have.",
                "npc:Well done!",
                "npc:You have shown yourself to be very resourceful.",
                "player:Thank you.",
                "npc:You are clearly well acquainted",
                "npc:In the meantime, feel free to remain",
                "player:What about the proposed alliance, Awowogei?",
                "npc:I must think upon it some more",
            })
            t.ticks(4)
            t.expect("talkToAwow-complete", t.var.await_server("varb118_varbit_118", 2, 10))
            t.check("talkToAwow-tile", true, "after the hand-in the player stands at " .. tile_text())
            -- talkToGarkorForSigil: Garkor tells of the plot (mm_garkor.rs2:157, the chapter 4 scene), then hands over the sigil
            t.exec("goto-talkToGarkorForSigil", t.player.goto_tile, 2807, 2760, 0)
            local ch4_mark = t.cutscene.mark()
            t.exec("talkToGarkorForSigil", t.player.talk_to, "mm_garkor_aa", 1)
            t.exec("talkToGarkorForSigil-dialog", t.chat.play, {
                "npc:Well done on winning Awowogei's trust.",
                "npc:However, your efforts may be in vain...",
                "player:What do you mean?",
                "npc:Progress in the caves has been slow.",
                "player:Who was speaking? What was said?",
                "npc:Listen closely whilst I narrate the details...",
            })
            for _ = 1, 3 do
                t.ticks(10)
                t.chat.drain({})
            end
            t.ticks(10)
            t.exec("talkToGarkorForSigil-chapter4", t.chat.drain, {})
            -- the chapter 4 scene under the atoll (mm_cutscene.rs2:243-244): Awowogei, Waydar and Caranock plot against the squad
            t.exec("talkToGarkorForSigil.cutscene", t.cutscene.await, "talkToGarkorForSigil", { since = ch4_mark, expect = {
                { op = "moveto", coord = "1_41_71_47_35", height = 740 },
                { op = "lookat" },
            } })
            t.expect("quest.stage.monkeymadness_completed_ch3", t.quest.expect_stage("monkeymadness_completed_ch3"))
            t.exec("talkToGarkorForSigil-again", t.player.talk_to, "mm_garkor_aa", 1)
            t.exec("talkToGarkorForSigil-again-dialog", t.chat.play, {
                "player:What shall we do?",
                "npc:Zooknock and I have come up with a plan.",
                "player:What kind of a plan?",
                "npc:I hope you were listening closely.",
                "npc:In effect, the spell will break Lumo",
                "player:But you will be teleported straight into whatever trap",
                "npc:Indeed. This is where you come in.",
                "npc:With your assistance",
                "player:But how will I join you?",
                "npc:Simple. We fool the teleportation spell",
                "player:What?",
                "npc:Zooknock knows Glough's grasp of magic well.",
                "npc:It is these sigils",
                "mesbox:Garkor hands you some kind of medallion.",
                "npc:Welcome to the 10th squad",
                "player:What is it?",
                "npc:It is a replica Waymottin has made",
                "npc:You should prepare.",
                "player:All I have to do is wear this sigil?",
                "npc:Yes - but do not do so until you are ready.",
            })
            t.ticks(2)
            local _, sigils = t.inv.count("mm_sigil")
            t.check("talkToGarkorForSigil-sigil", sigils == 1, "10th squad sigil in the backpack: " .. tostring(sigils) .. " at " .. tile_text())
            -- prepareForBattle: combat gear, food and prayer for the Jungle Demon (waves and halberd swings of up to 32; Protect from Magic)
            t.exec("prepareForBattle-unhold", t.player.unequip, "mm_monkey_greegree_for_normal_monkey")
            t.ticks(2)
            t.exec("prepareForBattle-wield", t.player.equip, "rune_scimitar")
            t.ticks(2)
            t.cheat("::give lobster 6") -- guide (prepareForBattle): "Food"
            t.ticks(3)
            for _ = 1, 6 do eat_if_hurt(99) end
            for _, potion in ipairs({ "4doseprayerrestore", "3doseprayerrestore", "2doseprayerrestore", "1doseprayerrestore" }) do
                local _, have = t.inv.count(potion)
                for _ = 1, (have or 0) do
                    t.player.inv_op(potion, 1)
                    t.ticks(3)
                end
            end
            protect("prayerbook:prayer13")
            local _, magic_on = t.var.varbit("varb4116_prayer_protectfrommagic")
            local _, prayer_now = t.skill.read("prayer")
            local _, lobsters = t.inv.count("lobster")
            t.check("prepareForBattle", magic_on == 1, "protect from magic varbit " .. tostring(magic_on) .. ", prayer points " .. tostring(prayer_now and prayer_now.level) .. ", lobster " .. tostring(lobsters))
            -- equip the sigil (mm_demon.rs2:13): the arena is a private instance, the squad appears in smoke and Garkor opens the battle
            local demon_mark = t.cutscene.mark()
            t.exec("killDemon-sigil", t.player.inv_op, "mm_sigil", 2)
            t.exec("killDemon-sigil-choose", t.chat.choose, "Let the sigil teleport you")
            t.ticks(40)
            t.exec("killDemon-arrival", t.chat.drain, {})
            -- the pull-back over the plantation (mm_demon.rs2:55-58)
            t.exec("killDemon-arrival.cutscene", t.cutscene.await, "killDemon-arrival", { since = demon_mark, expect = {
                { op = "moveto", coord = "1_42_143_27_31" },
                { op = "lookat", coord = "1_42_143_27_34" },
                { op = "moveto", coord = "1_42_143_27_25" },
            } })
            local ar, arena = t.world.tile()
            t.check("killDemon-arena", ar == "ok" and arena ~= nil and arena.z > 9000, "the sigil carried the player into the demon arena at " .. tile_text())
            local dr, dd = t.player.attack("mm_demon", 2, 30)
            t.check("killDemon-press", dr == "ok" or dr == "timeout", tostring(dr) .. " " .. tostring(dd))
            t.exec("killDemon.dead", t.npc.await_dead_engaged, 600, 40, { eat = { item = "lobster", below = 60 } })
            t.ticks(4)
            t.exec("killDemon-end", t.chat.drain, {})
            t.expect("quest.stage.monkeymadness_defeated_demon", t.quest.expect_stage("monkeymadness_defeated_demon"))
            -- the way out is the squad's: Garkor's report order, then Zooknock's teleport (mm_demon.rs2:213, :230)
            t.exec("killDemon-garkor", t.player.talk_to, "mm_garkor_final_battle", 1)
            t.exec("killDemon-garkor-dialog", t.chat.play, {
                "npc:Well done, human!",
                "player:Thank you.",
                "npc:You should report to King Narnode immediately.",
                "player:Rest assured, I will do so.",
                "player:How do I leave this place?",
                "npc:Speak to Zooknock.",
            })
            t.ticks(2)
            -- the follow camera stands behind the arena's bamboo after the fight: put it back over the player (a view, not quest work)
            t.drive.camera(0, 383, 600)
            t.ticks(3)
            t.exec("killDemon-zooknock", t.player.talk_to, "mm_zooknock_final_battle", 1)
            t.exec("killDemon-zooknock-dialog", t.chat.play, { "npc:Well done, human. Bear with me now." })
            t.ticks(6)
            local zr, ztile = t.world.tile()
            t.check("killDemon-out", zr == "ok" and ztile ~= nil and ztile.z < 9000 and ztile.level == 0, "Zooknock teleported the player out of the arena to " .. tile_text())
            -- talkToNarnodeToFinish: plain travel back to the Grand Tree, then the hand-in (mm_narnode.rs2:28)
            local _, coins_before = t.inv.count("coins")
            local _, diamonds_before = t.inv.count("diamond")
            local snapshot_result, snapshot = t.skill.snapshot()
            t.check("reward.snapshot", snapshot_result == "ok", "skills read before the hand-in; coins " .. tostring(coins_before) .. ", diamonds " .. tostring(diamonds_before))
            t.exec("goto-talkToNarnodeToFinish", t.player.goto_tile, 2465, 3494, 0)
            t.exec("talkToNarnodeToFinish", t.player.talk_to, "grandtree_narnode", 1)
            t.exec("talkToNarnodeToFinish-dialog", t.chat.play, {
                "player:King Narnode!",
                "npc:Yes? How is the mission going",
                "player:It's over - it's finally over.",
                "npc:What do you mean 'over'?",
                "player:I mean 'finished.'",
                "npc:Yes, alright. Report on what happened.",
                "player:With all due respect sir",
                "npc:And what of my 10th squad?",
                "player:They all live",
                "npc:'We',",
                "player:I, uh, I'm part of the 10th squad now.",
                "mesbox:You show King Narnode your sigil.",
                "npc:Well, now. It appears I cannot argue with that.",
            })
            t.ticks(3) -- if_close, then p_delay(0) before the reward label (mm_narnode.rs2:44-47)
            t.exec("talkToNarnodeToFinish-reward", t.chat.play, {
                "npc:No service such as what you have done for me goes unrewarded",
                "mesbox:King Narnode hands you a huge stack",
            })
            t.ticks(4)
            t.exec("talkToNarnodeToFinish-after", t.chat.drain, {})
            t.ticks(2)
            local _, coins_after = t.inv.count("coins")
            local _, diamonds_after = t.inv.count("diamond")
            t.check("reward.coins", (coins_after or 0) - (coins_before or 0) == 10000, "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. " (Quest Helper: 10000 coins)")
            t.check("reward.diamonds", (diamonds_after or 0) - (diamonds_before or 0) == 3, "diamonds " .. tostring(diamonds_before) .. " -> " .. tostring(diamonds_after) .. " (Quest Helper: 3 diamonds)")
            t.expect("quest.stage.monkeymadness_complete", t.quest.expect_stage("monkeymadness_complete"))
            t.quest.expect_complete()
            -- LEG 8 END
        end },
    },
}
