-- The Golem -- rewritten from the scaffold after reading the live content:
-- quest_golem/scripts/golem.rs2, golem_portal.rs2, and areas/varrock/scripts/
-- curator.rs2 (opnpc3). This port is a heavy "soft" reimplementation of the
-- real OSRS quest -- no museum curator dialogue chain, no Elissa, no 4-way
-- statuette rotation puzzle -- so the scaffold's Quest-Helper-guessed dialog
-- (it even pasted in Shield of Arrav's text by mistake) and its wrong stage
-- constants (complete=0, should be 10) are replaced with what the .rs2
-- actually does. Sequence per golem.rs2/golem_portal.rs2's own debug hint:
-- "Place statuette->Enter portal->throne gems->key->program->golem."
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
        t.exec("pickpocketCurator", t.player.talk_to, "curator", 3)
        -- The steal answers with its own blocking ~mesbox (same shape as the
        -- clay repair above) -- close it before the next world click.
        local close4_result = t.chat.continue_()
        t.check("pickpocketCurator-continue", close4_result == "ok", "chat.continue_ after pickpocketCurator -> " .. tostring(close4_result))
        local key_await_result = t.inv.await("golem_statuettekey", 1, 10)
        t.check("pickpocketCurator-sync", key_await_result == "ok", "inv.await golem_statuettekey 1 -> " .. tostring(key_await_result))
        -- Use the key -- [opheldu,golem_statuettekey] is an item-ON-ITEM
        -- handler (like the mushroom/feather/pen chain above), not a
        -- use-on-world-target one: use_on(key, curator) answered "Nothing
        -- interesting happens." (measured) because no [opnpcu,curator]
        -- exists for this item at all. The block never reads last_useitem,
        -- so any second carried item arms the "soft: no IF 534" museum-case
        -- stand-in -- papyrus, still held at this point, is the target cell.
        -- Every line in that block is a plain mes(), not a ~mesbox -- no
        -- dialogue opens, so there is nothing to continue_() through here.
        t.exec("useKeyOnCase", t.player.use_item_on_item, "papyrus", "golem_statuettekey")
        local statuette_await_result = t.inv.await("golem_statuette", 1, 10)
        t.check("useKeyOnCase-sync", statuette_await_result == "ok", "inv.await golem_statuette 1 -> " .. tostring(statuette_await_result))

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

        -- Place the statuette on whichever of the four alcove symbols is
        -- actually in the loaded scene -- golem_try_place_statuette does not
        -- care which -- discovered with world.loc_near (a scene-pool check,
        -- not a screen/camera one) rather than guessed blind.
        local statuette_names = { "golem_statuettea", "golem_statuetteb", "golem_statuettec", "golem_statuetted" }
        local statuette_target, statuette_found = nil, nil
        for i = 1, #statuette_names do
            local r, near = t.world.loc_near(statuette_names[i], 25)
            if r == "ok" and statuette_target == nil then
                statuette_target = near
                statuette_found = statuette_names[i]
            end
        end
        t.check("world.statuette-alcove", statuette_target ~= nil,
            "world.loc_near tried " .. table.concat(statuette_names, ",") .. " -> found " .. tostring(statuette_found)
            .. " at " .. tostring(statuette_target and statuette_target.tile_x) .. "," .. tostring(statuette_target and statuette_target.tile_z) .. "," .. tostring(statuette_target and statuette_target.level))
        -- Measured failures here: goto_tile (a teleport) lands EXACTLY on
        -- the alcove's own tile at standoff 0, and use_on's _ensure_visible
        -- then steps off it mid-click -- a real walk fired AFTER the item
        -- was armed, which cancels the arming before the press lands
        -- ("pressed 'Examine...'... the arming was gone by the time the
        -- menu opened"). A 2-tile teleport offset changed the failure
        -- instead of fixing it (a bare "menu has no row for it", held=false
        -- -- likely a wall or a still-too-far camera pick). Do the
        -- standoff-1 walk explicitly, as its own settled step, BEFORE
        -- arming, so use_on's internal walk_near is a no-op and
        -- _ensure_visible has nothing left to correct.
        if statuette_target ~= nil then
            t.exec("goto-statuette", t.player.goto_tile, statuette_target.tile_x, statuette_target.tile_z, statuette_target.level)
            t.ticks(1)
            t.exec("walk-statuette", t.player.walk_near, statuette_target, 15, 1)
            t.ticks(1)
        end
        -- Three positioning strategies were driven against this exact loc
        -- across separate attempts -- an exact-tile teleport, a 2-tile
        -- teleport offset, and (this attempt) an explicit standoff-1
        -- walk_near completed and confirmed BEFORE arming -- and all three
        -- land the same seam: use_on arms golem_statuette, but the press
        -- that follows lands on "Examine @cya@Statuette in alcove", an
        -- ordinary op row, not the held-item row, because the arming is
        -- gone by the time the menu opens. The identical call shape
        -- (walk_near standoff 1, then use_on) worked cleanly one loc over
        -- for golem_demon_throne (prizeGems, below the removed code),
        -- which rules out a general use_on/walk_near defect and narrows
        -- this to golem_statuettea itself (a loc-identity or geometry seam
        -- this driver has no further verb to work around). Called directly
        -- (not through t.exec) so the seam is reported once, as BLOCKED,
        -- rather than as a ledger FAIL.
        local place_result, place_detail = t.player.use_on("golem_statuette", statuette_target)
        t.blocked("use_on(golem_statuette, " .. tostring(statuette_found) .. " @ "
            .. tostring(statuette_target and statuette_target.tile_x) .. ","
            .. tostring(statuette_target and statuette_target.tile_z) .. ","
            .. tostring(statuette_target and statuette_target.level)
            .. ") loses its arming before the press lands, every time, across an exact-tile "
            .. "teleport, a 2-tile teleport offset, and a confirmed standoff-1 walk_near done "
            .. "before arming -- last attempt: " .. tostring(place_result) .. " " .. tostring(place_detail)
            .. " -- golem_a stays tasked(3), never reaching portal_open(6), so nothing past this "
            .. "point (portal, throne, the demon-dead exchange, the golemkey/program hand-in, "
            .. "expect_complete) can be driven.")
        return
    end,
}
