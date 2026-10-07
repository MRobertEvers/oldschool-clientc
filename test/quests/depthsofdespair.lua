-- The Depths of Despair (Quest Helper TheDepthsOfDespair), every leg driven through clicks.
--
-- b70 door-rule re-drive. Setup stages prerequisites only (Client of Kourend, X Marks the Spot,
-- Agility 18+, the snake kit, the Kourend Castle Teleport out of the library); ::depthsofdespair is NOT
-- used: its debugproc (depthsofdespair.rs2:283-294) only re-writes those prerequisite vars and then
-- p_teleports from Lumbridge across the sea to Kandur's house. Instead the player walks to Veos at
-- Port Sarim and sails ("Can you take me somewhere?" -> "I'd like to travel to Port Piscarilius, please."), as
-- taleoftherighteous does.
--
-- Doors and floors on the way (reach.py / map grids, --root the b63 worktree):
--  * Kandur's house is open (reach 1776,3567 -> 1760,3540 REACH with every door shut).
--  * Arceuus Library: the east double door archeuus_door_double_right_green 1642,3805 is the only
--    way in from Hosidius (1643,3805 outside REACH from 1776,3567; 1642,3805 inside NEEDS-DOOR).
--  * The Envoy shelf is on the middle floor (dod_envoy_shelf, depthsofdespair.rs2:385; ::dodshelf
--    prints it). A shelf at x <= 1640 stands in the central ring of the middle floor, which no
--    middle-floor walk reaches from the NE stairs (rows 3815-3816 solid): the ring is entered from
--    the top floor by archeuus_stairs_upper 1638,3804,2. Route: NE stairs up (1643,3819,0), the
--    middle-floor stairs up (1644,3828,1), the top floor walked to 1638,3803,2, its stairs down.
--    Landings: areas/area_arceuus/configs/arceuus_stairs_maplinks.dbrow (open end of the bottom <->
--    open end of the top, 7 tiles on): 1643,3825,1; 1650,3828,2; 1638,3810,1 in the ring.
--  * Crabclaw Caves: entrance 1644,3449 (climb; frame 0 -> 1), crevice, stepping stones, rocks,
--    rope (same frame: named by same_level), rubble -- each by its own op. The cave mouth lands on
--    1646,9848 and the crevice on 1711,9820 (shortest-path's rows); the stones (1708 <-> 1702,9800)
--    and the rock (1689 <-> 1687,9801) cross both ways. The player climbs the rope back up and
--    leaves by the sand pile, which lands on 1643,3450 (transports.tsv:5271). The library is left by
--    a real teleport (Kourend Castle Teleport, magic_spells.dbrow [magic_spell_teleport_kourend]:
--    level 48, 1 fire + 1 water + 2 law; unlocked by Client of Kourend), then overland from the
--    castle courtyard to the cave mouth.
--  Staged Magic 48 raises the combat level; no Depths of Despair dialogue reads it
--  (grep combat_level/stat( in quest_depthsofdespair/: stat_base(agility) gates the start; the cave
--  obstacles roll stat_random(agility), depthsofdespair_locs.rs2:36/55/77, so Agility 30 is a margin).
return {
    id = "depthsofdespair",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_xmarksthespot",
        "::complete quest_clientofkourend", -- Veos still ferries once both are done (quest_clientofkourend/scripts/veos_ferry.rs2)
        "::setlevel agility 30",
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel hitpoints 60",
        "::setlevel magic 48",
        "::give lobster 10",
        "::give firerune 2",
        "::give waterrune 2",
        "::give lawrune 4",
    },
    run = function(t)
        local dod_bind = {
            varp = "varb6027_hosidiusquest",
            constants = { not_started = 0, olivia = 1, galana = 2, envoy = 3, caves = 4, navigate = 6, artur = 7, snake = 8, chest = 9, ret = 10, complete = 11 },
            display = "The Depths of Despair", points = 1,
        }
        t.quest.bind(dod_bind)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function here()
            local _, tl = t.world.tile()
            return tl
        end
        local function tile_str(tl)
            return tl and (tostring(tl.x) .. "," .. tostring(tl.z) .. "," .. tostring(tl.level)) or "nil"
        end

        -- Lumbridge -> Port Sarim on foot (REACH 255, doors shut) -> Veos sails to Piscarilius.
        t.exec("goto-veosSarim", t.player.goto_tile, 3054, 3246, 0)
        t.exec("talkToVeos", t.player.talk_to, "veos_sarim", 1)
        t.exec("talkToVeos-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToVeos-somewhere", t.chat.choose, "Can you take me somewhere?")
        t.exec("talkToVeos-where", t.chat.drain, { stop_at = "options" })
        t.exec("talkToVeos-sail", t.chat.choose, "I'd like to travel to Port Piscarilius, please.")
        t.exec("talkToVeos-done", t.chat.drain, {})
        t.ticks(4)
        do local da = here(); t.check("talkToVeos-landed", da ~= nil and da.x >= 1800 and da.x < 1850 and da.z > 3600 and da.z < 3750, "landed at the Piscarilius dock at " .. tile_str(da)) end
        -- guide talkToLordKandur: Piscarilius dock -> Kandur's (open) house, REACH 195.
        t.exec("goto-kandur", t.player.goto_tile, 1782, 3569, 0)
        t.exec("talkToKandur", t.player.talk_to, "hosidiusquest_lord", 1)
        t.exec("kandur-menu", t.chat.drain, { stop_at = "options" })
        t.exec("kandur-yes", t.chat.choose, "Yes.")
        t.exec("kandur-done", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.olivia", t.quest.expect_stage("olivia"))
        t.exec("talkToOlivia", t.player.talk_to, "hosidiusquest_chef", 1)
        t.exec("olivia-done", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.galana", t.quest.expect_stage("galana"))

        -- guide talkToGalana: overland to the library's east door, pressed, walked in.
        t.exec("goto-libraryEastDoor", t.player.goto_tile, 1643, 3805, 0)
        t.exec("enterLibrary.eastDoor", t.player.pass_door, { closed = "archeuus_door_double_right_green",
            open = "archeuus_door_double_right_green_open", at = { 1642, 3805, 0 }, near = { 1643, 3805 }, far = { 1641, 3805 } })
        t.exec("walk-galana", t.player.walk_to, 1648, 3822, 30)
        t.exec("talkToGalana", t.player.talk_to, "hosidiusquest_librarian", 1)
        t.exec("galana-done", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.envoy", t.quest.expect_stage("envoy"))
        t.exec("galana-again", t.player.talk_to, "hosidiusquest_librarian", 1)
        t.exec("galana-again-done", t.chat.drain, {})
        local ok, n = t.inv.count("hosidiusquest_book")
        t.check("galana.grants_no_book", ok == "ok" and n == 0, "book count " .. tostring(n))
        t.cheat("::dodshelf")
        t.ticks(4)
        local _, list = t.msg.last(8)
        local sx, sz, sl, sid
        for _, line in ipairs(list or {}) do
            local text = type(line) == "table" and (line.text or line[1]) or line
            local a, b, c, d = tostring(text):match("dodshelf (%d+),(%d+),(%d+) type=(%d+)")
            if a then sx, sz, sl, sid = tonumber(a), tonumber(b), tonumber(c), tonumber(d) end
        end
        t.check("shelf.found", sx ~= nil and sl == 1, "shelf " .. tostring(sx) .. "," .. tostring(sz) .. "," .. tostring(sl) .. " id " .. tostring(sid))
        local sym = "archeuus_library_bookcase_g1_end_right_door_03_red"
        t.check("shelf.sym", sid == 28006, "uid-picked shelf type " .. tostring(sid))

        -- findTheVarlamoreEnvoy: up the NE stairs to the middle floor.
        local in_ring = sx ~= nil and sx <= 1640 and sz <= 3814
        local function ne_middle(tl) return tl.level == 1 and tl.x >= 1642 and tl.x <= 1659 and tl.z >= 3817 and tl.z <= 3830 end
        t.exec("library.stairsUp", t.player.climb, { loc = "archeuus_stairs_lower_right", op = 1, op_name = "Climb",
            at = { 1643, 3819, 0 }, dest = { 1643, 3825, 1 }, slack = 1, landed_ok = ne_middle, landed_desc = "the middle floor's NE stair hall" })
        t.ticks(2)
        if in_ring then
            -- The ring holding the shelf is entered from the top floor: up the hall's stairs, across the
            -- top floor, down archeuus_stairs_upper 1638,3804,2 into the ring.
            t.exec("library.middleStairsUp", t.player.climb, { loc = "archeuus_stairs_lower", op = 1, op_name = "Climb",
                at = { 1644, 3828, 1 }, dest = { 1650, 3828, 2 }, slack = 1,
                landed_ok = function(tl) return tl.level == 2 and tl.z >= 3817 end, landed_desc = "the top floor's NE stair hall" })
            t.ticks(2)
            t.exec("walk-topFloorRingStairs", t.player.walk_route, { { 1643, 3827 }, { 1636, 3826 }, { 1633, 3821 },
                { 1634, 3814 }, { 1639, 3811 }, { 1640, 3804 }, { 1638, 3803 } })
            t.exec("library.ringStairsDown", t.player.climb, { loc = "archeuus_stairs_upper", op = 1, op_name = "Climb",
                at = { 1638, 3804, 2 }, dest = { 1638, 3810, 1 }, slack = 1,
                landed_ok = function(tl) return tl.level == 1 and tl.x >= 1626 and tl.x <= 1640 and tl.z >= 3801 and tl.z <= 3814 end,
                landed_desc = "the middle floor's central ring" })
            t.ticks(2)
            local rt = here()
            t.check("climbToMiddleFloor.level", rt ~= nil and rt.level == 1, "at " .. tile_str(rt))
        end
        if in_ring then
            t.exec("walk-shelf", t.player.walk_to, sx - 1, sz, 30)
        end
        t.exec("searchShelf", t.player.click_loc, sym, 1, { at = { sx, sz, 1 } })
        t.inv.await("hosidiusquest_book", 1, 15)
        local bok, bn = t.inv.count("hosidiusquest_book")
        t.check("findEnvoy.book", bok == "ok" and bn == 1, "book count " .. tostring(bn))
        t.exec("readEnvoy", t.player.inv_op, "hosidiusquest_book", 1)
        t.ticks(2)
        t.expect("quest.stage.caves", t.quest.expect_stage("caves"))

        -- Out of the library by a real teleport (Kourend Castle Teleport), then overland from the
        -- courtyard to the cave mouth (REACH 286, doors shut).
        t.player.teleport_cast("kourend_teleport", { 1631, 3673, 0 }, { name = "leaveLibraryKourendTeleport",
            runes = { { "firerune", 1 }, { "waterrune", 1 }, { "lawrune", 2 } }, where = "Kourend Castle courtyard" })
        t.ticks(3)

        -- enterCrabclawCaves: overland from the courtyard to the cave mouth (REACH 286), then its own op.
        t.exec("goto-caves", t.player.goto_tile, 1645, 3451, 0)
        t.exec("enterCrabclawCaves", t.player.climb, { loc = "hosidiusquest_cave_entrance", op = 1, op_name = "Enter",
            at = { 1644, 3449, 0 }, src = { 1645, 3451 }, dest = { 1646, 9848, 0 }, slack = 0 })
        t.ticks(3)
        t.expect("quest.stage.navigate", t.quest.expect_stage("navigate"))

        -- goThroughCrevice / stepOverSteppingStones / climbPastRocks / enterTunnelEntrance: each by its own op.
        -- A waypoint chain through the upper cave (reach.py 1646,9848 -> 1711,9824 REACH 89; hops <= 8).
        t.exec("walk-crevice", t.player.walk_route, { { 1654, 9845 }, { 1662, 9844 }, { 1670, 9844 }, { 1678, 9843 },
            { 1685, 9835 }, { 1693, 9834 }, { 1701, 9834 }, { 1709, 9834 }, { 1711, 9826 }, { 1711, 9824 } })
        do
            local ct = here()
            t.check("walk-crevice.arrived", ct ~= nil and ct.x == 1711 and ct.z == 9824, "at " .. tile_str(ct))
        end
        t.exec("goThroughCrevice", t.player.cross_trap, { loc = "hosidiusquest_crackin", op_name = "Enter",
            at = { 1711, 9823, 0 }, src = { 1711, 9824 }, dest = { 1711, 9820 } })
        t.ticks(2)
        t.exec("walk-stones", t.player.walk_route, { { 1717, 9818 }, { 1717, 9810 }, { 1713, 9806 }, { 1708, 9803 }, { 1708, 9800 } })
        local VITALS = { eat = "lobster", below = 30 }
        t.exec("stepOverSteppingStones", t.player.cross_trap, { loc = "hosidiusquest_stone", op_name = "Cross",
            at = { 1706, 9800, 0 }, src = { 1708, 9800 }, dest = { 1702, 9800 }, attempts = 8, vitals = VITALS })
        t.exec("walk-rocks", t.player.walk_to, 1689, 9801, 30)
        t.exec("climbPastRocks", t.player.cross_trap, { loc = "hosidiusquest_rock", op_name = "Climb",
            at = { 1688, 9801, 0 }, src = { 1689, 9801 }, dest = { 1687, 9801 }, attempts = 8, vitals = VITALS })
        t.exec("walk-ropeTop", t.player.walk_to, 1673, 9800, 20)
        t.exec("enterTunnelEntrance", t.player.climb, { loc = "hosidiusquest_rope_top", op = 1, op_name = "Climb-down",
            at = { 1671, 9799, 0 }, dest = { 1678, 9747, 0 }, slack = 1,
            same_level = "depthsofdespair_locs.rs2 [oploc1,hosidiusquest_rope_top] p_teleport(^dod_downstairs)" })
        t.ticks(4)
        t.expect("quest.stage.artur", t.quest.expect_stage("artur"))
        t.exec("talkToArturHosidius", t.player.talk_to, "hosidiusquest_son_caves", 1)
        t.exec("artur-done", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.snake", t.quest.expect_stage("snake"))

        -- killSandSnake: over the rubble east (its own op), then the fight.
        t.exec("walk-rubble", t.player.walk_to, 1685, 9755, 20)
        t.exec("climbRubble", t.player.cross_trap, { loc = "hosidiusquest_rock_snake", op_name = "Climb",
            at = { 1687, 9755, 0 }, src = { 1685, 9755 }, dest = { 1689, 9755 }, attempts = 8, vitals = VITALS })
        local _, food0 = t.inv.count("lobster")
        local ra, da = t.player.attack("hosidiusquest_snake", 2, 10)
        t.check("snake-attack", ra == "ok" or ra == "timeout", tostring(ra) .. " " .. tostring(da))
        local kr, kd = t.npc.await_dead_engaged(300, 12, { eat = { item = "lobster", below = 30 } })
        t.step("killSandSnake", kr == "ok" and "PASS" or "FAIL", tostring(kr) .. " " .. tostring(kd))
        do
            local lowest = tonumber(tostring(kd):match("lowest hp (%d+)/"))
            local fr, food = t.inv.count("lobster")
            t.check("killSandSnake.margin", lowest ~= nil and lowest * 4 >= 60 and fr == "ok" and food >= 1,
                "lowest hp " .. tostring(lowest) .. "/60, lobsters " .. tostring(food0) .. " -> " .. tostring(food)
                .. " (margin: lowest hp >= a quarter of 60 AND food left)")
        end
        t.ticks(2)
        t.expect("quest.stage.chest", t.quest.expect_stage("chest"))
        t.exec("searchChest", t.player.click_loc, "hosidiusquest_chest", 1)
        t.inv.await("hosidiusquest_accord", 1, 15)
        t.expect("quest.stage.ret", t.quest.expect_stage("ret"))

        -- talkToLordKandurAgain: back the way it came, every obstacle by its own op: rubble west,
        -- the rope up, rocks and stones east, the crevice out, the sand pile up, then overland.
        t.exec("walk-rubbleBack", t.player.walk_to, 1689, 9755, 20)
        t.exec("climbRubbleBack", t.player.cross_trap, { loc = "hosidiusquest_rock_snake", op_name = "Climb",
            at = { 1687, 9755, 0 }, src = { 1689, 9755 }, dest = { 1685, 9755 }, attempts = 8, vitals = VITALS })
        t.exec("walk-ropeBottom", t.player.walk_to, 1678, 9748, 20)
        t.exec("climbRopeUp", t.player.climb, { loc = "hosidiusquest_rope_bottom", op = 1, op_name = "Climb",
            at = { 1677, 9746, 0 }, dest = { 1672, 9800, 0 }, slack = 1,
            same_level = "depthsofdespair_locs.rs2 [oploc1,hosidiusquest_rope_bottom] p_teleport(^dod_rope_top)" })
        t.ticks(3)
        t.exec("walk-rocksBack", t.player.walk_to, 1686, 9801, 20)
        t.exec("climbRocksBack", t.player.cross_trap, { loc = "hosidiusquest_rock", op_name = "Climb",
            at = { 1688, 9801, 0 }, src = { 1687, 9801 }, dest = { 1689, 9801 }, attempts = 8, vitals = VITALS })
        t.exec("walk-stonesBack", t.player.walk_to, 1702, 9800, 30)
        t.exec("stepOverSteppingStonesBack", t.player.cross_trap, { loc = "hosidiusquest_stone", op_name = "Cross",
            at = { 1704, 9800, 0 }, src = { 1702, 9800 }, dest = { 1708, 9800 }, attempts = 8, vitals = VITALS })
        t.exec("walk-creviceBack", t.player.walk_route, { { 1708, 9803 }, { 1713, 9806 }, { 1717, 9810 }, { 1717, 9814 }, { 1711, 9818 }, { 1711, 9820 } })
        t.exec("goThroughCreviceBack", t.player.cross_trap, { loc = "hosidiusquest_crackout", op_name = "Enter",
            at = { 1711, 9821, 0 }, src = { 1711, 9820 }, dest = { 1711, 9824 } })
        t.ticks(2)
        t.exec("walk-caveExit", t.player.walk_route, { { 1711, 9826 }, { 1709, 9834 }, { 1701, 9834 }, { 1693, 9834 }, { 1685, 9835 },
            { 1678, 9843 }, { 1670, 9844 }, { 1662, 9844 }, { 1654, 9845 }, { 1647, 9848 } })
        t.exec("leaveCrabclawCaves", t.player.climb, { loc = "hosidiusquest_cave_exit", op = 1, op_name = "Climb",
            at = { 1646, 9849, 0 }, dest = { 1643, 3450, 0 }, slack = 0 })
        t.ticks(3)
        t.exec("goto-kandurReturn", t.player.goto_tile, 1782, 3569, 0)
        local _, snap = t.skill.snapshot()
        local _, coins0 = t.inv.count("coins")
        t.exec("talkToLordKandurAgain", t.player.talk_to, "hosidiusquest_lord_vis", 1)
        t.exec("kandur-return-done", t.chat.drain, {})
        t.ticks(3)
        local gr, gd = t.skill.expect_gain("agility", 1500, snap)
        t.check("agility.gain", gr == "ok", tostring(gr) .. " " .. tostring(gd))
        local _, coins1 = t.inv.count("coins")
        t.check("reward.coins", (coins1 or 0) - (coins0 or 0) == 4000, "coins " .. tostring(coins0) .. " -> " .. tostring(coins1))
        local pr, pn = t.inv.count("veos_memoirs_hos_page")
        t.check("reward.page", pr == "ok" and pn == 1, "memoir page count " .. tostring(pn))
        t.quest.expect_complete()
        t.finish(0)
    end,
}
