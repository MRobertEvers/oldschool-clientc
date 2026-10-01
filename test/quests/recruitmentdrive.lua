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
        "::setlevel attack 20",
        "::setlevel strength 20",
        "::setlevel defence 20",
        "::setlevel hitpoints 20",
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

        -- ---- Starting out: climb to Sir Amik Varze (2nd floor Falador Castle) ----
        -- climbBottomSteps / climbSecondSteps: ladder/stair ObjectSteps --
        -- goto_tile the destination WITH its level is the whole of it
        -- (QUEST_AUTHORING.md section 2's "Floors and ladders" rule); no
        -- click_loc on fai_falador_castle_spiralstairs.
        t.exec("goto-talkToSirAmikVarze", t.player.goto_tile, 2960, 3336, 2)
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
        t.exec("goto-talkToSirTiffy", t.player.goto_tile, 2997, 3373, 0)
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
        t.ticks(3)
        t.expect("quest.stage.testing", t.quest.expect_stage("testing"))

        -- ==================================================================
        -- Room 1: Sir Spishyus -- fox, chicken and grain river crossing.
        -- ==================================================================
        t.exec("goto-talkToSpishyus", t.player.goto_tile, 2490, 4972, 0)
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
        -- rd_bridge_left sits on the starting ("west", x>=2479) bank;
        -- rd_bridge_right on the far ("east") bank -- both call the same
        -- rd_spishyus_cross.
        t.exec("moveChickenOnRightToLeft-pickup", t.player.click_loc, "rd_room2_chicken_multi", 1)
        t.exec("moveChickenOnRightToLeft-cross", t.player.click_loc, "rd_bridge_left", 1)
        t.exec("moveChickenOnRightToLeft-drop", t.player.inv_op, "rd_chicken", 5)
        t.exec("spishyus.return1-empty", t.player.click_loc, "rd_bridge_right", 1)
        t.exec("moveFoxOnRightToLeft-pickup", t.player.click_loc, "rd_room2_fox_multi", 1)
        t.exec("moveFoxOnRightToLeft-cross", t.player.click_loc, "rd_bridge_left", 1)
        t.exec("moveFoxOnRightToLeft-drop", t.player.inv_op, "rd_fox", 5)
        t.exec("moveChickenOnLeftToRight-pickup", t.player.click_loc, "rd_room2_chicken_multi_right", 1)
        t.exec("moveChickenOnLeftToRight-cross", t.player.click_loc, "rd_bridge_right", 1)
        t.exec("moveChickenOnLeftToRight-drop", t.player.inv_op, "rd_chicken", 5)
        t.exec("moveGrainOnRightToLeft-pickup", t.player.click_loc, "rd_room2_grain_multi", 1)
        t.exec("moveGrainOnRightToLeft-cross", t.player.click_loc, "rd_bridge_left", 1)
        t.exec("moveGrainOnRightToLeft-drop", t.player.inv_op, "rd_sack", 5)
        -- Run 2: this one crossing still photographs the void corner (the
        -- click re-frames its own pose), so aim the camera after the click,
        -- before the shot (eadgar.lua's enterStronghold shape).
        local return2_result, return2_detail = t.player.click_loc("rd_bridge_right", 1)
        t.drive.camera(1024, 383, 400)
        t.check("spishyus.return2-empty", return2_result == "ok",
            "click_loc(rd_bridge_right) -> " .. tostring(return2_result) .. " " .. tostring(return2_detail))
        t.exec("moveChickenOnRightToLeftAgain-pickup", t.player.click_loc, "rd_room2_chicken_multi", 1)
        t.exec("moveChickenOnRightToLeftAgain-cross", t.player.click_loc, "rd_bridge_left", 1)
        t.exec("moveChickenOnRightToLeftAgain-drop", t.player.inv_op, "rd_chicken", 5)
        t.ticks(2)
        t.check("spishyus.room1_complete", select(1, t.var.server("varb659_rd_room1_complete")) == "ok"
            and select(2, t.var.server("varb659_rd_room1_complete")) == 1,
            "var.server(rd_room1_complete) -> " .. tostring(select(1, t.var.server("varb659_rd_room1_complete"))) .. " " .. tostring(select(2, t.var.server("varb659_rd_room1_complete"))))

        t.drive.camera(0, 128, 600) -- boot follow pose back for the later rooms
        t.exec("leaveSirSpishyusRoom", t.player.click_loc, "rd_room1_exitdoor", 1)
        t.ticks(2)

        -- ==================================================================
        -- Room 3: Sir Kuam Ferentse -- defeat Sir Leye with the steel
        -- warhammer -- but the warhammer needs Attack 5 to WIELD, which a
        -- fresh Lumbridge character does not have ("You need to have an
        -- Attack level of 5.", measured run 2), so this room is solved
        -- bare-handed (PARITY.tsv: "bare-handed kill completes it", verified
        -- live) -- rd_leye_weapon only checks worn:rhand for a blade, and
        -- empty-handed is not one.
        -- ==================================================================
        t.exec("goto-talkToSirKuam", t.player.goto_tile, 2456, 4964, 0)
        t.exec("talkToSirKuam", t.player.talk_to, "rd_observer_room_3", 1)
        t.exec("talkToSirKuam-dialog", t.chat.play, {
            "npc:Ah, you're finally here. Your task for this room is to defeat Sir Leye.",
            "npc:He has been blessed by Saradomin so that no blade may harm him",
            "npc:There are some weapons nearby",
            "npc:If you are having problems, remember: a true warrior uses his wits as much as his brawn.",
        })
        t.ticks(2)
        -- run 1 measured bare-handed: 30/30 -> 15/30 hp in 80 ticks (real
        -- progress, not a stall -- 0 re-engagements needed); budget for the
        -- full kill.
        t.exec("killSirLeye-attack", t.player.attack, "rd_combat_npc_room_3", 2, 20)
        t.exec("killSirLeye-dead", t.npc.await_dead_engaged, 300, 30)
        t.check("killSirLeye.room3_complete", select(1, t.var.server("varb661_rd_room3_complete")) == "ok"
            and select(2, t.var.server("varb661_rd_room3_complete")) == 1,
            "var.server(rd_room3_complete) -> " .. tostring(select(1, t.var.server("varb661_rd_room3_complete"))) .. " " .. tostring(select(2, t.var.server("varb661_rd_room3_complete"))))

        t.exec("leaveSirKuamRoom", t.player.click_loc, "rd_room3_exitdoor", 1)
        t.ticks(2)

        -- ==================================================================
        -- Room 4: Sir Tinley -- patience (wait out the softtimer).
        -- ==================================================================
        t.exec("goto-talkToSirTinley", t.player.goto_tile, 2472, 4956, 0)
        t.exec("talkToSirTinley", t.player.talk_to, "rd_observer_room_4", 1)
        t.exec("talkToSirTinley-dialog", t.chat.play, {
            "npc:Ah, welcome. I have but one clue for you to pass this room's puzzle",
        })
        -- rd_tinley_wait_ticks = 15; doNothingStep: do not re-talk or fidget
        -- during the wait (recruitmentdrive_tinley.rs2 [softtimer,rd_tinley_wait]).
        -- t.ticks is hollow (trap 12) -- call it directly, not through t.exec.
        t.ticks(18)
        -- The softtimer's own verdict page (~chatnpc_specific from a
        -- softtimer, no protected player) can hang -- rd_room4_complete is
        -- already set before that call runs, so close defensively and move on.
        t.check("doNothingStep.room4_complete", select(1, t.var.server("varb662_rd_room4_complete")) == "ok"
            and select(2, t.var.server("varb662_rd_room4_complete")) == 1,
            "var.server(rd_room4_complete) -> " .. tostring(select(1, t.var.server("varb662_rd_room4_complete"))) .. " " .. tostring(select(2, t.var.server("varb662_rd_room4_complete"))))
        t.check("doNothingStep.close_hung_page", t.chat.close())

        t.exec("leaveSirTinleyRoom", t.player.click_loc, "rd_room4_exitdoor", 1)
        t.ticks(2)

        -- ==================================================================
        -- Room 2: Lady Table -- statue memory/observation.
        -- ==================================================================
        t.exec("goto-talkToLadyTable", t.player.goto_tile, 2460, 4979, 0)
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
        t.check("ladyTableStep.read_answer", table_answer_result == "ok",
            "var.server(rd_templock_1) -> " .. tostring(table_answer_result) .. " " .. tostring(table_answer))
        -- recruitmentdrive_table.rs2 index map (1..12), [oploc1,...] rows.
        local rd_table_symbols = {
            [1] = "rd_2b", [2] = "rd_2s", [3] = "rd_2g",
            [4] = "rd_1b", [5] = "rd_1s", [6] = "rd_1g",
            [7] = "rd_4g", [8] = "rd_4s", [9] = "rd_4b",
            [10] = "rd_3b", [11] = "rd_3s", [12] = "rd_3g",
        }
        local rd_table_target = rd_table_symbols[table_answer]
        -- The window closes and the multiloc's -1 rung re-places (fix
        -- 57b4ff6a1, Lady Table selftest) when rd_room_order returns to 0;
        -- rd_table_touch refuses "Memorise the statues first." until then.
        local table_window_result, table_window_detail = t.var.await_server("varb658_rd_room_order", 0, 20)
        t.check("ladyTableStep.window_closed", table_window_result == "ok",
            "var.await_server(rd_room_order, 0) -> " .. tostring(table_window_result) .. " " .. tostring(table_window_detail))
        t.exec("pwLadyTableStep", t.player.click_loc, rd_table_target, 1)
        t.ticks(2)
        t.exec("pwLadyTableStep-dialog", t.chat.play, {
            "npc:Excellent work. Please step through the portal to meet your next challenge.",
        })
        t.check("pwLadyTableStep.room2_complete", select(1, t.var.server("varb660_rd_room2_complete")) == "ok"
            and select(2, t.var.server("varb660_rd_room2_complete")) == 1,
            "var.server(rd_room2_complete) -> " .. tostring(select(1, t.var.server("varb660_rd_room2_complete"))) .. " " .. tostring(select(2, t.var.server("varb660_rd_room2_complete"))))

        t.exec("leaveLadyTableRoom", t.player.click_loc, "rd_room2_exitdoor", 1)
        t.ticks(2)

        -- ==================================================================
        -- Room 5: Sir Ren Itchood -- acrostic clue + combination lock (285).
        -- ==================================================================
        t.exec("goto-sirRenStep.talkToRen", t.player.goto_tile, 2439, 4956, 0)
        local ren_clue_result, ren_clue = t.var.server("varb666_rd_templock_1")
        t.check("sirRenStep.read_clue", ren_clue_result == "ok",
            "var.server(rd_templock_1) -> " .. tostring(ren_clue_result) .. " " .. tostring(ren_clue))
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
        local ren_word = rd_ren_words[ren_clue]
        t.exec("sirRenStep.talkToRen", t.player.talk_to, "rd_observer_room_5", 1)
        local ren_dialog = {
            "npc:Greetings friend, and welcome here, you'll find my puzzle not so clear.",
            "npc:Hidden amongst my words, it's true, the password for the door as a clue.",
            "choose:Can I have the clue for the door?",
            rd_ren_clue_lines[ren_clue],
        }
        t.exec("sirRenStep.talkToRen-dialog", t.chat.play, ren_dialog)
        t.ticks(2)

        t.exec("sirRenStep.tryOpenDoor", t.player.click_loc, "rd_room5_exitdoor", 1)
        t.ticks(2)

        -- Dial the four wheels (rd_combolock:rda..rdd), each starting at 'A'
        -- (index 0), left/right stepping mod 26 (recruitmentdrive_ren.rs2
        -- rd_ren_combolock_step) -- no per-press row (t.ui.invoke is hollow
        -- on success, trap 12); betweenarock.lua's dwarf_rock_schematics
        -- puzzle is the precedent for this un-rowed press-loop shape.
        local ren_wa_r, ren_wa_left = t.ui.widget("rd_combolock:rda_left")
        local _, ren_wa_right = t.ui.widget("rd_combolock:rda_right")
        local _, ren_wb_left = t.ui.widget("rd_combolock:rdb_left")
        local _, ren_wb_right = t.ui.widget("rd_combolock:rdb_right")
        local _, ren_wc_left = t.ui.widget("rd_combolock:rdc_left")
        local _, ren_wc_right = t.ui.widget("rd_combolock:rdc_right")
        local _, ren_wd_left = t.ui.widget("rd_combolock:rdd_left")
        local _, ren_wd_right = t.ui.widget("rd_combolock:rdd_right")
        local _, ren_wenter = t.ui.widget("rd_combolock:rdenter")
        t.check("sirRenStep.pwEnterDoorCode-widgets", ren_wa_r == "ok",
            "ui.widget(rd_combolock:rda_left) -> " .. tostring(ren_wa_r))

        local ren_wheel_rights = { ren_wa_right, ren_wb_right, ren_wc_right, ren_wd_right }
        local ren_wheel_lefts = { ren_wa_left, ren_wb_left, ren_wc_left, ren_wd_left }
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
        end
        t.ticks(2)
        t.ui.invoke(ren_wenter, 1)
        t.ticks(2)
        t.exec("sirRenStep.pwEnterDoorCode-dialog", t.chat.play, {
            "npc:Your wit is sharp, your brains quite clear",
        })
        t.check("sirRenStep.room5_complete", select(1, t.var.server("varb663_rd_room5_complete")) == "ok"
            and select(2, t.var.server("varb663_rd_room5_complete")) == 1,
            "var.server(rd_room5_complete) -> " .. tostring(select(1, t.var.server("varb663_rd_room5_complete"))) .. " " .. tostring(select(2, t.var.server("varb663_rd_room5_complete"))))

        t.exec("sirRenStep.leaveRoom", t.player.click_loc, "rd_room5_exitdoor", 1)
        t.ticks(2)

        -- ==================================================================
        -- Room 6: Miss Cheevers -- gather, the stone door, the bronze key.
        -- ==================================================================
        t.exec("goto-talkToMissCheevers", t.player.goto_tile, 2467, 4940, 0)
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

        t.exec("goto-getWire", t.player.goto_tile, 2475, 4942, 0)
        t.exec("getWire", t.player.click_loc, "rd_small_crates", 1)
        t.ticks(2)
        t.check("getWire.has", t.inv.expect_has("rd_wire", 1))
        t.exec("goto-getTin", t.player.goto_tile, 2476, 4942, 0)
        t.exec("getTin", t.player.click_loc, "rd_large_crate", 1)
        t.ticks(2)
        t.check("getTin.has", t.inv.expect_has("rd_tin", 1))
        t.exec("getShears", t.player.click_loc, "rd_chest_closed", 1)
        t.ticks(2)
        t.check("getShears.has", t.inv.expect_has("rd_shears", 1))
        t.exec("goto-getChisel", t.player.goto_tile, 2475, 4937, 0)
        t.exec("getChisel", t.player.click_loc, "rd_large_crates", 1)
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

        local rd_bunsen = t.player.by_symbol("loc", "rd_wooden_table_bunsen_burner")
        local rd_door = t.player.by_symbol("loc", "rd_stone_door")
        local rd_keychained = t.player.by_symbol("loc", "rd_key_chained")
        t.exec("useSpadeOnBunsenBurner", t.player.use_on, "rd_metal_spade", rd_bunsen)
        t.ticks(2)
        t.check("useSpadeOnBunsenBurner.head", t.inv.expect_has("rd_metal_spade_no_handle", 1))
        t.exec("useSpadeHeadOnDoor", t.player.use_on, "rd_metal_spade_no_handle", rd_door)
        t.exec("useCupricSulfateOnDoor", t.player.use_on, "rd_cupric_sulphate", rd_door)
        t.exec("useVialOfLiquidOnDoor", t.player.use_on, "rd_dihydrogen_monoxide", rd_door)
        t.ticks(2)
        local rs, vs = t.var.server("varb686_rd_room6_stone_door")
        t.check("useVialOfLiquidOnDoor.state2", rs == "ok" and vs == 2, "var.server(rd_room6_stone_door) -> " .. tostring(rs) .. " " .. tostring(vs))
        t.exec("openDoor", t.player.click_loc, "rd_stone_door", 1)
        t.ticks(2)
        local ro, vo = t.var.server("varb686_rd_room6_stone_door")
        t.check("openDoor.state3", ro == "ok" and vo == 3, "var.server(rd_room6_stone_door) -> " .. tostring(ro) .. " " .. tostring(vo))

        t.exec("useVialOfLiquidOnCakeTin", t.player.use_item_on_item, "rd_dihydrogen_monoxide", "rd_tin")
        t.exec("useGypsumOnTin", t.player.use_item_on_item, "rd_gypsum", "rd_tin")
        t.ticks(2)
        t.check("useGypsumOnTin.tinfull", t.inv.expect_has("rd_tinfull", 1))
        t.exec("useTinOnKey", t.player.use_on, "rd_tinfull", rd_keychained)
        t.exec("useCupricOrePowderOnTin", t.player.use_item_on_item, "rd_keymould", "rd_copper_ore_powder")
        t.exec("useTinOrePowderOnTin", t.player.use_item_on_item, "rd_full_keymould_copper", "rd_tin_ore_powder")
        t.exec("useTinOnBunsenBurner", t.player.use_on, "rd_full_keymould_unheated", rd_bunsen)
        t.exec("useEquipmentOnTin", t.player.use_item_on_item, "rd_wire", "rd_full_keymould_complete")
        t.ticks(2)
        t.check("pwMissCheeversStep.key", t.inv.expect_has("rd_puzzleroom_key", 1))

        t.exec("goto-walkThroughStoneDoor", t.player.goto_tile, 2476, 4940, 0)
        t.exec("walkThroughStoneDoor", t.player.click_loc, "rd_stone_door", 1)
        t.ticks(2)
        local tr, tile = t.world.tile()
        t.check("walkThroughStoneDoor.tile", tr == "ok" and tile ~= nil and tile.x == 2478 and tile.z == 4940,
            "world.tile -> " .. tostring(tr) .. " " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z))
        t.exec("leaveMissCheeversRoom", t.player.click_loc, "rd_room6_exitdoor", 1)
        t.ticks(3)
        t.check("pwMissCheeversStep.room6_complete", select(1, t.var.server("varb664_rd_room6_complete")) == "ok"
            and select(2, t.var.server("varb664_rd_room6_complete")) == 1,
            "var.server(rd_room6_complete) -> " .. tostring(select(2, t.var.server("varb664_rd_room6_complete"))))

        -- ==================================================================
        -- Room 7: Ms Hynn Terprett -- riddle (random of five).
        -- ==================================================================
        t.exec("goto-pwMsHynnTerprett", t.player.goto_tile, 2451, 4935, 0)
        local hynn_r, hynn_riddle = t.var.server("varb666_rd_templock_1")
        t.check("pwMsHynnTerprett.read_riddle", hynn_r == "ok",
            "var.server(rd_templock_1) -> " .. tostring(hynn_r) .. " " .. tostring(hynn_riddle))
        -- recruitmentdrive_hynn.rs2 [opnpc1,rd_observer_room_7]: five riddles
        -- (0..4), correct choice text per branch.
        local hynn_lines = {
            [0] = { "npc:I estimate there to be one million inhabitants", "npc:What number would you get if you multiply the number of fingers", "choose:0" },
            [1] = { "npc:Which of the following statements is true?", "choose:The number of false statements here is three." },
            [2] = { "npc:I have both a husband and daughter.", "npc:In twenty years time, he will be twice as old", "choose:10" },
            [3] = { "npc:You may pick your own demise", "npc:Which fate would you be wise to choose?", "choose:The wolves." },
            [4] = { "npc:I dropped four identical stones", "npc:Which bucket's stone dropped to the bottom last?", "choose:Bucket A (32 degrees)" },
        }
        t.exec("pwMsHynnTerprett", t.player.talk_to, "rd_observer_room_7", 1)
        local hynn_dialog = { "npc:Greetings. I am here to test your wits with a simple riddle." }
        for _, line in ipairs(hynn_lines[hynn_riddle]) do
            table.insert(hynn_dialog, line)
        end
        t.exec("pwMsHynnTerprett-dialog", t.chat.play, hynn_dialog)
        t.check("pwMsHynnTerprett.room7_complete", select(1, t.var.server("varb665_rd_room7_complete")) == "ok"
            and select(2, t.var.server("varb665_rd_room7_complete")) == 1,
            "var.server(rd_room7_complete) -> " .. tostring(select(1, t.var.server("varb665_rd_room7_complete"))) .. " " .. tostring(select(2, t.var.server("varb665_rd_room7_complete"))))

        t.exec("leaveMsHynnTerprettRoom", t.player.click_loc, "rd_room7_exitdoor", 1)
        t.ticks(3)
        t.expect("quest.stage.passed", t.quest.expect_stage("passed"))

        -- ---- Back to Sir Tiffy in Falador Park: hand in and complete. ----
        t.exec("goto-talkToSirTiffy-handin", t.player.goto_tile, 2997, 3373, 0)
        local snapshot_result, snapshot = t.skill.snapshot()
        t.step("reward.snapshot", snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot -> " .. tostring(snapshot_result)
            .. " prayer=" .. tostring(snapshot and snapshot.prayer and snapshot.prayer.xp)
            .. " herblore=" .. tostring(snapshot and snapshot.herblore and snapshot.herblore.xp)
            .. " agility=" .. tostring(snapshot and snapshot.agility and snapshot.agility.xp))
        local coins_before_result, coins_before = t.inv.count("coins")
        t.step("reward.coins_before", coins_before_result == "ok" and "PASS" or "FAIL",
            "inv.count(coins) -> " .. tostring(coins_before_result) .. " " .. tostring(coins_before))

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

