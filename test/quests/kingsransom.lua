-- King's Ransom (Quest Helper helpers/quests/kingsransom). Written by hand against
-- the quest's own .rs2 (OSRS-Content/osrs239-content/server/scripts/quests/
-- quest_kingsransom/) and the guide; b73 author pass. Tier 4.
--
-- Sources: Quest Helper KingsRansom.java / LockpickPuzzle.java; wiki
-- King's_Ransom/Quick_guide oldid 15173607, Transcript:King's_Ransom oldid
-- 15289962, Table_(King's_Ransom) oldid 15202578.
--
-- Route and the door rule (every door, gate, stair, ladder and window is pressed
-- on every visit; long trips are real teleports, three Camelot Teleports and one
-- Falador Teleport, staged with their exact runes):
--   * Lumbridge -> Camelot Teleport (2757,3478) -> the open road north to Gossip
--     on the mansion road (2742,3555) -> the grounds gate murder_qip_metalgate
--     2741,3555 (north edge) -> the guard in the garden.
--   * The mansion's front doors are locked during the quest (kr_mansion.rs2): in
--     by the east window (murderwindow 2748,3577, op3 Break, kr_mansion.rs2
--     [oploc3,murderwindow]) into the study, then kr_sin_poshdoor 2746,3576 (N
--     edge) -> antechamber -> 2745,3575 (W edge) -> front room -> 2741,3576
--     (S edge) -> central hall (the scrap paper by the fireplace, 2746,3580) ->
--     2738,3578 (W edge) -> corridor -> spiral stairs 2736,3581 -> level 1 ->
--     kr_sin_poshdoor 2740,3578,1 (S edge) -> library (address form 2739,3581,1,
--     bookcase 2738,3580,1); the same doors back and out of the window.
--   * The Seers' courthouse: its double door kr_courthouse_double_door_l
--     2736,3472 (S edge) both ways; the court room under it is entered by the
--     stairs kr_courthouse_stairs_top (kr_court.rs2 teleports into m28_66) and
--     left by its gate kr_court_fence_door 1820,4268.
--   * The statue east of Camelot (kr_prison.rs2) puts you in Keep Le Faye's
--     cell; out by the cell door kr_underground_jail_bars_gate 1904,4273 and the
--     corridor ladder kr_underground_jail_ladder 1887,4269 into the keep, up its
--     two staircases to the Grail table; out of the keep by Camelot Teleport
--     (the keep has no walk-out: the jail laddertop answers "Why would you want
--     to go back to jail?").
--   * Wizard Cromperty's house: castledoubledoorl 2678,3325 (E edge) both ways.
--   * Falador Teleport (2965,3378) -> the open ground south of the Black
--     Knights' Fortress -> bkfortressdoor1 3016,3514 -> bksecretdoor 3016,3517
--     (full black armour, kr_fortress.rs2 ~kr_bkf_wall_refused) -> the secret
--     room's ladder kr_bkf_basement_laddertop 3016,3519 -> Arthur's basement;
--     the same way back out.
--   * Camelot Teleport -> the courtyard gate kr_camelot_metalgateclosedr
--     2758,3482 -> the castle doors kr_cam_doubledoorr 2758,3503 -> King Arthur.
--
-- Setup stages only the prerequisites and the Quest Helper bring-alongs:
-- Black Knights' Fortress, Holy Grail, Murder Mystery and One Small Favour
-- complete; Defence 65 and Magic 45 (the quest's own requirements -- no
-- dialogue on the route branches on combat level); the black full armour, the
-- bronze med helm and iron chainbody, granite, the animate rock scroll (One
-- Small Favour's); Telekinetic Grab's law and air rune; the teleports' runes.
-- `::passive` only on the incidental aggressive npcs the quest never fights: the
-- fortress's black knights and Keep Le Faye's renegade knights.

local function tile_text(r, tl)
    if r == "ok" and type(tl) == "table" then
        return tl.x .. "," .. tl.z .. "," .. tl.level
    end
    return tostring(r)
end

local function on(level, x0, x1, z0, z1)
    return function(tt)
        return tt.level == level and tt.x >= x0 and tt.x <= x1 and tt.z >= z0 and tt.z <= z1
    end
end

return {
    id = "kingsransom",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000,
    setup = {
        "::clearinv",
        "::kingsransom", -- this quest's own reset (kr_debug.rs2): state, clues, lock, court; moves no one
        "::complete quest_blackknightsfortress",
        "::complete quest_holygrail",
        "::complete quest_murdermystery",
        "::complete quest_onesmallfavour",
        "::setlevel defence 65",
        "::setlevel magic 45",
        "::give black_full_helm 1",
        "::give black_platebody 1",
        "::give black_platelegs 1",
        "::give bronze_med_helm 1",
        "::give iron_chainbody 1",
        "::give enakh_granite_small 1",
        "::give favour_animate_rock 1",
        -- 3x Camelot Teleport (5 air + 1 law), 1x Falador Teleport (3 air + 1
        -- water + 1 law), 1x Telekinetic Grab (1 air + 1 law).
        "::give airrune 19",
        "::give lawrune 5",
        "::give waterrune 1",
        "::passive aggressive_black_knight",
        "::passive kr_aggressive_black_knight",
        "::passive kr_keep_knight",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb3888_kr_quest",
            constants = {
                not_started = 0, told_by_gossip = 5, investigating = 10, evidence_delivered = 15,
                gossip_history = 20, directed_to_anna = 25, anna_agreed = 30, anna_freed = 35,
                captured = 40, in_cell = 45, met_merlin = 50, merlin_escaped = 60, escaped_cell = 65,
                have_grail = 70, have_scroll = 75, arthur_freed = 80, told_arthur = 85, complete = 90,
            },
            row = "quest_kingsransom",
            display = "King's Ransom",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")

        -- ------------------------------------------------------------ helpers
        local function stage(name)
            t.ticks(1)
            t.exec("quest.stage." .. name, t.quest.expect_stage, name)
        end

        -- A dialogue driven page by page; at each options page the next pick is
        -- chosen. close_menu: an options page with no pick left is a looping
        -- menu the player walks away from (a court witness's questions), closed.
        local function dialog(name, picks, close_menu)
            local pi, trail = 1, ""
            for _ = 1, 8 do
                if t.chat.kind() ~= "none" then break end
                t.ticks(1)
            end
            local closed = false
            for _ = 1, 16 do
                local dr, dk = t.chat.drain({ stop_at = "options", max_pages = 80 })
                trail = trail .. "drain->" .. tostring(dr) .. "/" .. tostring(dk) .. " | "
                if t.chat.kind() ~= "options" then break end
                local sel = picks[pi]
                if sel == nil then
                    if close_menu then
                        t.chat.close()
                        closed = true
                        trail = trail .. "menu closed | "
                    end
                    break
                end
                pi = pi + 1
                local cr, cd = t.chat.choose(sel)
                trail = trail .. "choose:" .. sel .. "=" .. tostring(cr) .. " | "
                if cr ~= "ok" then
                    break
                end
            end
            local done = pi - 1 == #picks and (closed or not close_menu or t.chat.kind() == "none")
            t.check(name, done, "picks used " .. (pi - 1) .. " of " .. #picks .. "; " .. trail:sub(1, 600))
            t.ticks(2)
        end

        local function at_tile(name, want_ok, want_desc, ticks)
            t.await({ level = function()
                local r, tt = t.world.tile()
                return r == "ok" and want_ok(tt)
            end, note = name .. ": " .. want_desc }, ticks or 8)
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and want_ok(tt), "world.tile -> " .. tile_text(r, tt) .. " (want " .. want_desc .. ")")
        end

        local function inv_has(name, sym, n)
            local ar = t.inv.await(sym, n, 8)
            local cr, c = t.inv.count(sym)
            t.check(name, ar == "ok" and cr == "ok" and c == n, sym .. " count " .. tostring(c) .. " (want " .. n .. "; await " .. tostring(ar) .. ")")
        end

        local function camelot_teleport(name)
            t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = name,
                runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot, tele_coord 0_43_54_5_22" })
        end

        -- Sites.
        local POSH, POSH_OPEN = "kr_sin_poshdoor", "kr_sin_poshdooropen"
        local GATE, GATE_OPEN = "murder_qip_metalgateclosedl", "murder_qip_metalgateopenl"
        local in_grounds = on(0, 2725, 2760, 3556, 3570)
        local on_road = function(tt) return tt.level == 0 and tt.z <= 3555 end
        local in_study = on(0, 2746, 2747, 3577, 3579)
        local outside_window = function(tt) return tt.level == 0 and tt.x >= 2748 end
        local in_antechamber = on(0, 2745, 2747, 3574, 3576)
        local in_front_room = on(0, 2736, 2744, 3573, 3575)
        local in_central_hall = on(0, 2738, 2747, 3576, 3582)
        local in_corridor = on(0, 2736, 2737, 3576, 3582)
        local upper_landing = function(tt) return tt.level == 1 and tt.x >= 2736 and tt.x <= 2744 and (tt.z == 3577 or (tt.x <= 2737 and tt.z <= 3580)) end
        local in_library = on(1, 2738, 2744, 3578, 3582)
        local in_courthouse = on(0, 2731, 2740, 3464, 3471)
        local outside_courthouse = function(tt) return tt.level == 0 and tt.z >= 3472 end
        local in_court = on(0, 1814, 1826, 4269, 4277)
        local in_cell = on(0, 1896, 1910, 4273, 4284)
        local in_jail_corridor = function(tt) return tt.level == 0 and tt.z <= 4272 and tt.x >= 1885 and tt.x <= 1915 end
        local in_keep_f0 = on(0, 1689, 1701, 4250, 4264)
        local in_cromperty_house = on(0, 2679, 2690, 3317, 3330)
        local outside_cromperty = function(tt) return tt.level == 0 and tt.x <= 2678 end
        local in_fortress_hall = function(tt) return tt.level == 0 and tt.z >= 3515 and tt.z <= 3516 end
        local in_secret_room = function(tt) return tt.level == 0 and tt.z >= 3517 and tt.x <= 3016 end
        local outside_fortress = function(tt) return tt.level == 0 and tt.z <= 3514 end
        local in_basement = on(0, 1862, 1873, 4229, 4246)

        local function door(name, closed, open, x, z, level, nx, nz, fx, fz, far_ok, far_desc, loc_level)
            t.exec(name, t.player.pass_door, { closed = closed, open = open, at = { x, z, level },
                near = { nx, nz }, far = { fx, fz }, far_ok = far_ok, far_desc = far_desc, loc_level = loc_level })
        end

        -- ===================================================== Investigating
        -- talkToGossip: the first trip is a real teleport (every walk from
        -- Lumbridge to Seers' opens a members' gate); Camelot Teleport lands on
        -- open ground and the road north to the mansion is open overland travel.
        camelot_teleport("talkToGossip.camelotTeleport")
        t.exec("goto-talkToGossip", t.player.goto_tile, 2741, 3552, 0)
        t.exec("talkToGossip", t.player.talk_to, "gossipy_man", 1)
        dialog("talkToGossip-dialog", { "Yes." })
        stage("told_by_gossip")

        -- talkToGuard: the garden, through the grounds gate.
        door("talkToGuard.groundsGateIn", GATE, GATE_OPEN, 2741, 3555, 0, 2741, 3554, 2741, 3557, in_grounds, "inside the grounds")
        t.exec("talkToGuard", t.player.talk_to, "murderguard", 1)
        dialog("talkToGuard-dialog", {})
        stage("investigating")

        -- breakWindow: the east window, op3 Break, carries you into the study.
        t.exec("breakWindow", t.player.cross_gate, { loc = "murderwindow", op = 3, at = { 2748, 3577, 0 },
            near = { 2748, 3577 }, far_ok = in_study, far_desc = "in the study (x 2746-2747, z 3577-3579)" })
        t.exec("breakWindow.smashed", t.var.expect, "varb3889_kr_window", 1)

        -- grabPaper: the scrap paper by the fireplace, through the house.
        door("grabPaper.studyDoorOut", POSH, POSH_OPEN, 2746, 3576, 0, 2746, 3577, 2746, 3575, in_antechamber, "in the antechamber")
        door("grabPaper.antechamberDoorOut", POSH, POSH_OPEN, 2745, 3575, 0, 2745, 3575, 2743, 3575, in_front_room, "in the front room")
        door("grabPaper.centralHallIn", POSH, POSH_OPEN, 2741, 3576, 0, 2741, 3575, 2741, 3577, in_central_hall, "in the central hall")
        t.exec("grabPaper", t.player.click_obj, "kr_clue_note", 3)
        inv_has("grabPaper.have", "kr_clue_note", 1)

        -- goUpstairsManor
        door("goUpstairsManor.corridorIn", POSH, POSH_OPEN, 2738, 3578, 0, 2738, 3578, 2737, 3578, in_corridor, "in the west corridor")
        t.exec("goUpstairsManor", t.player.climb, { loc = "murder_qip_spiralstairs", op = 1, op_name = "Climb-up",
            at = { 2736, 3581, 0 }, src = { 2737, 3580 }, dest = { 2737, 3580, 1 } })

        -- takeForm / searchBookcase: the library on level 1.
        door("takeForm.libraryIn", POSH, POSH_OPEN, 2740, 3578, 1, 2740, 3577, 2740, 3579, in_library, "in the library, level 1")
        t.exec("takeForm", t.player.click_obj, "kr_clue_form", 3)
        inv_has("takeForm.have", "kr_clue_form", 1)
        t.exec("searchBookcase", t.player.click_loc, "kr_sin_bookcase3a", 1)
        dialog("searchBookcase-dialog", {})
        inv_has("searchBookcase.have", "kr_clue_armour", 1)

        -- goDownstairsManor / leaveWindow: the same way back and out.
        door("goDownstairsManor.libraryOut", POSH, POSH_OPEN, 2740, 3578, 1, 2740, 3579, 2740, 3577, upper_landing, "on the level-1 landing")
        t.exec("goDownstairsManor", t.player.climb, { loc = "murder_qip_spiralstairstop", op = 1, op_name = "Climb-down",
            at = { 2736, 3581, 1 }, src = { 2737, 3580 }, dest = { 2736, 3580, 0 }, slack = 1 })
        door("leaveWindow.corridorOut", POSH, POSH_OPEN, 2738, 3578, 0, 2737, 3578, 2739, 3578, in_central_hall, "in the central hall")
        door("leaveWindow.centralHallOut", POSH, POSH_OPEN, 2741, 3576, 0, 2741, 3577, 2741, 3575, in_front_room, "in the front room")
        door("leaveWindow.antechamberIn", POSH, POSH_OPEN, 2745, 3575, 0, 2744, 3575, 2746, 3575, in_antechamber, "in the antechamber")
        door("leaveWindow.studyIn", POSH, POSH_OPEN, 2746, 3576, 0, 2746, 3575, 2746, 3577, in_study, "in the study")
        t.exec("leaveWindow", t.player.cross_gate, { loc = "murderwindow", op = 3, at = { 2748, 3577, 0 },
            near = { 2747, 3577 }, far_ok = outside_window, far_desc = "outside the window, x >= 2748" })

        -- returnToGuard: each clue in its own option.
        t.exec("returnToGuard", t.player.talk_to, "murderguard", 1)
        dialog("returnToGuard-form", { "I have proof that the Sinclairs have left." })
        t.exec("returnToGuard.note", t.player.talk_to, "murderguard", 1)
        dialog("returnToGuard-note", { "I have proof that links the Sinclairs to Camelot." })
        t.exec("returnToGuard.helm", t.player.talk_to, "murderguard", 1)
        dialog("returnToGuard-helm", { "I have proof of foul play." })
        stage("evidence_delivered")
        local _, note_left = t.inv.count("kr_clue_note")
        local _, form_left = t.inv.count("kr_clue_form")
        local _, helm_left = t.inv.count("kr_clue_armour")
        t.check("returnToGuard.clues_taken", note_left == 0 and form_left == 0 and helm_left == 0,
            "after the hand-in: kr_clue_note " .. tostring(note_left) .. ", kr_clue_form " .. tostring(form_left)
                .. ", kr_clue_armour " .. tostring(helm_left) .. " (want 0 each: the guard took them)")

        -- talkToGossipAgain: out of the grounds; all three history topics.
        door("talkToGossipAgain.groundsGateOut", GATE, GATE_OPEN, 2741, 3555, 0, 2741, 3556, 2741, 3553, on_road, "on the road south of the gate")
        t.exec("talkToGossipAgain", t.player.talk_to, "gossipy_man", 1)
        dialog("talkToGossipAgain-dialog", { "Tell me about the family.", "Tell me about the mansion.", "Tell me about Anna Sinclair." })
        stage("directed_to_anna")

        -- ======================================================= Freeing Anna
        -- Gossip wanders the gate (2741-2742,3555); a talk can end inside the
        -- grounds, and then the way out is the gate again, on foot.
        local gr, gt = t.world.tile()
        if gr == "ok" and in_grounds(gt) then
            door("talkToAnna.groundsGateOut", GATE, GATE_OPEN, 2741, 3555, 0, 2741, 3556, 2741, 3553, on_road, "on the road south of the gate")
        end
        t.exec("goto-talkToAnna", t.player.goto_tile, 2736, 3477, 0)
        door("talkToAnna.courthouseIn", "kr_courthouse_double_door_l", "kr_courthouse_double_door_l_open", 2736, 3472, 0,
            2736, 3473, 2736, 3470, in_courthouse, "inside the courthouse", 1) -- the courthouse floor is a bridge deck (raw level 1)
        t.exec("talkToAnna", t.player.talk_to, "kr_multi_murderer", 1)
        dialog("talkToAnna-dialog", { "Okay, I guess I don't have much of a choice." })
        stage("anna_agreed")
        inv_has("talkToAnna.thread", "murderthreadg", 1)

        -- goIntoTrial: the stairs ask first, then the court opens.
        t.exec("goIntoTrial", t.player.climb, { loc = "kr_courthouse_stairs_top", op = 1, op_name = "Climb-down",
            at = { 2737, 3469, 0 }, loc_level = 1, dest = { 1819, 4269, 0 },
            same_level = "kr_court.rs2 [oploc1,kr_courthouse_stairs_top] p_teleport ^kr_court_defence_coord (m42_54 -> m28_66)",
            chat = { "mesbox:Are you sure you are prepared for court?", "options", "choose:Yes, I'm ready." } })
        -- The court's opening: the judge, the prosecution's case, the rules, and
        -- the first witness call; the defence walks away from the call and asks
        -- the judge (Quest Helper calls every witness from the judge).
        dialog("goIntoTrial-opening", {}, true)

        -- callHandlerAboutPoison / talkToHandlerAboutPoison
        t.exec("callHandlerAboutPoison", t.player.click_loc, "kr_judge", 1)
        dialog("callHandlerAboutPoison-dialog", { "Dog handler" })
        t.exec("callHandlerAboutPoison.witness", t.var.expect, "varb3907_kr_court_witness", 2)
        t.exec("talkToHandlerAboutPoison", t.player.talk_to, "kr_court_witness", 1)
        dialog("talkToHandlerAboutPoison-dialog", { "Ask about the poison" }, true)
        t.exec("talkToHandlerAboutPoison.proof", t.var.expect, "varb3912_kr_court_dog_proof", 1)

        -- callButlerAboutDagger / talkToButlerAboutDagger
        t.exec("callButlerAboutDagger", t.player.click_loc, "kr_judge", 1)
        dialog("callButlerAboutDagger-dialog", { "Butler" })
        t.exec("callButlerAboutDagger.witness", t.var.expect, "varb3907_kr_court_witness", 3)
        t.exec("talkToButlerAboutDagger", t.player.talk_to, "kr_court_witness", 1)
        dialog("talkToButlerAboutDagger-dialog", { "Ask about the dagger" }, true)
        t.exec("talkToButlerAboutDagger.proof", t.var.expect, "varb3913_kr_court_butl_proof", 1)

        -- callMaidAboutNight / talkToMaidAboutNight / callAboutThread (the maid)
        t.exec("callMaidAboutNight", t.player.click_loc, "kr_judge", 1)
        dialog("callMaidAboutNight-dialog", { "Next page", "Maid" })
        t.exec("callMaidAboutNight.witness", t.var.expect, "varb3907_kr_court_witness", 5)
        t.exec("talkToMaidAboutNight", t.player.talk_to, "kr_court_witness", 1)
        dialog("talkToMaidAboutNight-dialog", { "Ask about the night of the murder" }, true)
        t.exec("talkToMaidAboutNight.proof", t.var.expect, "varb3915_kr_court_maid_proof", 1)
        -- callAboutThread, then waitForVerdict: the fourth proof brings the jury's verdict.
        t.exec("callAboutThread", t.player.talk_to, "kr_court_witness", 1)
        dialog("callAboutThread-dialog", { "Ask about the thread" })
        stage("anna_freed")

        -- leaveCourt / talkToAnnaAfterTrial
        t.exec("leaveCourt", t.player.cross_gate, { loc = "kr_court_fence_door", at = { 1820, 4268, 0 },
            near = { 1820, 4269 }, far_ok = in_courthouse, far_desc = "back in the courthouse" })
        t.exec("talkToAnnaAfterTrial.atCell", t.player.walk_to, 2737, 3468, 20)
        t.exec("talkToAnnaAfterTrial.annaPresent", t.npc.await_present, "kr_multi_murderer", 8, 10)
        t.exec("talkToAnnaAfterTrial", t.player.talk_to, "kr_multi_murderer", 1)
        dialog("talkToAnnaAfterTrial-dialog", {})
        stage("captured")
        -- The open leaves stand on 2735-2736,3471: the way out is straight north from 2736,3470.
        t.exec("talkToAnnaAfterTrial.toDoor", t.player.walk_to, 2736, 3470, 20)
        door("talkToAnnaAfterTrial.courthouseOut", "kr_courthouse_double_door_l", "kr_courthouse_double_door_l_open", 2736, 3472, 0,
            2736, 3470, 2736, 3474, outside_courthouse, "outside the courthouse", 1)

        -- ================================================ Saving Merlin and Knights
        -- enterStatue: the statue east of Camelot springs the trap.
        t.exec("goto-enterStatue", t.player.goto_tile, 2781, 3508, 0)
        t.exec("enterStatue", t.player.click_loc, "kr_camelot_knight_statue", 1)
        dialog("enterStatue-ambush", {})
        at_tile("enterStatue.inCell", in_cell, "in Keep Le Faye's cell", 10)
        stage("in_cell")

        -- talkToMerlin: "What do we do now?"
        t.exec("talkToMerlin", t.player.talk_to, "kr_multi_merlin_jail", 1)
        dialog("talkToMerlin-dialog", { "What do we do now?", "Save King Arthur", "Never mind" })
        stage("met_merlin")

        -- reachForVent: the knights' pyramid lifts Merlin out.
        t.exec("reachForVent", t.player.click_loc, "kr_underground_jail_cell_wall_bottom_with_vent", 1)
        dialog("reachForVent-dialog", {})
        t.ticks(3)
        stage("merlin_escaped")

        -- useGrabOnGuard: Telekinetic Grab on the guard fixing his hair.
        t.exec("useGrabOnGuard", t.player.cast, "telegrab", "kr_keep_guard_hair", 12)
        inv_has("useGrabOnGuard.hairclip", "kr_hairclip", 1)

        -- useHairClipOnOnDoor: the clip in the lock opens the tumbler puzzle.
        t.exec("useHairClipOnOnDoor", t.player.click_loc, "kr_underground_jail_bars_gate", 1)
        t.exec("useHairClipOnOnDoor.lock", t.ui.await_open, "kr_picklock", 10)

        -- solvePuzzle: the wiki Quick guide's method -- every tumbler starts at
        -- its lowest height; after each try a tumbler showing the green "correct
        -- tumbler and height" key (the interface's kr_key_1 model, 27217) is
        -- left alone and every other one goes up by one. Read only from the
        -- chart the player sees (kr_chart_<tumbler>_<guess>) and the height the
        -- interface prints (kr_tumbler_height, "<pos>/5").
        local RIGHT = 27217
        local function widget(sym)
            local r, id = t.ui.widget(sym)
            return r == "ok" and id or nil
        end
        local function press(sym)
            local id = widget(sym)
            if id == nil then
                return false
            end
            t.ui.invoke(id, 1)
            return true
        end
        local function height_of(n)
            local r, txt = t.ui.text("kr_picklock:kr_tumbler_height")
            return tonumber(string.match(tostring(txt), "^(%d+)/")) or -1
        end
        local want = { 0, 0, 0, 0 }
        local locked = { false, false, false, false }
        local solved, tries, trail = false, 0, ""
        for guess = 1, 7 do
            for n = 1, 4 do
                if not press("kr_picklock:kr_tumb_" .. n) then break end
                t.ticks(1)
                local h = height_of(n)
                local guard = 0
                while h ~= want[n] and h >= 0 and guard < 8 do
                    if h < want[n] then
                        press("kr_picklock:kr_arrow_up")
                    else
                        press("kr_picklock:kr_arrow_down")
                    end
                    t.ticks(1)
                    h = height_of(n)
                    guard = guard + 1
                end
            end
            if not press("kr_picklock:kr_check_btn") then
                trail = trail .. "try " .. guess .. ": the lock interface is not open | "
                break
            end
            tries = guess
            t.ticks(2)
            if widget("kr_picklock:kr_check_btn") == nil then
                solved = true
                trail = trail .. "try " .. guess .. " {" .. table.concat(want, ",") .. "}: the lock opened | "
                break
            end
            local marks = {}
            for n = 1, 4 do
                -- The three key models (27217 / 27215 / 27221); a model that has not
                -- composited yet reads -1 (verbs-ui-and-npc.md), so it is read again.
                local model = nil
                for _ = 1, 8 do
                    local pr, pd, pose = t.ui.model_pose("kr_picklock:kr_chart_" .. n .. "_" .. guess)
                    model = (pr == "ok" and type(pose) == "table") and pose.model or nil
                    if model == 27217 or model == 27215 or model == 27221 then break end
                    t.ticks(1)
                end
                marks[n] = tostring(model)
                if model == RIGHT then
                    locked[n] = true
                elseif not locked[n] then
                    want[n] = want[n] + 1
                end
            end
            trail = trail .. "try " .. guess .. " marks {" .. table.concat(marks, ",") .. "} | "
        end
        t.check("solvePuzzle", solved, "tumbler lock: " .. tries .. " tr(ies); " .. trail:sub(1, 700))
        dialog("solvePuzzle-unlocked", {})
        stage("escaped_cell")

        -- openMetalDoor: out of the cell, down the corridor to its ladder.
        t.exec("openMetalDoor", t.player.cross_gate, { loc = "kr_underground_jail_bars_gate", at = { 1904, 4273, 0 },
            near = { 1904, 4273 }, far_ok = in_jail_corridor, far_desc = "in the jail corridor, z <= 4272" })
        t.exec("openMetalDoor.toLadder", t.player.walk_to, 1888, 4269, 40)
        t.exec("openMetalDoor.ladderUp", t.player.climb, { loc = "kr_underground_jail_ladder", op = 1, op_name = "Climb-up",
            at = { 1887, 4269, 0 }, src = { 1888, 4269 }, dest = { 1697, 4261, 0 },
            same_level = "kr_prison.rs2 [oploc1,kr_underground_jail_ladder] ~climb_ladder_to(^kr_keep_ladder_landing) (m29_66 -> m26_66)" })

        -- climbF0ToF1 / climbF1ToF2: Keep Le Faye's two staircases.
        t.exec("climbF0ToF1", t.player.climb, { loc = "kr_stairs", op = 1, op_name = "Climb-up",
            at = { 1695, 4259, 0 }, dest = { 1696, 4260, 1 }, slack = 3,
            landed_ok = on(1, 1689, 1701, 4250, 4264), landed_desc = "Keep Le Faye's first floor" })
        t.exec("climbF1ToF2", t.player.climb, { loc = "kr_stairs", op = 1, op_name = "Climb-up",
            at = { 1695, 4253, 1 }, dest = { 1696, 4257, 2 },
            landed_ok = on(2, 1689, 1701, 4257, 4264), landed_desc = "Keep Le Faye's top floor, north of the stairwell" })

        -- searchTable / selectPurpleBox: the round purple box, second from the right.
        t.exec("searchTable.toTable", t.player.walk_to, 1695, 4258, 20)
        t.exec("searchTable", t.player.click_loc, "kr_jewelry_box_table", 1)
        t.exec("searchTable.boxes", t.ui.await_open, "kr_jewellery_boxes", 10)
        t.check("selectPurpleBox.press", press("kr_jewellery_boxes:box_8"), "kr_jewellery_boxes:box_8 (component 16, the round purple box second from the right) pressed")
        inv_has("selectPurpleBox", "holy_grail", 1)
        dialog("selectPurpleBox-dialog", {})
        stage("have_grail")

        -- Out of the keep: a Camelot Teleport (the keep has no walk-out).
        camelot_teleport("talkToCromperty.camelotTeleport")

        -- talkToCromperty: overland to East Ardougne, into his house.
        t.exec("goto-talkToCromperty", t.player.goto_tile, 2675, 3325, 0)
        door("talkToCromperty.houseIn", "castledoubledoorl", "opencastledoubledoorl", 2678, 3325, 0,
            2678, 3325, 2681, 3325, in_cromperty_house, "inside Cromperty's house")
        t.exec("talkToCromperty", t.player.talk_to, "ardounge_wizard", 1)
        dialog("talkToCromperty-dialog", {})
        stage("have_scroll")
        door("talkToCromperty.houseOut", "castledoubledoorl", "opencastledoubledoorl", 2678, 3325, 0,
            2679, 3325, 2676, 3325, outside_cromperty, "outside Cromperty's house")

        -- =========================================================== Saving Arthur
        t.player.teleport_cast("falador_teleport", { 2965, 3378, 0 }, { name = "enterFortress.faladorTeleport",
            runes = { { "lawrune", 1 }, { "airrune", 3 }, { "waterrune", 1 } }, where = "Falador" })
        t.exec("goto-enterFortress", t.player.goto_tile, 3016, 3512, 0)
        local warn_open = t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "wilderness warning: waiting for the first mesbox" }, 6)
        if warn_open == "ok" then
            local ww_result, ww_kind = t.chat.drain({ stop_at = "none" })
            t.expect("enterFortress.wildernessWarning", ww_result, ww_kind)
        end
        -- The fortress disguise: full black armour (wiki Quick guide "Equip the
        -- black armour and enter"), the bronze/iron set carried for Arthur.
        t.exec("enterFortress.equipHelm", t.player.equip, "black_full_helm")
        t.exec("enterFortress.equipBody", t.player.equip, "black_platebody")
        t.exec("enterFortress.equipLegs", t.player.equip, "black_platelegs")
        t.exec("enterFortress", t.player.cross_gate, { loc = "bkfortressdoor1", at = { 3016, 3514, 0 },
            near = { 3016, 3514 }, far_ok = in_fortress_hall, far_desc = "inside the entrance hall, z >= 3515" })
        t.exec("enterWallInFortress.toWall", t.player.walk_to, 3016, 3516, 20)
        t.exec("enterWallInFortress", t.player.cross_gate, { loc = "bksecretdoor", at = { 3016, 3517, 0 },
            near = { 3016, 3516 }, far_ok = in_secret_room, far_desc = "in the secret room, z >= 3517" })
        t.exec("goDownToArthur", t.player.climb, { loc = "kr_bkf_basement_laddertop", op = 1, op_name = "Climb-down",
            at = { 3016, 3519, 0 }, dest = { 1867, 4243, 0 },
            same_level = "kr_fortress.rs2 [oploc1,kr_bkf_basement_laddertop] ~climb_ladder_to(^kr_bkf_basement_landing) (m47_54 -> m29_66)" })

        -- freeArthur: the scroll, the Grail and the granite at the statue.
        t.exec("freeArthur", t.player.click_loc, "kr_arthur_statue_multi", 1)
        t.ticks(4)
        stage("arthur_freed")
        local _, grail_left = t.inv.count("holy_grail")
        local _, granite_left = t.inv.count("enakh_granite_small")
        t.check("freeArthur.spent", grail_left == 0 and granite_left == 0,
            "holy_grail " .. tostring(grail_left) .. ", enakh_granite_small " .. tostring(granite_left) .. " (want 0 each: the spell took them)")

        -- talkToArthur: the bronze/iron disguise.
        t.exec("talkToArthur", t.player.talk_to, "kr_multi_king_arthur", 1)
        dialog("talkToArthur-dialog", {})
        stage("told_arthur")

        -- Out the way we came.
        t.exec("talkToArthurInCamelot.ladderUp", t.player.climb, { loc = "kr_bkf_basement_ladder", op = 1, op_name = "Climb-up",
            at = { 1867, 4244, 0 }, dest = { 3016, 3518, 0 },
            same_level = "kr_fortress.rs2 [oploc1,kr_bkf_basement_ladder] ~climb_ladder_to(^kr_bkf_secret_room_landing) (m29_66 -> m47_54)" })
        t.exec("talkToArthurInCamelot.toWall", t.player.walk_to, 3016, 3517, 20)
        t.exec("talkToArthurInCamelot.wallOut", t.player.cross_gate, { loc = "bksecretdoor", at = { 3016, 3517, 0 },
            near = { 3016, 3517 }, far_ok = in_fortress_hall, far_desc = "back in the entrance hall, z <= 3516" })
        t.exec("talkToArthurInCamelot.doorOut", t.player.cross_gate, { loc = "bkfortressdoor1", at = { 3016, 3514, 0 },
            near = { 3016, 3515 }, far_ok = outside_fortress, far_desc = "outside the fortress, z <= 3514" })

        -- talkToArthurInCamelot: Camelot Teleport, the courtyard gate and the castle doors.
        camelot_teleport("talkToArthurInCamelot.camelotTeleport")
        t.exec("talkToArthurInCamelot.courtyardGateIn", t.player.pass_door, { closed = "kr_camelot_metalgateclosedr",
            open = "kr_camelot_metalgateopenr", at = { 2758, 3482, 0 }, near = { 2758, 3482 }, far = { 2758, 3483 } })
        t.exec("talkToArthurInCamelot.castleDoorIn", t.player.pass_door, { closed = "kr_cam_doubledoorr",
            open = "kr_cam_doubledoorr_open", at = { 2758, 3503, 0 }, near = { 2758, 3503 }, far = { 2758, 3504 } })

        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        local lamp_before_result, lamp_before = t.inv.count("kr_reward_lamp")

        t.exec("talkToArthurInCamelot", t.player.talk_to, "king_arthur", 1)
        dialog("talkToArthurInCamelot-dialog", {})

        t.quest.expect_complete()

        local def_r, def_d = t.skill.expect_gain("defence", 33000, reward_before)
        t.check("reward.defence", def_r == "ok", def_d)
        local mag_r, mag_d = t.skill.expect_gain("magic", 5000, reward_before)
        t.check("reward.magic", mag_r == "ok", mag_d)
        local lamp_after_result, lamp_after = t.inv.count("kr_reward_lamp")
        t.check("reward.kr_reward_lamp", lamp_before_result == "ok" and lamp_after_result == "ok" and lamp_after == lamp_before + 1,
            string.format("kr_reward_lamp %s -> %s (want +1)", tostring(lamp_before), tostring(lamp_after)))

        t.finish(0)
    end,
}
