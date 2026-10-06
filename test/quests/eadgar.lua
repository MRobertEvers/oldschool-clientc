-- Eadgar's Ruse. Rewritten from the generated scaffold against the quest's
-- own .rs2 scripts (server/scripts/quests/quest_eadgar/, plus the offer/
-- turn-in half in areas/area_taverly/scripts/sanfew.rs2 and the state
-- machine in quests/quest_troll/scripts/troll_eadgar.rs2).
--
-- Every closed space is walked in AND out (owner rule 2026-10-03, b58
-- re-drive): no goto_tile departs from or lands in a room, a cave or a
-- floor behind a door or a climb. The gotos left are overland hops between
-- open tiles: Lumbridge to the south side of the members' gate (2934,3318;
-- the walk-through gate membergatel 2934,3320 is then pressed: it is the only
-- way on foot from Lumbridge to Taverley), the lane west of Sanfew's house (2890,3428), the
-- ground east of Tenzing's fence gate (2826,3555), the rocks (2856,3611),
-- the Camelot teleport landing (2757,3478), Ardougne zoo, Tegid's river
-- bank, and -- inside the one walkable summit component -- the Trollheim
-- summit beside the stronghold door (2840,3690), beside Eadgar's cave
-- mouth (2893,3671) and at the thistle (2891,3676).
--
-- The summit is a pocket on foot (sampler b58 round 2): no goto ever
-- leaves or reaches it. Every trip DOWN is Camelot Teleport cast from the
-- spellbook (runes and landing graded); every trip UP walks the whole way
-- (walk_up: Tenzing's gate and doors, the stile, both rock pairs, the
-- secret door, the prison, the top exit). The thistle is dried on a fire
-- lit on the summit itself. Everything between is clicked:
--
--   * Sanfew's house: the west door (the map places it standing open, so
--     the open leaf is asserted, not pressed) and the spiral staircase
--     (maplink 0_45_53_17_36 <-> 1_45_53_18_35), both ways.
--   * Tenzing's house: the fence gate, death_sherpa_door and
--     death_sherpa_backdoor (death_doors_mechanism.rs2 walks you through
--     both once %death_equiproom is complete), then the stile.
--   * Troll Stronghold (quest_troll.rs2:114-198): the secret door, the
--     prison stairs, the prison door (selfstage, open 500 ticks), the
--     north/south stairs, the top-floor exit (-> 2840,3690,0) and the top
--     door (-> 2837,10090,2); every landing is read back against the
--     script's movecoord.
--   * Eadgar's cave: troll_mad_eadgar_entrance (-> 2893,10074,2) and
--     troll_mad_eadgar_exit (-> 2893,3671,0) on every visit.
--   * The storeroom: the storeroom door (unlocked with the drawer key),
--     the interior door into the crate room, and the storeroom door again
--     on the way out after the crate guard's knockout drops you in the
--     storeroom (eadgar_troll_sguard.rs2 [queue,troll_guard_teleport]).
--
-- Setup: herblore 31, Druidic Ritual and Troll Stronghold complete
-- (prerequisites sanfew.rs2's `sanfew_more_work` gates on), tutorial kit
-- cleared. The ::give lines are shop-bought/craftable prerequisites the
-- quest's own script consumes one at a time by real clicks below (logs,
-- raw chicken, grain, vodka, pineapple chunks, pestle and mortar, an
-- unfinished ranarr potion) -- never the quest's own deliverable itself
-- (the fake man, the troll truth potion, the goutweed) which is built and
-- fetched for real. The dirty druid robe is NOT given -- it is talked out
-- of Tegid below (eadgar_druid_washing.rs2), same as every other real step.

return {
    id = "eadgar",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000, -- every stronghold and cave trip is walked floor by floor, both ways
    setup = {
        "::clearinv",
        "::setlevel herblore 31",
        "::setlevel hitpoints 99", -- the storeroom guards deal 0-6 unblockable damage per catch (eadgar_troll_sguard.rs2,
        -- eadgar_troll_chief_cook.rs2's crate guard) and a fresh character's 10 hp cannot absorb more than one or two
        -- catches; this is armour against the quest's OWN guaranteed knockout mechanic, not a shortcut through it
        "::setlevel firemaking 99", -- lighting the logs to dry the thistle is a stat_random(firemaking, 64, 512)
        -- roll every tick (skill_firemaking/scripts/firemaking.rs2:79); at level 1 that's a ~13% chance of
        -- a real timeout inside msg.await's budget. Firemaking is not this quest's deliverable (the dried
        -- thistle is), so boosting it is the same kind of prerequisite as the herblore 31 line above.
        "::complete quest_druidicritual", -- quest_cheat.rs2's dispatch row is quest_druidicritual, not quest_druid
        "::complete quest_trollstronghold", -- quest_cheat.rs2:1338: %troll_quest = ^troll_complete (50); the stronghold's own
        -- travel locs (troll_climbingrocks, troll_stronghold_entrance, the prison door) gate on it
        "::setvar varb0_troll_freed_eadgar 1", -- the cheat arm above sets only %troll_quest; freeing Eadgar is part of the same
        -- completed quest (sanfew.rs2's troll gate and troll_mad_eadgar_entrance's level-2 cave read this flag)
        "::setlevel agility 99", -- troll_climbingrocks needs 15 to attempt and rolls stat_random(agility,...) to cross
        -- without a fall (quest_troll.rs2 @rockslide_obstacle); this is armour for a real prerequisite traversal
        -- obstacle (rule (b): a goto past a named guide step is a cheat, so this leg is driven for real below),
        -- not the quest's own deliverable
        "::complete quest_deathplateau", -- quest_cheat.rs2:305: %death_equiproom = ^death_complete and %death_map =
        -- ^death_scouted_area. death_locs.rs2's [opheld2,death_climbingboots] refuses the boots below the first, and
        -- Tenzing's front and back doors walk you through only past ^death_spoken_tenzing/^death_got_map on the second
        -- (death_doors_mechanism.rs2:31-56; run 1 with a bare %death_equiproom setvar knocked on a shut door)
        "::give death_climbingboots 1", -- troll_climbingrocks requires boots worn on its southern approach (coordz=3611)
        "::give logs 2", -- one for Eadgar's scarecrow, one to burn for the troll thistle (see the dryThistle note below)
        "::give tinderbox 1",
        "::give raw_chicken 5",
        "::give grain 10",
        "::give vodka 1",
        "::give pineapple_chunks 1",
        "::give pestle_and_mortar 1",
        "::give ranarrvial 1",
        "::setlevel magic 45", -- Camelot Teleport (magic_spells.dbrow [magic_spell_teleport_camelot]: level 45, 5 air +
        -- 1 law, no quest gate in teleport.rs2:13): the summit is a pocket on foot (sampler b58 round 2), so every trip
        -- DOWN from it to the lowlands is this spell, cast by click from the spellbook; every trip back UP is walked
        "::give airrune 20", -- four casts: off the summit to Pete, from the zoo back towards Trollheim, off the summit
        -- to Tegid, out of the stronghold to Sanfew (same two stacks, no new slot)
        "::give lawrune 4",
        "::give shark 3", -- food for the stronghold's aggressive trolls (b58 run 2: hp 99 -> 49 over the trips, no eat);
        -- the guide names no food but warns Trollheim is dangerous (combat 50). Slots: the 23 above + 2 rune stacks
        -- + 3 sharks = 28 (r2 staged 5 lobsters, 60 hp; 3 sharks are the same 60 hp in the two slots the runes
        -- need -- Falador Teleport, nearer Taverley, would need a third rune stack, water). The boots are worn at
        -- Tenzing's, and the pack never holds more than 27 after that (the parrot replaces the alco-chunks)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp335_eadgar_quest",
            constants = {
                not_started = 0,
                started = 10,
                spoken_eadgar_first = 15,
                spoken_burntmeat_first = 20,
                spoken_burntmeat_second = 25,
                needs_parrot = 30,
                explained_plan = 50,
                hid_parrot = 60,
                needs_items = 70,
                needs_potion = 80,
                needs_parrot_back = 85,
                got_parrot_back = 86,
                got_fake_man = 87,
                got_burnt_meat = 90,
                unlocked_storeroom = 100,
                complete = 110,
            },
            row = "quest_eadgarsruse",
            display = "Eadgar's Ruse",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats (::complete, ::give) are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- -----------------------------------------------------------------
        -- Helpers
        -- -----------------------------------------------------------------
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
            end
            return tostring(r)
        end

        -- The stronghold's trolls attack whoever walks its floors (b58 run 2:
        -- hitpoints 99 -> 91 on the first top-floor walk, 49 by the
        -- storeroom). Hitpoints are sampled after every walk and crossing;
        -- below EAT_BELOW a shark is eaten (early: an eat will no longer
        -- hold a queued hit). Each stronghold trip ends in a margin row:
        -- lowest hp at least a quarter of 99 AND food left.
        local EAT_BELOW = 55
        local hp_low, hp_eaten = nil, 0
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if hp_low == nil or hp.level < hp_low then
                    hp_low = hp.level
                end
                if hp.level < EAT_BELOW then
                    local er = t.player.inv_op("shark", 1)
                    if er == "ok" then
                        hp_eaten = hp_eaten + 1
                    end
                    t.ticks(1)
                end
            end
        end
        local function margin_row(name, trip)
            vitals()
            local fr, food = t.inv.count("shark")
            local hr, hp = t.skill.read("hitpoints")
            t.check(name, hp_low ~= nil and hp_low >= 25 and fr == "ok" and food >= 1,
                trip .. ": lowest hp " .. tostring(hp_low) .. "/99 (sampled after every walk and crossing), hp now "
                    .. tostring(hr == "ok" and hp.level or hr) .. ", sharks staged 3, eaten " .. hp_eaten
                    .. ", left " .. tostring(food) .. " (" .. tostring(fr) .. ") (margin: lowest hp >= 25 AND food left)")
            hp_low = nil
        end

        -- Walk on the current floor and check where the walk ended.
        local function walk_check(name, x, z, level, ticks, tol)
            tol = tol or 1
            local wr, wd = t.player.walk_to(x, z, ticks)
            vitals()
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and tt.level == level and math.abs(tt.x - x) <= tol and math.abs(tt.z - z) <= tol,
                "walk_to(" .. x .. "," .. z .. ") -> " .. tostring(wr) .. " " .. tostring(wd) .. "; tile " .. tile_text(r, tt)
                    .. " (want within " .. tol .. " of " .. x .. "," .. z .. "," .. level .. ")")
        end

        -- Cross one door on foot. Walk to this side, press the CLOSED copy on
        -- the door tile on THIS level (click_loc's `at` answers no_row when no
        -- closed copy stands there: the map placed it open, or an earlier
        -- press left it open). Either way the open leaf must stand within
        -- `tol` of the door tile on this level -- and for a selfstage door
        -- (open symbol == closed symbol) it must have MOVED off the door
        -- tile -- a row that fails when the door is shut. Then walk to the
        -- far side and check the tile.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc, tol)
            tol = tol or 1
            t.player.walk_to(near_x, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. "," .. door_z .. " -> " .. tile_text(nr, nt))
            local here = nr == "ok" and nt.level or -1
            local pr, pd = t.player.click_loc(closed_sym, 1, { at = { door_x, door_z, here } })
            if pr ~= "no_row" then
                t.ticks(1)
            end
            local orr, od = t.world.loc_near(open_sym, tol + 2)
            local open_here = orr == "ok" and od.level == here
                and math.abs(od.tile_x - door_x) <= tol and math.abs(od.tile_z - door_z) <= tol
                and (closed_sym ~= open_sym or od.tile_x ~= door_x or od.tile_z ~= door_z)
            local leaf = open_sym .. ": " .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. tostring(od.level)) or tostring(orr))
                .. " (want within " .. tol .. " of the door tile " .. door_x .. "," .. door_z .. "," .. tostring(here) .. ")"
            if pr == "no_row" then
                t.check(prefix .. ".doorStandsOpen", open_here,
                    "no closed " .. closed_sym .. " on the door tile (" .. tostring(pd) .. "); " .. leaf
                        .. " -- standing open, so it is walked through, not pressed again")
            else
                t.check(prefix .. ".openDoor", pr == "ok" and open_here,
                    "click_loc " .. closed_sym .. " op1 at " .. door_x .. "," .. door_z .. "," .. tostring(here) .. " -> " .. tostring(pr) .. " " .. tostring(pd) .. "; " .. leaf)
            end
            t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- A loc whose op teleports you (stairs, a cave mouth, a stronghold
        -- door or exit, a walk-through door): press the copy on its tile and
        -- level, wait for the landing, and grade the row on the EXACT tile
        -- the script's p_teleport names. A short walk-through hop can answer
        -- `timeout settle_after_click` on a crossing that landed
        -- (start-and-travel: "A short hop (stiles) does not trip it"), so the
        -- row is graded on the tiles before and after, with the click's
        -- answer in the detail.
        local function cross(name, sym, lx, lz, ll, want_ok, want_desc)
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc(sym, 1, { at = { lx, lz, ll } })
            t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and want_ok(tt)
                end,
                note = name .. ": waiting for the landing",
            }, 10)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and br == "ok" and not want_ok(bt) and wr == "ok" and want_ok(wt),
                "from " .. tile_text(br, bt) .. " click_loc(" .. sym .. " at " .. lx .. "," .. lz .. "," .. ll .. ") -> "
                    .. tostring(cr) .. " " .. tostring(cd) .. "; landed " .. tile_text(wr, wt) .. " (want " .. want_desc .. ")")
            vitals()
        end
        local function climb(name, sym, lx, lz, ll, want_x, want_z, want_level)
            cross(name, sym, lx, lz, ll,
                function(tt) return tt.x == want_x and tt.z == want_z and tt.level == want_level end,
                want_x .. "," .. want_z .. "," .. want_level)
        end

        local function inv_row(name, want, why)
            local ok = true
            local parts = {}
            for _, pair in ipairs(want) do
                local r, n = t.inv.count(pair[1])
                ok = ok and r == "ok" and n == pair[2]
                parts[#parts + 1] = pair[1] .. " " .. tostring(n) .. " (" .. tostring(r) .. ", want " .. pair[2] .. ")"
            end
            t.check(name, ok, table.concat(parts, "; ") .. " -- " .. why)
        end

        -- Sanfew's house (Taverley): the west door stands open on the map
        -- (poordooropen 2895,3428; the wall gap is the east edge of
        -- 2894,3428), the spiral staircase goes up from maplink src
        -- 2897,3428 to 2898,3427,1 and back down to 2897,3428,0.
        local function sanfew_in(pfx, up_name)
            pass_door(pfx .. ".houseDoorIn", "poordoor", "poordooropen", 2894, 3428, 2893, 3428, 2896, 3428,
                function(tt) return tt.level == 0 and tt.x >= 2895 and tt.x <= 2901 end, "inside the house, x 2895-2901, level 0")
            walk_check(pfx .. ".toStairs", 2897, 3428, 0, 10, 0)
            climb(up_name, "spiralstairs", 2898, 3428, 0, 2898, 3427, 1)
        end
        local function sanfew_out(pfx, down_name)
            walk_check(pfx .. ".toStairsTop", 2898, 3427, 1, 10, 0)
            climb(down_name, "spiralstairstop", 2898, 3428, 1, 2897, 3428, 0)
            pass_door(pfx .. ".houseDoorOut", "poordoor", "poordooropen", 2894, 3428, 2895, 3428, 2891, 3428,
                function(tt) return tt.level == 0 and tt.x <= 2893 end, "outside the house to the west, x <= 2893, level 0")
        end

        -- Trollheim summit -> the stronghold's top floor through
        -- troll_stronghold_door (2839,3689; p_teleport 2_44_157_21_42).
        local function enter_stronghold(goto_name, enter_name)
            t.exec(goto_name, t.player.goto_tile, 2840, 3690, 0) -- the open summit tile beside the door (the top exits land here)
            climb(enter_name, "troll_stronghold_door", 2839, 3689, 0, 2837, 10090, 2)
            t.drive.camera(0, 383, 600) -- a flatter pitch: the default one framed unrendered void in the gate's pre-login corner (runs 6/7)
        end
        -- Top floor -> summit through the top exit (p_teleport 0_44_57_24_42).
        local function top_exit(pfx, exit_name)
            walk_check(pfx .. ".toTopExit", 2837, 10090, 2, 80)
            climb(exit_name, "troll_stronghold_top_exit_mid", 2838, 10090, 2, 2840, 3690, 0)
            margin_row(pfx .. ".margin", "the walk up and out of the stronghold to the summit")
        end
        -- Summit -> Eadgar's cave, and across the cave to Eadgar.
        local function enter_cave(goto_name, enter_name)
            t.exec(goto_name, t.player.goto_tile, 2893, 3671, 0) -- the cave exit's own landing tile, on the open summit
            climb(enter_name, "troll_mad_eadgar_entrance", 2892, 3672, 0, 2893, 10074, 2)
            t.ticks(2) -- the scene the teleport landed in builds a frame late (run 3: no npc in the pool yet)
            walk_check(enter_name .. ".toEadgar", 2890, 10086, 2, 30)
        end
        local function leave_cave(exit_name)
            climb(exit_name, "troll_mad_eadgar_exit", 2892, 10072, 2, 2893, 3671, 0)
        end
        -- Top floor (north stairs) -> level 1 -> prison door -> the prison.
        local function top_to_prison(pfx, north_name, prison_name)
            walk_check(pfx .. ".toNorthStairs", 2843, 10106, 2, 60)
            climb(north_name, "troll_stronghold_stairstop", 2843, 10108, 2, 2841, 10108, 1)
            pass_door(pfx .. ".prisonDoorEast", "troll_stronghold_prison_door_closed", "troll_stronghold_prison_door_closed", 2848, 10107, 2847, 10107, 2850, 10107,
                function(tt) return tt.level == 1 and tt.x >= 2848 end, "east of the prison door, x >= 2848, level 1")
            walk_check(pfx .. ".toPrisonStairs", 2852, 10105, 1, 20)
            climb(prison_name, "troll_stronghold_stairstop", 2852, 10107, 1, 2852, 10105, 0)
            margin_row(pfx .. ".margin", "the walk from the stronghold door down to the prison")
        end
        -- The prison (level 0) -> prison stairs -> prison door -> north stairs -> top floor -> summit.
        local function prison_to_summit(pfx, stairs_name, top_name, exit_name, from_secret_door)
            if from_secret_door then
                -- 2852,10106 is past the edge of the scene loaded around the
                -- secret door's landing (run 1: walk_to refused move_to), so
                -- the walk goes by the prison corridor first (reach.py: 54
                -- tiles, then 30, every door shut).
                walk_check(pfx .. ".toPrisonCorridor", 2837, 10090, 0, 90)
            end
            walk_check(pfx .. ".toPrisonStairsUp", 2851, 10106, 0, 140)
            climb(stairs_name, "troll_stronghold_stairs", 2852, 10106, 0, 2852, 10109, 1)
            pass_door(pfx .. ".prisonDoorWest", "troll_stronghold_prison_door_closed", "troll_stronghold_prison_door_closed", 2848, 10107, 2848, 10107, 2845, 10107,
                function(tt) return tt.level == 1 and tt.x <= 2847 end, "west of the prison door, x <= 2847, level 1")
            walk_check(pfx .. ".toNorthStairsUp", 2841, 10108, 1, 20)
            climb(top_name, "troll_stronghold_stairs", 2842, 10108, 1, 2845, 10108, 2)
            top_exit(pfx, exit_name)
        end
        -- Kitchen floor (level 1, south) -> south stairs up -> top floor -> summit.
        local function kitchen_to_summit(pfx, top_name, exit_name)
            walk_check(pfx .. ".toSouthStairsUp", 2841, 10051, 1, 40)
            climb(top_name, "troll_stronghold_stairs", 2842, 10051, 1, 2845, 10051, 2)
            top_exit(pfx, exit_name)
        end

        -- The Trollheim summit is a pocket on foot (sampler b58 round 2:
        -- the flood from 2840,3690 is 2,075 tiles and never reaches the
        -- lowlands; the ledge outside the secret door is a separate 57,
        -- the ground between the two rock pairs a separate 169). So EVERY
        -- trip up from the lowlands walks the whole way, the guide's
        -- "Travel to Eadgar" panel: Tenzing's fence gate, his front and
        -- back doors, the stile, both troll_climbingrocks pairs (the
        -- south one with the climbing boots worn, quest_troll.rs2:16),
        -- the secret door, the prison stairs, the prison door, the north
        -- stairs and the top exit. The two gotos left are overland hops
        -- between open tiles (to the ground east of Tenzing's gate, and
        -- from north of the stile to the south rocks). `sfx` is "" on the
        -- first trip (the guide's own step names), a trip name after.
        local function walk_up(sfx, first)
            t.exec("goto-tenzing" .. sfx, t.player.goto_tile, 2826, 3555, 0)
            pass_door("tenzingGate" .. sfx, "death_fencegate_l", "death_openfencegate_l", 2824, 3555, 2825, 3555, 2823, 3555,
                function(tt) return tt.level == 0 and tt.x == 2823 and tt.z == 3555 end, "2823,3555,0 in the yard, west of the gate", 2)
            cross("enterTenzingHouse" .. sfx, "death_sherpa_door", 2822, 3555, 0,
                function(tt) return tt.level == 0 and tt.x >= 2819 and tt.x <= 2822 and tt.z >= 3554 and tt.z <= 3557 end,
                "inside Tenzing's house, x 2819-2822 z 3554-3557")
            if first then
                t.exec("equipClimbingBoots", t.player.equip, "death_climbingboots")
            end
            cross("leaveTenzingHouse" .. sfx, "death_sherpa_backdoor", 2820, 3557, 0,
                function(tt) return tt.level == 0 and tt.z >= 3558 end, "north of the back door, z >= 3558")

            -- click_loc's own settle never resolves for death_fullstyle --
            -- `[oploc1,_stile]` (stiles.rs2) is TWO bare p_teleport()s either
            -- side of a silent `~agility_exactmove`, a hop too short to trip
            -- the "teleport" settle arm. Graded on the read-back tile: the
            -- stile spans 2817,3562-3563 (crossed along z) and the crossing
            -- lands one past the far end, 2817,3564, which only the crossing
            -- itself reaches.
            walk_check("walk-stile" .. sfx, 2817, 3561, 0, 20)
            local stile_before_result, stile_before_tile = t.world.tile()
            local stile_click, stile_detail = t.player.click_loc("death_fullstyle", 1)
            t.ticks(5) -- let the silent ~agility_exactmove crossing finish
            local stile_after_result, stile_after_tile = t.world.tile()
            t.check("climbStile" .. sfx, stile_after_result == "ok" and stile_after_tile ~= nil
                    and stile_after_tile.level == 0 and math.abs(stile_after_tile.x - 2817) <= 1 and stile_after_tile.z >= 3564,
                "click_loc(death_fullstyle) -> " .. tostring(stile_click) .. " " .. tostring(stile_detail)
                    .. "; tile before " .. tile_text(stile_before_result, stile_before_tile)
                    .. ", after " .. tile_text(stile_after_result, stile_after_tile) .. " (want 2817,>=3564,0: north of the stile)")

            t.exec("goto-rocks" .. sfx, t.player.goto_tile, 2856, 3611, 0)
            t.exec("climbRocks" .. sfx, t.player.click_loc, "troll_climbingrocks", 1)
            t.ticks(5) -- let the exact-move crossing finish
            local rocks_after_result, rocks_after_tile = t.world.tile()
            t.check("crossedRocks" .. sfx, rocks_after_result == "ok" and rocks_after_tile ~= nil and rocks_after_tile.z > 3611,
                "world.tile() after troll_climbingrocks -> " .. tile_text(rocks_after_result, rocks_after_tile))
            vitals()
            t.player.walk_to(2834, 3626, 120) -- the path west to the second rock pair (2833-2834,3628)
            t.exec("climbRocks2" .. sfx, t.player.click_loc, "troll_climbingrocks", 1, { at = { 2834, 3628 } })
            t.ticks(6)
            local r2_result, r2_tile = t.world.tile()
            t.check("crossedRocks2" .. sfx, r2_result == "ok" and r2_tile ~= nil and r2_tile.z >= 3629, "tile " .. tile_text(r2_result, r2_tile))
            walk_check("walk-secretdoor" .. sfx, 2827, 3646, 0, 60, 2)

            -- RUN 4 (b55): click_loc's own pose/pixel hunt answered
            -- "screen_position: yaw 0 framed nothing in 5 poses" -- the secret
            -- door's model never rendered a hittable pixel at this approach
            -- tile (a disguised rock-face). drive.op sends the op with no pixel
            -- and no route; the server still validates and runs the real
            -- [oploc1,troll_stronghold_entrance] trigger. Graded on the exact
            -- landing p_teleport(0_44_157_7_2) = 2823,10050,0.
            local secretdoor_target = t.player.by_symbol("loc", "troll_stronghold_entrance")
            local secretdoor_op, secretdoor_detail = t.drive.op(secretdoor_target, 1)
            t.ticks(3)
            local secretdoor_after_result, secretdoor_after_tile = t.world.tile()
            t.check("enterSecretDoor" .. sfx, secretdoor_after_result == "ok" and secretdoor_after_tile.x == 2823
                    and secretdoor_after_tile.z == 10050 and secretdoor_after_tile.level == 0,
                "drive.op(troll_stronghold_entrance) -> " .. tostring(secretdoor_op) .. " " .. tostring(secretdoor_detail)
                    .. "; tile " .. tile_text(secretdoor_after_result, secretdoor_after_tile) .. " (want 2823,10050,0: the secret door's p_teleport(0_44_157_7_2))")

            -- Through the prison and up to the top floor, then out onto the
            -- summit through the top exit (the guide's goUpStairsPrison,
            -- goUpToTopFloorStronghold, exitStronghold).
            prison_to_summit(first and "travel" or ("up" .. sfx), "goUpStairsPrison" .. sfx, "goUpToTopFloorStronghold" .. sfx,
                "exitStronghold" .. sfx, true)
        end

        -- Off the summit: Camelot Teleport, pressed in the spellbook by the
        -- driver's verb (t.player.teleport_cast writes <name>.cast /
        -- .runes / .landed): 5 air + 1 law (magic_spells.dbrow
        -- [magic_spell_teleport_camelot]), landing tele_coord 0_43_54_5_22
        -- = 2757,3478,0 within 2 (teleport.rs2 [label,magic_teleport]).
        local function teleport_down(name)
            t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = name,
                runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        end

        -- ---------------------------------------------------------------
        -- 1. Sanfew: accept the quest (sanfew.rs2 sanfew_more_work).
        -- ---------------------------------------------------------------
        -- The run's first goto obeys the door rule (owner ruling 2026-10-05).
        -- From the Lumbridge fixture the only way on foot to Taverley is the
        -- members' gate south of it (reach.py 3206,3233 -> 2890,3428:
        -- NEEDS-DOOR via membergater@2933,3320), so: overland to the gate's
        -- south side (REACH closed-doors 387), the walk-through gate pressed
        -- (gates.rs2 [label,member_fencegate_try]), then overland from its
        -- north side to the lane west of Sanfew's house (REACH 156).
        t.exec("goto-sanfew-1.memberGate", t.player.goto_tile, 2934, 3318, 0)
        t.exec("goUpToSanfew.memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
            near = { 2934, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2934) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320" })
        t.exec("goto-sanfew-1", t.player.goto_tile, 2890, 3428, 0)
        sanfew_in("accept", "goUpToSanfew")
        t.exec("talkToSanfew-accept", t.player.talk_to, "sanfew", 1)
        t.exec("talkToSanfew-accept-dialog", t.chat.play, {
            "npc:What can I do for you young 'un",
            "choose:Have you any more work for me, to help reclaim the circle?",
            "player:Have you any more work for me to help reclaim the stone circle?",
            "npc:Ah, you've come just in time. I need a certain herb",
            "npc:It used to be quite common, but nowadays only the trolls know",
            "player:And what exactly do you want me to do?",
            "npc:Journey to the north into the land of the trolls",
            "npc:My friend Eadgar lives in the area, and he may be able to help you",
            "choose:I'll head up to Trollheim and see what I can find out.",
            "player:I'll head up to Trollheim and see what I can find out.",
            "mesbox:You are now on a quest: Eadgar's Ruse",
        })
        t.chat.close()
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        sanfew_out("accept", "leaveSanfewAccept")

        -- ---------------------------------------------------------------
        -- 2. Eadgar (1st): ask about goutweed (troll_eadgar.rs2
        --    eadgar_quest_ask_goutweed, reached only while stage=started).
        --
        --    Quest Helper's "Travel to Eadgar" panel: climbOverStile,
        --    climbOverRocks, enterSecretEntrance, goUpStairsPrison,
        --    goUpToTopFloorStronghold, exitStronghold, enterEadgarsCave.
        --    The stile (2817,3562) is behind Tenzing's house: the fence gate
        --    (2824,3555), his front door (2822,3555) and back door
        --    (2820,3557) are the only way through (reach.py: NEEDS-DOOR via
        --    all three).
        -- ---------------------------------------------------------------
        walk_up("", true)

        t.exec("goto-eadgarcaveentrance", t.player.goto_tile, 2893, 3671, 0)
        climb("enterEadgarsCave", "troll_mad_eadgar_entrance", 2892, 3672, 0, 2893, 10074, 2)
        t.ticks(2)
        -- seam28: Eadgar wanders, and on his own stream he stepped next to
        -- the Cave Exit, which then covered every pixel of him. So hunt
        -- rather than retry once: give him a few ticks, step up beside
        -- wherever he is now (a new camera pose), and press again.
        local eadgar_talk_result, eadgar_talk_detail = t.player.talk_to("troll_eadgar", 1)
        for attempt = 1, 8 do
            if eadgar_talk_result == "ok" then
                break
            end
            t.ticks(5)
            local eadgar_target = t.player.by_symbol("npc", "troll_eadgar")
            if eadgar_target then
                t.player.walk_near(eadgar_target, 10)
            end
            eadgar_talk_result, eadgar_talk_detail = t.player.talk_to("troll_eadgar", 1)
        end
        t.check("talkToEadgar-askGoutweed", eadgar_talk_result == "ok",
            "talk_to(troll_eadgar) -> " .. tostring(eadgar_talk_result) .. " " .. tostring(eadgar_talk_detail))
        t.exec("talkToEadgar-askGoutweed-dialog", t.chat.play, {
            "choose:Do you know where I can find some goutweed?",
            "player:Do you know where I can find some goutweed?",
            "npc:Goutweed is used as an ingredient in troll cooking",
            "player:Thanks, I'll go and find one.",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_eadgar_first", t.quest.expect_stage("spoken_eadgar_first"))

        -- ---------------------------------------------------------------
        -- 3. Burntmeat (1st): "bring me a tasty human"
        --    (eadgar_troll_chief_cook.rs2). leaveEadgarsCave,
        --    enterStronghold, goDownSouthStairs are the guide's own steps.
        -- ---------------------------------------------------------------
        leave_cave("leaveEadgarsCave")
        enter_stronghold("goto-strongholddoor", "enterStronghold")
        walk_check("walk-goDownSouthStairs", 2843, 10053, 2, 70)
        climb("goDownSouthStairs", "troll_stronghold_stairstop", 2843, 10051, 2, 2841, 10051, 1)
        walk_check("walk-burntmeat-1", 2844, 10057, 1, 20, 2)
        margin_row("toKitchen1.margin", "the walk from the stronghold door down to the kitchen")
        t.ticks(2)
        t.drive.camera(0, 383, 200)
        t.exec("talkToBurntmeat-1", t.player.talk_to, "eadgar_troll_chief_cook", 1)
        t.exec("talkToBurntmeat-1-dialog", t.chat.play, {
            "player:Er, hi.",
            "npc:Hmm? What human do in troll kitchen?",
            "player:Oh, you don't want to eat me!",
            "npc:Hmm. Burntmeat think you probably right.",
            "player:I'm on a quest to find some goutweed.",
            "npc:Bwahahaha! Burntmeat not give his greatest cooking secret",
            "npc:But Burntmeat also has quest for human!",
            "player:Really? What is it?",
            "npc:Bring back a tasty human for Burntmeat's stew.",
            "player:Right. I'll just...go fetch that for you then. Bye!",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_burntmeat_second", t.quest.expect_stage("spoken_burntmeat_second"))

        -- ---------------------------------------------------------------
        -- 4. Eadgar (2nd): explains the fake-man plan -> needs a parrot.
        -- ---------------------------------------------------------------
        kitchen_to_summit("fromCook", "goUpToTopFloorStrongholdFromCook", "exitStrongholdFromCook")
        enter_cave("goto-enterEadgarsCaveFromCook", "enterEadgarsCaveFromCook")
        t.exec("talkToEadgar-explainParrot", t.player.talk_to, "troll_eadgar", 1)
        t.exec("talkToEadgar-explainParrot-dialog", t.chat.play, {
            "player:The troll cook wants me to bring him a tasty human",
            "npc:Hoho! Does he now?",
            "npc:What we need is something that looks like a human",
            "player:And how exactly are we going to manage that?",
            "npc:Yes! It's bound to work. First of all, I will need a parrot!",
            "player:A parrot? Alright, I'll see what I can do.",
        })
        t.chat.close()
        t.expect("quest.stage.needs_parrot", t.quest.expect_stage("needs_parrot"))

        -- ---------------------------------------------------------------
        -- 5. Ardougne Zoo: Parroty Pete, make alco-chunks, lure the parrot
        --    through the aviary hatch.
        -- ---------------------------------------------------------------
        -- make_alco_chunks reads two independent bits
        -- (eadgar_pete_dialog_1/_2) set only by Pete's "When did you add
        -- it?" and "What do you feed them?" answers
        -- (eadgar_zoo_keeper_aviary.rs2:59-65), so two conversations.
        -- Off the summit by Camelot Teleport (the summit is a pocket on
        -- foot), then overland to the zoo.
        leave_cave("leaveEadgarsCaveForPete")
        teleport_down("teleportCamelotForPete")
        t.exec("goto-pete", t.player.goto_tile, 2611, 3285, 0)
        t.exec("talkToPete-1", t.player.talk_to, "eadgar_zoo_keeper_aviary", 1)
        t.exec("talkToPete-1-dialog", t.chat.play, {
            "npc:Good day, good day. Come to admire the new parrot aviary",
            "choose:When did you add it?",
            "player:When did you add it?",
            "npc:Just recently. It would have been sooner",
        })
        t.chat.close()

        t.exec("talkToPete-2", t.player.talk_to, "eadgar_zoo_keeper_aviary", 1)
        t.exec("talkToPete-2-dialog", t.chat.play, {
            "npc:Good day, good day. Come to admire the new parrot aviary",
            "choose:What do you feed them?",
            "player:What do you feed them?",
            "npc:Well, fruit and grain mostly",
        })
        t.chat.close()
        t.exec("makeAlcoChunks", t.player.use_item_on_item, "vodka", "pineapple_chunks")
        t.inv.await("eadgar_alco_chunks", 1, 10)
        t.chat.close()
        inv_row("makeAlcoChunks.items", { { "eadgar_alco_chunks", 1 }, { "vodka", 0 }, { "pineapple_chunks", 0 } },
            "vodka on pineapple chunks makes the alco-chunks")

        -- No goto here: the hatch's own tile (2611,3287,0) puts the player
        -- where Parroty Pete's model occludes it; use_on's own walk-into-
        -- range from Pete's spot lands the click (b55 runs 1-3).
        local hatch_lookup_result, hatch_row = t.world.loc_near("eadgar_aviary_wall_hatch", 15)
        t.check("lookup.hatch", hatch_row ~= nil,
            "world.loc_near(eadgar_aviary_wall_hatch, 15) -> " .. tostring(hatch_lookup_result))
        local hatch_target = t.player.by_symbol("loc", "eadgar_aviary_wall_hatch")
        -- b58 run 1: the press once landed on 'Examine Aviary Hatch' ("the
        -- arming was gone by the time the menu opened"), a pointer seam;
        -- the item use is pressed again until it lands (the row after it
        -- grades the pack, so a press that never lands still fails).
        local catch_result, catch_detail = t.player.use_on("eadgar_alco_chunks", hatch_target)
        for attempt = 2, 3 do
            if catch_result == "ok" then
                break
            end
            t.chat.close()
            t.ticks(2)
            catch_result, catch_detail = t.player.use_on("eadgar_alco_chunks", hatch_target)
            catch_detail = "attempt " .. attempt .. ": " .. tostring(catch_detail)
        end
        t.check("catchParrot", catch_result == "ok",
            "use_on(eadgar_alco_chunks, eadgar_aviary_wall_hatch) -> " .. tostring(catch_result) .. " " .. tostring(catch_detail))
        t.inv.await("eadgar_drunk_parrot", 1, 10)
        t.chat.close()
        inv_row("catchParrot.items", { { "eadgar_drunk_parrot", 1 }, { "eadgar_alco_chunks", 0 } },
            "the guide's useChunksOnParrot: the alco-chunks left the pack through the hatch and the drunk parrot came back")

        -- ---------------------------------------------------------------
        -- 6. Give the parrot to Eadgar -> explained_plan. Hide it under
        --    the rack in the prison -> hid_parrot.
        -- ---------------------------------------------------------------
        -- Back up to Trollheim on foot, the whole way (the guide's
        -- enterEadgarsCaveWithParrot), then the cave.
        -- The zoo is ~950 tiles' walk from Tenzing's: Camelot Teleport first
        -- (a player's way back north), then overland to Burthorpe.
        teleport_down("teleportCamelotFromZoo")
        walk_up("WithParrot")
        enter_cave("goto-enterEadgarCaveWithTrainedParrot", "enterEadgarCaveWithTrainedParrot")
        local eadgar_npc = t.player.by_symbol("npc", "troll_eadgar")
        t.exec("giveParrotToEadgar", t.player.use_on, "eadgar_drunk_parrot", eadgar_npc)
        t.exec("giveParrotToEadgar-dialog", t.chat.play, {
            "player:Here it is!",
            "npc:Now are you going to explain your plan?",
            "npc:If we hide the parrot somewhere the trolls talk",
            "player:I'll go hide it right away.",
        })
        t.chat.close()
        t.expect("quest.stage.explained_plan", t.quest.expect_stage("explained_plan"))
        inv_row("giveParrotToEadgar.kept", { { "eadgar_drunk_parrot", 1 } },
            "Eadgar hands the parrot back to hide: troll_eadgar.rs2 keeps it in the pack at explained_plan")

        local rack_target = t.player.by_symbol("loc", "eadgar_rack")
        leave_cave("leaveEadgarsCaveWithParrot")
        enter_stronghold("goto-strongholddoor-WithParrot", "enterStrongholdWithParrot")
        top_to_prison("withParrot", "goDownNorthStairsWithParrot", "goDownToPrisonWithParrot")
        walk_check("walk-rack-1", 2829, 10095, 0, 60)
        t.exec("hideParrot", t.player.use_on, "eadgar_drunk_parrot", rack_target)
        t.chat.close()
        t.expect("quest.stage.hid_parrot", t.quest.expect_stage("hid_parrot"))
        inv_row("hideParrot.items", { { "eadgar_drunk_parrot", 0 } }, "the guide's parrotOnRack: the parrot left the pack under the rack")

        -- ---------------------------------------------------------------
        -- 7. Eadgar (3rd): explain the shopping list -> needs_items.
        -- ---------------------------------------------------------------
        prison_to_summit("hiddenParrot", "leavePrisonHiddenParrot", "goUpToTopFloorHiddenParrot", "exitStrongholdHiddenParrot")
        enter_cave("goto-enterEadgarsCaveHiddenParrot", "enterEadgarsCaveHiddenParrot")
        t.exec("talkToEadgar-explainItems", t.player.talk_to, "troll_eadgar", 1)
        t.exec("talkToEadgar-explainItems-dialog", t.chat.play, {
            "player:I've hidden the parrot under the rack.",
            "npc:Splendid! Now, we'll just make a scarecrow.",
            "npc:We'll just stuff it with a few chickens.",
            "npc:And if we use dirty clothes it'll smell human",
            "player:Logs, ten sheaves of grain, five raw chickens",
        })
        t.chat.close()
        t.expect("quest.stage.needs_items", t.quest.expect_stage("needs_items"))

        -- ---------------------------------------------------------------
        -- 8. Tegid: talk him out of a dirty robe.
        -- ---------------------------------------------------------------
        leave_cave("leaveEadgarsCaveForTegid")
        teleport_down("teleportCamelotForTegid")
        t.exec("goto-tegid", t.player.goto_tile, 2913, 3417, 0)
        t.exec("talkToTegid-robe", t.player.talk_to, "eadgar_druid_washing", 1)
        t.exec("talkToTegid-robe-dialog", t.chat.play, {
            "player:Could I have one of your dirty robes?",
            "npc:What? No! These are my robes!",
            "choose:I'm sure Sanfew won't be happy when I tell him it's your fault he can't perform the purification ritual.",
            "player:I'm sure Sanfew won't be happy",
            "npc:What? Oh well, if it's a matter of that much importance",
        })
        t.chat.close()
        t.inv.await("eadgar_dirty_druid_robe", 1, 10)
        inv_row("talkToTegid.robe", { { "eadgar_dirty_druid_robe", 1 } }, "Tegid's robe")

        -- ---------------------------------------------------------------
        -- 9. Deliver logs, robe, 5 raw chicken, 10 grain to Eadgar. Each
        --    delivery is a real opnpcu click.
        -- ---------------------------------------------------------------
        walk_up("WithItems")
        enter_cave("goto-enterEadgarsCaveWithItems", "enterEadgarsCaveWithItems")
        eadgar_npc = t.player.by_symbol("npc", "troll_eadgar")

        t.exec("giveLogs", t.player.use_on, "logs", eadgar_npc)
        t.exec("giveLogs-dialog", t.chat.play, {
            "player:Here are some logs for you.",
            "npc:Wonderful! That's one thing off the list.",
        })
        t.chat.close()

        t.exec("giveRobe", t.player.use_on, "eadgar_dirty_druid_robe", eadgar_npc)
        t.exec("giveRobe-dialog", t.chat.play, {
            "player:Here are some dirty clothes.",
            "npc:Splendid! They smell absolutely dreadful.",
        })
        t.chat.close()

        -- Every chicken and every grain delivery shows the SAME npc line;
        -- without closing between deliveries an identical repeat page is
        -- not a page TRANSITION and the next press is swallowed (run 4/5).
        for i = 1, 5 do
            t.exec("giveChicken" .. i, t.player.use_on, "raw_chicken", eadgar_npc)
            if i < 5 then
                t.exec("giveChicken" .. i .. "-dialog", t.chat.play, {"npc:Good, keep them coming!"})
            else
                t.exec("giveChicken" .. i .. "-dialog", t.chat.play, {"npc:Excellent, that's all the chickens I need!"})
            end
            t.chat.close()
            t.ticks(1)
        end

        for i = 1, 10 do
            t.exec("giveGrain" .. i, t.player.use_on, "grain", eadgar_npc)
            if i < 10 then
                t.exec("giveGrain" .. i .. "-dialog", t.chat.play, {"npc:More stuffing! Keep it up!"})
            else
                t.exec("giveGrain" .. i .. "-dialog", t.chat.play, {
                    "npc:That's all the grain I need too!",
                    "npc:That's everything I need! Now, if you can just get me",
                })
            end
            t.chat.close()
            t.ticks(1)
        end
        t.expect("quest.stage.needs_potion", t.quest.expect_stage("needs_potion"))
        inv_row("talkToEadgarWithItems.items",
            { { "logs", 1 }, { "eadgar_dirty_druid_robe", 0 }, { "raw_chicken", 0 }, { "grain", 0 } },
            "one of the two logs, the robe, all 5 chickens and all 10 grain left the pack into the scarecrow (the second log is burned below)")

        -- ---------------------------------------------------------------
        -- 10. Pick a troll thistle, dry it, grind it, mix it into a
        --     ranarr potion (unf) to make the troll truth potion.
        -- ---------------------------------------------------------------
        leave_cave("leaveEadgarsCaveForThistle")
        t.exec("goto-thistle", t.player.goto_tile, 2891, 3676, 0)
        t.exec("pickThistle", t.player.talk_to, "eadgar_troll_thistle", 1)
        t.inv.await("eadgar_troll_thistle", 1, 10)
        t.chat.close()
        inv_row("pickThistle.item", { { "eadgar_troll_thistle", 1 } }, "the picked thistle")

        -- A fire the player lights is the real `fire` loc (firemaking.rs2);
        -- troll_stronghold_camp_fire falls through to cooking's generic
        -- "You can't cook that." (run 6). The guide's lightFire names no
        -- place, so the fire is lit right here beside the thistle, on the
        -- summit (firemaking.rs2 [proc,area_allow_loc_add] refuses only a
        -- blocked tile): no trip down and back up for it.
        local fire_from_r, fire_from = t.world.tile()
        t.check("fire.onSummit", fire_from_r == "ok" and fire_from.level == 0 and fire_from.x >= 2870 and fire_from.x <= 2910
                and fire_from.z >= 3660 and fire_from.z <= 3700,
            "lighting the fire at " .. tile_text(fire_from_r, fire_from) .. " (want the Trollheim summit beside the thistle)")
        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        local fire_lit_result, fire_lit_detail = t.msg.await("The fire catches", 15)
        t.check("fire.lit", fire_lit_result == "ok",
            "msg.await('The fire catches', 15) -> " .. tostring(fire_lit_result) .. " " .. tostring(fire_lit_detail))
        inv_row("lightFire.logs", { { "logs", 0 }, { "tinderbox", 1 } }, "the second log burned")

        -- loc_near at a tight radius pins the fire we just lit (run 7:
        -- by_symbol found some OTHER fire).
        local fire_lookup_result, fire_row = t.world.loc_near("fire", 3)
        t.check("lookup.fire", fire_row ~= nil,
            "world.loc_near(fire, 3) -> " .. tostring(fire_lookup_result))
        t.exec("dryThistle", t.player.use_on, "eadgar_troll_thistle", fire_row)
        t.chat.close()

        -- dryThistle's own settle fires on the mes() line, one packet ahead
        -- of the inv_del/inv_add pair behind it: poll first.
        t.inv.await("eadgar_dried_troll_thistle", 1, 10)
        local dried_read, dried_count = t.inv.count("eadgar_dried_troll_thistle")
        local thistle_read, thistle_count = t.inv.count("eadgar_troll_thistle")
        t.check("thistle.dried",
            dried_read == "ok" and dried_count == 1 and thistle_read == "ok" and thistle_count == 0,
            "inv.count(eadgar_dried_troll_thistle) -> " .. tostring(dried_read) .. " " .. tostring(dried_count)
                .. "; inv.count(eadgar_troll_thistle) -> " .. tostring(thistle_read) .. " " .. tostring(thistle_count)
                .. " -- ~eadgar_dry_troll_thistle, quest_eadgar/scripts/eadgar_troll_thistle.rs2:60-63")

        -- ---------------------------------------------------------------
        -- 11. Grind the dried thistle, then mix it into the ranarr potion
        --     (unf) to make the troll truth potion
        --     (eadgar_troll_thistle.rs2:39-57).
        -- ---------------------------------------------------------------
        t.exec("grindThistle", t.player.use_item_on_item, "pestle_and_mortar", "eadgar_dried_troll_thistle")
        t.inv.await("eadgar_ground_troll_thistle", 1, 10)
        t.chat.close()
        inv_row("grindThistle.items", { { "eadgar_ground_troll_thistle", 1 }, { "eadgar_dried_troll_thistle", 0 }, { "pestle_and_mortar", 1 } },
            "the pestle and mortar ground the dried thistle")

        t.exec("mixPotion", t.player.use_item_on_item, "ranarrvial", "eadgar_ground_troll_thistle")
        t.inv.await("eadgar_ground_troll_thistle_potion", 1, 10)
        t.chat.close()
        inv_row("mixPotion.items", { { "eadgar_ground_troll_thistle_potion", 1 }, { "eadgar_ground_troll_thistle", 0 }, { "ranarrvial", 0 } },
            "the guide's useGroundThistleOnRanarr: the ground thistle and the ranarr potion (unf) became the troll potion")

        -- ---------------------------------------------------------------
        -- 12. Give the troll truth potion to Eadgar -> needs_parrot_back.
        -- ---------------------------------------------------------------
        enter_cave("goto-enterEadgarsCaveWithTrollPotion", "enterEadgarsCaveWithTrollPotion")
        eadgar_npc = t.player.by_symbol("npc", "troll_eadgar")
        t.exec("givePotion", t.player.use_on, "eadgar_ground_troll_thistle_potion", eadgar_npc)
        t.exec("givePotion-dialog", t.chat.play, {
            "player:I've got the troll truth potion.",
            "npc:Excellent, thank you. Now just go fetch that poor parrot back",
        })
        t.chat.close()
        t.expect("quest.stage.needs_parrot_back", t.quest.expect_stage("needs_parrot_back"))
        inv_row("givePotion.items", { { "eadgar_ground_troll_thistle_potion", 0 } }, "the guide's giveTrollPotionToEadgar: the potion left the pack")

        -- ---------------------------------------------------------------
        -- 13. Fetch the trained parrot back from the rack -> got_parrot_back
        --     (eadgar_troll_chief_cook.rs2 [oploc1,eadgar_rack]).
        -- ---------------------------------------------------------------
        leave_cave("leaveEadgarsCaveForParrot")
        enter_stronghold("goto-strongholddoor-ForParrot", "enterStrongholdForParrot")
        top_to_prison("forParrot", "goDownNorthStairsForParrot", "goDownToPrisonForParrot")
        walk_check("walk-rack-2", 2829, 10095, 0, 60)
        t.exec("fetchParrot", t.player.click_loc, "eadgar_rack", 1)
        t.exec("fetchParrot-dialog", t.chat.play, {
            "mesbox:Parrot: Ah, hello Sir. Could you please free me?",
        })
        t.chat.close()
        t.inv.await("eadgar_drunk_parrot", 1, 10)
        t.expect("quest.stage.got_parrot_back", t.quest.expect_stage("got_parrot_back"))

        -- ---------------------------------------------------------------
        -- 14. Show the trained parrot to Eadgar -> makes the fake man,
        --     got_fake_man (troll_eadgar.rs2 eadgar_quest_make_fake_man).
        -- ---------------------------------------------------------------
        prison_to_summit("withParrotBack", "leavePrisonWithParrot", "goUpToTopFloorWithParrot", "leaveStrongholdWithParrot")
        enter_cave("goto-enterEadgarsCaveWithTrainedParrotAgain", "enterEadgarsCaveWithTrainedParrotAgain")
        eadgar_npc = t.player.by_symbol("npc", "troll_eadgar")
        t.exec("makeFakeMan", t.player.use_on, "eadgar_drunk_parrot", eadgar_npc)
        t.exec("makeFakeMan-dialog", t.chat.play, {
            "npc:Can you tell this isn't a bona fide human being? I sure can't!",
            "player:It's remarkably convincing.",
            "npc:Take it to the troll cook -- and don't let anyone else see it",
        })
        t.chat.close()
        t.inv.await("eadgar_fake_man", 1, 10)
        t.expect("quest.stage.got_fake_man", t.quest.expect_stage("got_fake_man"))
        inv_row("makeFakeMan.items", { { "eadgar_fake_man", 1 }, { "eadgar_drunk_parrot", 0 } }, "the parrot went into the fake man")

        -- ---------------------------------------------------------------
        -- 15. Take the fake man to Burntmeat -> got_burnt_meat, learn the
        --     storeroom key's hiding place.
        -- ---------------------------------------------------------------
        leave_cave("leaveEadgarsCaveWithScarecrow")
        enter_stronghold("goto-strongholddoor-WithScarecrow", "enterStrongholdWithScarecrow")
        walk_check("walk-goDownSouthStairsWithScarecrow", 2843, 10053, 2, 70)
        climb("goDownSouthStairsWithScarecrow", "troll_stronghold_stairstop", 2843, 10051, 2, 2841, 10051, 1)
        walk_check("walk-burntmeat-2", 2844, 10057, 1, 20, 2)
        margin_row("toKitchen2.margin", "the walk from the stronghold door down to the kitchen")
        local burntmeat_npc = t.player.by_symbol("npc", "eadgar_troll_chief_cook")
        t.exec("giveFakeMan", t.player.use_on, "eadgar_fake_man", burntmeat_npc)
        t.exec("giveFakeMan-dialog", t.chat.play, {
            "npc:Did you find tasty human? Burntmeat smell something good.",
            "player:Yes! Look!",
            "npc:Ah, dat look like nice tasty human.",
            "npc:Yep, sound like human too. Burntmeat put it in stew.",
            "player:This is burnt meat.",
            "npc:It first thing I ever try to cook! Very precious to Burntmeat.",
            "player:Thank you... and how's the stew?",
            "npc:Slurp, mmm... Human stew cheer Burntmeat up!",
            "choose:So, where can I get some goutweed?",
            "player:So, where can I get some goutweed?",
            "npc:Hah! Trolls pick it all until none left, many years ago.",
            "npc:It well guarded, and Burntmeat hide key in fake bottom of kitchen drawer.",
            "player:That's some well-guarded secret alright.",
        })
        t.chat.close()
        t.inv.await("burnt_meat", 1, 10)
        t.expect("quest.stage.got_burnt_meat", t.quest.expect_stage("got_burnt_meat"))
        inv_row("giveFakeMan.items", { { "eadgar_fake_man", 0 }, { "burnt_meat", 1 } }, "the guide's talkToCookWithScarecrow: the fake man went into the stew")

        -- ---------------------------------------------------------------
        -- 16. Search the kitchen drawers (2852/2854,10049,1) for the
        --     storeroom key, go down the storeroom stairs (2852,10061,1 ->
        --     2852,10064,0), unlock the storeroom door (2869,10085, on its
        --     south edge), cross the interior door (2861,10092, its east
        --     edge) into the crate room and search the crate for goutweed
        --     (eadgar_troll_chief_cook.rs2:120-218).
        -- ---------------------------------------------------------------
        walk_check("walk-drawers", 2853, 10051, 1, 30)
        t.exec("openDrawers", t.player.click_loc, "eadgar_kitchen_drawers", 1)
        t.ticks(3) -- let the loc_change to eadgar_kitchen_drawers_open land before searching it
        t.exec("searchDrawers", t.player.click_loc, "eadgar_kitchen_drawers_open", 2)
        t.inv.await("eadgar_troll_storeroom_key", 1, 10)
        inv_row("searchDrawers.key", { { "eadgar_troll_storeroom_key", 1 } }, "the storeroom key from the drawer's fake bottom")

        walk_check("walk-goDownToStoreroom", 2853, 10060, 1, 20)
        climb("goDownToStoreroom", "troll_stronghold_stairstop", 2852, 10061, 1, 2852, 10064, 0)
        -- The storeroom's 24 tiles (flood fill from the knockout tile
        -- 2865,10088 over the map with its two doors shut).
        local STOREROOM = {}
        for _, c in ipairs({ { 2862, 10092 }, { 2863, 10090 }, { 2863, 10091 }, { 2863, 10092 }, { 2864, 10089 }, { 2864, 10090 },
                { 2864, 10091 }, { 2864, 10092 }, { 2865, 10088 }, { 2865, 10089 }, { 2865, 10090 }, { 2866, 10088 }, { 2866, 10089 },
                { 2866, 10090 }, { 2867, 10088 }, { 2867, 10089 }, { 2867, 10090 }, { 2868, 10087 }, { 2868, 10088 }, { 2868, 10089 },
                { 2869, 10085 }, { 2869, 10086 }, { 2869, 10087 }, { 2869, 10088 } }) do
            STOREROOM[c[1] * 100000 + c[2]] = true
        end
        local function in_storeroom(tt)
            return tt.level == 0 and STOREROOM[tt.x * 100000 + tt.z] == true
        end
        -- The storeroom door (2869,10085, a wall on that tile's south edge)
        -- is a walk-through door (eadgar_troll_chief_cook.rs2
        -- [oploc1,eadgar_storeroomdoor], LostCity quest_eadgar.rs2:488-506):
        -- from the corridor the drawer key unlocks it once at got_burnt_meat
        -- ("You unlock the door.", the key is used up) and
        -- [proc,eadgar_storeroomdoor_pass] puts the player on the door tile,
        -- inside; the open leaf stands one tile south for 3 ticks.
        walk_check("walk-storeroomdoor", 2869, 10084, 0, 60, 0)
        local key_before_r, key_before = t.inv.count("eadgar_troll_storeroom_key")
        cross("enterStoreroomDoor", "eadgar_storeroomdoor", 2869, 10085, 0, in_storeroom,
            "inside the storeroom: the walk-through lands on the door tile 2869,10085,0")
        -- already in the ring: the crossing's own click settled on it
        t.expect("enterStoreroomDoor.unlockMessage", t.msg.expect("You unlock the door."))
        local key_after_r, key_after = t.inv.count("eadgar_troll_storeroom_key")
        t.check("enterStoreroomDoor.keyUsed", key_before_r == "ok" and key_before == 1 and key_after_r == "ok" and key_after == 0,
            "eadgar_troll_storeroom_key " .. tostring(key_before) .. " (" .. tostring(key_before_r) .. ") -> " .. tostring(key_after)
                .. " (" .. tostring(key_after_r) .. ") (want 1 -> 0: the one-time unlock uses the key up)")
        t.expect("quest.stage.unlocked_storeroom", t.quest.expect_stage("unlocked_storeroom"))
        walk_check("enterStoreroom.walkIn", 2869, 10087, 0, 10, 0)

        -- The crate room is walked into through its door; eight troll_sguard
        -- guards patrol it on fixed loops (configs/quest_eadgar.npc) and
        -- each tick catch a player on their own tile or one/two tiles ahead
        -- (eadgar_troll_sguard.rs2 [proc,eadgar_troll_sguard_hunt]); a catch
        -- knocks the player back into the storeroom (2865,10088). Walking
        -- straight in was caught 6 times of 6 (b58 probe 1), so the room is
        -- crossed the way the guide says, "avoid the troll guards": hop
        -- between tiles no patrol or look-ahead ever covers (HOPS below),
        -- and start each hop only when the guards' own loops, read off
        -- their live tiles, keep every tile of it clear tick by tick at a
        -- walk. The search is made from 2858,10074 on the east crate
        -- (2857,10074), clear of the posted guard's tile (2857,10075). A
        -- catch anyway is retried from the storeroom.
        local SGUARDS = {
            { "troll_sguard1", { 34, 44, 43, 44, 43, 41, 34, 41 } },
            { "troll_sguard2", { 34, 41, 34, 38, 39, 38, 39, 41 } },
            { "troll_sguard3", { 39, 41, 39, 38, 43, 38, 43, 41 } },
            { "troll_sguard4", { 34, 38, 38, 38, 38, 34, 36, 34, 36, 30, 34, 30 } },
            { "troll_sguard5", { 45, 32, 47, 32, 44, 29, 45, 29, 48, 32, 48, 37, 45, 37 } },
            { "troll_sguard6", { 39, 34, 36, 34, 36, 30, 34, 30, 34, 27, 37, 27, 37, 29, 39, 29 } },
            { "troll_sguard7", { 42, 28, 42, 34, 39, 34, 39, 28 } },
            { "troll_sguard8", { 48, 37, 48, 32, 45, 29, 44, 29, 47, 32, 45, 32, 45, 37 } },
        }
        local function sgn(v)
            if v > 0 then return 1 elseif v < 0 then return -1 end
            return 0
        end
        for _, g in ipairs(SGUARDS) do
            local pts = {}
            for i = 1, #g[2], 2 do
                pts[#pts + 1] = { 2816 + g[2][i], 10048 + g[2][i + 1] }
            end
            local tiles = {}
            local cx, cz = pts[1][1], pts[1][2]
            for k = 2, #pts + 1 do
                local nxt = pts[(k - 1) % #pts + 1]
                while cx ~= nxt[1] or cz ~= nxt[2] do
                    cx, cz = cx + sgn(nxt[1] - cx), cz + sgn(nxt[2] - cz)
                    tiles[#tiles + 1] = { cx, cz }
                end
            end
            g.tiles = tiles
        end
        local function guard_tiles()
            local out = {}
            for gi, g in ipairs(SGUARDS) do
                local r, row = t.npc.nearest(g[1], 40)
                if r == "ok" and type(row) == "table" and row.x then
                    out[gi] = { row.x, row.z }
                end
            end
            return out
        end
        -- Two reads a tick apart place each guard on its loop (the step
        -- between them picks the right copy of a tile a loop passes twice).
        local function sync_guards()
            local p0 = guard_tiles()
            t.ticks(1)
            local p1 = guard_tiles()
            local idx = {}
            for gi, g in ipairs(SGUARDS) do
                local a, b = p0[gi], p1[gi]
                if b then
                    local L = #g.tiles
                    local pick
                    for i = 1, L do
                        local tl = g.tiles[i]
                        if tl[1] == b[1] and tl[2] == b[2] then
                            local pv = g.tiles[(i - 2) % L + 1]
                            if a and (a[1] ~= b[1] or a[2] ~= b[2]) then
                                if pv[1] == a[1] and pv[2] == a[2] then
                                    pick = i
                                    break
                                end
                            elseif pick == nil then
                                pick = i
                            end
                        end
                    end
                    idx[gi] = pick
                end
            end
            return idx, p1
        end
        local function threatened(gi, i, k, x, z)
            local g = SGUARDS[gi]
            local L = #g.tiles
            local p = g.tiles[(i - 1 + k) % L + 1]
            local q = g.tiles[(i - 2 + k) % L + 1]
            local hx, hz = p[1] - q[1], p[2] - q[2]
            for s = 0, 2 do
                if p[1] + hx * s == x and p[2] + hz * s == z then
                    return true
                end
            end
            return false
        end
        -- A hop of n tiles started now, at a walk (one tile a tick; the
        -- run is switched off first so the speed is known): at tick k the
        -- player stands on tile k, or still on tile k-1 when the guards'
        -- hunt runs before the player's step, or up to `ahead` tiles
        -- further where the client may cut a corner diagonally. Every such
        -- tile must be outside every guard's reach at tick k, and at tick
        -- k-1 too (the guard reads can be a tick stale: b58 probe 10 was
        -- caught by troll_sguard8's look-ahead from the tile it had just
        -- left). eadgar_tools/windows.py counts the windows this leaves
        -- over the guards' joint loops: 81/1848 ticks for the first hop,
        -- 2/22 for the column.
        local function hop_clear(seg, ahead, idx, pos)
            local n = #seg
            for k = 0, n + 2 do
                for j = math.max(1, k - 1), math.min(n, k + ahead) do
                    local x, z = seg[j][1], seg[j][2]
                    for gi = 1, #SGUARDS do
                        if idx[gi] then
                            for dk = -1, 0 do
                                if k + dk >= 0 and threatened(gi, idx[gi], k + dk, x, z) then
                                    return false, SGUARDS[gi][1] .. " reaches " .. x .. "," .. z .. " at tick " .. (k + dk)
                                end
                            end
                        elseif pos[gi] == nil or math.max(math.abs(pos[gi][1] - x), math.abs(pos[gi][2] - z)) <= 6 then
                            return false, SGUARDS[gi][1] .. " is off its loop or unseen (" .. (pos[gi] and (pos[gi][1] .. "," .. pos[gi][2]) or "no row") .. ")"
                        end
                    end
                end
            end
            return true, "clear"
        end
        -- Hideouts no patrol or look-ahead ever covers: the doorway
        -- 2862,10092 (storeroom side), 2862,10086, 2860,10082, 2861,10076,
        -- 2860,10075, then 2860,10074 -> 2858,10074 beside the east
        -- goutweed crate (2857,10074) are safe tiles all the way.
        local HOPS = {
            { name = "doorToMiddle", dest = { 2862, 10086 }, ahead = 0, seg = { { 2861, 10092 }, { 2860, 10092 }, { 2859, 10092 }, { 2859, 10091 },
                { 2859, 10090 }, { 2859, 10089 }, { 2859, 10088 }, { 2859, 10087 }, { 2859, 10086 }, { 2860, 10086 }, { 2861, 10086 }, { 2862, 10086 } } },
            { name = "middleToEast", dest = { 2860, 10082 }, ahead = 1, seg = { { 2861, 10086 }, { 2861, 10085 }, { 2861, 10084 }, { 2861, 10083 },
                { 2861, 10082 }, { 2860, 10082 }, { 2862, 10085 } } },
            { name = "eastToColumn", dest = { 2861, 10076 }, ahead = 0, seg = { { 2861, 10082 }, { 2861, 10081 }, { 2861, 10080 }, { 2861, 10079 },
                { 2861, 10078 }, { 2861, 10077 }, { 2861, 10076 } } },
            { name = "columnToSouth", dest = { 2860, 10075 }, ahead = 0, seg = { { 2860, 10076 }, { 2860, 10075 } } },
        }
        local catches = {} -- "<attempt>:<hop>" for every guard catch on the way in
        local function hop(h, attempt)
            local why = "no window"
            for w = 1, 150 do
                local idx, pos = sync_guards()
                local ok, reason = hop_clear(h.seg, h.ahead, idx, pos)
                why = reason
                if ok then
                    t.player.walk_to(h.dest[1], h.dest[2], 20)
                    t.ticks(3) -- a catch on the way lands its knockout teleport within three ticks
                    local r, tt = t.world.tile()
                    local there = r == "ok" and tt.level == 0 and tt.x == h.dest[1] and tt.z == h.dest[2]
                    if not there and r == "ok" and in_storeroom(tt) then
                        catches[#catches + 1] = attempt .. ":" .. h.name
                    end
                    t.check("crateRoom." .. h.name .. attempt, there or (r == "ok" and in_storeroom(tt)),
                        "after " .. w .. " tick(s) of waiting the guards' loops cleared the hop; walked to " .. h.dest[1] .. "," .. h.dest[2]
                            .. " -> " .. tile_text(r, tt) .. " (want there, or back in the storeroom if a guard caught it anyway)")
                    return there
                end
            end
            t.check("crateRoom." .. h.name .. attempt, false, "no clear window in 150 ticks: " .. tostring(why))
            return false
        end

        -- Walk, do not run, through the crate room: the hops are timed at a
        -- walk (b58 probe 5: the run had run out and the player walked one
        -- tile a tick into troll_sguard3). varp173_option_run is the run
        -- orb's own toggle (interface_orbs/scripts/orbs.rs2:82-87).
        local run_r, run_v = t.var.varp("varp173_option_run")
        if run_r == "ok" and run_v == 1 then
            local wr, wid = t.ui.widget("orbs:runbutton")
            if wr == "ok" then
                t.ui.invoke(wid, 1)
            end
            t.ticks(2)
        end
        local run_r2, run_v2 = t.var.varp("varp173_option_run")
        t.check("crateRoom.walking", run_r2 == "ok" and run_v2 == 0,
            "varp173_option_run " .. tostring(run_r) .. " " .. tostring(run_v) .. " -> " .. tostring(run_r2) .. " " .. tostring(run_v2)
                .. " (want 0: the run orb off, so the hops below move one tile a tick)")

        local goutweed_got = false
        local crate_detail = "never reached the crate"
        for attempt = 1, 4 do
            local sr, st = t.world.tile()
            if not (sr == "ok" and in_storeroom(st)) then
                break
            end
            -- The interior door (2861,10092, its east edge): open it from
            -- the storeroom side, or see it standing open from the last try.
            walk_check("crateRoomDoor" .. attempt .. ".atDoor", 2862, 10092, 0, 20, 0)
            local dr, dd = t.player.click_loc("troll_stronghold_interior_door", 1, { at = { 2861, 10092, 0 } })
            if dr ~= "no_row" then
                t.ticks(1)
            end
            local orr, od = t.world.loc_near("troll_stronghold_interior_door_open", 3)
            t.check("crateRoomDoor" .. attempt .. ".open", orr == "ok" and od.level == 0 and math.abs(od.tile_x - 2861) <= 1 and math.abs(od.tile_z - 10092) <= 1,
                "click_loc troll_stronghold_interior_door op1 at 2861,10092,0 -> " .. tostring(dr) .. " " .. tostring(dd)
                    .. "; troll_stronghold_interior_door_open: " .. (orr == "ok" and (od.tile_x .. "," .. od.tile_z .. "," .. tostring(od.level)) or tostring(orr))
                    .. " (want within 1 of 2861,10092,0; no_row = still open from the last try)")
            local reached = true
            for _, h in ipairs(HOPS) do
                if not hop(h, attempt) then
                    reached = false
                    break
                end
            end
            if reached then
                -- 2860,10074, 2859,10074 and 2858,10074 are outside every
                -- patrol: walked in two straight legs so no corner is cut
                -- through 2859,10075 (troll_sguard8's look-ahead).
                t.player.walk_to(2860, 10074, 5)
                walk_check("crateRoom.besideCrate" .. attempt, 2858, 10074, 0, 10, 0)
                local cr, cd = t.player.click_loc("eadgar_crate_goutweed", 1, { at = { 2857, 10074, 0 } })
                local gr, gd = t.inv.await("eadgar_goutweed_herb", 1, 10)
                crate_detail = "attempt " .. attempt .. ": click_loc(eadgar_crate_goutweed at 2857,10074,0) -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; inv.await(eadgar_goutweed_herb) -> " .. tostring(gr) .. " " .. tostring(gd)
                if gr == "ok" then
                    goutweed_got = true
                    break
                end
            end
            -- Caught: the knockout drops the player in the storeroom; the
            -- next attempt starts there.
            t.ui.await_close("fade_overlay", 12)
            t.ticks(2)
        end
        t.check("searchCrate", goutweed_got, crate_detail .. "; guard catches on the way in: " .. #catches
            .. (#catches > 0 and (" (" .. table.concat(catches, ", ") .. ")") or ""))
        t.chat.close() -- dismiss the "You've found some goutweed!" objbox (~objbox, eadgar_troll_chief_cook.rs2:218)

        -- The crate's guard check opens fade_overlay (troll_guard_knockout,
        -- delay 2), p_teleports one tick later, then closes the fade; the
        -- open is awaited first so the close proves the cycle ran.
        local fade_open_result, fade_open_detail = t.ui.await_open("fade_overlay", 10)
        t.check("crate.knockoutFade", fade_open_result == "ok",
            "ui.await_open(fade_overlay) -> " .. tostring(fade_open_result) .. " " .. tostring(fade_open_detail)
                .. " -- the crate's own posted guard (eadgar_storeroom_guard) always catches this search")
        t.ui.await_close("fade_overlay", 10)
        local ejected_result, ejected_tile = t.world.tile()
        t.check("crate.ejected", ejected_result == "ok" and ejected_tile.x == 2865 and ejected_tile.z == 10088 and ejected_tile.level == 0,
            "world.tile() after fade_overlay close -> " .. tile_text(ejected_result, ejected_tile)
                .. " (want 2865,10088,0: [queue,troll_guard_teleport] p_teleport(0_44_157_49_40), inside the storeroom)")
        t.expect("player.alive_after_ejection", t.player.alive())
        margin_row("storeroom.margin", "the kitchen, the storeroom and the crate room, with the guards' knockouts")
        inv_row("getGoutweed.item", { { "eadgar_goutweed_herb", 1 } }, "the goutweed survives the knockout")

        -- Out of the storeroom by its door, up the storeroom stairs
        -- (2852,10061,0 east -> 2852,10060,1), up the south stairs, out the
        -- top exit.
        -- From the door tile (0_44_157_53_37, inside) the door always opens
        -- and [proc,eadgar_storeroomdoor_pass] steps the player across its
        -- south edge to 2869,10084; no key is used (it is gone). The press
        -- is made from 2869,10086 (click_loc steps off a loc's own tile
        -- first, run r2.1): the player walks onto the door tile and the
        -- script takes it from there.
        walk_check("leaveStoreroom.atDoor", 2869, 10086, 0, 20, 0)
        cross("leaveStoreroomDoor", "eadgar_storeroomdoor", 2869, 10085, 0,
            function(tt) return tt.level == 0 and tt.x == 2869 and tt.z == 10084 end,
            "2869,10084,0: the corridor tile south of the door")
        walk_check("leaveStoreroom.corridor", 2869, 10083, 0, 10, 0)
        walk_check("walk-storeroomStairsUp", 2852, 10064, 0, 60, 0)
        climb("goUpFromStoreroom", "troll_stronghold_stairs", 2852, 10061, 0, 2852, 10060, 1)
        kitchen_to_summit("toSanfew", "goUpToTopFloorToSanfew", "exitStrongholdToSanfew")

        -- ---------------------------------------------------------------
        -- 17. Hand the goutweed to Sanfew -> quest complete
        --     (areas/area_taverly/scripts/sanfew.rs2 sanfew_eadgar_turnin).
        --     Reward: 11000 Herblore XP and 1 quest point,
        --     ~quest_complete_rewards(quest_eadgarsruse, ...), sanfew.rs2:190.
        -- ---------------------------------------------------------------
        local _, herblore_snap = t.skill.snapshot() -- read only; reward.herblore_xp below is the assertion

        local qp_before_result, qp_before = t.var.varp("varp101_qp")
        teleport_down("teleportCamelotToSanfew")
        t.exec("goto-sanfew-2", t.player.goto_tile, 2890, 3428, 0)
        sanfew_in("turnin", "returnUpToSanfew")
        t.exec("talkToSanfew-turnin", t.player.talk_to, "sanfew", 1)
        t.exec("talkToSanfew-turnin-dialog", t.chat.play, {
            "npc:What can I do for you young 'un",
            "choose:Have you any more work for me, to help reclaim the circle?",
            "player:Have you any more work for me to help reclaim the stone circle?",
            "npc:Did you find some goutweed for me?",
            "player:I have some goutweed!",
            "npc:Excellent! I will be able to complete the next part of the ritual now.",
            "npc:If you ever come across more goutweed, bring it to me",
        })
        t.chat.close()
        t.ticks(3) -- completion's own reward scroll mounts asynchronously (section 8)

        t.quest.expect_complete()
        t.exec("reward.herblore_xp", t.skill.expect_gain, "herblore", 11000, herblore_snap)
        local qp_after_result, qp_after = t.var.varp("varp101_qp")
        t.check("reward.questpoint", type(qp_before) == "number" and type(qp_after) == "number" and qp_after - qp_before == 1,
            "quest points " .. tostring(qp_before_result) .. " " .. tostring(qp_before) .. " -> " .. tostring(qp_after_result) .. " "
                .. tostring(qp_after) .. " (the scroll shows 1 Quest Point)")
        inv_row("returnToSanfew.goutweed", { { "eadgar_goutweed_herb", 0 } }, "the goutweed went to Sanfew")
        sanfew_out("turnin", "leaveSanfewTurnin")
        t.finish(0)
        return
    end,
}
