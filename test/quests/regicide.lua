-- Regicide (quest_regicide), written as a relay of legs (docs/quest_authoring/relay.md).
-- Prerequisites per Quest Helper: Underground Pass complete (and Biohazard, which gates its cave
-- entrance); Agility 56 and Crafting 10 are required levels (Crafting is a later leg's).

return {
    id = "regicide",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
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
        "::give rope 1", -- Rope
        "::give spade 1", -- Spade
        "::give tinderbox 1", -- lights the cloth-wrapped arrow (the guide's fire)
        "::give lobster 6", -- food for the pass's traps
        -- Leg 4: killGuard is Quest Helper's Tyras guard (combat 110: 110 hitpoints, defence 100, attack 85, strength 95;
        -- regicide_tyras_guard.rs2 / combat_stats.generated.npc:21612). Brought along: a ranger's levels, a magic
        -- shortbow with rune arrows, and sharks. The guide lists combat gear for this fight.
        "::setlevel ranged 70",
        "::setlevel hitpoints 70",
        "::setlevel defence 40",
        "::give magic_shortbow 1",
        "::give rune_arrow 150",
        "::give shark 12",
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
                if want_x then ok = math.abs(w.x - want_x) <= 6 and math.abs(w.z - want_z) <= 6 and w.level == want_level end
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
            for attempt = 1, 16 do -- two railings stand on x 2393 (z 9656 then z 9655); each pick can fail
                local _, before = t.world.tile()
                t.player.click_loc("cave_railings2", 1, { at = { 2393, before.z >= 9657 and 9656 or 9655 } }) -- upass_unicorn.rs2:11
                t.ticks(8)
                local _, after = t.world.tile()
                if after.z <= 9654 then picked = true end
                if picked then break end
            end
            where("pickCellLock", nil)

            -- digMud: spade on the loose mud (upass_unicorn_tunnels.rs2:9) -> 2392,9646
                        t.ticks(1)
            local mud = t.player.by_symbol("loc", "upass_mud")
            t.exec("digMud", t.player.use_on, "spade", mud) -- upass_unicorn_tunnels.rs2:9
            t.ticks(8)
            where("digMud-tile", 2392, 9646, 0)

            -- crossLedge: from the east side of the ledge (upass_obstacles.rs2:313) -> 2374,9638
            t.exec("goto-crossLedge", t.player.goto_tile, 2376, 9644, 0)
            t.exec("crossLedge", t.player.click_loc, "upass_ledge", 1)
            t.ticks(8)
            where("crossLedge-tile")

            t.blocked("ROUND 3 (orchestrator): the maze -- walk from the ledge landing to the pipe for real and check a tile only that route reaches; this replaced goto 2420,9605 (sampler e4bc0342e). See test/quests/wip/regicide/relay.md round 3")
            t.exec("goThroughPipe", t.player.click_loc, "upass_pipe6", 1)
            t.ticks(8)
            where("goThroughPipe-tile", 2390, 9605, 0)
            -- (the always-true navigateMaze summary row was removed: sampler e4bc0342e finding b)

            -- leaveUnicornArea
            t.exec("goto-leaveUnicornArea", t.player.goto_tile, 2373, 9611, 0)
            t.exec("leaveUnicornArea", t.player.click_loc, "upass_unicorn_doorl", 1)
            t.ticks(6)
            where("leaveUnicornArea-tile")

            -- openIbansDoor: with the badges and the horn the door opens onto Iban's temple
            t.exec("goto-openIbansDoor", t.player.goto_tile, 2369, 9718, 0)
            t.exec("openIbansDoor", t.player.click_loc, "cavetempledoor2r", 1)
            t.ticks(6)
            where("openIbansDoor-tile")

            -- enterWell
            t.blocked("ROUND 3 (orchestrator): Iban's temple -- from 2173,4725 cross the four collapsed bridges and click upass_templedoor_closed_right from the east (lands 2014,4712 L1, seam1 2dd52a46a5), then walk to the well; this replaced goto 2010,4709. See relay.md seam1 + round 3")
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
            t.check("leg.2.state", true, "player at " .. at.x .. "," .. at.z .. " level " .. at.level .. "; regicide_quest=" .. tostring(stage) .. " read from the server; planks " .. tostring(planks))
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

            -- The way on from the pit, driven by clicks as leg 1 does (upass_obstacles.rs2, upass_grid.rs2): rockslides
            -- 4 and 5, the grid on its safe bands, then the lever.
            local function other_side_lines()
                local _, lines = t.msg.last(12)
                local n = 0
                for _, line in ipairs(lines or {}) do
                    if string.find(tostring(type(line) == "table" and line.text or line), "step down the other side", 1, true) then n = n + 1 end
                end
                return n
            end
            local function climb(name, slide_x, slide_z)
                local over = false
                local detail = ""
                for attempt = 1, 8 do
                    local before = other_side_lines()
                    local result = t.player.click_loc("rockslide2_obstacle_upass", 1, { at = { slide_x, slide_z } })
                    t.ticks(8)
                    local _, here = t.world.tile()
                    detail = "attempt " .. attempt .. ": " .. tostring(result) .. " now at " .. here.x .. "," .. here.z
                    if other_side_lines() > before then
                        over = true
                        detail = detail .. " (server: ...and step down the other side)"
                        break
                    end
                end
                t.check(name, over, detail)
            end
            climb("climbOverRockslide4", 2491, 9691)
            climb("climbOverRockslide5", 2482, 9679)
            -- crossTheGrid: %varp6010_upass_grid_pattern names one safe 2-row band per column group (upass_grid.rs2:72-96)
            -- Lathas seeds the pattern at the pass's start (king_lathas.rs2:172), which setup skips; a relogged checkpoint loses it
            t.cheat("::setvar varp6010_upass_grid_pattern 232")
            t.ticks(2)
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
            local _, gridat = t.world.tile()
            t.check("crossTheGrid", gridat.x <= 2467 and not string.find(last_lines(6), "trap", 1, true), "walked the safe bands " .. table.concat(trail, " > ") .. " -> " .. gridat.x .. "," .. gridat.z .. " :: " .. last_lines(4))
            t.player.walk_to(2466, 9673, 10)
            t.ticks(2)
            local _, leverat = t.world.tile()
            t.check("walk-pullLeverAfterGrid", leverat.x == 2466 and leverat.z <= 9674, "walked south along x 2466 to " .. leverat.x .. "," .. leverat.z)
            -- pullLeverAfterGrid: the lever at the grid's gate (upass_grid.rs2:25)
            t.exec("pullLeverAfterGrid", t.player.click_loc, "portcullis_lever_up", 1)
            t.ticks(10)
            where("pullLeverAfterGrid-tile")

            -- passTrap1..5: the spear traps in the west corridor (upass_obstacles.rs2:203)
            -- The five upass_speartrap placements (m38_151.jl2 and m37_151.jl2), east to west. Disarming can fail
            -- (thieving 1), so the first attempt is the row and any retry is the same press without a row.
            local traps = {
                { "passTrap1", 2445, 9677, 2443 }, { "passTrap2", 2442, 9677, 2440 },
                { "passTrap3", 2436, 9675, 2435 }, { "passTrap4", 2434, 9675, 2432 }, { "passTrap5", 2433, 9675, 2431 },
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
                    if here.x <= want_x then passed = true break end
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
            -- note: climbThroughForest needs regicide_quest >= spoken_tracker2, regicide_route.rs2:72-75; the crossing is driven at that stage in a later leg
            t.exec("goto-climbThroughForest", t.player.goto_tile, 2240, 3149, 0)
            t.exec("climbThroughForest", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2238, 3148 } })
            t.ticks(4)
            t.check("climbThroughForest-refused", string.find(last_lines(4), "no way to get past", 1, true) ~= nil, "stage 3 refusal :: " .. last_lines(3))

            -- goUpToLeafTowardsLog: the ring of leaves south of the camp, src tile 2209,3201 (maplink_agility.dbrow)
            cross("goUpToLeafTowardsLog", "regicide_pitfall_side", 2209, 3201, 2209, 3202, "cross safely")

            -- goCrossLogToCamp: the log north to Iorwerth's camp (regicide_traps.rs2:70)
            t.exec("goto-goCrossLogToCamp", t.player.goto_tile, 2201, 3236, 0)
            t.exec("goCrossLogToCamp", t.player.click_loc, "regicide_logbalance1_start", 1, { at = { 2201, 3237 } })
            t.ticks(10)
            where("goCrossLogToCamp-tile", 2196, 3237, 0)

            -- talkToIorwerth: Lord Iorwerth at the camp, stage spoken_scouts (lord_iorwerth.rs2:107)
            t.exec("goto-talkToIorwerth", t.player.goto_tile, 2203, 3253, 0)
            t.exec("talkToIorwerth", t.player.talk_to, "lord_iorwerth_vis")
            t.exec("talkToIorwerth-dialog", t.chat.play, {
                "player:Hello there", "npc:Ahh", "npc:Unfortunately", "player:I see",
                "npc:Indeed", "npc:You'll find him", "player:Thank you",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_iorwerth", t.quest.expect_stage(4))
            t.ticks(10)
            local _, w = t.world.tile()
            local _, stage = t.quest.stage()
            local _, lobsters = t.inv.count("lobster")
            t.check("leg.3.end", true, "tile " .. w.x .. "," .. w.z .. " level " .. w.level .. ", regicide_quest=" .. tostring(stage) .. ", lobster x" .. tostring(lobsters) .. ", shortbow worn, tinderbox, spade, bronze arrows, woodplank (rope spent on the pit)")
            -- LEG 3 END
        end },
        { name = "tracker_and_camp_guard", run = function(t)
            -- LEG 4 BEGIN: goFromCaveToLeaves
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
            local function eat_if_low()
                local _, food = t.inv.count("lobster")
                if food == 0 then
                    t.cheat("::give lobster 6") -- brought-along food (setup gives six); the traps used it up
                    t.ticks(2)
                end
                t.player.inv_op("lobster", 1)
                t.ticks(3)
            end
            local function cross(name, sym, stand_x, stand_z, at_x, at_z, success_text)
                local crossed = false
                local from_x, from_z
                for attempt = 1, 10 do
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
                    eat_if_low()
                end
                local _, here = t.world.tile()
                t.check(name .. "-crossed", crossed, "from " .. tostring(from_x) .. "," .. tostring(from_z) .. " to " .. here.x .. "," .. here.z .. " :: " .. last_lines(3))
            end

            -- crossLogFromCamp: the log south of the camp, from the camp's end (regicide_traps.rs2:73)
            t.exec("goto-crossLogFromCamp", t.player.goto_tile, 2196, 3237, 0)
            t.exec("crossLogFromCamp", t.player.click_loc, "regicide_logbalance1_start", 1, { at = { 2197, 3237 } })
            t.ticks(10)
            where("crossLogFromCamp-tile", 2201, 3237, 0)

            -- goFromCampToLeavesSouth: the ring of leaves (regicide_traps.rs2:32)
            cross("goFromCampToLeavesSouth", "regicide_pitfall_side", 2209, 3205, 2209, 3204, "cross safely")
            where("goFromCampToLeavesSouth-tile")

            -- Walking is real travel; a hazard in the way is crossed with its own click. The walk verb stops short on
            -- a long route ("stalled at"), so ask again until it stands near the target.
            local function travel(label, x, z, via)
                for _, pt in ipairs(via or {}) do
                    t.player.walk_to(pt[1], pt[2], 40)
                    t.ticks(1)
                end
                for _ = 1, 4 do
                    local _, w = t.world.tile()
                    if math.abs(w.x - x) <= 2 and math.abs(w.z - z) <= 2 then break end
                    t.player.walk_to(x, z, 40)
                    t.ticks(1)
                end
                where(label, x, z, 0)
            end
            local function food_up()
                local _, hp = t.skill.read("hitpoints")
                local _, sharks = t.inv.count("shark")
                if sharks == 0 then
                    local _, lobsters = t.inv.count("lobster")
                    if lobsters == 0 then t.cheat("::give lobster 6") t.ticks(2) end
                end
            end
            -- Camp to tracker: the log, the ring of leaves, the spring (east), then on foot. Back again the other way.
            local function camp_to_tracker(tag)
                t.exec("goto-crossLogFromCamp" .. tag, t.player.goto_tile, 2196, 3237, 0)
                t.exec("crossLogFromCamp" .. tag, t.player.click_loc, "regicide_logbalance1_start", 1, { at = { 2197, 3237 } })
                t.ticks(10)
                where("crossLogFromCamp" .. tag .. "-tile", 2201, 3237, 0)
                cross("goFromCampToLeavesSouth" .. tag, "regicide_pitfall_side", 2209, 3205, 2209, 3204, "cross safely")
                travel("walk-toSpring" .. tag, 2233, 3181, { { 2211, 3191 }, { 2221, 3181 } })
                cross("goFromLeavesToStickTrap" .. tag, "regicide_trap_woodspring", 2234, 3181, 2235, 3181, "skillfully pass")
                travel("walk-toTracker" .. tag, 2257, 3150, { { 2250, 3170 } })
            end

            camp_to_tracker("")

            -- talkToTracker (regicide_camp_tracker.rs2:38): stage spoken_iorwerth, no pendant yet
            t.exec("talkToTracker", t.player.talk_to, "regicide_old_camp_tracker_vis")
            t.exec("talkToTracker-dialog", t.chat.play, {
                "player:Hello", "npc:Human! You must be", "player:No I'm", "npc:And you have something",
                "player:Well... Err", "npc:As I was saying",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_tracker", t.quest.expect_stage(5))

            -- goReturnToIorwerth: back the way we came (spring on foot, the ring north, the log, the camp)
            travel("walk-backToSpring", 2239, 3181, { { 2250, 3170 } })
            travel("walk-backToRing", 2221, 3181, { { 2239, 3181 } })
            travel("walk-backToRing2", 2209, 3201, { { 2211, 3191 } })
            cross("goReturnToIorwerth", "regicide_pitfall_side", 2209, 3201, 2209, 3202, "cross safely")
            t.exec("goto-goReturnToIorwerth-log", t.player.goto_tile, 2201, 3236, 0)
            t.exec("goReturnToIorwerth-log", t.player.click_loc, "regicide_logbalance1_start", 1, { at = { 2201, 3237 } })
            t.ticks(10)
            where("goReturnToIorwerth-log-tile", 2196, 3237, 0)
            t.exec("goto-goReturnToIorwerth-camp", t.player.goto_tile, 2203, 3253, 0)
            t.exec("goReturnToIorwerth-talk", t.player.talk_to, "lord_iorwerth_vis")
            t.exec("goReturnToIorwerth-dialog", t.chat.play, {
                "npc:Good day", "player:Your scout refused", "npc:Bless his loyalty", "mesbox:Lord Iorwerth gives you a crystal pendant",
            })
            t.ticks(2)
            t.expect("pendant.held", t.inv.expect_has("regicide_crystal_pendant", 1))

            -- goReturnToTracker: and again to the tracker, to show the pendant (stage shown_pendant)
            camp_to_tracker("-again")
            t.exec("goReturnToTracker", t.player.talk_to, "regicide_old_camp_tracker_vis")
            t.exec("goReturnToTracker-dialog", t.chat.play, {
                "player:Hello", "npc:Human! You must be", "player:No I'm", "npc:And you have something",
                "mesbox:You show the tracker", "npc:That's Lord Iorwerth's pendant",
                "player:I need to find Tyras", "npc:Well this was his old camp", "player:Can I help at all",
                "npc:As it goes", "player:What is?", "npc:Ahh I guess", "npc:I tell you what",
            })
            t.ticks(2)
            t.expect("quest.stage.shown_pendant", t.quest.expect_stage(6))

            -- clickTracks: Follow the footprints west of the camp (regicide_camp_tracker.rs2:31)
            t.exec("goto-clickTracks", t.player.goto_tile, 2243, 3150, 0)
            t.exec("clickTracks", t.player.click_loc, "regicide_old_camp_footprints_vis_op", 1)
            t.ticks(3)
            t.expect("quest.stage.found_footprints", t.quest.expect_stage(7))

            -- goTalkToTrackerAfterTracks: he explains the dense wood (stage spoken_tracker2)
            travel("walk-backToTracker", 2255, 3149)
            t.exec("goTalkToTrackerAfterTracks", t.player.talk_to, "regicide_old_camp_tracker_vis")
            t.exec("goTalkToTrackerAfterTracks-dialog", t.chat.play, {
                "player:I've found tracks", "npc:These forests", "player:Thanks",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_tracker2", t.quest.expect_stage(8))
            t.exec("wield-magic-shortbow", t.player.equip, "magic_shortbow")
            t.exec("wield-rune-arrows", t.player.equip, "rune_arrow")
            t.ticks(1)

            -- climbThroughForest (stage 8): three dense-forest locs in a row west of the tracker, o3 2238,3148,
            -- o2 2235,3148, o1 2232,3148 (LostCity quest_regicide.rs2:388-480, regicide_route.rs2); each one crossed
            -- three squares west on z=3149. The guard is summoned on 2231,3149 (LostCity spawn_tyras_guard).
            t.exec("goto-climbThroughForest-stage8", t.player.goto_tile, 2240, 3149, 0)
            t.exec("climbThroughForest-stage8", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2238, 3148 } })
            t.ticks(6)
            where("climbThroughForest-tile", 2237, 3149, 0)
            t.exec("climbThroughForest-o2", t.player.click_loc, "regicide_cross_over2", 1, { at = { 2235, 3148 } })
            t.ticks(6)
            where("climbThroughForest-o2-tile", 2234, 3149, 0)
            t.exec("climbThroughForest-o1", t.player.click_loc, "regicide_cross_over1", 1, { at = { 2232, 3148 } })
            t.ticks(6)
            local _, me = t.world.tile()
            t.check("climbThroughForest-o1-tile", me.x == 2231 and me.z == 3149, "standing at " .. me.x .. "," .. me.z .. " :: " .. last_lines(3))
            t.exec("guard.arrived", t.npc.await_present, "regicide_old_camp_guard", 12, 10)

            -- killGuard: a real fight (lvl 110); the quest queues regicide_quest_guard_defeated on its death
            t.exec("killGuard", t.player.attack, "regicide_old_camp_guard", 2, 30)
            t.exec("killGuard-dead", t.npc.await_dead_engaged, 400, 3, { eat = { item = "shark", below = 35 } })
            t.ticks(3)
            t.expect("quest.stage.defeated_guard", t.quest.expect_stage("defeated_guard"))

            -- note: enterTyrasCamp is a position-only step (2190,3144) behind the camp passage regicide_cross_over2_tyras_camp, regicide_route.rs2:72-75 (stage 9 -> 10); it is driven in leg 5 with goKillGuardAtSecondForest
            -- crossTripwire: the tripwire north of the path at 2220,3154 (regicide_traps.rs2:19; pass or snag, both continue)
            t.exec("goto-crossTripwire", t.player.goto_tile, 2220, 3152, 0)
            t.exec("crossTripwire", t.player.click_loc, "regicide_trap_tripwire", 1)
            t.ticks(6)
            local _, tw = t.world.tile()
            t.check("crossTripwire-tile", tw.z >= 3155, "player " .. tw.x .. "," .. tw.z .. " :: " .. last_lines(3))

            local _, w = t.world.tile()
            local _, stage = t.quest.stage()
            local _, sharks = t.inv.count("shark")
            t.check("leg.4.end", true, "tile " .. w.x .. "," .. w.z .. " level " .. w.level .. ", regicide_quest=" .. tostring(stage) .. ", shark x" .. tostring(sharks))
            -- LEG 4 END
        end },
        { name = "camp_and_bomb_ingredients", run = function(t)
            -- LEG 5 BEGIN: goKillGuardAtSecondForest
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
            local function at_exact(label, want_x, want_z)
                local _, w = t.world.tile()
                t.check(label, w.x == want_x and w.z == want_z, "at " .. w.x .. "," .. w.z .. "," .. w.level .. " want " .. want_x .. "," .. want_z .. " :: " .. last_lines(3))
            end
            local function eat_if_low()
                local _, food = t.inv.count("lobster")
                if food == 0 then
                    t.cheat("::give lobster 6") -- brought-along food (setup gives six); the hazards used it up
                    t.ticks(2)
                end
                t.player.inv_op("lobster", 1)
                t.ticks(3)
            end
            local function cross(name, sym, stand_x, stand_z, at_x, at_z, success_text)
                local crossed = false
                local from_x, from_z
                for attempt = 1, 10 do
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
                    eat_if_low()
                end
                local _, here = t.world.tile()
                t.check(name .. "-crossed", crossed, "from " .. tostring(from_x) .. "," .. tostring(from_z) .. " to " .. here.x .. "," .. here.z .. " :: " .. last_lines(3))
            end
            -- Walking is real travel; ask again until it stands near the target.
            local function travel(label, x, z, via)
                for _, pt in ipairs(via or {}) do
                    t.player.walk_to(pt[1], pt[2], 40)
                    t.ticks(1)
                end
                for _ = 1, 4 do
                    local _, w = t.world.tile()
                    if math.abs(w.x - x) <= 2 and math.abs(w.z - z) <= 2 then break end
                    t.player.walk_to(x, z, 40)
                    t.ticks(1)
                end
                where(label, x, z, 0)
            end

            -- Brought along for this leg (guide item requirements of the bomb chapter): limestone for the furnace, gloves
            -- that cover the hands, a pestle and mortar with a pot for the grinding, and coal for the Chemist step. They
            -- are given here, not in setup: the 28 slots are full of leg 4's food and ammunition at the start of the run.
            t.cheat("::give limestone 1")
            t.cheat("::give leather_gloves 1")
            t.cheat("::give pestle_and_mortar 1")
            t.cheat("::give pot_empty 1")
            t.cheat("::give coal 1")
            t.exec("leg5.pack", t.inv.await_all, { limestone = 1, leather_gloves = 1, pestle_and_mortar = 1, pot_empty = 1, coal = 1 }, 10)

            -- Leg 4 ends poisoned and hurt, just north of the tripwire: eat before the next hazards.
            t.player.inv_op("shark", 1)
            t.ticks(3)
            t.player.inv_op("shark", 1)
            t.ticks(3)

            -- goKillGuardAtSecondForest: "Go through the dense forest north then to the west". The middle passage is the
            -- three dense forests o3 2216,3161 / o2 2216,3164 / o3 2216,3167 (regicide_route.rs2, each crossed three squares
            -- north by the loc's own geometry); the guard at the camp entrance is dealt with below.
            t.exec("goto-goKillGuardAtSecondForest", t.player.goto_tile, 2217, 3160, 0)
            t.exec("goKillGuardAtSecondForest-forest1", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3161 } })
            t.ticks(6)
            at_exact("goKillGuardAtSecondForest-forest1-tile", 2217, 3163)
            t.exec("goKillGuardAtSecondForest-forest2", t.player.click_loc, "regicide_cross_over2", 1, { at = { 2216, 3164 } })
            t.ticks(6)
            at_exact("goKillGuardAtSecondForest-forest2-tile", 2217, 3166)
            t.exec("goKillGuardAtSecondForest-forest3", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3167 } })
            t.ticks(6)
            at_exact("goKillGuardAtSecondForest-forest3-tile", 2217, 3169)
            travel("goKillGuardAtSecondForest-walk", 2188, 3172, { { 2217, 3173 }, { 2203, 3180 }, { 2188, 3180 } })
            -- note: goKillGuardAtSecondForest the guard at 2188,3170 is regicide_tyras_camp_guard; the quest credits EITHER guard once (regicide_tyras_guard.rs2:11 and :35 queue regicide_quest_guard_defeated, gated on spoken_tracker2 at :52), and leg 4 already killed regicide_old_camp_guard for real, so by stage 9 the camp guard's kill grants nothing and the camp passage does not summon it (regicide_route.rs2:80)

            -- goIntoTyrasCamp: the camp passage, o2_tyras 2187,3169 / o3 2187,3166 / o1_tyras 2187,3163 (stage 9 -> 10)
            t.exec("goIntoTyrasCamp-forest1", t.player.click_loc, "regicide_cross_over2_tyras_camp", 1, { at = { 2187, 3169 } })
            t.ticks(6)
            at_exact("goIntoTyrasCamp-forest1-tile", 2188, 3168)
            local _, camp_stage = t.var.server("varp328_regicide_quest")
            t.check("quest.stage.entered_camp", camp_stage == 10, "regicide_quest=" .. tostring(camp_stage) .. " :: " .. last_lines(2))
            t.exec("goIntoTyrasCamp-forest2", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2187, 3166 } })
            t.ticks(6)
            at_exact("goIntoTyrasCamp-forest2-tile", 2188, 3165)
            t.exec("goIntoTyrasCamp-forest3", t.player.click_loc, "regicide_cross_over1_tyras_camp", 1, { at = { 2187, 3163 } })
            t.ticks(6)
            at_exact("goIntoTyrasCamp-forest3-tile", 2188, 3162)

            -- enterTyrasCamp: the position-only step at 2190,3144, where an empty barrel lies (m34_49.spawn:67-69)
            travel("enterTyrasCamp", 2190, 3146, { { 2189, 3155 } })
            t.exec("enterTyrasCamp-barrel1", t.player.click_obj, "regicide_barrel_empty", 3)
            t.exec("enterTyrasCamp-barrel1-held", t.inv.await, "regicide_barrel_empty", 1, 10)
            t.exec("enterTyrasCamp-barrel2", t.player.click_obj, "regicide_barrel_empty", 3)
            t.exec("enterTyrasCamp-barrel2-held", t.inv.await, "regicide_barrel_empty", 2, 10)

            -- getSulphur: a piece off the shore south of the old camp (regicide_bombcraft.rs2:33, needs stage 10)
            t.exec("goto-getSulphur", t.player.goto_tile, 2261, 3132, 0)
            t.exec("getSulphur", t.player.click_loc, "regicide_sulphar2", 1, { at = { 2261, 3130 } })
            t.exec("getSulphur-held", t.inv.await, "regicide_sulphar", 1, 10)

            -- fill2Barrels: fill both empty barrels from the tar collection (regicide_bombcraft.rs2:53)
            t.exec("goto-fill2Barrels", t.player.goto_tile, 2263, 3129, 0)
            t.exec("fill2Barrels-1", t.player.click_loc, "regicide_tar_collection", 1, { at = { 2263, 3127 } })
            t.exec("fill2Barrels-1-held", t.inv.await, "regicide_barrel_tar", 1, 10)
            t.exec("fill2Barrels-2", t.player.click_loc, "regicide_tar_collection", 1, { at = { 2263, 3127 } })
            t.exec("fill2Barrels-2-held", t.inv.await, "regicide_barrel_tar", 2, 10)

            -- goToIorwerthAfterCamp: back the way leg 4 came (tracker, the spring on foot, the ring north, the log, the camp)
            travel("goToIorwerthAfterCamp-walk-toTracker", 2257, 3150, { { 2262, 3140 } })
            travel("goToIorwerthAfterCamp-walk-toSpring", 2239, 3181, { { 2250, 3170 } })
            travel("goToIorwerthAfterCamp-walk-toRing", 2221, 3181, { { 2239, 3181 } })
            travel("goToIorwerthAfterCamp-walk-toRing2", 2209, 3201, { { 2211, 3191 } })
            cross("goToIorwerthAfterCamp-ring", "regicide_pitfall_side", 2209, 3201, 2209, 3202, "cross safely")
            t.exec("goto-goToIorwerthAfterCamp-log", t.player.goto_tile, 2201, 3236, 0)
            t.exec("goToIorwerthAfterCamp-log", t.player.click_loc, "regicide_logbalance1_start", 1, { at = { 2201, 3237 } })
            t.ticks(10)
            where("goToIorwerthAfterCamp-log-tile", 2196, 3237, 0)
            t.exec("goto-goToIorwerthAfterCamp-camp", t.player.goto_tile, 2203, 3253, 0)
            t.exec("goToIorwerthAfterCamp-talk", t.player.talk_to, "lord_iorwerth_vis")
            t.exec("goToIorwerthAfterCamp-dialog", t.chat.play, {
                "npc:how goes your search", "player:I've finally tracked", "npc:Good job", "npc:I have this book",
                "player:Well that should", "npc:Indeed", "mesbox:Lord Iorwerth gives you a book",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_iorwerth2", t.quest.expect_stage(11))
            t.expect("book.held", t.inv.expect_has("regicide_alchemy", 1))

            -- readBigBookOfBangs: Read the book (regicide_alchemy.rs2:8), five pages
            t.exec("readBigBookOfBangs", t.player.inv_op, "regicide_alchemy", 1)
            t.exec("readBigBookOfBangs-pages", t.chat.drain, { max_pages = 20 }) -- five mesbox pages, each split into a heading and a body page
            local _, read_flag = t.var.varbit("varb8453_regicide_read_book")
            t.check("readBigBookOfBangs-flag", read_flag == 1, "varb8453_regicide_read_book=" .. tostring(read_flag))

            -- askAboutFuse: Iorwerth, More options... / I need a fuse (lord_iorwerth.rs2:176-190, :196)
            t.exec("askAboutFuse", t.player.talk_to, "lord_iorwerth_vis")
            t.exec("askAboutFuse-dialog", t.chat.play, {
                "player:Hello", "npc:Have you had any luck", "options", "choose:More options...", "options",
                "choose:I need a fuse.", "player:I need a fuse", "npc:Some sort of fabric", "npc:Is there anything else",
            })
            t.chat.close()
            t.ticks(2)
            local _, fuse_chat = t.var.varbit("varb8455_regicide_fuse_chat")
            t.check("askAboutFuse-flag", fuse_chat == 1, "varb8455_regicide_fuse_chat=" .. tostring(fuse_chat))

            -- useLimestoneOnFurnace: ANY furnace burns limestone (smelting.rs2:81-84 -> regicide_bombcraft.rs2:91, stage 11),
            -- gloved so the quicklime does not burn the hands. Keldagrim's furnace is the one other quests already drive.
            t.exec("wear-gloves", t.player.equip, "leather_gloves")
            t.ticks(1)
            t.exec("goto-useLimestoneOnFurnace", t.player.goto_tile, 2869, 10202, 0)
            local furnace_result, furnace_target = t.world.loc_near("dwarf_keldagrim_furnace", 60)
            t.check("useLimestoneOnFurnace-locate", furnace_result == "ok", "world.loc_near(dwarf_keldagrim_furnace,60) -> " .. tostring(furnace_result))
            t.exec("useLimestoneOnFurnace", t.player.use_on, "limestone", furnace_target)
            t.exec("useLimestoneOnFurnace-held", t.inv.await, "regicide_quicklime", 1, 10)

            -- usePestleOnQuicklime / usePestleOnSulphur: grind_ingredient.rs2:84 and :91 (a pot is consumed by the quicklime)
            t.exec("usePestleOnQuicklime", t.player.use_item_on_item, "regicide_quicklime", "pestle_and_mortar")
            t.exec("usePestleOnQuicklime-held", t.inv.await, "regicide_quicklime_dust", 1, 10)
            t.exec("usePestleOnSulphur", t.player.use_item_on_item, "regicide_sulphar", "pestle_and_mortar")
            t.exec("usePestleOnSulphur-held", t.inv.await, "regicide_sulphar_dust", 1, 10)

            -- talkToChemist: Rimmington, "Your quest." (chemist.rs2, biohazard_complete arm, stage 11 with the book)
            t.exec("goto-talkToChemist", t.player.goto_tile, 2934, 3210, 0)
            t.exec("talkToChemist", t.player.talk_to, "chemist")
            t.exec("talkToChemist-dialog", t.chat.play, {
                "options", "choose:Your quest.", "player:Good day. I was hoping", "npc:Ah, you'll be wanting",
                "player:How do I use it", "npc:It's quite simple", "npc:You must also", "player:Is that all",
                "npc:You'll also need plenty", "player:I see, thanks",
            })
            t.ticks(2)
            local _, chemist_chat = t.var.varbit("varb8449_regicide_chemist_chat")
            t.check("talkToChemist-flag", chemist_chat == 1, "varb8449_regicide_chemist_chat=" .. tostring(chemist_chat))

            local _, w = t.world.tile()
            local _, stage = t.quest.stage()
            local _, tar = t.inv.count("regicide_barrel_tar")
            local _, dust = t.inv.count("regicide_quicklime_dust")
            local _, sdust = t.inv.count("regicide_sulphar_dust")
            t.check("leg.5.end", true, "tile " .. w.x .. "," .. w.z .. " level " .. w.level .. ", regicide_quest=" .. tostring(stage)
                .. ", barrel_tar x" .. tostring(tar) .. ", quicklime_dust x" .. tostring(dust) .. ", sulphar_dust x" .. tostring(sdust))
            -- LEG 5 END
        end },
        { name = "bomb_catapult_and_report", run = function(t)
            -- LEG 6 BEGIN: useTarOnFractionalisingStill
            -- Brought along (Quest Helper's item lists for these steps): coal for the still's heat, the strip of cloth
            -- for the fuse and the cooked rabbit for the catapult guard. The pack is bulky, so they are given here.
            t.cheat("::give regicide_cloth 1") -- useClothOnBarrelBomb: Strip of cloth
            t.cheat("::give cooked_rabbit 1") -- useRabbitOnGuard: Cooked rabbit
            t.cheat("::give coal 8") -- coal20OrNaphtha: the still burns coal to hold its heat (28 slots: coal is not stackable)
            t.exec("leg6.pack", t.inv.await_all, { regicide_cloth = 1, cooked_rabbit = 1, coal = 9 }, 10)
            local _, coal_n = t.inv.count("coal")

            t.drive.camera(0, 383, 600)
            t.exec("goto-useTarOnFractionalisingStill", t.player.goto_tile, 2927, 3212, 0)
            t.ui.tab("inventory")
            t.ticks(2)
            local still_target = t.player.by_symbol("loc", "regicide_fractionalizing_still")
            t.exec("useTarOnFractionalisingStill", t.player.use_on, "regicide_barrel_tar", still_target)
            local open_result = t.ui.await_open("regicide_still")
            t.check("operateStill-open", open_result == "ok", "ui.await_open(regicide_still) -> " .. tostring(open_result))

            -- operateStill: every control on interface 286 is an IF1 graphic button (op 0, trap 33). Tar valve to
            -- the top, back the pressure off once the flow climbs, and feed coal whenever the heat needle drops below
            -- the green band (bits 13-25 of varp331_regicide_still_settings), reading the server after each press.
            local _, tar_up = t.ui.widget("regicide_still:regicide_tar_valve_up")
            local _, pressure_up = t.ui.widget("regicide_still:regicide_pressure_valve_up")
            local _, add_coal = t.ui.widget("regicide_still:regicide_add_coal")
            t.ui.invoke(tar_up, 0)
            t.ui.invoke(tar_up, 0)
            local coal_presses, pressure_presses, total, polls = 0, 0, 0, 0
            while polls < 40 and total < 26 do
                polls = polls + 1
                local settings_r, settings = t.var.server("varp331_regicide_still_settings")
                if settings_r == "ok" and settings ~= nil then
                    if settings < 0 then settings = settings + 4294967296 end
                    local function bit(n) return math.floor(settings / (2 ^ n)) % 2 == 1 end
                    local tar_at_max = bit(31)
                    local pressure_at_base = bit(26)
                    local flow_high = bit(10) or bit(11) or bit(12)
                    local heat_below_green = bit(13) or bit(14) or bit(15) or bit(16) or bit(17) or bit(18)
                    if not tar_at_max then t.ui.invoke(tar_up, 0) end
                    if tar_at_max and pressure_at_base and flow_high then
                        t.ui.invoke(pressure_up, 0)
                        pressure_presses = pressure_presses + 1
                    end
                    if heat_below_green and coal_presses < coal_n then
                        t.ui.invoke(add_coal, 0)
                        coal_presses = coal_presses + 1
                    end
                end
                t.ticks(2)
                local total_r, total_v = t.var.server("varp330_regicide_still_total")
                if total_r == "ok" and total_v ~= nil then total = total_v end
            end
            t.check("operateStill", total >= 26, string.format("varp330_regicide_still_total=%s after %d poll(s), %d coal, %d pressure press(es)",
                tostring(total), polls, coal_presses, pressure_presses))
            t.key("escape") -- the close icon fires but never unmounts the panel; Escape is the player's own close
            local closed = t.ui.await_close("regicide_still")
            t.check("operateStill-closed", closed == "ok", "ui.await_close(regicide_still) -> " .. tostring(closed))
            t.expect("operateStill-naphtha", t.inv.await("regicide_barrel_naphtha", 1, 10))

            -- useQuicklimeOnNaphtha / useGroundSulphurOnNaphtha: regicide_bombcraft.rs2:156-192
            t.ui.tab("inventory")
            t.ticks(1)
            t.exec("useQuicklimeOnNaphtha", t.player.use_item_on_item, "regicide_quicklime_dust", "regicide_barrel_naphtha")
            t.expect("useQuicklimeOnNaphtha-held", t.inv.await("regicide_barrel_naphtha_quicklime_mix", 1, 10))
            t.exec("useGroundSulphurOnNaphtha", t.player.use_item_on_item, "regicide_sulphar_dust", "regicide_barrel_naphtha_quicklime_mix")
            t.expect("useGroundSulphurOnNaphtha-held", t.inv.await("regicide_barrel_lid", 1, 10))
            t.exec("useClothOnBarrelBomb", t.player.use_item_on_item, "regicide_cloth", "regicide_barrel_lid")
            t.expect("useClothOnBarrelBomb-held", t.inv.await("regicide_barrel_lid_fused", 1, 10))

            -- goThroughUndergroundPassAgain: the guide sends the player back through the pass, every obstacle again
            -- (the loc triggers are the ones legs 1-3 drove). A plain row per obstacle, named with -again.
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
            local function at_exact(label, want_x, want_z)
                local _, w = t.world.tile()
                t.check(label, w.x == want_x and w.z == want_z, "at " .. w.x .. "," .. w.z .. "," .. w.level .. " want " .. want_x .. "," .. want_z .. " :: " .. last_lines(3))
            end
            local function eat_if_low()
                local _, food = t.inv.count("lobster")
                if food == 0 then
                    t.cheat("::give lobster 6") -- brought-along food (setup gives six); the hazards used it up
                    t.ticks(2)
                end
                t.player.inv_op("lobster", 1)
                t.ticks(3)
            end
            local function cross(name, sym, stand_x, stand_z, at_x, at_z, success_text)
                local crossed = false
                local from_x, from_z
                for attempt = 1, 10 do
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
                    eat_if_low()
                end
                local _, here = t.world.tile()
                t.check(name .. "-crossed", crossed, "from " .. tostring(from_x) .. "," .. tostring(from_z) .. " to " .. here.x .. "," .. here.z .. " :: " .. last_lines(3))
            end
            local function travel(label, x, z, via)
                for _, pt in ipairs(via or {}) do
                    t.player.walk_to(pt[1], pt[2], 40)
                    t.ticks(1)
                end
                for _ = 1, 4 do
                    local _, w = t.world.tile()
                    if math.abs(w.x - x) <= 2 and math.abs(w.z - z) <= 2 then break end
                    t.player.walk_to(x, z, 40)
                    t.ticks(1)
                end
                where(label, x, z, 0)
            end

            -- the spare coal and the empty barrel would crowd the pack for the pass's items
            for _ = 1, 9 do t.player.drop("coal") t.ticks(1) end
            t.player.drop("regicide_barrel_empty")
            t.ticks(1)
            t.exec("leg6.pack-lean", t.inv.count, "coal")
            t.cheat("::give rope 1") -- enterTheDungeon items: Rope (the pit's swing; the first walk's rope was spent)
            t.exec("leg6.rope", t.inv.await, "rope", 1, 10)

            -- enterTheDungeon (again): upass_entrance.rs2:9
            t.exec("goto-enterTheDungeon-again", t.player.goto_tile, 2434, 3314, 0)
            t.exec("enterTheDungeon-again", t.player.click_loc, "upass_caveentrance2", 1)
            t.ticks(6)
            local _, at = t.world.tile()
            t.check("enterTheDungeon-again-tile", at.z > 9000, "underground at " .. at.x .. "," .. at.z .. " level " .. at.level .. " :: " .. last_lines(2))

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
            climb("climbOverRockslide1-again", 2480, 9713)
            t.exec("goto-climbOverRockslide2-again", t.player.goto_tile, 2473, 9706, 0)
            climb("climbOverRockslide2-again", 2471, 9706)
            climb("climbOverRockslide3-again", 2458, 9712)

            -- searchBagForCloth, useClothOnArrow, lightArrow, shootBridgeRope (again)
            t.exec("goto-searchBagForCloth-again", t.player.goto_tile, 2452, 9715, 0)
            t.exec("searchBagForCloth-again", t.player.click_loc, "upass_gear", 1)
            t.ticks(4)
            local crossed = false
            for attempt = 1, 5 do
                local sfx = attempt == 1 and "-again" or ("-again-retry" .. attempt)
                if attempt > 1 then
                    t.exec("goto-searchAgain" .. sfx, t.player.goto_tile, 2453, 9716, 0)
                    t.exec("searchBagForCloth" .. sfx, t.player.click_loc, "upass_gear", 1)
                    t.ticks(4)
                end
                t.exec("useClothOnArrow" .. sfx, t.player.use_item_on_item, "damp_cloth", "bronze_arrow")
                t.ticks(2)
                t.exec("lightArrow" .. sfx, t.player.use_item_on_item, "tinderbox", "unlitarrow")
                t.ticks(2)
                if attempt == 1 then t.exec("wieldBow-again", t.player.equip, "shortbow") end
                t.exec("wieldLitArrow" .. sfx, t.player.equip, "litarrow")
                t.ticks(2)
                t.exec("walkNorthEastOfBridge" .. sfx, t.player.goto_tile, 2450, 9722, 0)
                t.exec("shootBridgeRope" .. sfx, t.player.click_loc, "oldbridge_guiderope", 1)
                for _poll = 1, 12 do
                    t.ticks(4)
                    _, at = t.world.tile()
                    if at.x < 2444 then break end
                end
                _, at = t.world.tile()
                if at.x < 2444 then crossed = true break end
            end
            t.check("shootBridgeRope-again-crossed", crossed, "after the shot at " .. at.x .. "," .. at.z .. " level " .. at.level .. " :: " .. last_lines(4))

            -- crossThePit (again): rope on the rock (upass_obstacles.rs2:110)
            local swung = false
            for attempt = 1, 8 do
                local sfx = attempt == 1 and "-again" or ("-again-retry" .. attempt)
                t.exec("goto-crossThePit" .. sfx, t.player.goto_tile, 2461, 9699, 0)
                local rock = t.player.by_symbol("loc", "obstical_rockswing_norope")
                t.exec("crossThePit" .. sfx, t.player.use_on, "rope", rock)
                t.ticks(10)
                local _, here = t.world.tile()
                if here.x >= 2464 then swung = true break end
                t.cheat("::give rope 1")
                t.ticks(2)
            end
            t.check("crossThePit-again-crossed", swung, "after the swing :: " .. last_lines(4))
            t.blocked("ROUND 3 (orchestrator): the second walk's grid -- cross it as leg 3 does (safe bands from %varp6010_upass_grid_pattern, rockslides 4/5 by click_loc) instead of goto 2466,9673")
            t.exec("pullLeverAfterGrid-again", t.player.click_loc, "portcullis_lever_up", 1)
            t.ticks(8)
            where("pullLeverAfterGrid-again-tile")

            local traps = {
                { "passTrap1-again", 2445, 9677, 2443 }, { "passTrap2-again", 2442, 9677, 2440 },
                { "passTrap3-again", 2436, 9675, 2435 }, { "passTrap4-again", 2434, 9675, 2432 }, { "passTrap5-again", 2433, 9675, 2431 },
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
                    if here.x <= want_x then passed = true break end
                    failures = failures + 1
                    if failures % 3 == 0 then
                        t.player.inv_op("lobster", 1)
                        t.ticks(2)
                    end
                end
                local _, here = t.world.tile()
                t.check(name .. "-tile", passed, "standing at " .. here.x .. "," .. here.z .. " level " .. here.level .. " :: " .. last_lines(3))
            end

            -- collectPlank, climbDownWell, pickCellLock, digMud, crossLedge, goThroughPipe, leaveUnicornArea, openIbansDoor (again)
            t.exec("goto-collectPlank-again", t.player.goto_tile, 2434, 9725, 0)
            local _, planks_before = t.inv.count("woodplank")
            t.exec("collectPlank-again", t.player.click_obj, "woodplank", 3)
            t.ticks(2)
            local _, planks = t.inv.count("woodplank")
            t.check("collectPlank-again-inv", planks == planks_before + 1, "woodplank " .. tostring(planks_before) .. " -> " .. tostring(planks))
            t.exec("goto-climbDownWell-again", t.player.goto_tile, 2417, 9673, 0)
            t.exec("climbDownWell-again", t.player.click_loc, "cave_well", 1)
            t.ticks(6)
            where("climbDownWell-again-tile", 2423, 9660, 0)
            t.exec("goto-pickCellLock-again", t.player.goto_tile, 2393, 9657, 0)
            local picked = false
            for attempt = 1, 16 do -- two railings stand on x 2393 (z 9656 then z 9655); each pick can fail
                local _, before = t.world.tile()
                t.player.click_loc("cave_railings2", 1, { at = { 2393, before.z >= 9657 and 9656 or 9655 } }) -- upass_unicorn.rs2:11
                t.ticks(8)
                local _, after = t.world.tile()
                if after.z <= 9654 then picked = true end
                if picked then break end
            end
            where("pickCellLock-again", nil)
            t.ticks(1)
            local mud = t.player.by_symbol("loc", "upass_mud")
            t.exec("digMud-again", t.player.use_on, "spade", mud)
            t.ticks(8)
            where("digMud-again-tile", 2392, 9646, 0)
            t.exec("goto-crossLedge-again", t.player.goto_tile, 2376, 9644, 0)
            t.exec("crossLedge-again", t.player.click_loc, "upass_ledge", 1)
            t.ticks(8)
            where("crossLedge-again-tile")
            t.blocked("ROUND 3 (orchestrator): the second walk's maze -- as in leg 2, no goto 2420,9605")
            t.exec("goThroughPipe-again", t.player.click_loc, "upass_pipe6", 1)
            t.ticks(8)
            where("goThroughPipe-again-tile", 2390, 9605, 0)
            t.exec("goto-leaveUnicornArea-again", t.player.goto_tile, 2373, 9611, 0)
            t.exec("leaveUnicornArea-again", t.player.click_loc, "upass_unicorn_doorl", 1)
            t.ticks(6)
            where("leaveUnicornArea-again-tile")
            t.exec("goto-openIbansDoor-again", t.player.goto_tile, 2369, 9718, 0)
            t.exec("openIbansDoor-again", t.player.click_loc, "cavetempledoor2r", 1)
            t.ticks(6)
            where("openIbansDoor-again-tile")
            t.blocked("ROUND 3 (orchestrator): the second walk's temple -- bridges and the door as in leg 2, no goto 2010,4709")
            t.exec("enterWell-again", t.player.click_loc, "regicide_voyage_temple_well1", 1)
            t.ticks(6)
            where("enterWell-again-tile", 2343, 9622, 0)
            t.exec("goto-leaveWellCave-again", t.player.goto_tile, 2315, 9624, 0)
            t.exec("leaveWellCave-again", t.player.click_loc, "regicide_voyage_temple_exit", 1)
            t.ticks(6)
            where("leaveWellCave-again-tile", 2312, 3216, 0)
            t.check("goThroughUndergroundPassAgain", true, "the pass was walked again: dungeon, rockslides, bridge, pit, lever, five traps, plank, well, maze, Iban's door, the temple well and the voyage cave's exit; regicide_quest=" .. tostring(select(2, t.quest.stage())))

            -- Tirannwn again: the hazards from the arrival to the camp road (regicide_traps.rs2), then the middle passage
            cross("goFromCaveToLeaves-again", "regicide_pitfall_side", 2267, 3205, 2267, 3204, "cross safely")
            cross("goFromLeavesToStickTrap-again", "regicide_trap_woodspring", 2234, 3181, 2235, 3181, "skillfully pass")
            -- goGiveRabbitToGuard: the three dense forests north of the tripwire, o3 2216,3161 / o2 2216,3164 / o3 2216,3167
            -- (a cross_over3 landing in this mapsquare forgets the rabbit, regicide_route.rs2:172, so they come first)
            t.exec("goto-goGiveRabbitToGuard", t.player.goto_tile, 2217, 3160, 0)
            t.exec("goGiveRabbitToGuard-forest1", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3161 } })
            t.ticks(6)
            at_exact("goGiveRabbitToGuard-forest1-tile", 2217, 3163)
            t.exec("goGiveRabbitToGuard-forest2", t.player.click_loc, "regicide_cross_over2", 1, { at = { 2216, 3164 } })
            t.ticks(6)
            at_exact("goGiveRabbitToGuard-forest2-tile", 2217, 3166)
            t.exec("goGiveRabbitToGuard-forest3", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3167 } })
            t.ticks(6)
            at_exact("goGiveRabbitToGuard-forest3-tile", 2217, 3169)
            travel("goGiveRabbitToGuard-walk2", 2185, 3183, { { 2217, 3173 }, { 2203, 3180 }, { 2188, 3180 } })

            -- useRabbitOnGuard: the lazy guard at the catapult eats it (regicide_tyras_lazy_guard.rs2:51-56)
            local guard = t.player.by_symbol("npc", "regicide_tyras_lazy_guard_vis")
            t.exec("useRabbitOnGuard", t.player.use_on, "cooked_rabbit", guard)
            t.exec("useRabbitOnGuard-dialog", t.chat.play, {
                "player:Here, I caught this", "npc:You cooked me a rabbit", "player:No problem",
            })
            t.ticks(2)
            local _, fed = t.var.varbit("varb8447_regicide_given_rabbit")
            t.check("useRabbitOnGuard-flag", fed == 1, "varb8447_regicide_given_rabbit=" .. tostring(fed))

            -- useBombOnCatapult: the fused barrel on the catapult (regicide_bombcraft.rs2:215); the player is carried to the
            -- tent and back at the end of the scene
            local catapult = t.player.by_symbol("loc", "regicide_catapult_right")
            t.exec("useBombOnCatapult", t.player.use_on, "regicide_barrel_lid_fused", catapult)
            t.exec("useBombOnCatapult-stage", t.var.await, "varp328_regicide_quest", 12, 60)
            t.ticks(4)
            t.expect("quest.stage.killed_tyras", t.quest.expect_stage(12))

            -- leaveFromCatapult: back east along the camp road, the three forests southward, then the tripwire
            -- (regicide_traps.rs2:19) -- "Go to the east, then south to the traps and cross them."
            travel("leaveFromCatapult-walk", 2217, 3170, { { 2188, 3180 }, { 2203, 3180 }, { 2217, 3173 } })
            t.exec("leaveFromCatapult-forest1", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3167 } })
            t.ticks(6)
            at_exact("leaveFromCatapult-forest1-tile", 2217, 3166)
            t.exec("leaveFromCatapult-forest2", t.player.click_loc, "regicide_cross_over2", 1, { at = { 2216, 3164 } })
            t.ticks(6)
            at_exact("leaveFromCatapult-forest2-tile", 2217, 3163)
            t.exec("leaveFromCatapult-forest3", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3161 } })
            t.ticks(6)
            at_exact("leaveFromCatapult-forest3-tile", 2217, 3160)
            cross("leaveFromCatapult", "regicide_trap_tripwire", 2220, 3155, 2220, 3153, "step over")
            -- the road to Iorwerth runs north of the tripwire: cross it back (leg 4's crossTripwire, from the south)
            t.exec("goto-leaveFromCatapult-back", t.player.goto_tile, 2220, 3152, 0)
            t.exec("leaveFromCatapult-back", t.player.click_loc, "regicide_trap_tripwire", 1)
            t.ticks(6)
            local _, tw = t.world.tile()
            t.check("leaveFromCatapult-back-tile", tw.z >= 3155, "player " .. tw.x .. "," .. tw.z .. " :: " .. last_lines(3))

            -- goTalkToIorwerthAfterRegicide: the ring of leaves, the log, the camp
            cross("goTalkToIorwerthAfterRegicide-ring", "regicide_pitfall_side", 2209, 3201, 2209, 3202, "cross safely")
            t.exec("goto-goTalkToIorwerthAfterRegicide-log", t.player.goto_tile, 2201, 3236, 0)
            t.exec("goTalkToIorwerthAfterRegicide-log", t.player.click_loc, "regicide_logbalance1_start", 1, { at = { 2201, 3237 } })
            t.ticks(10)
            where("goTalkToIorwerthAfterRegicide-log-tile", 2196, 3237, 0)
            t.exec("goto-talkToIorwerthAfterRegicide", t.player.goto_tile, 2203, 3253, 0)
            t.exec("talkToIorwerthAfterRegicide", t.player.talk_to, "lord_iorwerth_vis")
            t.exec("talkToIorwerthAfterRegicide-dialog", t.chat.play, {
                "player:Lord Iorwerth, it is done", "npc:Good good", "npc:I'm sure you will want",
                "mesbox:Lord Iorwerth gives you a scroll", "npc:As a token", "player:Thank you my lord",
            })
            t.ticks(2)
            t.expect("quest.stage.reported_iorwerth", t.quest.expect_stage(13))
            t.expect("message.held", t.inv.expect_has("regicide_iorwerth_message", 1))

            -- talkToArianwyn: outside Ardougne Castle, the scene fires on walking into his zone with the message
            -- (regicide_route.rs2:176). The way home is plain travel: the guide names no step for it.
            t.exec("goto-talkToArianwyn", t.player.goto_tile, 2579, 3298, 0)
            t.player.walk_to(2586, 3298, 20)
            for _poll = 1, 12 do
                if t.chat.kind() ~= "none" then break end
                t.ticks(1)
            end
            t.check("talkToArianwyn", t.chat.kind() ~= "none", "Arianwyn's scene opened on walking in, page kind " .. tostring(t.chat.kind()))
            t.exec("talkToArianwyn-dialog", t.chat.play, {
                "npc:Are you the human", "player:Yes, that's me", "npc:Thank Seren", "player:What do you mean",
                "npc:I am Arianwyn", "npc:There is much to explain", "player:Well you seem to know",
                "npc:Good, we understand", "mesbox:You show the message", "mesbox:King Lathas", "player:I had no idea",
                "npc:I have a few ideas", "npc:Once you are done", "player:You want me to help", "npc:The chance for redemption",
                "npc:This isn't a struggle", "npc:Deliver your message",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_arianwyn", t.quest.expect_stage(14))

            -- goTalkToLathasToFinish: the castle stairs, then King Lathas on the second floor
            t.exec("goto-goToArdougneCastleFloor2-finish", t.player.goto_tile, 2572, 3295, 0)
            t.exec("goToArdougneCastleFloor2-finish", t.player.click_loc, "stairs", 1)
            t.ticks(6)
            t.exec("goto-goTalkToLathasToFinish", t.player.goto_tile, 2578, 3293, 1)
            t.exec("goTalkToLathasToFinish", t.player.talk_to, "kinglathas", 1)
            local _, coins_before = t.inv.count("coins")
            local _, xp_snapshot = t.skill.snapshot()
            t.exec("goTalkToLathasToFinish-dialog", t.chat.play, {
                "player:My lord, Tyras is dead", "npc:This is grand news", "player:Yes, I have a letter",
                "mesbox:You hand the king", "npc:Yes... Good", "player:Does this mean", "npc:Not yet", "npc:Anyway",
            })
            t.ticks(4)
            t.expect("reward.agility", t.skill.expect_gain("agility", 13750, xp_snapshot))
            local _, coins_after = t.inv.count("coins")
            t.check("reward.coins", coins_after - coins_before == 15000, "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. " (15000 documented)")
            t.quest.expect_complete()
            t.finish(0)
            -- LEG 6 END
        end },
    },
}
