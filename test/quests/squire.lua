-- The Knight's Sword: Squire (Falador courtyard) accepts, sends the player
-- to Reldo (Varrock library) for the Imcando dwarf lead, then to Thurgo
-- (south of Port Sarim) with a redberry pie. Thurgo agrees to smith a
-- replacement sword but needs to see the design first -- a portrait kept
-- in Sir Vyvin's cupboard, upper floor of Falador Castle. Thurgo then
-- needs two iron bars (brought along) and a blurite ore, mined in the
-- eastern cavern of the Asgarnian Ice Dungeon. Handing the finished sword
-- to the Squire completes the quest.
--
-- Every gather step below is driven through real clicks against the
-- quest's own scripts (OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_squire/scripts/quest_squire.rs2, areas/falador/scripts/
-- squire.rs2, areas/varrock/scripts/reldo.rs2, areas/port_sarim/scripts/
-- thurgo.rs2) -- redberry pie, both iron bars and a pickaxe are setup
-- ::give's (Quest Helper's own getItemRequirements() -- a tool and two
-- brought-along items, never the quest's own deliverable).
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md, owner 2026-10-03/05): every goto
-- departs from and lands on an open, walkable tile OUTSIDE; every door,
-- ladder, stair and trapdoor between the player and a target is clicked,
-- going in and coming out. The places, from the map squares (m46_52.jl2,
-- m50_54.jl2, m47_49.jl2, m47_149.jl2) and maplink.dbrow:
--   * The run's first goto: the fixture's tile 3206,3233,0 (north of
--     Lumbridge castle, open) to the Falador castle courtyard 2977,3342,0
--     (reach.py: closed-doors walk, len 440).
--   * Varrock palace: open arch 3212-3213,3471 from the street 3212,3466,
--     then fai_varrock_castle_door 3215,3477 (south edge), 3214,3486 (north
--     edge) and 3210,3490 (south edge) into the library (Reldo spawns at
--     3209,3495). The goto lands on the street, never on the bookcase.
--   * Falador castle, east wing (the guide's goUpCastle1/2): the castle
--     double door fai_falador_castledoubledoorl 2981,3341 (east edge), then
--     fai_falador_poor_castle_door 2985,3341 (east edge) and 2991,3341 (west
--     edge), the east ladder fai_falador_castle_ladder_up 2994,3341
--     (forceapproach: from the west, 2993,3341; no maplink row, so +1 plane
--     on that tile), on level 1 the same door 2991,3341, the staircase
--     fai_falador_castle_stairs 2984,3337 (maplink_1_46_52_40_8_up:
--     2984,3336,1 -> 2984,3340,2), and on level 2 Sir Vyvin's door
--     fai_falador_poor_castle_door 2982,3337 (south edge) into his room
--     (x 2981-2986 z 3334-3336). The cupboard (2984-2985,3336, forceapproach
--     from the south) is searched from 2985,3335.
--   * Thurgo (3001,3144) stands in the open south of Port Sarim: no door
--     (comp.py: no door on its component's edge; reach.py closed-doors from
--     the Port Sarim docks and from the courtyard).
--   * Asgarnian Ice Dungeon: fai_trapdoor 3008,3150 (forceapproach from the
--     east; maplink_0_47_49_1_14_down: 3009,3150,0 -> 3009,9550,0, the
--     underground frame), then one dungeon passage (reach.py closed-doors
--     len 127) to the blurite rock 3049,9566; the goto lands on 3049,9567
--     beside it, never on it.
--   * Out of the dungeon: back along the same passage to the ladder's foot
--     3009,9550 (reach.py closed-doors len 127), then the ladder
--     ladder_from_cellar 3008,9550 (raw level 1) is climbed. It has no
--     maplink.dbrow row; since seam matthew-mbp-m4-b66-seam1 ladders.rs2
--     [oploc1,ladder_from_cellar] climbs a rowless copy out of the dungeon by
--     LostCity's loc_1755 rule (movecoord(coord, 0, 0, -6400), LostCity_Content2
--     ladders+stairs/scripts/ladders.rs2:87-94): 3009,9550,0 -> 3009,3150,0,
--     beside the trapdoor on open ground. Then a short walk on foot to Thurgo
--     (reach.py 3009,3150 -> 3001,3144: closed-doors len 46). No teleport, so
--     no Magic or runes are staged: the player keeps the fixture's combat
--     level, and no dialogue on the route branches on it anyway.
return {
    id = "squire",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give redberry_pie 1",
        "::give iron_bar 2",
        "::give bronze_pickaxe 1",
        "::setlevel mining 10",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp122_squire",
            constants = {
                complete = 7,
                given_pie = 3,
                looking_blurite = 6,
                looking_portrait = 5,
                not_started = 0,
                questpoints = 1,
                spoken_reldo = 2,
                spoken_thurgo = 4,
                started = 1,
            },
            row = "quest_theknightssword",
            display = "The Knight's Sword",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local VARROCK_DOOR, VARROCK_DOOR_OPEN = "fai_varrock_castle_door", "fai_varrock_castle_door_open"
        local FALADOR_DOUBLE, FALADOR_DOUBLE_OPEN = "fai_falador_castledoubledoorl", "fai_falador_opencastledoubledoorl"
        local FALADOR_DOOR, FALADOR_DOOR_OPEN = "fai_falador_poor_castle_door", "fai_falador_poor_castle_door_open"

        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        -- A walk on one floor, graded on the exact tile it reached.
        local function walk_check(name, x, z, level, why, ticks)
            local wr, wd = t.player.walk_to(x, z, ticks or 40)
            local tr, tt = t.world.tile()
            t.check(name, tr == "ok" and type(tt) == "table" and tt.x == x and tt.z == z and tt.level == level,
                "walk_to " .. x .. "," .. z .. " (" .. why .. ") -> " .. tostring(wr) .. " " .. tostring(wd)
                    .. "; at " .. tile_text(tr, tt) .. " (want " .. x .. "," .. z .. "," .. level .. ")")
        end

        -- ------------------------------------------- Squire: accept the quest
        t.exec("goto.squire", t.player.goto_tile, 2977, 3342, 0)
        t.exec("squire.talk", t.player.talk_to, "squire", 1)

        local d1r, d1d = t.chat.drain({ stop_at = "options" })
        t.expect("squire.menu1", d1r, d1d)
        t.exec("squire.choose1", t.chat.choose, "And how is life as a squire?")

        local d2r, d2d = t.chat.drain({ stop_at = "options" })
        t.expect("squire.menu2", d2r, d2d)
        t.exec("squire.choose2", t.chat.choose, "I can make a new sword if you like...")

        local d3r, d3d = t.chat.drain({ stop_at = "options" })
        t.expect("squire.menu3", d3r, d3d)
        t.exec("squire.choose3", t.chat.choose, "So would these dwarves make another one?")

        local d4r, d4d = t.chat.drain({ stop_at = "options" })
        t.expect("squire.menu4", d4r, d4d)
        t.exec("squire.accept", t.chat.choose, "Ok, I'll give it a go.")
        t.chat.close()

        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ------------------------------- Reldo (Varrock palace library)
        -- From the open courtyard to the street south of the palace's open
        -- arch, then the three palace doors on foot.
        t.exec("goto.reldo", t.player.goto_tile, 3212, 3466, 0)
        t.exec("talkToReldo.palaceDoorIn1", t.player.pass_door, { closed = VARROCK_DOOR, open = VARROCK_DOOR_OPEN,
            at = { 3215, 3477, 0 }, near = { 3215, 3476 }, far = { 3215, 3478 } })
        t.exec("talkToReldo.palaceDoorIn2", t.player.pass_door, { closed = VARROCK_DOOR, open = VARROCK_DOOR_OPEN,
            at = { 3214, 3486, 0 }, near = { 3214, 3486 }, far = { 3214, 3487 } })
        t.exec("talkToReldo.libraryDoorIn", t.player.pass_door, { closed = VARROCK_DOOR, open = VARROCK_DOOR_OPEN,
            at = { 3210, 3490, 0 }, near = { 3210, 3489 }, far = { 3210, 3491 } })
        t.exec("reldo.talk", t.player.talk_to, "reldo_normal", 1)

        local rdr, rdd = t.chat.drain({ stop_at = "options" })
        t.expect("reldo.menu", rdr, rdd)
        t.exec("reldo.choose", t.chat.choose, "What do you know about the Imcando dwarves?")

        local rd2r, rd2d = t.chat.drain({ stop_at = "none" })
        t.expect("reldo.done", rd2r, rd2d)
        t.expect("quest.stage.spoken_reldo", t.quest.expect_stage("spoken_reldo"))

        -- Out of the palace the way in, door by door, to the street.
        t.exec("talkToReldo.libraryDoorOut", t.player.pass_door, { closed = VARROCK_DOOR, open = VARROCK_DOOR_OPEN,
            at = { 3210, 3490, 0 }, near = { 3210, 3491 }, far = { 3210, 3489 } })
        t.exec("talkToReldo.palaceDoorOut2", t.player.pass_door, { closed = VARROCK_DOOR, open = VARROCK_DOOR_OPEN,
            at = { 3214, 3486, 0 }, near = { 3214, 3487 }, far = { 3214, 3486 } })
        t.exec("talkToReldo.palaceDoorOut1", t.player.pass_door, { closed = VARROCK_DOOR, open = VARROCK_DOOR_OPEN,
            at = { 3215, 3477, 0 }, near = { 3215, 3478 }, far = { 3215, 3476 } })
        walk_check("talkToReldo.street", 3212, 3466, 0, "the street south of the palace arch")

        -- ------------------------------- Thurgo (south of Port Sarim)
        t.exec("goto.thurgo1", t.player.goto_tile, 3001, 3144, 0)
        t.exec("thurgo.talk1", t.player.talk_to, "thurgo", 1)

        local tdr, tdd = t.chat.drain({ stop_at = "options" })
        t.expect("thurgo.menu1", tdr, tdd)
        t.exec("thurgo.pie", t.chat.choose, "Would you like a redberry pie?")

        local td2r, td2d = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo.pie.done", td2r, td2d)
        t.expect("quest.stage.given_pie", t.quest.expect_stage("given_pie"))
        local pie_r, pie_d = t.inv.expect_absent("redberry_pie")
        t.step("thurgo.pie.handed_over", pie_r == "ok" and "PASS" or "FAIL",
            "redberry_pie after giving it to Thurgo -> " .. tostring(pie_r) .. " " .. tostring(pie_d))

        -- Thurgo, second talk: he agrees to try, but wants a picture of
        -- the sword's design first.
        t.exec("thurgo.talk2", t.player.talk_to, "thurgo", 1)
        local td3r, td3d = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo.sword.done", td3r, td3d)
        t.expect("quest.stage.spoken_thurgo", t.quest.expect_stage("spoken_thurgo"))

        -- Squire, status check: he mentions Sir Vyvin's cupboard.
        t.exec("goto.squire2", t.player.goto_tile, 2977, 3342, 0)
        t.exec("squire.talk2", t.player.talk_to, "squire", 1)
        local sq2r, sq2d = t.chat.drain({ stop_at = "none" })
        t.expect("squire.status1.done", sq2r, sq2d)
        t.expect("quest.stage.looking_portrait", t.quest.expect_stage("looking_portrait"))

        -- ------------------- Sir Vyvin's cupboard (Falador castle, level 2)
        -- Into the east wing on foot, up the east ladder (goUpCastle1) and
        -- the staircase west of it (goUpCastle2), into Sir Vyvin's room.
        t.exec("goUpCastle1.castleDoorIn", t.player.pass_door, { closed = FALADOR_DOUBLE, open = FALADOR_DOUBLE_OPEN,
            at = { 2981, 3341, 0 }, near = { 2981, 3341 }, far = { 2982, 3341 } })
        t.exec("goUpCastle1.hallDoorIn", t.player.pass_door, { closed = FALADOR_DOOR, open = FALADOR_DOOR_OPEN,
            at = { 2985, 3341, 0 }, near = { 2985, 3341 }, far = { 2986, 3341 } })
        t.exec("goUpCastle1.ladderRoomDoorIn", t.player.pass_door, { closed = FALADOR_DOOR, open = FALADOR_DOOR_OPEN,
            at = { 2991, 3341, 0 }, near = { 2990, 3341 }, far = { 2991, 3341 } })
        t.exec("goUpCastle1", t.player.climb, { loc = "fai_falador_castle_ladder_up", op = 1, op_name = "Climb-up",
            at = { 2994, 3341, 0 }, src = { 2993, 3341 }, dest = { 2993, 3341, 1 } })
        t.exec("goUpCastle2.ladderRoomDoorOut", t.player.pass_door, { closed = FALADOR_DOOR, open = FALADOR_DOOR_OPEN,
            at = { 2991, 3341, 1 }, near = { 2991, 3341 }, far = { 2990, 3341 } })
        t.exec("goUpCastle2", t.player.climb, { loc = "fai_falador_castle_stairs", op = 1, op_name = "Climb-up",
            at = { 2984, 3337, 1 }, src = { 2984, 3336 }, dest = { 2984, 3340, 2 } })
        t.exec("searchCupboard.vyvinDoorIn", t.player.pass_door, { closed = FALADOR_DOOR, open = FALADOR_DOOR_OPEN,
            at = { 2982, 3337, 2 }, near = { 2982, 3337 }, far = { 2982, 3336 } })

        -- The cupboard is a shut/open PAIR (trap 20), not a multiloc, so
        -- the open half needs its own symbol and click, and each mesbox is
        -- drained before the next click.
        walk_check("searchCupboard.stand", 2985, 3335, 2, "south of the cupboard, its only approach side")
        t.exec("cupboard.open", t.player.click_loc, "vyvincupboardshut", 1)
        local cor, cod = t.chat.drain({ stop_at = "none" })
        t.expect("cupboard.open.msg", cor, cod)
        t.ticks(3)  -- let the loc_change (shut -> open) reach the client's entity pool

        -- quest_squire.rs2 [proc,vyvin_distracted]: npc_find(coord,
        -- sir_vyvin, 1, 0) -- Sir Vyvin within 1 tile of the player catches
        -- the search ("HEY! ... STAY OUT of MY cupboard!") and gives
        -- nothing (the guide: "You'll need Sir Vyvin to be in the other
        -- room"). He wanders his room and the landing (moverestrict
        -- indoors), so wait until he is at least 3 tiles off before each
        -- search; each attempt is a note, the portrait is the row.
        local function vyvin_away(stand_x, stand_z, ticks)
            local last = "never read"
            for i = 0, ticks do
                local vr, row = t.npc.nearest("sir_vyvin", 15)
                if vr == "ok" and type(row) == "table" then
                    local d = math.max(math.abs(row.x - stand_x), math.abs(row.z - stand_z))
                    last = "sir_vyvin at " .. tostring(row.x) .. "," .. tostring(row.z) .. " (distance " .. d .. ")"
                    if d >= 3 then
                        return true, last .. " after " .. i .. " tick(s)"
                    end
                else
                    last = "npc.nearest(sir_vyvin) -> " .. tostring(vr) .. " " .. tostring(row)
                end
                t.ticks(1)
            end
            return false, last .. " after " .. ticks .. " tick(s)"
        end

        local portrait_n, attempts_log = 0, {}
        for attempt = 1, 5 do
            local sr, st = t.world.tile()
            if not (sr == "ok" and st.x == 2985 and st.z == 3335 and st.level == 2) then
                t.player.walk_to(2985, 3335, 10)
            end
            local away, away_d = vyvin_away(2985, 3335, 80)
            local cr, cd = t.player.click_loc("vyvincupboardopen", 1)
            local dr, dd = t.chat.drain({ stop_at = "none" })
            t.ticks(2)  -- let the inv_add reach the client's own container read
            local nr, n = t.inv.count("knights_portrait")
            portrait_n = (nr == "ok" and n) or 0
            local line = "attempt " .. attempt .. ": " .. away_d .. (away and "" or " (still close)")
                .. "; search -> " .. tostring(cr) .. " " .. tostring(cd) .. "; chat " .. tostring(dr) .. " " .. tostring(dd)
                .. "; knights_portrait " .. tostring(n)
            attempts_log[#attempts_log + 1] = line
            t.note(line)
            if portrait_n >= 1 then
                break
            end
        end
        t.check("searchCupboard", portrait_n == 1,
            "knights_portrait " .. tostring(portrait_n) .. " after " .. #attempts_log .. " search(es)")

        -- Out the way in: Vyvin's door, the stairs, the level-1 door, the
        -- ladder, the two hall doors and the castle door, to the courtyard.
        t.exec("leaveCastle.vyvinDoorOut", t.player.pass_door, { closed = FALADOR_DOOR, open = FALADOR_DOOR_OPEN,
            at = { 2982, 3337, 2 }, near = { 2982, 3336 }, far = { 2982, 3337 } })
        t.exec("leaveCastle.stairsDown", t.player.climb, { loc = "fai_falador_castle_stairstop", op = 1, op_name = "Climb-down",
            at = { 2984, 3338, 2 }, src = { 2984, 3340 }, dest = { 2984, 3336, 1 } })
        t.exec("leaveCastle.ladderRoomDoorIn", t.player.pass_door, { closed = FALADOR_DOOR, open = FALADOR_DOOR_OPEN,
            at = { 2991, 3341, 1 }, near = { 2990, 3341 }, far = { 2991, 3341 } })
        t.exec("leaveCastle.ladderDown", t.player.climb, { loc = "fai_falador_castle_laddertop", op = 1, op_name = "Climb-down",
            at = { 2994, 3341, 1 }, src = { 2993, 3341 }, dest = { 2993, 3341, 0 } })
        t.exec("leaveCastle.ladderRoomDoorOut", t.player.pass_door, { closed = FALADOR_DOOR, open = FALADOR_DOOR_OPEN,
            at = { 2991, 3341, 0 }, near = { 2991, 3341 }, far = { 2990, 3341 } })
        t.exec("leaveCastle.hallDoorOut", t.player.pass_door, { closed = FALADOR_DOOR, open = FALADOR_DOOR_OPEN,
            at = { 2985, 3341, 0 }, near = { 2986, 3341 }, far = { 2985, 3341 } })
        t.exec("leaveCastle.castleDoorOut", t.player.pass_door, { closed = FALADOR_DOUBLE, open = FALADOR_DOUBLE_OPEN,
            at = { 2981, 3341, 0 }, near = { 2982, 3341 }, far = { 2981, 3341 } })
        walk_check("leaveCastle.courtyard", 2977, 3342, 0, "the open courtyard")

        -- Thurgo, third talk: hand over the portrait; he asks for the
        -- smithing materials.
        t.exec("goto.thurgo2", t.player.goto_tile, 3001, 3144, 0)
        t.exec("thurgo.portrait", t.player.talk_to, "thurgo", 1)
        local td4r, td4d = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo.portrait.done", td4r, td4d)
        t.expect("quest.stage.looking_blurite", t.quest.expect_stage("looking_blurite"))
        t.ticks(2)  -- let the inv_del reach the client's own container read

        local port2_r, port2_d = t.inv.expect_absent("knights_portrait")
        t.step("inv.portrait.handed_over", port2_r == "ok" and "PASS" or "FAIL",
            "knights_portrait after handing it to Thurgo -> " .. tostring(port2_r) .. " " .. tostring(port2_d))

        -- ------------------------------- Asgarnian Ice Dungeon: blurite
        -- Walk from Thurgo to the trapdoor's east side (its only approach,
        -- the maplink row's src) and climb down into the underground frame.
        walk_check("enterDungeon.walk", 3009, 3150, 0, "east of the trapdoor", 80)
        t.exec("enterDungeon", t.player.climb, { loc = "fai_trapdoor", op = 1, op_name = "Climb-down",
            at = { 3008, 3150, 0 }, src = { 3009, 3150 }, dest = { 3009, 9550, 0 } })

        -- One dungeon passage from the ladder's foot to the eastern cavern
        -- (reach.py closed-doors len 127): the goto lands beside the rock.
        local hp0_r, hp0 = t.skill.read("hitpoints")
        t.exec("goto.mine", t.player.goto_tile, 3049, 9567, 0)
        local ore_before_r, ore_before = t.inv.count("blurite_ore")
        t.exec("mine.blurite", t.player.click_loc, "blurite_rock_1", 1)
        -- Keep swinging until the ore lands. Each swing is a roll on the
        -- player's own stream (seam28), and level 10 on a level-10 rock
        -- misses most swings.
        local ore_await_r = t.inv.await("blurite_ore", 1, 300)
        local ore_after_r, ore_after = t.inv.count("blurite_ore")
        t.check("mineBlurite", ore_await_r == "ok" and ore_before_r == "ok" and ore_before == 0
                and ore_after_r == "ok" and ore_after == 1,
            "blurite_ore " .. tostring(ore_before) .. " -> " .. tostring(ore_after)
                .. " (await=" .. tostring(ore_await_r) .. ")")
        local hp1_r, hp1 = t.skill.read("hitpoints")
        t.note("hitpoints before the dungeon walk " .. tostring(hp0_r == "ok" and hp0.level or hp0_r)
            .. ", after mining " .. tostring(hp1_r == "ok" and hp1.level or hp1_r))

        -- Out by the dungeon's own ladder: back along the same passage to its
        -- foot (travel between two open tiles of one passage, reach.py
        -- closed-doors len 127), then ladder_from_cellar 3008,9550 (raw level
        -- 1; no maplink row, so LostCity's loc_1755 rule, -6400 from the
        -- player's tile) up to 3009,3150,0 beside the trapdoor.
        t.exec("goto.leaveDungeon", t.player.goto_tile, 3009, 9550, 0)
        t.exec("leaveDungeon.ladderUp", t.player.climb, { loc = "ladder_from_cellar", op = 1, op_name = "Climb-up",
            at = { 3008, 9550, 0 }, loc_level = 1, src = { 3009, 9550 }, dest = { 3009, 3150, 0 } })

        -- Thurgo, fourth talk: walk on foot from the trapdoor to him (open
        -- ground, reach.py closed-doors len 46), hand over the blurite ore
        -- and the two iron bars; he forges the sword.
        walk_check("thurgo3.walk", 3001, 3144, 0, "Thurgo's open ground south of Port Sarim", 40)
        t.exec("thurgo.ore", t.player.talk_to, "thurgo", 1)
        local td5r, td5d = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo.ore.done", td5r, td5d)
        t.ticks(2)  -- let the inv_add(faladian_sword) reach the client's own container read

        local sword_r, sword_d = t.inv.expect_has("faladian_sword", 1)
        t.step("inv.sword", sword_r == "ok" and "PASS" or "FAIL",
            "faladian_sword after Thurgo forges it -> " .. tostring(sword_r) .. " " .. tostring(sword_d))
        local ore_gone_r, ore_gone_d = t.inv.expect_absent("blurite_ore")
        t.step("inv.ore.handed_over", ore_gone_r == "ok" and "PASS" or "FAIL",
            "blurite_ore after Thurgo forges the sword -> " .. tostring(ore_gone_r) .. " " .. tostring(ore_gone_d))
        local bars_gone_r, bars_gone_d = t.inv.expect_absent("iron_bar")
        t.step("inv.bars.handed_over", bars_gone_r == "ok" and "PASS" or "FAIL",
            "iron_bar after Thurgo forges the sword -> " .. tostring(bars_gone_r) .. " " .. tostring(bars_gone_d))

        -- Squire, final hand-in: give him the finished sword.
        local snap_r, snap_before = t.skill.snapshot()
        t.step("reward.snapshot", snap_r == "ok" and "PASS" or "FAIL", "skill.snapshot -> " .. tostring(snap_r))

        t.exec("goto.squire3", t.player.goto_tile, 2977, 3342, 0)
        t.ticks(2)  -- let the entity pool settle after the goto before hunting for a menu row
        t.exec("squire.final", t.player.talk_to, "squire", 1)
        local sq3r, sq3d = t.chat.drain({ stop_at = "none" })
        t.expect("squire.final.done", sq3r, sq3d)

        t.ticks(3)  -- completion is queued ([queue,squire_complete]), not immediate

        -- The literal reward of quest_squire.rs2 [queue,squire_complete]:
        -- stat_advance(smithing, 127250) -> 12725 Smithing XP; 1 quest point
        -- (expect_complete's quest.points row, delta 1).
        t.quest.expect_complete()
        t.check("reward.smithing", t.skill.expect_gain("smithing", 12725, snap_before))
        local sword2_r, sword2_d = t.inv.expect_absent("faladian_sword")
        t.step("reward.sword_handed_over", sword2_r == "ok" and "PASS" or "FAIL",
            "faladian_sword after the hand-in -> " .. tostring(sword2_r) .. " " .. tostring(sword2_d))

        t.finish(0)
    end,
}
