-- Recruitment Drive (Falador -> the Temple Knights' training grounds).
-- Content: OSRS-Content/osrs-content/server/scripts/quests/quest_recruitmentdrive/.
-- Analysis: docs/rsprox-analysis/quests/recruitment_drive.md (RSProx index
-- 4413 and 4414).
--
-- The recorded flow: Sir Amik upstairs in the White Knights' Castle, Sir
-- Tiffy in Falador Park (Testing...? and Organisation...? and walking off,
-- then refused for carrying something, then taken in), then five rooms of
-- seven, each picked at random as the last is passed. The rooms are read off
-- the player's tile on arrival and solved by their own function below. The
-- first room is failed as the recording failed it when it is Sir Ren's (a
-- wrong word), Ms. Hynn's (a wrong answer) or Sir Tinley's (fidgeting), and
-- Sir Tiffy takes the player back in; the fifth door ends in the park with
-- the completion.

local STEP = {
    [1] = { 2489, 4972 }, [2] = { 2459, 4979 }, [3] = { 2456, 4964 }, [4] = { 2472, 4956 },
    [5] = { 2440, 4956 }, [6] = { 2468, 4940 }, [7] = { 2451, 4936 },
}
local ROOM_NAME = { "spishyus", "table", "kuam", "tinley", "ren", "cheevers", "hynn" }
local DOOR = {
    [1] = { "rd_room1_exitdoor", 2472, 4972 }, [2] = { "rd_room2_exitdoor", 2447, 4979 },
    [3] = { "rd_room3_exitdoor", 2463, 4963 }, [4] = { "rd_room4_exitdoor", 2480, 4956 },
    [5] = { "rd_room5_exitdoor", 2446, 4956 }, [6] = { "rd_room6_exitdoor", 2478, 4940 },
    [7] = { "rd_room7_exitdoor", 2452, 4943 },
}
-- recruitmentdrive_ren.rs2 rd_ren_password; [4] = BITE is RSProx 4413 [277..300].
local REN_WORD = { [1] = "MEAT", [2] = "LAST", [3] = "RAIN", [4] = "BITE", [5] = "TIME", [6] = "FISH" }
local REN_CLUE = {
    [1] = "More than words", [2] = "Look closely at the words", [3] = "Rare it is that you will see",
    [4] = "Better than me, you'll not find", [5] = "This riddle of mine may confuse", [6] = "Feel the aching of your mind",
}
-- recruitmentdrive_table.rs2: the place each shuffled order leaves empty, and
-- the statue standing in that place once the order is 0.
local TABLE_PLACE = { [1] = 6, [2] = 5, [3] = 4, [4] = 3, [5] = 2, [6] = 1, [7] = 10, [8] = 11, [9] = 12, [10] = 9, [11] = 8, [12] = 7 }
local TABLE_STATUE = { "rd_1g", "rd_1s", "rd_1b", "rd_2g", "rd_2s", "rd_2b", "rd_3g", "rd_3s", "rd_3b", "rd_4g", "rd_4s", "rd_4b" }

return {
    id = "recruitmentdrive",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_blackknightsfortress",
        "::complete quest_druidicritual",
        -- Sir Leye is a level 20 fighter and the room is fought bare-handed
        -- (owner ruling 2026-10-06: stats staged 25 for margin).
        "::setlevel attack 25",
        "::setlevel strength 25",
        "::setlevel defence 25",
        "::setlevel hitpoints 25",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb657_rd_main",
            constants = { not_started = 0, started = 1, complete = 2 },
            display = "Recruitment Drive",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.ticklog.start()

        local function tile()
            local r, tt = t.world.tile()
            if r ~= "ok" or type(tt) ~= "table" then
                return nil
            end
            return tt
        end
        local function at(x, z)
            local tt = tile()
            return tt ~= nil and tt.x == x and tt.z == z and tt.level == 0
        end
        local function var(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or -1
        end
        local function count(item)
            local r, n = t.inv.count(item)
            return r == "ok" and n or -1
        end
        local function room_here()
            for room, s in pairs(STEP) do
                if at(s[1], s[2]) then
                    return room
                end
            end
            return nil
        end
        -- The arrival step lands the player one tile into the room
        -- (recruitmentdrive.rs2 ~rd_room_arrive).
        local function await_room(name)
            t.await({ level = function() return room_here() ~= nil end, note = "arrive in a room" }, 25)
            local room = room_here()
            local tt = tile()
            t.check(name, room ~= nil, "tile " .. (tt and (tt.x .. "," .. tt.z) or "?") .. " -> room "
                .. tostring(room and ROOM_NAME[room]))
            return room
        end

        -- ---- Sir Amik Varze, the White Knights' Castle (4413 [105..139]) ----
        t.exec("goto-castle", t.player.goto_tile, 2968, 3338, 0)
        t.exec("castleDoorIn", t.player.pass_door, { closed = "fai_falador_castledoubledoorl",
            open = "fai_falador_opencastledoubledoorl", at = { 2965, 3338, 0 }, near = { 2966, 3338 },
            far = { 2962, 3338 }, far_ok = function(tt) return tt.x <= 2964 end,
            far_desc = "inside the west keep, x <= 2964" })
        t.exec("climbBottomSteps", t.player.climb, { loc = "fai_falador_castle_spiralstairs", op = 1,
            op_name = "Climb-up", at = { 2954, 3338, 0 }, dest = { 2956, 3338, 1 } })
        t.exec("climbSecondSteps", t.player.climb, { loc = "fai_falador_castle_spiralstairs", op = 1,
            op_name = "Climb-up", at = { 2960, 3338, 1 }, dest = { 2959, 3339, 2 } })
        t.exec("talkToSirAmikVarze", t.player.talk_to, "sir_amik_varze", 1)
        t.exec("talkToSirAmikVarze-dialog", t.chat.play, {
            "player:Hello Sir Amik.",
            "npc:Hello, friend!",
            "player:Do you have any other quests for me to do?",
            "npc:Quests, eh?",
            "npc:Your excellent work sorting out those Black Knights",
            "npc:Would you like me to put your name forwards to",
            "choose:Yes.",
            "player:Sure thing Sir Amik, sign me up!",
            "npc:Erm, well, this is a little embarrassing",
            "npc:They are called the Temple Knights",
            "player:Okey dokey, I'll go do that then.",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        t.exec("climbDownSecondFloorStaircase", t.player.climb, { loc = "fai_falador_castle_spiralstairstop",
            op = 1, op_name = "Climb-down", at = { 2960, 3339, 2 }, dest = { 2960, 3340, 1 } })
        t.exec("climbDownfirstFloorStaircase", t.player.climb, { loc = "fai_falador_castle_spiralstairstop",
            op = 1, op_name = "Climb-down", at = { 2955, 3338, 1 }, dest = { 2955, 3337, 0 } })
        t.exec("castleDoorOut", t.player.pass_door, { closed = "fai_falador_castledoubledoorl",
            open = "fai_falador_opencastledoubledoorl", at = { 2965, 3338, 0 }, near = { 2963, 3338 },
            far = { 2968, 3338 }, far_ok = function(tt) return tt.x >= 2965 end,
            far_desc = "back in the open courtyard, x >= 2965" })
        t.exec("goto-talkToSirTiffy", t.player.goto_tile, 2996, 3375, 0)

        -- ---- Sir Tiffy Cashien (4413 [176..277]) ----
        local GREETING = {
            "player:Sir Amik Varze sent me to meet you here for some",
            "npc:Amik told me all about you, dontchaknow!",
            "player:...thanks, I think.",
            "npc:Well, a top-notch adventurer like yourself",
            "npc:So, are you ready to begin testing?",
        }
        local function with(list, more)
            local out = {}
            for _, v in ipairs(list) do table.insert(out, v) end
            for _, v in ipairs(more) do table.insert(out, v) end
            return out
        end
        -- First: Testing...? then Organisation...?, and walked off at the
        -- third menu ([184..220]).
        t.exec("talkToSirTiffy", t.player.talk_to, "rd_teleporter_guy", 1)
        t.exec("talkToSirTiffy-dialog", t.chat.play, with(GREETING, {
            "choose:Testing...?",
            "player:...testing?",
            "npc:Jolly bad show!",
            "npc:Well, not your fault I suppose, what?",
            "player:So...",
            "npc:Not at all, old bean.",
            "npc:That's really not the kind of thing",
            "player:So you want me to go and fetch you some kind of",
            "player:Because I really hate doing that!",
            "npc:Haw haw haw",
            "npc:I know what you mean though",
            "player:So what exactly will this test consist of?",
            "npc:Can't let just any old riff-raff in, what?",
            "player:So you want to test my brains?",
            "npc:Haw Haw Haw",
            "npc:Not in the slightest...",
            "player:What do you want me to do then?",
            "npc:It's a test of wits, what?",
            "npc:adventurer.",
            "npc:Standard puzzle room rules will apply.",
            "player:Erm... what are 'standard  puzzle room rules' exactly?",
            "npc:Never done this sort of thing before, what?",
            "npc:The simple rules are:",
            "npc:Of course, if you quit a room",
            "npc:Our organisation manages to filter",
            "choose:Organisation...?",
            "player:This organisation you keep mentioning...",
            "npc:Oh, that Amik!",
            "player:No...",
            "npc:Well now old sport",
            "npc:I represent the Temple Knights.",
            "npc:centuries ago, and we answer only to him.",
            "npc:Only the very best of the best",
            "npc:You might say that we are the front line",
            "player:So what's the difference between you and the White",
            "npc:Well, in simple terms, we're better!",
            "npc:The Temple Knights on the other hand",
            "npc:Saradomin himself!",
            "player:Cool, you mean I get to meet Saradomin?",
            "npc:Well, uh, not exactly...",
            "npc:Saradomin's implicit permission.",
            "npc:It's not every job where you have more authority",
            "player:Wait...",
            "npc:Well, not me personally",
            "npc:Plus a few others, so I hear....",
            "npc:Anyway, this is why we keep our organisation shrouded",
            "npc:testing?",
            "options",
        }))
        t.chat.close()
        t.ticks(2)

        -- Carrying something: refused ([236..238]).
        t.cheat("::give coins 1", false)
        t.ticks(2)
        t.exec("talkToSirTiffy-carrying", t.player.talk_to, "rd_teleporter_guy", 1)
        t.exec("talkToSirTiffy-carrying-dialog", t.chat.play, with(GREETING, {
            "choose:Yes, let's go!",
            "player:Yeah, this sounds right up my street.",
            "npc:Well, bad luck old",
            "npc:Don't want people cheating by smuggling stuff in, what?",
            "npc:Come and see me again after you've been to the old",
        }))
        t.check("tiffy.refused.stage", var("varb657_rd_main") == 1 and var("varb692_rd_spoke_to_tiffy") == 0,
            "rd_main " .. var("varb657_rd_main") .. " (want 1), rd_spoke_to_tiffy " .. var("varb692_rd_spoke_to_tiffy") .. " (want 0)")
        t.cheat("::clearinv", false)
        t.ticks(2)

        -- Taken in ([258..277]): three pages with nothing to click, then the room.
        local function taken_in(name, first)
            t.exec(name, t.player.talk_to, "rd_teleporter_guy", 1)
            local pages
            if first then
                pages = with(GREETING, { "choose:Yes, let's go!" })
            else
                pages = { "npc:Ah, what ho!", "choose:Yes, let's go!" }
            end
            table.insert(pages, "player:Yeah, this sounds right up my street.")
            table.insert(pages, "npc:Jolly good show!")
            t.exec(name .. "-dialog", t.chat.play, pages)
            return await_room(name .. ".room")
        end
        local room = taken_in("talkToSirTiffy-go", true)
        t.check("tiffy.spoke_to_tiffy", var("varb692_rd_spoke_to_tiffy") == 1,
            "rd_spoke_to_tiffy " .. var("varb692_rd_spoke_to_tiffy") .. " (want 1)")

        -- ---- The rooms ----
        local function exit_door(name, room_id)
            local d = DOOR[room_id]
            t.exec(name, t.player.click_loc, d[1], 1, { at = { d[2], d[3], 0 } })
        end

        -- The black comes down on the fail page's click; the move to the park
        -- is three ticks on and Sir Tiffy's page a tick after it.
        local function failed(name)
            t.await({ level = function() return at(2996, 3375) and t.chat.kind() ~= "none" end,
                note = "Sir Tiffy in Falador Park" }, 12)
            t.exec(name .. "-tiffy", t.chat.play, {
                "npc:Oh, jolly bad luck, what?",
                "npc:Well, never mind!",
            })
            t.check(name .. ".park", at(2996, 3375) and var("varb663_rd_room5_complete") == 0,
                "back in Falador Park at 2996,3375 with no room passed")
        end

        local solve = {}

        solve[5] = function(fail)
            t.exec("ren.greeting", t.chat.play, {
                "npc:Greetings friend, and welcome here,",
                "choose:Can I have the clue for the door?",
                "npc:" .. (REN_CLUE[var("varb666_rd_templock_1")] or "?"),
            })
            local word = fail and "AAAA" or (REN_WORD[var("varb666_rd_templock_1")] or "BITE")
            t.exec("ren.door", t.player.click_loc, "rd_room5_exitdoor", 1, { at = { 2446, 4956, 0 } })
            t.expect("ren.lock", t.ui.await_open("rd_combolock", 10))
            local wheels = { "rda", "rdb", "rdc", "rdd" }
            for i = 1, 4 do
                local letter = string.sub(word, i, i)
                local n = string.byte(letter) - string.byte("A")
                local _, right = t.ui.widget("rd_combolock:" .. wheels[i] .. "_right")
                local _, left = t.ui.widget("rd_combolock:" .. wheels[i] .. "_left")
                if n <= 13 then
                    for _ = 1, n do t.ui.invoke(right, 1) end
                else
                    for _ = 1, 26 - n do t.ui.invoke(left, 1) end
                end
                t.expect("ren.wheel" .. i, t.ui.expect_text("rd_combolock:" .. wheels[i], "/^" .. letter .. "$/", 5))
            end
            local _, enter = t.ui.widget("rd_combolock:rdenter")
            t.ui.invoke(enter, 1)
            t.ticks(2)
            if fail then
                t.exec("ren.fail", t.chat.play, { "npc:It's sad to say, this test beat you." })
                failed("ren.fail")
                return "fail"
            end
            t.exec("ren.pass", t.chat.play, { "npc:Your wit is sharp, your brains quite clear;" })
            t.check("ren.room5_complete", var("varb663_rd_room5_complete") == 1, "rd_room5_complete")
            return "pass"
        end

        solve[7] = function(fail)
            local riddle = var("varb667_rd_templock_2")
            local pages = { "npc:Greetings, " }
            local count_answer = nil
            if riddle == 3 then
                for _, p in ipairs({ "npc:Here is my riddle:", "npc:Your first choice", "npc:Your second choice",
                    "npc:Your third choice", "npc:Your final choice of fate", "npc:Which fate would you be wise to choose?" }) do
                    table.insert(pages, p)
                end
                table.insert(pages, fail and "choose:The large fire." or "choose:The wolves.")
            elseif riddle == 4 then
                for _, p in ipairs({ "npc:Here is my riddle: I dropped four", "npc:The first bucket's water", "npc:Which bucket's stone" }) do
                    table.insert(pages, p)
                end
                table.insert(pages, fail and "choose:Bucket D (35 degrees)" or "choose:Bucket A (32 degrees)")
            elseif riddle == 5 then
                table.insert(pages, "npc:Here is my riddle: I estimate")
                table.insert(pages, "npc:What would be the number")
                count_answer = fail and 5 or 0
            elseif riddle == 1 then
                for _, p in ipairs({ "npc:Here is my riddle: I have both", "npc:My husband is four times", "npc:How old is my daughter now?" }) do
                    table.insert(pages, p)
                end
                count_answer = fail and 20 or 10
            else
                table.insert(pages, "npc:Here is my riddle: Which of the following")
                table.insert(pages, fail and "choose:The number of false statements here is one" or "choose:The number of false statements here is three.")
            end
            if count_answer ~= nil then
                -- "Enter amount:" is clientscript 108, which scriptrun never
                -- runs (its meslayer_mode is always 0): there the room is
                -- given up through its entrance portal instead.
                t.exec("hynn.riddle", t.chat.play, pages)
                t.ticks(1)
                local cr, cd = t.chat.count(count_answer)
                if cr == "ok" then
                    -- Live, after an npc page the client has been seen to
                    -- send no RESUME_P_COUNTDIALOG at all (2026-10-29, open
                    -- in docs/rsprox-analysis/quests/recruitment_drive.md).
                    t.await({ level = function() return var("varb665_rd_room7_complete") == 1
                        or (fail and t.chat.kind() ~= "none") end, note = "the answer" }, 6)
                    if var("varb665_rd_room7_complete") ~= 1 and not fail then
                        cr, cd = "unsupported", "entered " .. count_answer .. " and the server never heard it"
                    end
                end
                if cr == "unsupported" then
                    t.step("hynn.count", "BLOCKED", tostring(cd) .. " -- quitting through rd_portal_room7_entrance")
                    t.exec("hynn.quit", t.player.click_loc, "rd_portal_room7_entrance", 1)
                    t.ticks(4)
                    failed("hynn.quit")
                    return "fail"
                end
                t.step("hynn.count", cr == "ok" and "PASS" or "FAIL", tostring(cd))
                pages = {}
            end
            if fail then
                table.insert(pages, "npc:No... I am very sorry.")
                t.exec("hynn.fail", t.chat.play, pages)
                failed("hynn.fail")
                return "fail"
            end
            table.insert(pages, "npc:Excellent work, ")
            t.exec("hynn.answer", t.chat.play, pages)
            t.check("hynn.room7_complete", var("varb665_rd_room7_complete") == 1, "rd_room7_complete (riddle " .. riddle .. ")")
            return "pass"
        end

        solve[4] = function(fail)
            t.exec("tinley.greeting", t.chat.play, { "npc:you have arrived." })
            t.exec("tinley.talk", t.player.talk_to, "rd_observer_room_4", 1)
            t.exec("tinley.patience", t.chat.play, { "npc:I have but one clue for you" })
            if fail then
                t.ticks(4)
                t.exec("tinley.fidget", t.player.talk_to, "rd_observer_room_4", 1)
                t.exec("tinley.fail", t.chat.play, { "npc:No... I am very sorry." })
                failed("tinley.fail")
                return "fail"
            end
            t.ticks(17)
            t.exec("tinley.pass", t.chat.play, { "npc:Excellent work, " })
            t.check("tinley.room4_complete", var("varb662_rd_room4_complete") == 1, "rd_room4_complete")
            return "pass"
        end

        solve[3] = function()
            t.exec("kuam.greeting", t.chat.play, { "npc:you're finally here.", "npc:If you are having problems" })
            t.ticks(2)
            local passed_before = 0
            for r = 1, 7 do
                if var("varb" .. (658 + r) .. "_rd_room" .. r .. "_complete") == 1 then
                    passed_before = passed_before + 1
                end
            end
            -- A restart (every room passed is cleared) can bring the player
            -- back to Sir Leye on the hitpoints the last fight left; nothing
            -- in the rooms heals, so the staged 25 is staged again.
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level < 20 then
                t.cheat("::setlevel hitpoints 25", false)
                t.ticks(1)
            end
            -- The warhammer, the one weapon that is not a blade (OSRS wiki).
            if count("steel_warhammer") < 1 then
                local r = t.player.click_obj("steel_warhammer", 3)
                t.step("kuam.take_warhammer", r == "ok" and "PASS" or "FAIL", "click_obj steel_warhammer -> " .. tostring(r))
                t.ticks(3)
            end
            t.exec("kuam.wield", t.player.equip, "steel_warhammer")
            t.exec("kuam.attack", t.player.attack, "rd_combat_npc_room_3", 2, 20)
            t.exec("kuam.leye_dead", t.npc.await_dead_engaged, 300, 30)
            local lines
            if passed_before >= 4 then
                lines = { "npc:Excellent work! You have passed all five" }
            elseif passed_before == 3 then
                lines = { "npc:Well done, that was your fourth test", "npc:Please step through the portal to your final test" }
            else
                lines = { "npc:Excellent work, " }
            end
            t.exec("kuam.won", t.chat.play, lines)
            t.check("kuam.room3_complete", var("varb661_rd_room3_complete") == 1, "rd_room3_complete")
            return "pass"
        end

        solve[2] = function()
            t.exec("table.greeting", t.chat.play, {
                "npc:This room will test your observation skills.",
                "npc:Study the statues closely.",
                "npc:We will also mix the order up",
                "npc:You have 10 seconds to memorise",
            })
            local order = var("varb658_rd_room_order")
            local place = TABLE_PLACE[order]
            t.check("table.order", place ~= nil, "rd_room_order " .. order .. " (want 1..12)")
            t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the lights" }, 25)
            t.exec("table.lights", t.chat.play, {
                "npc:We will now dim the lights",
                "npc:Please touch the statue you think has been added.",
            })
            t.exec("table.touch", t.player.click_loc, TABLE_STATUE[place or 1], 1)
            t.ticks(2)
            t.exec("table.pass", t.chat.play, { "npc:Excellent work, " })
            t.check("table.room2_complete", var("varb660_rd_room2_complete") == 1, "rd_room2_complete")
            return "pass"
        end

        solve[1] = function()
            -- rd_room1_intro_chat stays set once heard (4414 [0]): a second
            -- visit is greeted with the exchange recorded at 4414 [5..20].
            if var("varb691_rd_room1_intro_chat") == 1 then
                t.exec("spishyus.greeting-again", t.chat.play, {
                    "npc:Ah, welcome",
                    "player:Listen, this is really annoying me.",
                    "npc:I am afraid not",
                    "player:No, but seriously, please?",
                    "npc:I am truly sorry, but my hands are tied.",
                    "npc:This is the only help I may give you...",
                    "player:Well, thanks I guess...",
                })
            else
                t.exec("spishyus.greeting", t.chat.play, {
                "npc:Ah, welcome",
                "player:Hello there. What am I supposed to be doing in this room?",
                "npc:Well, your task is to take this fox",
                "npc:When you have done that",
                "player:Is that it?",
                "npc:Well, it is not quite as simple",
                "npc:Firstly, you may only carry one",
                "npc:Secondly, the fox wants to eat the chicken",
                "npc:one of them will be eaten",
                "player:Okay, I'll see what I can do.",
                })
            end
            t.ticks(2)
            t.drive.camera(1024, 383, 400)
            -- The bridge's ends stand on raw level 1 (a deck); the walk across
            -- is eight steps, a tile a tick, from the op.
            local function cross(name, west)
                local sym, at_tile, dest = "rd_bridge_left", { 2483, 4972, 1 }, 2476
                if not west then
                    sym, at_tile, dest = "rd_bridge_right", { 2477, 4972, 1 }, 2484
                end
                t.exec(name, t.player.click_loc, sym, 1, { at = at_tile })
                t.await({ level = function() return at(dest, 4972) end, note = sym .. " -> " .. dest }, 16)
                t.check(name .. ".landed", at(dest, 4972), "on " .. dest .. ",4972")
                t.ticks(2)
            end
            -- Picked up, an animal goes into a worn slot, not the pack, so
            -- the click's own settle never sees it land: graded on its varbit.
            local function take(name, loc, varbit, want)
                t.player.click_loc(loc, 1)
                t.await({ level = function() return var(varbit) == want end, note = loc }, 20)
                t.check(name, var(varbit) == want, varbit .. " " .. var(varbit) .. " (want " .. want .. ")")
                t.ticks(1)
            end
            local function put(name, item)
                local r = t.player._worn_press(item, 1)
                t.step(name, r == "ok" and "PASS" or "FAIL", "worn press " .. item .. " -> " .. tostring(r))
                t.ticks(2)
            end
            take("spishyus.chicken1", "rd_room2_chicken_multi", "varb682_rd_chickleft", 1)
            t.check("spishyus.chicken_worn", count("rd_chicken") == 0, "rd_chicken in the pack " .. count("rd_chicken") .. " (want 0: worn in the left hand)")
            cross("spishyus.cross1", true)
            put("spishyus.put_chicken1", "rd_chicken")
            cross("spishyus.cross2", false)
            take("spishyus.fox", "rd_room2_fox_multi", "varb680_rd_foxleft", 1)
            cross("spishyus.cross3", true)
            put("spishyus.put_fox", "rd_fox")
            take("spishyus.chicken2", "rd_room2_chicken_multi_right", "varb683_rd_chickright", 0)
            cross("spishyus.cross4", false)
            put("spishyus.put_chicken2", "rd_chicken")
            take("spishyus.grain", "rd_room2_grain_multi", "varb684_rd_grainleft", 1)
            cross("spishyus.cross5", true)
            put("spishyus.put_grain", "rd_sack")
            cross("spishyus.cross6", false)
            take("spishyus.chicken3", "rd_room2_chicken_multi", "varb682_rd_chickleft", 1)
            cross("spishyus.cross7", true)
            put("spishyus.put_chicken3", "rd_chicken")
            t.drive.camera(0, 128, 600)
            t.check("spishyus.room1_complete", var("varb659_rd_room1_complete") == 1,
                "rd_room1_complete; fox/chick/grain right " .. var("varb681_rd_foxright") .. "/"
                .. var("varb683_rd_chickright") .. "/" .. var("varb685_rd_grainright"))
            return "pass"
        end

        solve[6] = function()
            t.exec("cheevers.greeting", t.chat.play, {
                "npc:Welcome to my challenge.",
                "npc:All you need to do is leave from the opposite door",
                "npc:I will warn you that this is more complicated",
                "npc:I should also warn you",
                "npc:Best of luck!",
            })
            t.exec("cheevers.talk", t.player.talk_to, "rd_observer_room_6", 1)
            t.exec("cheevers.talk-dialog", t.chat.play, {
                "player:Can you give me any help?",
                "npc:No, I am sorry, but that is forbidden by our rules.",
                "npc:If you are having a particularly tough time",
                "npc:Sometimes a break from concentration",
                "player:Okay, thanks!",
            })
            local function search(name, loc, item, at_tile)
                t.exec(name, t.player.click_loc, loc, 1, at_tile and { at = at_tile } or nil)
                t.ticks(4)
                t.check(name .. ".has", t.inv.expect_has(item, 1))
            end
            local function shelf(name, loc, pages, item, n)
                t.exec(name, t.player.click_loc, loc, 1)
                t.exec(name .. "-dialog", t.chat.play, pages)
                t.ticks(2)
                t.check(name .. ".has", t.inv.expect_has(item, n or 1))
            end
            search("cheevers.magnet", "rd_bookshelf_old_tall", "rd_magnet")
            shelf("cheevers.vials1", "rd_shelves_chemicals_1", { "mesbox:There are two vials on this shelf.", "choose:Take both vials." }, "rd_acetic_acid")
            shelf("cheevers.cupric", "rd_shelves_chemicals_2", { "mesbox:There is a vial on this shelf.", "choose:YES" }, "rd_cupric_sulphate")
            shelf("cheevers.gypsum", "rd_shelves_chemicals_3", { "mesbox:There is a vial on this shelf.", "choose:YES" }, "rd_gypsum")
            shelf("cheevers.salt", "rd_shelves_chemicals_4", { "mesbox:There is a vial on this shelf.", "choose:YES" }, "rd_sodium_chloride")
            search("cheevers.wire", "rd_small_crates", "rd_wire", { 2475, 4943, 0 })
            search("cheevers.tin", "rd_large_crate", "rd_tin", { 2476, 4943, 0 })
            t.exec("cheevers.chest", t.player.click_loc, "rd_chest_closed", 1)
            t.ticks(3)
            search("cheevers.shears", "rd_chest_open", "rd_shears")
            search("cheevers.chisel", "rd_large_crates", "rd_chisel", { 2476, 4937, 0 })
            t.drive.camera(1024, 383, 400)
            shelf("cheevers.n2o", "rd_shelves_chemicals_5", { "mesbox:There is a vial on this shelf.", "choose:YES" }, "rd_nitorus_oxide")
            shelf("cheevers.tinore", "rd_shelves_chemicals_6", { "mesbox:There is a vial on this shelf.", "choose:YES" }, "rd_tin_ore_powder")
            shelf("cheevers.copore", "rd_shelves_chemicals_7", { "mesbox:There is a vial on this shelf.", "choose:YES" }, "rd_copper_ore_powder")
            shelf("cheevers.water1", "rd_shelves_chemicals_8", { "mesbox:There are three vials on this shelf.", "choose:Take one vial" }, "rd_dihydrogen_monoxide", 2)
            shelf("cheevers.water2", "rd_shelves_chemicals_8", { "mesbox:There are two vials on this shelf.", "choose:Take both vials" }, "rd_dihydrogen_monoxide", 4)
            search("cheevers.knife", "rd_bookshelf_old_tall3", "rd_knife")
            t.drive.camera(0, 128, 600)
            local spade = t.player.click_obj("rd_metal_spade", 3)
            t.step("cheevers.spade", spade == "ok" and "PASS" or "FAIL", "click_obj rd_metal_spade -> " .. tostring(spade))
            t.ticks(2)
            t.check("cheevers.spade.has", t.inv.expect_has("rd_metal_spade", 1))

            local bunsen = t.player.by_symbol("loc", "rd_wooden_table_bunsen_burner")
            local door = t.player.by_symbol("loc", "rd_stone_door")
            local key = t.player.by_symbol("loc", "rd_key_chained")
            t.exec("cheevers.burn", t.player.use_on, "rd_metal_spade", bunsen)
            t.ticks(2)
            t.check("cheevers.burn.head", count("rd_metal_spade_no_handle") == 1, "rd_metal_spade_no_handle " .. count("rd_metal_spade_no_handle"))
            t.exec("cheevers.wedge", t.player.use_on, "rd_metal_spade_no_handle", door)
            t.ticks(2)
            t.exec("cheevers.cupric_on_door", t.player.use_on, "rd_cupric_sulphate", door)
            t.ticks(2)
            t.exec("cheevers.water_on_door", t.player.use_on, "rd_dihydrogen_monoxide", door)
            t.ticks(2)
            t.check("cheevers.door.state2", var("varb686_rd_room6_stone_door") == 2, "rd_room6_stone_door " .. var("varb686_rd_room6_stone_door"))
            t.exec("cheevers.pull", t.player.click_loc, "rd_stone_door", 1)
            t.ticks(3)
            t.check("cheevers.door.open", var("varb686_rd_room6_stone_door") == 3, "rd_room6_stone_door " .. var("varb686_rd_room6_stone_door"))
            t.exec("cheevers.water_in_tin", t.player.use_item_on_item, "rd_dihydrogen_monoxide", "rd_tin")
            t.ticks(2)
            t.exec("cheevers.gypsum_in_tin", t.player.use_item_on_item, "rd_gypsum", "rd_tin")
            t.ticks(2)
            t.check("cheevers.tinfull", count("rd_tinfull") == 1, "rd_tinfull " .. count("rd_tinfull"))
            t.exec("cheevers.impression", t.player.use_on, "rd_tinfull", key)
            t.ticks(2)
            t.exec("cheevers.copper", t.player.use_item_on_item, "rd_copper_ore_powder", "rd_keymould")
            t.ticks(2)
            t.exec("cheevers.tinpowder", t.player.use_item_on_item, "rd_tin_ore_powder", "rd_full_keymould_copper")
            t.ticks(2)
            t.exec("cheevers.heat", t.player.use_on, "rd_full_keymould_unheated", bunsen)
            t.ticks(2)
            t.exec("cheevers.prise", t.player.use_item_on_item, "rd_knife", "rd_full_keymould_complete")
            t.ticks(2)
            t.check("cheevers.room6_complete", var("varb664_rd_room6_complete") == 1 and count("rd_puzzleroom_key") == 1,
                "rd_room6_complete " .. var("varb664_rd_room6_complete") .. ", key " .. count("rd_puzzleroom_key"))
            t.player.walk_to(2476, 4940, 12)
            -- The walk through is the content's (two forced steps), which the
            -- click's own settle reads as a walk that never answered: graded
            -- on the far tile instead.
            t.player.click_loc("rd_stone_door", 1)
            t.await({ level = function() return at(2478, 4940) end, note = "through the stone door" }, 8)
            t.check("cheevers.through.tile", at(2478, 4940), "on 2478,4940")
            t.ticks(2)
            return "pass"
        end

        -- ---- Five rooms ----
        -- A failure clears every room passed (Sir Tiffy, 4413 [202]).
        local failed_once = false
        local passed = 0
        local entered = 0
        while passed < 5 and room ~= nil and entered < 14 do
            entered = entered + 1
            local fail = (not failed_once) and (room == 4 or room == 5 or room == 7)
            local outcome = solve[room](fail)
            if outcome == "fail" then
                failed_once = true
                passed = 0
                room = taken_in("talkToSirTiffy-again" .. entered, false)
            else
                passed = passed + 1
                exit_door("exit." .. ROOM_NAME[room], room)
                if passed < 5 then
                    room = await_room("exit." .. ROOM_NAME[room] .. ".next")
                end
            end
        end
        t.check("rooms.passed", passed == 5, passed .. " rooms passed (want 5) in " .. entered
            .. " entered; a failure first: " .. tostring(failed_once))

        -- ---- The finish, beside Sir Tiffy (4414 [368..381]) ----
        local _, snapshot = t.skill.snapshot()
        local coins_r, coins_before = t.inv.count("coins")
        t.await({ level = function() return at(2997, 3374) end, note = "Falador Park beside Sir Tiffy" }, 20)
        t.exec("finish-dialog", t.chat.play, {
            "player:I believe I've managed to pass the tests!",
            "npc:Oh, jolly well done!",
        })
        t.ticks(3)
        t.check("reward.prayer_xp", t.skill.expect_gain("prayer", 1000, snapshot))
        t.check("reward.herblore_xp", t.skill.expect_gain("herblore", 1000, snapshot))
        t.check("reward.agility_xp", t.skill.expect_gain("agility", 1000, snapshot))
        local coins_after_r, coins_after = t.inv.count("coins")
        t.check("reward.coins", coins_r == "ok" and coins_after_r == "ok" and coins_after - coins_before == 3000,
            "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after))
        t.check("reward.initiate_sallet", t.inv.expect_has("basic_tk_helm", 1))
        t.quest.expect_complete()
        t.finish(0)
    end,
}
