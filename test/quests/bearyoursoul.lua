-- Bear Your Soul (miniquest, 0 quest points): shelf book, Aretha, dig, Key Master.
return {
    id = "bearyoursoul",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000, -- up to 48 shelf searches on foot, then Kourend, Taverley and the dungeon
    setup = {
        "::clearinv",
        "::complete quest_xmarksthespot", -- Veos sails to Great Kourend only once X Marks the Spot is done (xmarksthespot.rs2:27)
        "::give spade 1",
        -- Quest Helper BearYourSoul.java:80-82,126-130: "Dusty key, or another way to get into
        -- the deep Taverley Dungeon" is an item requirement (brought along).
        "::give dusty_key 1",
        -- survival only: the dungeon's aggressive npcs kill a 10-hitpoint account on the walk
        "::setlevel hitpoints 80",
        "::setlevel defence 75",
        -- the way to Taverley (bearyoursoul.remainder_proven.lua): Falador Teleport, Magic 37,
        -- 1 law + 3 air + 1 water; no Bear Your Soul dialogue reads Magic or combat level.
        "::give lawrune 1",
        "::give airrune 3",
        "::give waterrune 1",
        "::setlevel magic 37",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb5078_arceuus_soulbearer_story",
            constants = { not_started = 0, aretha = 1, repair = 2, complete = 3 },
            row = "miniquest_bearyoursoul",
            display = "Bear Your Soul",
            points = 0,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Travel to Great Kourend by ship and on foot: Port Sarim -> Veos "Can you take me to Great Kourend?"
        -- (clientofkourend.rs2:30) -> Port Piscarilius dock, then overland (reach.py 1824,3690 -> 1632,3818
        -- REACH closed-doors, 380 tiles) to the Arceuus Library's north door (turquoise double door 1632,3817).
        t.exec("goto-veosSarim", t.player.goto_tile, 3054, 3246, 0)
        t.exec("talkToVeos", t.player.talk_to, "veos_sarim", 1)
        t.exec("talkToVeos-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToVeos-sail", t.chat.choose, "Can you take me to Great Kourend?")
        t.exec("talkToVeos-done", t.chat.drain, {})
        t.ticks(4)
        local _, dock_arrival = t.world.tile()
        t.check("talkToVeos-landed", dock_arrival ~= nil and dock_arrival.x >= 1800 and dock_arrival.x < 1850, "landed at the Piscarilius dock at " .. tostring(dock_arrival and (dock_arrival.x .. "," .. dock_arrival.z)))
        t.exec("walk-library", t.player.walk_route, {
            { 1815, 3690 }, { 1806, 3690 }, { 1806, 3699 }, { 1806, 3708 }, { 1804, 3715 }, { 1799, 3719 }, { 1799, 3728 },
            { 1791, 3729 }, { 1782, 3729 }, { 1778, 3734 }, { 1773, 3738 }, { 1772, 3746 }, { 1764, 3747 }, { 1762, 3754 },
            { 1757, 3758 }, { 1756, 3766 }, { 1747, 3766 }, { 1745, 3773 }, { 1743, 3780 }, { 1736, 3782 }, { 1727, 3782 },
            { 1719, 3781 }, { 1711, 3780 }, { 1702, 3780 }, { 1694, 3779 }, { 1685, 3779 }, { 1676, 3779 }, { 1667, 3779 },
            { 1658, 3779 }, { 1649, 3779 }, { 1648, 3787 }, { 1648, 3796 }, { 1648, 3805 }, { 1650, 3812 }, { 1659, 3812 },
            { 1660, 3820 }, { 1660, 3829 }, { 1655, 3833 }, { 1646, 3833 }, { 1637, 3833 }, { 1632, 3829 }, { 1632, 3820 },
            { 1632, 3818 }
        }, { ticks = 40 })
        t.exec("libraryDoorIn", t.player.pass_door, { closed = "archeuus_door_double_left_turquoise", open = "archeuus_door_double_left_turquoise_open",
            at = { 1632, 3817, 0 }, near = { 1632, 3818 }, far = { 1632, 3816 } })

        -- findSoulJourneyAndRead: the Soul journey sits on ONE rotating shelf in 24 (bearyoursoul.rs2
        -- bys_shelf_holds_book: (x*31 + z*17 + level*5 + map_clock/8000) % 24 = 0; nothing a player can read), so
        -- search the way a player does: shelves in the hall, one per residue class (31x+17z) % 24, walking
        -- from one to the next on foot, until "You find a book titled 'The Soul journey'". Whichever shelf holds
        -- it now is found within 24 searches; nothing here depends on the rotation.
        local shelves = {
            { "archeuus_library_bookcase_g2_end_left_01_door_10_turquoise", 1626, 3820 },
            { "archeuus_library_bookcase_g1_middle_door_16_turquoise", 1626, 3821 },
            { "archeuus_library_bookcase_g1_end_left_01", 1625, 3822 },
            { "archeuus_library_bookcase_g2_end_right_door_03_green", 1624, 3822 },
            { "archeuus_library_bookcase_g2_end_left_02_door_07_green", 1624, 3823 },
            { "archeuus_library_bookcase_g1_end_right_door_01_green", 1622, 3823 },
            { "archeuus_library_bookcase_g1_middle_01", 1621, 3823 },
            { "archeuus_library_bookcase_g2_end_left_01_door_07_turquoise", 1622, 3822 },
            { "archeuus_library_bookcase_g1_end_right_door_04_green", 1615, 3819 },
            { "archeuus_library_bookcase_g1_end_right_01", 1614, 3817 },
            { "archeuus_library_bookcase_g1_end_right_door_08_green", 1618, 3814 },
            { "archeuus_library_bookcase_g1_middle_door_07_turquoise", 1620, 3831 },
            { "archeuus_library_bookcase_g1_end_left_02_door_01_green", 1621, 3831 },
            { "archeuus_library_bookcase_g1_middle_door_16_turquoise", 1639, 3828 },
            { "archeuus_library_bookcase_g1_end_right_door_01_green", 1639, 3827 },
            { "archeuus_library_bookcase_g2_end_left_02_door_01_green", 1639, 3822 },
            { "archeuus_library_bookcase_g2_middle_door_12_turquoise", 1639, 3821 },
            { "archeuus_library_bookcase_g2_end_right_door_07_turquoise", 1639, 3820 },
            { "archeuus_library_bookcase_g1_end_left_01_door_03_green", 1645, 3814 },
            { "archeuus_library_bookcase_g1_end_right_01", 1646, 3814 },
            { "archeuus_library_bookcase_g2_end_left_02", 1647, 3814 },
            { "archeuus_library_bookcase_g1_end_left_01", 1650, 3816 },
            { "archeuus_library_bookcase_g1_end_left_02_door_10_green", 1653, 3814 },
            { "archeuus_library_bookcase_g1_end_right_door_08_green", 1654, 3814 },
        }
        -- map_clock/8000 can roll over mid-sweep (~600 ticks of 8000), moving the book to a residue already
        -- searched, so a single sweep can miss. A rollover happens at most once in two sweeps, so a second
        -- full sweep always finds it. max_frames covers the worst case (48 searches).
        local found_at = nil
        for sweep = 1, 2 do
            for i, sh in ipairs(shelves) do
                t.player.walk_to(sh[2], sh[3], 14) -- stalls beside the shelf: its own tile is solid
                local cr, cd = t.player.click_loc(sh[1], 1, { at = { sh[2], sh[3] } })
                local br = t.inv.await("arceuus_library_soulbearerbook", 1, 10) -- the walk in plus the search's 2-tick delay
                if br == "ok" then
                    found_at = sh[2] .. "," .. sh[3] .. " (sweep " .. sweep .. ")"
                    break
                end
            end
            if found_at then
                break
            end
        end
        t.check("findSoulJourneyAndRead.search", found_at ~= nil, "the Soul journey found on the shelf at " .. tostring(found_at))
        t.exec("findSoulJourneyAndRead.read", t.player.inv_op, "arceuus_library_soulbearerbook", 1)
        t.exec("findSoulJourneyAndRead.read-pages", t.chat.play, {
            "mesbox:The Journey of Souls",
            "mesbox:An artefact named the Soul Bearer",
            "mesbox:The book referred to an artefact",
        })
        t.expect("quest.stage.aretha", t.quest.expect_stage("aretha"))

        -- talkToAretha: on foot. Out of the library by its turquoise double door (1632,3817), then
        -- overland east to the Soul Altar (reach.py: 1633,3819 -> 1814,3851 REACH, 328 tiles, no door).
        local function hop(name, x, z, ticks)
            local wr, wd = t.player.walk_to(x, z, ticks)
            if wr ~= "ok" then
                t.ticks(2)
                wr, wd = t.player.walk_to(x, z, ticks)
            end
            local _, at = t.world.tile()
            t.check(name, wr == "ok", "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " (" .. tostring(wd) .. ") at " .. tostring(at.x) .. "," .. tostring(at.z))
        end
        hop("walk-libraryDoor", 1632, 3816, 60)
        t.exec("libraryDoor", t.player.pass_door, { closed = "archeuus_door_double_left_turquoise", open = "archeuus_door_double_left_turquoise_open",
            at = { 1632, 3817, 0 }, near = { 1632, 3816 }, far = { 1632, 3818 } })
        t.exec("walk-soulAltar", t.player.walk_route, {
            { 1632, 3826 }, { 1632, 3834 }, { 1632, 3842 }, { 1632, 3850 }, { 1632, 3858 }, { 1632, 3866 },
            { 1632, 3874 }, { 1635, 3879 }, { 1642, 3880 }, { 1650, 3880 }, { 1658, 3880 }, { 1666, 3880 },
            { 1674, 3880 }, { 1682, 3880 }, { 1690, 3880 }, { 1696, 3882 }, { 1703, 3883 }, { 1707, 3887 },
            { 1713, 3889 }, { 1721, 3889 }, { 1727, 3891 }, { 1735, 3891 }, { 1741, 3893 }, { 1747, 3895 },
            { 1755, 3895 }, { 1763, 3895 }, { 1771, 3895 }, { 1779, 3895 }, { 1786, 3894 }, { 1794, 3894 },
            { 1802, 3894 }, { 1809, 3893 }, { 1815, 3891 }, { 1821, 3889 }, { 1827, 3887 }, { 1826, 3880 },
            { 1825, 3873 }, { 1822, 3868 }, { 1819, 3863 }, { 1817, 3857 }, { 1814, 3852 }, { 1814, 3851 }
        }, { ticks = 40 })
        t.exec("talkToAretha", t.player.talk_to, "arceuus_soulguardian", 1)
        t.exec("talkToAretha-dialog", t.chat.play, {
            "player:I've been reading your book",
            "npc:did you come to ask me where the soul bearer",
            "choose:Yes.",
            "player:Yes.",
            "npc:We buried the artefact",
        })
        t.expect("quest.stage.repair", t.quest.expect_stage("repair"))

        -- arceuusChurchDig: on foot back west to the church's west stair hall (reach.py 1814,3851 ->
        -- 1675,3784 REACH closed-doors, 431 tiles), then its double door (1678,3784) and the stair flight
        -- 1679,3783 up to level 1. The dig spot 1699,3794,0 is the walled ground floor of the church
        -- (reach.py: UNREACHABLE except by stairs); ~bys_try_dig needs distance(coord,
        -- 0_26_59_35_18) <= 8, so no tile outside the church will do.
        t.exec("walk-church", t.player.walk_route, {
            { 1817, 3856 }, { 1818, 3863 }, { 1822, 3867 }, { 1825, 3872 }, { 1826, 3879 }, { 1825, 3884 },
            { 1819, 3886 }, { 1813, 3888 }, { 1807, 3890 }, { 1800, 3891 }, { 1792, 3891 }, { 1785, 3892 },
            { 1778, 3893 }, { 1771, 3894 }, { 1764, 3895 }, { 1756, 3895 }, { 1748, 3895 }, { 1742, 3893 },
            { 1736, 3891 }, { 1728, 3891 }, { 1721, 3890 }, { 1714, 3889 }, { 1708, 3887 }, { 1703, 3884 },
            { 1697, 3882 }, { 1691, 3880 }, { 1683, 3880 }, { 1675, 3880 }, { 1667, 3880 }, { 1659, 3880 },
            { 1651, 3880 }, { 1643, 3880 }, { 1636, 3879 }, { 1635, 3872 }, { 1635, 3864 }, { 1636, 3857 },
            { 1637, 3850 }, { 1641, 3846 }, { 1645, 3842 }, { 1651, 3840 }, { 1659, 3840 }, { 1660, 3833 },
            { 1660, 3825 }, { 1660, 3817 }, { 1657, 3812 }, { 1649, 3812 }, { 1648, 3805 }, { 1649, 3798 },
            { 1653, 3794 }, { 1655, 3788 }, { 1659, 3784 }, { 1664, 3781 }, { 1669, 3784 }, { 1675, 3784 }
        }, { ticks = 40 })
        t.exec("churchDoor", t.player.pass_door, { closed = "archeuus_door_double_left_blue", open = "archeuus_door_double_left_blue_open",
            at = { 1678, 3784, 0 }, near = { 1677, 3784 }, far = { 1678, 3784 } })
        -- The church's west flight (archeuus_stairs_lower_left 1679,3783, pressed from its open
        -- end 1678,3784) lands on the upper floor at 1685,3784,1 (shortest-path transports.tsv:5907,
        -- the generated ladders_stairs maplink.dbrow row [maplink_0_26_59_14_8]); across the upper floor to the
        -- top of the nave stair (archeuus_stairs_upper 1689,3793,1, pressed from 1688,3793), down
        -- to 1695,3793,0 (transports.tsv:5913) on the walled church floor, beside the dig spot.
        t.exec("churchStairsUp", t.player.climb, { loc = "archeuus_stairs_lower_left", op = 1, op_name = "Climb",
            at = { 1679, 3783, 0 }, src = { 1678, 3784 }, dest = { 1685, 3784, 1 }, slack = 0 })
        t.ticks(2)
        t.exec("walk-naveStairs", t.player.walk_route, { { 1686, 3791 }, { 1688, 3793 } })
        t.exec("churchStairsDown", t.player.climb, { loc = "archeuus_stairs_upper", op = 1, op_name = "Climb",
            at = { 1689, 3793, 1 }, src = { 1688, 3793 }, dest = { 1695, 3793, 0 }, slack = 0 })
        t.ticks(2)
        hop("walk-digSpot", 1699, 3794, 20)
        t.exec("arceuusChurchDig", t.player.inv_op, "spade", 1)
        t.exec("arceuusChurchDig-pages", t.chat.play, { "mesbox:You dig up a damaged Soul Bearer" })
        t.exec("arceuusChurchDig-pack", t.inv.await, "arceuus_soulbearer_damaged", 1, 8)

        -- goToTaverleyDungeon: Falador Teleport, overland into Taverley, its dungeon ladder.
        t.player.teleport_cast("falador_teleport", { 2965, 3379, 0 }, { name = "taverleyTeleport", runes = { { "lawrune", 1 }, { "airrune", 3 }, { "waterrune", 1 } }, where = "Falador" })
        local function hop(name, x, z, ticks)
            local wr, wd = t.player.walk_to(x, z, ticks)
            if wr ~= "ok" then
                t.ticks(2)
                wr, wd = t.player.walk_to(x, z, ticks)
            end
            local _, at = t.world.tile()
            t.check(name, wr == "ok", "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " (" .. tostring(wd) .. ") at " .. tostring(at.x) .. "," .. tostring(at.z))
        end
        for i, w in ipairs({ { 2964, 3390 }, { 2957, 3395 }, { 2955, 3405 }, { 2952, 3414 }, { 2949, 3423 }, { 2947, 3433 }, { 2946, 3444 }, { 2940, 3450 }, { 2938, 3450 }, { 2936, 3450 } }) do
            hop("walk-taverleyGate" .. i, w[1], w[2], 40)
        end
        t.exec("taverleyGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 }, near = { 2936, 3450 },
            far_ok = function(tt) return tt.level == 0 and tt.x <= 2935 end, far_desc = "through the east gate into Taverley, x <= 2935" })
        for i, w in ipairs({ { 2924, 3450 }, { 2922, 3442 }, { 2920, 3434 }, { 2912, 3432 }, { 2904, 3430 }, { 2902, 3422 }, { 2895, 3419 }, { 2891, 3413 }, { 2886, 3408 }, { 2884, 3400 }, { 2884, 3398 } }) do
            hop("walk-ladder" .. i, w[1], w[2], 40)
        end
        t.exec("goToTaverleyDungeon", t.player.climb, { loc = "ladder_outside_to_underground", at = { 2884, 3397, 0 }, dest = { 2884, 9798, 0 } })
        -- Seam bearyoursoul_dusty_key_route: on foot from the ladder to the gate's entering
        -- (east) side, the dusty key on deepdungeondoor, then on foot to the cave mouth.
        local function hop(name, x, z, ticks)
            local wr, wd = t.player.walk_to(x, z, ticks)
            if wr ~= "ok" then
                t.ticks(2)
                wr, wd = t.player.walk_to(x, z, ticks)
            end
            local _, at = t.world.tile()
            t.check(name, wr == "ok", "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " (" .. tostring(wd) .. ") at " .. tostring(at.x) .. "," .. tostring(at.z))
        end
        hop("walk-cauldrondoor", 2888, 9831, 60)
        for i = 1, 3 do
            t.exec("cauldrondoor" .. i, t.player.click_loc, "cauldrondoor", 1, { at = { 2889, 9831 } })
            t.ticks(3)
        end
        hop("walk-metalgate", 2897, 9831, 30)
        t.exec("metalgate", t.player.click_loc, "metalgateclosedl", 1, { at = { 2898, 9831 } })
        t.ticks(2)
        hop("walk-railing", 2933, 9813, 80)
        hop("walk-east", 2951, 9790, 80)
        hop("walk-south", 2949, 9774, 80)
        hop("walk-gate-east", 2925, 9803, 80)
        local gate = t.player.by_symbol("loc", "deepdungeondoor")
        t.exec("useDustyKeyOnGate", t.player.use_on, "dusty_key", gate)
        t.ticks(3)
        t.exec("useDustyKeyOnGate.msg", t.msg.expect, "You unlock the gate")
        t.ticks(3)
        hop("walk-in-1", 2905, 9805, 60)
        hop("walk-in-2", 2892, 9799, 60)
        hop("walk-in-3", 2877, 9813, 60)
        local cr, cd = t.player.walk_to(2875, 9846, 60)
        local _, cm = t.world.tile()
        t.check("walk-enterCaveToKeyMaster", math.abs(cm.x - 2874) <= 1 and math.abs(cm.z - 9846) <= 2, "walk_to 2875,9846 -> " .. tostring(cr) .. " at " .. tostring(cm.x) .. "," .. tostring(cm.z))
        t.exec("enterCaveToKeyMaster", t.player.click_loc, "hellhound_cave_entrance_a_01", 1)
        t.ticks(6)
        local kr, kt = t.world.tile()
        t.check("enterCaveToKeyMaster.lobby", kr == "ok", tostring(kt.x) .. "," .. tostring(kt.z))
        t.exec("speakKeyMaster", t.player.talk_to, "keeper_of_keys", 1)
        t.exec("speakKeyMaster-dialog", t.chat.play, {
            "npc:The soul bearer! You have the soul bearer",
            "mesbox:The Key Master repairs your soul bearer",
            "npc:The voices say you must use it wisely",
        })
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.quest.expect_complete()
        local rr, rc = t.inv.count("arceuus_soulbearer")
        t.check("reward.arceuus_soulbearer", rr == "ok" and rc == 1, "arceuus_soulbearer count " .. tostring(rc))
        local dr2, dc = t.inv.count("arceuus_soulbearer_damaged")
        t.check("reward.damaged-consumed", dr2 == "ok" and dc == 0, "damaged count " .. tostring(dc))
        t.finish(0)
    end,
}
