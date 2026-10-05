-- Witch's Potion quest test.
-- The fixture (fresh_lumbridge.ini) stands the player at 3206,3233,0 (Lumbridge, beside Hans).
-- Setup gives all the ingredients required for this tier-1 quest.

return {
    id = "hetty",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        -- getItemRequirements() only: onion, burnt_meat, eye_of_newt are
        -- brought-along (WitchsPotion.java). rats_tail is NOT on that list --
        -- the guide's own killRat step ("Kill a rat in the house to the west
        -- for a rat tail.") makes it the quest's own deliverable (trap 16),
        -- driven below with a real fight and a ground-item pickup.
        "::give onion 1",
        "::give burnt_meat 1",
        "::give eye_of_newt 1",
        -- Kit for the rat fight's margin row (lowest hp >= 25 AND food left): a fresh
        -- level-3 has 10 hitpoints, so the margin is staged deliberately. A rat_indoors
        -- hits at most 1, so the trout should never be eaten.
        "::setlevel hitpoints 30",
        "::give trout 2",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp67_hetty",
            constants = {
                complete = 3,
                not_started = 0,
                objects_given = 2,
                questpoints = 1,
                started = 1,
            },
            row = "quest_witchspotion",
            display = "Witch's Potion",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3)
        t.expect("hetty.reset", t.quest.expect_stage("not_started"))

        local function tile_text(r, tt)
            return r == "ok" and (tt.x .. "," .. tt.z .. "," .. tt.level) or tostring(r)
        end

        -- Cross one door on foot (wanted.lua's pass_door). Walk to the tile on this side of
        -- it; if the closed leaf (closed_sym) stands at door_x,door_z, click THAT copy;
        -- otherwise it already stands open (an earlier press -- doors swing back after 500
        -- ticks -- or open in the map), so assert the open leaf (open_sym) is really on or
        -- beside the door tile and do not press it again. Then walk to the far side and check
        -- the tile.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and nt.level == 0 and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. "," .. door_z .. " -> " .. tile_text(nr, nt))
            local cr, cd = t.world.loc_near(closed_sym, 3)
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z then
                t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 3)
                t.check(prefix .. ".doorStandsOpen", orr == "ok" and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. ": " .. (cr == "ok" and ("nearest closed copy at " .. cd.tile_x .. "," .. cd.tile_z) or tostring(cr))
                        .. "; " .. open_sym .. ": " .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z) or tostring(orr))
                        .. " (want within 1 of the door tile: already standing open, so it is walked through, not pressed again)")
            end
            t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and ft.level == 0 and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- Hetty's house (x 2965-2970, z 3203-3208, maps/m46_50.jl2) is walled in; its one
        -- door is poordoor on the east edge of 2964,3206. The rat house west of it (x
        -- 2953-2960, z 3202-3205) has its doorway at 2956,3205/3206 whose leaf the map places
        -- OPEN (poordooropen at 2956,3205), so it is asserted, never pressed.
        local function in_hetty(tt) return tt.x >= 2965 and tt.x <= 2970 and tt.z >= 3203 and tt.z <= 3208 end
        local function in_rat_house(tt) return tt.x >= 2953 and tt.x <= 2960 and tt.z >= 3202 and tt.z <= 3205 end
        local function enter_hetty(prefix)
            pass_door(prefix, "poordoor", "poordooropen", 2964, 3206, 2964, 3206, 2966, 3206, in_hetty,
                "inside Hetty's house, x 2965-2970 z 3203-3208")
        end
        local function leave_hetty(prefix)
            pass_door(prefix, "poordoor", "poordooropen", 2964, 3206, 2965, 3206, 2963, 3206,
                function(tt) return tt.x <= 2964 and tt.x >= 2961 and tt.z >= 3205 and tt.z <= 3207 end,
                "the street west of Hetty's door, x 2961-2964 z 3205-3207")
        end

        -- First goto of the run: the fixture's Lumbridge courtyard (3206,3233) to the open
        -- street west of Hetty's door (2962,3206) -- overland travel between open tiles.
        t.exec("goto-hettyStreet", t.player.goto_tile, 2962, 3206, 0)
        enter_hetty("hetty.enterHouse")
        -- Talk to Hetty in Rimmington.
        t.exec("talkToWitch", t.player.talk_to, "hetty", 1)

        -- Drain to the first options menu
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.expect("hetty.drain_to_quest_option", drain1_result, drain1_detail)
        t.shot("hetty-quest-options")

        -- Choose "I am in search of a quest."
        t.exec("hetty.choose_quest", t.chat.choose, "I am in search of a quest.")

        -- Drain to the second options menu
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "options" })
        t.expect("hetty.drain_to_darker_option", drain2_result, drain2_detail)
        t.shot("hetty-darker-options")

        -- Choose "Yes help me become one with my darker side."
        t.exec("hetty.choose_darker", t.chat.choose, "Yes help me become one with my darker side.")

        -- Drain to the end of the accept dialogue
        local drain3_result, drain3_detail = t.chat.drain({ stop_at = "none" })
        t.expect("hetty.drain_accept_close", drain3_result, drain3_detail)

        t.expect("hetty.expect_stage_started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------------------
        -- killRat (WitchsPotion.java): "Kill a rat in the house to the west
        -- for a rat tail." rat_indoors spawns at 2953-2960,3202-3205,0
        -- (areas/world/configs/m46_50.spawn), the house west of Hetty's own
        -- 2968,3205 -- rats_tail is a real ground drop from the kill
        -- (drop_tables/scripts/rat.rs2's [ai_queue3,rat_indoors] block),
        -- never a cheat (trap 16).
        -- ---------------------------------------------------------------
        -- Out of Hetty's house through her door, then on foot (8 tiles) to the rat
        -- house's doorway and in through its open leaf.
        leave_hetty("hetty.leaveHouse")
        pass_door("ratHouse.enter", "poordoor", "poordooropen", 2956, 3205, 2956, 3207, 2956, 3204, in_rat_house,
            "inside the rat house, x 2953-2960 z 3202-3205")

        local rat_attack_result, rat_attack_detail = t.player.attack("rat_indoors", 2, 20)
        t.check("attackRat", rat_attack_result == "ok", tostring(rat_attack_result) .. " " .. tostring(rat_attack_detail))
        -- rat_indoors spawns four-deep in this house -- await_dead_engaged
        -- (trap 21) follows the SLOT the attack pressed, not a bare symbol
        -- lookup that a crowded spawn could hand to a neighbour's corpse.
        local _, kill_detail = t.exec("killRat", t.npc.await_dead_engaged, 40, 40, { eat = { item = "trout", below = 20 } })
        t.expect("player.aliveAfterRat", t.player.alive())
        local rat_low = tonumber(tostring(kill_detail):match("lowest hp (%d+)/"))
        local trout_result, trout_left = t.inv.count("trout")
        t.check("killRat.margin", rat_low ~= nil and rat_low >= 25 and trout_result == "ok" and trout_left >= 1,
            "lowest hp " .. tostring(rat_low) .. "/30 (staged ::setlevel hitpoints 30), trout staged 2, left "
                .. tostring(trout_left) .. " (" .. tostring(trout_result) .. ") -- margin: lowest hp >= 25 AND trout left >= 1")

        -- rats_tail is a real ground drop (obj_add at npc_coord in
        -- [ai_queue3,rat_indoors]), not a chat grant -- poll the entity pool
        -- before clicking, same idiom as hero.lua's grip_keys pickup.
        local tail_visible_result = t.await({
            level = function()
                return t.world.obj_near("rats_tail", 10) == "ok"
            end,
            note = "waiting for the dead rat's dropped tail to reach the client's entity pool",
        }, 10)
        t.step("ratTail.visible", tail_visible_result == "ok" and "PASS" or "FAIL",
            "t.world.obj_near(rats_tail, 10) polled up to 10 ticks -> " .. tostring(tail_visible_result))

        local tail_before_result, tail_before = t.inv.count("rats_tail")
        local tail_click_result, tail_click_detail = t.player.click_obj("rats_tail")
        if tail_click_result ~= "ok" then
            -- the rat's corpse/death animation can cover its ground drop for
            -- a moment (the same "covered" geometry class section 8
            -- describes for a loc) -- settle and press once more.
            t.ticks(3)
            tail_click_result, tail_click_detail = t.player.click_obj("rats_tail")
        end
        t.inv.await("rats_tail", 1, 10)
        local tail_after_result, tail_after = t.inv.count("rats_tail")
        local tail_pass = tail_click_result == "ok" and tail_after_result == "ok"
            and tail_after > (tail_before_result == "ok" and tail_before or 0)
        t.step("pickUpRatTail", tail_pass and "PASS" or "FAIL",
            string.format("click_obj rats_tail -> %s (%s), count %s -> %s",
                tostring(tail_click_result), tostring(tail_click_detail), tostring(tail_before), tostring(tail_after)))
        t.shot("hetty-rat-tail-collected")

        -- Bring the ingredients to Hetty: out through the rat house's open doorway, on foot
        -- to her door, and in.
        pass_door("ratHouse.leave", "poordoor", "poordooropen", 2956, 3205, 2956, 3205, 2956, 3207,
            function(tt) return tt.x >= 2955 and tt.x <= 2957 and tt.z >= 3206 and tt.z <= 3208 end,
            "outside the rat house's doorway, x 2955-2957 z 3206-3208")
        enter_hetty("hetty.reenterHouse")
        t.exec("returnToWitch", t.player.talk_to, "hetty", 1)

        -- Drain through the completion dialogue
        local drain4_result, drain4_detail = t.chat.drain({ stop_at = "none" })
        t.expect("hetty.drain_complete_close", drain4_result, drain4_detail)

        t.expect("hetty.expect_stage_given", t.quest.expect_stage("objects_given"))

        -- Reward snapshot before the FINAL hand-in step
        -- (a status-only row on this read was dropped: expect_gain below fails on a bad snapshot)
        local _, reward_before = t.skill.snapshot()

        -- Drink from the cauldron (hettycauldron 2967,3205, inside Hetty's house) to finish off the quest.
        t.exec("drinkPotion", t.player.click_loc, "hettycauldron", 1)

        -- The completion mesbox appears: its literal text (quest_hetty.rs2:8)
        local mesbox_text_result, mesbox_text = t.chat.text()
        t.check("hetty.completion_mesbox_text", mesbox_text_result == "ok"
                and tostring(mesbox_text):find("You drink from the cauldron, it tastes horrible! You feel yourself imbued with power.", 1, true) ~= nil,
            tostring(mesbox_text_result) .. ": " .. tostring(mesbox_text))

        -- Dismissing the mesbox runs the queue that awards the quest
        local continue_result, continue_detail = t.chat.continue_()
        t.expect("hetty.completion_continue", continue_result,
            continue_detail or "chat.continue_ dismissed the completion mesbox")

        -- Completion is asynchronous; wait for the reward scroll
        t.ticks(3)

        t.quest.expect_complete()

        -- Reward checks -- the quest's actual reward, not just its completion (H2, docs/QUEST_SUITE_KIT.md).
        t.check("reward.magic", t.skill.expect_gain("magic", 325, reward_before))

        t.finish(0)
    end,
}
