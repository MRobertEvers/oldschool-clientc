-- Tower of Life. Scaffolded by tools/quest_gate/new_quest.py from Quest Helper's
-- helpers/quests/toweroflife/ (b72), then rewritten by hand against the pack's own
-- scripts: OSRS-Content/osrs239-content/server/scripts/quests/quest_toweroflife/
-- scripts/{tol_start,tol_outfit,tol_tower,tol_puzzles,tol_basement,tol_homunculus,tol_shared}.rs2
-- and configs/toweroflife.constant (the stage ladder 0/2/4/6/8/10/11/12/14/16/17/18,
-- Quest Helper's steps.put keys).
--
-- Route (door rule, b72): Lumbridge -> overland to the open ground south of the
-- members' wall gate south of Taverley (membergatel 2934,3320, pressed by
-- cross_gate: the only walk on foot into Kandarin, as in cog.lua) -> overland to
-- the open ground beside Effigy (2638,3218, outside the tower). Everything after
-- that is walked. The tower (walls x 2642/2656, z 3211/3225; interior x 2643-2655,
-- z 3212-3224) is entered and left ONLY by its door tol_tower_wall_door 2649,3225
-- (reach.py 2650,3227 -> 2649,3220: NEEDS-DOOR via tol_tower_wall_door), on every
-- visit, in and out; every floor change is a real climb, and the dungeon below
-- (Creature Creation, 3038,4376) is reached by the ground floor's trapdoor and left
-- by its ladder.
--
-- The three machines are the cache's puzzle screens (interfaces 510/511/509), solved
-- by the wiki's own instructions (Tower_of_Life oldid 15366014, Quick_guide oldid
-- 14831032) and read back on the varbits Quest Helper's PuzzleSolver.java reads.
-- The homunculus's questions are answered with the quick guide's 'Magic' line.
--
-- No fight. No dialogue branches on a stat other than Construction 10, the
-- quest's own requirement (tol_shared.rs2 ~tol_meets_requirements), staged in setup.

return {
    id = "toweroflife",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- Lumbridge -> Kandarin on foot, then four floors climbed several times
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::give beer 1", -- Quest Helper's getItemRequirements(): Beer (for 'The Guns', tol_outfit.rs2 [opnpc1,tol_npc_builder04])
        "::give hammer 1", -- Quest Helper's getItemRequirements(): Hammer (tol_tower.rs2: every machine build checks hammer)
        "::give poh_saw 1", -- Quest Helper's getItemRequirements(): Saw (tol_tower.rs2: every machine build checks poh_saw)
        "::setlevel construction 10", -- the quest's requirement (constant ^tol_req_construction = 10)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb3337_tol_prog",
            constants = {
                not_started = 0,
                agreed_to_help = 2,
                bonafido_briefed = 4,
                outfit_ready = 6,
                fixing_tower = 8,
                tower_fixed = 10,
                creation_seen = 11,
                confronting_homunculus = 12,
                homunculus_quiz = 14,
                scare_alchemists = 16,
                basement_homunculus = 17,
                complete = 18,
            },
            row = "quest_toweroflife",
            display = "Tower of Life",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function tile_text(r, tl)
            if r ~= "ok" or not tl then
                return tostring(r)
            end
            return string.format("%d,%d,%d", tl.x, tl.z, tl.level)
        end
        local function check_at(name, want_level, x0, x1, z0, z1, why)
            local r, tl = t.world.tile()
            t.check(name, r == "ok" and tl.level == want_level and tl.x >= x0 and tl.x <= x1 and tl.z >= z0 and tl.z <= z1,
                string.format("player at %s (want level %d, x %d-%d, z %d-%d: %s)", tile_text(r, tl), want_level, x0, x1, z0, z1, why))
        end
        local function in_tower(level)
            return function(tl)
                return tl.level == level and tl.x >= 2643 and tl.x <= 2655 and tl.z >= 3212 and tl.z <= 3224
            end
        end

        -- The tower door (wall on 2649,3225; outside is z >= 3225 north of it, inside z <= 3224).
        local door_in_spec = { closed = "tol_tower_wall_door", open = "tol_tower_wall_door_open",
            at = { 2649, 3225, 0 }, near = { 2649, 3226 }, far = { 2649, 3224 } }
        local function tower_door_in(name)
            return t.exec(name, t.player.pass_door, door_in_spec)
        end
        local function tower_door_out(name)
            return t.exec(name, t.player.pass_door, { closed = "tol_tower_wall_door", open = "tol_tower_wall_door_open",
                at = { 2649, 3225, 0 }, near = { 2649, 3224 }, far = { 2649, 3226 } })
        end

        -- Floor changes. ladders.rs2 [proc,climb]: no maplink row for the stairs, so the player moves
        -- one plane on the tile they pressed from; graded on the new level inside the tower.
        local function stairs_up_ground(name) -- tol_stairs01 2644,3219 (2x2), ground -> 1
            t.exec(name, t.player.climb, { loc = "tol_stairs01", op = 1, op_name = "Climb-up",
                at = { 2644, 3219, 0 }, dest = { 2645, 3220, 1 }, slack = 3,
                landed_ok = in_tower(1), landed_desc = "the tower's first floor" })
        end
        local function stairs_up_first(name) -- tol_stairs01 2652,3219 (2x2), 1 -> 2
            t.exec(name, t.player.climb, { loc = "tol_stairs01", op = 1, op_name = "Climb-up",
                at = { 2652, 3219, 1 }, dest = { 2653, 3220, 2 }, slack = 3,
                landed_ok = in_tower(2), landed_desc = "the tower's second floor" })
        end
        -- The ladder's forceapproach=27 walks every press to its west side 2646,3221,2;
        -- maplink_landings.dbrow maplink_2_41_50_22_21_tolup lands it on 2648,3221,3, the open floor
        -- beside the ladder down (reach.py 2648,3221 -> 2648,3216 level 3: REACH).
        local ladder_up_spec = { loc = "area_sanguine_ghetto_ladder_up", op = 1, op_name = "Climb-up",
            at = { 2647, 3221, 2 }, src = { 2646, 3221 }, dest = { 2648, 3221, 3 }, slack = 0,
            landed_ok = in_tower(3), landed_desc = "the tower's top floor" }
        local function ladder_up_second(name) -- area_sanguine_ghetto_ladder_up 2647,3221, 2 -> 3
            return t.exec(name, t.player.climb, ladder_up_spec)
        end
        local function ladder_down_top(name) -- area_sanguine_ghetto_ladder_down 2647,3221, 3 -> 2
            t.exec(name, t.player.climb, { loc = "area_sanguine_ghetto_ladder_down", op = 1, op_name = "Climb-down",
                at = { 2647, 3221, 3 }, src = { 2648, 3221 }, dest = { 2648, 3221, 2 }, slack = 0,
                landed_ok = in_tower(2), landed_desc = "the tower's second floor" })
        end
        local function stairs_down_second(name) -- tol_gapfill01 2652,3219, 2 -> 1
            t.exec(name, t.player.climb, { loc = "tol_gapfill01", op = 1, op_name = "Climb-down",
                at = { 2652, 3219, 2 }, dest = { 2653, 3220, 1 }, slack = 3,
                landed_ok = in_tower(1), landed_desc = "the tower's first floor" })
        end
        local function stairs_down_first(name) -- tol_gapfill01 2644,3219, 1 -> ground
            t.exec(name, t.player.climb, { loc = "tol_gapfill01", op = 1, op_name = "Climb-down",
                at = { 2644, 3219, 1 }, dest = { 2645, 3220, 0 }, slack = 3,
                landed_ok = in_tower(0), landed_desc = "the tower's ground floor" })
        end

        -- ==== Lumbridge -> the Tower of Life, on foot ====
        t.exec("goto-memberGate", t.player.goto_tile, 2934, 3318, 0)
        t.exec("memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
            near = { 2934, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2934) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320", far = { 2934, 3322 } })
        t.exec("goto-talkToEffigy", t.player.goto_tile, 2638, 3218, 0)

        -- ==== talkToEffigy (tol_start.rs2 [opnpc1,tol_npc_efergy01], stage 0 -> 2) ====
        t.exec("talkToEffigy", t.player.talk_to, "tol_npc_efergy01", 1)
        t.exec("talkToEffigy-dialog", t.chat.play, {
            "npc:Oh, hello! I don't suppose",
            "npc:There are some rather odd alchemists",
            "choose:Sure, why not.",
            "player:Sure, why not.",
            "npc:Wonderful! Speak to Bonafido",
        })
        t.expect("quest.stage.agreed_to_help", t.quest.expect_stage("agreed_to_help"))

        -- ==== talkToBonafido (tol_start.rs2 [opnpc1,tol_npc_barry01], 2 -> 4) ====
        t.exec("talkToBonafido", t.player.talk_to, "tol_npc_barry01", 1)
        t.exec("talkToBonafido-dialog", t.chat.play, {
            "npc:Ah, Effigy sent you!",
            "npc:Black-eye, No fingers, The Guns",
        })
        t.expect("quest.stage.bonafido_briefed", t.quest.expect_stage("bonafido_briefed"))

        -- ==== getBuildersOutfit (tol_outfit.rs2) ====
        -- Black-eye's three questions: the hard hat goes straight onto the head (inv_add(worn, ...)).
        t.exec("talkToBlackeye", t.player.talk_to, "tol_npc_builder01", 1)
        t.exec("talkToBlackeye-dialog", t.chat.play, {
            "npc:'Black-eye', they call me",
            "choose:Three.",
            "player:Three.",
            "npc:Ha, spot on!",
            "choose:Torn curtains.",
            "player:Torn curtains.",
            "npc:Ha! Right again.",
            "choose:10 clay pieces.",
            "player:10 clay pieces.",
            "npc:Ha! Knew you had it in you.",
            "mesbox:'Black-eye' hands you a hard hat",
        })

        -- No fingers: talk first (sets tol_nofingers_asked), then Pickpocket (op3) lifts the boots.
        t.exec("talkToNoFingers", t.player.talk_to, "tol_npc_builder02", 1)
        t.exec("talkToNoFingers-dialog", t.chat.play, {
            "npc:'No fingers', at your service.",
            "mesbox:'No fingers' doesn't seem to be carrying",
        })
        t.exec("pickpocketNoFingers", t.player.talk_to, "tol_npc_builder02", 3)
        t.exec("pickpocketNoFingers-dialog", t.chat.play, {
            "mesbox:You deftly lift a pair of builder's boots",
        })

        -- The Guns: the beer (brought along) for the shirt.
        local beer_r, beer_n = t.inv.count("beer")
        t.check("getBeerForGuns", beer_r == "ok" and beer_n == 1, "beer in the backpack for 'The Guns': " .. tostring(beer_r) .. " " .. tostring(beer_n))
        t.exec("talkToGuns", t.player.talk_to, "tol_npc_builder04", 1)
        t.exec("talkToGuns-dialog", t.chat.play, {
            "player:Here, have a beer.",
            "npc:Ha, cheers! A deal's a deal",
            "mesbox:'The Guns' hands you a builder's shirt.",
        })
        t.exec("talkToGuns-beerGone", t.inv.await, "beer", 0, 5)

        -- The trousers: search the bushes south of the tower (tol_plant4, any copy).
        t.exec("goto-getTrousers", t.player.goto_tile, 2646, 3207, 0)
        t.exec("getTrousers", t.player.click_loc, "tol_plant4", 1, { at = { 2645, 3208 } })
        t.exec("getTrousers-msg", t.msg.expect, "find a pair of builder's trousers")

        -- Back round to Bonafido, wearing all four pieces: his four questions.
        t.exec("goto-talkToBonafidoWithOutfit", t.player.goto_tile, 2649, 3228, 0)
        t.exec("talkToBonafidoWithOutfit", t.player.talk_to, "tol_npc_barry01", 1)
        t.exec("talkToBonafidoWithOutfit-dialog", t.chat.play, {
            "npc:Well would you look at that!",
            "npc:Plenty of work to do",
            "choose:Tea.",
            "player:Tea.",
            "npc:Bingo! Nothing better.",
            "choose:Whistle for attention.",
            "player:*whistles for attention*",
            "npc:Wahey! Nice one.",
            "choose:Your legs are getting a bit cold.",
            "player:Your legs are getting a bit cold.",
            "npc:Exactamondo!",
            "choose:Carry on, it'll fix itself.",
            "player:Carry on, it'll fix itself.",
            "npc:Yep, that's the one!",
        })
        t.expect("quest.stage.outfit_ready", t.quest.expect_stage("outfit_ready"))

        -- ==== enterTower (tol_tower.rs2 [oploc1,tol_tower_wall_door], 6 -> 8) ====
        -- The tower's only way in (reach.py 2650,3227 -> 2649,3220: NEEDS-DOOR via tol_tower_wall_door),
        -- worn outfit required while the quest runs (wiki Tower_of_Life oldid 15366014).
        tower_door_in("enterTower")
        t.ticks(2)
        t.expect("quest.stage.fixing_tower", t.quest.expect_stage("fixing_tower"))
        check_at("enterTower-inside", 0, 2643, 2655, 3212, 3224, "inside the tower")

        -- Interface presses. An IF3 op (the pressure and pipe machines) is op 1; the cage is an IF1
        -- screen (every component if3=no), pressed with op 0 (trap 33).
        local function widget(name)
            for _ = 1, 15 do
                local r, w = t.ui.widget(name)
                if r == "ok" and w then
                    return w
                end
                t.ticks(1)
            end
            return nil
        end
        local function press(name, op)
            local w = widget(name)
            if not w then
                return "no_widget " .. name
            end
            local r = t.ui.invoke(w, op)
            t.ticks(1)
            return tostring(r)
        end
        local function read(name)
            local _, v = t.var.server(name)
            return v
        end

        -- ==== Pressure machine (first floor) ====
        t.exec("fixPressureMachineGetSheets", t.player.click_loc, "tol_crate06", 1)
        t.exec("fixPressureMachineGetSheets-has", t.inv.await, "tol_metal_sheet", 3, 8)
        t.exec("fixPressureMachineGetBalls", t.player.click_loc, "tol_crate07", 1)
        t.exec("fixPressureMachineGetBalls-has", t.inv.await, "tol_pressure_ball", 4, 8)
        t.exec("fixPressureMachineGetWheels", t.player.click_loc, "tol_crate05", 1)
        t.exec("fixPressureMachineGetWheels-has", t.inv.await, "tol_wheel", 4, 8)
        stairs_up_ground("climbUpToFloor1")
        t.exec("buildPressureMachine", t.player.click_loc, "tol_pressure_machine01", 1)
        t.exec("buildPressureMachine-dialog", t.chat.play, {
            "mesbox:The machine appears unfinished.",
            "mesbox:4 coloured balls, 3 pieces of metal sheeting",
            "choose:Yes",
            "mesbox:You built the machine!",
            "mesbox:The pressure seems to be affected by holes",
        })
        t.exec("buildPressureMachine-built", t.var.await, "varb3338_tol_pres_prog", 1, 10)
        t.exec("solvePressureMachinePuzzle-open", t.ui.await_open, "tol_pressure_machine", 10)

        -- solvePressureMachinePuzzle: wiki Tower_of_Life/Quick_guide oldid 14831032, in its order. Each turn is
        -- read back on the pipe's level varbit; a blocked hole on tol_pres_solved<n>.
        local pres_level = { "varb3351_tol_pres1_level", "varb3352_tol_pres2_level", "varb3353_tol_pres3_level", "varb3355_tol_pres4_level" }
        local pres_solved = { "varb3347_tol_pres_solved1", "varb3348_tol_pres_solved2", "varb3349_tol_pres_solved3", "varb3350_tol_pres_solved4" }
        local function lever(name, n, want)
            local r = press("tol_pressure_machine:lever_" .. n, 1)
            local a = t.var.await_server(n == 1 and "varb3356_tol_lever1" or "varb3357_tol_lever2", want, 5)
            t.check(name, a == "ok", "lever_" .. n .. " -> " .. r .. "; lever" .. n .. " now " .. tostring(read(n == 1 and "varb3356_tol_lever1" or "varb3357_tol_lever2")) .. " (want " .. want .. ")")
        end
        local function turn(name, pipe, dir, times)
            local log = {}
            local ok = true
            for _ = 1, times do
                local before = read(pres_level[pipe])
                local want = before + (dir == "left" and -1 or 1)
                local r = press("tol_pressure_machine:valve_" .. pipe .. "_" .. dir, 1)
                local a = t.var.await_server(pres_level[pipe], want, 5)
                log[#log + 1] = string.format("%s %d->%s", r, before, tostring(read(pres_level[pipe])))
                if a ~= "ok" then
                    ok = false
                    break
                end
            end
            t.check(name, ok, "valve " .. pipe .. " " .. dir .. " x" .. times .. ": " .. table.concat(log, ", "))
        end
        local function blocked(name, pipe)
            local a = t.var.await_server(pres_solved[pipe], 1, 5)
            t.check(name, a == "ok", "pipe " .. pipe .. " leak blocked (" .. pres_solved[pipe] .. " = " .. tostring(read(pres_solved[pipe])) .. ")")
        end
        -- Pipe 2: pull down the left lever, valve 2 left twice, then right until the pipe is full.
        lever("solvePressureMachinePuzzle.leftLeverDown", 1, 1)
        turn("solvePressureMachinePuzzle.pipe2Left", 2, "left", 2)
        blocked("solvePressureMachinePuzzle.pipe2Blocked", 2)
        turn("solvePressureMachinePuzzle.pipe2Fill", 2, "right", 3)
        -- Pipe 4: pull down the right lever, valve 4 right three times, then left once, then right until full.
        lever("solvePressureMachinePuzzle.rightLeverDown", 2, 1)
        turn("solvePressureMachinePuzzle.pipe4Right", 4, "right", 3)
        turn("solvePressureMachinePuzzle.pipe4Left", 4, "left", 1)
        blocked("solvePressureMachinePuzzle.pipe4Blocked", 4)
        turn("solvePressureMachinePuzzle.pipe4Fill", 4, "right", 2)
        -- Pipe 3: lift the right lever back up, valve 3 right twice and left once, then right until full.
        lever("solvePressureMachinePuzzle.rightLeverUp", 2, 0)
        turn("solvePressureMachinePuzzle.pipe3Right", 3, "right", 2)
        turn("solvePressureMachinePuzzle.pipe3Left", 3, "left", 1)
        blocked("solvePressureMachinePuzzle.pipe3Blocked", 3)
        turn("solvePressureMachinePuzzle.pipe3Fill", 3, "right", 3)
        -- Pipe 1: lift the left lever up, valve 1 left twice, then right until full.
        lever("solvePressureMachinePuzzle.leftLeverUp", 1, 0)
        turn("solvePressureMachinePuzzle.pipe1Left", 1, "left", 2)
        blocked("solvePressureMachinePuzzle.pipe1Blocked", 1)
        turn("solvePressureMachinePuzzle.pipe1Fill", 1, "right", 4)
        t.exec("solvePressureMachinePuzzle-dialog", t.chat.play, { "mesbox:The machine is working!" })
        t.exec("solvePressureMachinePuzzle", t.var.await, "varb3338_tol_pres_prog", 2, 10)

        -- ==== Pipe machine (second floor) ====
        stairs_down_first("climbDownToGround")
        t.exec("fixPipeMachineGetPipes", t.player.click_loc, "tol_crate02", 1)
        t.exec("fixPipeMachineGetPipes-has", t.inv.await, "tol_pipe", 4, 8)
        t.exec("fixPipeMachineGetRings", t.player.click_loc, "tol_crate03", 1)
        t.exec("fixPipeMachineGetRings-has", t.inv.await, "tol_ring", 5, 8)
        t.exec("fixPipeMachineGetRivets", t.player.click_loc, "tol_crate04", 1)
        t.exec("fixPipeMachineGetRivets-has", t.inv.await, "tol_rivets", 6, 8)
        stairs_up_ground("climbUpToFloor1-pipe")
        stairs_up_first("climbUpToFloor2")
        t.exec("buildPipeMachine", t.player.click_loc, "tol_pipe_machine_multi", 1)
        t.exec("buildPipeMachine-dialog", t.chat.play, {
            "mesbox:The machine appears unfinished.",
            "mesbox:6 rivets, 4 metal pipes and 5 metal rings.",
            "choose:Yes",
            "mesbox:You built the machine!",
        })
        t.exec("buildPipeMachine-built", t.var.await, "varb3339_tol_pipe_prog", 1, 10)
        t.exec("solvePipeMachinePuzzle-open", t.ui.await_open, "tol_pipe_machine", 10)

        -- solvePipeMachinePuzzle: select each piece, turn it if it needs it (only the big bend does, wiki
        -- oldid 15366014), and slide it onto its place in the frame (Quest Helper PuzzleSolver.java's
        -- targets), reading the piece's place off the screen's scratch (%if1..%if5 = x*1000+y, +1000000 locked).
        local pipe_pieces = {
            { n = 1, com = "tol_pipe_piece01", active = "varb3341_tol_pipe_piece1_active", var = "varp261_if1", gx = 83, gy = 80 },
            { n = 2, com = "tol_pipe_piece02", active = "varb3342_tol_pipe_piece2_active", var = "varp262_if2", gx = 126, gy = 64 },
            { n = 3, com = "tol_pipe_piece03", active = "varb3343_tol_pipe_piece3_active", var = "varp263_if3", gx = 159, gy = 69 },
            { n = 4, com = "tol_pipe_piece04", active = "varb3344_tol_pipe_piece4_active", var = "varp264_if4", gx = 256, gy = 60 },
            { n = 5, com = "tol_pipe_piece05", active = "varb3345_tol_pipe_piece5_active", var = "varp265_if5", gx = 237, gy = 155 },
        }
        for _, p in ipairs(pipe_pieces) do
            local rs = press("tol_pipe_machine:" .. p.com, 1)
            local sel = t.var.await_server(p.active, 1, 5)
            t.check("solvePipeMachinePuzzle.select" .. p.n, sel == "ok", "Select " .. p.com .. " -> " .. rs .. "; " .. p.active .. " = " .. tostring(read(p.active)))
            if p.n == 5 then
                local turns = 0
                while read("varb7981_tol_pipe5_rot") ~= 0 and turns < 4 do
                    press("tol_pipe_machine:tol_pipe_rotate_button_layer01", 1)
                    turns = turns + 1
                end
                t.check("solvePipeMachinePuzzle.rotate5", read("varb7981_tol_pipe5_rot") == 0, "Rotate x" .. turns .. ": tol_pipe5_rot = " .. tostring(read("varb7981_tol_pipe5_rot")))
            end
            local moves = 0
            local function pos()
                local v = read(p.var) or 0
                v = v % 1000000
                return math.floor(v / 1000), v % 1000
            end
            for _ = 1, 60 do
                local x, y = pos()
                if read(p.var) >= 1000000 then
                    break
                end
                if x > p.gx then
                    press("tol_pipe_machine:tol_move_left_layer01", 1)
                elseif x < p.gx then
                    press("tol_pipe_machine:tol_move_right_layer01", 1)
                elseif y > p.gy then
                    press("tol_pipe_machine:tol_move_up_layer01", 1)
                elseif y < p.gy then
                    press("tol_pipe_machine:tol_move_down_layer01", 1)
                else
                    break
                end
                moves = moves + 1
            end
            local x, y = pos()
            t.check("solvePipeMachinePuzzle.place" .. p.n, (read(p.var) or 0) >= 1000000,
                string.format("%s: %d moves -> %d,%d (goal %d,%d), locked %s", p.com, moves, x, y, p.gx, p.gy, tostring((read(p.var) or 0) >= 1000000)))
        end
        t.exec("solvePipeMachinePuzzle-dialog", t.chat.play, { "mesbox:The machine is working!" })
        t.exec("solvePipeMachinePuzzle", t.var.await, "varb3339_tol_pipe_prog", 2, 10)

        -- ==== Cage (top floor) ====
        stairs_down_second("climbDownToFloor1")
        stairs_down_first("climbDownToGround-cage")
        t.exec("fixCageGetBars", t.player.click_loc, "tol_crate10", 1)
        t.exec("fixCageGetBars-has", t.inv.await, "tol_bar", 5, 8)
        t.exec("fixCageGetFluid", t.player.click_loc, "tol_crate08", 1)
        t.exec("fixCageGetFluid-has", t.inv.await, "tol_glue", 4, 8)
        stairs_up_ground("climbUpToFloor1-cage")
        stairs_up_first("climbUpToFloor2-cage")
        ladder_up_second("climbUpToFloor3")
        t.exec("buildCage", t.player.click_loc, "tol_cage_multi", 1)
        t.exec("buildCage-dialog", t.chat.play, {
            "mesbox:The cage appears unfinished.",
            "mesbox:5 metal bars and 4 bottles of binding fluid.",
            "choose:Yes",
            "mesbox:You built the cage!",
            "mesbox:Some of the bars need to be completed",
        })
        t.exec("buildCage-built", t.var.await, "varb3340_tol_cage_prog", 1, 10)
        t.exec("solveCagePuzzle-open", t.ui.await_open, "tol_cage_puzzle", 10)

        -- solveCagePuzzle: wiki Tower_of_Life oldid 15366014's quick guide, side by side, turning the cage
        -- with the right arrow after each. Each placed bar shows "You fix the part of the cage!".
        local cage_sides = {
            { "side1", { { "tol_cage_horiz", 2 }, { "tol_cage_horiz", 3 }, { "tol_cage_vert", 2 } } },
            { "side2", { { "tol_cage_horiz", 2 }, { "tol_cage_vert", 4 }, { "tol_cage_vert", 2 } } },
            { "side3", { { "tol_cage_horiz", 4 }, { "tol_cage_vert", 2 }, { "tol_cage_vert", 3 } } },
            { "side4", { { "tol_cage_horiz", 2 }, { "tol_cage_horiz", 2 }, { "tol_cage_vert", 2 } } },
        }
        for i, side in ipairs(cage_sides) do
            for j, bar in ipairs(side[2]) do
                local log = { press("tol_cage_puzzle:" .. bar[1], 0) }
                for _ = 2, bar[2] do
                    log[#log + 1] = press("tol_cage_puzzle:tol_cage_plus", 0)
                end
                log[#log + 1] = press("tol_cage_puzzle:tol_cage_confirm", 0)
                local m = t.msg.expect("You fix the part of the cage!")
                t.check("solveCagePuzzle." .. side[1] .. ".bar" .. j, m == "ok",
                    string.format("%s size %d, Place bar: %s -> %s", bar[1], bar[2], table.concat(log, " "), tostring(m)))
            end
            if i < #cage_sides then
                local r = press("tol_cage_puzzle:tol_cage_right", 0)
                t.note("solveCagePuzzle." .. side[1] .. ".turn: right arrow -> " .. r)
            end
        end
        t.exec("solveCagePuzzle-dialog", t.chat.play, {
            "mesbox:The cage is complete!",
            "mesbox:The tower should be in working order now!",
        })
        t.exec("solveCagePuzzle", t.var.await, "varb3340_tol_cage_prog", 2, 10)
        t.exec("solveCagePuzzle-cageState", t.var.await, "varb3354_tol_cage_state", 1, 10)
        t.expect("quest.stage.fixing_tower-fixed", t.quest.expect_stage("fixing_tower"))

        -- ==== talkToEffigyAgain (8 -> 10, the tower fixed): down three floors, out of the door ====
        ladder_down_top("climbBackDownToFloor2")
        stairs_down_second("climbBackDownToFloor1")
        stairs_down_first("climbBackDownToGround")
        tower_door_out("talkToEffigyAgain.doorOut")
        t.exec("goto-talkToEffigyAgain", t.player.goto_tile, 2638, 3218, 0)
        t.exec("talkToEffigyAgain", t.player.talk_to, "tol_npc_efergy01", 1)
        t.exec("talkToEffigyAgain-dialog", t.chat.play, {
            "player:I've fixed all the machinery.",
            "npc:Hurrah!",
            "player:Now listen, what does it all do?",
            "npc:To the top of the tower, fellow alchemists!",
            "player:Wait! Why does nobody listen?",
        })
        t.expect("quest.stage.tower_fixed", t.quest.expect_stage("tower_fixed"))

        -- ==== followTheAlchemists (10 -> 11): in, up three floors; the creation scene plays at the top ====
        t.exec("goto-enterTowerAgain", t.player.goto_tile, 2649, 3227, 0)
        tower_door_in("enterTowerAgain")
        stairs_up_ground("climbBackUpToFloor1")
        stairs_up_first("climbBackUpToFloor2")
        ladder_up_second("climbBackUpToFloor3")
        t.exec("followTheAlchemists-scene", t.chat.play, {
            "player:What on Gielinor is...",
            "npc:It is time, my friends!",
            "npc:A long time indeed!",
            "npc:So many hours we have worked!",
            "npc:Years of planning",
            "player:They're insane!",
            "npc:It begins!",
            "npc:It's alive!",
            "npc:We did it. A homunculus!",
            "npc:Nwwooo!",
            "player:This is terrible",
            "npc:Don't worry, it has no soul.",
            "npc:You create hurt.",
            "npc:Be still",
            "player:Stop this! Let it go!",
            "npc:Arghhh!",
            "npc:Get out of the tower",
            "npc:Flee for your lives!",
            "player:They've run away.",
        })
        t.expect("quest.stage.creation_seen", t.quest.expect_stage("creation_seen"))

        -- ==== confrontEffigy (11 -> 12): down three floors, out, Effigy ====
        ladder_down_top("climbBackDownToFloor2-confront")
        stairs_down_second("climbBackDownToFloor1-confront")
        stairs_down_first("climbBackDownToGround-confront")
        tower_door_out("confrontEffigy.doorOut")
        t.exec("goto-talkToEffigyConfront", t.player.goto_tile, 2638, 3218, 0)
        t.exec("talkToEffigyAgain-confront", t.player.talk_to, "tol_npc_efergy01", 1)
        t.exec("talkToEffigyAgain-confront-dialog", t.chat.play, {
            "player:Effigy!",
            "npc:I know, I know.",
            "player:I hope you've learnt something",
            "npc:Perhaps you could go and have a talk with it?",
            "player:Why me? You made it!",
            "npc:Pretty please?",
            "player:Fine. They do say experience teaches fools.",
        })
        t.expect("quest.stage.confronting_homunculus", t.quest.expect_stage("confronting_homunculus"))

        -- ==== confrontTheHomunculus (12 -> 14 -> 16): in, up three floors, the homunculus's questions ====
        t.exec("goto-enterTowerConfront", t.player.goto_tile, 2649, 3227, 0)
        tower_door_in("enterTowerAgain-confront")
        stairs_up_ground("climbBackUpToFloor1-confront")
        stairs_up_first("climbBackUpToFloor2-confront")
        ladder_up_second("climbBackUpToFloor3-confront")
        t.exec("talkToHomunculusTopOfTower", t.player.talk_to, "tol_homonculus_cage_broken", 1)
        -- Wiki Tower_of_Life/Quick_guide oldid 14831032, 'Magic': 2, 2, 1, 3, 3, 1, 1 -- seven magical answers
        -- take the arrow (tol_homon_align, 7) to the magic end (0).
        t.exec("talkToHomunculusTopOfTower-dialog", t.chat.play, {
            "player:Hello?",
            "npc:Leeet me free!",
            "player:It's okay, I'm here to help you.",
            "npc:Alchemists make me",
            "player:A creature made of logic and magic",
            "mesbox:You must now make sense of the Homunculus's mind.",
            "player:You say you're confused",
            "choose:With the aid of 5 fire runes.",
            "player:With the aid of 5 fire runes.",
            "choose:With the help of the magical dragonstones!",
            "player:With the help of the magical dragonstones!",
            "choose:Runecraft, enchant jewellery, perform alchemy.",
            "player:Runecraft, enchant jewellery, perform alchemy.",
            "choose:Turn them into bananas or peaches!",
            "player:Turn them into bananas or peaches!",
            "choose:Depends where you are headed, but teleport spells are a safe bet.",
            "player:Depends where you are headed",
            "choose:Yes, you can make magic potions to boost your skills.",
            "player:Yes, you can make magic potions",
            "choose:By harnessing the power of the gods!",
            "player:By harnessing the power of the gods!",
            "npc:That it! Make sense now",
            "player:I decided to root for magic.",
            "npc:Not matter which you choose.",
            "npc:Now we scare alchemists",
            "player:Sounds like a good plan.",
            "npc:Easy. You run down",
        })
        t.exec("talkToHomunculusTopOfTower-align", t.var.await_server, "varb3358_tol_homon_align", 0, 5)
        t.expect("quest.stage.scare_alchemists", t.quest.expect_stage("scare_alchemists"))

        -- ==== scareTheAlchemists (16 -> 17): down three floors, out, Effigy; the homunculus appears ====
        ladder_down_top("climbBackDownToFloor2-scare")
        stairs_down_second("climbBackDownToFloor1-scare")
        stairs_down_first("climbBackDownToGround-scare")
        tower_door_out("scareTheAlchemists.doorOut")
        t.exec("goto-talkToEffigyThird", t.player.goto_tile, 2638, 3218, 0)
        t.exec("talkToEffigyAgain-scare", t.player.talk_to, "tol_npc_efergy01", 1)
        t.exec("talkToEffigyAgain-scare-dialog", t.chat.play, {
            "player:Effigy, I need a word with you.",
            "npc:You've killed it?",
            "player:Not quite...",
            "npc:Boo.",
            "npc:Oh no, look!",
            "npc:Argh! Have mercy!",
            "npc:You set it free!",
            "npc:Me not hurt.",
            "npc:We only wanted to experiment",
            "npc:We needed our magic",
            "npc:Bad play with.",
            "npc:Right away, right away!",
            "npc:Me look dungeon",
        })
        t.expect("quest.stage.basement_homunculus", t.quest.expect_stage("basement_homunculus"))

        -- ==== talkToHomunculusInDungeon (17 -> 18): in, the trapdoor, the dungeon ====
        t.exec("goto-enterTowerDungeon", t.player.goto_tile, 2649, 3227, 0)
        tower_door_in("enterTower-dungeon")
        t.exec("openTrapdoor", t.player.click_loc, "tol_trapdoor01", 1, { at = { 2648, 3212 } })
        t.exec("openTrapdoor-open", t.var.await_server, "varb3372_tol_trapdoor_open", 1, 10)
        -- tol_basement.rs2 [oploc1,tol_trapdoor_open]: ~climb_ladder_to 3038,4376,0, beside the dungeon's
        -- ladder up -- the same level, so the landing is named.
        t.exec("climbDownToBasement", t.player.climb, { loc = "tol_trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 2648, 3212, 0 }, dest = { 3038, 4376, 0 }, slack = 0,
            same_level = "tol_basement.rs2 [oploc1,tol_trapdoor_open] ~climb_ladder_to(^tol_basement_landing)" })
        t.ticks(3)
        local snap_r, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", snap_r == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(snap_r))
        t.exec("talkToHomunculusBasement", t.player.talk_to, "tol_homonculus_nocage", 1)
        t.exec("talkToHomunculusBasement-dialog", t.chat.play, {
            "player:This place is bizarre!",
            "npc:Me know. Pleased you rescue.",
            "player:My pleasure. So what is this place?",
            "npc:They use essence of Guthix power.",
            "player:Oh dear, but you're okay now.",
            "npc:Thank you. For reward",
        })
        t.ticks(3)
        t.quest.expect_complete()

        local r_con, d_con = t.skill.expect_gain("construction", 1000, reward_before)
        t.check("reward.construction", r_con == "ok", d_con or tostring(r_con))
        local r_cra, d_cra = t.skill.expect_gain("crafting", 500, reward_before)
        t.check("reward.crafting", r_cra == "ok", d_cra or tostring(r_cra))
        local r_thi, d_thi = t.skill.expect_gain("thieving", 500, reward_before)
        t.check("reward.thieving", r_thi == "ok", d_thi or tostring(r_thi))

        -- Out the way the dungeon is left: its ladder up (maplink_landings.dbrow maplink_0_47_68_30_24_tolup)
        -- to beside the trapdoor, then the tower door.
        t.exec("leaveDungeon", t.player.climb, { loc = "area_sanguine_ghetto_ladder_up", op = 1, op_name = "Climb-up",
            at = { 3038, 4375, 0 }, src = { 3038, 4376 }, dest = { 2648, 3213, 0 }, slack = 0,
            same_level = "maplink_landings.dbrow maplink_0_47_68_30_24_tolup" })
        tower_door_out("leaveTower.doorOut")

        t.finish(0)
    end,
}
