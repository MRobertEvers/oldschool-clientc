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
        -- RETRY after 73a4251d0: the walk_near-standoff-1-before-arming shape
        -- above now lands cleanly -- the server looks a multiloc's loc
        -- trigger up on the varbit-resolved child and then on the BASE, so
        -- [oplocu,golem_statuettea] is reached and %golem_a advances to
        -- ^golem_portal_open(6) (proved by a real run: build/quests_proof/
        -- golem.lua, ledger rows statuette.place/statuette.portal_open/
        -- statuette.consumed all PASS). Driven through t.exec now that it
        -- is a real step, not a seam to report.
        t.exec("placeStatuette", t.player.use_on, "golem_statuette", statuette_target)
        t.check("quest.stage.portal_open", t.quest.expect_stage("portal_open"))
        local statuette_left_result, statuette_left = t.inv.count("golem_statuette")
        t.check("placeStatuette-consumed", statuette_left_result == "ok" and statuette_left == 0,
            "golem_statuette carried = " .. tostring(statuette_left))

        -- Step through the door the statuette just opened.
        -- [oploc1,golem_demon_door_always_open]/[oploc1,golem_demon_portal]
        -- (the multiloc's open child) and [oploc1,golem_portal] (a second,
        -- state-gated alias shared with Shadow of the Storm) all run the
        -- same body once %golem_a>=portal_open: "You step into the
        -- portal.", the first-time skeleton line, %golem_seen_underground=1,
        -- p_teleport(^golem_demon_lair). Which symbol the scene actually
        -- placed is discovered with world.loc_near across all four
        -- (all.loc.compack:6301-6365), not guessed.
        local door_names = { "golem_demon_door_always_open", "golem_demon_portal", "golem_portal", "golem_demon_door" }
        local door_target, door_found = nil, nil
        for i = 1, #door_names do
            local r, near = t.world.loc_near(door_names[i], 30)
            if r == "ok" and door_target == nil then
                door_target = near
                door_found = door_names[i]
            end
        end
        t.check("world.demon-door", door_target ~= nil,
            "world.loc_near tried " .. table.concat(door_names, ",") .. " -> found " .. tostring(door_found)
            .. " at " .. tostring(door_target and door_target.tile_x) .. "," .. tostring(door_target and door_target.tile_z) .. "," .. tostring(door_target and door_target.level))
        t.exec("enterPortal", t.player.click_loc, door_found or "golem_demon_door_always_open", 1)
        -- p_teleport(^golem_demon_lair) is a multi-region jump (per section 2's
        -- Abyss note): poll the player's own tile rather than a fixed tick
        -- count, since a fixed wait either races the load or wastes ticks.
        local arrived_result = t.await({
            level = function()
                local r, tile = t.world.tile()
                return r == "ok" and tile ~= nil and (tile.x > 3200 or tile.z > 4000)
            end,
            note = "arrival in the demon lair (golem_demon_lair)",
        }, 20)
        local tile_result, tile_now = t.world.tile()
        t.check("enterPortal-arrived", arrived_result == "ok",
            "await arrival -> " .. tostring(arrived_result) .. "; t.world.tile() -> " .. tostring(tile_result)
            .. " " .. tostring(tile_now and tile_now.x) .. "," .. tostring(tile_now and tile_now.z) .. "," .. tostring(tile_now and tile_now.level))
        local seen_result, seen_value = t.var.server("golem_seen_underground")
        t.check("enterPortal-seen", seen_result == "ok" and seen_value == 1,
            "var.server golem_seen_underground -> " .. tostring(seen_result) .. " " .. tostring(seen_value))

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
