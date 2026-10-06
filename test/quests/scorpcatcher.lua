-- Scorpion Catcher. quest_scorpcatcher's own content
-- (OSRS-Content/osrs239-content/server/scripts/quests/quest_scorpcatcher/,
-- areas/area_seers/scripts/{thormac,seer}.rs2): Thormac at the top of the
-- Sorcerer's Tower, two Seers' visits, and three Kharid scorpions caught with
-- a use of the cage (opnpcu) -- Taverley Dungeon's secret room, the Edgeville
-- Monastery's upper floor and the Barbarian Outpost's prison room.
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md standing rules, owner 2026-10-03 and
-- 2026-10-05): every goto departs from and lands on an open outside tile.
-- Every door, ladder, gate and shortcut between is clicked on every visit,
-- in and out:
--   * the Taverley members' wall (membergater 2935,3450, gates.rs2
--     [label,member_fencegate_try], a walk-through) is the only way on foot
--     between Lumbridge/Falador/Edgeville and the Seers'/Taverley side
--     (reach.py: NEEDS-DOOR via membergater at margin 250 only) -- crossed by
--     cross_gate on all three crossings (in at the start, out to the
--     monastery, back in for the outpost);
--   * the Sorcerer's Tower: poordoor 2702,3401,0 and three ladders, each by
--     its maplink row (ladders_stairs/configs/maplink.dbrow 0_42_53_*);
--   * Taverley Dungeon: the ladder at 2884,3397, then the strange floor
--     (taverly_dungeon_floor_spikes_sc 2879,9813, maplink_agility.dbrow
--     level 80) -- the ladder's 205-tile component never reaches the
--     scorpion's side otherwise (comp.py; the pipe at 70 Agility runs past
--     the blue dragons and black demons, the dusty-key door needs the
--     Jailer). Quest Helper picks the strange floor at 80 Agility
--     (ScorpionCatcher.java:256), so setup stages Agility 80. Then the Old
--     wall (scorpionwall 2875,9799) is searched and the room walked into;
--   * the Barbarian Outpost: barbariangatel 2545,3570 (doors_selfstage) and
--     the prison room's prisondoor_r 2551,3570 (doubledoors);
--   * the monastery: only members of the order may climb its ladder
--     (prayer_guild.rs2 [oploc1,monasteryladder]); Abbot Langley takes the
--     player in (abbot_langley.rs2, Prayer 31), then the ladder and the
--     dormitory's poordoor 3058,3485,1.
--
-- Staged levels: Prayer 31 (thormac.rs2's start requirement) and Agility 80
-- (the strange floor). Neither Thormac, the seer, Abbot Langley nor the
-- monastery ladder branches on the combat level (no combat_level read in
-- thormac.rs2, seer.rs2, abbot_langley.rs2, prayer_guild.rs2), so the staged
-- player sees the same dialogue as any other.
--
-- Reward: 1 quest point and 6,625 Strength XP (wiki; Quest Helper
-- ScorpionCatcher.java:327; LostCity quest_scorpcatcher.rs2:60
-- `stat_advance(strength, 66250)`), paid by this pack's
-- [queue,scorpcatcher_quest_complete] since seam matthew-mbp-m4-b66-seam1.
--
-- Catch order is the guide's: Taverley (empty -> scorpioncagea), the
-- monastery (a -> scorpioncageac), the outpost (ac -> scorpioncagefull);
-- each catch is a use_on of the held cage on that scorpion, graded on the
-- cage swap (scorpcatcher_scorpions.rs2 ~scorpcatcher_next_cage).

local VITALS = { eat = "lobster", below = 25, antipoison = true }

return {
    id = "scorpcatcher",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- fourteen tutorial slots, so the scorpion cage fits
        "::setlevel prayer 31", -- thormac.rs2's only real start requirement
        "::setlevel agility 80", -- Taverley Dungeon's strange floor (maplink_agility.dbrow level 80)
        -- The guide's combat requirement is "the ability to run past ... level 64 poison spiders"
        -- (ScorpionCatcher.java getCombatRequirements). Run 1 with the fixture's 10 Hitpoints: a
        -- poison spider (huntmode=aggressive, combat_stats.generated.npc) followed the player
        -- through the opened Old wall and killed it at 2879,9798 during the catch. 40 Hitpoints
        -- is a player who can run past them; no dialogue this run reaches reads the combat level.
        "::setlevel hitpoints 40",
        "::give lobster 6", -- the guide's recommended food: aggressive poison spiders by the secret room
        "::give 4doseantipoison 1", -- the guide's recommended antipoison (poison spiders)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp76_scorpcatcher",
            constants = {
                not_started = 0,
                started = 1,
                first_hint = 2,
                second_hint = 3,
                complete = 6,
            },
            row = "quest_scorpioncatcher",
            display = "Scorpion Catcher",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- ---------------------------------------------------------------
        -- Crossings, each one row graded on the tiles (verbs-pointer.md).
        -- ---------------------------------------------------------------
        -- Taverley's east members' gate: membergater 2935,3450 (rot 2, the
        -- east wall of its tile): Taverley is x <= 2935, outside x >= 2936.
        local function taverley_gate_in(name)
            t.exec(name, t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
                near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
                far_desc = "inside Taverley, x <= 2935" })
        end
        local function taverley_gate_out(name)
            t.exec(name, t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
                near = { 2934, 3450 }, far_ok = function(tile) return tile.x >= 2936 end,
                far_desc = "outside Taverley, x >= 2936" })
        end

        -- The Sorcerer's Tower, door to top floor: poordoor 2702,3401,0 is
        -- the south wall of its tile (outside 2702,3400). Each ladder lands
        -- on its maplink row's dest (maplink.dbrow 0_42_53_*).
        local function tower_up(prefix)
            t.exec(prefix .. ".towerDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 2702, 3401, 0 }, near = { 2702, 3400 }, far = { 2702, 3402 } })
            t.exec(prefix .. ".ladder0Up", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
                at = { 2701, 3408, 0 }, dest = { 2701, 3407, 1 }, slack = 1 })
            t.exec(prefix .. ".ladder1Up", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
                at = { 2704, 3403, 1 }, dest = { 2704, 3404, 2 }, slack = 1 })
            t.exec(prefix .. ".ladder2Up", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
                at = { 2699, 3405, 2 }, dest = { 2700, 3405, 3 }, slack = 1 })
        end
        local function tower_down(prefix)
            t.exec(prefix .. ".ladder3Down", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
                at = { 2699, 3405, 3 }, dest = { 2700, 3405, 2 }, slack = 1 })
            t.exec(prefix .. ".ladder2Down", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
                at = { 2704, 3403, 2 }, dest = { 2704, 3404, 1 }, slack = 1 })
            t.exec(prefix .. ".ladder1Down", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
                at = { 2701, 3408, 1 }, dest = { 2701, 3407, 0 }, slack = 1 })
            t.exec(prefix .. ".towerDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 2702, 3401, 0 }, near = { 2702, 3401 }, far = { 2702, 3400 } })
        end

        -- ---------------------------------------------------------------
        -- Thormac: from Lumbridge over the members' wall to the tower.
        -- ---------------------------------------------------------------
        t.exec("goto-thormac.memberGate", t.player.goto_tile, 2938, 3450, 0)
        taverley_gate_in("goToTopOfTower.memberGateIn")
        t.exec("goto-thormac", t.player.goto_tile, 2702, 3400, 0)
        tower_up("sorcerersTowerLadder0")
        t.exec("talk.thormac1", t.player.talk_to, "thormac", 1)
        -- [opnpc1,thormac]: not_started & prayer>=31 -> @thormac_assistance.
        -- 2026-09-23 parity2 (area_seers/scripts/thormac.rs2) restored the
        -- p_choice3 offer ("how to catch" / "what's in it for me" / "not
        -- interested") -- choose "So how would I go about catching them
        -- then?" -> @thormac_how_catch. Its cage-empty branch (setup's
        -- ::clearinv leaves no cage) if_close's the page, `mes("Thormac gives
        -- you a cage.")`, then mounts a fresh npc page after p_delay(4): one
        -- chat.play list dies on that reopened page (docs/QUEST_AUTHORING.md
        -- section 8), so split it in two and await the remount.
        t.exec("talk.thormac1-dialog", t.chat.play, {
            "npc:Hello I am Thormac the sorcere",
            "choose:What do you need assistance with?",
            "player:What do you need assistance wi",
            "npc:I've lost my pet scorpions. Th",
            "npc:I left their cage door open, n",
            "npc:There's three of them, and the",
            "choose:So how would I go about catching them then?",
            "player:So how would I go about catchi",
            "npc:Well I have a scorpion cage he",
            "end",
        })
        t.expect("thormac.cage_given_msg", t.msg.expect("Thormac gives you a cage."))

        local reopen1_result, reopen1_detail = t.await({
            level = function()
                return t.chat.kind() == "npc"
            end,
            note = "thormac.howcatch_reopen",
        }, 15)
        t.step("thormac.howcatch_reopen", reopen1_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == npc) after if_close+mes+p_delay(4) -> "
                .. tostring(reopen1_result) .. " " .. tostring(reopen1_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))

        -- thormac_how_catch's reopened page + its p_choice2 -> "Ok, I will do
        -- it then" -> @thormac_startquest (cage already held; varp write).
        t.exec("talk.thormac1-dialog2", t.chat.play, {
            "npc:If you go up to the village of",
            "choose:Ok, I will do it then",
            "player:Ok, I will do it then.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        local cage_count = select(2, t.inv.count("scorpioncageempty"))
        t.check("thormac.cage_in_pack", cage_count == 1, "scorpioncageempty in the pack: " .. tostring(cage_count))

        -- ---------------------------------------------------------------
        -- Seers' Village, first visit (seer.rs2's `started` branch). The seer
        -- at 2716,3485 stands in the open street (comp.py: a 2,612-tile
        -- component, no door between).
        -- ---------------------------------------------------------------
        tower_down("speakToSeer1")
        t.exec("goto-seer1", t.player.goto_tile, 2716, 3484, 0)
        t.exec("talk.seer1", t.player.talk_to, "seer", 1)
        t.exec("talk.seer1-dialog", t.chat.play, {
            "npc:Many greetings.",
            "choose:I need to locate some scorpions.",
            "player:I need to locate some scorpion",
            "npc:Well you have come to the righ",
            "npc:Do you need to locate any part",
            "player:I'm looking for some lesser Kh",
            "npc:Let me look into my looking gl",
            "npc:I can see a scorpion that you ",
            "npc:The scorpion seems to be going",
            "npc:Well see if you can find that ",
        })
        t.expect("quest.stage.first_hint", t.quest.expect_stage("first_hint"))

        -- ---------------------------------------------------------------
        -- Taverley Dungeon. Seers' -> the dungeon ladder is open ground
        -- (reach.py closed-doors len 455: through Burthorpe, no gate).
        -- ---------------------------------------------------------------
        t.exec("goto-enterTaverleyDungeon", t.player.goto_tile, 2884, 3399, 0)
        t.exec("enterTaverleyDungeon", t.player.climb, { loc = "ladder_outside_to_underground", op = 1,
            op_name = "Climb-down", at = { 2884, 3397, 0 }, src = { 2884, 3398 }, dest = { 2884, 9798, 0 } })
        t.exec("walk-goOverStrangeFloor", t.player.walk_route,
            { { 2884, 9798 }, { 2883, 9802 }, { 2883, 9807 }, { 2881, 9810 }, { 2880, 9813 } },
            { vitals = VITALS })
        -- maplink_agility.dbrow [maplink_agility_0_45_153_0_21]: from
        -- 2880,9813 the strange floor puts the player on 2877,9813.
        t.exec("goOverStrangeFloor", t.player.cross_trap, { loc = "taverly_dungeon_floor_spikes_sc",
            op_name = "Jump-over", at = { 2879, 9813, 0 }, src = { 2880, 9813 }, dest = { 2877, 9813 },
            attempts = 1, vitals = VITALS })
        t.exec("walk-searchOldWall", t.player.walk_route,
            { { 2877, 9813 }, { 2875, 9810 }, { 2875, 9805 }, { 2875, 9800 }, { 2875, 9799 } },
            { vitals = VITALS })

        -- The Old wall (scorpcatcher_scorpions.rs2 [oploc1,scorpionwall]):
        -- first_hint or later -> mes, a mesbox, then loc_del(500). The
        -- scorpion's room (x 2874-2879, z 9793-9798) has no other way in.
        t.exec("searchOldWall", t.player.click_loc, "scorpionwall", 1, { at = { 2875, 9799, 0 } })
        t.exec("searchOldWall-dialog", t.chat.play, { "mesbox:You find a hidden catch" })
        local wall_result = t.world.loc_near("scorpionwall", 3, { at = { 2875, 9799, 0 } })
        t.check("searchOldWall.opened", wall_result == "not_found",
            "scorpionwall at 2875,9799,0 after the search: " .. tostring(wall_result) .. " (want not_found: loc_del(500))")
        t.exec("walk-catchTaverleyScorpion", t.player.walk_to, 2875, 9798)
        local in_room_result, in_room = t.world.tile()
        t.check("catchTaverleyScorpion.inRoom", in_room_result == "ok" and in_room.z <= 9798 and in_room.level == 0,
            "inside the secret room (z <= 9798): " .. (in_room_result == "ok"
                and (in_room.x .. "," .. in_room.z .. "," .. in_room.level) or tostring(in_room_result)))

        local scorpion_a, scorpion_a_result = t.player.by_symbol("npc", "questscorpiona")
        t.step("lookup.scorpiona", scorpion_a_result == "ok" and "PASS" or "FAIL",
            "by_symbol npc questscorpiona -> " .. tostring(scorpion_a_result))
        local cage_before_a = select(2, t.inv.count("scorpioncageempty"))
        t.exec("catch.scorpiona", t.player.use_on, "scorpioncageempty", scorpion_a)
        -- [opnpcu,questscorpiona] -> ~scorpcatcher_catch_scorpion: swaps the
        -- held cage for scorpioncagea and deletes the npc.
        local cage_after_a = select(2, t.inv.count("scorpioncageempty"))
        local cage_a_count = select(2, t.inv.count("scorpioncagea"))
        t.check("catch.scorpiona.inv", cage_before_a == 1 and cage_after_a == 0 and cage_a_count == 1,
            "scorpioncageempty " .. tostring(cage_before_a) .. " -> " .. tostring(cage_after_a)
                .. ", scorpioncagea " .. tostring(cage_a_count))

        -- Out the way it came: the wall is still gone (500 ticks), the floor
        -- back east (src 2878,9813 -> 2881,9813), the ladder up.
        t.exec("walk-leaveScorpionRoom", t.player.walk_route,
            { { 2875, 9798 }, { 2875, 9799 }, { 2875, 9804 }, { 2875, 9809 }, { 2876, 9813 }, { 2878, 9813 } },
            { vitals = VITALS })
        t.exec("goOverStrangeFloorBack", t.player.cross_trap, { loc = "taverly_dungeon_floor_spikes_sc",
            op_name = "Jump-over", at = { 2879, 9813, 0 }, src = { 2878, 9813 }, dest = { 2881, 9813 },
            attempts = 1, vitals = VITALS })
        t.exec("walk-leaveTaverleyDungeon", t.player.walk_route,
            { { 2881, 9813 }, { 2884, 9811 }, { 2884, 9806 }, { 2884, 9801 }, { 2884, 9798 } },
            { vitals = VITALS })
        t.exec("leaveTaverleyDungeon", t.player.climb, { loc = "ladder_from_cellar", op = 1, op_name = "Climb-up",
            at = { 2884, 9797, 0 }, src = { 2884, 9798 }, dest = { 2884, 3398, 0 } })
        -- Margin past the aggressive spiders: a quarter of the 40 staged
        -- Hitpoints AND food left (no fight, but the spiders hit).
        local hp_result, hp_reading = t.skill.read("hitpoints")
        local lobsters_left = select(2, t.inv.count("lobster"))
        t.check("leaveTaverleyDungeon.margin", hp_result == "ok" and type(hp_reading) == "table"
                and hp_reading.level >= 10 and type(lobsters_left) == "number" and lobsters_left >= 1,
            "out of Taverley Dungeon: hitpoints " .. (type(hp_reading) == "table"
                and (tostring(hp_reading.level) .. "/" .. tostring(hp_reading.base_level)) or tostring(hp_result))
                .. ", lobsters left " .. tostring(lobsters_left) .. " (want >= 10 hp AND >= 1 lobster)")

        -- ---------------------------------------------------------------
        -- Seers' Village, second visit: seer.rs2's has_a branch (second_hint).
        -- ---------------------------------------------------------------
        t.exec("goto-seer2", t.player.goto_tile, 2716, 3484, 0)
        t.exec("talk.seer2", t.player.talk_to, "seer", 1)
        t.exec("talk.seer2-dialog", t.chat.play, {
            "npc:Many greetings.",
            "choose:I've retrieved the scorpion from near the spiders.",
            "player:I've retrieved the scorpion fr",
            "npc:Well, I've checked my looking g",
            "npc:That's all I can tell you about",
            "player:Any more scorpions?",
            "npc:It's good that you should ask.",
            "npc:It seems to be in some sort of ",
        })
        t.expect("quest.stage.second_hint", t.quest.expect_stage("second_hint"))

        -- ---------------------------------------------------------------
        -- Edgeville Monastery, the guide's second catch (ScorpionCatcher.java
        -- step order: Taverley, monastery, outpost): out over the members'
        -- wall (Seers' -> 2934,3450 is open ground), then the monastery from
        -- its south approach (3051,3471, closed-doors from the gate). Abbot
        -- Langley takes the player into the order: the ladder answers a
        -- non-member with his "Only members..." page.
        -- ---------------------------------------------------------------
        t.exec("goto-enterMonastery.memberGate", t.player.goto_tile, 2932, 3450, 0)
        taverley_gate_out("enterMonastery.memberGateOut")
        t.exec("goto-enterMonastery", t.player.goto_tile, 3051, 3471, 0)
        t.exec("walk-enterMonastery", t.player.walk_to, 3057, 3482)
        t.exec("talk.abbot", t.player.talk_to, "abbot_langley", 1)
        t.exec("talk.abbot-dialog", t.chat.play, {
            "npc:Greetings traveller.",
            "choose:How do I get further into the monastery?",
            "player:How do I get further into the",
            "npc:I'm sorry but only members of",
            "choose:Well can I join your order?",
            "player:Well can I join your order?",
            "npc:Ok, I see you are someone suit",
        })
        -- varp5751_prayer_guild is server-authored (configs/prayer_guild.varp): the client has no
        -- copy (run 2: t.var.varp -> not_found), so read the server's.
        local guild_result, guild_value, guild_source = t.var.server("varp5751_prayer_guild")
        t.check("enterMonastery.joinedOrder", guild_result == "ok" and guild_value == 1,
            "varp5751_prayer_guild after Abbot Langley: " .. tostring(guild_result) .. " " .. tostring(guild_value)
                .. " (" .. tostring(guild_source) .. "; want 1, abbot_langley.rs2 @ask_to_join_abbot_langley2)")
        -- No maplink row: [proc,climb] moves the player one plane on the tile
        -- it stands on.
        t.exec("enterMonastery", t.player.climb, { loc = "monasteryladder", op = 1, op_name = "Climb-up",
            at = { 3057, 3483, 0 }, src = { 3057, 3484 }, dest = { 3057, 3484, 1 } })
        -- The dormitory: poordoor 3058,3485,1 is the north wall of its tile.
        t.exec("dormitoryDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3058, 3485, 1 }, near = { 3058, 3485 }, far = { 3058, 3486 } })
        local scorpion_c, scorpion_c_result = t.player.by_symbol("npc", "questscorpionc")
        t.step("lookup.scorpionc", scorpion_c_result == "ok" and "PASS" or "FAIL",
            "by_symbol npc questscorpionc -> " .. tostring(scorpion_c_result))
        local cage_a_before_c = select(2, t.inv.count("scorpioncagea"))
        t.exec("catch.scorpionc", t.player.use_on, "scorpioncagea", scorpion_c)
        -- ~scorpcatcher_next_cage(a, catch_c) -> scorpioncageac.
        local cage_a_after_c = select(2, t.inv.count("scorpioncagea"))
        local cage_ac_count = select(2, t.inv.count("scorpioncageac"))
        t.check("catch.scorpionc.inv", cage_a_before_c == 1 and cage_a_after_c == 0 and cage_ac_count == 1,
            "scorpioncagea " .. tostring(cage_a_before_c) .. " -> " .. tostring(cage_a_after_c)
                .. ", scorpioncageac " .. tostring(cage_ac_count))
        t.exec("dormitoryDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3058, 3485, 1 }, near = { 3058, 3486 }, far = { 3058, 3485 } })
        t.exec("leaveMonastery", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
            at = { 3057, 3483, 1 }, src = { 3057, 3484 }, dest = { 3057, 3484, 0 } })
        t.exec("walk-leaveMonastery", t.player.walk_to, 3051, 3471)
        -- Back in over the members' wall: the Barbarian Outpost is on the
        -- Taverley side (2934,3450 -> 2543,3570 is open ground).
        t.exec("goto-enterOutpost.memberGate", t.player.goto_tile, 2938, 3450, 0)
        taverley_gate_in("enterOutpost.memberGateIn")

        -- ---------------------------------------------------------------
        -- Barbarian Outpost, the guide's third catch (open ground from the
        -- members' wall).
        -- barbariangatel 2545,3570 is the east wall of its tile
        -- (maps/m39_55.jl2:1051), a doors_selfstage gate: the opened leaf is
        -- the same symbol one tile over. The yard inside (98 tiles) holds
        -- the prison room behind prisondoor_l/r 2551,3569-3570 (west wall).
        -- ---------------------------------------------------------------
        t.exec("goto-enterOutpost", t.player.goto_tile, 2543, 3570, 0)
        t.exec("enterOutpost", t.player.pass_door, { closed = "barbariangatel", open = "barbariangatel",
            at = { 2545, 3570, 0 }, near = { 2545, 3570 }, far = { 2546, 3570 } })
        t.exec("prisonDoorIn", t.player.pass_door, { closed = "prisondoor_r", open = "prisondooropen_r",
            at = { 2551, 3570, 0 }, near = { 2550, 3570 }, far = { 2551, 3570 } })
        local scorpion_b, scorpion_b_result = t.player.by_symbol("npc", "questscorpionb")
        t.step("lookup.scorpionb", scorpion_b_result == "ok" and "PASS" or "FAIL",
            "by_symbol npc questscorpionb -> " .. tostring(scorpion_b_result))
        local cage_ac_before_b = select(2, t.inv.count("scorpioncageac"))
        t.exec("catch.scorpionb", t.player.use_on, "scorpioncageac", scorpion_b)
        -- ~scorpcatcher_next_cage(ac, catch_b) -> scorpioncagefull.
        local cage_ac_after_b = select(2, t.inv.count("scorpioncageac"))
        local cage_full_count = select(2, t.inv.count("scorpioncagefull"))
        t.check("catch.scorpionb.inv", cage_ac_before_b == 1 and cage_ac_after_b == 0 and cage_full_count == 1,
            "scorpioncageac " .. tostring(cage_ac_before_b) .. " -> " .. tostring(cage_ac_after_b)
                .. ", scorpioncagefull " .. tostring(cage_full_count))
        t.exec("prisonDoorOut", t.player.pass_door, { closed = "prisondoor_r", open = "prisondooropen_r",
            at = { 2551, 3570, 0 }, near = { 2551, 3570 }, far = { 2550, 3570 } })
        t.exec("leaveOutpost", t.player.pass_door, { closed = "barbariangatel", open = "barbariangatel",
            at = { 2545, 3570, 0 }, near = { 2546, 3570 }, far = { 2545, 3570 } })

        -- ---------------------------------------------------------------
        -- Back to Thormac: the outpost -> the tower door is open ground on
        -- the Taverley side of the members' wall; up the tower.
        -- ---------------------------------------------------------------
        t.exec("goto-thormac2", t.player.goto_tile, 2702, 3400, 0)
        tower_up("returnToThormac")

        -- Reward snapshot before the hand-in.
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        local qp_result, qp_before = t.var.varp("varp101_qp")

        -- [opnpc1,thormac]'s cagefull & stage != complete branch takes the
        -- cage and queues scorpcatcher_quest_complete.
        t.exec("talk.thormac2", t.player.talk_to, "thormac", 1)
        t.exec("talk.thormac2-dialog", t.chat.play, {
            "npc:How goes your quest?",
            "player:I have retrieved all your scorpions.",
            "npc:Aha, my little scorpions home at",
        })
        t.ticks(3) -- the queued scorpcatcher_quest_complete is not client-side yet
        local full_after = select(2, t.inv.count("scorpioncagefull"))
        t.check("returnToThormac.cageHandedIn", full_after == 0,
            "scorpioncagefull after the hand-in: " .. tostring(full_after) .. " (want 0, thormac.rs2 inv_del)")

        t.quest.expect_complete()

        -- Literal rewards: 1 quest point (a before/after delta), 6,625 Strength XP.
        local qp_after_result, qp_after = t.var.varp("varp101_qp")
        t.check("reward.quest_points_1", qp_result == "ok" and qp_after_result == "ok"
                and type(qp_before) == "number" and type(qp_after) == "number" and qp_after - qp_before == 1,
            "varp101_qp " .. tostring(qp_before) .. " -> " .. tostring(qp_after) .. " (want +1)")
        if reward_snapshot_result ~= "ok" then
            t.step("reward.strength_xp_6625", "FAIL", "no skill snapshot before the hand-in: " .. tostring(reward_snapshot_result))
            t.finish(0)
            return
        end
        t.expect("reward.strength_xp_6625", t.skill.expect_gain("strength", 6625, reward_before))

        t.finish(0)
    end,
}
