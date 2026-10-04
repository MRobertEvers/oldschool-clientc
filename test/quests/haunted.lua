-- Ernest the Chicken (quest_haunted). Drives Veronica's opening scene to
-- start the quest, walks into Draynor Manor and up to Professor
-- Oddenstein's top-floor room, and plays the "I'm looking for a guy called
-- Ernest" branch through to its end -- content's own oddenstein_looking
-- label (areas/draynor/scripts/professor_oddenstein.rs2) sets
-- %haunted = ^haunted_spoken_to_oddenstein there, with no lever puzzle
-- involved at all.
--
-- From spoken_to_oddenstein on, the three missing parts he asks for
-- (pressure_gauge, oil_can, rubber_tube) are each a REAL content
-- interaction: a poisoned-fountain trick for the gauge, a spade/compost/
-- key/closet_door leg for the tube, and a bookcase/ladder/six-lever maze
-- for the can. Every one of them is driven with a real click, and so is
-- every door, stair, ladder and maze gate between them.
--
-- BLOCKED (content_bug, 2026-10-04): the basement maze cannot be walked.
-- Each of its nine gates is a shape-10 tile with a blankwall_no_blockrange
-- on its south/west neighbour's edge (OSRS-Content/osrs239-content/maps/
-- m48_152.jl2:3-11, :998-1006), so it can only be pressed from its north/
-- east side; the guide's solved order needs the 4to7 gate from the ladder
-- room's (south) side first. The run pulls levers A and B, is refused at
-- 4to7 ("I can't reach that!"), and ends on that BLOCKED row
-- (gate_from_walled_side below). Run 1 of the b58 fixer walked the rest:
-- the way out (8to9 refused the same way from the oil-can side, then
-- puzzle_ladder up, hauntedleverup, both staircases) all PASSed.
--
-- THE WHOLE MANOR IS WALKED (owner rule 2026-10-03, docs/QUEST_ORCHESTRATOR.md:
-- a goto into or out of any closed space is a cheat). goto_tile is used only
-- for the overland hops in the open grounds: to Veronica, to the front gate,
-- from the back door to the compost heap, to the fountain, back to the gate.
-- Rooms, doors and floors (maps/m48_52.jl2 and m48_152.jl2, static collision):
--   ground  Y entrance hall (x3106-3111 z3354-3357) | N stair hall (stairs up
--           at 3108,3362; closet_door 3107,3367 to the tube room) | F ring
--           corridor | T bookcase room | R secret room | H | Q poison room |
--           V kitchen (spade) with the back door to the grounds.
--           Doors: D1 3109,3358 Y|N, D2 3106,3368 N|F, D3 3103,3364 T|F,
--           D4 3101,3371 H|F, D5 3099,3366 Q|H, D6 3120,3356 F|V, back door
--           hauntedbackdoor 3123,3361 V|grounds.
--   first   corridor (stairs landing 3108,3366; spiral up 3104,3362) | fish
--           food room via D7 3116,3361.
--   second  landing 3105,3364 | Oddenstein's room via D8 3108,3364.
-- The front doors only open from the south (quest_haunted.rs2:36-38 "The
-- doors won't open."), so the manor is LEFT through the kitchen and its back
-- door, and re-entered through the front doors (the guide's enterManorWithKey).
-- Stairs land on their maplink rows (ladders_stairs/configs/maplink.dbrow:
-- 10228-10263, 10186): 3108,3361,0 <-> 3108,3366,1; 3106,3362,1 -> 3105,3364,2;
-- 3105,3364,2 -> 3106,3363,1.
--
-- Hand-in (professor_oddenstein.rs2 oddenstein_items -> oddenstein_ernest_
-- thanks) is ONE continuous chain with zero player choices, from "Have you
-- found anything yet?" through Ernest's "Of course, of course." -- driven as
-- a SINGLE chat.play list.

return {
    id = "haunted",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- the whole manor, its three floors and the basement maze are walked
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so nothing crowds the backpack
        "::haunted", -- resets %haunted/%haunted_settle/fountain; p_teleports to 3110,3330 (Veronica, open grounds)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp32_haunted",
            constants = {
                not_started = 0,
                started = 1,
                spoken_to_oddenstein = 2,
                complete = 3,
                settle_none = 0,
                settle_handover = 1,
                settle_paid = 2,
                questpoints = 4,
            },
            row = "quest_ernestthechicken",
            display = "Ernest the Chicken",
            points = 4,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        -- Wait for a teleport the click queued (a walk-through door, a climb,
        -- a maze gate) to land.
        local function await_tile(pred, ticks, what)
            return t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and pred(tt)
                end,
                note = what .. ": waiting for the landing",
            }, ticks)
        end

        -- Cross one swinging door on foot. Walk to the near side; if THIS
        -- level's closed leaf stands on the door tile, press it by tile AND
        -- level. If not, an earlier press left it open (a door swings back
        -- after 500 ticks): assert the open leaf stands on this level within
        -- one tile of the door tile -- a row that fails when neither leaf is
        -- there -- and walk through without pressing it again. loc_near
        -- returns the nearest copy on ANY level: D6's near tile 3119,3356,0
        -- has the first floor's closed copy straight above it, so when the
        -- nearest closed copy is another level's and no open leaf is here,
        -- this level's copy is pressed by tile and level (no_row, a FAIL,
        -- if it is not there). Then walk through and check the far tile.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 60)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and nt.x == near_x and nt.z == near_z,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. "," .. door_z .. " -> " .. tile_text(nr, nt))
            local lvl = (nr == "ok") and nt.level or -1
            local cr, cd = t.world.loc_near(closed_sym, 2)
            local closed_here = cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z and cd.level == lvl
            if closed_here then
                t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z, lvl } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 2)
                local open_ok = orr == "ok" and od.level == lvl and not (od.tile_x == door_x and od.tile_z == door_z)
                    and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1
                local seen = closed_sym .. " at " .. door_x .. "," .. door_z .. "," .. lvl .. ": nearest closed copy "
                    .. (cr == "ok" and (cd.tile_x .. "," .. cd.tile_z .. "," .. cd.level) or tostring(cr))
                    .. "; " .. open_sym .. ": " .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr))
                if not open_ok and cr == "ok" and cd.level ~= lvl then
                    t.note(seen .. " -- the nearest closed copy is another floor's; this floor's is pressed by tile and level")
                    t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z, lvl } })
                    t.ticks(1)
                else
                    t.check(prefix .. ".doorStandsOpen", open_ok,
                        seen .. " (want the open leaf on this level within 1 of the door tile: an earlier press left it open, so it is walked through, not pressed again)")
                end
            end
            t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- A click that TELEPORTS the player (a stair or ladder, the closet's
        -- walk-through, a maze gate, the bookcase, the lever): optionally walk
        -- to the exact stand tile first (a maplink is keyed on the player's
        -- own tile; a maze gate sends you to the mirror of your tile), click
        -- the copy by tile and level, wait for the landing, and grade the row
        -- on the tiles before and after -- a short hop can answer `timeout
        -- settle_after_click` although it landed, so the click's answer is in
        -- the detail and the landing decides.
        local function hop(name, sym, at_x, at_z, at_level, stand_x, stand_z, want_x, want_z, want_level)
            if stand_x ~= nil then
                t.player.walk_to(stand_x, stand_z, 60)
            end
            local br, bt = t.world.tile()
            local stood = br == "ok" and (stand_x == nil or (bt.x == stand_x and bt.z == stand_z and bt.level == at_level))
            local cr, cd = t.player.click_loc(sym, 1, { at = { at_x, at_z, at_level } })
            await_tile(function(tt) return tt.x == want_x and tt.z == want_z and tt.level == want_level end, 12, name)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and stood
                    and not (bt.x == want_x and bt.z == want_z and bt.level == want_level)
                    and wr == "ok" and wt.x == want_x and wt.z == want_z and wt.level == want_level,
                "from " .. tile_text(br, bt) .. (stand_x ~= nil and (" (stand tile " .. stand_x .. "," .. stand_z .. "," .. at_level .. ")") or "")
                    .. " click_loc(" .. sym .. " at " .. at_x .. "," .. at_z .. "," .. at_level .. ") -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; landed " .. tile_text(wr, wt) .. " (want " .. want_x .. "," .. want_z .. "," .. want_level .. ")")
        end

        -- A basement lever: click it and read its own line back ("You pull
        -- lever <L> <dir>.", quest_haunted.rs2:313) -- every pull in the
        -- solved order prints a distinct line, so the line proves THIS pull.
        -- click_loc can answer on the walk's map flag before the pull: read
        -- the ring first, then wait for the line if it has not printed yet.
        local function lever(name, sym, letter, dir)
            local cr, cd = t.player.click_loc(sym, 1)
            local want = "You pull lever " .. letter .. " " .. dir .. "."
            local how = "msg.expect"
            local mr, md = t.msg.expect(want)
            if mr ~= "ok" then
                how = "msg.expect -> " .. tostring(mr) .. ", then msg.await(10)"
                mr, md = t.msg.await(want, 10)
            end
            t.check(name, cr == "ok" and mr == "ok",
                "click_loc(" .. sym .. ") -> " .. tostring(cr) .. " " .. tostring(cd) .. "; " .. how .. "('" .. want .. "') -> " .. tostring(mr) .. " " .. tostring(md))
        end

        -- A maze gate crossed from its SOUTH or WEST side. Every one of the
        -- nine gates is placed as a shape-10 scenery tile
        -- (OSRS-Content/osrs239-content/maps/m48_152.jl2:3-11) with a
        -- blankwall_no_blockrange wall on the edge of its south/west
        -- neighbour (m48_152.jl2:998-1006), so a press from that side is
        -- refused "I can't reach that!" (run 1: gate4to7 from 3108,9757 and
        -- from both other approach tiles). LostCity places the same doors as
        -- wall doors (shape 0) that open from either side (LostCity_Server/
        -- content/maps/m48_152.jm2:4783 `0 36 30: 144 0 3`, no blankwall at
        -- 0 36 29), and ~ernest_walk_through_door's own gate branch
        -- (quest_haunted.rs2:438-451) expects a press from either orthogonal
        -- side. The guide's solved order needs four such crossings (4to7,
        -- 5to8, 3to6 and 2to3/1to2 from the west, 8to9 back), so the maze
        -- cannot be walked: the first refused crossing ends the run on a
        -- content_bug BLOCKED row that carries the press's own answer. If the
        -- crossing lands, the row is graded like any other hop.
        local function gate_from_walled_side(name, sym, gate_x, gate_z, stand_x, stand_z, want_x, want_z)
            t.player.walk_to(stand_x, stand_z, 60)
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc(sym, 1, { at = { gate_x, gate_z, 0 } })
            await_tile(function(tt) return tt.x == want_x and tt.z == want_z and tt.level == 0 end, 12, name)
            local wr, wt = t.world.tile()
            local detail = "from " .. tile_text(br, bt) .. " (stand tile " .. stand_x .. "," .. stand_z .. ",0) click_loc(" .. sym
                .. " at " .. gate_x .. "," .. gate_z .. ",0) -> " .. tostring(cr) .. " " .. string.sub(tostring(cd), 1, 200)
                .. "; landed " .. tile_text(wr, wt) .. " (want " .. want_x .. "," .. want_z .. ",0)"
            if not (wr == "ok" and wt.x == want_x and wt.z == want_z and wt.level == 0) then
                t.blocked("content_bug: " .. name .. " -- the basement maze gates are one-sided: " .. sym .. " at " .. gate_x .. "," .. gate_z
                    .. " is placed shape 10 (OSRS-Content/osrs239-content/maps/m48_152.jl2:3-11) behind a blankwall_no_blockrange on its"
                    .. " south/west neighbour's edge (m48_152.jl2:998-1006), so it cannot be reached from this side; LostCity places it as a"
                    .. " shape-0 wall door crossable both ways (LostCity_Server/content/maps/m48_152.jm2:4783) and quest_haunted.rs2:438-451"
                    .. " expects a press from either side. " .. detail)
                return false
            end
            t.check(name, (cr == "ok" or cr == "timeout") and br == "ok" and bt.x == stand_x and bt.z == stand_z, detail)
            return true
        end

        local function count_of(item)
            local r, n = t.inv.count(item)
            return r == "ok" and n or -1, r
        end

        -- the doors, by side
        local PD, PDO = "draynor_panelled_door", "draynor_panelled_door_open"
        local function hall_door_in(pfx) -- D1, entrance hall -> stair hall
            pass_door(pfx, PD, PDO, 3109, 3358, 3109, 3357, 3109, 3359,
                function(tt) return tt.level == 0 and tt.z >= 3358 end, "the stair hall, z >= 3358, level 0")
        end
        local function stairs_up(pfx) -- ground -> first floor
            hop(pfx, "draynor_manor_stairs_up", 3108, 3362, 0, 3108, 3361, 3108, 3366, 1)
        end
        local function stairs_down(pfx) -- first floor -> ground
            hop(pfx, "draynor_manor_stairs_down", 3108, 3364, 1, 3108, 3366, 3108, 3361, 0)
        end
        local function spiral_up(pfx) -- first -> second floor
            hop(pfx, "draynor_spiralstairs", 3104, 3362, 1, 3106, 3362, 3105, 3364, 2)
        end
        local function spiral_down(pfx) -- second -> first floor
            hop(pfx, "sarim_spiralstairstop", 3105, 3363, 2, 3105, 3364, 3106, 3363, 1)
        end
        local function lab_in(pfx) -- D8, second-floor landing -> Oddenstein's room
            pass_door(pfx, PD, PDO, 3108, 3364, 3107, 3364, 3109, 3364,
                function(tt) return tt.level == 2 and tt.x >= 3108 end, "Oddenstein's room, x >= 3108, level 2")
        end
        local function lab_out(pfx)
            pass_door(pfx, PD, PDO, 3108, 3364, 3108, 3364, 3106, 3364,
                function(tt) return tt.level == 2 and tt.x <= 3107 end, "the second-floor landing, x <= 3107, level 2")
        end
        local function n_to_f(pfx) -- D2, stair hall -> north corridor
            pass_door(pfx, PD, PDO, 3106, 3368, 3106, 3368, 3106, 3370,
                function(tt) return tt.level == 0 and tt.z >= 3369 end, "the north corridor, z >= 3369, level 0")
        end
        local function f_to_n(pfx)
            pass_door(pfx, PD, PDO, 3106, 3368, 3106, 3369, 3106, 3367,
                function(tt) return tt.level == 0 and tt.z <= 3368 end, "the stair hall, z <= 3368, level 0")
        end
        local function f_to_t(pfx) -- D3, corridor -> bookcase room
            pass_door(pfx, PD, PDO, 3103, 3364, 3103, 3364, 3103, 3362,
                function(tt) return tt.level == 0 and tt.z <= 3363 end, "the bookcase room, z <= 3363, level 0")
        end
        local function t_to_f(pfx)
            pass_door(pfx, PD, PDO, 3103, 3364, 3103, 3363, 3103, 3365,
                function(tt) return tt.level == 0 and tt.z >= 3364 end, "the corridor, z >= 3364, level 0")
        end

        -- ---------------------------------------------------------- Veronica
        t.exec("goto-veronica", t.player.goto_tile, 3110, 3330, 0) -- first goto: the setup teleport's own tile, open grounds
        t.exec("talkToVeronica", t.player.talk_to, "veronica", 1)
        -- veronica.rs2 [opnpc1,veronica] @haunted_start: opens with the
        -- NPC's own line (chatnpc_anim), then a two-row choice menu, spelled
        -- verbatim from the .rs2.
        t.exec("veronica-accept", t.chat.play, {
            "npc:Can you please help me? I'm in",
            "choose:Aha, sounds like a quest. I'll help.",
            "player:Aha, sounds like a quest. I'll",
            "npc:Yes yes, I suppose it is a que",
            "npc:Seeing as we were a little los",
            "npc:That was an hour ago. That hou",
            "player:Ok, I'll see what I can do.",
            "npc:Thank you, thank you. I'm very",
        })
        t.ticks(2) -- the varp write (%haunted = ^haunted_started) settles a tick behind the closed dialogue
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- --------------------------------------------------- into the manor
        -- open_manor_entrance (quest_haunted.rs2:35-57) only opens from the
        -- south and p_teleports the player one tile north, into the hall.
        t.exec("goto-manor-gate", t.player.goto_tile, 3108, 3349, 0)
        hop("enterManor", "haunteddoorl", 3108, 3353, 0, 3108, 3352, 3108, 3354, 0)
        hall_door_in("hallDoorIn")

        -- --------------------------------------------------- to Oddenstein
        stairs_up("goToFirstFloor")
        spiral_up("goToSecondFloor")
        lab_in("labDoorIn")
        t.exec("talkToOddenstein", t.player.talk_to, "professor_oddenstein", 1)
        -- professor_oddenstein.rs2 [label,oddenstein_looking] (reached at
        -- %haunted == ^haunted_started) opens directly on a THREE-row choice
        -- menu -- no npc line before it -- spelled verbatim through to
        -- @oddenstein_change_back / @oddenstein_not_easy, which is the page
        -- that writes %haunted = ^haunted_spoken_to_oddenstein.
        t.exec("oddenstein-looking", t.chat.play, {
            "choose:I'm looking for a guy called Ernest.",
            "player:I'm looking for a guy called E",
            "npc:Ah Ernest, top notch bloke. He",
            "player:So you know where he is then?",
            "npc:He's that chicken over there.",
            "player:Ernest is a chicken...? Are yo",
            "npc:Oh, he isn't normally a chicke",
            "npc:It was originally going to be ",
            "choose:Change him back this instant!",
            "player:Change him back this instant!",
            "npc:Umm... It's not so easy...",
            "npc:My machine is broken, and the ",
            "player:Well I can look for them.",
            "npc:That would be a help. They'll ",
            "npc:I'm missing the pressure gauge",
        })
        t.ticks(2) -- same settle as above, before reading the varp back
        t.expect("quest.stage.spoken_to_oddenstein", t.quest.expect_stage("spoken_to_oddenstein"))
        lab_out("labDoorOut")
        spiral_down("spiralDown")

        -- --------------------------------------------- fish food (first floor)
        pass_door("fishRoomIn", PD, PDO, 3116, 3361, 3116, 3362, 3116, 3360,
            function(tt) return tt.level == 1 and tt.z <= 3361 end, "the fish food room, z <= 3361, level 1")
        local fish_result, fish_detail = t.player.click_obj("fish_food", 3)
        local fish_have = count_of("fish_food")
        t.check("pickupFishFood", fish_result == "ok" and fish_have == 1,
            string.format("click_obj(fish_food) -> %s (%s); inv fish_food=%s (want 1)",
                tostring(fish_result), tostring(fish_detail), tostring(fish_have)))
        pass_door("fishRoomOut", PD, PDO, 3116, 3361, 3116, 3361, 3116, 3363,
            function(tt) return tt.level == 1 and tt.z >= 3362 end, "the first-floor corridor, z >= 3362, level 1")
        stairs_down("goDownToGroundFloor")

        -- ------------------------------------------------- poison (ground floor)
        n_to_f("corridorDoorOut1")
        pass_door("hDoorIn", PD, PDO, 3101, 3371, 3102, 3371, 3100, 3371,
            function(tt) return tt.level == 0 and tt.x <= 3101 end, "the west room, x <= 3101, level 0")
        pass_door("poisonRoomIn", PD, PDO, 3099, 3366, 3099, 3367, 3099, 3365,
            function(tt) return tt.level == 0 and tt.z <= 3366 end, "the poison room, z <= 3366, level 0")
        local poison_result, poison_detail = t.player.click_obj("poison", 3)
        local poison_have = count_of("poison")
        t.check("pickupPoison", poison_result == "ok" and poison_have == 1,
            string.format("click_obj(poison) -> %s (%s); inv poison=%s (want 1)",
                tostring(poison_result), tostring(poison_detail), tostring(poison_have)))

        -- [opheldu,poison]/[opheldu,fish_food] both trigger @poison_fish_food
        -- (quest_haunted.rs2:118-132): both inputs leave, poisoned_fish_food
        -- arrives.
        t.exec("usePoisonOnFishFood", t.player.use_item_on_item, "poison", "fish_food")
        local pff_await_result, pff_await_detail = t.inv.await("poisoned_fish_food", 1, 5)
        local poison_left, fish_left = count_of("poison"), count_of("fish_food")
        t.check("have.poisoned_fish_food", pff_await_result == "ok" and poison_left == 0 and fish_left == 0,
            string.format("inv.await(poisoned_fish_food,1,5) -> %s (%s); poison=%s fish_food=%s (want both 0: used up)",
                tostring(pff_await_result), tostring(pff_await_detail), tostring(poison_left), tostring(fish_left)))
        pass_door("poisonRoomOut", PD, PDO, 3099, 3366, 3099, 3366, 3099, 3368,
            function(tt) return tt.level == 0 and tt.z >= 3367 end, "the west room, z >= 3367, level 0")
        pass_door("hDoorOut", PD, PDO, 3101, 3371, 3101, 3371, 3103, 3371,
            function(tt) return tt.level == 0 and tt.x >= 3102 end, "the north corridor, x >= 3102, level 0")

        -- ---------------------------------------- spade, then out the back door
        pass_door("kitchenDoorIn", PD, PDO, 3120, 3356, 3119, 3356, 3121, 3356,
            function(tt) return tt.level == 0 and tt.x >= 3120 end, "the kitchen, x >= 3120, level 0")
        local spade_result, spade_detail = t.player.click_obj("spade", 3)
        local spade_have = count_of("spade")
        t.check("pickupSpade", spade_result == "ok" and spade_have == 1,
            string.format("click_obj(spade) -> %s (%s); inv spade=%s (want 1)",
                tostring(spade_result), tostring(spade_detail), tostring(spade_have)))
        -- hauntedbackdoor is a door_selfstage door: open, it is the SAME
        -- symbol one tile south (doors_selfstage.rs2:76-89).
        pass_door("backDoorOut", "hauntedbackdoor", "hauntedbackdoor", 3123, 3361, 3123, 3360, 3123, 3362,
            function(tt) return tt.level == 0 and tt.z >= 3361 end, "the grounds north of the kitchen, z >= 3361, level 0")

        -- ------------------------------------------------------ the compost
        -- [oplocu,hauntedcompostheap] (quest_haunted.rs2:62-74) grants the
        -- key two ticks behind its first mes(): poll (trap 24/25).
        t.exec("goto-compost", t.player.goto_tile, 3087, 3361, 0)
        local compostheap = t.player.by_symbol("loc", "hauntedcompostheap")
        t.exec("searchCompost", t.player.use_on, "spade", compostheap)
        local key_await_result, key_await_detail = t.inv.await("closet_key", 1, 5)
        t.check("have.closet_key", key_await_result == "ok",
            string.format("inv.await(closet_key,1,5) -> %s (%s) after digging the compost with the spade",
                tostring(key_await_result), tostring(key_await_detail)))

        -- ------------------------------------------------------- the gauge
        t.exec("goto-fountain", t.player.goto_tile, 3089, 3335, 0)
        local fountain = t.player.by_symbol("loc", "hauntedfountain")
        t.exec("useFishFoodOnFountain", t.player.use_on, "poisoned_fish_food", fountain)
        -- [oplocu,hauntedfountain] (quest_haunted.rs2:134-145): plain mes()
        -- lines, then %haunted_manor_fountain_poisoned = 1 as the LAST
        -- statement after "...then die and float to the surface."
        local poison_wait_result, poison_wait_detail = t.msg.await("then die and float to the surface", 10)
        local pff_left = count_of("poisoned_fish_food")
        t.check("poison-fountain-wait", poison_wait_result == "ok" and pff_left == 0,
            "msg.await('...then die and float to the surface', 10) -> " .. tostring(poison_wait_result) .. " " .. tostring(poison_wait_detail)
                .. "; poisoned_fish_food=" .. tostring(pff_left) .. " (want 0: poured in)")

        -- A second plain click on the now-poisoned fountain grants the gauge
        -- (oploc1,hauntedfountain): its chatplayer_anim pages are real
        -- modal pages, so drain them.
        t.exec("searchFountain", t.player.click_loc, "hauntedfountain", 1)
        t.exec("fountain-gauge-drain", t.chat.drain, { stop_at = "none" })
        local gauge_await_result, gauge_await_detail = t.inv.await("pressure_gauge", 1, 10)
        t.check("pickup.pressure_gauge", gauge_await_result == "ok",
            string.format("inv.await(pressure_gauge,1,10) -> %s (%s) after the poisoned fountain's second click",
                tostring(gauge_await_result), tostring(gauge_await_detail)))

        -- ------------------------------------------------- the rubber tube
        -- open_manor_entrance's swing is timed (loc_change duration 3), so
        -- the guide has the player click the front doors again
        -- (enterManorWithKey).
        t.exec("goto-manor-reentry", t.player.goto_tile, 3108, 3349, 0)
        hop("enterManorWithKey", "haunteddoorl", 3108, 3353, 0, 3108, 3352, 3108, 3354, 0)
        hall_door_in("hallDoorIn2")
        -- closet_door (quest_haunted.rs2:77-93) opens only with closet_key
        -- held and walks the player through (~ernest_walk_through_door): in
        -- from 3107,3367 to 3108,3367, and back out the same way.
        hop("openClosetDoor", "closet_door", 3107, 3367, 0, 3107, 3367, 3108, 3367, 0)
        local tube_result, tube_detail = t.player.click_obj("rubber_tube", 3)
        local tube_have = count_of("rubber_tube")
        t.check("getTube", tube_result == "ok" and tube_have == 1,
            string.format("click_obj(rubber_tube) -> %s (%s); inv rubber_tube=%s (want 1)",
                tostring(tube_result), tostring(tube_detail), tostring(tube_have)))
        hop("leaveCloset", "closet_door", 3107, 3367, 0, 3108, 3367, 3107, 3367, 0)

        -- ----------------------------------------------------- the oil can
        -- The bookcase (quest_haunted.rs2:172-177) only answers from the EAST
        -- and its manor_bookcase_door proc puts the player in the secret room
        -- at 0_48_52_24_30 = 3096,3358.
        n_to_f("corridorDoorOut2")
        f_to_t("bookcaseRoomIn")
        hop("searchBookcase", "hauntedbookcasel", 3097, 3358, 0, 3098, 3358, 3096, 3358, 0)
        -- puzzle_ladder_top (quest_haunted.rs2:211-214) resets every lever and
        -- door bit and lands at 0_48_152_44_26 = 3116,9754 in the basement.
        hop("goDownLadder", "puzzle_ladder_top", 3092, 3362, 0, nil, nil, 3116, 9754, 0)

        -- Nine lever pulls in Quest Helper's solved order: A down, B down,
        -- D down, B up, A up, F down, E down, C down, E up. Between them the
        -- maze gates are walked through as each pull opens them
        -- (update_ernest_doors, quest_haunted.rs2:325-372; lever bits A..F =
        -- 1..6): 4to7 = A&B&!C&!D&!E&!F; 4to5 = A&B&D; 5to8 = !C&D, or
        -- !A&!B&C&D&!E&F; 5to6 = D; 3to6 = !B&D&!F; 2to3 = !B&D&F; 1to2 =
        -- !A&!B&D&E&F; 2to5 = !A&!B&C&D&!E&F; 8to9 = !E&F. A gate is a s10
        -- scenery tile crossed by a 2-tile teleport to the mirror of the
        -- player's own orthogonal tile (quest_haunted.rs2:438-451), so each
        -- crossing stands on the exact side tile first.
        -- Rooms (static collision): ladder room (levers A, B) x3100-3118
        -- z9745-9757; east room (C, D) x3105-3112 z9758-9767; room 2
        -- x3100-3104 z9763-9767; room 5 x3101-3104 z9758-9762; room 6
        -- x3096-3099 z9758-9762; room 3 (E, F) x3096-3099 z9763-9767; the oil
        -- can's room x3090-3099 z9753-9757.
        lever("pullDownLeverA", "levera", "A", "down")
        lever("pullDownLeverB", "leverb", "B", "down")
        if not gate_from_walled_side("gate4to7.north", "4to7", 3108, 9758, 3108, 9757, 3108, 9759) then return end
        lever("pullDownLeverD", "leverd", "D", "down")
        hop("gate4to5.west", "4to5", 3105, 9760, 0, 3106, 9760, 3104, 9760, 0)
        hop("gate5to8.south", "5to8", 3102, 9758, 0, 3102, 9759, 3102, 9757, 0)
        lever("pullUpLeverB", "leverb", "B", "up")
        lever("pullUpLeverA", "levera", "A", "up")
        if not gate_from_walled_side("gate5to8.north", "5to8", 3102, 9758, 3102, 9757, 3102, 9759) then return end
        hop("gate5to6.west", "5to6", 3100, 9760, 0, 3101, 9760, 3099, 9760, 0)
        if not gate_from_walled_side("gate3to6.north", "3to6", 3097, 9763, 3097, 9762, 3097, 9764) then return end
        lever("pullDownLeverF", "leverf", "F", "down")
        lever("pullDownLeverE", "levere", "E", "down")
        if not gate_from_walled_side("gate2to3.east", "2to3", 3100, 9765, 3099, 9765, 3101, 9765) then return end
        if not gate_from_walled_side("gate1to2.east", "1to2", 3105, 9765, 3104, 9765, 3106, 9765) then return end
        lever("pullDownLeverC", "leverc", "C", "down")
        hop("gate1to2.west", "1to2", 3105, 9765, 0, 3106, 9765, 3104, 9765, 0)
        hop("gate2to3.west", "2to3", 3100, 9765, 0, 3101, 9765, 3099, 9765, 0)
        lever("pullUpLeverE", "levere", "E", "up")
        if not gate_from_walled_side("gate2to3.east2", "2to3", 3100, 9765, 3099, 9765, 3101, 9765) then return end
        hop("gate2to5.south", "2to5", 3102, 9763, 0, 3102, 9764, 3102, 9762, 0)
        hop("gate5to8.south2", "5to8", 3102, 9758, 0, 3102, 9759, 3102, 9757, 0)
        hop("gate8to9.west", "8to9", 3100, 9755, 0, 3101, 9755, 3099, 9755, 0)

        -- oil_can is a plain ground item in the last room (areas/world/
        -- configs/m48_152.spawn), the tile [debugproc,hauntedbmp_oil] shows.
        local oil_result, oil_detail = t.player.click_obj("oil_can", 3)
        local oil_have = count_of("oil_can")
        t.check("pickupOilCan", oil_result == "ok" and oil_have == 1,
            string.format("click_obj(oil_can) -> %s (%s); inv oil_can=%s (want 1)",
                tostring(oil_result), tostring(oil_detail), tostring(oil_have)))

        -- ------------------------------------------------------- hand-in
        -- Out the way the player came: back through 8to9 (still open: !E&F),
        -- up puzzle_ladder (quest_haunted.rs2:216-220: lands 0_48_52_20_33 =
        -- 3092,3361 in the secret room), out by the lever (hauntedleverup,
        -- :186-194: a tile north, then two east through the bookcase, to
        -- 3098,3358), and up both staircases to Oddenstein.
        if not gate_from_walled_side("gate8to9.east", "8to9", 3100, 9755, 3099, 9755, 3101, 9755) then return end
        hop("goUpFromBasement", "puzzle_ladder", 3117, 9754, 0, nil, nil, 3092, 3361, 0)
        hop("pullLeverToLeave", "hauntedleverup", 3096, 3357, 0, 3096, 3357, 3098, 3358, 0)
        t_to_f("bookcaseRoomOut")
        f_to_n("corridorDoorIn")
        stairs_up("goToFirstFloorToFinish")
        spiral_up("goToSecondFloorToFinish")
        lab_in("labDoorIn2")

        local reward_coins_before_result, reward_coins_before = t.inv.count("coins")
        t.exec("talkToOddenteinAgain", t.player.talk_to, "professor_oddenstein", 1)
        -- oddenstein_items (all three parts present) -> haunted_take_parts
        -- -> oddenstein_ernest_thanks -> haunted_commit, ONE continuous
        -- chain, no choices at all. The "You give the rubber tube..." /
        -- "The machine hums and shakes." lines are PLAIN mes() (chat log,
        -- not pages), so only the chatnpc/chatplayer pages are entries.
        t.exec("oddenstein-handin", t.chat.play, {
            "npc:Have you found anything yet?",
            "player:I have everything!",
            "npc:Give 'em here then.",
            "npc:Let's get this fixed then.",
            "npc:It was dreadfully irritating being a chicken. How can I ever thank you?",
            "player:Well a cash reward is always nice...",
            "npc:Of course, of course.",
        })
        t.ticks(3) -- completion (haunted_commit) is queued behind its own dialogue, not synchronous

        t.quest.expect_complete()

        local parts_left = count_of("pressure_gauge") + count_of("oil_can") + count_of("rubber_tube")
        t.check("handin.parts_taken", parts_left == 0,
            "pressure_gauge + oil_can + rubber_tube left in the pack = " .. tostring(parts_left) .. " (want 0: haunted_take_parts took all three)")

        local reward_coins_after_result, reward_coins_after = t.inv.count("coins")
        t.check("reward.coins", reward_coins_before_result == "ok" and reward_coins_after_result == "ok"
            and reward_coins_before == 0 and reward_coins_after == 300,
            string.format("coins %s -> %s (want 0 -> 300, haunted_commit inv_add(coins, 300)), reads %s/%s",
                tostring(reward_coins_before), tostring(reward_coins_after),
                tostring(reward_coins_before_result), tostring(reward_coins_after_result)))

        t.finish(0)
    end,
}
