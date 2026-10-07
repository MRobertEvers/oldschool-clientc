return {
    id = "icthlarin",
    fixture = "fresh_lumbridge.ini",
    -- Prerequisite: Gertrude's Cat (the cat follows you to the Sphinx). Brought along: kitten, supplies for
    -- the Wanderer (given after he asks), bucket + knife + coins + willow logs for the Embalmer and Carpenter.
    -- ::icthlarinenergy tops up run energy for the pit jumps (the pit costs energy).
    -- Travel (door rule): the desert is entered by the Shantay Pass doorway on every trip (a pass bought
    -- from Shantay each time, shantay_pass.rs2:78-117; out from the south is free, :84), and Sophanem's
    -- north gate (sophanem_gate_right 3284,2809, doors_selfstage) is passed on foot both ways. The first
    -- memory goes in by the quest's own rock (icthlarin.rs2 [oploc1,icthal_entrance_open]).
    setup = {
        "::clearinv",
        "::complete quest_gertrudescat",
        "::give kittenobject 1",
        "::setlevel agility 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel magic 99",
        "::give rune_scimitar 1",
        "::wield rune_scimitar",
        "::give lobster 12",
        "::give bucket_empty 2",
        "::give knife 1",
        "::give coins 100",
        "::give willow_logs 1",
        "::icthlarinenergy",
    },
    run = function(t)
        t.quest.bind({
            varp = "varb418_ics_little_var",
            constants = { not_started = 0, need_supplies = 1, entered_city = 2, first_memory = 3, in_pyramid = 4, sphinx = 5,
                high_priest = 6, return_jar = 7, jar_guardian = 8, jar_killed = 11, jar_crossed = 12, place_jar = 13,
                jar_done = 14, embalm = 15, ritual = 16, place_symbol = 17, symbol_placed = 18, ceremony = 19,
                possessed = 20, priest_dead = 23, meet_god = 24, finish_talk = 25, complete = 26 },
            row = "quest_icthlarinslittlehelper", display = "Icthlarin's Little Helper", points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        local presses
        local pos

        local function reading()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then return tt.x .. "," .. tt.z .. "," .. tostring(tt.level) end
            return tostring(r)
        end
        local function count(sym) local r, n = t.inv.count(sym) return r == "ok" and n or 0 end

        -- The Shantay Pass from the north: a pass bought from Shantay (shantay.rs2, 5 coins), then the
        -- doorway's op (shantay_pass.rs2 [oploc1,shantay_pass_henge_doorway]: poster pages and a disclaimer
        -- only the first time, the pass handed over, [queue,shantay_pass_enter] lands 3304,3115).
        local function shantay_in(pfx, first)
            t.exec("goto-" .. pfx .. ".shantay", t.player.goto_tile, 3304, 3123, 0)
            local coins0, pass0 = count("coins"), count("shantay_pass")
            t.exec(pfx .. ".buyPass", t.player.talk_to, "shantay", 1)
            local lines = first and { "npc:Hello effendi, I am Shantay.", "npc:I see you're new." } or { "npc:Hello again friend." }
            for _, l in ipairs({ "choose:I want to buy a shantay pass for 5 gold coins.", "player:I want to buy a shantay pass for",
                "mesbox:You purchase a Shantay Pass." }) do lines[#lines + 1] = l end
            t.exec(pfx .. ".buyPass-dialog", t.chat.play, lines)
            t.inv.await("shantay_pass", 1, 5)
            t.check(pfx .. ".buyPass-paid", count("shantay_pass") == pass0 + 1 and count("coins") == coins0 - 5,
                "shantay_pass " .. pass0 .. " -> " .. count("shantay_pass") .. ", coins " .. coins0 .. " -> " .. count("coins") .. " (5 coins, shantay.rs2)")
            local chat = {}
            if first then
                chat = { "mesbox:There is a large poster on the wall", "mesbox:The Desert is a VERY Dangerous place",
                    "mesbox:That seems pretty scary!", "choose:Yeah, that poster doesn't scare me!" }
            end
            for _, l in ipairs({ "npc:Can I see your Shantay Desert Pass", "mesbox:You hand over a Shantay Pass.", "player:Sure, here you go!" }) do
                chat[#chat + 1] = l
            end
            if first then chat[#chat + 1] = "npc:Here, have a disclaimer" end
            t.exec(pfx .. ".shantayDoorway", t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 },
                near = { 3304, 3118 }, far_ok = function(tile) return tile.z <= 3115 end,
                far_desc = "south of the Shantay Pass doorway, z <= 3115", chat = chat })
            t.check(pfx .. ".passHandedOver", count("shantay_pass") == pass0, "shantay_pass " .. count("shantay_pass") .. " (want " .. pass0 .. ": handed over)")
        end
        -- Out of the desert from the south: free (shantay_pass.rs2:84 p_telejump 3 north).
        local function shantay_out(pfx)
            t.exec("goto-" .. pfx .. ".shantayOut", t.player.goto_tile, 3304, 3113, 0)
            t.exec(pfx .. ".shantayDoorwayOut", t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 },
                near = { 3304, 3114 }, far_ok = function(tile) return tile.z > 3116 end, far_desc = "north of the Shantay Pass doorway" })
        end
        -- Sophanem's north gate (sophanem_gate_right 3284,2809, a selfstage door: the same symbol swings).
        local function sophanem_in(pfx)
            t.exec("goto-" .. pfx .. ".sophanemGate", t.player.goto_tile, 3284, 2812, 0)
            t.exec(pfx .. ".sophanemGateIn", t.player.pass_door, { closed = "sophanem_gate_right", open = "sophanem_gate_right",
                at = { 3284, 2809, 0 }, near = { 3284, 2810 }, far = { 3284, 2808 } })
        end
        local function sophanem_out(pfx)
            t.exec("goto-" .. pfx .. ".sophanemGateInside", t.player.goto_tile, 3284, 2806, 0)
            t.exec(pfx .. ".sophanemGateOut", t.player.pass_door, { closed = "sophanem_gate_right", open = "sophanem_gate_right",
                at = { 3284, 2809, 0 }, near = { 3284, 2808 }, far = { 3284, 2810 } })
        end
        -- The pit (icthlarin_pyramid.rs2 [oploc2,ics_little_pit_to|from]): 20% run energy a jump; below it
        -- the jump only says so, so wait for energy to come back and press again.
        local function jump_pit(name, sym, at_z, want_z)
            for attempt = 1, 8 do
                t.exec(name .. (attempt > 1 and ("-retry" .. attempt) or ""), t.player.click_loc, sym, 2, { at = { 3292, at_z } })
                t.ticks(4)
                local _, p = t.world.tile()
                if type(p) == "table" and p.z == want_z then break end
                if not tostring(t.msg.last(3)):find("run energy") then break end
                t.ticks(60)
            end
            local _, p = t.world.tile()
            t.check(name .. ".landed", type(p) == "table" and p.z == want_z, "tile " .. reading() .. " (want z " .. want_z .. ") msgs " .. tostring(t.msg.last(3)))
        end
        -- The ladder out (icthlarin_pyramid.rs2:315-317 [oploc1,ics_ladder] p_teleport(^ics_pyramid_door)).
        -- CONTENT BUG: ^ics_pyramid_door = 0_51_43_31_27 (icthlarin.constant:66) is 3295,2779, a walled
        -- pocket between the temple door and inviswall_active on 3294/3295,2780: no walk leaves it
        -- (b69 probe: walk_to 3295,2782 / 3283,2772 / 3284,2810 all time out on 3295,2779). Walk off
        -- the landing on foot; when that fails the run stops here.
        local function off_ladder_landing(name)
            local wr = t.player.walk_to(3295, 2782, 20)
            local _, p = t.world.tile()
            local ok = type(p) == "table" and p.level == 0 and p.z >= 2781 and p.z < 2900
            local detail = "walk_to 3295,2782 off the ladder landing -> " .. tostring(wr) .. "; at " .. reading()
            if ok then t.check(name, ok, detail) end
            return ok, detail
        end
        local function food_and_hp()
            local _, hp = t.skill.read("hitpoints")
            return count("lobster"), hp and (hp.current or hp.boosted or hp.level)
        end

        -- leg 1: the Wanderer, the first memory
        shantay_in("talkToWanderer", true)
        t.exec("goto-talkToWanderer", t.player.goto_tile, 3315, 2850, 0)
        t.exec("talkToWanderer", t.player.talk_to, "ics_little_multi_wanderer", 1)
        t.exec("talkToWanderer-dialog", t.chat.play, {
            "npc:Get that cat away from me!",
            "choose:Why? What's your problem with it?",
            "player:Why? What's your problem with it?",
            "npc:Those things give me the creeps",
            "mesbox:You pick up your cat.",
            "npc:Thank you. Now",
            "choose:Ok I'll get your supplies.",
            "player:Ok I'll get your supplies.",
        })
        t.ticks(2)
        t.expect("quest.stage.need_supplies", t.quest.expect_stage("need_supplies"))
        t.cheat("::give tinderbox 1")
        t.cheat("::give water_skin4 1")
        t.ticks(3)
        t.exec("talkToWandererAgain", t.player.talk_to, "ics_little_multi_wanderer", 1)
        t.exec("talkToWandererAgain-dialog", t.chat.play, {
            "choose:Yes. I have them all here.",
            "player:Yes. I have them all here.",
            "npc:Good... Now, look into my eyes",
            "npc:Look deeply",
            "npc:Don't blink",
            "mesbox:You slowly pick yourself off the ground",
        })
        t.ticks(2)
        t.expect("quest.stage.entered_city", t.quest.expect_stage("entered_city"))
        local _, JAR = t.var.server("varb397_ics_little_jar_multi")
        t.check("hypnosis.jar", JAR == 1 or JAR == 2 or JAR == 3 or JAR == 4, "ics_little_jar_multi = " .. tostring(JAR))
        -- 1 het (liver), 2 scabaras (stomach), 3 apmeken (intestines), 4 crondis (lungs)
        local POT = { [1] = { "ics_little_pot_liver", 3286, 9194, "ics_little_het", "ics_little_canopic_jar_liver", "4dose1defense" },
                      [2] = { "ics_little_pot_stomach", 3286, 9196, "ics_little_scabaras", "ics_little_canopic_jar_stomach", "4dose1agility" },
                      [3] = { "ics_little_pot_intestines", 3286, 9193, "ics_little_apmeken", "ics_little_canopic_jar_intestines", "4dose1attack" },
                      [4] = { "ics_little_pot_lungs", 3286, 9195, "ics_little_crondis", "ics_little_canopic_jar_lungs", "4dose1magic" } }
        local pot = POT[JAR]
        local _, tb = t.inv.count("tinderbox")
        t.check("hypnosis.took_supplies", tb == 0, "tinderbox left " .. tostring(tb))

        t.exec("goto-rock", t.player.goto_tile, 3322, 2855, 0)
        t.exec("enterRock", t.player.click_loc, "ics_little_entrance_multi", 1)
        t.ticks(3)
        _, pos = t.world.tile()
        t.check("enterRock.landed", type(pos) == "table" and pos.z < 2810, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z))
        t.expect("quest.stage.first_memory", t.quest.expect_stage("first_memory"))

        t.exec("goto-touchPyramidDoor", t.player.goto_tile, 3295, 2781, 0)
        t.exec("touchPyramidDoor", t.player.click_loc, "icthalarins_temple_door", 1)
        t.exec("touchPyramidDoor-page", t.chat.play, { "mesbox:You feel compelled" })
        t.ticks(3)
        t.expect("quest.stage.in_pyramid", t.quest.expect_stage("in_pyramid"))
        _, pos = t.world.tile()
        t.check("touchPyramidDoor.landed", type(pos) == "table" and pos.z > 9000, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z))

        t.exec("goto-jumpPit", t.player.goto_tile, 3292, 9193, 0)
        t.exec("jumpPit", t.player.click_loc, "ics_little_pit_to", 2, { at = { 3292, 9194 } })
        t.ticks(4)
        _, pos = t.world.tile()
        t.check("jumpPit.landed", type(pos) == "table" and pos.z == 9197, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z) .. " msgs " .. tostring(t.msg.last(3)))

        t.exec("goto-openWestDoor", t.player.goto_tile, 3280, 9201, 0)
        t.exec("openWestDoor", t.player.click_loc, "icthalarins_ancient_temple_door_1", 1)
        t.exec("openWestDoor-open", t.ui.await_open, "icthalarins_tile_game", 20)
        t.shot("tile-game-start")
        do
            local n = 25
            local state = {}
            for i = 1, n do state[i] = select(2, t.var.server("varb" .. (419 + i) .. "_ics_tile" .. i)) end
            local A = {}
            for i = 0, n - 1 do
                A[i + 1] = {}
                for j = 0, n - 1 do
                    local ri, ci, rj, cj = i // 5, i % 5, j // 5, j % 5
                    A[i + 1][j + 1] = (math.abs(ri - rj) <= 1 and math.abs(ci - cj) <= 1) and 1 or 0
                end
                A[i + 1][n + 1] = 1 - state[i + 1]
            end
            local row, pivots = 1, {}
            for col = 1, n do
                local p
                for r = row, n do if A[r][col] == 1 then p = r break end end
                if p then
                    A[row], A[p] = A[p], A[row]
                    for r = 1, n do
                        if r ~= row and A[r][col] == 1 then
                            for c = col, n + 1 do A[r][c] = (A[r][c] + A[row][c]) % 2 end
                        end
                    end
                    pivots[col] = row
                    row = row + 1
                end
            end
            presses = {}
            for col = 1, n do
                if pivots[col] and A[pivots[col]][n + 1] == 1 then presses[#presses + 1] = col end
            end
        end
        t.check("puzzle.solution", #presses > 0, "presses: " .. table.concat(presses, ","))
        for _, j in ipairs(presses) do
            local _, w = t.ui.widget("icthalarins_tile_game:ics_" .. j)
            t.ui.invoke(w, 0)
            t.ticks(1)
        end
        t.ticks(3)
        t.check("puzzle.solved", select(2, t.var.server("varb445_ics_tilechecker")) == 33554431 or select(2, t.var.server("varb418_ics_little_var")) == 5, "ics_tilechecker = " .. tostring(select(2, t.var.server("varb445_ics_tilechecker"))))
        t.expect("quest.stage.sphinx", t.quest.expect_stage("sphinx"))
        _, pos = t.world.tile()
        t.check("memory.exit", type(pos) == "table" and pos.z < 2900, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z))

        sophanem_in("talkToSphinx")
        t.exec("goto-talkToSphinx", t.player.goto_tile, 3301, 2786, 0)
        t.exec("talkToSphinx", t.player.talk_to, "ics_little_sphinx", 1)
        t.exec("talkToSphinx-dialog", t.chat.play, {
            "npc:Ah, a visitor",
            "player:I need help.",
            "npc:Help has a price",
            "npc:A husband and wife",
            "choose:9.",
            "player:9.",
            "npc:Well answered, human",
            "mesbox:The Sphinx gives you a token.",
        })
        t.ticks(2)
        t.expect("quest.stage.high_priest", t.quest.expect_stage("high_priest"))
        t.inv.expect_has("ics_little_sphinxstatue", 1)

        t.exec("goto-talkToHighPriest", t.player.goto_tile, 3283, 2772, 0)
        t.exec("talkToHighPriest", t.player.talk_to, "ics_little_hipriest_town", 1)
        t.exec("talkToHighPriest-dialog", t.chat.play, {
            "player:The Sphinx asked me to give you this.",
            "npc:Ah, her token",
            "npc:A canopic jar was stolen",
            "npc:The jars are in the room",
        })
        t.ticks(2)
        t.expect("quest.stage.return_jar", t.quest.expect_stage("return_jar"))

        -- leg 2: the jar room
        t.exec("goto-openPyramidDoor", t.player.goto_tile, 3295, 2781, 0)
        t.exec("openPyramidDoor", t.player.click_loc, "icthalarins_temple_door", 1)
        t.ticks(3)
        _, pos = t.world.tile()
        t.check("openPyramidDoor.landed", type(pos) == "table" and pos.z > 9000 and pos.x == 3277, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z))
        t.exec("goto-jumpPitAgain", t.player.goto_tile, 3292, 9193, 0)
        t.exec("jumpPitAgain", t.player.click_loc, "ics_little_pit_to", 2, { at = { 3292, 9194 } })
        t.ticks(4)
        _, pos = t.world.tile()
        t.check("jumpPitAgain.landed", type(pos) == "table" and pos.z == 9197, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z) .. " msgs " .. tostring(t.msg.last(2)))
        t.exec("goto-openWestDoorAgain", t.player.goto_tile, 3280, 9201, 0)
        t.exec("openWestDoorAgain", t.player.click_loc, "icthalarins_ancient_temple_door_1", 1)
        t.ticks(3)
        _, pos = t.world.tile()
        t.check("openWestDoorAgain.landed", type(pos) == "table" and pos.z < 9199 and pos.z > 9190, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z))

        -- the wrong pot will not budge
        local wrong = (JAR == 1) and POT[2] or POT[1]
        t.exec("goto-pots", t.player.goto_tile, 3285, 9194, 0)
        t.exec("wrongJar", t.player.click_loc, wrong[1], 1)
        t.ticks(2)
        t.check("wrongJar.refused", select(2, t.var.server("varb418_ics_little_var")) == 7, "stage still 7; msgs " .. tostring(t.msg.last(2)))

        t.exec("pickUpAnyJar", t.player.click_loc, pot[1], 1)
        t.chat.play({ "mesbox:As you reach for the jar" })
        t.ticks(2)
        t.expect("quest.stage.jar_guardian", t.quest.expect_stage("jar_guardian"))
        t.exec("apparition.present", t.npc.await_present, pot[4], 12, 10)
        t.exec("killApparition", t.player.attack, pot[4], 2, 20)
        local app_food0 = count("lobster")
        local _, app_detail = t.exec("killApparition-wait", t.npc.await_dead_engaged, 900, 4, { eat = { item = "lobster", below = 35 } })
        local app_lowest = tonumber(tostring(app_detail):match("lowest hp (%d+)/"))
        local app_food1, app_hp = food_and_hp()
        t.check("killApparition.margin", app_food1 >= 1 and (app_lowest or 0) >= 25, "lobsters before " .. app_food0 .. ", left " .. app_food1
            .. ", lowest hp " .. tostring(app_lowest) .. "/99, hp after " .. tostring(app_hp) .. " (margin: lowest >= 25 AND food left >= 1)")
        t.ticks(10)
        t.expect("quest.stage.jar_killed", t.quest.expect_stage("jar_killed"))

        t.exec("pickUpAnyJarAgain", t.player.click_loc, pot[1], 1)
        t.exec("pickUpAnyJarAgain-inv", t.inv.await, pot[5], 1, 10)

        -- out of the jar room by its door (from inside it only lets you out, icthlarin_pyramid.rs2:80-84)
        t.exec("goto-leaveJarRoomWithJar", t.player.goto_tile, 3280, 9198, 0)
        t.exec("leaveJarRoomWithJar", t.player.click_loc, "icthalarins_ancient_temple_door_1", 1)
        t.ticks(3)
        _, pos = t.world.tile()
        t.check("leaveJarRoomWithJar.landed", type(pos) == "table" and pos.z == 9200, "tile " .. reading())
        t.exec("goto-returnOverPit", t.player.goto_tile, 3292, 9197, 0)
        jump_pit("returnOverPit", "ics_little_pit_from", 9196, 9193)
        t.expect("quest.stage.jar_crossed", t.quest.expect_stage("jar_crossed"))

        -- leg 3
        jump_pit("jumpOverPitAgain", "ics_little_pit_to", 9194, 9197)
        t.expect("quest.stage.place_jar", t.quest.expect_stage("place_jar"))
        t.exec("goto-solvePuzzleAgain", t.player.goto_tile, 3280, 9201, 0)
        t.exec("solvePuzzleAgain", t.player.click_loc, "icthalarins_ancient_temple_door_1", 1)
        t.exec("solvePuzzleAgain-open", t.ui.await_open, "icthalarins_tile_game", 20)
        do
            local n = 25
            local state = {}
            for i = 1, n do state[i] = select(2, t.var.server("varb" .. (419 + i) .. "_ics_tile" .. i)) end
            local A = {}
            for i = 0, n - 1 do
                A[i + 1] = {}
                for j = 0, n - 1 do
                    local ri, ci, rj, cj = i // 5, i % 5, j // 5, j % 5
                    A[i + 1][j + 1] = (math.abs(ri - rj) <= 1 and math.abs(ci - cj) <= 1) and 1 or 0
                end
                A[i + 1][n + 1] = 1 - state[i + 1]
            end
            local row, pivots = 1, {}
            for col = 1, n do
                local p
                for r = row, n do if A[r][col] == 1 then p = r break end end
                if p then
                    A[row], A[p] = A[p], A[row]
                    for r = 1, n do
                        if r ~= row and A[r][col] == 1 then
                            for c = col, n + 1 do A[r][c] = (A[r][c] + A[row][c]) % 2 end
                        end
                    end
                    pivots[col] = row
                    row = row + 1
                end
            end
            presses = {}
            for col = 1, n do
                if pivots[col] and A[pivots[col]][n + 1] == 1 then presses[#presses + 1] = col end
            end
        end
        t.check("puzzle2.solution", #presses > 0, "presses: " .. table.concat(presses, ","))
        for _, j in ipairs(presses) do
            local _, w = t.ui.widget("icthalarins_tile_game:ics_" .. j)
            t.ui.invoke(w, 0)
            t.ticks(1)
        end
        t.ticks(3)
        _, pos = t.world.tile()
        t.check("puzzle2.door", type(pos) == "table" and pos.z < 9199 and pos.z > 9190, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z)
            .. " tilechecker " .. tostring(select(2, t.var.server("varb445_ics_tilechecker"))))

        t.exec("goto-dropJar", t.player.goto_tile, 3285, pot[3], 0)
        t.exec("dropJar", t.player.inv_op, pot[5], 5)
        t.ticks(3)
        t.expect("quest.stage.jar_done", t.quest.expect_stage("jar_done"))
        t.inv.expect_absent(pot[5])

        t.exec("goto-leaveJarRoom", t.player.goto_tile, 3280, 9198, 0)
        t.exec("leaveJarRoom", t.player.click_loc, "icthalarins_ancient_temple_door_1", 1)
        t.ticks(3)
        _, pos = t.world.tile()
        t.check("leaveJarRoom.landed", type(pos) == "table" and pos.z == 9200, "tile " .. reading())
        t.exec("goto-pitNorthBank", t.player.goto_tile, 3292, 9197, 0)
        jump_pit("jumpPitBackToLadder", "ics_little_pit_from", 9196, 9193)
        t.exec("goto-leavePyramid", t.player.goto_tile, 3277, 9173, 0)
        t.exec("leavePyramid", t.player.climb, { loc = "ics_ladder", at = { 3277, 9172, 0 }, dest = { 3295, 2779, 0 }, slack = 2 })
        t.ticks(3)
        local off_ok, off_detail = off_ladder_landing("leavePyramid.offLanding")
        if not off_ok then
            t.blocked("content_bug: the ics_ladder landing ^ics_pyramid_door = 0_51_43_31_27 (3295,2779; icthlarin.constant:66, "
                .. "icthlarin_pyramid.rs2:317 p_teleport) is a walled pocket (inviswall_active north edges 3294/3295,2780, columns, "
                .. "the temple door object): no walk leaves it, so the player is trapped after every pyramid exit; " .. off_detail)
            return
        end
        t.exec("goto-returnToHighPriest", t.player.goto_tile, 3283, 2772, 0)
        t.exec("returnToHighPriest", t.player.talk_to, "ics_little_hipriest_town", 1)
        t.exec("returnToHighPriest-dialog", t.chat.play, {
            "player:I returned the jar.",
            "npc:Well done",
            "choose:Sure, no problem.",
            "player:Sure, no problem.",
            "npc:Speak to the Embalmer",
        })
        t.ticks(2)
        t.expect("quest.stage.embalm", t.quest.expect_stage("embalm"))

        -- the embalming
        t.exec("goto-raetul-early", t.player.goto_tile, 3311, 2789, 0)
        t.exec("raetul-early", t.player.talk_to, "ics_little_linen1", 1)
        t.exec("raetul-early-dialog", t.chat.play, { "npc:Linen, fine linen!" })
        t.chat.close()
        t.inv.expect_absent("ics_little_linen")
        t.exec("goto-carpenter-early", t.player.goto_tile, 3313, 2770, 0)
        t.exec("carpenter-early", t.player.talk_to, "ics_little_carpenter", 1)
        t.exec("carpenter-early-dialog", t.chat.play, { "npc:The Embalmer has first claim" })
        t.chat.close()

        t.exec("goto-talkToEmbalmer", t.player.goto_tile, 3287, 2757, 0)
        t.exec("talkToEmbalmer", t.player.talk_to, "ics_little_embalmer", 1)
        t.exec("talkToEmbalmer-dialog", t.chat.play, {
            "player:The High Priest says",
            "npc:I need salt, a bucket of sap",
            "npc:Salt from the lake",
            "npc:My manual lies",
        })
        t.ticks(2)
        t.expect("embalmer.met", t.var.expect("varb399_ics_metembalmer", 1))
        t.cheat("::give ics_little_bookofembalming 1")
        t.ticks(2)
        t.exec("readManual", t.player.inv_op, "ics_little_bookofembalming", 1)
        t.exec("readManual-page", t.chat.play, { "mesbox:Embalming, by Bod E. Wrapper", "mesbox:'Work without haste" })

        t.exec("goto-buyLinen", t.player.goto_tile, 3311, 2789, 0)
        local coins_before_linen = count("coins")
        t.exec("buyLinen", t.player.talk_to, "ics_little_linen1", 1)
        t.exec("buyLinen-dialog", t.chat.play, {
            "npc:Linen — thirty coins a piece.",
            "choose:Buy one linen.",
            "npc:Pleasure.",
        })
        t.exec("buyLinen-inv", t.inv.await, "ics_little_linen", 1, 10)
        local _, coins = t.inv.count("coins")
        t.check("buyLinen.coins", coins == coins_before_linen - 30, "coins " .. tostring(coins_before_linen) .. " -> " .. tostring(coins) .. " want -30 (^ics_linen_cost)")

        sophanem_out("lake")
        t.exec("goto-lake", t.player.goto_tile, 3286, 2839, 0)
        t.exec("fillBucketWithWater", t.player.click_loc, "icthalarins_waters_edge", 1)
        t.exec("fillBucketWithWater-inv", t.inv.await, "ics_little_saltwaterbucket", 1, 10)
        sophanem_in("suntrap")
        t.exec("goto-suntrap", t.player.goto_tile, 3305, 2758, 0)
        local suntrap = t.player.by_symbol("loc", "icthalarins_suntrap_centre")
        t.exec("makeSalt", t.player.use_on, "ics_little_saltwaterbucket", suntrap)
        t.exec("makeSalt-inv", t.inv.await, "ics_little_pileofsalt", 1, 10)
        sophanem_out("evergreen")
        shantay_out("evergreen")
        t.exec("goto-evergreen", t.player.goto_tile, 3018, 3460, 0)
        local tree = t.player.by_symbol("loc", "evergreen")
        t.exec("tapSap", t.player.use_on, "knife", tree, { at = { 3018, 3458 } })
        t.exec("tapSap-inv", t.inv.await, "ics_little_sap_bucket", 1, 20)

        shantay_in("talkToEmbalmerAgain", false)
        sophanem_in("talkToEmbalmerAgain")
        t.exec("goto-talkToEmbalmerAgain", t.player.goto_tile, 3287, 2757, 0)
        t.exec("talkToEmbalmerAgain", t.player.talk_to, "ics_little_embalmer", 1)
        t.exec("talkToEmbalmerAgain-dialog", t.chat.play, {
            "npc:Good — the salt.",
            "npc:And the sap.",
            "npc:And the linen. Perfect.",
            "npc:That is everything I need",
        })
        t.ticks(2)
        local _, packed = t.var.server("varb400_ics_little_embalmer_multi")
        t.check("embalmer.all_items", packed == 7, "ics_little_embalmer_multi = " .. tostring(packed))
        t.inv.expect_absent("ics_little_linen")

        t.exec("goto-talkToCarpenter", t.player.goto_tile, 3313, 2770, 0)
        t.exec("talkToCarpenter", t.player.talk_to, "ics_little_carpenter", 1)
        t.exec("talkToCarpenter-dialog", t.chat.play, {
            "npc:The High Priest's ritual needs a holy symbol",
            "choose:Here are some willow logs.",
            "player:Here are some willow logs.",
            "npc:Thanks. Give me a moment",
        })
        t.ticks(2)
        t.expect("carpenter.logs", t.var.expect("varb398_ics_little_carpenter_multi", 1))
        t.inv.expect_absent("ics_little_holy_symbol")
        t.exec("talkToCarpenterAgain", t.player.talk_to, "ics_little_carpenter", 1)
        t.exec("talkToCarpenterAgain-dialog", t.chat.play, { "npc:Here — a holy symbol." })
        t.ticks(2)
        t.inv.expect_has("ics_little_holy_symbol", 1)
        t.expect("quest.stage.ritual", t.quest.expect_stage("ritual"))

        -- leg 4: the ritual
        t.exec("goto-openPyramidDoorWithSymbol", t.player.goto_tile, 3295, 2781, 0)
        t.exec("openPyramidDoorWithSymbol", t.player.click_loc, "icthalarins_temple_door", 1)
        t.ticks(3)
        t.exec("goto-jumpPitWithSymbol", t.player.goto_tile, 3292, 9193, 0)
        jump_pit("jumpPitWithSymbol", "ics_little_pit_to", 9194, 9197)

        t.exec("goto-enterEastRoom", t.player.goto_tile, 3306, 9201, 0)
        t.exec("enterEastRoom", t.player.click_loc, "icthalarins_ancient_temple_door_2", 1)
        t.exec("enterEastRoom-page", t.chat.play, { "mesbox:As you step inside" })
        t.ticks(3)
        t.expect("quest.stage.place_symbol", t.quest.expect_stage("place_symbol"))
        t.inv.expect_has("ics_little_unholy_symbol", 1)
        t.inv.expect_absent("ics_little_holy_symbol")
        _, pos = t.world.tile()
        t.check("enterEastRoom.landed", type(pos) == "table" and pos.z < 9199 and pos.z > 9190, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z))

        t.exec("goto-useSymbolOnSarcopagus", t.player.goto_tile, 3311, 9195, 0)
        local sarc = t.player.by_symbol("loc", "ics_sarcophigi_door_2_op")
        t.exec("useSymbolOnSarcopagus", t.player.use_on, "ics_little_unholy_symbol", sarc, { at = { 3312, 9195 } })
        t.exec("useSymbolOnSarcopagus-page", t.chat.play, { "mesbox:You lay the unholy symbol" })
        t.ticks(3)
        t.expect("quest.stage.symbol_placed", t.quest.expect_stage("symbol_placed"))
        t.inv.expect_has("ics_little_holy_symbol", 1)
        t.inv.expect_absent("ics_little_unholy_symbol")

        t.exec("goto-leaveEastRoom", t.player.goto_tile, 3306, 9198, 0)
        t.exec("leaveEastRoom", t.player.click_loc, "icthalarins_ancient_temple_door_2", 1)
        t.ticks(3)
        t.expect("quest.stage.ceremony", t.quest.expect_stage("ceremony"))
        _, pos = t.world.tile()
        t.check("leaveEastRoom.landed", type(pos) == "table" and pos.z >= 9199, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z))

        t.exec("enterEastRoomAgain", t.player.click_loc, "icthalarins_ancient_temple_door_2", 1)
        t.ticks(3)
        _, pos = t.world.tile()
        t.check("enterEastRoomAgain.landed", type(pos) == "table" and pos.z < 9199, "tile " .. tostring(pos and pos.x) .. "," .. tostring(pos and pos.z))
        t.exec("talkToHighPriestInPyramid", t.player.talk_to, "ics_little_hipriest_ceremony_op", 1)
        t.exec("talkToHighPriestInPyramid-dialog", t.chat.play, {
            "npc:You have come back, friend",
            "npc:Wait... who comes there?",
            "player:The Wanderer!",
            "npc:He speaks the words",
            "npc:She has taken one of my priests",
        })
        t.ticks(3)
        t.expect("quest.stage.possessed", t.quest.expect_stage("possessed"))
        t.exec("priest.present", t.npc.await_present, "ics_little_possessedpriest", 14, 10)
        t.exec("killPriest", t.player.attack, "ics_little_possessedpriest", 2, 20)
        local pr_food0 = count("lobster")
        local _, pr_detail = t.exec("killPriest-wait", t.npc.await_dead_engaged, 1200, 4, { eat = { item = "lobster", below = 40 } })
        local pr_lowest = tonumber(tostring(pr_detail):match("lowest hp (%d+)/"))
        local pr_food1, pr_hp = food_and_hp()
        t.check("killPriest.margin", pr_food1 >= 1 and (pr_lowest or 0) >= 25, "lobsters before " .. pr_food0 .. ", left " .. pr_food1
            .. ", lowest hp " .. tostring(pr_lowest) .. "/99, hp after " .. tostring(pr_hp) .. " (margin: lowest >= 25 AND food left >= 1)")
        t.ticks(10)
        t.expect("quest.stage.priest_dead", t.quest.expect_stage("priest_dead"))
        t.exec("priest.potion", t.player.click_obj, pot[6], 3)
        t.exec("priest.potion-inv", t.inv.await, pot[6], 1, 10)

        t.exec("talkToHighPriestAfter", t.player.talk_to, "ics_little_hipriest_ceremony_op", 1)
        t.exec("talkToHighPriestAfter-dialog", t.chat.play, {
            "player:The priest is beaten.",
            "npc:Then the ritual is safe",
            "npc:Be ready",
        })
        t.ticks(2)
        t.expect("quest.stage.meet_god", t.quest.expect_stage("meet_god"))

        t.exec("leaveEastRoomFinal", t.player.click_loc, "icthalarins_ancient_temple_door_2", 1)
        t.ticks(3)
        _, pos = t.world.tile()
        t.check("leaveEastRoomFinal.landed", type(pos) == "table" and pos.z >= 9199, "tile " .. reading())
        t.exec("goto-pitNorthBankFinal", t.player.goto_tile, 3292, 9197, 0)
        jump_pit("jumpPitBackFinal", "ics_little_pit_from", 9196, 9193)
        t.exec("goto-leavePyramidToFinish", t.player.goto_tile, 3277, 9173, 0)
        t.exec("leavePyramidToFinish", t.player.climb, { loc = "ics_ladder", at = { 3277, 9172, 0 }, dest = { 3295, 2779, 0 }, slack = 2 })
        t.exec("leavePyramidToFinish-cutscene", t.chat.play, {
            "mesbox:As you leave the pyramid",
            "mesbox:'This is no mere mortal",
            "mesbox:A voice hisses",
        })
        t.ticks(3)
        t.expect("quest.stage.finish_talk", t.quest.expect_stage("finish_talk"))
        local off_ok, off_detail = off_ladder_landing("leavePyramidToFinish.offLanding")
        if not off_ok then
            t.blocked("content_bug: the ics_ladder landing ^ics_pyramid_door = 0_51_43_31_27 (3295,2779; icthlarin.constant:66, "
                .. "icthlarin_pyramid.rs2:317 p_teleport) is a walled pocket: no walk leaves it; " .. off_detail)
            return
        end

        local _, snap = t.skill.snapshot()
        t.exec("goto-talkToHighPriestToFinish", t.player.goto_tile, 3283, 2772, 0)
        t.exec("talkToHighPriestToFinish", t.player.talk_to, "ics_little_hipriest_town", 1)
        t.exec("talkToHighPriestToFinish-dialog", t.chat.play, {
            "player:The ritual is safe.",
            "npc:Icthlarin is pleased",
        })
        t.ticks(3)
        t.check("reward.thieving", t.skill.expect_gain("thieving", 4500, snap) == "ok", "thieving +4500")
        t.check("reward.agility", t.skill.expect_gain("agility", 4000, snap) == "ok", "agility +4000")
        t.check("reward.woodcutting", t.skill.expect_gain("woodcutting", 4000, snap) == "ok", "woodcutting +4000")
        t.inv.expect_has("ics_little_amulet_of_catspeak", 1)
        t.quest.expect_complete()
        t.finish(0)
    end,
}
