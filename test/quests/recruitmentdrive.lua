-- Recruitment Drive (Falador -> Temple Knights training grounds).
-- Content: OSRS-Content/osrs239-content/server/scripts/quests/quest_recruitmentdrive/.
-- Port note (recruitmentdrive.rs2:4-8): only 5-of-7 random rooms + random
-- order per the real wiki quest (Sir Leye mandatory) is DEFERRED -- this
-- port always runs all 7 rooms in a FIXED order: Spishyus(1) -> Kuam(3) ->
-- Tinley(4) -> Lady Table(2) -> Ren(5) -> Cheevers(6) -> Hynn(7). Every guide
-- step from Quest Helper's RecruitmentDrive.java is still driven below, just
-- in the order this port's own scripts actually chain them, not Quest
-- Helper's per-zone panel order.
--
-- Travel (door rule, b61): the White Knights' Castle is entered by its
-- double door and both spiral staircases are climbed, up and down. Inside the
-- training grounds there is NO goto: every room is entered by the content's
-- own teleport -- Sir Tiffy's dialogue (recruitmentdrive.rs2 rd_enter_grounds
-- -> ^rd_room_spishyus) and each room's exit door ([oploc1,rd_roomN_exitdoor]
-- -> ~rd_enter_<next>: p_delay(1), p_teleport(^rd_room_<next>)). Each exit door
-- is pressed through t.player.pass_door from its inside tile and graded on the
-- next room's landing tile (recruitmentdrive.constant, 0_38_77 = 2432,4928);
-- Ms Hynn's door lands in Falador Park (^rd_park 0_46_52_45_42 = 2989,3370).
-- Sir Spishyus' bridge and Miss Cheevers' stone door are crossed by their own
-- op (t.player.cross_trap), graded on the content's landing tile.
--
-- Miss Cheevers' room (recruitmentdrive_cheevers.rs2, ported for real in
-- seam26): every item is gathered from the loc Quest Helper's
-- MissCheeversStep.java names (bookshelves, shelves, crates, chest, the spade
-- off the table), and each of its sub-steps is a row below.

return {
    id = "recruitmentdrive",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::complete quest_blackknightsfortress", -- prerequisite (getGeneralRequirements); quest_cheat.rs2's own dbrow name
        "::complete quest_druidicritual", -- prerequisite (getGeneralRequirements); quest_cheat.rs2's own dbrow name
        -- getCombatRequirements: "Sir Leye (level 20) with no items". A fresh
        -- level-3 character with 10 hitpoints won that fight bare-handed only
        -- while the world's single random stream happened to favour it; on
        -- the player's own stream (seam28) Sir Leye killed him at tick 201.
        -- Staged to a character that meets the stated requirement.
        -- owner ruling 2026-10-06: bare-handed, stats staged 25 for margin.
        -- Combat-level branch check: quest_recruitmentdrive/scripts has no read of
        -- combat level or Attack/Strength/Defence/Hitpoints (only the npc_type test
        -- at recruitmentdrive_kuam.rs2:60), so the staged stats change nothing but the fight.
        "::setlevel attack 25",
        "::setlevel strength 25",
        "::setlevel defence 25",
        "::setlevel hitpoints 25",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb657_rd_main",
            constants = {
                not_started = 0,
                referred = 1,
                testing = 2,
                passed = 3,
                complete = 4,
            },
            display = "Recruitment Drive",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function tile_text(r, tt)
            if r ~= "ok" or type(tt) ~= "table" then
                return tostring(r)
            end
            return tt.x .. "," .. tt.z .. "," .. tt.level
        end

        -- A content teleport (a dialogue's, not a press the test can name a
        -- door for) graded on its exact landing tile.
        local function landed(name, x, z, what, ticks)
            t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and tt.x == x and tt.z == z and tt.level == 0
                end,
                note = what,
            }, ticks or 12)
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and tt.x == x and tt.z == z and tt.level == 0,
                "t.world.tile() -> " .. tile_text(r, tt) .. " (want " .. x .. "," .. z .. ",0: " .. what .. ")")
        end

        -- A training-room exit door: pressed from its inside tile, graded on
        -- the next room's landing (the content's p_teleport, not a walk).
        local function exit_door(name, door, x, z, far_x, far_z, far_desc)
            t.exec(name, t.player.pass_door, { closed = door, at = { x, z, 0 }, near = { x, z },
                far = { far_x, far_z }, far_desc = far_desc })
        end

        -- ---- Starting out: climb to Sir Amik Varze (2nd floor Falador Castle) ----
        -- The White Knights' Castle (blackknight.lua's route): the goto lands
        -- in the open courtyard 2968,3338 (reach.py walks it to Falador
        -- square and the park with every door shut); the west keep holding
        -- the spiral stairs is behind the double door
        -- fai_falador_castledoubledoorl/r at 2965,3338-3339 (courtyard
        -- x >= 2965, keep x <= 2964), crossed on foot both ways. The
        -- staircases are maplink rows (ladders_stairs maplink.dbrow
        -- maplink_0_46_52_11_9_up / 1_46_52_16_12_up / 2_46_52_15_11_down /
        -- 1_46_52_12_10_down), graded on their dest tiles.
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
        -- areas/falador/scripts/sir_amik_varze.rs2 label
        -- black_knights_fortress_sir_amik_postquest (spy=complete via setup,
        -- rd_main=not_started): opens with the player's own line, then the
        -- "any other quests" -> "Yes please" branch sets rd_main=referred.
        t.exec("talkToSirAmikVarze-dialog", t.chat.play, {
            "player:Hello Sir Amik.",
            "npc:Hello, friend!",
            "choose:Do you have any other quests for me to do?",
            "player:Do you have any other quests for me to do?",
            "npc:Quests, eh? Well, I don't have anything on the go at the moment",
            "npc:Your work sorting out those Black Knights means I will happily write you a letter of recommendation.",
            "npc:Would you like me to put your name forwards to them?",
            "choose:Yes please",
            "player:Sure thing Sir Amik, sign me up!",
            "npc:Erm, well, this is a little embarrassing",
            "npc:They are the Temple Knights, and you are to meet Sir Tiffy Cashien in Falador park for testing immediately.",
            "player:Okey dokey, I'll go do that then.",
        })
        t.ticks(2)
        t.expect("quest.stage.referred", t.quest.expect_stage("referred"))

        -- ---- Start the testing: Sir Tiffy Cashien, Falador Park ----
        t.exec("climbDownSecondFloorStaircase", t.player.climb, { loc = "fai_falador_castle_spiralstairstop",
            op = 1, op_name = "Climb-down", at = { 2960, 3339, 2 }, dest = { 2960, 3340, 1 } })
        t.exec("climbDownfirstFloorStaircase", t.player.climb, { loc = "fai_falador_castle_spiralstairstop",
            op = 1, op_name = "Climb-down", at = { 2955, 3338, 1 }, dest = { 2955, 3337, 0 } })
        t.exec("castleDoorOut", t.player.pass_door, { closed = "fai_falador_castledoubledoorl",
            open = "fai_falador_opencastledoubledoorl", at = { 2965, 3338, 0 }, near = { 2963, 3338 },
            far = { 2968, 3338 }, far_ok = function(tt) return tt.x >= 2965 end,
            far_desc = "back in the open courtyard, x >= 2965" })
        -- Courtyard -> Falador Park: open ground both ends (reach.py
        -- 2968,3338 -> 2989,3370 REACH with every door shut); 2989,3370 is
        -- the park tile the content itself lands players on (^rd_park).
        t.exec("goto-talkToSirTiffy", t.player.goto_tile, 2989, 3370, 0)
        t.exec("talkToSirTiffy", t.player.talk_to, "rd_teleporter_guy", 1)
        -- recruitmentdrive.rs2 [opnpc1,rd_teleporter_guy], rd_main=referred
        -- branch, "Yes, let's go!" -> label rd_tiffy_try_enter (inv+worn
        -- empty from ::clearinv/no gear given) -> rd_main=testing ->
        -- rd_enter_grounds.
        t.exec("talkToSirTiffy-dialog", t.chat.play, {
            "player:Sir Amik Varze sent me to meet you here for some sort of testing",
            "npc:Ah! Amik told me all about you",
            "npc:Well, a top-notch adventurer like yourself is just the sort we've been looking for.",
            "npc:So, are you ready to begin testing?",
            "choose:Yes, let's go!",
            "player:Yeah, this sounds right up my street. Let's go!",
            "npc:Jolly good show! Now the training grounds location is a secret, so",
        })
        -- rd_enter_grounds: two p_delay(1) then p_teleport(^rd_room_spishyus).
        landed("talkToSirTiffy.landedInSpishyusRoom", 2490, 4972, "Sir Spishyus' room, ^rd_room_spishyus")
        t.expect("quest.stage.testing", t.quest.expect_stage("testing"))

        -- ==================================================================
        -- Room 1: Sir Spishyus -- fox, chicken and grain river crossing.
        -- ==================================================================
        t.exec("talkToSpishyus", t.player.talk_to, "rd_observer_room_1", 1)
        t.exec("talkToSpishyus-dialog", t.chat.play, {
            "npc:Ah, welcome.",
            "player:Hello there. What am I supposed to be doing in this room?",
            "npc:Well, your task is to take this fox, this chicken and this bag of grain across that bridge",
            "npc:Firstly, you may only carry one of the objects across at a time",
            "npc:Secondly, the fox wants to eat the chicken, and the chicken wants to eat the grain.",
            "player:Okay, I'll see what I can do.",
        })
        t.ticks(2)
        -- The bridge room's default pose photographs unrendered void in the
        -- checked top-left corner (pre-login fingerprint, runs 1-3): look
        -- down from the far side instead (eadgar.lua's precedent).
        t.drive.camera(1024, 383, 400)

        -- Classic river-crossing solution, 7 crossings (verified against
        -- recruitmentdrive_spishyus.rs2's rd_spishyus_fail conditions):
        -- 1. chicken over.  2. return empty.  3. fox over.  4. chicken back.
        -- 5. grain over.  6. return empty.  7. chicken over again.
        -- The items start on the x >= 2479 bank (the content's "west",
        -- rd_spishyus_on_west); rd_bridge_left is pressed from that bank and
        -- the content p_teleports to ^rd_spishyus_east 2476,4972;
        -- rd_bridge_right from the x < 2479 bank lands on ^rd_spishyus_west
        -- 2484,4972 (rd_spishyus_cross). The bridge is crossed only by its
        -- op (a river between the banks), so each crossing is cross_trap:
        -- the player ON the bank's landing tile before the press and ON the
        -- far bank's after it. One press: a refused crossing (a fail) is a
        -- teleport to the park, never retried.
        -- The bridge locs stand on RAW level 1 (a bridge deck: the client's
        -- pool answered "nearest copies: 2483,4972,1", run 1) while the
        -- player walks level 0, and t.player.cross_trap takes the player's
        -- level from spec.at -- it cannot name this copy (verb gap, reported).
        -- So the crossing is the same two-tile grade written out: ON the
        -- bank's landing before the press, ON the far bank's after it.
        local BRIDGE_LEFT = { 2483, 4972, 1 }
        local BRIDGE_RIGHT = { 2477, 4972, 1 }
        local function cross_bridge(name, sym, at, src_x, src_z, dest_x, dest_z, after_press)
            t.player.walk_to(src_x, src_z, 12)
            local fr, from = t.world.tile()
            local cr, cd = t.player.click_loc(sym, 1, { at = at })
            t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and tt.x == dest_x and tt.z == dest_z and tt.level == 0
                end,
                note = sym .. " lands " .. dest_x .. "," .. dest_z,
            }, 10)
            local ar, after = t.world.tile()
            if after_press then
                after_press()
            end
            t.check(name, fr == "ok" and from.x == src_x and from.z == src_z and from.level == 0
                    and ar == "ok" and after.x == dest_x and after.z == dest_z and after.level == 0,
                "from " .. tile_text(fr, from) .. " (want " .. src_x .. "," .. src_z .. ",0) click_loc(" .. sym .. " at "
                    .. at[1] .. "," .. at[2] .. "," .. at[3] .. ", op1 Cross) -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; landed " .. tile_text(ar, after) .. " (want " .. dest_x .. "," .. dest_z .. ",0)")
        end
        local function cross_east(name, after_press)
            cross_bridge(name, "rd_bridge_left", BRIDGE_LEFT, 2484, 4972, 2476, 4972, after_press)
        end
        local function cross_west(name, after_press)
            cross_bridge(name, "rd_bridge_right", BRIDGE_RIGHT, 2476, 4972, 2484, 4972, after_press)
        end
        t.exec("moveChickenOnRightToLeft-pickup", t.player.click_loc, "rd_room2_chicken_multi", 1)
        cross_east("moveChickenToLeft")
        t.exec("dropChickenWest", t.player.inv_op, "rd_chicken", 5)
        cross_west("moveToEastSide1")
        t.exec("moveFoxOnRightToLeft-pickup", t.player.click_loc, "rd_room2_fox_multi", 1)
        cross_east("moveFoxToWest")
        t.exec("moveFoxOnRightToLeft-drop", t.player.inv_op, "rd_fox", 5)
        t.exec("moveChickenOnLeftToRight", t.player.click_loc, "rd_room2_chicken_multi_right", 1)
        cross_west("moveChickenOnLeftToRight-cross")
        t.exec("moveChickenOnLeftToRight-drop", t.player.inv_op, "rd_chicken", 5)
        t.exec("moveGrainOnRightToLeft-pickup", t.player.click_loc, "rd_room2_grain_multi", 1)
        cross_east("moveGrainOnRightToLeft-cross")
        t.exec("moveGrainOnRightToLeft-drop", t.player.inv_op, "rd_sack", 5)
        -- Run 2 (seam era): this one crossing photographs the void corner
        -- (the click re-frames its own pose), so aim the camera after the
        -- crossing, before the row's shot (eadgar.lua's enterStronghold shape).
        cross_west("spishyus.return2-empty", function() t.drive.camera(1024, 383, 400) end)
        t.exec("moveChickenOnRightToLeftAgain", t.player.click_loc, "rd_room2_chicken_multi", 1)
        cross_east("moveChickenToLeftAgain")
        t.exec("moveChickenOnRightToLeftAgain-drop", t.player.inv_op, "rd_chicken", 5)
        t.ticks(2)
        local room1_r, room1_v = t.var.server("varb659_rd_room1_complete")
        t.check("spishyus.room1_complete", room1_r == "ok" and room1_v == 1,
            "var.server(rd_room1_complete) -> " .. tostring(room1_r) .. " " .. tostring(room1_v))

        t.drive.camera(0, 128, 600) -- boot follow pose back for the later rooms
        exit_door("leaveSirSpishyusRoom", "rd_room1_exitdoor", 2472, 4972, 2455, 4964,
            "Sir Kuam's room, ^rd_room_kuam 2455,4964,0")

        -- ==================================================================
        -- Room 3: Sir Kuam Ferentse -- defeat Sir Leye with the steel
        -- warhammer -- but the warhammer needs Attack 5 to WIELD, which a
        -- fresh Lumbridge character does not have ("You need to have an
        -- Attack level of 5.", measured run 2), so this room is solved
        -- bare-handed (PARITY.tsv: "bare-handed kill completes it", verified
        -- live) -- rd_leye_weapon only checks worn:rhand for a blade, and
        -- empty-handed is not one.
        -- ==================================================================
        t.exec("talkToSirKuam", t.player.talk_to, "rd_observer_room_3", 1)
        t.exec("talkToSirKuam-dialog", t.chat.play, {
            "npc:Ah, you're finally here. Your task for this room is to defeat Sir Leye.",
            "npc:He has been blessed by Saradomin so that no blade may harm him",
            "npc:There are some weapons nearby",
            "npc:If you are having problems, remember: a true warrior uses his wits as much as his brawn.",
        })
        t.ticks(2)
        -- Sir Leye is a real fighter (all.npc [rd_combat_npc_room_3]
        -- stat1-4 = 18/15/18/20, attackrate 5). Hitpoints are read before
        -- and after the kill; nothing heals in a 16-tick fight (no food can
        -- exist: rd_tiffy_try_enter refuses any carried item,
        -- recruitmentdrive.rs2:112-116, and this room hands out weapons only).
        local function hp_now()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" then
                return hp.level
            end
            return nil
        end
        local hp_before = hp_now()
        t.exec("killSirLeye-attack", t.player.attack, "rd_combat_npc_room_3", 2, 20)
        t.exec("killSirLeye", t.npc.await_dead_engaged, 300, 30)
        local hp_after = hp_now()
        t.check("killSirLeye.margin", hp_after ~= nil and hp_after >= 7,
            "Sir Leye, bare-handed: hitpoints " .. tostring(hp_before) .. " before, " .. tostring(hp_after)
                .. " after (margin: lowest hp >= 7, a quarter of 25 rounded up; food: none -- the quest bars every carried item)")
        local room3_r, room3_v = t.var.server("varb661_rd_room3_complete")
        t.check("killSirLeye.room3_complete", room3_r == "ok" and room3_v == 1,
            "var.server(rd_room3_complete) -> " .. tostring(room3_r) .. " " .. tostring(room3_v))

        exit_door("leaveSirKuamRoom", "rd_room3_exitdoor", 2463, 4963, 2471, 4956,
            "Sir Tinley's room, ^rd_room_tinley 2471,4956,0")

        -- ==================================================================
        -- Room 4: Sir Tinley -- patience (wait out the softtimer).
        -- ==================================================================
        t.exec("talkToSirTinley", t.player.talk_to, "rd_observer_room_4", 1)
        t.exec("talkToSirTinley-dialog", t.chat.play, {
            "npc:Ah, welcome. I have but one clue for you to pass this room's puzzle",
        })
        -- rd_tinley_wait_ticks = 15; doNothingStep: do not re-talk or fidget
        -- during the wait (recruitmentdrive_tinley.rs2 [softtimer,rd_tinley_wait]).
        -- t.ticks is hollow (trap 12) -- call it directly, not through t.exec.
        t.ticks(18)
        local room4_r, room4_v = t.var.server("varb662_rd_room4_complete")
        t.check("doNothingStep.room4_complete", room4_r == "ok" and room4_v == 1,
            "var.server(rd_room4_complete) -> " .. tostring(room4_r) .. " " .. tostring(room4_v))
        -- The softtimer's verdict page (~chatnpc_specific from a softtimer, no
        -- protected player) can hang on "Please wait..."; room4_complete is
        -- set before it, so it is closed here, ungraded.
        t.chat.close()

        exit_door("leaveSirTinleyRoom", "rd_room4_exitdoor", 2480, 4956, 2460, 4979,
            "Lady Table's room, ^rd_room_table 2460,4979,0")

        -- ==================================================================
        -- Room 2: Lady Table -- statue memory/observation.
        -- ==================================================================
        t.exec("talkToLadyTable", t.player.talk_to, "rd_observer_room_2", 1)
        t.exec("talkToLadyTable-dialog", t.chat.play, {
            "npc:Welcome. This room will test your observation skills.",
            "npc:Study the statues closely. There is one missing statue in this room.",
            "npc:You have a short time to memorise the statues",
        })
        -- Read the answer index (1..12) directly off the server's own
        -- rd_templock_1 -- recruitmentdrive.varp declares it transmit=yes
        -- on carrier rd_rooms_tempvar, so var.server reads the live value,
        -- same shape section 3's Hynn/Ren riddle reads use.
        local table_answer_result, table_answer = t.var.server("varb666_rd_templock_1")
        t.check("ladyTableStep.read_answer", table_answer_result == "ok" and type(table_answer) == "number"
                and table_answer >= 1 and table_answer <= 12,
            "var.server(rd_templock_1) -> " .. tostring(table_answer_result) .. " " .. tostring(table_answer)
                .. " (want 1..12: recruitmentdrive_table.rs2 calc(1 + random(12)))")
        -- recruitmentdrive_table.rs2 index map (1..12), [oploc1,...] rows.
        local rd_table_symbols = {
            [1] = "rd_2b", [2] = "rd_2s", [3] = "rd_2g",
            [4] = "rd_1b", [5] = "rd_1s", [6] = "rd_1g",
            [7] = "rd_4g", [8] = "rd_4s", [9] = "rd_4b",
            [10] = "rd_3b", [11] = "rd_3s", [12] = "rd_3g",
        }
        local rd_table_target = rd_table_symbols[table_answer] or "rd_2b"
        -- The window closes and the multiloc's -1 rung re-places (fix
        -- 57b4ff6a1, Lady Table selftest) when rd_room_order returns to 0;
        -- rd_table_touch refuses "Memorise the statues first." until then.
        local table_window_result, table_window_detail = t.var.await_server("varb658_rd_room_order", 0, 20)
        t.check("ladyTableStep.window_closed", table_window_result == "ok",
            "var.await_server(rd_room_order, 0) -> " .. tostring(table_window_result) .. " " .. tostring(table_window_detail))
        -- The same softtimer queues Lady Table's prompt (queue
        -- rd_table_touch_prompt -> "Please touch the statue you think has
        -- been added."): read it before touching, or it opens over the walk
        -- to the statue and the touch is lost (branch probe 8, rd_4s: the
        -- press answered the prompt's page and room2 stayed 0).
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "Lady Table's touch prompt opens" }, 6)
        t.exec("ladyTableStep-prompt", t.chat.play, {
            "npc:Please touch the statue you think has been added.",
        })
        t.exec("ladyTableStep", t.player.click_loc, rd_table_target, 1)
        t.ticks(2)
        t.exec("ladyTableStep-dialog", t.chat.play, {
            "npc:Excellent work. Please step through the portal to meet your next challenge.",
        })
        local room2_r, room2_v = t.var.server("varb660_rd_room2_complete")
        t.check("ladyTableStep.room2_complete", room2_r == "ok" and room2_v == 1,
            "var.server(rd_room2_complete) -> " .. tostring(room2_r) .. " " .. tostring(room2_v))

        exit_door("leaveLadyTableRoom", "rd_room2_exitdoor", 2447, 4979, 2439, 4956,
            "Sir Ren Itchood's room, ^rd_room_ren 2439,4956,0")

        -- ==================================================================
        -- Room 5: Sir Ren Itchood -- acrostic clue + combination lock.
        -- ==================================================================
        local ren_clue_result, ren_clue = t.var.server("varb666_rd_templock_1")
        t.check("sirRenStep.read_clue", ren_clue_result == "ok" and type(ren_clue) == "number"
                and ren_clue >= 0 and ren_clue <= 5,
            "var.server(rd_templock_1) -> " .. tostring(ren_clue_result) .. " " .. tostring(ren_clue)
                .. " (want 0..5: recruitmentdrive_ren.rs2 rd_enter_ren calc(random(6)))")
        -- recruitmentdrive_ren.rs2 rd_ren_password: 0=BITE 1=FISH 2=LAST
        -- 3=MEAT 4=RAIN 5=TIME (this port's own order, NOT Quest Helper's
        -- live-client answers[] array, which is a different encoding).
        local rd_ren_words = { [0] = "BITE", [1] = "FISH", [2] = "LAST", [3] = "MEAT", [4] = "RAIN", [5] = "TIME" }
        local rd_ren_clue_lines = {
            [0] = "npc:Better than me, you'll not find",
            [1] = "npc:Feel the aching of your mind",
            [2] = "npc:Look closely at the words i speak",
            [3] = "npc:More than words, i have not for you",
            [4] = "npc:Rare it is that you will see",
            [5] = "npc:This riddle of mine may confuse",
        }
        local ren_word = rd_ren_words[ren_clue] or "BITE"
        t.exec("sirRenStep.talkToRen", t.player.talk_to, "rd_observer_room_5", 1)
        local ren_dialog = {
            "npc:Greetings friend, and welcome here, you'll find my puzzle not so clear.",
            "npc:Hidden amongst my words, it's true, the password for the door as a clue.",
            "choose:Can I have the clue for the door?",
            rd_ren_clue_lines[ren_clue] or rd_ren_clue_lines[0],
        }
        t.exec("sirRenStep.talkToRen-dialog", t.chat.play, ren_dialog)
        t.ticks(2)

        -- The door's first press opens the lock (rd_ren_open_combolock), it
        -- does not take the player anywhere.
        t.player.walk_to(2446, 4956, 12)
        t.exec("tryOpenDoor", t.player.click_loc, "rd_room5_exitdoor", 1, { at = { 2446, 4956, 0 } })
        t.expect("tryOpenDoor.lockOpen", t.ui.await_open("rd_combolock", 10))

        -- Dial the four wheels (rd_combolock:rda..rdd), each starting at 'A'
        -- (index 0), left/right stepping mod 26 (recruitmentdrive_ren.rs2
        -- rd_ren_combolock_step); each wheel's letter is read back off its
        -- own text component (if_settext) before Enter.
        local ren_wa_r, ren_wa_left = t.ui.widget("rd_combolock:rda_left")
        local _, ren_wa_right = t.ui.widget("rd_combolock:rda_right")
        local _, ren_wb_left = t.ui.widget("rd_combolock:rdb_left")
        local _, ren_wb_right = t.ui.widget("rd_combolock:rdb_right")
        local _, ren_wc_left = t.ui.widget("rd_combolock:rdc_left")
        local _, ren_wc_right = t.ui.widget("rd_combolock:rdc_right")
        local _, ren_wd_left = t.ui.widget("rd_combolock:rdd_left")
        local _, ren_wd_right = t.ui.widget("rd_combolock:rdd_right")
        local ren_we_r, ren_wenter = t.ui.widget("rd_combolock:rdenter")
        t.check("sirRenStep.pwEnterDoorCode-widgets", ren_wa_r == "ok" and ren_we_r == "ok",
            "ui.widget(rd_combolock:rda_left) -> " .. tostring(ren_wa_r) .. ", (rdenter) -> " .. tostring(ren_we_r))

        local ren_wheel_rights = { ren_wa_right, ren_wb_right, ren_wc_right, ren_wd_right }
        local ren_wheel_lefts = { ren_wa_left, ren_wb_left, ren_wc_left, ren_wd_left }
        local ren_wheel_texts = { "rd_combolock:rda", "rd_combolock:rdb", "rd_combolock:rdc", "rd_combolock:rdd" }
        for ren_i = 1, 4 do
            local ren_letter = string.sub(ren_word, ren_i, ren_i)
            local ren_target = string.byte(ren_letter) - string.byte("A")
            if ren_target <= 13 then
                for _ = 1, ren_target do
                    t.ui.invoke(ren_wheel_rights[ren_i], 1)
                end
            else
                for _ = 1, (26 - ren_target) do
                    t.ui.invoke(ren_wheel_lefts[ren_i], 1)
                end
            end
            t.expect("sirRenStep.pwEnterDoorCode-wheel" .. ren_i,
                t.ui.expect_text(ren_wheel_texts[ren_i], "/^" .. ren_letter .. "$/", 5))
        end
        t.ticks(2)
        t.ui.invoke(ren_wenter, 1)
        t.ticks(2)
        t.exec("sirRenStep.pwEnterDoorCode-dialog", t.chat.play, {
            "npc:Your wit is sharp, your brains quite clear",
        })
        local room5_r, room5_v = t.var.server("varb663_rd_room5_complete")
        t.check("sirRenStep.room5_complete", room5_r == "ok" and room5_v == 1,
            "var.server(rd_room5_complete) -> " .. tostring(room5_r) .. " " .. tostring(room5_v))

        exit_door("leaveRoom", "rd_room5_exitdoor", 2446, 4956, 2467, 4940,
            "Miss Cheevers' room, ^rd_room_cheevers 2467,4940,0")

        -- ==================================================================
        -- Room 6: Miss Cheevers -- gather, the stone door, the bronze key.
        -- ==================================================================
        t.exec("talkToMissCheevers", t.player.talk_to, "rd_observer_room_6", 1)
        t.exec("talkToMissCheevers-dialog", t.chat.play, {
            "npc:Welcome to my challenge.",
            "npc:All you need to do is leave from the opposite door",
            "npc:more complicated than it may at first appear.",
            "npc:limited supplies of the items in this room",
            "npc:Best of luck!",
        })
        t.ticks(2)
        -- the old port's grant: the dialogue must NOT hand anything over.
        local r0, n0 = t.inv.count("rd_cupric_sulphate")
        t.check("noGrant", r0 == "ok" and n0 == 0, "inv.count(rd_cupric_sulphate) -> " .. tostring(r0) .. " " .. tostring(n0))

        t.exec("getMagnet", t.player.click_loc, "rd_bookshelf_old_tall", 1)
        t.ticks(2)
        t.check("getMagnet.has", t.inv.expect_has("rd_magnet", 1))

        t.exec("getTwoVials", t.player.click_loc, "rd_shelves_chemicals_1", 1)
        t.exec("getTwoVials-dialog", t.chat.play, { "mesbox:There are two vials on this shelf.", "choose:Take both vials." })
        t.ticks(2)
        t.check("getTwoVials.acetic", t.inv.expect_has("rd_acetic_acid", 1))
        t.check("getTwoVials.liquid", t.inv.expect_has("rd_dihydrogen_monoxide", 1))

        t.exec("getCupricSulfate", t.player.click_loc, "rd_shelves_chemicals_2", 1)
        t.exec("getCupricSulfate-dialog", t.chat.play, { "mesbox:There is a vial on this shelf.", "choose:YES" })
        t.ticks(2)
        t.check("getCupricSulfate.has", t.inv.expect_has("rd_cupric_sulphate", 1))
        t.exec("getGypsum", t.player.click_loc, "rd_shelves_chemicals_3", 1)
        t.exec("getGypsum-dialog", t.chat.play, { "mesbox:There is a vial on this shelf.", "choose:YES" })
        t.ticks(2)
        t.check("getGypsum.has", t.inv.expect_has("rd_gypsum", 1))
        t.exec("getSodiumChloride", t.player.click_loc, "rd_shelves_chemicals_4", 1)
        t.exec("getSodiumChloride-dialog", t.chat.play, { "mesbox:There is a vial on this shelf.", "choose:YES" })
        t.ticks(2)
        t.check("getSodiumChloride.has", t.inv.expect_has("rd_sodium_chloride", 1))

        -- The guide's own copies, pressed by tile (rd_large_crate and
        -- rd_large_crates each stand twice in the room).
        t.exec("getWire", t.player.click_loc, "rd_small_crates", 1, { at = { 2475, 4943, 0 } })
        t.ticks(2)
        t.check("getWire.has", t.inv.expect_has("rd_wire", 1))
        t.exec("getTin", t.player.click_loc, "rd_large_crate", 1, { at = { 2476, 4943, 0 } })
        t.ticks(2)
        t.check("getTin.has", t.inv.expect_has("rd_tin", 1))
        t.exec("getShears", t.player.click_loc, "rd_chest_closed", 1)
        t.ticks(2)
        t.check("getShears.has", t.inv.expect_has("rd_shears", 1))
        t.exec("getChisel", t.player.click_loc, "rd_large_crates", 1, { at = { 2476, 4937, 0 } })
        t.ticks(2)
        t.check("getChisel.has", t.inv.expect_has("rd_chisel", 1))

        -- The south shelves' default pose frames the void past the wall, which
        -- the gate's pre_login fingerprint mistakes for a boot frame
        -- (s26rd_full shot 147); face the room instead, as the bridge room does.
        t.drive.camera(1024, 383, 400)
        t.exec("getNitrousOxide", t.player.click_loc, "rd_shelves_chemicals_5", 1)
        t.exec("getNitrousOxide-dialog", t.chat.play, { "mesbox:There is a vial on this shelf.", "choose:YES" })
        t.ticks(2)
        t.check("getNitrousOxide.has", t.inv.expect_has("rd_nitorus_oxide", 1))
        t.exec("getTinOrePowder", t.player.click_loc, "rd_shelves_chemicals_6", 1)
        t.exec("getTinOrePowder-dialog", t.chat.play, { "mesbox:There is a vial on this shelf.", "choose:YES" })
        t.ticks(2)
        t.check("getTinOrePowder.has", t.inv.expect_has("rd_tin_ore_powder", 1))
        t.exec("getCupricOrePowder", t.player.click_loc, "rd_shelves_chemicals_7", 1)
        t.exec("getCupricOrePowder-dialog", t.chat.play, { "mesbox:There is a vial on this shelf.", "choose:YES" })
        t.ticks(2)
        t.check("getCupricOrePowder.has", t.inv.expect_has("rd_copper_ore_powder", 1))
        t.exec("getThreeVials", t.player.click_loc, "rd_shelves_chemicals_8", 1)
        t.exec("getThreeVials-dialog", t.chat.play, { "mesbox:There are three vials on this shelf.", "choose:Take all three vials" })
        t.ticks(2)
        t.check("getThreeVials.has", t.inv.expect_has("rd_dihydrogen_monoxide", 4))
        t.exec("getKnife", t.player.click_loc, "rd_bookshelf_old_tall3", 1)
        t.ticks(2)
        t.check("getKnife.has", t.inv.expect_has("rd_knife", 1))
        t.drive.camera(0, 128, 600) -- boot follow pose back
        -- trap 12: click_obj is hollow (ok, nil detail) -- call it and step it
        local spade_result = t.player.click_obj("rd_metal_spade", 3)
        t.step("getMetalSpade", spade_result == "ok" and "PASS" or "FAIL", "click_obj rd_metal_spade -> " .. tostring(spade_result))
        t.ticks(2)
        t.check("getMetalSpade.has", t.inv.expect_has("rd_metal_spade", 1))

        -- Each "use X on Y" row is followed by the item leaving the pack and
        -- the effect recruitmentdrive_cheevers.rs2 gives it.
        local function count(item)
            local r, n = t.inv.count(item)
            return r == "ok" and n or -1
        end
        local function door_state()
            local r, v = t.var.server("varb686_rd_room6_stone_door")
            return r == "ok" and v or -1
        end
        local rd_bunsen = t.player.by_symbol("loc", "rd_wooden_table_bunsen_burner")
        local rd_door = t.player.by_symbol("loc", "rd_stone_door")
        local rd_keychained = t.player.by_symbol("loc", "rd_key_chained")
        t.exec("useSpadeOnBunsenBurner", t.player.use_on, "rd_metal_spade", rd_bunsen)
        t.ticks(2)
        t.check("useSpadeOnBunsenBurner.head", count("rd_metal_spade") == 0 and count("rd_metal_spade_no_handle") == 1,
            "rd_metal_spade " .. count("rd_metal_spade") .. " (want 0), rd_metal_spade_no_handle "
                .. count("rd_metal_spade_no_handle") .. " (want 1)")
        t.exec("useSpadeHeadOnDoor", t.player.use_on, "rd_metal_spade_no_handle", rd_door)
        t.ticks(2)
        t.check("useSpadeHeadOnDoor.inHole", count("rd_metal_spade_no_handle") == 0 and door_state() == 1,
            "rd_metal_spade_no_handle " .. count("rd_metal_spade_no_handle") .. " (want 0), rd_room6_stone_door "
                .. door_state() .. " (want 1)")
        t.exec("useCupricSulfateOnDoor", t.player.use_on, "rd_cupric_sulphate", rd_door)
        t.ticks(2)
        local react_r, react_v = t.var.server("varb687_rd_react_on_spade")
        t.check("useCupricSulfateOnDoor.poured", count("rd_cupric_sulphate") == 0 and react_r == "ok" and react_v == 1,
            "rd_cupric_sulphate " .. count("rd_cupric_sulphate") .. " (want 0), rd_react_on_spade "
                .. tostring(react_r) .. " " .. tostring(react_v) .. " (want 1)")
        t.exec("useVialOfLiquidOnDoor", t.player.use_on, "rd_dihydrogen_monoxide", rd_door)
        t.ticks(2)
        t.check("useVialOfLiquidOnDoor.state2", count("rd_dihydrogen_monoxide") == 3 and door_state() == 2,
            "rd_dihydrogen_monoxide " .. count("rd_dihydrogen_monoxide") .. " (want 3 of 4), rd_room6_stone_door "
                .. door_state() .. " (want 2)")
        t.exec("openDoor", t.player.click_loc, "rd_stone_door", 1)
        t.ticks(2)
        t.check("openDoor.state3", door_state() == 3, "rd_room6_stone_door " .. door_state() .. " (want 3)")

        t.exec("useVialOfLiquidOnCakeTin", t.player.use_item_on_item, "rd_dihydrogen_monoxide", "rd_tin")
        t.ticks(1)
        local water_r, water_v = t.var.server("varb689_rd_water_in_tin")
        t.check("useVialOfLiquidOnCakeTin.poured", count("rd_dihydrogen_monoxide") == 2 and water_r == "ok" and water_v == 1,
            "rd_dihydrogen_monoxide " .. count("rd_dihydrogen_monoxide") .. " (want 2), rd_water_in_tin "
                .. tostring(water_r) .. " " .. tostring(water_v) .. " (want 1)")
        t.exec("useGypsumOnTin", t.player.use_item_on_item, "rd_gypsum", "rd_tin")
        t.ticks(2)
        t.check("useGypsumOnTin.tinfull", count("rd_gypsum") == 0 and count("rd_tin") == 0 and count("rd_tinfull") == 1,
            "rd_gypsum " .. count("rd_gypsum") .. ", rd_tin " .. count("rd_tin") .. " (want 0, 0), rd_tinfull "
                .. count("rd_tinfull") .. " (want 1)")
        t.exec("useTinOnKey", t.player.use_on, "rd_tinfull", rd_keychained)
        t.ticks(2)
        t.check("useTinOnKey.mould", count("rd_tinfull") == 0 and count("rd_keymould") == 1,
            "rd_tinfull " .. count("rd_tinfull") .. " (want 0), rd_keymould " .. count("rd_keymould") .. " (want 1)")
        t.exec("useCupricOrePowderOnTin", t.player.use_item_on_item, "rd_keymould", "rd_copper_ore_powder")
        t.ticks(1)
        t.check("useCupricOrePowderOnTin.poured", count("rd_copper_ore_powder") == 0 and count("rd_keymould") == 0
                and count("rd_full_keymould_copper") == 1,
            "rd_copper_ore_powder " .. count("rd_copper_ore_powder") .. ", rd_keymould " .. count("rd_keymould")
                .. " (want 0, 0), rd_full_keymould_copper " .. count("rd_full_keymould_copper") .. " (want 1)")
        t.exec("useTinOrePowderOnTin", t.player.use_item_on_item, "rd_full_keymould_copper", "rd_tin_ore_powder")
        t.ticks(1)
        t.check("useTinOrePowderOnTin.poured", count("rd_tin_ore_powder") == 0 and count("rd_full_keymould_copper") == 0
                and count("rd_full_keymould_unheated") == 1,
            "rd_tin_ore_powder " .. count("rd_tin_ore_powder") .. ", rd_full_keymould_copper "
                .. count("rd_full_keymould_copper") .. " (want 0, 0), rd_full_keymould_unheated "
                .. count("rd_full_keymould_unheated") .. " (want 1)")
        t.exec("useTinOnBunsenBurner", t.player.use_on, "rd_full_keymould_unheated", rd_bunsen)
        t.ticks(2)
        t.check("useTinOnBunsenBurner.heated", count("rd_full_keymould_unheated") == 0
                and count("rd_full_keymould_complete") == 1,
            "rd_full_keymould_unheated " .. count("rd_full_keymould_unheated") .. " (want 0), rd_full_keymould_complete "
                .. count("rd_full_keymould_complete") .. " (want 1)")
        t.exec("useEquipmentOnTin", t.player.use_item_on_item, "rd_wire", "rd_full_keymould_complete")
        t.ticks(2)
        t.check("pwMissCheeversStep.key", count("rd_full_keymould_complete") == 0 and count("rd_puzzleroom_key") == 1,
            "rd_full_keymould_complete " .. count("rd_full_keymould_complete") .. " (want 0), rd_puzzleroom_key "
                .. count("rd_puzzleroom_key") .. " (want 1)")

        -- The open stone door fills the tile before the exit door's alcove;
        -- its op p_teleports from 2476,4940 to 2478,4940
        -- (rd_cheevers_walk_through).
        t.player.walk_to(2476, 4940, 12)
        t.exec("walkThroughStoneDoor", t.player.cross_trap, { loc = "rd_stone_door", op = 1, at = { 2477, 4940, 0 },
            src = { 2476, 4940 }, dest = { 2478, 4940 }, attempts = 1 })
        exit_door("leaveMissCheeversRoom", "rd_room6_exitdoor", 2478, 4940, 2451, 4935,
            "Ms Hynn Terprett's room, ^rd_room_hynn 2451,4935,0")
        local room6_r, room6_v = t.var.server("varb664_rd_room6_complete")
        t.check("pwMissCheeversStep.room6_complete", room6_r == "ok" and room6_v == 1,
            "var.server(rd_room6_complete) -> " .. tostring(room6_r) .. " " .. tostring(room6_v))

        -- ==================================================================
        -- Room 7: Ms Hynn Terprett -- riddle (random of five).
        -- ==================================================================
        local hynn_r, hynn_riddle = t.var.server("varb666_rd_templock_1")
        t.check("pwMsHynnTerprett.read_riddle", hynn_r == "ok" and type(hynn_riddle) == "number"
                and hynn_riddle >= 0 and hynn_riddle <= 4,
            "var.server(rd_templock_1) -> " .. tostring(hynn_r) .. " " .. tostring(hynn_riddle)
                .. " (want 0..4: recruitmentdrive_hynn.rs2 rd_enter_hynn calc(random(5)))")
        -- recruitmentdrive_hynn.rs2 [opnpc1,rd_observer_room_7]: five riddles
        -- (0..4), correct choice text per branch.
        local hynn_lines = {
            [0] = { "npc:I estimate there to be one million inhabitants", "npc:What number would you get if you multiply the number of fingers", "choose:0" },
            [1] = { "npc:Which of the following statements is true?", "choose:The number of false statements here is three." },
            [2] = { "npc:I have both a husband and daughter.", "npc:In twenty years time, he will be twice as old", "choose:10" },
            [3] = { "npc:You may pick your own demise", "npc:Which fate would you be wise to choose?", "choose:The wolves." },
            [4] = { "npc:I dropped four identical stones", "npc:Which bucket's stone dropped to the bottom last?", "choose:Bucket A (32 degrees)" },
        }
        t.exec("msHynnDialogQuiz", t.player.talk_to, "rd_observer_room_7", 1)
        local hynn_dialog = { "npc:Greetings. I am here to test your wits with a simple riddle." }
        for _, line in ipairs(hynn_lines[hynn_riddle] or hynn_lines[0]) do
            table.insert(hynn_dialog, line)
        end
        t.exec("msHynnDialogQuiz-dialog", t.chat.play, hynn_dialog)
        local room7_r, room7_v = t.var.server("varb665_rd_room7_complete")
        t.check("pwMsHynnTerprett.room7_complete", room7_r == "ok" and room7_v == 1,
            "var.server(rd_room7_complete) -> " .. tostring(room7_r) .. " " .. tostring(room7_v))

        exit_door("leaveMsHynnTerprettRoom", "rd_room7_exitdoor", 2452, 4943, 2989, 3370,
            "Falador Park, ^rd_park 2989,3370,0")
        t.expect("quest.stage.passed", t.quest.expect_stage("passed"))

        -- ---- Back to Sir Tiffy in Falador Park: hand in and complete. ----
        local snapshot_result, snapshot = t.skill.snapshot()
        t.step("reward.snapshot", snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot -> " .. tostring(snapshot_result)
            .. " prayer=" .. tostring(snapshot and snapshot.prayer and snapshot.prayer.experience)
            .. " herblore=" .. tostring(snapshot and snapshot.herblore and snapshot.herblore.experience)
            .. " agility=" .. tostring(snapshot and snapshot.agility and snapshot.agility.experience))
        local coins_before_result, coins_before = t.inv.count("coins")
        t.step("reward.coins_before", coins_before_result == "ok" and "PASS" or "FAIL",
            "inv.count(coins) -> " .. tostring(coins_before_result) .. " " .. tostring(coins_before))

        -- The park scene is rebuilt after the teleport: wait for Sir Tiffy in
        -- the client's pool before the press (run 2: talk_to found no npc).
        t.expect("talkToSirTiffy-handin.present", t.npc.await_present("rd_teleporter_guy", 15, 20))
        t.exec("talkToSirTiffy-handin", t.player.talk_to, "rd_teleporter_guy", 1)
        -- recruitmentdrive.rs2 rd_main=passed branch -> rd_main=complete,
        -- stat_advance x3, inv_add coins, quest_complete_rewards.
        t.exec("talkToSirTiffy-handin-dialog", t.chat.play, {
            "npc:Oh, jolly well done! Welcome to the team!",
        })
        t.ticks(3)

        -- Quest Helper's getExperienceRewards() lists 1000 XP flat per skill
        -- (not the wiki's old 1,000.5); recruitmentdrive.rs2's own
        -- stat_advance(prayer, 10005) is the *10 unit, and the client's
        -- read floors the fraction, so the delta actually observed is 1000 --
        -- matches, run 1 measured exactly this.
        t.check("reward.prayer_xp", t.skill.expect_gain("prayer", 1000, snapshot))
        t.check("reward.herblore_xp", t.skill.expect_gain("herblore", 1000, snapshot))
        t.check("reward.agility_xp", t.skill.expect_gain("agility", 1000, snapshot))
        local coins_after_result, coins_after = t.inv.count("coins")
        t.check("reward.coins_after", coins_after_result == "ok"
            and coins_before_result == "ok"
            and (coins_after - coins_before) == 3000,
            "inv.count(coins) -> " .. tostring(coins_after_result) .. " " .. tostring(coins_after)
            .. " (before " .. tostring(coins_before) .. ")")

        -- Quest Helper getItemRewards: Initiate Helm (BASIC_TK_HELM); OSRS wiki
        -- Rewards: "You will be given a sallet for free".
        t.check("reward.initiate_sallet", t.inv.expect_has("basic_tk_helm", 1))

        t.quest.expect_complete()
        t.finish(0)
        return
    end,
}
