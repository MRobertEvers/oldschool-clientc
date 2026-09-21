-- Black Knights' Fortress -- driven through the real Sir Amik Varze dialogue
-- tree (areas/falador/scripts/sir_amik_varze.rs2), the witchgrill listen and
-- the cabbage-in-the-hole sabotage (quests/quest_blackknight/scripts/
-- quest_blackknight.rs2), never through ::setvar on %spy.
--
-- Every fortress interior loc Quest Helper's guide walks through
-- (bkfortressdoor1/2/3, the ladders, bksecretdoor) is plain walk-through
-- scenery -- grepping the quest's own .rs2 shows only [oploc1,witchgrill]
-- and [oplocu,blackknighthole] ever touch %spy, and bkfortressdoor1's own
-- guard dialogue and bkfortressdoor2's banquet-hall warning are both
-- optional flavour that never gate %spy either. So this file reaches each
-- of the two %spy-writing locs directly with player.goto_tile (doc section
-- 2: "goto_tile ... climbs stairs and ladders for you", the same shortcut
-- the druid/Wizards' Tower trapdoor files use) instead of walking every
-- door and ladder the guide lists. bronze_med_helm/iron_chainbody would
-- only matter for the entrance guard's disguise check, and that guard is
-- never walked up to here, so they are given in setup (Quest Helper
-- bring-alongs -- new_quest.py left no gather-CHECK marker for either)
-- but never equipped.
--
-- %qp must be >= ^blackknight_qp_req (12) before Sir Amik offers the quest
-- at all (sir_amik_varze.rs2's prequest branch) -- a prerequisite (quest
-- points earned elsewhere), not this quest's own deliverable, so
-- ::setvar qp is the setup cheat trap 16 names for exactly this case.

return {
    id = "blackknight",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::blackknightrun",         -- content's own reset: %spy=0, clears the cauldron/armour hints and any leftover coins/cabbage/dossier
        "::give bronze_med_helm 1", -- Quest Helper bring-along (guard disguise) -- not equipped, this run never walks the guarded door
        "::give iron_chainbody 1",
        "::give cabbage 1",         -- Quest Helper bring-along (an ordinary cabbage, sourced anywhere) -- the SABOTAGE is using it on the hole, driven for real below
        "::setvar qp 12",           -- prerequisite quest points, not blackknight's own reward
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "spy",
            constants = {
                not_started = 0,
                started = 1,
                listened = 2,
                sabotaged = 3,
                complete = 4,
                questpoints = 3,
                qp_req = 12,
            },
            row = "quest_blackknightsfortress",
            display = "Black Knights' Fortress",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3) -- the setup cheats' writes are not client-visible yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ------------------------------------------------- accept the quest
        t.exec("goto-amik", t.player.goto_tile, 2960, 3336, 2)
        t.exec("amik.greet", t.player.talk_to, "sir_amik_varze")

        local d1_result, d1_detail = t.chat.drain({ stop_at = "options" })
        t.expect("amik.drain_to_quest_offer", d1_result, d1_detail)
        t.shot("amik-quest-offer")

        t.exec("amik.choose_seek_quest", t.chat.choose, "I seek a quest!")

        local d2_result, d2_detail = t.chat.drain({ stop_at = "options" })
        t.expect("amik.drain_to_danger_choice", d2_result, d2_detail)
        t.shot("amik-danger-choice")

        t.exec("amik.choose_laugh", t.chat.choose, "I laugh in the face of danger!")

        local d3_result, d3_detail = t.chat.drain({ stop_at = "options" })
        t.expect("amik.drain_to_start_confirm", d3_result, d3_detail)
        t.shot("amik-start-confirm")

        t.exec("amik.choose_start_yes", t.chat.choose, "Yes.")

        local d4_result, d4_detail = t.chat.drain({ stop_at = "none" })
        t.expect("amik.accept_drain_close", d4_result, d4_detail)
        t.shot("amik-accepted")

        -- inv_add from the dialogue's own grant is not client-visible the
        -- instant drain's last continue_ returns (section 8's inv-sync
        -- gap) -- poll, then write the settled count back as the detail.
        local dossier_wait_result = t.inv.await("bk_dossier", 1, 10)
        local dossier_count_result, dossier_count = t.inv.count("bk_dossier")
        t.check("dossier.granted",
            dossier_wait_result == "ok" and dossier_count_result == "ok" and dossier_count == 1,
            "inv.await(bk_dossier,1) -> " .. tostring(dossier_wait_result)
                .. " count=" .. tostring(dossier_count) .. "(" .. tostring(dossier_count_result) .. ")")

        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- --------------------------------------------- listen at the grill
        t.exec("goto-grill", t.player.goto_tile, 3026, 3509, 0)

        local grill_loc_result, grill_loc = t.world.loc_near("witchgrill", 20)
        t.step("grill.locate", grill_loc_result == "ok" and "PASS" or "FAIL",
            "world.loc_near(witchgrill,20) -> " .. tostring(grill_loc_result) .. " "
                .. (grill_loc_result == "ok"
                    and string.format("tile=%d,%d,%d match=%s", grill_loc.tile_x, grill_loc.tile_z,
                        grill_loc.level, tostring(grill_loc.match))
                    or tostring(grill_loc)))

        -- click_loc's own walk_near always converges on the SAME target
        -- tile no matter where this goto lands first, and its own
        -- step-off/retry ladder answered `covered` from every side it
        -- tried (measured: south, north, east -- three of the four
        -- cardinals, west never attempted). So this presses directly
        -- (drive.click_minimenu, bypassing click_loc's own approach) from
        -- all four neighbours of the loc's own placed tile in turn,
        -- including the one side the built-in retry never reaches.
        -- Pressing the row is not proof the server acted on it -- a press
        -- can register on the client (menu closes, row read back correctly)
        -- with no dialogue ever mounting, so "opened a real page" is the
        -- actual success test each attempt below waits on, not the press's
        -- own `ok`.
        local listen_result, listen_detail
        local listen_tried = ""
        local listen_row_text = "n/a"
        local listen_opened = false
        if grill_loc_result == "ok" then
            local grill_sides = {
                { grill_loc.tile_x - 1, grill_loc.tile_z, "west" },
                { grill_loc.tile_x + 1, grill_loc.tile_z, "east" },
                { grill_loc.tile_x, grill_loc.tile_z - 1, "south" },
                { grill_loc.tile_x, grill_loc.tile_z + 1, "north" },
            }
            for side_index = 1, #grill_sides do
                local side_x = grill_sides[side_index][1]
                local side_z = grill_sides[side_index][2]
                local side_name = grill_sides[side_index][3]
                t.player.goto_tile(side_x, side_z, grill_loc.level)
                for press = 1, 2 do
                    listen_result, listen_detail = t.drive.click_minimenu(grill_loc, 1)
                    listen_tried = listen_tried .. side_name .. "#" .. press .. "=" .. tostring(listen_result) .. " "
                    if listen_result == "ok" then
                        if type(listen_detail) == "table" then
                            listen_row_text = tostring(listen_detail.row_text)
                        end
                        local open_result = t.await({
                            level = function() return t.chat.kind() ~= "none" end,
                            note = "listen.grill: waiting for the grill's own dialogue",
                        }, 8)
                        if open_result == "ok" then
                            listen_opened = true
                            break
                        end
                    end
                end
                if listen_opened then
                    break
                end
            end
        end
        t.step("listen.grill", listen_opened and "PASS" or "FAIL",
            "drive.click_minimenu(witchgrill,1) attempts: " .. listen_tried
                .. "-- pressed row: " .. listen_row_text .. " -- dialogue opened: " .. tostring(listen_opened))
        t.shot("listen-grill")

        local d5_result, d5_detail = t.chat.drain({ stop_at = "none" })
        t.expect("grill.dialogue_drain", d5_result, d5_detail)
        t.shot("grill-dialogue-closed")

        t.expect("quest.stage.listened", t.quest.expect_stage("listened"))

        -- ---------------------------------------------- sabotage the potion
        t.exec("goto-hole", t.player.goto_tile, 3026, 3510, 1)

        local hole_loc_result, hole_loc = t.world.loc_near("blackknighthole", 20)
        t.step("hole.locate", hole_loc_result == "ok" and "PASS" or "FAIL",
            "world.loc_near(blackknighthole,20) -> " .. tostring(hole_loc_result) .. " "
                .. (hole_loc_result == "ok"
                    and string.format("tile=%d,%d,%d match=%s", hole_loc.tile_x, hole_loc.tile_z,
                        hole_loc.level, tostring(hole_loc.match))
                    or tostring(hole_loc)))
        if hole_loc_result == "ok" then
            t.exec("goto-hole-exact", t.player.goto_tile, hole_loc.tile_x, hole_loc.tile_z, hole_loc.level)
        end

        local hole_target, hole_lookup_result = t.player.by_symbol("loc", "blackknighthole")
        t.step("hole.lookup", hole_lookup_result == "ok" and "PASS" or "FAIL",
            "player.by_symbol(loc, blackknighthole) -> " .. tostring(hole_lookup_result))

        t.exec("use.cabbage_on_hole", t.player.use_on, "cabbage", hole_target)

        local d6_result, d6_detail = t.chat.drain({ stop_at = "none" })
        t.expect("hole.sabotage_drain", d6_result, d6_detail)
        t.shot("hole-sabotage-closed")

        t.expect("quest.stage.sabotaged", t.quest.expect_stage("sabotaged"))

        local cabbage_absent_result, cabbage_absent_detail = t.inv.expect_absent("cabbage")
        t.check("cabbage.consumed", cabbage_absent_result == "ok",
            "inv.expect_absent(cabbage) -> " .. tostring(cabbage_absent_result) .. " " .. tostring(cabbage_absent_detail))

        -- ------------------------------------------------------- hand in
        t.exec("goto-amik-return", t.player.goto_tile, 2960, 3336, 2)

        local reward_coins_before_result, reward_coins_before = t.inv.count("coins")

        t.exec("amik.return_talk", t.player.talk_to, "sir_amik_varze")

        -- Stop AT a mesbox rather than draining past it: the hand-in's own
        -- [queue,black_knights_fortress_quest_complete] can land mid-drain
        -- (its own "Sir Amik hands you 2500 coins." mesbox arriving while
        -- this monologue is still being clicked through) or only after the
        -- monologue's dialogue has fully closed -- section 8's completion-is-
        -- asynchronous gap. Either way this single drain call never
        -- swallows that mesbox as an ordinary page of the conversation.
        local d7_result, d7_kind = t.chat.drain({ stop_at = "mesbox" })
        t.expect("amik.return_drain", d7_result, d7_kind)
        t.shot("amik-return-closed")

        if d7_kind ~= "mesbox" then
            local mesbox_wait_result, mesbox_wait_detail = t.await({
                level = function() return t.chat.kind() == "mesbox" end,
                note = "handin: waiting for the queued coins mesbox",
            }, 20)
            t.expect("handin.await_mesbox", mesbox_wait_result, mesbox_wait_detail)
        end

        t.exec("handin.mesbox_text", t.chat.expect_text, "Sir Amik hands you 2500 coins.")
        t.exec("handin.dismiss_mesbox", t.chat.continue_, true)

        t.quest.expect_complete()

        local reward_coins_after_result, reward_coins_after = t.inv.count("coins")
        t.check("reward.coins",
            reward_coins_before_result == "ok" and reward_coins_after_result == "ok"
                and reward_coins_after == reward_coins_before + 2500,
            string.format("coins %s -> %s (want +2500), reads %s/%s",
                tostring(reward_coins_before), tostring(reward_coins_after),
                tostring(reward_coins_before_result), tostring(reward_coins_after_result)))

        t.finish(0)
    end,
}
