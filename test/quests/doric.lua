-- Doric's Quest -- quest_doric.rs2 [opnpc1,doric] dispatch on varp doricquest
-- (0=not_started, 10=started, 100=complete).
--
-- Flow: accept the quest, receive a bronze pickaxe, commit when all three materials
-- (6 clay, 4 copper_ore, 2 iron_ore) are handed in. The quest script grants the pickaxe
-- only on the 0->10 transition ([proc,doric_accept_grant]), so this flow opens a
-- dialogue on quest stage 0, chooses the anvils option, accepts the quest (varp->10),
-- receives the pickaxe, mines the materials with it at the Rimmington mine, then talks
-- again to hand in and complete.
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md, owner 2026-10-03 / 2026-10-05): Doric stands in
-- a 9-tile room (x 2950-2953, z 3449-3452, maps/m46_53.jl2) whose only way in from outside
-- is the poordoor on the east edge of 2949,3450 (the other poordoor, 2952,3452, leads to the
-- back bedroom). Every goto lands on open ground west of the house (2946,3450) and the door
-- is pressed in and out on every visit. The Rimmington mine is open ground (reach.py:
-- REACH closed-doors from 2946,3450 to every stand tile used below); its west iron rocks
-- (2968-2971,3237-3242) sit behind the rockslides and are not used.

return {
    id = "doric",
    fixture = "fresh_lumbridge.ini",
    setup = {
        -- The guide's requirement for the iron ore (doric_give_directions: Mining 15).
        "::setlevel mining 15",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp31_doricquest",
            constants = {
                complete = 100,
                diary_blurite_limbs = 11,
                not_started = 0,
                started = 10,
            },
            row = "quest_doricsquest",
            display = "Doric's Quest",
            points = 1,
        })
        t.ticks(3)
        t.expect("doric.reset", t.quest.expect_stage("not_started"))

        local function count(item)
            local r, n = t.inv.count(item)
            if r ~= "ok" then return nil end
            return n
        end
        local function free_slots()
            local n = 0
            for i = 0, 27 do
                local r, sl = t.inv.slot(i)
                if r == "ok" and type(sl) == "table" and sl.name == "" then n = n + 1 end
            end
            return n
        end

        -- Doric's front door: poordoor on the east edge of 2949,3450 (its open leaf is
        -- poordooropen). Outside is 2949,3450 / 2948,3450; inside is 2950,3450.
        local function door_in(name)
            t.exec(name, t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 2949, 3450, 0 }, near = { 2949, 3450 }, far = { 2950, 3450 } })
        end
        local function door_out(name)
            t.exec(name, t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 2949, 3450, 0 }, near = { 2950, 3450 }, far = { 2948, 3450 } })
        end

        -- Travel from Lumbridge to open ground west of Doric's house (north of Falador).
        t.exec("goto-doric", t.player.goto_tile, 2946, 3450, 0)
        door_in("doric.doorIn")

        local pickaxe_before = count("bronze_pickaxe")
        t.check("doric.room_for_pickaxe", free_slots() >= 13,
            "free backpack slots " .. tostring(free_slots())
                .. " (want >= 13: the pickaxe, 6 clay, 4 copper ore, 2 iron ore)")

        -- Talk to Doric and see the greeting
        t.exec("doric.greet", t.player.talk_to, "doric")
        t.check("doric.expect_head", t.chat.expect_head("doric"))

        -- Drain to the first options menu
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.expect("doric.drain_to_menu", drain1_result, drain1_detail)
        t.shot("doric-menu-1")

        -- Choose "I wanted to use your anvils."
        t.exec("doric.choose_anvils", t.chat.choose, "I wanted to use your anvils.")

        -- Drain to the quest start confirmation menu
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "options" })
        t.expect("doric.drain_to_confirmation", drain2_result, drain2_detail)
        t.shot("doric-menu-confirm")

        -- Choose "Yes." to accept the quest
        t.exec("doric.accept", t.chat.choose, "Yes.")

        -- After accepting: "Where can I find those?" / "Certainly, I'll be right back!"
        local drain_directions_result, drain_directions_detail = t.chat.drain({ stop_at = "options" })
        t.expect("doric.drain_to_directions_menu", drain_directions_result, drain_directions_detail)
        t.shot("doric-directions-menu")

        t.exec("doric.skip_directions", t.chat.choose, "Certainly, I'll be right back!")

        -- Drain the rest of the dialogue
        local drain3_result, drain3_detail = t.chat.drain({ stop_at = "none" })
        t.expect("doric.drain_close", drain3_result, drain3_detail)
        t.shot("doric-dialogue-closed")

        -- Verify the quest stage changed to started
        t.expect("doric.started", t.quest.expect_stage("started"))

        -- [proc,doric_accept_grant]: inv_add(inv, bronze_pickaxe, 1) on 0 -> 10.
        t.expect("doric.pickaxe_await", t.inv.await("bronze_pickaxe", (pickaxe_before or 0) + 1, 5))
        local pickaxe_after = count("bronze_pickaxe")
        t.check("doric.pickaxe_given",
            pickaxe_before ~= nil and pickaxe_after ~= nil and pickaxe_after - pickaxe_before == 1,
            "bronze_pickaxe before=" .. tostring(pickaxe_before) .. " after=" .. tostring(pickaxe_after)
                .. " (want +1)")

        door_out("doric.doorOut")

        -- ------------------------------------------------ the materials, mined
        -- Rimmington mine (open ground south of Falador). Each rock is a one-ore rock that
        -- depletes; the copies are pressed in turn and the count is the verdict.
        t.exec("goto-mine", t.player.goto_tile, 2980, 3243, 0)

        local function mine(name, ore, stand, rocks, want)
            t.exec(name .. ".walk", t.player.walk_to, stand[1], stand[2], 30)
            local before = count(ore) or 0
            local got = before
            local presses = 0
            for att = 1, 60 do
                got = count(ore) or 0
                if got >= want then break end
                local rock = rocks[(att - 1) % #rocks + 1]
                presses = presses + 1
                t.player.click_loc(rock.sym, 1, { at = { rock.x, rock.z } })
                t.inv.await(ore, got + 1, 20)
            end
            got = count(ore) or 0
            t.check(name, got >= want,
                "mined " .. ore .. ": " .. tostring(before) .. " -> " .. tostring(got)
                    .. " in the pack (want " .. want .. ") after " .. presses .. " press(es)")
        end

        mine("gather.clay", "clay", { 2986, 3240 }, {
            { sym = "clayrock1", x = 2987, z = 3240 },
            { sym = "clayrock2", x = 2986, z = 3239 },
        }, 6)
        mine("gather.copper", "copper_ore", { 2977, 3246 }, {
            { sym = "copperrock1", x = 2977, z = 3247 },
            { sym = "copperrock2", x = 2977, z = 3245 },
            { sym = "copperrock2", x = 2976, z = 3247 },
        }, 4)
        mine("gather.iron", "iron_ore", { 2982, 3235 }, {
            { sym = "ironrock1", x = 2982, z = 3234 },
            { sym = "ironrock2", x = 2981, z = 3233 },
        }, 2)

        -- ------------------------------------------------ the hand-in
        t.exec("goto-doric.return", t.player.goto_tile, 2946, 3450, 0)
        door_in("doric.doorIn2")

        local clay_before = count("clay")
        local copper_before = count("copper_ore")
        local iron_before = count("iron_ore")
        local coins_before = count("coins")
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.note("skill.snapshot before the hand-in -> " .. tostring(xp_snapshot_result))

        -- Talk to Doric again to hand in materials
        t.exec("doric.handin_talk", t.player.talk_to, "doric")
        t.check("doric.handin_head", t.chat.expect_head("doric"))

        -- Drain through the hand-in dialogue
        local drain4_result, drain4_detail = t.chat.drain({ stop_at = "none" })
        t.expect("doric.handin_drain", drain4_result, drain4_detail)
        t.shot("doric-handin-dialogue")

        -- Verify completion
        t.ticks(2)
        t.quest.expect_complete()

        -- The literal reward of quest_doric.rs2 [proc,doric_commit]:
        -- inv_del clay 6, copper_ore 4, iron_ore 2; inv_add coins 180;
        -- stat_advance(mining, 13000) -> 1300 Mining XP; 1 quest point (expect_complete).
        t.expect("reward.mining_xp", t.skill.expect_gain("mining", 1300, xp_snapshot))
        local coins_after = count("coins")
        t.check("reward.coins",
            coins_before ~= nil and coins_after ~= nil and coins_after - coins_before == 180,
            "coins before=" .. tostring(coins_before) .. " after=" .. tostring(coins_after) .. " (want +180)")
        local clay_after = count("clay")
        local copper_after = count("copper_ore")
        local iron_after = count("iron_ore")
        t.check("handin.materials_taken",
            clay_before ~= nil and clay_after ~= nil and clay_before - clay_after == 6
                and copper_before ~= nil and copper_after ~= nil and copper_before - copper_after == 4
                and iron_before ~= nil and iron_after ~= nil and iron_before - iron_after == 2,
            "clay " .. tostring(clay_before) .. "->" .. tostring(clay_after)
                .. ", copper_ore " .. tostring(copper_before) .. "->" .. tostring(copper_after)
                .. ", iron_ore " .. tostring(iron_before) .. "->" .. tostring(iron_after)
                .. " (want -6, -4, -2)")
        t.expect("reward.coins_message", t.msg.expect("Doric hands you 180 coins."))

        door_out("doric.doorOut2")
        t.finish(0)
    end,
}
