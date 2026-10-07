-- Imp Catcher: start with Wizard Mizgog (Wizards' Tower level 2), hunt imps
-- for the four coloured beads, climb back and hand them in.
-- Guide: helpers/quests/impcatcher (collectBeads, moveToTower, climbUpF1, turnInQuest).
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md, owner 2026-10-03; first goto, owner 2026-10-05;
-- re-driven b68): a goto only hops between open tiles outside. The Wizards' Tower is entered and
-- left by its doors and stairs on every visit (routes as runemysteries.lua / demon.lua):
--   * the north door fai_wiztower_poor_door 3109,3167 (island 3109,3169 <-> hall z <= 3166);
--   * the stair room's diagonal door fai_wiztower_poor_door 3107,3162 (hall <-> x <= 3106);
--   * fai_wiztower_spiralstairs 3103,3159 up (maplink 0_48_49_33_24 -> 3104,3161,1), the
--     middle fai_wiztower_spiralstairs_middle op2 Climb-up (no maplink: +1 on the tile) to
--     level 2, where Mizgog stands (m48_49.spawn: 3103,3163,2, wanderrange 0) with no door
--     between him and the stair top; down by fai_wiztower_spiralstairstop 3104,3160,2
--     (-1 on the tile) and the middle op3 Climb-down (maplink 1_48_49_31_25 -> 3104,3161,0).
-- Overland hops (reach.py, doors closed, margins 30/80/160): the fixture's tile 3206,3233 ->
-- the tower island 3109,3169 REACH len=171; island -> the imp ground in Lumbridge 3217,3224
-- REACH len=187, and back the same. The fixture's tile walks to 3217,3224 (REACH len=20): open
-- ground, the castle keep's doorway is open map.
return {
    id = "imp",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- a real imp hunt: each bead is 5/128 per kill, roughly fifty kills for four colours
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::setlevel attack 60", -- arms the imp hunt: fists let an imp teleport away mid-fight
        "::setlevel strength 60",
        "::give rune_scimitar 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp160_imp",
            constants = {
                not_started = 0,
                started = 1,
                complete = 2,
            },
            row = "quest_impcatcher",
            display = "Imp Catcher",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------- crossings
        local TOWER_DOOR, TOWER_DOOR_OPEN = "fai_wiztower_poor_door", "fai_wiztower_poor_door_open"
        -- The tower island outside the north door (3109,3169) -> Mizgog's floor (level 2).
        local function up_to_mizgog(tag)
            t.exec(tag .. ".towerIn", t.player.pass_door, { closed = TOWER_DOOR, open = TOWER_DOOR_OPEN,
                at = { 3109, 3167, 0 }, near = { 3109, 3168 }, far = { 3109, 3164 },
                far_ok = function(tt) return tt.z <= 3166 and tt.level == 0 end, far_desc = "inside the tower's hall, z <= 3166" })
            t.exec(tag .. ".stairRoomIn", t.player.pass_door, { closed = TOWER_DOOR, open = TOWER_DOOR_OPEN,
                at = { 3107, 3162, 0 }, near = { 3108, 3163 }, far = { 3105, 3162 },
                far_ok = function(tt) return tt.x <= 3106 and tt.level == 0 end, far_desc = "inside the stair room, x <= 3106" })
            t.exec(tag, t.player.climb, { loc = "fai_wiztower_spiralstairs", op = 1, op_name = "Climb-up",
                at = { 3103, 3159, 0 }, src = { 3105, 3160 }, dest = { 3104, 3161, 1 } })
            t.exec(tag .. ".climbUpF1", t.player.climb, { loc = "fai_wiztower_spiralstairs_middle", op = 2,
                op_name = "Climb-up", at = { 3103, 3159, 1 }, dest = { 3104, 3161, 2 }, slack = 1 })
        end
        -- Mizgog's floor (level 2) -> the tower island outside the north door.
        local function down_from_mizgog(tag)
            t.exec(tag .. ".stairsDownF2", t.player.climb, { loc = "fai_wiztower_spiralstairstop", op = 1,
                op_name = "Climb-down", at = { 3104, 3160, 2 }, dest = { 3104, 3161, 1 }, slack = 1 })
            t.exec(tag .. ".stairsDownF1", t.player.climb, { loc = "fai_wiztower_spiralstairs_middle", op = 3,
                op_name = "Climb-down", at = { 3103, 3159, 1 }, src = { 3103, 3161 }, dest = { 3104, 3161, 0 } })
            t.exec(tag .. ".stairRoomOut", t.player.pass_door, { closed = TOWER_DOOR, open = TOWER_DOOR_OPEN,
                at = { 3107, 3162, 0 }, near = { 3105, 3162 }, far = { 3108, 3163 },
                far_ok = function(tt) return tt.x >= 3107 and tt.level == 0 end, far_desc = "back in the tower's hall, x >= 3107" })
            t.exec(tag .. ".towerOut", t.player.pass_door, { closed = TOWER_DOOR, open = TOWER_DOOR_OPEN,
                at = { 3109, 3167, 0 }, near = { 3109, 3165 }, far = { 3109, 3169 },
                far_ok = function(tt) return tt.z >= 3167 and tt.level == 0 end, far_desc = "outside the tower's north door, z >= 3167" })
        end

        -- moveToTower / climbUpF1: overland from the fixture's open ground (3206,3233) to the tower
        -- island, in by both doors, up two flights to Mizgog (level 2).
        t.exec("goto-moveToTower", t.player.goto_tile, 3109, 3169, 0)
        up_to_mizgog("moveToTower")

        -- Start the quest with Mizgog.
        t.exec("startQuest", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "player:Give me a quest!",
            "npc:Give me a quest what?",
            "choose:Give me a quest please.",
            "player:Give me a quest please.",
            "npc:Well seeing as you asked nicely",
            "npc:The wizard Grayzag",
            "npc:These imps stole",
            "npc:But they stole my four magical beads",
            "npc:These imps have now spread out",
            "choose:Yes.",
            "player:I'll try.",
            "npc:That's great, thank you.",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- collectBeads: down both flights, out by both doors, overland to Lumbridge's open
        -- ground and kill imps until all four colours are held.
        t.exec("armHunt", t.player.equip, "rune_scimitar")
        down_from_mizgog("leaveTower")
        t.exec("goto-collectBeads", t.player.goto_tile, 3217, 3224, 0)
        local colours = { "black_bead", "red_bead", "white_bead", "yellow_bead" }
        local kills = 0
        local hunt_log = ""
        -- The fight's margin: the lowest hp read after each kill (imp: hitpoints 8, attack 2,
        -- strength 1, lumbridge.npc:226). No food is staged: the guide recommends none and an imp
        -- hits at most 1, so the row grades hp alone.
        local lowest_hp, max_hp = nil, nil
        for attempt = 1, 250 do
            local held = 0
            for _, colour in ipairs(colours) do
                local _, count = t.inv.count(colour)
                if count and count > 0 then held = held + 1 end
            end
            if held == 4 then break end
            -- A chased imp can pull the player off the ground: walk back (a graded row) when away.
            local _, here = t.world.tile()
            if here and (math.abs(here.x - 3217) > 4 or math.abs(here.z - 3224) > 4) then
                t.exec("collectBeads.regroup", t.player.walk_to, 3217, 3224)
            end
            t.cheat("::spawn imp 1")
            t.ticks(2)
            local attack_result = t.player.attack("imp", 2, 20)
            local dead_result = t.npc.await_dead_engaged(60, 6)
            if dead_result == "ok" then kills = kills + 1 end
            local hp_result, hp = t.skill.read("hitpoints")
            if hp_result == "ok" and type(hp) == "table" and hp.level then
                if lowest_hp == nil or hp.level < lowest_hp then lowest_hp = hp.level end
                max_hp = hp.base_level
            end
            t.ticks(2)
            for _, colour in ipairs(colours) do
                local near_result = t.world.obj_near(colour, 6)
                local _, count = t.inv.count(colour)
                if near_result == "ok" and (count or 0) == 0 then
                    t.player.click_obj(colour)
                    t.inv.await(colour, 1, 8)
                end
            end
            hunt_log = "attempt " .. attempt .. " attack=" .. tostring(attack_result) .. " dead=" .. tostring(dead_result)
        end
        local bead_summary = {}
        local bead_total = 0
        for _, colour in ipairs(colours) do
            local _, count = t.inv.count(colour)
            bead_summary[#bead_summary + 1] = colour .. "=" .. tostring(count)
            if (count or 0) > 0 then bead_total = bead_total + 1 end
        end
        t.check("collectBeads", bead_total == 4,
            "kills " .. kills .. "; " .. table.concat(bead_summary, " ") .. "; last " .. hunt_log)
        t.check("collectBeads.margin", lowest_hp ~= nil and max_hp ~= nil and lowest_hp * 4 >= max_hp,
            "lowest hp " .. tostring(lowest_hp) .. "/" .. tostring(max_hp) .. " over " .. kills
            .. " kill(s) (margin: lowest hp >= a quarter of max; no food carried)")

        -- Back to the tower island, in by both doors and up both flights; hand the four beads in.
        t.exec("goto-moveToTower2", t.player.goto_tile, 3109, 3169, 0)
        up_to_mizgog("moveToTower-return")

        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("reward.snapshot", snapshot_result == "ok", "skill.snapshot before the hand-in -> " .. tostring(snapshot_result))
        local _, amulet_before = t.inv.count("amulet_of_accuracy")
        local beads_before = {}
        for _, colour in ipairs(colours) do
            local _, count = t.inv.count(colour)
            beads_before[colour] = count or 0
        end

        t.exec("turnInQuest", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("turnInQuest-dialog", t.chat.play, {
            "npc:So how are you doing finding my beads",
            "player:I've got all four beads",
            "npc:Give them here",
            "mesbox:You give four coloured beads",
        })
        t.ticks(4)
        t.chat.drain({ stop_at = "none", max_pages = 4 })

        t.quest.expect_complete()

        t.check("reward.magic", t.skill.expect_gain("magic", 875, snapshot))
        local _, amulet_after = t.inv.count("amulet_of_accuracy")
        t.check("reward.amulet_of_accuracy", (amulet_after or 0) == (amulet_before or 0) + 1,
            string.format("amulet_of_accuracy %s -> %s (want +1)", tostring(amulet_before), tostring(amulet_after)))
        local paid = {}
        local all_paid = true
        for _, colour in ipairs(colours) do
            local _, count = t.inv.count(colour)
            paid[#paid + 1] = colour .. " " .. beads_before[colour] .. " -> " .. tostring(count)
            if (count or 0) ~= beads_before[colour] - 1 then all_paid = false end
        end
        t.check("reward.beads_taken", all_paid, "one of each bead taken: " .. table.concat(paid, "; "))

        t.finish(0)
    end,
}
