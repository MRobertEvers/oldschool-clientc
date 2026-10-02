-- Regicide (quest_regicide), written as a relay of legs (docs/quest_authoring/relay.md).
-- Prerequisites per Quest Helper: Underground Pass complete (and Biohazard, which gates its cave
-- entrance); Agility 56 and Crafting 10 are required levels (Crafting is a later leg's).

return {
    id = "regicide",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_biohazard", -- Quest Helper prerequisite chain: Underground Pass needs it
        -- Quest Helper: Underground Pass is a Regicide requirement. quest_cheat.rs2 has no ::complete arm
        -- for it, so its prerequisite state is written: complete, and Lathas met (upass_entrance.rs2:15).
        "::setvar varp161_upass ^upass_complete",
        "::setvar varb9125_upass_lathas_met 1",
        -- Underground Pass prerequisite state for the second walk through the pass (leg 2): the area-1 well
        -- needs all four orbs (upass_well.rs2:12) and Iban's door the three badges and the horn
        -- (upass_bloodwell.rs2:25). A completed Underground Pass has delivered all of them.
        "::setvar varb9119_upass_caveorb_1 1",
        "::setvar varb9120_upass_caveorb_2 1",
        "::setvar varb9121_upass_caveorb_3 1",
        "::setvar varb9122_upass_caveorb_4 1",
        "::setvar varb9128_upass_paladinbadge_1 1",
        "::setvar varb9129_upass_paladinbadge_2 1",
        "::setvar varb9130_upass_paladinbadge_3 1",
        "::setvar varb9136_upass_cave_unicorn 1",
        "::setlevel agility 56", -- Quest Helper: Agility 56 (rockslides, upass_obstacles.rs2:38)
        "::setlevel hitpoints 40", -- a questing account's fighting levels for the pass's spiders
        "::setlevel defence 30",
        "::give shortbow 1", -- enterTheDungeon items: Bow (not crossbow)
        "::give bronze_arrow 20", -- Arrows (metal, unpoisoned)
        "::give rope 2", -- Rope: the pit swing eats one each crossing (upass_obstacles.rs2:130), walked here and again by a later leg
        -- the grid pattern the Underground Pass start rolls (king_lathas.rs2:129); a completed pass leaves it unset, which
        -- makes the grid harmless, so the safe bands are written for the grid to be crossed for real
        "::setvar varp6010_upass_grid_pattern 232",
        "::give spade 1", -- Spade
        "::give tinderbox 1", -- lights the cloth-wrapped arrow (the guide's fire)
        "::give lobster 6", -- food for the pass's traps
    },
    bind = {
        varp = "varp328_regicide_quest",
        constants = {
            not_started = 0, received_message = 1, spoken_lathas = 2, spoken_scouts = 3, spoken_iorwerth = 4,
            spoken_tracker = 5, shown_pendant = 6, found_footprints = 7, spoken_tracker2 = 8, defeated_guard = 9,
            entered_camp = 10, spoken_iorwerth2 = 11, killed_tyras = 12, reported_iorwerth = 13,
            spoken_arianwyn = 14, complete = 15,
        },
        row = "quest_regicide",
        display = "Regicide",
        points = 3,
    },

    legs = {
        { name = "lathas_and_first_half_of_the_pass", run = function(t)
            -- LEG 1 BEGIN: goToArdougneCastleFloor2
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            t.exec("goto-goToArdougneCastleFloor2", t.player.goto_tile, 2572, 3295, 0)
            t.exec("goToArdougneCastleFloor2", t.player.click_loc, "stairs", 1) -- ladders.rs2:175
            t.ticks(4)
            local _, at = t.world.tile()
            t.check("goToArdougneCastleFloor2-level", at.level == 1, "standing at " .. at.x .. "," .. at.z .. " level " .. at.level)

            t.exec("goto-talkToKingLathas", t.player.goto_tile, 2578, 3293, 1)
            t.exec("talkToKingLathas", t.player.talk_to, "kinglathas", 1) -- king_lathas.rs2:79
            t.exec("talkToKingLathas-dialog", t.chat.play, {
                "player:I received your message",
                "npc:Ahh... adventurer",
                "options",
                "choose:I assume you have a plan?",
                "player:I assume",
                "npc:I do indeed",
                "player:Elves?",
                "npc:Well that may",
                "player:So what do I need",
                "npc:You are to head",
                "npc:With their help",
                "options",
                "choose:Yes.",
                "player:Very well",
                "npc:My brother may not",
                "player:I see",
                "npc:Good luck",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_lathas", t.quest.expect_stage("spoken_lathas"))

            -- enterTheDungeon: the cave mouth west of Ardougne (upass_entrance.rs2:9)
            t.exec("goto-enterTheDungeon", t.player.goto_tile, 2434, 3314, 0)
            t.exec("enterTheDungeon", t.player.click_loc, "upass_caveentrance2", 1)
            t.ticks(6)
            _, at = t.world.tile()
            t.check("enterTheDungeon-tile", at.z > 9000, "underground at " .. at.x .. "," .. at.z .. " level " .. at.level .. " :: " .. last_lines(2))

            -- a failed agility roll slips the climber back (upass_obstacles.rs2:38): click until the far side
            local function other_side_lines()
                local _, lines = t.msg.last(12)
                local n = 0
                for _, line in ipairs(lines or {}) do
                    if string.find(tostring(type(line) == "table" and line.text or line), "step down the other side", 1, true) then n = n + 1 end
                end
                return n
            end
            local function climb(name, slide_x, slide_z)
                local crossed = false
                local detail = ""
                for attempt = 1, 8 do
                    local before = other_side_lines()
                    local result = t.player.click_loc("rockslide2_obstacle_upass", 1, { at = { slide_x, slide_z } })
                    t.ticks(8)
                    local _, here = t.world.tile()
                    detail = "attempt " .. attempt .. ": " .. tostring(result) .. " now at " .. here.x .. "," .. here.z
                    if other_side_lines() > before then
                        crossed = true
                        detail = detail .. " (server: ...and step down the other side)"
                        break
                    end
                end
                t.check(name, crossed, detail)
            end
            climb("climbOverRockslide1", 2480, 9713)
            -- rockslides 2 and 3 lie on the same corridor to the bridge; plain travel then climb
            t.exec("goto-climbOverRockslide2", t.player.goto_tile, 2473, 9706, 0)
            climb("climbOverRockslide2", 2471, 9706)
            climb("climbOverRockslide3", 2458, 9712)

            -- searchBagForCloth, useClothOnArrow, lightArrow, then the shot
            t.exec("goto-searchBagForCloth", t.player.goto_tile, 2452, 9715, 0)
            t.exec("searchBagForCloth", t.player.click_loc, "upass_gear", 1) -- koftik.rs2:143
            t.ticks(4)
            local crossed = false
            for attempt = 1, 5 do
                local sfx = attempt == 1 and "" or ("-retry" .. attempt)
                if attempt > 1 then
                    t.exec("goto-searchAgain" .. sfx, t.player.goto_tile, 2453, 9716, 0)
                    t.exec("searchBagForCloth" .. sfx, t.player.click_loc, "upass_gear", 1)
                    t.ticks(4)
                end
                t.exec("useClothOnArrow" .. sfx, t.player.use_item_on_item, "damp_cloth", "bronze_arrow") -- upass_bridge.rs2:13
                t.ticks(2)
                t.exec("lightArrow" .. sfx, t.player.use_item_on_item, "tinderbox", "unlitarrow") -- firemaking.rs2:25
                t.ticks(2)
                if attempt == 1 then t.exec("wieldBow", t.player.equip, "shortbow") end
                t.exec("wieldLitArrow" .. sfx, t.player.equip, "litarrow")
                t.ticks(2)
                t.exec("walkNorthEastOfBridge" .. sfx, t.player.goto_tile, 2450, 9722, 0)
                t.exec("shootBridgeRope" .. sfx, t.player.click_loc, "oldbridge_guiderope", 1) -- upass_bridge.rs2:64
                for _poll = 1, 12 do -- forcewalks then a teleport
                    t.ticks(4)
                    _, at = t.world.tile()
                    if at.x < 2444 then break end
                end
                _, at = t.world.tile()
                if at.x < 2444 then crossed = true break end
            end
            t.check("shootBridgeRope-crossed", crossed, "after the shot at " .. at.x .. "," .. at.z .. " level " .. at.level .. " :: " .. last_lines(4))

            -- The way on from the bridge, driven by clicks (upass_obstacles.rs2, upass_grid.rs2): the rope swing over the
            -- pit, rockslides 4 and 5, the grid, its lever and the spear traps. Row names carry -outbound because the
            -- return walk of a later leg meets the same obstacles.
            local function eat_if_low()
                local hp_result, hp_row = t.skill.read("hitpoints")
                if hp_result == "ok" and hp_row.level <= 14 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(3)
                end
            end
            t.exec("goto-crossThePit-outbound", t.player.goto_tile, 2461, 9699, 0)
            local pit_rock = t.player.by_symbol("loc", "obstical_rockswing_norope")
            t.exec("crossThePit-outbound", t.player.use_on, "rope", pit_rock) -- upass_obstacles.rs2:110
            t.ticks(12)
            _, at = t.world.tile()
            t.check("crossThePit-outbound-landed", at.x >= 2465, "after the swing at " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            climb("climbOverRockslide4", 2491, 9691)
            climb("climbOverRockslide5", 2482, 9679)
            -- crossTheGrid: %varp6010_upass_grid_pattern names one safe 2-row band per column group (upass_grid.rs2:72-96);
            -- the timer fails a player outside all three. Band of digit d is z 9673+2(d-1) .. +1.
            local _, pattern = t.var.server("varp6010_upass_grid_pattern")
            pattern = tonumber(pattern) or 0
            local d1, d2, d3 = math.floor(pattern / 100) % 10, math.floor(pattern / 10) % 10, pattern % 10
            local function band_z(d) return 9673 + 2 * (d - 1) end
            t.check("crossTheGrid-pattern", d1 >= 1 and d1 <= 5 and d2 >= 1 and d3 >= 1, "upass_grid_pattern=" .. tostring(pattern) .. " -> safe bands z " .. band_z(d1) .. " / " .. band_z(d2) .. " / " .. band_z(d3))
            local grid_path = { { 2478, band_z(d1) }, { 2473, band_z(d1) }, { 2473, band_z(d2) }, { 2469, band_z(d2) }, { 2469, band_z(d3) }, { 2467, band_z(d3) }, { 2466, band_z(d3) } }
            local trail = {}
            for _, wp in ipairs(grid_path) do
                t.player.walk_to(wp[1], wp[2], 14)
                t.ticks(2)
                local _, here = t.world.tile()
                trail[#trail + 1] = here.x .. "," .. here.z
            end
            _, at = t.world.tile()
            t.check("crossTheGrid", at.x <= 2467 and not string.find(last_lines(6), "trap", 1, true), "walked the safe bands " .. table.concat(trail, " > ") .. " -> " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            t.player.walk_to(2466, 9673, 10)
            t.ticks(2)
            _, at = t.world.tile()
            t.check("goto-pullLeverAfterGrid-outbound", at.x == 2466 and at.z <= 9674, "walked south along x 2466, outside the grid zone, to " .. at.x .. "," .. at.z)
            t.exec("pullLeverAfterGrid-outbound", t.player.click_loc, "portcullis_lever_up", 1) -- upass_grid.rs2:25
            t.ticks(10)
            _, at = t.world.tile()
            t.check("pullLeverAfterGrid-outbound-through", at.x < 2465, "after the lever at " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            -- the five spear traps (upass_obstacles.rs2:203): a disarm roll, a failure costs hp
            local function pass_trap(name, trap_x, trap_z)
                local detail = ""
                local passed = false
                for attempt = 1, 5 do
                    local result = t.player.click_loc("upass_speartrap", 1, { at = { trap_x, trap_z } })
                    t.ticks(2)
                    local kind = t.chat.kind()
                    local played = "no page (" .. tostring(kind) .. ")"
                    if kind ~= "none" then
                        played = tostring(t.chat.play({ "mesbox:It's a trap", "choose:Yes, I'll give it a go." }))
                    end
                    t.ticks(6)
                    local _, there = t.world.tile()
                    local hp_result, hp_row = t.skill.read("hitpoints")
                    detail = "attempt " .. attempt .. ": click " .. tostring(result) .. ", chat " .. played .. ", now " .. there.x .. "," .. there.z .. ", hp " .. tostring(hp_result == "ok" and hp_row.level or hp_result) .. " :: " .. last_lines(3)
                    if there.x < trap_x then passed = true break end
                    eat_if_low()
                end
                t.check(name, passed, detail)
                eat_if_low()
            end
            pass_trap("passTrap1-outbound", 2443, 9677)
            pass_trap("passTrap2-outbound", 2440, 9677)
            pass_trap("passTrap3-outbound", 2435, 9675)
            pass_trap("passTrap4-outbound", 2432, 9675)
            pass_trap("passTrap5-outbound", 2430, 9675)

            -- goBackUpToIbansCavern: the cave-wall tunnel at the west end of the first half
            t.exec("goto-goBackUpToIbansCavern", t.player.goto_tile, 2337, 9793, 0)
            t.exec("goBackUpToIbansCavern", t.player.click_loc, "cavewalltunnel_upass_up", 1) -- upass_tunnels.rs2:21
            t.ticks(6)
            _, at = t.world.tile()
            t.check("goBackUpToIbansCavern-level", at.level == 1, "standing at " .. at.x .. "," .. at.z .. " level " .. at.level .. " :: " .. last_lines(2))

            local _, stage = t.quest.stage()
            local _, lobsters = t.inv.count("lobster")
            _, at = t.world.tile()
            t.check("leg.1.end", true, "player at " .. at.x .. "," .. at.z .. " level " .. at.level .. "; regicide_quest=" .. tostring(stage) .. " read from the server; shortbow worn, spade/rope/tinderbox carried, lobster x" .. tostring(lobsters))
            -- LEG 1 END
        end },
        { name = "well_and_pass_west", run = function(t)
            -- LEG 2 BEGIN: leaveWellCave
            -- The ladder lists the pass back to front; the route runs collectPlank, climbDownWell,
            -- digMud, crossLedge, navigateMaze (pickCellLock, goThroughPipe), leaveUnicornArea,
            -- openIbansDoor, enterWell, leaveWellCave.
            local function where(label, want_x, want_z, want_level)
                local _, w = t.world.tile()
                local _, ml = t.msg.last(3)
                local out = {}
                for _, line in ipairs(ml or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                local ok = true
                if want_x then ok = math.abs(w.x - want_x) <= 2 and math.abs(w.z - want_z) <= 2 and w.level == want_level end
                t.check(label, ok, "standing at " .. w.x .. "," .. w.z .. " level " .. w.level .. " :: " .. table.concat(out, " | "))
            end

            -- collectPlank: the plank lies on the floor of the north room (m38_151.spawn:35)
            t.exec("goto-collectPlank", t.player.goto_tile, 2434, 9725, 0)
            local _, planks_before = t.inv.count("woodplank")
            t.exec("collectPlank", t.player.click_obj, "woodplank", 3)
            t.ticks(2)
            local _, planks = t.inv.count("woodplank")
            t.check("collectPlank-inv", planks == planks_before + 1, "woodplank " .. tostring(planks_before) .. " -> " .. tostring(planks))

            -- climbDownWell: upass_well.rs2:10, all four orbs placed -> 2423,9660
            t.exec("goto-climbDownWell", t.player.goto_tile, 2417, 9673, 0)
            t.exec("climbDownWell", t.player.click_loc, "cave_well", 1)
            t.ticks(6)
            where("climbDownWell-tile", 2423, 9660, 0)

            -- navigateMaze: the cell lock and the pipe are its sub steps
            t.exec("goto-pickCellLock", t.player.goto_tile, 2393, 9657, 0)
            local picked = false
            for attempt = 1, 8 do
                local _, before = t.world.tile()
                t.player.click_loc("cave_railings2", 1)
                t.ticks(8)
                local _, after = t.world.tile()
                if after.z ~= before.z or after.x ~= before.x then
                    picked = after.z <= 9654 or after.z ~= before.z
                end
                if picked then break end
                t.player.goto_tile(2393, 9657, 0)
            end
            where("pickCellLock", 2393, 9655, 0)

            -- digMud: spade on the loose mud (upass_unicorn_tunnels.rs2:9) -> 2392,9646
                        t.ticks(1)
            local mud = t.player.by_symbol("loc", "upass_mud")
            t.exec("digMud", t.player.use_on, "spade", mud, { stand_on_square = true }) -- the mud fills its approach tiles; the walk ends two tiles short at 2395,9651
            t.ticks(8)
            where("digMud-tile", 2392, 9646, 0)

            -- crossLedge: from the east side of the ledge (upass_obstacles.rs2:313) -> 2374,9638
            t.exec("goto-crossLedge", t.player.goto_tile, 2376, 9644, 0)
            t.exec("crossLedge", t.player.click_loc, "upass_ledge", 1)
            t.ticks(8)
            where("crossLedge-tile", 2374, 9638, 0)

            t.exec("goto-goThroughPipe", t.player.goto_tile, 2420, 9605, 0)
            t.exec("goThroughPipe", t.player.click_loc, "upass_pipe6", 1)
            t.ticks(8)
            where("goThroughPipe-tile", 2390, 9605, 0)
            t.check("navigateMaze", true, "the maze's sub steps ran: cell lock, ledge, mud and pipe rows above")

            -- leaveUnicornArea
            t.exec("goto-leaveUnicornArea", t.player.goto_tile, 2373, 9611, 0)
            t.exec("leaveUnicornArea", t.player.click_loc, "upass_unicorn_doorl", 1)
            t.ticks(6)
            where("leaveUnicornArea-tile", 2371, 9666, 0)

            -- openIbansDoor: with the badges and the horn the door opens onto Iban's temple
            t.exec("goto-openIbansDoor", t.player.goto_tile, 2369, 9718, 0)
            t.exec("openIbansDoor", t.player.click_loc, "cavetempledoor2r", 1)
            t.ticks(6)
            where("openIbansDoor-tile", 2173, 4725, 1)

            -- enterWell
            t.exec("goto-enterWell", t.player.goto_tile, 2010, 4709, 1)
            t.exec("enterWell", t.player.click_loc, "regicide_voyage_temple_well1", 1) -- regicide_route.rs2:11
            t.ticks(6)
            where("enterWell-tile", 2343, 9622, 0)

            -- leaveWellCave: arrival fires the Idris scene (regicide_route.rs2:35)
            t.exec("goto-leaveWellCave", t.player.goto_tile, 2315, 9624, 0)
            t.exec("leaveWellCave", t.player.click_loc, "regicide_voyage_temple_exit", 1) -- regicide_route.rs2:27
            for _poll = 1, 10 do
                if t.chat.kind() ~= "none" then break end
                t.ticks(1)
            end
            t.check("leaveWellCave-scene", t.chat.kind() ~= "none", "Idris's scene opened on the arrival tile, page kind " .. tostring(t.chat.kind()))
            t.exec("talkToIdris-dialog", t.chat.play, {
                "npc:Halt human",
                "npc:Wait! What was that",
                "npc:Are you the human",
                "player:Yes that's me",
                "npc:Good... We've been expecting you",
                "npc:You should speak with Lord Iorwerth",
            })
            t.ticks(4)
            t.expect("quest.stage.spoken_scouts", t.quest.expect_stage("spoken_scouts"))
            local _, stage = t.quest.stage()
            local _, at = t.world.tile()
            t.check("leg.2.end", true, "player at " .. at.x .. "," .. at.z .. " level " .. at.level .. "; regicide_quest=" .. tostring(stage) .. " read from the server; planks " .. tostring(planks))
            -- LEG 2 END
        end },
        { name = "pass_east_and_tirannwn_traps", run = function(t)
            -- LEG 3 BEGIN: crossThePit
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function where(label, want_x, want_z, want_level)
                local _, w = t.world.tile()
                local ok = true
                if want_x then ok = math.abs(w.x - want_x) <= 6 and math.abs(w.z - want_z) <= 6 and w.level == want_level end
                t.check(label, ok, "standing at " .. w.x .. "," .. w.z .. " level " .. w.level .. " :: " .. last_lines(3))
            end

            -- Back into the voyage cave from the arrival tile (regicide_route.rs2:23), then the pass's pit.
            t.exec("goto-reenterVoyageCave", t.player.goto_tile, 2312, 3216, 0)
            t.exec("reenterVoyageCave", t.player.click_loc, "regicide_voyage_temple_entrance", 1)
            t.ticks(6)
            where("reenterVoyageCave-tile", 2314, 9624, 0)

            -- crossThePit: rope on the rock (upass_obstacles.rs2:110); a fall loses the rope and costs hitpoints
            local swung = false
            for attempt = 1, 8 do
                local sfx = attempt == 1 and "" or ("-retry" .. attempt)
                t.exec("goto-crossThePit" .. sfx, t.player.goto_tile, 2461, 9699, 0)
                local rock = t.player.by_symbol("loc", "obstical_rockswing_norope")
                t.exec("crossThePit" .. sfx, t.player.use_on, "rope", rock)
                t.ticks(10)
                local _, here = t.world.tile()
                if here.x >= 2464 then swung = true break end
                t.cheat("::give rope 1") -- brought along (enterTheDungeon: Rope); the fall lost this one
                t.ticks(2)
            end
            where("crossThePit-tile")
            t.check("crossThePit-crossed", swung, "after the swing :: " .. last_lines(4))

            -- pullLeverAfterGrid: the lever at the grid's gate (upass_grid.rs2:25)
            t.exec("goto-pullLeverAfterGrid", t.player.goto_tile, 2467, 9673, 0)
            t.exec("pullLeverAfterGrid", t.player.click_loc, "portcullis_lever_up", 1, { stand_on_square = true })
            t.ticks(8)
            where("pullLeverAfterGrid-tile")

            -- passTrap1..5: the spear traps in the west corridor (upass_obstacles.rs2:203)
            -- The five upass_speartrap placements (m38_151.jl2 and m37_151.jl2), east to west. Disarming can fail
            -- (thieving 1), so the first attempt is the row and any retry is the same press without a row.
            local traps = {
                { "passTrap1", 2445, 9677, 2443 }, { "passTrap2", 2442, 9677, 2440 },
                { "passTrap3", 2436, 9675, 2435 }, { "passTrap4", 2434, 9675, 2432 }, { "passTrap5", 2433, 9675, 2430 },
            }
            local failures = 0
            for _, trap in ipairs(traps) do
                local name, sx, sz, want_x = trap[1], trap[2], trap[3], trap[4]
                local passed = false
                for attempt = 1, 12 do
                    if attempt == 1 then
                        t.exec("goto-" .. name, t.player.goto_tile, sx, sz, 0)
                        t.exec(name, t.player.click_loc, "upass_speartrap", 1)
                        t.ticks(2)
                        t.exec(name .. "-dialog", t.chat.play, { "mesbox:The markings appear", "choose:/give it a go/" })
                    else
                        t.player.goto_tile(sx, sz, 0)
                        t.player.click_loc("upass_speartrap", 1)
                        t.ticks(2)
                        t.chat.play({ "mesbox:The markings appear", "choose:/give it a go/" })
                    end
                    t.ticks(6)
                    local _, here = t.world.tile()
                    if here.x <= want_x or string.find(last_lines(4), "and succeed", 1, true) then passed = true break end
                    failures = failures + 1
                    if failures % 3 == 0 then
                        t.player.inv_op("lobster", 1)
                        t.ticks(2)
                    end
                end
                local _, here = t.world.tile()
                t.check(name .. "-tile", passed, "standing at " .. here.x .. "," .. here.z .. " level " .. here.level .. " :: " .. last_lines(3))
            end

            -- Back out of the pass by the same route the first walk took (leg 2): the well cave's exit.
            t.exec("goto-leavePassForTirannwn", t.player.goto_tile, 2315, 9624, 0)
            t.exec("leavePassForTirannwn", t.player.click_loc, "regicide_voyage_temple_exit", 1) -- regicide_route.rs2:27
            t.ticks(6)
            where("leavePassForTirannwn-tile", 2312, 3216, 0)
            -- talkToIdris: the scene fired at stage 2 in leg 2 (talkToIdris-dialog); Idris is killed in it, so he
            -- is gone for good (regicide_route.rs2:35-60).
            t.check("talkToIdris", true, "Idris's scene already played in leg 2 (row talkToIdris-dialog), stage " .. tostring(select(2, t.quest.stage())))

            -- The hazards between the arrival and Lord Iorwerth's camp (regicide_traps.rs2). Each is crossed by
            -- the loc's own click from the tile beside it. A failure (pitfall: fall into a pit, trap: damage) is
            -- retried; the first attempt is the row, a retry is the same press without one.
            local function cross(name, sym, stand_x, stand_z, at_x, at_z, success_text)
                local crossed = false
                local from_x, from_z
                for attempt = 1, 10 do
                    local _, before = t.world.tile()
                    if attempt == 1 then
                        t.exec("goto-" .. name, t.player.goto_tile, stand_x, stand_z, 0)
                        from_x, from_z = stand_x, stand_z
                        t.exec(name, t.player.click_loc, sym, 1, { at = { at_x, at_z } })
                    else
                        t.player.goto_tile(stand_x, stand_z, 0)
                        t.player.click_loc(sym, 1, { at = { at_x, at_z } })
                    end
                    t.ticks(6)
                    if string.find(last_lines(5), success_text, 1, true) then crossed = true break end
                    t.player.click_loc("regicide_trap_hand_holds", 1) -- out of the pit, if it was a pitfall
                    t.ticks(6)
                    -- a failed trap costs 8-15 hitpoints (regicide_traps.rs2): eat after every failure
                    local _, food = t.inv.count("lobster")
                    if food == 0 then
                        t.cheat("::give lobster 6") -- brought-along food (setup gives six); the traps used it up
                        t.ticks(2)
                    end
                    t.player.inv_op("lobster", 1)
                    t.ticks(3)
                end
                local _, here = t.world.tile()
                t.check(name .. "-crossed", crossed, "from " .. tostring(from_x) .. "," .. tostring(from_z) .. " to " .. here.x .. "," .. here.z .. " :: " .. last_lines(3))
            end
            -- Every crossing fires ~maplink_agility, which only answers on the exact source tile of its row
            -- (skill_agility/configs/maplink_agility.dbrow): the stand tiles below are those rows' src tiles.
            cross("goFromCaveToLeaves", "regicide_pitfall_side", 2267, 3205, 2267, 3204, "cross safely")
            cross("goFromLeavesToStickTrap", "regicide_trap_woodspring", 2234, 3181, 2235, 3181, "skillfully pass")
            cross("goFromTyrasToTrap", "regicide_trap_tripwire", 2220, 3155, 2220, 3153, "step over")

            -- climbThroughForest: the dense forest west of the tracker. regicide_route.rs2:72 refuses it below
            -- spoken_tracker2 ("You can see no way to get past this."); the real crossing is leg 5's
            -- (goKillGuardAtSecondForest), after the tracker has been talked to.
            -- GUIDE-GAP: climbThroughForest needs regicide_quest >= spoken_tracker2, regicide_route.rs2:72-75; the crossing is driven at that stage in a later leg
            t.exec("goto-climbThroughForest", t.player.goto_tile, 2240, 3149, 0)
            t.exec("climbThroughForest", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2238, 3148 } })
            t.ticks(4)
            t.check("climbThroughForest-refused", string.find(last_lines(4), "no way to get past", 1, true) ~= nil, "stage 3 refusal :: " .. last_lines(3))

            -- goUpToLeafTowardsLog: the ring of leaves south of the camp, src tile 2209,3201 (maplink_agility.dbrow)
            cross("goUpToLeafTowardsLog", "regicide_pitfall_side", 2209, 3201, 2209, 3202, "cross safely")

            -- goCrossLogToCamp: the log north to Iorwerth's camp (regicide_traps.rs2:70)
            t.exec("goto-goCrossLogToCamp", t.player.goto_tile, 2201, 3236, 0)
            t.player.click_loc("regicide_logbalance1_start", 1, { at = { 2201, 3237 } })
            t.ticks(8)
            local _, at = t.world.tile()
            t.blocked("content_bug: the log to Iorwerth's camp does nothing: regicide_traps.rs2:74-79 [label,regicide_cross_log] ends in ~maplink_agility, and skill_agility/configs/maplink_agility.dbrow has no regicide_logbalance*_start row, so the click answers 'Nothing interesting happens.' and the player stays at " .. at.x .. "," .. at.z .. " (stage 3, talkToIorwerth is on the far side)")
            return
            -- LEG 3 END
        end },
    },
}
