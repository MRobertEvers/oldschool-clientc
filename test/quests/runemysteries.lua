-- Rune Mysteries -- driven from the quest's own scripts
-- (areas/lumbridge/scripts/duke_horacio.rs2, areas/wizard_tower/scripts/sedridor.rs2,
-- areas/varrock/scripts/aubury.rs2, quests/quest_runemysteries).
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md, owner 2026-10-03; re-driven b65): a goto only hops
-- between open tiles outside. Every closed space is entered and left by its own door, stair
-- or ladder, on every visit (routes as b62 dragon.lua / b63 losttribe.lua / b64
-- entertheabyss.lua drive them):
--   * Lumbridge castle: the keep's front doorway is open map; the north spiral stairs
--     spiralstairsbottom_3 / spiralstairsmiddle 3204,3229 (+-1 level on the tile); the Duke's
--     room behind elfdoor 3207,3222,1 (room x >= 3208, maps/m50_50.jl2).
--   * Wizards' Tower: north door fai_wiztower_poor_door 3109,3167, the ladder room's door
--     fai_wiztower_poor_door 3107,3162, the ladder wizards_tower_laddertop 3104,3162 (maplink
--     src 3105,3162 -> 3104,9576, the basement is another map frame), Sedridor's room
--     (x 3096-3107 z 9566-9574) behind poordoor 3108,9570 (maps/m48_149.jl2); back up
--     wizards_tower_ladder 3103,9576.
--   * Aubury's rune shop: fai_varrock_poor_door 3253,3398 on its north wall (shop z >= 3399).
-- Overland hops (reach.py, doors closed, margins 30/80/160): castle courtyard 3222,3218 ->
-- tower island 3109,3169 REACH len=186; island -> Aubury's street 3253,3396 REACH len=441,
-- and back REACH len=441. The fixture's tile 3206,3233 walks to the stair foot 3205,3228
-- (REACH len=54), so the run has no starting goto at all.
return {
    id = "runemysteries",
    fixture = "fresh_lumbridge.ini",
    setup = {},

    run = function(t)
        t.quest.bind({
            varp = "varp63_runemysteries",
            constants = {
                complete = 6,
                given_package = 4,
                given_talisman = 2,
                not_started = 0,
                questpoints = 1,
                received_notes = 5,
                received_package = 3,
                started = 1,
            },
            row = "quest_runemysteries",
            display = "Rune Mysteries",
            points = 1,
        })

        -- ---------------------------------------------------------- crossings
        local function walk(name, points, level)
            t.exec(name, t.player.walk_route, points, { level = level or 0 })
        end
        local function door(name, closed_sym, open_sym, door_x, door_z, door_level, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.exec(name, t.player.pass_door, { closed = closed_sym, open = open_sym, at = { door_x, door_z, door_level },
                near = { near_x, near_z }, far = { far_x, far_z }, far_ok = far_ok, far_desc = far_desc })
        end

        -- Lumbridge castle: level 0 at the north stair foot <-> the Duke's room on level 1.
        local function up_to_duke(tag)
            t.exec(tag .. ".stairsUp", t.player.climb, { loc = "spiralstairsbottom_3", op = 1, op_name = "Climb-up",
                at = { 3204, 3229, 0 }, src = { 3205, 3228 }, dest = { 3205, 3228, 1 } })
            door(tag .. ".dukeDoorIn", "elfdoor", "elfdooropen", 3207, 3222, 1, 3207, 3222, 3209, 3221,
                function(tt) return tt.x >= 3208 and tt.level == 1 end, "in the Duke's room, x >= 3208")
        end
        local function down_from_duke(tag)
            door(tag .. ".dukeDoorOut", "elfdoor", "elfdooropen", 3207, 3222, 1, 3208, 3222, 3206, 3224,
                function(tt) return tt.x <= 3207 and tt.level == 1 end, "out of the Duke's room, x <= 3207")
            walk(tag .. ".toStairsTop", { { 3205, 3228 } }, 1)
            t.exec(tag .. ".stairsDown", t.player.climb, { loc = "spiralstairsmiddle", op = 3, op_name = "Climb-down",
                at = { 3204, 3229, 1 }, dest = { 3205, 3228, 0 } })
        end

        -- Wizards' Tower: the open island outside the north door (3109,3169) <-> Sedridor's room.
        local function down_to_sedridor(tag)
            door(tag .. ".towerIn", "fai_wiztower_poor_door", "fai_wiztower_poor_door_open", 3109, 3167, 0, 3109, 3168, 3109, 3164,
                function(tt) return tt.z <= 3166 and tt.level == 0 end, "inside the tower's hall, z <= 3166")
            door(tag .. ".ladderRoomIn", "fai_wiztower_poor_door", "fai_wiztower_poor_door_open", 3107, 3162, 0, 3108, 3163, 3105, 3162,
                function(tt) return tt.x <= 3106 and tt.level == 0 end, "inside the ladder room, x <= 3106")
            t.exec(tag .. ".ladderDown", t.player.climb, { loc = "wizards_tower_laddertop", op = 1, op_name = "Climb-down",
                at = { 3104, 3162, 0 }, src = { 3105, 3162 }, dest = { 3104, 9576, 0 } })
            door(tag .. ".sedridorDoorIn", "poordoor", "poordooropen", 3108, 9570, 0, 3109, 9570, 3106, 9570,
                function(tt) return tt.x <= 3107 and tt.z >= 9566 and tt.z <= 9574 end, "inside Sedridor's room, x <= 3107")
        end
        local function up_from_sedridor(tag)
            door(tag .. ".sedridorDoorOut", "poordoor", "poordooropen", 3108, 9570, 0, 3107, 9570, 3109, 9570,
                function(tt) return tt.x >= 3108 end, "in the corridor east of Sedridor's door, x >= 3108")
            t.exec(tag .. ".ladderUp", t.player.climb, { loc = "wizards_tower_ladder", op = 1, op_name = "Climb-up",
                at = { 3103, 9576, 0 }, src = { 3104, 9576 }, dest = { 3105, 3162, 0 } })
            door(tag .. ".ladderRoomOut", "fai_wiztower_poor_door", "fai_wiztower_poor_door_open", 3107, 3162, 0, 3105, 3162, 3108, 3163,
                function(tt) return tt.x >= 3107 and tt.level == 0 end, "back in the tower's hall, x >= 3107")
            door(tag .. ".towerOut", "fai_wiztower_poor_door", "fai_wiztower_poor_door_open", 3109, 3167, 0, 3109, 3165, 3109, 3169,
                function(tt) return tt.z >= 3167 and tt.level == 0 end, "outside the tower's north door, z >= 3167")
        end

        -- Aubury's shop: the street south of it (3253,3396) <-> inside (z >= 3399).
        local function into_aubury(tag)
            door(tag .. ".shopDoorIn", "fai_varrock_poor_door", "fai_varrock_poor_door_open", 3253, 3398, 0, 3253, 3397, 3253, 3400,
                function(tt) return tt.z >= 3399 and tt.level == 0 end, "inside Aubury's shop, z >= 3399")
        end
        local function out_of_aubury(tag)
            door(tag .. ".shopDoorOut", "fai_varrock_poor_door", "fai_varrock_poor_door_open", 3253, 3398, 0, 3253, 3400, 3253, 3396,
                function(tt) return tt.z <= 3397 and tt.level == 0 end, "on the street south of the shop, z <= 3397")
        end

        -- Start quest: talk to Duke Horacio. The fixture stands the player on open ground north of
        -- the keep (3206,3233): walk in by the open front doorway to the north stair foot.
        walk("toDuke.toStairs", { { 3211, 3231 }, { 3217, 3230 }, { 3220, 3226 }, { 3221, 3220 },
            { 3215, 3219 }, { 3214, 3226 }, { 3207, 3227 }, { 3205, 3228 } })
        up_to_duke("toDuke")
        t.exec("talk-duke", t.player.talk_to, "duke_of_lumbridge")

        -- Duke greeting + options
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-duke-greeting", drain1_result == "ok", drain1_detail)
        t.exec("choose-quests-duke", t.chat.choose, "Have you any quests for me?")

        -- Accept the quest: choose "Sure, no problem." -- duke_horacio.rs2
        -- [label,duke_rune_mysteries_start] hands over the air talisman right
        -- here (inv_add(inv, air_talisman, 1)) before %runemysteries even
        -- moves to started, so it is already in the backpack by the time
        -- drain3 below settles.
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-duke-offer", drain2_result == "ok", drain2_detail)
        t.exec("choose-help", t.chat.choose, "Sure, no problem.")

        -- Drain the acceptance dialogue
        local drain3_result, drain3_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-duke-accept", drain3_result == "ok", drain3_detail)

        -- Verify the quest varp is in the right state -- the only stage
        -- check needed here; quest.stage() alone (select(1,...) == "ok")
        -- only proves the varp read succeeded and duplicates this row.
        t.exec("quest.stage.started", t.quest.expect_stage, "started")

        -- Next: Go to Wizard Tower to get Air Talisman from Sedridor (head_wizard)
        down_from_duke("toSedridor")
        walk("toSedridor.toCourtyard", { { 3207, 3227 }, { 3214, 3226 }, { 3215, 3219 }, { 3222, 3218 } })
        -- overland: the castle courtyard -> the open island outside the tower's north door
        t.exec("goto-wizard-tower", t.player.goto_tile, 3109, 3169, 0)
        down_to_sedridor("toSedridor")
        t.exec("talk-sedridor", t.player.talk_to, "head_wizard")

        -- Drain the initial greeting and get options
        local drain_sedridor_result, drain_sedridor_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-sedridor-greeting", drain_sedridor_result == "ok", drain_sedridor_detail)

        -- Choose "I'm looking for the head wizard."
        t.exec("choose-head-wizard", t.chat.choose, "I'm looking for the head wizard.")

        -- Continue the dialogue until we get options about the talisman
        local drain_talisman_result, drain_talisman_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-sedridor-talisman", drain_talisman_result == "ok", drain_talisman_detail)

        -- Check we are carrying the air talisman the Duke gave us, with the
        -- actual reading in the detail (not just a bare boolean) -- this is
        -- what sedridor.rs2's [label,seridor_3] itself checks
        -- (inv_total(inv, air_talisman) = 0) before it will accept the
        -- talisman below.
        local has_result, has_bool = t.inv.has("air_talisman")
        local count_result, count_val = t.inv.count("air_talisman")
        t.check("inv-has-talisman", has_result == "ok" and has_bool == true,
            "inv.has(air_talisman) -> " .. tostring(has_result) .. " " .. tostring(has_bool)
                .. " count=" .. tostring(count_val) .. "(" .. tostring(count_result) .. ")")

        -- Choose to give the talisman (both options lead here eventually)
        t.exec("choose-give-talisman", t.chat.choose, "Ok, here you are.")

        -- Sedridor responds with excitement and gives us a package to deliver
        local drain_package_result, drain_package_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-sedridor-package", drain_package_result == "ok", drain_package_detail)

        -- Accept to deliver the package
        t.exec("choose-accept-package", t.chat.choose, "Yes, certainly.")

        -- Drain the rest of the dialogue
        local drain_package_delivery_result, drain_package_delivery_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-package-delivery", drain_package_delivery_result == "ok", drain_package_delivery_detail)

        -- Advance time to settle the dialogue
        t.ticks(1)

        -- Verify we have the package
        t.exec("quest.stage.received-package", t.quest.expect_stage, "received_package")

        -- Now go to Varrock to find Aubury at the rune shop
        up_from_sedridor("toAubury")
        -- overland: the tower island -> the street south of Aubury's shop door
        t.exec("goto-aubury", t.player.goto_tile, 3253, 3396, 0)
        into_aubury("toAubury")

        -- Find and talk to Aubury
        t.exec("talk-aubury", t.player.talk_to, "aubury")

        -- Handle the dialogue with Aubury and give him the package
        local drain_aubury_result, drain_aubury_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-aubury-dialogue", drain_aubury_result == "ok", drain_aubury_detail)

        -- Choose to give him the package
        t.exec("choose-give-package", t.chat.choose, "I have been sent here with a package for you.")

        -- Drain the rest of the dialogue
        local drain_aubury_accept_result, drain_aubury_accept_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-aubury-accept", drain_aubury_accept_result == "ok", drain_aubury_accept_detail)

        -- Verify the quest stage changed to "given_package"
        t.exec("quest.stage.given-package", t.quest.expect_stage, "given_package")

        -- Talk to Aubury again to get the research notes back
        t.exec("talk-aubury-notes", t.player.talk_to, "aubury")

        -- Drain the dialogue where he gives us the notes
        local drain_notes_result, drain_notes_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-aubury-notes", drain_notes_result == "ok", drain_notes_detail)

        -- Verify the quest stage changed to "received_notes"
        t.exec("quest.stage.received-notes", t.quest.expect_stage, "received_notes")

        -- Now return to Sedridor with the research notes
        out_of_aubury("toSedridorFinal")
        -- overland: Aubury's street -> the open island outside the tower's north door
        t.exec("goto-sedridor-final", t.player.goto_tile, 3109, 3169, 0)
        down_to_sedridor("toSedridorFinal")
        -- The talisman went to Sedridor at the first visit; the reward hands it back. Read the
        -- count before the final talk so the reward row is a 0 -> 1 delta.
        local pre_r, pre_n = t.inv.count("air_talisman")
        t.check("reward.air_talisman.before", pre_r == "ok" and pre_n == 0,
            "inv.count(air_talisman) before the notes -> " .. tostring(pre_r) .. " " .. tostring(pre_n) .. " (want 0)")
        t.exec("talk-sedridor-final", t.player.talk_to, "head_wizard")

        -- Complete the quest dialogue -- sedridor.rs2 [label,head_wizard_notes]
        -- ends "You hand the head wizard the research notes. He hands you
        -- back the Air Talisman.", deletes research_notes, re-adds
        -- air_talisman, and queues rune_mysteries_complete.
        local drain_complete_result, drain_complete_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-quest-completion", drain_complete_result == "ok", drain_complete_detail)

        -- Completion is asynchronous: the queued completion (varp write,
        -- quest_complete_rewards, reward scroll mount) lands a couple of
        -- ticks after the mesbox that triggers it, not the instant the
        -- drain call returns.
        t.ticks(3)

        -- What the reward scroll itself advertises, read BEFORE
        -- quest.expect_complete() closes it -- quest_runemysteries.rs2's own
        -- [queue,rune_mysteries_complete] documents both halves of the
        -- reward: ~quest_complete_rewards(quest_runemysteries,
        -- "Access to mine rune essence|Air talisman", air_talisman). Assert
        -- those literal strings, not a number read back from the scroll.
        local rewards_result, rewards_detail = t.scroll.rewards()
        local rewards_lines = (rewards_result == "ok" and type(rewards_detail) == "table"
                and type(rewards_detail.lines) == "table")
            and table.concat(rewards_detail.lines, " | ") or tostring(rewards_detail)
        local has_essence_line = rewards_result == "ok"
            and string.find(rewards_lines, "Access to mine rune essence", 1, true) ~= nil
        local has_talisman_line = rewards_result == "ok"
            and string.find(rewards_lines, "Air talisman", 1, true) ~= nil
        t.check("reward.scroll_lines", has_essence_line and has_talisman_line,
            "scroll.rewards -> " .. tostring(rewards_result) .. " lines=[" .. rewards_lines .. "]")

        -- The committed state: varp at ^runemysteries_complete, the reward
        -- scroll's title, +1 quest point, and the journal entry. Writes its
        -- own four rows (quest.varp_complete, quest.scroll_title,
        -- quest.points, quest.journal) and closes the reward scroll.
        t.quest.expect_complete()

        -- The other half of the documented reward -- the Air talisman
        -- sedridor.rs2 hands back in the same mesbox that queued
        -- completion -- asserted as the literal item/count the quest
        -- documents, not a value read back from the scroll.
        t.exec("reward.air_talisman", t.inv.expect_has, "air_talisman", 1)

        t.finish(0)
    end,
}
