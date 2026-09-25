-- The Golem -- re-authored against the 2026-09-23 parity pass
-- (quest_golem/scripts/golem.rs2, golem_portal.rs2, areas/varrock/scripts/
-- curator.rs2, and the golem_elissa branch spliced into
-- quest_deserttreasureii/scripts/deserttreasureii.rs2), which landed the
-- real museum curator dialogue, Elissa's bookcase gate and the 4-way
-- statuette rotation puzzle that an earlier draft of this file reported as
-- absent. Sequence: repair (4 clay) -> letter -> Elissa -> feather/mushroom/
-- ink/pen -> curator talk+pickpocket -> open the display case -> bookcase/
-- notes -> write the program -> place+turn the statuettes -> the real
-- portal door -> throne gems -> report back -> key -> program hand-in.
--
-- Rewards per the live ~quest_complete_rewards call: 1000 Thieving XP,
-- 1000 Crafting XP, and a carpet-ride service (not assertable). No item
-- reward -- the scaffold's ruby/emerald/sapphire +2 checks do not exist in
-- this content pack's completion (that gem trio is real-OSRS Golem lore,
-- not what this port grants) and are dropped.

return {
    id = "golem",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::golem", -- [debugproc,golem]: teleports to Uzer, resets golem_a/clay/b/etc to 0
        "::give softclay 4", -- ^golem_clay_needed = 4
        "::give vial_empty 1", -- crushes the black mushroom into ink
        "::give pestle_and_mortar 1", -- tool for the mushroom grind
        "::give papyrus 1", -- written on with the phoenix-quill pen
        "::give hammer 1", -- prerequisite tool for the demon throne's gems (not the quest's own deliverable)
        "::setlevel crafting 20", -- ^golem_craft_req
        "::setlevel thieving 25", -- ^golem_thieve_req
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "golem_a", -- a varbit (all.varbit.compack:347), bound the same as a varp
            constants = {
                not_started = 0,
                offered = 1,
                repaired = 2,
                tasked = 3,
                portal_open = 6,
                need_program = 7,
                head_open = 8,
                complete = 10,
            },
            row = "quest_golem", -- all.dbrow.compack:66, the literal symbol ~quest_complete_rewards uses
            display = "The Golem",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", "quest.bind golem_a -> " .. tostring(bind_result) .. " " .. tostring(bind_detail))
        t.ticks(3) -- the ::golem setup cheat's reset is not client-side yet
        t.check("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Talk to the golem at its spawn (m54_48.spawn:15, 3488,3090,0). Stat
        -- reqs are already met so golem_talk skips straight past the
        -- "not going to find a conversation here" stat-fail branch to the
        -- repair offer.
        t.exec("goto-golem-1", t.player.goto_tile, 3488, 3090, 0)
        t.exec("talkToGolem-offer", t.player.talk_to, "golem_golem", 1)
        t.exec("talkToGolem-offer-dialog", t.chat.play, {
            "npc:Damage... severe... task... incomplete...",
            "choose:Shall I try to repair you?",
            "player:Shall I try to repair you?",
            "npc:Repairs... needed...",
        })
        t.check("quest.stage.offered", t.quest.expect_stage("offered"))

        -- Repair with all 4 soft clay -- [opnpcu,golem_golem] requires
        -- %golem_a=offered on every use and advances %golem_clay until it
        -- hits ^golem_clay_needed, which flips golem_a to repaired. The
        -- target is re-resolved by symbol before EACH use rather than
        -- reused, in case the golem's live npc slot changes as its model
        -- transforms broken -> partially_broken -> repaired (multinpc-style
        -- re-identification, trap 19/13's pattern) and a stale target
        -- silently stops landing.
        -- Each use answers with its own blocking ~mesbox (measured: the pilot
        -- run's second use pressed "Walk here" instead of re-arming softclay,
        -- because the first use's box was still open). t.chat.close() (a
        -- hard api_drive.close_modal(), not a click) answered `ok` but left
        -- %golem_clay stuck at 0 forever -- it does not click the box's own
        -- "click here to continue" the way the server script's suspended
        -- ~mesbox call is waiting for, so the label's own trailing
        -- `%golem_clay = $clay;` after the mesbox never ran. t.chat.continue_()
        -- is the real click-through and lets that line execute.
        local golem_target, golem_target_result = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol-1", golem_target_result == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(golem_target_result))
        t.exec("repairClay1", t.player.use_on, "softclay", golem_target)
        local close1_result = t.chat.continue_()
        t.check("repairClay1-continue", close1_result == "ok", "chat.continue_ after repairClay1 -> " .. tostring(close1_result))
        t.ticks(1) -- a frame for the close to land before the next arm+click
        local clay1_result, clay1_value = t.var.server("golem_clay")
        t.check("repairClay1-clay-var", clay1_result == "ok", "var.server golem_clay -> " .. tostring(clay1_result) .. " " .. tostring(clay1_value))

        local golem_target_r2, golem_target_r2_result = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol-r2", golem_target_r2_result == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(golem_target_r2_result))
        t.exec("repairClay2", t.player.use_on, "softclay", golem_target_r2)
        local close2_result = t.chat.continue_()
        t.check("repairClay2-continue", close2_result == "ok", "chat.continue_ after repairClay2 -> " .. tostring(close2_result))
        t.ticks(1)
        local clay2_result, clay2_value = t.var.server("golem_clay")
        t.check("repairClay2-clay-var", clay2_result == "ok", "var.server golem_clay -> " .. tostring(clay2_result) .. " " .. tostring(clay2_value))

        local golem_target_r3, golem_target_r3_result = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol-r3", golem_target_r3_result == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(golem_target_r3_result))
        t.exec("repairClay3", t.player.use_on, "softclay", golem_target_r3)
        local close3_result = t.chat.continue_()
        t.check("repairClay3-continue", close3_result == "ok", "chat.continue_ after repairClay3 -> " .. tostring(close3_result))
        t.ticks(1)
        local clay3_result, clay3_value = t.var.server("golem_clay")
        t.check("repairClay3-clay-var", clay3_result == "ok", "var.server golem_clay -> " .. tostring(clay3_result) .. " " .. tostring(clay3_value))

        local golem_target_r4, golem_target_r4_result = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol-r4", golem_target_r4_result == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(golem_target_r4_result))
        t.exec("repairClay4", t.player.use_on, "softclay", golem_target_r4)
        local close4a_result = t.chat.continue_()
        t.check("repairClay4-continue", close4a_result == "ok", "chat.continue_ after repairClay4 -> " .. tostring(close4a_result))
        t.ticks(1)
        t.check("quest.stage.repaired", t.quest.expect_stage("repaired"))

        -- Talk again: golem_a=repaired's branch runs straight through to
        -- tasked with no choice, just a page chain ending on the portal ask.
        t.exec("talkToGolem-task", t.player.talk_to, "golem_golem", 1)
        t.exec("talkToGolem-task-dialog", t.chat.play, {
            "npc:Damage repaired...",
            "npc:Thank you. My body and mind are fully healed.",
            "npc:Now I must complete my task by defeating the great enemy.",
            "player:What enemy?",
            "npc:A great demon. It broke through from its dimension to attack the city.",
            "npc:The golem army was created to fight it. Many were destroyed, but we drove the demon back!",
            "npc:The demon is still wounded. You must open the portal so that I can strike the final blow and complete my task.",
        })
        t.check("quest.stage.tasked", t.quest.expect_stage("tasked"))

        -- Pick up the letter (m54_48.spawn:19, a ground obj, 3479,3092,0)
        -- and open it. click_obj answers `ok` with a nil detail (a hollow
        -- verb), so it is called directly and the count written by hand.
        t.exec("goto-letter", t.player.goto_tile, 3479, 3092, 0)
        local letter_before_result, letter_before = t.inv.count("golem_letter")
        local letter_click_result, letter_click_detail = t.player.click_obj("golem_letter")
        t.inv.await("golem_letter", 1, 10)
        local letter_after_result, letter_after = t.inv.count("golem_letter")
        local letter_pass = letter_click_result == "ok" and letter_after_result == "ok"
            and letter_after > (letter_before_result == "ok" and letter_before or 0)
        t.step("pickUpLetter", letter_pass and "PASS" or "FAIL",
            string.format("click_obj golem_letter -> %s (%s), count %s -> %s",
                tostring(letter_click_result), tostring(letter_click_detail), tostring(letter_before), tostring(letter_after)))
        -- [opheld1,golem_letter]: sets %golem_b=1, mesbox about the notes/statuette.
        t.exec("readLetter", t.player.inv_op, "golem_letter", 1)
        local close_letter_result = t.chat.continue_()
        t.check("readLetter-continue", close_letter_result == "ok", "chat.continue_ after readLetter -> " .. tostring(close_letter_result))

        -- Talk to Elissa in the north-east Digsite (m52_53.spawn:22,
        -- 3376,3429,0) -- her branch is spliced into the shared
        -- [opnpc1,golem_elissa] trigger (deserttreasureii.rs2:320-336),
        -- guarded on npc_type so it never bleeds into the Vardorvis/Kasonde
        -- soft-skip cascade above it. It requires %golem_b>=1 (just set by
        -- readLetter) and writes %golem_b=2 -- the gate golem_bookcase's
        -- [oploc1,...] checks before it will search (quest-helper's own
        -- talkedToElissa=ge(2) rung).
        t.exec("goto-elissa", t.player.goto_tile, 3376, 3429, 0)
        t.exec("talkToElissa", t.player.talk_to, "golem_elissa", 1)
        t.exec("talkToElissa-dialog", t.chat.play, {
            "player:I found a letter in the desert with your name on it. It mentions notes by someone called Varmen.",
            "npc:Varmen? Oh, that old fossil! He used to work here. I think some of his old papers are still on the bookshelves in the Exam Centre.",
        })
        local elissa_b_result, elissa_b_value = t.var.server("golem_b")
        t.check("golem.b-elissa", elissa_b_result == "ok" and elissa_b_value == 2, "golem_b (varbit) = " .. tostring(elissa_b_value))

        -- Steal a feather from the desert phoenix -- [opnpc3,golem_phoenix],
        -- op3 same as a pickpocket, not op1 talk.
        t.exec("goto-phoenix", t.player.goto_tile, 3414, 3156, 0)
        t.exec("stealFeather", t.player.talk_to, "golem_phoenix", 3)

        -- Pick black mushrooms (loc, quest_shadowstorm's shared oploc1),
        -- then grind one with the pestle+vial into ink -- [opheldu,
        -- golem_mushroom] keys on last_useitem=pestle_and_mortar.
        t.exec("goto-mushroom", t.player.goto_tile, 3495, 3088, 0)
        t.exec("pickMushroom", t.player.click_loc, "golem_black_mushrooms", 1)
        -- oploc1's inv_add lands a tick or two behind the settle's own new
        -- chat line (section 8's inventory-sync trap) -- poll rather than
        -- read straight through.
        local mushroom_await_result = t.inv.await("golem_mushroom", 1, 10)
        t.check("pickMushroom-sync", mushroom_await_result == "ok", "inv.await golem_mushroom 1 -> " .. tostring(mushroom_await_result))
        t.exec("grindMushroom", t.player.use_item_on_item, "pestle_and_mortar", "golem_mushroom")
        local close_grind_result = t.chat.continue_()
        t.check("grindMushroom-continue", close_grind_result == "ok", "chat.continue_ after grindMushroom -> " .. tostring(close_grind_result))
        local ink_await_result = t.inv.await("golem_ink", 1, 10)
        t.check("grindMushroom-sync", ink_await_result == "ok", "inv.await golem_ink 1 -> " .. tostring(ink_await_result))
        -- Dip the feather in the ink -- [opheldu,golem_phoenixfeather] keys
        -- on last_useitem=golem_ink.
        t.exec("dipFeather", t.player.use_item_on_item, "golem_ink", "golem_phoenixfeather")
        local close_dip_result = t.chat.continue_()
        t.check("dipFeather-continue", close_dip_result == "ok", "chat.continue_ after dipFeather -> " .. tostring(close_dip_result))
        local pen_await_result = t.inv.await("golem_pen", 1, 10)
        t.check("dipFeather-sync", pen_await_result == "ok", "inv.await golem_pen 1 -> " .. tostring(pen_await_result))

        -- Pickpocket Curator Haig for the statuette key -- [opnpc3,curator]'s
        -- golem branch is checked BEFORE the eaa pickpocket fallback, so no
        -- thieving-level gate applies here.
        t.exec("goto-curator", t.player.goto_tile, 3257, 3447, 0)

        -- Ask Curator Haig about the statuette first -- his branch is
        -- spliced into [opnpc1,curator] (curator.rs2:49-67, label
        -- curator_golem_talk), gated on %golem_a>=tasked and no statuette
        -- held/retrieved yet, ordered after the letter/certificate/phoenix/
        -- blackarm branches so it never steals their click. The op1 press
        -- always opens on the museum's own unconditional greeting first.
        t.exec("talkToCurator", t.player.talk_to, "curator", 1)
        t.exec("talkToCurator-dialog", t.chat.play, {
            "npc:Welcome to the museum of Varrock.",
            "player:I'm looking for a statuette recovered from the city of Uzer.",
            "npc:Ah yes, a wonderful piece. It's in the display case upstairs.",
            "player:Could I take it, please?",
            "npc:This museum never lets go of its treasures.",
            "choose:I want to open a portal to the lair of an elder-demon.",
            "player:I want to open a portal to the lair of an elder-demon.",
            "npc:Good heavens! I'd never let you do such a dangerous thing.",
        })

        t.exec("pickpocketCurator", t.player.talk_to, "curator", 3)
        -- The steal answers with its own blocking ~mesbox (same shape as the
        -- clay repair above) -- close it before the next world click.
        local close4_result = t.chat.continue_()
        t.check("pickpocketCurator-continue", close4_result == "ok", "chat.continue_ after pickpocketCurator -> " .. tostring(close4_result))
        local key_await_result = t.inv.await("golem_statuettekey", 1, 10)
        t.check("pickpocketCurator-sync", key_await_result == "ok", "inv.await golem_statuettekey 1 -> " .. tostring(key_await_result))
        -- Open the display case upstairs in the Varrock Museum --
        -- 2026-09-23 parity pass: this is now a real [oploc2,
        -- vm_timeline_terracotta_statue_multi] "Open" (golem_portal.rs2:84-
        -- 104), gated on the pickpocketed key (auto-consumed) and
        -- %golem_a>=tasked, not an item-on-item stand-in. The case sits on
        -- the museum's first floor (quest-helper's openCabinet WorldPoint,
        -- 3257,3453,1) -- goto_tile the floor tile directly (the ladder
        -- rule: stairs/floors are plain travel, no click_loc on them).
        t.exec("goto-museum-upstairs", t.player.goto_tile, 3257, 3453, 1)
        local case_names = { "vm_timeline_terracotta_statue_multi", "vm_timeline_terracotta_statue" }
        local case_target, case_found = nil, nil
        for i = 1, #case_names do
            local r, near = t.world.loc_near(case_names[i], 15)
            if r == "ok" and case_target == nil then
                case_target = near
                case_found = case_names[i]
            end
        end
        t.check("world.display-case", case_target ~= nil,
            "world.loc_near tried " .. table.concat(case_names, ",") .. " -> found " .. tostring(case_found)
            .. " at " .. tostring(case_target and case_target.tile_x) .. "," .. tostring(case_target and case_target.tile_z) .. "," .. tostring(case_target and case_target.level))
        t.exec("openCase", t.player.click_loc, case_found or "vm_timeline_terracotta_statue_multi", 2)
        local statuette_await_result = t.inv.await("golem_statuette", 1, 10)
        t.check("openCase-sync", statuette_await_result == "ok", "inv.await golem_statuette 1 -> " .. tostring(statuette_await_result))

        -- Search the bookcase in the Digsite Exam Centre's south-east corner
        -- for Varmen's notes, then read them -- [opheld1,golem_notes] sets
        -- %golem_b=2, needed for both the golemkey and the pen-write below.
        t.exec("goto-bookcase", t.player.goto_tile, 3367, 3332, 0)
        t.exec("searchBookcase", t.player.click_loc, "golem_bookcase", 1)
        -- The search opens a plain mes() first (the settle's own trigger)
        -- and the notes' ~mesbox a moment behind it -- close before inv_op.
        local close5_result = t.chat.continue_()
        t.check("searchBookcase-continue", close5_result == "ok", "chat.continue_ after searchBookcase -> " .. tostring(close5_result))
        local notes_await_result = t.inv.await("golem_notes", 1, 10)
        t.check("searchBookcase-sync", notes_await_result == "ok", "inv.await golem_notes 1 -> " .. tostring(notes_await_result))
        t.exec("readNotes", t.player.inv_op, "golem_notes", 1)
        local close_notes_result = t.chat.continue_()
        t.check("readNotes-continue", close_notes_result == "ok", "chat.continue_ after readNotes -> " .. tostring(close_notes_result))

        -- Write the program -- [opheldu,golem_pen] keys on last_useitem=papyrus,
        -- requires %golem_b>=2 (just satisfied by readNotes).
        t.exec("writeProgram", t.player.use_item_on_item, "papyrus", "golem_pen")
        local close_write_result = t.chat.continue_()
        t.check("writeProgram-continue", close_write_result == "ok", "chat.continue_ after writeProgram -> " .. tostring(close_write_result))
        local program_await_result = t.inv.await("golem_program", 1, 10)
        t.check("writeProgram-sync", program_await_result == "ok", "inv.await golem_program 1 -> " .. tostring(program_await_result))

        -- Enter the Uzer ruins. golem_insidestairs_top is a pure maplink
        -- (ladders_stairs/configs/maplink.dbrow:12909, no opnpc/oploc script
        -- of its own) -- per the ladder rule, goto_tile the far side directly
        -- (its dest 0_42_76_33_22 decodes to 2721,4886,0) instead of
        -- click_loc'ing the stairs.
        t.exec("goto-ruin", t.player.goto_tile, 2721, 4886, 0)
        t.ticks(2)

        -- Place the museum statuette in its own empty alcove -- ONLY
        -- golem_statuetted carries the [oplocu,...] insertion handler (the
        -- other three alcoves already hold theirs, golem_portal.rs2:106-
        -- 128), so the target must be that one symbol specifically, not
        -- whichever of the four the scene resolves first.
        local statuetted_result, statuetted_target = t.world.loc_near("golem_statuetted", 25)
        t.check("world.statuette-alcove", statuetted_result == "ok",
            "world.loc_near golem_statuetted -> " .. tostring(statuetted_result)
            .. " at " .. tostring(statuetted_target and statuetted_target.tile_x) .. "," .. tostring(statuetted_target and statuetted_target.tile_z) .. "," .. tostring(statuetted_target and statuetted_target.level))
        -- Measured failures here: goto_tile (a teleport) lands EXACTLY on
        -- the alcove's own tile at standoff 0, and use_on's _ensure_visible
        -- then steps off it mid-click -- a real walk fired AFTER the item
        -- was armed, which cancels the arming before the press lands. Do
        -- the standoff-1 walk explicitly, as its own settled step, BEFORE
        -- arming, so use_on's internal walk_near is a no-op and
        -- _ensure_visible has nothing left to correct.
        if statuetted_target ~= nil then
            t.exec("goto-statuette", t.player.goto_tile, statuetted_target.tile_x, statuetted_target.tile_z, statuetted_target.level)
            t.ticks(1)
            t.exec("walk-statuette", t.player.walk_near, statuetted_target, 15, 1)
            t.ticks(1)
        end
        t.exec("placeStatuette", t.player.use_on, "golem_statuette", statuetted_target)
        local statuette_left_result, statuette_left = t.inv.count("golem_statuette")
        t.check("placeStatuette-consumed", statuette_left_result == "ok" and statuette_left == 0,
            "golem_statuette carried = " .. tostring(statuette_left))
        local statusd1_result, statusd1_value = t.var.server("golem_statuettestatusd")
        t.check("golem.statuetted-inserted", statusd1_result == "ok" and statusd1_value == 1,
            "golem_statuettestatusd (varbit) = " .. tostring(statusd1_value))

        -- Turn A, B and D to face the door (quest-helper's turnedStatue
        -- targets: A=1, B=1, C=0 -- already its rest state, never touched --
        -- D=2, one more turn past the insertion above). Each [oploc1,...]
        -- turn handler calls ~golem_check_statuettes_aligned itself
        -- (golem_portal.rs2:130-200), and the door opens the moment all
        -- four match -- order among these three does not matter.
        local statuettea_result, statuettea_target = t.world.loc_near("golem_statuettea", 25)
        t.check("world.statuette-a", statuettea_result == "ok", "world.loc_near golem_statuettea -> " .. tostring(statuettea_result))
        if statuettea_target ~= nil then
            t.exec("goto-statuettea", t.player.goto_tile, statuettea_target.tile_x, statuettea_target.tile_z, statuettea_target.level)
        end
        t.exec("turnStatuetteA", t.player.click_loc, "golem_statuettea", 1)

        local statuetteb_result, statuetteb_target = t.world.loc_near("golem_statuetteb", 25)
        t.check("world.statuette-b", statuetteb_result == "ok", "world.loc_near golem_statuetteb -> " .. tostring(statuetteb_result))
        if statuetteb_target ~= nil then
            t.exec("goto-statuetteb", t.player.goto_tile, statuetteb_target.tile_x, statuetteb_target.tile_z, statuetteb_target.level)
        end
        t.exec("turnStatuetteB", t.player.click_loc, "golem_statuetteb", 1)

        t.exec("turnStatuetteD", t.player.click_loc, "golem_statuetted", 1)

        local statusa_result, statusa_value = t.var.server("golem_statuettestatusa")
        t.check("golem.statuettea-turned", statusa_result == "ok" and statusa_value == 1, "golem_statuettestatusa (varbit) = " .. tostring(statusa_value))
        local statusb_result, statusb_value = t.var.server("golem_statuettestatusb")
        t.check("golem.statuetteb-turned", statusb_result == "ok" and statusb_value == 1, "golem_statuettestatusb (varbit) = " .. tostring(statusb_value))
        local statusd2_result, statusd2_value = t.var.server("golem_statuettestatusd")
        t.check("golem.statuetted-turned", statusd2_result == "ok" and statusd2_value == 2, "golem_statuettestatusd (varbit) = " .. tostring(statusd2_value))
        t.check("quest.stage.portal_open", t.quest.expect_stage("portal_open"))

        -- Enter the throne room through the real portal door --
        -- [oploc1,golem_portal] (golem_portal.rs2:214-229, a static
        -- placement at 2720,4912,0 -- all.loc.compack:6311, m42_76.jl2:277)
        -- is gated on %golem_a>=portal_open and has NO maplink registration
        -- of its own (grep of ladders_stairs/configs/maplink.dbrow), so it
        -- is the puzzle-gated door itself and must be clicked for real, not
        -- goto_tile'd past. golem_demon_portal/golem_demon_door_always_open
        -- -- an earlier draft's candidate list matched those first -- are a
        -- DIFFERENT, state-INDEPENDENT copy already inside the lair (used
        -- below to leave); clicking one of those from the surface answered
        -- "menu has no row for it" every time because it is not physically
        -- there to click from this plane.
        t.exec("goto-portal-door", t.player.goto_tile, 2720, 4912, 0)
        t.exec("enterPortal", t.player.click_loc, "golem_portal", 1)
        -- p_teleport(^golem_demon_lair) is a multi-region jump (per section 2's
        -- Abyss note): poll the real script evidence the teleport landed and
        -- ran (%golem_seen_underground flips 0->1 inside the handler) rather
        -- than a coordinate threshold -- the ruin's own courtyard already
        -- reads z>4000 (section 8's golem-specific trap), so that predicate
        -- is true before the click and proves nothing.
        local seen_result = t.await({
            level = function()
                local r, v = t.var.server("golem_seen_underground")
                return r == "ok" and v == 1
            end,
            note = "golem_seen_underground flips after entering the portal",
        }, 20)
        local seen_check_result, seen_check_value = t.var.server("golem_seen_underground")
        t.check("enterPortal-seen", seen_result == "ok" and seen_check_value == 1,
            "await golem_seen_underground -> " .. tostring(seen_result) .. "; var.server -> " .. tostring(seen_check_result) .. " " .. tostring(seen_check_value))
        local tile_result, tile_now = t.world.tile()
        t.step("enterPortal-arrived", tile_result == "ok" and "PASS" or "FAIL",
            "t.world.tile() -> " .. tostring(tile_result) .. " " .. tostring(tile_now and tile_now.x) .. "," .. tostring(tile_now and tile_now.z) .. "," .. tostring(tile_now and tile_now.level))

        -- Prize the gems from the demon's throne with the hammer --
        -- [oplocu,golem_demon_throne]/[oplocu,golem_throne_withgems] grant
        -- ruby + golem_golemkey, needed to open the golem's skull below.
        -- Same walk_near-standoff-1-before-arming shape that now lands the
        -- statuette (the comment above this block, before the retry, noted
        -- this exact shape already worked cleanly for this same loc). A
        -- generous radius (a scene-pool check, not a screen one) because the
        -- lair's own layout relative to the teleport's landing tile is
        -- unknown going in.
        local throne_names = { "golem_demon_throne", "golem_throne_withgems" }
        local throne_target, throne_found = nil, nil
        for i = 1, #throne_names do
            local r, near = t.world.loc_near(throne_names[i], 80)
            if r == "ok" and throne_target == nil then
                throne_target = near
                throne_found = throne_names[i]
            end
        end
        t.check("world.demon-throne", throne_target ~= nil,
            "world.loc_near tried " .. table.concat(throne_names, ",") .. " -> found " .. tostring(throne_found)
            .. " at " .. tostring(throne_target and throne_target.tile_x) .. "," .. tostring(throne_target and throne_target.tile_z) .. "," .. tostring(throne_target and throne_target.level))
        if throne_target ~= nil then
            t.exec("goto-throne", t.player.goto_tile, throne_target.tile_x, throne_target.tile_z, throne_target.level)
            t.ticks(1)
            t.exec("walk-throne", t.player.walk_near, throne_target, 15, 1)
            t.ticks(1)
        end
        t.exec("prizeGems", t.player.use_on, "hammer", throne_target)
        local golemkey_await_result = t.inv.await("golem_golemkey", 1, 10)
        t.check("prizeGems-sync", golemkey_await_result == "ok", "inv.await golem_golemkey 1 -> " .. tostring(golemkey_await_result))

        -- Leave the throne room -- quest-helper's own leaveThroneRoom step,
        -- ObjectStep(GOLEM_DEMON_PORTAL, 2720,4883,2). Unlike golem_portal
        -- (the entry, no maplink registration -- section above), this exact
        -- loc IS also registered in ladders_stairs/configs/maplink.dbrow
        -- (src 2_42_76_{30,31,32,33}_{18,19,20}, dest 0_42_76_33_47 =
        -- 2721,4911,0) -- the same "climb" mechanism a ladder uses, keyed
        -- on the PLAYER's own coord (trap 13), so it is pressed for real
        -- here rather than goto_tile'd past, to leave a driven row for the
        -- guide step and let the maplink table (not a guess) pick the exit
        -- tile.
        local demonportal_result, demonportal_target = t.world.loc_near("golem_demon_portal", 30)
        t.check("world.demon-portal-exit", demonportal_result == "ok",
            "world.loc_near golem_demon_portal -> " .. tostring(demonportal_result)
            .. " at " .. tostring(demonportal_target and demonportal_target.tile_x) .. "," .. tostring(demonportal_target and demonportal_target.tile_z) .. "," .. tostring(demonportal_target and demonportal_target.level))
        if demonportal_target ~= nil then
            t.exec("goto-demon-portal-exit", t.player.goto_tile, demonportal_target.tile_x, demonportal_target.tile_z, demonportal_target.level)
        end
        -- [oploc1,golem_demon_portal]'s own body (golem_portal.rs2:205-212)
        -- is one-directional -- both the mesbox line and the screenshot
        -- read "You step into the portal." followed by another
        -- p_teleport(^golem_demon_lair), the SAME destination the entry
        -- door used, not a route back to the surface (measured: the
        -- player's tile is unchanged by this press, still 2719,488x,2).
        -- Nothing in this content pack's own .rs2 sends the player back up
        -- via a click at all -- the return trip is the maplink table alone
        -- (ladders_stairs/configs/maplink.dbrow, src 2_42_76_*_* -> dest
        -- 0_42_76_33_47), the same "walk onto the tile and it climbs for
        -- you" mechanism section 2 describes for a real staircase, so it is
        -- left to the plain-travel goto below rather than asserted here.
        t.exec("leaveThroneRoom", t.player.click_loc, "golem_demon_portal", 1)

        -- Back to the golem on the surface to report the demon dead --
        -- %golem_a=portal_open & %golem_seen_underground=1's branch is a
        -- straight npc/player/npc/player/npc chain, no choice (golem.rs2
        -- lines 87-94).
        t.exec("goto-golem-2", t.player.goto_tile, 3488, 3090, 0)
        t.exec("talkToGolem-demondead", t.player.talk_to, "golem_golem", 1)
        t.exec("talkToGolem-demondead-dialog", t.chat.play, {
            "npc:My task is incomplete. You must open the portal so I can defeat the great demon.",
            "player:It's ok, the demon is dead!",
            "npc:The demon must be defeated...",
            "player:No, you don't understand. I saw the demon's skeleton. It must have died of its wounds.",
            "npc:Demon must be defeated! Task incomplete.",
        })
        t.check("quest.stage.need_program", t.quest.expect_stage("need_program"))

        -- Insert the golemkey -- [opnpcu,golem_golem] last_useitem=
        -- golem_golemkey. A plain mes(), no page to continue through.
        local golem_target_key, golem_target_key_result = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol-key", golem_target_key_result == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(golem_target_key_result))
        t.exec("insertKey", t.player.use_on, "golem_golemkey", golem_target_key)
        t.check("quest.stage.head_open", t.quest.expect_stage("head_open"))

        -- Reward snapshot BEFORE the hand-in (section 6's rule).
        local skill_snapshot_result, skill_snapshot = t.skill.snapshot()
        t.step("skill.snapshot", skill_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot -> " .. tostring(skill_snapshot_result))

        -- Hand in the program -- [opnpcu,golem_golem] last_useitem=
        -- golem_program completes the quest: three chained npc lines, then
        -- %golem_a=complete, stat_advance(crafting/thieving), and
        -- ~quest_complete_rewards(quest_golem, "1000 Thieving XP|1000
        -- Crafting XP|Carpet ride from Shantay Pass to Uzer", golem_program).
        local golem_target_program, golem_target_program_result = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol-program", golem_target_program_result == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(golem_target_program_result))
        t.exec("handInProgram", t.player.use_on, "golem_program", golem_target_program)
        t.exec("handInProgram-dialog", t.chat.play, {
            "npc:New instructions... Updating program...",
            "npc:Task complete!",
            "npc:Thank you. Now my mind is at rest.",
        })
        t.ticks(3) -- completion is asynchronous behind the chained dialogue -- see section 8

        t.quest.expect_complete()
        t.check("reward.crafting", t.skill.expect_gain("crafting", 1000, skill_snapshot))
        t.check("reward.thieving", t.skill.expect_gain("thieving", 1000, skill_snapshot))
        t.finish(0)
        return
    end,
}
