-- Underground Pass (upass). Relay file: one leg per author, legs table of docs/quest_authoring/relay.md.
-- Scaffolded by tools/quest_gate/new_quest.py, hand-corrected against quests/quest_upass/scripts/.
-- Tier (quest_inventory.tsv): 3.

return {
    id = "upass",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- the whole relay is ~5000 server ticks; the default 60000 frames stops at ~2000
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        -- Quest Helper: Underground Pass needs 25 Ranged (king_lathas.rs2 gates the start on stat_base(ranged) >= 25)
        "::setlevel ranged 25",
        -- Quest Helper: Biohazard is a prerequisite (upass_entrance.rs2:9 refuses the cave until it is complete)
        "::complete quest_biohazard",
        -- Quest Helper items for the bridge leg: a bow (not crossbow), metal arrows, a tinderbox and a rope
        "::give shortbow",
        "::give bronze_arrow 5",
        "::give tinderbox",
        "::give rope",
    },
    bind = {
        varp = "varp161_upass",
        constants = {
            not_started = 0,
            spoken_koftik = 1,
            passed_bridge = 2,
            complete = 10,
        },
        row = "quest_undergroundpass",
        display = "Underground Pass",
        points = 5,
    },
    legs = {
        { name = "koftik_and_first_rockslides", run = function(t)
            -- LEG 1 BEGIN: goToArdougneCastleFloor2
            t.ticks(3) -- a setup cheat's effect is not client-side yet
            t.expect("upass.reset", t.quest.expect_stage("not_started"))

            t.exec("goto-goToArdougneCastleFloor2", t.player.goto_tile, 2572, 3295, 0)
            t.exec("goToArdougneCastleFloor2", t.player.click_loc, "stairs", 1) -- ladders.rs2:175
            t.ticks(3)
            local _, at = t.world.tile()
            t.check("goToArdougneCastleFloor2-level", at.level == 1, "tile after the stairs " .. at.x .. "," .. at.z .. " level " .. at.level)

            t.exec("goto-talkToKingLathas", t.player.goto_tile, 2578, 3292, 1)
            t.exec("talkToKingLathas", t.player.talk_to, "kinglathas", 1) -- king_lathas.rs2:35
            t.exec("talkToKingLathas-dialog", t.chat.play, {
                "player:Hello King Lathas",
                "npc:Adventurer, thank Saradomin",
                "player:Have your scouts found",
                "npc:Not quite, we found a path",
                "npc:However during recent times",
                "player:Iban",
                "npc:A crazy loon",
                "npc:Go meet my main tracker",
                "npc:We must find a way",
                "player:I'll do my best",
                "npc:A warning traveller",
            })
            t.ticks(2)
            t.check("talkToKingLathas-met", select(2, t.var.varbit("varb9125_upass_lathas_met")) == 1, "upass_lathas_met varbit read " .. tostring(select(2, t.var.varbit("varb9125_upass_lathas_met"))))

            t.exec("goto-goDownCastleStairs", t.player.goto_tile, 2572, 3295, 1)
            t.exec("goDownCastleStairs", t.player.click_loc, "stairstop", 1) -- ladders.rs2:178
            t.ticks(3)
            _, at = t.world.tile()
            t.check("goDownCastleStairs-level", at.level == 0, "tile after the stairs " .. at.x .. "," .. at.z .. " level " .. at.level)

            t.exec("goto-enterWestArdougne", t.player.goto_tile, 2559, 3300, 0)
            t.exec("enterWestArdougne", t.player.click_loc, "ardougnedoor_r", 1) -- doors.rs2:89
            t.ticks(3)

            t.exec("goto-talkToKoftik", t.player.goto_tile, 2437, 3314, 0)
            t.exec("talkToKoftik", t.player.talk_to, "caveguide1", 1) -- koftik.rs2:14
            t.exec("talkToKoftik-dialog", t.chat.play, {
                "player:Hello there, are you the King",
                "npc:That I am",
                "npc:I'm afraid you'll have to go",
                "player:That's OK",
                "npc:These caves are different",
                "npc:You can feel it",
                "npc:Not so many travellers",
                "choose:I'll take my chances.",
                "player:I'll take my chances",
                "npc:Okay traveller",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_koftik", t.quest.expect_stage("spoken_koftik"))

            t.exec("goto-enterTheDungeon", t.player.goto_tile, 2434, 3314, 0)
            t.exec("enterTheDungeon", t.player.click_loc, "upass_caveentrance2", 1) -- upass_entrance.rs2:9
            t.ticks(6)
            _, at = t.world.tile()
            t.check("enterTheDungeon-tile", at.z > 9000, "underground tile " .. at.x .. "," .. at.z .. " level " .. at.level)

            -- The rockslide slips the climber back on a failed agility roll (upass_obstacles.rs2:38), so each
            -- slide is clicked until the player stands on its far side.
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
            t.exec("goto-climbOverRockslide2", t.player.goto_tile, 2473, 9706, 0)
            climb("climbOverRockslide2", 2471, 9706)
            climb("climbOverRockslide3", 2458, 9712)

            -- THE SWAMP FALL, where the guide and LostCity put it (b52, owner: fix against LostCity).
            -- Quest Helper UndergroundPass.java:591-594 crossTheBridge: inFallArea -> leaveFallArea, then
            -- isBeforeRockslide1 -> climbOverRockslide1..3. LostCity upass_obstacles.rs2:33-48 drops the player from
            -- upass_swampbubbles1 to 2485,9649 (15% of hitpoints); the five pocket rockslides (m38_150.jm2) lead west to
            -- caverockpile (:50-55), which surfaces at 2482,9715 -- beside rockslide 1 -- so the way on is climbing
            -- rockslides 1-3 again, on foot. (Before b52 the fall sat in leg 4 and a goto went back to the well.)
            t.cheat("::give lobster 10") -- food for the fall (15% hp) and the slips (3 each), as leg 4 carried before
            t.ticks(2)
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function hp_now()
                local r, row = t.skill.read("hitpoints")
                return r == "ok" and row.level or 0
            end
            -- leaveFallArea (guide 6.52; QH crossTheBridge: inFallArea 2440,9628-2486,9657 -> leaveFallArea). The pocket is
            -- entered by the swamp (upass_swampbubbles1, upass_obstacles.rs2:59 -> 2485,9649) or a failed rope swing (:132,
            -- same tile). It is NOT sealed: the way out is five rockslide2_obstacle_upass climbs (op1 Climb-over,
            -- @rockslide_obstacle :29) on the guide's line points, then the caverockpile at 2443,9651 (:77 -> 2482,9715).
            -- LostCity m38_150.jm2 places the same five slides and pile. Seam upass_fall_pocket_exit (matthew-mbp-m4-b49-seam1).
            local function fall_here()
                local _, w = t.world.tile()
                return w
            end
            t.exec("walk-enterSwampBubbles", t.player.walk_to, 2453, 9716, 30) -- Koftik's ledge, just past rockslide 3
            t.exec("enterSwampBubbles", t.player.click_loc, "upass_swampbubbles1", 1, { at = { 2465, 9713 } })
            t.await({ level = function() return fall_here().z < 9660 end, note = "landed in the fall pocket" }, 20)
            t.ticks(2)
            local landed = fall_here()
            t.check("enterSwampBubbles-landed", landed.x == 2485 and landed.z == 9649, "after the swamp at " .. landed.x .. "," .. landed.z .. " level " .. landed.level .. " :: " .. last_lines(3))
            local fall_slides = {
                { 2479, 9629, 2480, 9629, 2478, 9629, { { 2485, 9645 }, { 2483, 9642 }, { 2483, 9635 }, { 2481, 9629 }, { 2480, 9629 } } },
                { 2467, 9646, 2468, 9646, 2466, 9646, { { 2476, 9636 }, { 2474, 9637 }, { 2470, 9635 }, { 2467, 9637 }, { 2467, 9639 }, { 2471, 9642 }, { 2471, 9646 }, { 2468, 9646 } } },
                { 2456, 9633, 2457, 9633, 2455, 9633, { { 2465, 9646 }, { 2460, 9640 }, { 2460, 9633 }, { 2457, 9633 } } },
                { 2455, 9647, 2456, 9647, 2454, 9647, { { 2452, 9637 }, { 2452, 9640 }, { 2459, 9645 }, { 2459, 9647 }, { 2456, 9647 } } },
                { 2448, 9650, 2449, 9650, 2447, 9650, { { 2449, 9650 } } },
            }
            for i, s in ipairs(fall_slides) do
                for _, hop in ipairs(s[7]) do t.player.walk_to(hop[1], hop[2], 30) end
                local note, over = "", false
                for attempt = 1, 10 do
                    local w = fall_here()
                    if not (w.x == s[3] and w.z == s[4]) then t.player.walk_to(s[3], s[4], 30) end
                    if hp_now() <= 12 and select(2, t.inv.count("lobster")) > 0 then t.player.inv_op("lobster", 1) t.ticks(3) end
                    local r = t.player.click_loc("rockslide2_obstacle_upass", 1, { at = { s[1], s[2] } })
                    t.ticks(6)
                    local after = fall_here()
                    note = note .. "[" .. attempt .. " " .. tostring(r) .. " -> " .. after.x .. "," .. after.z .. "] "
                    if after.x == s[5] and after.z == s[6] then over = true break end
                end
                t.check("leaveFallArea-rockslide" .. i, over, "slide " .. s[1] .. "_" .. s[2] .. " now " .. fall_here().x .. "," .. fall_here().z .. "; hp " .. hp_now() .. " :: " .. note .. last_lines(2))
            end
            t.exec("leaveFallArea", t.player.click_loc, "caverockpile", 1, { at = { 2443, 9651 } }) -- upass_obstacles.rs2:77
            t.await({ level = function() return fall_here().z > 9700 end, note = "surfaced by the swamp" }, 15)
            t.ticks(2)
            local out = fall_here()
            t.check("leaveFallArea-surfaced", out.x == 2482 and out.z == 9715, "after the pile at " .. out.x .. "," .. out.z .. " level " .. out.level .. " :: " .. last_lines(3))
            -- surfaced at 2482,9715, before rockslide 1: climb the three again, as the guide's crossTheBridge sends you
            climb("climbOverRockslide1-again", 2480, 9713)
            t.exec("goto-climbOverRockslide2-again", t.player.goto_tile, 2473, 9706, 0)
            climb("climbOverRockslide2-again", 2471, 9706)
            climb("climbOverRockslide3-again", 2458, 9712)

            t.exec("goto-talkToKoftikAtBridge", t.player.goto_tile, 2452, 9715, 0)
            t.exec("talkToKoftikAtBridge", t.player.talk_to, "caveguide2", 1) -- koftik.rs2 [opnpc1,caveguide2]
            t.exec("talkToKoftikAtBridge-dialog", t.chat.play, {
                "player:Koftik, how can we cross the bridge",
                "npc:I'm not sure",
                "npc:I found this cloth",
                "player:Charred arrows",
                "player:Interesting",
                "npc:I have also found the remains",
                "choose:Not to worry, probably just litter.",
                "player:Not to worry",
                "npc:Well.. maybe",
            })
            t.ticks(2)
            local _, cloths = t.inv.count("damp_cloth")
            _, at = t.world.tile()
            t.check("leg.1.state", cloths == 1 and t.world.level() ~= nil, "at " .. at.x .. "," .. at.z .. " level " .. at.level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. "; damp_cloth x" .. tostring(cloths))
            -- LEG 1 END
        end },
        { name = "bridge_and_grid", run = function(t)
            -- LEG 2 BEGIN: searchBagForCloth
            local _, at = t.world.tile()
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
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
                t.exec("goto-walkNorthEastOfBridge" .. sfx, t.player.goto_tile, 2450, 9722, 0)
                t.exec("shootBridgeRope" .. sfx, t.player.click_loc, "oldbridge_guiderope", 1) -- upass_bridge.rs2:64
                for _ = 1, 12 do -- the crossing is a run of forcewalks, then a teleport
                    t.ticks(4)
                    if select(2, t.quest.stage()) == 2 then break end
                end
                _, at = t.world.tile()
                if select(2, t.quest.stage()) == 2 then crossed = true break end
            end
            t.check("shootBridgeRope-crossed", crossed, "after the shot at " .. at.x .. "," .. at.z .. " level " .. at.level .. " stage " .. tostring(select(2, t.quest.stage())) .. " :: " .. last_lines(4))
            t.expect("quest.stage.passed_bridge", t.quest.expect_stage("passed_bridge"))

            t.exec("goto-crossThePit", t.player.goto_tile, 2461, 9699, 0)
            local pit_rock = t.player.by_symbol("loc", "obstical_rockswing_norope")
            t.exec("crossThePit", t.player.use_on, "rope", pit_rock) -- upass_obstacles.rs2:110
            t.ticks(12)
            _, at = t.world.tile()
            t.check("crossThePit-landed", at.x >= 2465, "after the swing at " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            local function other_side_lines()
                local _, lines = t.msg.last(12)
                local n = 0
                for _, line in ipairs(lines or {}) do
                    if string.find(tostring(type(line) == "table" and line.text or line), "step down the other side", 1, true) then n = n + 1 end
                end
                return n
            end
            local function climb(name, slide_x, slide_z)
                local crossed_slide = false
                local detail = ""
                for attempt = 1, 8 do
                    local before = other_side_lines()
                    local result = t.player.click_loc("rockslide2_obstacle_upass", 1, { at = { slide_x, slide_z } })
                    t.ticks(8)
                    local _, here = t.world.tile()
                    detail = "attempt " .. attempt .. ": " .. tostring(result) .. " now at " .. here.x .. "," .. here.z
                    if other_side_lines() > before then
                        crossed_slide = true
                        detail = detail .. " (server: ...and step down the other side)"
                        break
                    end
                end
                t.check(name, crossed_slide, detail)
            end
            climb("climbOverRockslide4", 2491, 9691)
            climb("climbOverRockslide5", 2482, 9679)
            -- crossTheGrid: %upass_grid_pattern (set at the quest start by ~setupassgrilltrap, king_lathas.rs2:129)
            -- names one safe 2-row band per column group (upass_grid.rs2:72-96): the timer fails a player who is
            -- outside all three bands. Band of digit d is z 9673+2(d-1) .. +1; col1 x 2473-2478, col3 x 2469-2474, col5 x 2467-2470.
            local _, pattern = t.var.server("varp6010_upass_grid_pattern")
            pattern = tonumber(pattern) or 0
            local d1, d2, d3 = math.floor(pattern / 100) % 10, math.floor(pattern / 10) % 10, pattern % 10
            local function band_z(d) return 9673 + 2 * (d - 1) end
            local function walk_grid(x, z)
                t.player.walk_to(x, z, 14)
                t.ticks(2)
                local _, here = t.world.tile()
                return here
            end
            t.check("crossTheGrid-pattern", d1 >= 1 and d1 <= 5 and d2 >= 1 and d3 >= 1, "upass_grid_pattern=" .. tostring(pattern) .. " -> safe bands z " .. band_z(d1) .. " / " .. band_z(d2) .. " / " .. band_z(d3))
            local path = { { 2478, band_z(d1) }, { 2473, band_z(d1) }, { 2473, band_z(d2) }, { 2469, band_z(d2) }, { 2469, band_z(d3) }, { 2467, band_z(d3) }, { 2466, band_z(d3) } }
            local trail = {}
            for _, wp in ipairs(path) do
                local here = walk_grid(wp[1], wp[2])
                trail[#trail + 1] = here.x .. "," .. here.z
            end
            _, at = t.world.tile()
            t.check("crossTheGrid", at.x <= 2467 and not string.find(last_lines(6), "trap", 1, true), "walked the safe bands " .. table.concat(trail, " > ") .. " -> " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            t.player.walk_to(2466, 9673, 10)
            t.ticks(2)
            _, at = t.world.tile()
            t.check("goto-pullLeverAfterGrid", at.x == 2466 and at.z <= 9674, "walked south along x 2466, outside the grid zone, to " .. at.x .. "," .. at.z)
            t.exec("pullLeverAfterGrid", t.player.click_loc, "portcullis_lever_up", 1) -- upass_grid.rs2:25
            t.ticks(10)
            _, at = t.world.tile()
            t.check("pullLeverAfterGrid-through", at.x < 2465, "after the lever at " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            -- passTrap2..4 (upass_obstacles.rs2:203): disarm attempt (thieving check), a failure costs ~10% hp + 1
            local function pass_trap(name, trap_x, trap_z)
                local before
                local detail = ""
                local passed = false
                for attempt = 1, 4 do
                    local _, here = t.world.tile()
                    before = here.x
                    local result = t.player.click_loc("upass_speartrap", 1, { at = { trap_x, trap_z } })
                    t.ticks(2)
                    local kind = t.chat.kind()
                    local played = "no page (" .. tostring(kind) .. ")"
                    if kind ~= "none" then
                        played = tostring(t.chat.play({ "mesbox:It's a trap", "choose:Yes, I'll give it a go." }))
                    end
                    t.ticks(6)
                    local _, there = t.world.tile()
                    local hp_result, hp_row = t.skill.read("hitpoints"); local hp = hp_result == "ok" and hp_row.level or hp_result
                    detail = "attempt " .. attempt .. ": click " .. tostring(result) .. ", chat " .. played .. ", x " .. before .. " -> " .. there.x .. "," .. there.z .. ", hp " .. tostring(hp) .. " :: " .. last_lines(3)
                    if there.x < trap_x then passed = true break end
                end
                t.check(name, passed, detail)
            end
            pass_trap("passTrap1", 2443, 9677)
            pass_trap("passTrap2", 2440, 9677)
            pass_trap("passTrap3", 2435, 9675)
            pass_trap("passTrap4", 2432, 9675)
            _, at = t.world.tile()
            local hp_result, hp_row = t.skill.read("hitpoints")
            t.check("leg.2.state", true, "at " .. at.x .. "," .. at.z .. " level " .. at.level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. "; shortbow worn, bronze_arrow x" .. tostring(select(2, t.inv.count("bronze_arrow"))) .. ", tinderbox x" .. tostring(select(2, t.inv.count("tinderbox"))) .. "; hitpoints " .. tostring(hp_result == "ok" and hp_row.level or hp_result) .. " (traps cost hp, no food carried)")
            -- LEG 2 END
        end },
        { name = "traps_planks_orbs", run = function(t)
            -- LEG 3 BEGIN: passTrap5
            local _, at = t.world.tile()
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            -- food for the trap damage (leg 2 left the player at about 2 hp)
            t.cheat("::give lobster 6")
            -- the western cave is full of blessed spiders and ogres (m37_151.spawn); the guide's player is a fighter, a 10-hp
            -- account dies to them (run 3-4). Levels a questing account brings:
            t.cheat("::setlevel hitpoints 40")
            t.cheat("::setlevel defence 30")
            t.ticks(2)
            local hp_result, hp_row = t.skill.read("hitpoints")
            t.check("leg.3.start", true, "at " .. at.x .. "," .. at.z .. " level " .. at.level .. "; hitpoints " .. tostring(hp_result == "ok" and hp_row.level or hp_result) .. "; woodplank x" .. tostring(select(2, t.inv.count("woodplank"))))
            t.exec("eatFood", t.player.inv_op, "lobster", 1)
            t.ticks(3)
            hp_result, hp_row = t.skill.read("hitpoints")
            t.check("leg.3.fed", hp_result == "ok" and hp_row.level > 5, "hitpoints after the lobster " .. tostring(hp_result == "ok" and hp_row.level or hp_result))
            -- Quest Helper: the Plank is a brought-along requirement of plankRock1-3 (items: Plank). The guide's collectPlank (2435,9726) is a
            -- substep of crossThePit (leg 2), reachable only from Koftik's ledge before the bridge; from here the four traps and the pit lie between
            -- (walk_to stalls at 2444,9677, tried b49), so it is given here. A leg 2 that picks it up at crossThePit would let this line go.
            t.cheat("::give woodplank")
            local function pass_trap(name, trap_x, trap_z)
                local detail = ""
                local passed = false
                for attempt = 1, 4 do
                    local result = t.player.click_loc("upass_speartrap", 1, { at = { trap_x, trap_z } })
                    t.ticks(2)
                    local kind = t.chat.kind()
                    local played = "no page (" .. tostring(kind) .. ")"
                    if kind ~= "none" then
                        played = tostring(t.chat.play({ "mesbox:It's a trap", "choose:Yes, I'll give it a go." }))
                    end
                    t.ticks(6)
                    local _, there = t.world.tile()
                    local hpr, hprow = t.skill.read("hitpoints")
                    detail = "attempt " .. attempt .. ": click " .. tostring(result) .. ", chat " .. played .. ", now " .. there.x .. "," .. there.z .. ", hp " .. tostring(hpr == "ok" and hprow.level or hpr) .. " :: " .. last_lines(3)
                    if there.x < trap_x then passed = true break end
                    if hpr == "ok" and hprow.level < 4 then t.player.inv_op("lobster", 1) t.ticks(3) end
                end
                t.check(name, passed, detail)
            end
            pass_trap("passTrap5", 2430, 9675)
            _, at = t.world.tile()
            local function plank_rock(name, stand_x, stand_z, rock_x, rock_z)
                t.player.walk_to(stand_x, stand_z, 16)
                t.ticks(2)
                local _, pre = t.world.tile()
                t.check("goto-" .. name, math.abs(pre.x - stand_x) <= 1 and math.abs(pre.z - stand_z) <= 1, "walked to " .. pre.x .. "," .. pre.z .. " (wanted " .. stand_x .. "," .. stand_z .. ")")
                local rock = t.player.by_symbol("loc", "upass_double_springtrap_trigger")
                t.exec(name, t.player.use_on, "woodplank", rock, { at = { rock_x, rock_z } }) -- upass_obstacles.rs2:242
                t.ticks(8)
                local _, here = t.world.tile()
                t.check(name .. "-crossed", here.z > rock_z, "after the plank at " .. here.x .. "," .. here.z .. " (rock " .. rock_x .. "," .. rock_z .. ") :: " .. last_lines(3))
            end
            plank_rock("plankRock1", 2418, 9680, 2418, 9681)
            plank_rock("plankRock2", 2418, 9684, 2418, 9685)
            plank_rock("plankRock3", 2416, 9688, 2416, 9689)
            local function orb_count()
                return select(2, t.inv.count("caveorb1")) + select(2, t.inv.count("caveorb2")) + select(2, t.inv.count("caveorb3")) + select(2, t.inv.count("caveorb4"))
            end
            local function take_orb(name, stand_x, stand_z, orb_x, orb_z)
                local walk_result, walk_detail = t.player.walk_to(stand_x, stand_z, 60)
                t.ticks(2)
                local _, pre = t.world.tile()
                t.check("goto-" .. name, math.abs(pre.x - stand_x) <= 2 and math.abs(pre.z - stand_z) <= 2, "walk_to " .. tostring(walk_result) .. " " .. tostring(walk_detail) .. "; standing " .. pre.x .. "," .. pre.z)
                local before = orb_count()
                t.exec(name, t.player.click_loc, "caveorb_vis", 1, { at = { orb_x, orb_z } }) -- upass_orbs.rs2 [oploc1,caveorb_vis]
                t.ticks(6)
                local _, here = t.world.tile()
                local after = orb_count()
                t.check(name .. "-taken", after == before + 1, "Take pressed at " .. orb_x .. "," .. orb_z .. "; standing " .. here.x .. "," .. here.z .. "; orbs " .. before .. " -> " .. after .. " :: " .. last_lines(2))
            end
            t.player.walk_to(2416, 9696, 16)
            t.ticks(2)
            take_orb("collectOrb1", 2416, 9696, 2416, 9698)
            do
                local _, spider_summary = t.npc.tiles("blessed_spider", 30)
                local _, ogre_summary = t.npc.tiles("ogre", 30)
                local hpr, hprow = t.skill.read("hitpoints")
                t.check("probe-cave-npcs", true, "spiders: " .. tostring(spider_summary) .. " ogres: " .. tostring(ogre_summary) .. " hp " .. tostring(hpr == "ok" and hprow.level or hpr))
            end
            -- the cave west of the planks is a long way round: walk it in hops, noting where each lands
            for _, hop in ipairs({ { 2404, 9692 }, { 2398, 9688 }, { 2392, 9686 } }) do
                local hop_detail = ""
                for attempt = 1, 6 do
                    local hop_result, hop_note = t.player.walk_to(hop[1], hop[2], 12)
                    local _, hop_at = t.world.tile()
                    local hpr, hprow = t.skill.read("hitpoints")
                    hop_detail = hop_detail .. "[" .. attempt .. ": " .. tostring(hop_result) .. " at " .. hop_at.x .. "," .. hop_at.z .. " hp " .. tostring(hpr == "ok" and hprow.level or hpr) .. "] "
                    if hpr == "ok" and hprow.level <= 6 then t.player.inv_op("lobster", 1) t.ticks(2) end
                    if math.abs(hop_at.x - hop[1]) <= 1 and math.abs(hop_at.z - hop[2]) <= 1 then break end
                end
                t.check("hop-" .. hop[1] .. "-" .. hop[2], true, hop_detail .. last_lines(2))
            end
            take_orb("collectOrb2", 2387, 9685, 2385, 9685)
            take_orb("collectOrb3", 2387, 9677, 2386, 9677)
            -- collectOrb4: the logtrap rock; disarm it (a failed thieving roll fires the trap, so retry)
            local function careful_walk(x, z)
                local note = ""
                for attempt = 1, 8 do
                    local walk_result = t.player.walk_to(x, z, 8)
                    local _, here = t.world.tile()
                    local hpr, hprow = t.skill.read("hitpoints")
                    note = note .. "[" .. tostring(walk_result) .. " " .. here.x .. "," .. here.z .. " hp " .. tostring(hpr == "ok" and hprow.level or hpr) .. "] "
                    if hpr == "ok" and hprow.level <= 18 then t.player.inv_op("lobster", 1) t.ticks(2) end
                    if math.abs(here.x - x) <= 1 and math.abs(here.z - z) <= 1 then break end
                end
                return note
            end
            local approach_note = careful_walk(2384, 9668)
            t.check("goto-collectOrb4", true, approach_note)
            local got4 = false
            local detail4 = ""
            for attempt = 1, 5 do
                if orb_count() >= 4 then got4 = true break end
                t.player.click_loc("upass_logtrap_trigger", 1, { at = { 2382, 9668 } })
                t.ticks(2)
                local kind = t.chat.kind()
                if kind ~= "none" then
                    t.chat.play({ "mesbox:The rock appears to move", "choose:Yes, I'll give it a go." })
                end
                t.ticks(8)
                local hpr, hprow = t.skill.read("hitpoints")
                local _, here = t.world.tile()
                detail4 = detail4 .. "attempt " .. attempt .. " at " .. here.x .. "," .. here.z .. " orbs " .. orb_count() .. " hp " .. tostring(hpr == "ok" and hprow.level or hpr) .. " :: " .. last_lines(2) .. " ## "
                if hpr == "ok" and hprow.level <= 18 then t.player.inv_op("lobster", 1) t.ticks(3) end
                if orb_count() >= 4 then got4 = true break end
                careful_walk(2384, 9668)
            end
            t.check("collectOrb4", got4, detail4)
            t.ticks(4)
            local _, fin = t.world.tile()
            t.check("leg.3.end", got4, "at " .. fin.x .. "," .. fin.z .. " level " .. fin.level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. "; orbs carried " .. orb_count() .. " (caveorb1-4), woodplank x" .. tostring(select(2, t.inv.count("woodplank"))) .. ", shortbow worn")
            -- LEG 3 END
        end },
        { name = "furnace_well_maze", run = function(t)
            -- LEG 4 BEGIN: orbsToFurnace
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function hp_now()
                local r, row = t.skill.read("hitpoints")
                return r == "ok" and row.level or 0
            end
            -- Quest Helper lists a Spade as brought along for the mud (digMud); food for the cave damage
            t.cheat("::give spade 1")
            t.cheat("::give lobster 10") -- food for the cave damage
            t.ticks(2)
            local _, start = t.world.tile()
            t.check("leg.4.start", true, "at " .. start.x .. "," .. start.z .. " level " .. start.level .. "; hitpoints " .. hp_now() .. "; upass stage " .. tostring(select(2, t.quest.stage())))
            -- orbsToFurnace: plain travel back east over the planks the guide already crossed, then the orbs go into the furnace
            t.exec("goto-orbsToFurnace", t.player.goto_tile, 2453, 9683, 0)
            local furnace = t.player.by_symbol("loc", "furnace_upass")
            for i = 1, 4 do
                local name = i == 1 and "orbsToFurnace" or ("orbsToFurnace-" .. i)
                t.exec(name, t.player.use_on, "caveorb" .. i, furnace) -- smelting.rs2:44 -> upass_orbs.rs2:9
                t.ticks(8)
            end
            t.check("orbsToFurnace-all", select(2, t.inv.count("caveorb1")) + select(2, t.inv.count("caveorb2")) + select(2, t.inv.count("caveorb3")) + select(2, t.inv.count("caveorb4")) == 0,
                "orbs left in the backpack after four furnace uses; :: " .. last_lines(3))
            -- climbDownWell
            t.exec("goto-climbDownWell", t.player.goto_tile, 2417, 9677, 0) -- from the furnace; the fall is in leg 1 now
            t.exec("climbDownWell", t.player.click_loc, "cave_well", 1) -- upass_well.rs2:10
            t.ticks(8)
            local _, down = t.world.tile()
            t.check("climbDownWell-down", down.z < 9670, "after the well at " .. down.x .. "," .. down.z .. " level " .. down.level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. " :: " .. last_lines(3))
            local function eat_if_low(limit)
                if hp_now() <= limit and select(2, t.inv.count("lobster")) > 0 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(3)
                end
            end
            local function here()
                local _, at = t.world.tile()
                return at
            end
            -- pickCellLock: Thieving 1 fails often (stat_random(thieving, 128, 400)); each fail is "You fail to pick the lock."
            local lock_note = ""
            for _, hop in ipairs({ { 2415, 9660 }, { 2405, 9658 }, { 2400, 9657 }, { 2396, 9657 }, { 2394, 9658 }, { 2394, 9654 } }) do
                local walk_result, walk_detail = t.player.walk_to(hop[1], hop[2], 20)
                t.ticks(1)
                lock_note = lock_note .. "walk_to " .. hop[1] .. "," .. hop[2] .. ": " .. tostring(walk_result) .. " " .. tostring(walk_detail) .. " now " .. here().x .. "," .. here().z .. "; "
            end
            local walked_through = false
            for attempt = 1, 12 do
                eat_if_low(10)
                local before = here()
                local click_result, click_detail = t.player.click_loc("cave_railings2", 1, { at = { 2393, 9655 } }) -- upass_unicorn.rs2:11
                t.ticks(8)
                local after = here()
                local lines = last_lines(3)
                lock_note = lock_note .. "[" .. attempt .. " click " .. tostring(click_result) .. " " .. tostring(click_detail) .. ": " .. before.x .. "," .. before.z .. " -> " .. after.x .. "," .. after.z .. "] "
                if string.find(lines, "cage slams shut", 1, true) or string.find(lines, "You walk through", 1, true) then
                    walked_through = true
                    lock_note = lock_note .. ":: " .. lines
                    break
                end
            end
            t.check("pickCellLock", walked_through, lock_note)
            -- digMud: the spade (brought along) on the loose mud; it teleports the player through the tunnel
            local mud = t.player.by_symbol("loc", "upass_mud")
            t.exec("digMud", t.player.use_on, "spade", mud) -- upass_unicorn_tunnels.rs2:9
            t.ticks(6)
            local after_mud = here()
            t.check("digMud-through", after_mud.x < 2400 and after_mud.z < 9650, "after the tunnel at " .. after_mud.x .. "," .. after_mud.z .. " :: " .. last_lines(3))
            -- crossLedge: east of the ledge (upass_obstacles.rs2:313 refuses from the west); a failed agility roll drops you 5 hp
            local ledge_note = ""
            local ledge_ok = false
            for _, hop in ipairs({ { 2386, 9646 }, { 2378, 9645 }, { 2376, 9645 } }) do
                local walk_result, walk_detail = t.player.walk_to(hop[1], hop[2], 16)
                t.ticks(1)
                ledge_note = ledge_note .. "walk_to " .. hop[1] .. "," .. hop[2] .. ": " .. tostring(walk_result) .. " now " .. here().x .. "," .. here().z .. "; "
            end
            for attempt = 1, 8 do
                eat_if_low(10)
                local before = here()
                local click_result, click_detail = t.player.click_loc("upass_ledge", 1, { at = { 2374, 9644 } }) -- upass_obstacles.rs2:313
                t.ticks(10)
                local after = here()
                local lines = last_lines(3)
                ledge_note = ledge_note .. "[" .. attempt .. " click " .. tostring(click_result) .. ": " .. before.x .. "," .. before.z .. " -> " .. after.x .. "," .. after.z .. " L" .. after.level .. "] "
                if after.x <= 2375 and after.z <= 9639 then
                    ledge_ok = true
                    ledge_note = ledge_note .. ":: " .. lines
                    break
                end
            end
            t.check("crossLedge", ledge_ok, ledge_note)
            -- navigateMaze: after the ledge the cave is rock bridges over pits (upass_obstacles.rs2:360); each bridge is crossed by
            -- clicking its walkway_upass_narrow_mid_top, the walkways between them are plain travel. The Thieving-50 shortcut
            -- (cave_railings5, upass_unicorn.rs2:28) is not open to this account, so the long way round is driven.
            local maze_note = ""
            local bridges = { { 2380, 9634 }, { 2387, 9631 }, { 2392, 9627 }, { 2399, 9632 }, { 2406, 9637 } }
            -- walk legs to each bridge's west side (plain travel; seam b48-seam2 maze_walk route)
            local bridge_legs = {
                { { 2373, 9634 }, { 2379, 9634 } },
                { { 2384, 9634 }, { 2384, 9631 }, { 2386, 9631 } },
                { { 2389, 9631 }, { 2389, 9627 }, { 2391, 9627 } },
                { { 2395, 9627 }, { 2395, 9632 }, { 2398, 9632 } },
                { { 2403, 9632 }, { 2403, 9637 }, { 2405, 9637 } },
            }
            for index, bridge in ipairs(bridges) do
                -- a failed roll drops you under the bridge: eat, walk the leg again and click again
                for attempt = 1, 8 do
                    eat_if_low(12)
                    for _, hop in ipairs(bridge_legs[index]) do t.player.walk_to(hop[1], hop[2], 30) end
                    t.ticks(1)
                    -- the pit under the last bridge (2406..2410,9632..9635) is walled in on foot (probe: every walk_to ends inside it);
                    -- a failed roll there is put back on the near side with plain travel, which crosses nothing
                    local near = bridge_legs[index][#bridge_legs[index]]
                    if here().x ~= near[1] or here().z ~= near[2] then
                        if attempt > 1 and index == #bridges then
                            t.player.goto_tile(near[1], near[2], 0)
                            t.ticks(2)
                        end
                    end
                    local click_result = t.player.click_loc("walkway_upass_narrow_mid_top", 1, { at = { bridge[1], bridge[2] } })
                    t.ticks(12)
                    maze_note = maze_note .. "bridge " .. bridge[1] .. "," .. bridge[2] .. " #" .. attempt .. " click " .. tostring(click_result) .. " now " .. here().x .. "," .. here().z .. "; "
                    if here().x == bridge[1] + 1 and here().z == bridge[2] then break end
                end
            end
            for _, hop in ipairs({ { 2414, 9637 }, { 2420, 9628 }, { 2422, 9618 }, { 2422, 9612 }, { 2420, 9606 } }) do
                eat_if_low(12)
                t.player.walk_to(hop[1], hop[2], 20)
                t.ticks(1)
            end
            t.check("navigateMaze", here().x >= 2418 and here().z <= 9610, "pick the cell lock, dig the mud, cross the ledge, then four rock bridges (2380,9634 2387,9631 2392,9627 2399,9632), the x2403 walkway and the bridge 2406,9637; now at " .. here().x .. "," .. here().z .. " hp " .. hp_now() .. " :: " .. maze_note)
            -- goThroughPipe: the east end of upass_pipe6 at 2417,9605 (the guide's 2418,9605); before the unicorn is dead it crawls west
            local pipe_x_before = here().x
            local pipe_result = ""
            for attempt = 1, 4 do
                eat_if_low(12)
                t.player.click_loc("upass_pipe6", 1, { at = { 2417, 9605 } }) -- upass_obstacles.rs2:390
                t.ticks(14)
                if here().x < 2415 then break end
            end
            t.check("goThroughPipe", here().x < 2415 and here().z >= 9603 and here().z <= 9608, "from x " .. pipe_x_before .. " to " .. here().x .. "," .. here().z .. " level " .. here().level .. " :: " .. last_lines(2))
            -- searchUnicornCage: op2 on the cage (upass_unicorn.rs2:66) gives the loose railing
            t.player.walk_to(2397, 9607, 30)
            t.ticks(1)
            t.exec("searchUnicornCage", t.player.click_loc, "cave_railings3", 2, { at = { 2397, 9605 } })
            t.ticks(6)
            t.check("searchUnicornCage-railing", select(2, t.inv.count("caverailing")) == 1, "caverailing in the backpack: " .. tostring(select(2, t.inv.count("caverailing"))) .. " :: " .. last_lines(2))
            -- useRailingOnBoulder: the loose railing on the boulder (upass_unicorn.rs2:54); stage 3 -> 4 and the player is moved 25 tiles west
            local boulder = t.player.by_symbol("npc", "boulder_upass")
            t.exec("useRailingOnBoulder", t.player.use_on, "caverailing", boulder)
            t.ticks(14)
            t.exec("useRailingOnBoulder-dialog", t.chat.play, { "player:I heard something breaking" }) -- upass_unicorn.rs2:63
            t.ticks(2)
            t.check("quest.stage.killed_unicorn", select(2, t.quest.stage()) == 4, "upass stage read from the server: " .. tostring(select(2, t.quest.stage())) .. " (upass_killed_unicorn = 4)")
            t.check("useRailingOnBoulder-moved", here().x < 2395, "after the boulder at " .. here().x .. "," .. here().z .. " :: " .. last_lines(3))
            -- searchUnicornCageAgain: the destroyed cage gives the horn (upass_unicorn.rs2:76)
            t.exec("searchUnicornCageAgain", t.player.click_loc, "unicorncage_destroyed_upass", 1)
            t.ticks(4)
            t.exec("searchUnicornCageAgain-dialog", t.chat.play, { "player:All that remains is a damaged horn" })
            t.ticks(3)
            t.check("searchUnicornCageAgain-horn", select(2, t.inv.count("cave_unicorn_horn")) == 1, "cave_unicorn_horn in the backpack: " .. tostring(select(2, t.inv.count("cave_unicorn_horn"))) .. " :: " .. last_lines(2))
            -- the destroyed-cage room is full of skeletons (areas/world/configs/m37_150.spawn:2371-2383,9605-9611) that keep the
            -- single-way combat claim alive; the leg ends only once they are dead, fought for real with food
            local fought = ""
            -- the skeletons (level 25) out-damage an unarmed 10-hp-era account: arm it the way leg 3 does (hitpoints/defence are
            -- combat levels, not guide work), carry food and WEAR the weapon
            t.cheat("::setlevel attack 40")
            t.cheat("::setlevel strength 40")
            t.cheat("::setlevel defence 45")
            t.cheat("::give adamant_scimitar 1")
            t.cheat("::give lobster 10")
            t.ticks(3)
            t.exec("leg.4.wield", t.player.inv_op, "adamant_scimitar", 2) -- op 1 answered "Nothing interesting happens."; op 2 UNTESTED
            t.ticks(2)
            for _ = 1, 3 do eat_if_low(25) end
            for _, sym in ipairs({ "skeleton_armed2", "skeleton_unarmed", "skeleton_unarmed2", "skeleton_armed", "skeleton_armed4" }) do
                local found = t.npc.nearest(sym, 25)
                if found == "ok" then
                    eat_if_low(22)
                    local attack_result = t.player.attack(sym, 2, 25)
                    t.ticks(2)
                    local dead_result = t.npc.await_dead_engaged(60, 10, { eat = { item = "lobster", below = 22 } })
                    fought = fought .. sym .. " attack " .. tostring(attack_result) .. " dead " .. tostring(dead_result) .. "; "
                end
            end
            eat_if_low(14)
            t.check("leg.4.skeletons", true, "skeleton room cleared before the boundary: " .. fought .. "hp " .. hp_now())
            t.ticks(12)
            local _, finish = t.world.tile()
            t.check("leg.4.end", true, "at " .. finish.x .. "," .. finish.z .. " level " .. finish.level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. " read from the server; backpack: cave_unicorn_horn x" .. tostring(select(2, t.inv.count("cave_unicorn_horn"))) .. ", lobster x" .. tostring(select(2, t.inv.count("lobster"))) .. ", spade x" .. tostring(select(2, t.inv.count("spade"))) .. "; hitpoints " .. hp_now())
            -- LEG 4 END
        end },
        { name = "tunnel_knights_and_well", run = function(t)
            -- LEG 5 BEGIN: leaveUnicornArea
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function hp_now()
                local r, row = t.skill.read("hitpoints")
                return r == "ok" and row.level or 0
            end
            local function eat_if_low(limit)
                if hp_now() <= limit and select(2, t.inv.count("lobster")) > 0 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(3)
                end
            end
            local function here()
                local _, at = t.world.tile()
                return at
            end
            local _, start = t.world.tile()
            t.check("leg.5.start", true, "at " .. start.x .. "," .. start.z .. " level " .. start.level .. "; hitpoints " .. hp_now() .. "; upass stage " .. tostring(select(2, t.quest.stage())))
            -- leaveUnicornArea: the tunnel door of the skeleton room (upass_unicorn_tunnels.rs2:25 -> :30 telejumps to 0_37_150_8_10 once the unicorn is dead)
            eat_if_low(20)
            t.exec("leaveUnicornArea", t.player.click_loc, "upass_unicorn_doorl", 1)
            t.ticks(6)
            local after_door = here()
            t.check("leaveUnicornArea-moved", true, "after the tunnel door at " .. after_door.x .. "," .. after_door.z .. " level " .. after_door.level .. " :: " .. last_lines(3))
            -- walkToKnights: the tunnel beyond the door runs north-east to the knights' camp
            local tunnel_note = ""
            for _, hop in ipairs({ { 2380, 9680 }, { 2395, 9700 }, { 2410, 9710 }, { 2424, 9715 } }) do
                eat_if_low(15)
                local walk_result, walk_detail = t.player.walk_to(hop[1], hop[2], 40)
                t.ticks(1)
                tunnel_note = tunnel_note .. "walk_to " .. hop[1] .. "," .. hop[2] .. ": " .. tostring(walk_result) .. " " .. tostring(walk_detail) .. " now " .. here().x .. "," .. here().z .. "; "
            end
            t.check("walkToKnights", here().x >= 2418 and here().z >= 9708, "tunnel walk: " .. tunnel_note .. "hp " .. hp_now())
            -- killJerro: talk to one knight first for the supplies (upass_encounters.rs2:82 needs 7 free slots), then kill all three.
            -- Paladins out-hit a 40-hp account: raise the combat levels the way legs 3-4 do (not guide work).
            t.cheat("::setlevel hitpoints 80")
            t.cheat("::setlevel attack 80")
            t.cheat("::setlevel strength 80")
            t.cheat("::setlevel defence 60")
            t.ticks(3)
            -- the Paladin's gift needs 7 free slots (upass_encounters.rs2:101): the bridge/well items already spent go on the floor first
            for _, spent in ipairs({ "woodplank", "caverailing", "shortbow", "bronze_arrow", "spade" }) do
                if select(2, t.inv.count(spent)) > 0 then
                    t.player.drop(spent)
                    t.ticks(2)
                end
            end
            local function free_slots()
                local free = 0
                for slot = 0, 27 do
                    local _, row = t.inv.slot(slot)
                    if row and row.name == "" then free = free + 1 end
                end
                return free
            end
            local lobsters_dropped = 0
            while free_slots() < 7 and select(2, t.inv.count("lobster")) > 3 do
                t.player.drop("lobster")
                t.ticks(2)
                lobsters_dropped = lobsters_dropped + 1
            end
            t.check("killJerro-room", free_slots() >= 7, "free backpack slots before the knight's talk: " .. free_slots() .. " (7 needed); lobsters dropped " .. lobsters_dropped .. ", lobster x" .. tostring(select(2, t.inv.count("lobster"))))
            t.exec("killJerro-talk", t.player.talk_to, "upass_paladin1", 1)
            local drained, drain_kind = t.chat.drain{ max_pages = 20 }
            t.ticks(3)
            t.check("killJerro-supplies", select(2, t.inv.count("bread")) == 2, "after the knight's talk: drain " .. tostring(drained) .. " " .. tostring(drain_kind) .. "; bread x" .. tostring(select(2, t.inv.count("bread"))) .. " stew x" .. tostring(select(2, t.inv.count("stew"))) .. " :: " .. last_lines(2))
            local function kill_knight(step, sym, badge)
                eat_if_low(30)
                local attack_result = t.player.attack(sym, 2, 25)
                t.ticks(2)
                local dead_result = t.npc.await_dead_engaged(200, 15, { eat = { item = "lobster", below = 35 } })
                t.ticks(2)
                local take_result = t.player.click_obj(badge, 3)
                t.ticks(3)
                local got = select(2, t.inv.count(badge))
                t.check(step, got == 1, sym .. " attack " .. tostring(attack_result) .. " dead " .. tostring(dead_result) .. "; take " .. tostring(take_result) .. "; " .. badge .. " x" .. tostring(got) .. "; hp " .. hp_now() .. " :: " .. last_lines(2))
            end
            kill_knight("killJerro", "upass_paladin1", "paladinbadge1")
            kill_knight("killHarry", "upass_paladin3", "paladinbadge3")
            kill_knight("killCarl", "upass_paladin2", "paladinbadge2")
            -- useBadge*OnWell / useUnicornHornOnWell: plain travel west along the path to the blood well (upass_bloodwell.rs2:11), then each item on it
            local well_note = ""
            for _, hop in ipairs({ { 2410, 9719 }, { 2395, 9719 }, { 2380, 9719 }, { 2376, 9719 } }) do
                eat_if_low(30)
                local walk_result = t.player.walk_to(hop[1], hop[2], 30)
                t.ticks(1)
                well_note = well_note .. "walk_to " .. hop[1] .. "," .. hop[2] .. ": " .. tostring(walk_result) .. " now " .. here().x .. "," .. here().z .. "; "
            end
            t.check("walkToWell", here().x <= 2380 and here().z >= 9712, "walk to the blood well: " .. well_note)
            local well = t.player.by_symbol("loc", "bloodwell_upass")
            for _, use in ipairs({
                { "useBadgeHarryOnWell", "paladinbadge3" },
                { "useBadgeCarlOnWell", "paladinbadge2" },
                { "useUnicornHornOnWell", "cave_unicorn_horn" },
                { "useBadgeJerroOnWell", "paladinbadge1" },
            }) do
                t.exec(use[1], t.player.use_on, use[2], well)
                t.ticks(4)
                t.check(use[1] .. "-gone", select(2, t.inv.count(use[2])) == 0, use[2] .. " left in the backpack after the well: " .. tostring(select(2, t.inv.count(use[2]))) .. " :: " .. last_lines(3))
            end
            -- openIbansDoor: the door at the end of the path (upass_bloodwell.rs2:88) leads to Iban's temple once all four offerings are made
            t.exec("openIbansDoor", t.player.click_loc, "cavetempledoor2r", 1)
            t.ticks(6)
            local at_door = here()
            t.check("openIbansDoor-through", at_door.level == 1 or at_door.z < 6400, "after the door at " .. at_door.x .. "," .. at_door.z .. " level " .. at_door.level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. " :: " .. last_lines(3))
            t.ticks(6)
            local _, finish = t.world.tile()
            t.check("leg.5.end", true, "at " .. finish.x .. "," .. finish.z .. " level " .. finish.level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. " read from the server; hitpoints " .. hp_now() .. "; lobster x" .. tostring(select(2, t.inv.count("lobster"))) .. ", adamant scimitar worn, caverailing x" .. tostring(select(2, t.inv.count("caverailing"))) .. ", shortbow x" .. tostring(select(2, t.inv.count("shortbow"))))
            -- LEG 5 END
        end },
        { name = "iban_cave_dwarves", run = function(t)
            -- LEG 6 BEGIN: descendCave
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function hp_now()
                local r, row = t.skill.read("hitpoints")
                return r == "ok" and row.level or 0
            end
            local function here()
                local _, at = t.world.tile()
                return at
            end
            local _, start = t.world.tile()
            t.check("leg.6.start", true, "at " .. start.x .. "," .. start.z .. " level " .. start.level .. "; hitpoints " .. hp_now() .. "; upass stage " .. tostring(select(2, t.quest.stage())))
            -- descendCave: follow the cavern edge south to the wall tunnel (upass_tunnels.rs2:9)
            local edge_note = ""
            for _, hop in ipairs({ { 2165, 4690 }, { 2167, 4644 }, { 2168, 4625 }, { 2168, 4600 }, { 2168, 4580 }, { 2166, 4560 }, { 2158, 4550 }, { 2151, 4548 } }) do
                local walk_result, walk_detail = t.player.walk_to(hop[1], hop[2], 40)
                t.ticks(1)
                edge_note = edge_note .. "walk_to " .. hop[1] .. "," .. hop[2] .. ": " .. tostring(walk_result) .. " " .. tostring(walk_detail) .. " now " .. here().x .. "," .. here().z .. "; "
            end
            t.check("walkToCaveEdge", here().z <= 4560, "cavern edge walk: " .. edge_note)
            t.exec("descendCave", t.player.click_loc, "cavewalltunnel_upass_down", 1) -- upass_tunnels.rs2:9
            t.ticks(4)
            local below = here()
            t.check("descendCave-moved", below.z > 9000, "after the cavern wall tunnel at " .. below.x .. "," .. below.z .. " level " .. below.level .. " :: " .. last_lines(2))
            -- the first descent meets the insane Koftik (koftik.rs2:172 koftik_isthatyou, %upass_koftik_chat = 0)
            local drained, drain_kind = t.chat.drain{ max_pages = 20 }
            t.ticks(2)
            t.check("descendCave-koftik", true, "Koftik's dialogue after the descent: drain " .. tostring(drained) .. " " .. tostring(drain_kind) .. "; koftik_chat " .. tostring(select(2, t.var.varbit("varb9135_upass_koftik_chat"))))
            -- talkToNiloof: the dwarf by the cave mouth (m36_153.spawn: upassdwarf1 2315,9806); nilhoof.rs2:9 stage entered_main_area
            local walk_to_niloof = t.player.walk_to(2318, 9805, 30)
            t.ticks(1)
            t.exec("talkToNiloof", t.player.talk_to, "upassdwarf1", 1)
            t.exec("talkToNiloof-dialog", t.chat.play, {
                "npc:Back away",
                "player:That's right, I'm on a quest",
                "npc:Ha ha, listen up",
                "player:What gateway",
                "npc:It once stood",
                "npc:He sends his followers",
                "player:But how",
                "npc:If I knew",
                "npc:She lives on the platforms",
            })
            t.ticks(4)
            local niloof_drained, niloof_kind = t.chat.drain{ max_pages = 5 } -- the closing "Thanks Niloof" page lands after the food (nilhoof.rs2:30)
            t.ticks(2)
            t.check("talkToNiloof-stage", select(2, t.quest.stage()) == 6 and select(2, t.inv.count("meat_pie")) >= 2, "walk " .. tostring(walk_to_niloof) .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. " (spoken_nilhoof); meat_pie x" .. tostring(select(2, t.inv.count("meat_pie"))) .. ", meat_pizza x" .. tostring(select(2, t.inv.count("meat_pizza"))) .. " :: " .. last_lines(2))
            -- talkToKlankForGauntlets: the blacksmith (m36_153.spawn: upassdwarf2 2323,9804); klank.rs2:9 stage spoken_nilhoof, option "What happened to them?"
            t.player.walk_to(2325, 9803, 20)
            t.ticks(1)
            t.exec("talkToKlankForGauntlets", t.player.talk_to, "upassdwarf2", 1)
            t.exec("talkToKlankForGauntlets-dialog", t.chat.play, {
                "player:Hello my good man",
                "npc:Good day to you outsider",
                "npc:If you're not careful",
                "player:Who?",
                "npc:They're not followers",
                "options",
                "choose:What happened to them?",
                "player:What happened to them",
                "npc:They were normal once",
                "npc:Now they all seem",
                "player:Iban",
                "npc:Maybe",
                "npc:Eventually they all fall",
            })
            t.ticks(3)
            t.check("talkToKlankForGauntlets-done", t.chat.kind() == "none", "after Klank's talk chat is " .. t.chat.kind() .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. "; tinderbox x" .. tostring(select(2, t.inv.count("tinderbox"))))
            -- goBackUpToIbansCavern: the tunnel back up (upass_tunnels.rs2:21)
            t.player.walk_to(2336, 9796, 30)
            t.ticks(1)
            t.exec("goBackUpToIbansCavern", t.player.click_loc, "cavewalltunnel_upass_up", 1)
            t.ticks(5)
            local _, finish = t.world.tile()
            t.check("leg.6.end", finish.level >= 1 and finish.z < 6400, "at " .. finish.x .. "," .. finish.z .. " level " .. finish.level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. " read from the server; hitpoints " .. hp_now() .. "; meat_pie x" .. tostring(select(2, t.inv.count("meat_pie"))) .. ", lobster x" .. tostring(select(2, t.inv.count("lobster"))) .. ", tinderbox x" .. tostring(select(2, t.inv.count("tinderbox"))))
            -- LEG 6 END
        end },
        { name = "witch_demons_brew", run = function(t)
            -- LEG 7 BEGIN: pickUpWitchsCat
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function hp_now()
                local r, row = t.skill.read("hitpoints")
                return r == "ok" and row.level or 0
            end
            local function here()
                local _, at = t.world.tile()
                return at
            end
            local function eat_if_low(below)
                for _, food in ipairs({ "shark", "lobster", "meat_pie", "bread" }) do
                    if hp_now() < below and select(2, t.inv.count(food)) > 0 then
                        t.player.inv_op(food, 1)
                        t.ticks(3)
                    end
                end
            end
            local function walk_hops(label, hops)
                local note = ""
                for _, hop in ipairs(hops) do
                    eat_if_low(30)
                    local r, d = t.player.walk_to(hop[1], hop[2], 40)
                    t.ticks(1)
                    note = note .. hop[1] .. "," .. hop[2] .. ": " .. tostring(r) .. " " .. tostring(d) .. " now " .. here().x .. "," .. here().z .. "; "
                end
                return label .. ": " .. note
            end
            local _, start = t.world.tile()
            t.check("leg.7.start", true, "at " .. start.x .. "," .. start.z .. " level " .. start.level .. "; hitpoints " .. hp_now() .. "; upass stage " .. tostring(select(2, t.quest.stage())))
            -- brought along: the guide's empty Bucket for useBucketOnBrew (Quest Helper lists it as a requirement of that step)
            t.cheat("::give bucket_empty 1")
            -- the spent bridge/well items go on the floor so the witch's chest (4 free slots) and the cat fit
            for _, spent in ipairs({ "woodplank", "caverailing", "shortbow", "bronze_arrow" }) do
                if select(2, t.inv.count(spent)) > 0 then
                    t.player.drop(spent)
                    t.ticks(2)
                end
            end
            -- pickUpWitchsCat: the cat sits on the bridges north-west of the arrival tunnel (m33_71.spawn: cavewitchcat 2131,4602 L1)
            -- ROUTE (static map m33_71 BFS + probes): east cliff north to 2171,4581, then west along the z~4582 bridge, then north-west to the cat.
            -- the collapsed bridge pieces (upass_obstacles.rs2:425, a long-jump over the gap) sit on the route; agility is a level setup so the roll rarely drops us
            t.cheat("::setlevel agility 70")
            t.ticks(2)
            local function cross_bridge(sym, lx, lz, want_x, want_z)
                local note = ""
                for attempt = 1, 4 do
                    local r, d = t.player.click_loc(sym, 1, { at = { lx, lz } })
                    t.ticks(8)
                    note = note .. "[" .. attempt .. " " .. tostring(r) .. " now " .. here().x .. "," .. here().z .. " L" .. here().level .. "] "
                    if here().level == 1 and math.abs(here().x - want_x) <= 2 and math.abs(here().z - want_z) <= 2 then break end
                end
                return note
            end
            local cat_walk = walk_hops("toCat", { { 2150, 4549 }, { 2167, 4552 }, { 2172, 4561 }, { 2172, 4575 }, { 2171, 4581 }, { 2160, 4582 } })
            cat_walk = cat_walk .. cross_bridge("bridgecollapsed2", 2156, 4582, 2155, 4582)
            cat_walk = cat_walk .. walk_hops("toCat1b", { { 2150, 4583 } })
            cat_walk = cat_walk .. cross_bridge("bridgecollapsed2", 2147, 4583, 2146, 4583)
            cat_walk = cat_walk .. walk_hops("toCat2", { { 2143, 4589 }, { 2137, 4595 }, { 2132, 4596 } })
            -- the cat wanders; keep pressing Pick-up until it is in the backpack
            local cat_tries = 0
            for attempt = 1, 6 do
                cat_tries = attempt
                if select(2, t.inv.count("cavewitchcat")) > 0 then break end
                local pr, pd = t.player.press("cavewitchcat", 1, 15)
                t.ticks(3)
                cat_walk = cat_walk .. "[press " .. attempt .. " " .. tostring(pr) .. "] "
            end
            t.step("pickUpWitchsCat", select(2, t.inv.count("cavewitchcat")) > 0 and "PASS" or "FAIL", "cat pressed " .. cat_tries .. " time(s); cavewitchcat x" .. tostring(select(2, t.inv.count("cavewitchcat"))) .. " at " .. here().x .. "," .. here().z)
            t.check("pickUpWitchsCat-held", select(2, t.inv.count("cavewitchcat")) == 1, "cavewitchcat x" .. tostring(select(2, t.inv.count("cavewitchcat"))) .. " at " .. here().x .. "," .. here().z .. " :: " .. cat_walk .. " :: " .. last_lines(2))
            -- useCatOnDoor: the witch's door in the south-east corner (kardia.rs2:12)
            local door_walk = walk_hops("toDoor", { { 2137, 4595 }, { 2143, 4589 }, { 2144, 4584 } })
            door_walk = door_walk .. cross_bridge("bridgecollapsed2", 2147, 4583, 2150, 4583)
            door_walk = door_walk .. walk_hops("toDoor2", { { 2149, 4579 }, { 2150, 4574 }, { 2154, 4572 }, { 2158, 4570 }, { 2160, 4567 } })
            local door = t.player.by_symbol("loc", "cavewitch_door")
            t.exec("useCatOnDoor", t.player.use_on, "cavewitchcat", door)
            t.ticks(12)
            t.check("useCatOnDoor-gone", select(2, t.inv.count("cavewitchcat")) == 0, "cat x" .. tostring(select(2, t.inv.count("cavewitchcat"))) .. " gavecat " .. tostring(select(2, t.var.varbit("varb9123_upass_gavecat"))) .. " at " .. here().x .. "," .. here().z .. " :: " .. door_walk .. " :: " .. last_lines(3))
            -- searchWitchsChest: through the door (kardia.rs2:12 gavecat = 1 walks you in), then the chest (kardia.rs2:91)
            t.exec("openWitchDoor", t.player.click_loc, "cavewitch_door", 1)
            t.ticks(6)
            t.exec("searchWitchsChest", t.player.click_loc, "cavewitchchest", 1)
            t.ticks(4)
            local chest_drained, chest_kind = t.chat.play({ "mesbox:You search the chest" })
            t.ticks(4)
            local find_result, find_detail = t.chat.play({ "mesbox:Inside you find a book" })
            chest_kind = tostring(chest_kind) .. " / " .. tostring(find_result) .. " " .. tostring(find_detail)
            t.ticks(3)
            t.check("searchWitchsChest-found", select(2, t.quest.stage()) == 7 and select(2, t.inv.count("ibandoll")) == 1, "drain " .. tostring(chest_drained) .. " " .. tostring(chest_kind) .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. " (found_doll); ibandoll x" .. tostring(select(2, t.inv.count("ibandoll"))) .. " at " .. here().x .. "," .. here().z .. " :: " .. last_lines(3))
            -- the demons are level-91 aggressive: raise the combat levels as legs 3-5 do (not guide work)
            t.cheat("::setlevel hitpoints 99")
            t.cheat("::setlevel defence 80")
            t.ticks(3)
            -- killHolthion / killDoomion / killOthainian: owner-private amulets drop only now that the doll is found (upass_demon_drops.rs2:17)
            -- the door walks us back out (kardia.rs2:12 gavecat = 1 -> west_ardy_walk_door)
            local exit_result, exit_detail = t.player.click_loc("cavewitch_door", 1)
            t.ticks(6)
            t.check("exitWitchHouse", here().x >= 2159, "click_loc door " .. tostring(exit_result) .. "; now " .. here().x .. "," .. here().z .. " level " .. here().level .. " :: " .. last_lines(2))
            walk_hops("toDemons", { { 2160, 4567 }, { 2155, 4569 }, { 2150, 4571 }, { 2149, 4577 }, { 2152, 4581 }, { 2151, 4583 } })
            cross_bridge("bridgecollapsed2", 2147, 4583, 2146, 4583)
            walk_hops("toDemons2", { { 2142, 4581 }, { 2142, 4574 }, { 2142, 4565 } })
            cross_bridge("bridgecollapsed1", 2142, 4562, 2142, 4561)
            walk_hops("toDemons3", { { 2139, 4556 }, { 2136, 4556 } })
            -- seam36: ~upass_spawn_demons is bound to [mapzone,0_33_71] now (upass_encounters.rs2), so the three demons
            -- stand in this square; read them before the first attack
            do
                local probe = ""
                for _, sym in ipairs({ "holthion", "doomion", "othainian" }) do
                    local r, d = t.npc.nearest(sym, 30)
                    probe = probe .. sym .. "=" .. tostring(r) .. " " .. tostring(type(d) == "table" and (tostring(d.x) .. "," .. tostring(d.z)) or d) .. "; "
                end
                t.check("leg.7.demons_present", select(1, t.npc.nearest("holthion", 30)) == "ok", "at " .. here().x .. "," .. here().z .. " L" .. here().level .. " :: " .. probe)
            end
            -- killHolthion / killDoomion / killOthainian: each drops its owner-private amulet now that the doll is found
            -- (upass_demon_drops.rs2:6,17,28); the amulet is taken off the floor (Take, op 3)
            local function kill_demon(step, sym, amulet)
                eat_if_low(75)
                local attack_result, attack_detail = t.player.attack(sym, 2, 30)
                t.ticks(2)
                local dead_result, dead_detail = t.npc.await_dead_engaged(300, 20, { eat = { item = "shark", below = 60 } })
                t.ticks(2)
                local take_result = t.player.click_obj(amulet, 3)
                t.ticks(3)
                local got = select(2, t.inv.count(amulet))
                t.check(step, got == 1, sym .. " attack " .. tostring(attack_result) .. " (" .. tostring(attack_detail) .. ") dead " .. tostring(dead_result) .. " (" .. tostring(dead_detail) .. "); take " .. tostring(take_result) .. "; " .. amulet .. " x" .. tostring(got) .. "; hp " .. hp_now() .. " at " .. here().x .. "," .. here().z .. " :: " .. last_lines(2))
            end
            -- supply for three level-91 demons (the reviewer rerun died to Othainian): sharks are food, not quest work
            local function leg7_free_slots()
                local free = 0
                for slot = 0, 27 do
                    local _, row = t.inv.slot(slot)
                    if row and row.name == "" then free = free + 1 end
                end
                return free
            end
            -- weak leftovers go on the floor to make room for the sharks (a full pack gave only 1 of 16)
            for _, weak in ipairs({ "half_a_meat_pie", "meat_pie", "meat_pizza", "half_meat_pizza", "bread", "stew", "lobster" }) do
                while select(2, t.inv.count(weak)) > 0 and leg7_free_slots() < 16 do
                    t.player.drop(weak)
                    t.ticks(2)
                end
            end
            local shark_count = math.min(16, math.max(1, leg7_free_slots() - 1))
            t.cheat("::give shark " .. shark_count)
            t.ticks(2)
            kill_demon("killHolthion", "holthion", "holthion_amulet")
            kill_demon("killDoomion", "doomion", "doomion_amulet")
            -- Othainian's platform (2121-2126, 4560-4566; m33_71.jl2 floor decor) is across the collapsed bridge at
            -- 2126,4566 west of Doomion's path: the attack from Doomion's side answers "I can't reach that!"
            local othainian_route = walk_hops("toOthainian", { { 2131, 4566 }, { 2128, 4566 } })
            othainian_route = othainian_route .. cross_bridge("bridgecollapsed2", 2126, 4566, 2125, 4566)
            t.check("crossToOthainian", here().x <= 2125, othainian_route .. " now " .. here().x .. "," .. here().z .. " L" .. here().level)
            t.check("leg.7.pre_othainian", true, "hp " .. hp_now() .. "; shark x" .. tostring(select(2, t.inv.count("shark"))) .. " lobster x" .. tostring(select(2, t.inv.count("lobster"))) .. " free " .. leg7_free_slots())
            kill_demon("killOthainian", "othainian", "othainian_amulet")
            -- back over the same bridge to Doomion's path for the chest north of him
            local chest_route = cross_bridge("bridgecollapsed2", 2126, 4566, 2128, 4566)
            chest_route = chest_route .. walk_hops("toChest", { { 2131, 4566 }, { 2136, 4570 }, { 2136, 4576 } })
            t.check("crossBackToChest", here().x >= 2127, chest_route .. " now " .. here().x .. "," .. here().z .. " L" .. here().level)
            -- searchDoomionsChest: the chest north of Doomion takes the three amulets and pours Iban's shadow over the doll
            -- (upass_cages.rs2:48)
            t.exec("searchDoomionsChest", t.player.click_loc, "upassshutchest1", 1)
            t.ticks(6)
            t.check("searchDoomionsChest-shadow", select(2, t.var.server("varb9118_upass_shadow_on_doll")) == 1, "upass_shadow_on_doll " .. tostring(select(2, t.var.server("varb9118_upass_shadow_on_doll"))) .. "; amulets " .. tostring(select(2, t.inv.count("holthion_amulet"))) .. "/" .. tostring(select(2, t.inv.count("doomion_amulet"))) .. "/" .. tostring(select(2, t.inv.count("othainian_amulet"))) .. " at " .. here().x .. "," .. here().z .. " :: " .. last_lines(4))
            -- returnToDwarfs: the tunnel at the south wall of Iban's cavern drops us at the dwarf encampment (upass_tunnels.rs2:9)
            eat_if_low(50)
            walk_hops("toTunnel", { { 2144, 4560 }, { 2142, 4556 } })
            cross_bridge("bridgecollapsed1", 2142, 4562, 2142, 4565)
            local back_route = walk_hops("toTunnel2", { { 2142, 4574 }, { 2142, 4581 } })
            back_route = back_route .. cross_bridge("bridgecollapsed2", 2147, 4583, 2150, 4583)
            back_route = back_route .. walk_hops("toTunnel3", { { 2153, 4582 } })
            back_route = back_route .. cross_bridge("bridgecollapsed2", 2156, 4582, 2159, 4582)
            back_route = back_route .. walk_hops("toTunnel4", { { 2171, 4581 }, { 2172, 4575 }, { 2172, 4561 }, { 2167, 4552 }, { 2150, 4549 }, { 2150, 4547 } })
            t.exec("returnToDwarfs", t.player.click_loc, "cavewalltunnel_upass_down", 1, { at = { 2150, 4545 } })
            t.ticks(4)
            t.check("returnToDwarfs-moved", here().z > 9000 and here().level == 0, "after the tunnel at " .. here().x .. "," .. here().z .. " level " .. here().level .. " :: " .. back_route .. " :: " .. last_lines(2))
            -- useBucketOnBrew: the brew barrel in the dwarf encampment (upass_tomb.rs2:18 oplocu, bucket_empty)
            -- the barrel stands inside a hut (m36_153.jl2: oldwall ring, poordoor at 2325,9801 in the north wall): open the door first
            walk_hops("toBarrel", { { 2326, 9803 } })
            t.exec("openBrewHutDoor", t.player.click_loc, "poordoor", 1, { at = { 2325, 9801 } })
            t.ticks(5)
            local barrel = t.player.by_symbol("loc", "upassdwarfbrewbarrel")
            t.exec("useBucketOnBrew", t.player.use_on, "bucket_empty", barrel)
            t.ticks(4)
            t.check("useBucketOnBrew-filled", select(2, t.inv.count("upassdwarfbrew")) == 1, "upassdwarfbrew x" .. tostring(select(2, t.inv.count("upassdwarfbrew"))) .. " at " .. here().x .. "," .. here().z .. " :: " .. last_lines(2))
            -- useBrewOnTomb: Iban's tomb in the south-east corner (upass_tomb.rs2:27 oploc1 / :27 oplocu, stage found_doll)
            walk_hops("toTomb", { { 2345, 9800 }, { 2355, 9802 } })
            local tomb = t.player.by_symbol("loc", "ibantomb_left")
            t.exec("useBrewOnTomb", t.player.use_on, "upassdwarfbrew", tomb)
            t.ticks(4)
            t.check("useBrewOnTomb-poured", select(2, t.inv.count("upassdwarfbrew")) == 0 and select(2, t.inv.count("bucket_empty")) == 1, "brew x" .. tostring(select(2, t.inv.count("upassdwarfbrew"))) .. " bucket_empty x" .. tostring(select(2, t.inv.count("bucket_empty"))) .. " upass_brew_tomb " .. tostring(select(2, t.var.server("varb9134_upass_brew_tomb"))) .. " :: " .. last_lines(3))
            t.ticks(6)
            local final = here()
            t.check("leg.7.end", true, "at " .. final.x .. "," .. final.z .. " level " .. final.level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. "; upass_shadow_on_doll " .. tostring(select(2, t.var.server("varb9118_upass_shadow_on_doll"))) .. " upass_brew_tomb " .. tostring(select(2, t.var.server("varb9134_upass_brew_tomb"))) .. "; bucket_empty x" .. tostring(select(2, t.inv.count("bucket_empty"))) .. ", hp " .. hp_now() .. "; adamant_scimitar worn")
            -- LEG 7 END
        end },
        { name = "iban_and_the_temple", run = function(t)
            -- LEG 8 BEGIN: useTinderboxOnTomb
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function hp_now()
                local r, row = t.skill.read("hitpoints")
                return r == "ok" and row.level or 0
            end
            local function here()
                local _, at = t.world.tile()
                return at
            end
            local function eat_if_low(below)
                for _, food in ipairs({ "lobster", "meat_pie", "bread" }) do
                    while hp_now() < below and select(2, t.inv.count(food)) > 0 do
                        t.player.inv_op(food, 1)
                        t.ticks(3)
                    end
                end
            end
            local function var(name) return tostring(select(2, t.var.server(name))) end
            local function walk_hops(label, hops)
                local note = ""
                for _, hop in ipairs(hops) do
                    eat_if_low(30)
                    local r, d = t.player.walk_to(hop[1], hop[2], 40)
                    t.ticks(1)
                    note = note .. hop[1] .. "," .. hop[2] .. ": " .. tostring(r) .. " now " .. here().x .. "," .. here().z .. "; "
                end
                return label .. ": " .. note
            end
            -- the collapsed bridges are long jumps (upass_obstacles.rs2:425); agility 70 is set in leg 7, a failed roll drops us below
            local function cross_bridge(sym, lx, lz, want_x, want_z)
                local note = ""
                for attempt = 1, 4 do
                    local r = t.player.click_loc(sym, 1, { at = { lx, lz } })
                    t.ticks(8)
                    note = note .. "[" .. attempt .. " " .. tostring(r) .. " now " .. here().x .. "," .. here().z .. " L" .. here().level .. "] "
                    if here().level == 1 and math.abs(here().x - want_x) <= 2 and math.abs(here().z - want_z) <= 2 then break end
                end
                return note
            end
            local _, start = t.world.tile()
            t.check("leg.8.start", true, "at " .. start.x .. "," .. start.z .. " level " .. start.level .. "; hitpoints " .. hp_now() .. "; upass stage " .. tostring(select(2, t.quest.stage())))
            -- brought along: food for the Kalrag and Iban fights (Quest Helper: Food), and the gauntlets come from Klank below
            t.cheat("::give lobster 15")
            t.ticks(2)
            eat_if_low(90)
            -- Klank hands Klank's gauntlets over once the doll is found (klank.rs2:35 found_doll); he stands at 2323,9804 (m36_153.spawn)
            t.player.walk_to(2325, 9803, 40)
            t.ticks(1)
            t.exec("talkToKlankForGauntlets-doll", t.player.talk_to, "upassdwarf2", 1)
            t.exec("talkToKlankForGauntlets-doll-dialog", t.chat.play, {
                "player:Hi Klank",
                "npc:Traveller, I hear you plan to destroy Iban",
                "player:That's right",
                "npc:I have a gift for you",
                "npc:I haven't seen another pair",
                "player:Thanks Klank",
                "npc:Good luck traveller",
            })
            t.ticks(2)
            t.check("klanks_gauntlets-given", select(2, t.inv.count("klanks_gauntlets")) == 1, "klanks_gauntlets x" .. tostring(select(2, t.inv.count("klanks_gauntlets"))) .. " :: " .. last_lines(3))
            t.exec("wear-klanks_gauntlets", t.player.equip, "klanks_gauntlets")
            t.ticks(2)
            -- useTinderboxOnTomb: with the brew poured the tomb lights, and the ashes are rubbed into the doll (upass_tomb.rs2:42)
            t.player.walk_to(2355, 9802, 40)
            t.ticks(1)
            local tomb = t.player.by_symbol("loc", "ibantomb_left")
            t.exec("useTinderboxOnTomb", t.player.use_on, "tinderbox", tomb)
            t.ticks(8)
            t.check("useTinderboxOnTomb-ashes", var("varb9117_upass_ashes_on_doll") == "1", "upass_ashes_on_doll " .. var("varb9117_upass_ashes_on_doll") .. " ibans_ashes x" .. tostring(select(2, t.inv.count("ibans_ashes"))) .. " :: " .. last_lines(4))
            -- killKalrag: Kalrag (78 across) stands at 2356,9911 (upass_encounters.rs2:324 spawns him for the doll carrier); his blood
            -- is smeared on the doll when he dies (kalrag.rs2:11). The way north is plain travel.
            eat_if_low(95)
            local goto_result, goto_detail = t.player.goto_tile(2356, 9850, 0)
            t.ticks(2)
            local walk_result, walk_detail = t.player.walk_to(2356, 9890, 60)
            goto_detail = tostring(goto_detail) .. "; walk " .. tostring(walk_result) .. " " .. tostring(walk_detail)
            t.ticks(3)
            t.check("goto-killKalrag", goto_result == "ok", "goto " .. tostring(goto_detail) .. " now " .. here().x .. "," .. here().z .. " L" .. here().level)
            -- the square fires its spawn on entry; Kalrag stands 11 tiles on, so close in before the presence read
            t.player.walk_to(2356, 9900, 40)
            t.ticks(4)
            local kalrag_seen, kalrag_row = t.npc.nearest("kalrag", 30)
            t.check("leg.8.kalrag_present", kalrag_seen == "ok", "kalrag " .. tostring(kalrag_seen) .. " " .. tostring(type(kalrag_row) == "table" and (kalrag_row.x .. "," .. kalrag_row.z) or kalrag_row) .. " from " .. here().x .. "," .. here().z)
            local kalrag_attack, kalrag_attack_detail = t.player.attack("kalrag", 2, 40)
            t.ticks(2)
            local kalrag_dead, kalrag_dead_detail = t.npc.await_dead_engaged(400, 30, { eat = { item = "lobster", below = 45 } })
            t.ticks(6)
            t.check("killKalrag", kalrag_dead == "ok" and var("varb9115_upass_venom_on_doll") == "1", "attack " .. tostring(kalrag_attack) .. " (" .. tostring(kalrag_attack_detail) .. ") dead " .. tostring(kalrag_dead) .. " (" .. tostring(kalrag_dead_detail) .. "); upass_venom_on_doll " .. var("varb9115_upass_venom_on_doll") .. "; hp " .. hp_now() .. " :: " .. last_lines(3))
            -- ascendToHalfSoulless: the north-west exit at 2304,9915 climbs to the upper level (upass_tunnels.rs2:21)
            eat_if_low(70)
            t.player.walk_to(2306, 9913, 40)
            t.ticks(1)
            t.exec("ascendToHalfSoulless", t.player.click_loc, "cavewalltunnel_upass_up", 1)
            t.ticks(4)
            t.check("ascendToHalfSoulless-moved", here().level == 1, "now " .. here().x .. "," .. here().z .. " L" .. here().level .. " :: " .. last_lines(2))
            -- searchCage: the marked cage in the north west of the upper level; Klank's gauntlets are worn, so the bite does not land (upass_cages.rs2:11)
            -- the marked cage is the one dummy placed as upass_cage_dummy (m33_73.jl2:1980, 2134,4702). A static BFS over m33_73/m33_72 level 1
            -- (jm2 flags + jl2 loc shapes) shows the way: south end of the west cliff, the collapsed bridge at 2121,4686, then east and north.
            local cage_route = walk_hops("toCage", { { 2116, 4708 }, { 2117, 4687 } })
            cage_route = cage_route .. cross_bridge("bridgecollapsed2", 2121, 4686, 2124, 4686)
            cage_route = cage_route .. walk_hops("toCage2", { { 2129, 4691 }, { 2134, 4698 }, { 2138, 4702 }, { 2133, 4703 } })
            t.check("walk-searchCage", math.abs(here().x - 2133) <= 3 and math.abs(here().z - 4703) <= 3 and here().level == 1, cage_route .. " now " .. here().x .. "," .. here().z .. " L" .. here().level)
            t.exec("searchCage", t.player.click_loc, "upass_cage_dummy", 1, { at = { 2134, 4702 } })
            t.ticks(10)
            t.check("searchCage-dove", var("varb9116_upass_dove_on_doll") == "1", "upass_dove_on_doll " .. var("varb9116_upass_dove_on_doll") .. " hp " .. hp_now() .. " :: " .. last_lines(4))
            -- killDisciple: the Disciples of Iban stand before the temple (upass_encounters.rs2:340; ibanmonk 2149-2156,4646-4649). A static BFS shows the way:
            -- back west over the cage platform, east along the z~4686 ledge, then north over the two collapsed bridges 2162,4663 and 2161,4654.
            eat_if_low(90)
            local temple_route = walk_hops("toTemple", { { 2129, 4691 }, { 2132, 4686 }, { 2143, 4685 }, { 2153, 4683 }, { 2158, 4676 }, { 2164, 4670 }, { 2163, 4666 } })
            temple_route = temple_route .. cross_bridge("bridgecollapsed1", 2162, 4663, 2162, 4660)
            temple_route = temple_route .. walk_hops("toTemple2", { { 2161, 4657 } })
            temple_route = temple_route .. cross_bridge("bridgecollapsed2", 2161, 4654, 2160, 4651)
            temple_route = temple_route .. walk_hops("toTemple3", { { 2157, 4649 } })
            t.check("walk-killDisciple", here().level == 1 and here().z <= 4652 and here().x >= 2150, temple_route .. " now " .. here().x .. "," .. here().z .. " L" .. here().level)
            -- room for the robes and the damp cloth: the spent tools go on the floor
            for _, spent in ipairs({ "spade", "bucket_empty", "tinderbox" }) do
                if select(2, t.inv.count(spent)) > 0 then
                    t.player.drop(spent)
                    t.ticks(2)
                end
            end
            local disciple_attack, disciple_attack_detail = t.player.attack("ibanmonk", 2, 40)
            t.ticks(2)
            local disciple_dead, disciple_dead_detail = t.npc.await_dead_engaged(300, 20, { eat = { item = "lobster", below = 45 } })
            t.ticks(3)
            local after_kind = t.chat.kind()
            if after_kind ~= "none" then
                t.chat.continue_()
                t.ticks(2)
            end
            t.check("killDisciple", disciple_dead == "ok", "attack " .. tostring(disciple_attack) .. " (" .. tostring(disciple_attack_detail) .. ") dead " .. tostring(disciple_dead) .. " (" .. tostring(disciple_dead_detail) .. "); chat " .. tostring(after_kind) .. "; hp " .. hp_now() .. " :: " .. last_lines(3))
            t.exec("take-zamrobetop", t.player.click_obj, "zamrobetop", 3)
            t.ticks(2)
            t.exec("take-zamrobebottom", t.player.click_obj, "zamrobebottom", 3)
            t.ticks(2)
            t.check("killDisciple-robes", select(2, t.inv.count("zamrobetop")) == 1 and select(2, t.inv.count("zamrobebottom")) == 1, "zamrobetop x" .. tostring(select(2, t.inv.count("zamrobetop"))) .. " zamrobebottom x" .. tostring(select(2, t.inv.count("zamrobebottom"))))
            -- enterTemple: the doors admit only a worshipper whose worn slots hold exactly the two monk robes (upass_tomb.rs2:141), so the
            -- scimitar and the gauntlets come off first
            eat_if_low(99)
            t.exec("unequip-adamant_scimitar", t.player.unequip, "adamant_scimitar")
            t.ticks(2)
            t.exec("unequip-klanks_gauntlets", t.player.unequip, "klanks_gauntlets")
            t.ticks(2)
            t.exec("wear-zamrobetop", t.player.equip, "zamrobetop")
            t.ticks(2)
            t.exec("wear-zamrobebottom", t.player.equip, "zamrobebottom")
            t.ticks(2)
            local doll_flags = "venom " .. var("varb9115_upass_venom_on_doll") .. " dove " .. var("varb9116_upass_dove_on_doll") .. " ashes " .. var("varb9117_upass_ashes_on_doll") .. " shadow " .. var("varb9118_upass_shadow_on_doll")
            t.check("enterTemple-robes", select(2, t.inv.count("zamrobetop")) == 0 and select(2, t.inv.count("zamrobebottom")) == 0 and select(2, t.inv.count("adamant_scimitar")) == 1, "robes worn (none left in the backpack), scimitar in the backpack; doll: " .. doll_flags)
            t.player.walk_to(2147, 4648, 20)
            t.ticks(1)
            t.exec("enterTemple", t.player.click_loc, "upass_templedoor_closed_right", 1)
            t.ticks(6)
            t.check("enterTemple-inside", here().x <= 2143 and here().level == 1, "now " .. here().x .. "," .. here().z .. " L" .. here().level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. " (confronted_iban) :: " .. last_lines(5))
            -- useDollOnWell: the doll goes onto the altar pit (upass_tomb.rs2:186); Iban stands at 2133,4647 and hits hard, so no pause
            local altar = t.player.by_symbol("loc", "cave_temple_altar")
            t.exec("useDollOnWell", t.player.use_on, "ibandoll", altar)
            t.ticks(24)
            t.check("useDollOnWell-iban-dead", select(2, t.quest.stage()) == 9 and select(2, t.inv.count("ibanstaff")) == 1, "upass stage " .. tostring(select(2, t.quest.stage())) .. " (defeated_iban); ibanstaff x" .. tostring(select(2, t.inv.count("ibanstaff"))) .. "; hp " .. hp_now() .. "; now " .. here().x .. "," .. here().z .. " L" .. here().level .. " :: " .. last_lines(4))
            -- seam37 upass_iban_lock_and_exit: the throw lands the player at 2482,9607 (upass_tomb.rs2 p_teleport 0_38_150_50_7).
            -- talkToKoftikAfterTemple: Koftik is caveguide6 at 2443,9607 (m38_150.spawn:8; LostCity's caveguide5 at the same tile).
            t.ticks(10)
            local start_pocket = here()
            t.check("leg.8.thrown-to-pocket", start_pocket.level == 0 and start_pocket.z >= 9601 and start_pocket.z <= 9613 and start_pocket.x >= 2439, "now " .. start_pocket.x .. "," .. start_pocket.z .. " L" .. start_pocket.level .. "; upass stage " .. tostring(select(2, t.quest.stage())) .. " :: " .. last_lines(4))
            local pocket_walk, pocket_walk_detail = t.player.walk_to(2446, 9607, 60)
            t.ticks(1)
            local after_walk = here()
            t.check("leg.8.unlocked-walk", math.abs(after_walk.x - 2446) <= 2 and math.abs(after_walk.z - 9607) <= 2, "walk_to 2446,9607 " .. tostring(pocket_walk) .. " (" .. tostring(pocket_walk_detail) .. "): from " .. start_pocket.x .. "," .. start_pocket.z .. " to " .. after_walk.x .. "," .. after_walk.z .. " L" .. after_walk.level)
            local koftik_seen, koftik_row = t.npc.nearest("caveguide6", 12)
            t.check("leg.8.koftik_present", koftik_seen == "ok", "caveguide6 " .. tostring(koftik_seen) .. " " .. tostring(type(koftik_row) == "table" and (koftik_row.x .. "," .. koftik_row.z) or koftik_row))
            -- probe: is Koftik himself reachable from the pocket, or only the Cave he stands by (LC [oploc1,upass_last_out])?
            local talk_result, talk_detail = t.player.talk_to("caveguide6", 1)
            t.check("probe.talk_caveguide6", true, "talk_to caveguide6 -> " .. tostring(talk_result) .. " (" .. tostring(talk_detail) .. ") from " .. here().x .. "," .. here().z)
            if talk_result ~= "ok" then
                t.exec("talkToKoftikAfterTemple", t.player.click_loc, "upass_last_out", 1)
            end
            t.exec("talkToKoftikAfterTemple-dialog", t.chat.play, {
                "npc:Traveller, where am I",
                "player:We were losing you",
                "npc:of course, the voices",
                "player:Iban's dead",
                "npc:You've done well",
                "player:At last! I've had enough of caves",
            })
            t.ticks(6)
            local led_out = here()
            t.check("talkToKoftikAfterTemple-led-out", led_out.x == 2481 and led_out.z == 9717 and led_out.level == 0, "now " .. led_out.x .. "," .. led_out.z .. " L" .. led_out.level .. " :: " .. last_lines(3))
            -- the way out of the pass is the cave exit at 2496,9713 (m39_151.jl2; upass_entrance.rs2:28 -> 2436,3315)
            t.exec("leaveThePass", t.player.click_loc, "cave_exit_upass", 1)
            t.ticks(6)
            local surface = here()
            t.check("leaveThePass-surface", surface.z < 4000, "now " .. surface.x .. "," .. surface.z .. " L" .. surface.level .. " :: " .. last_lines(3))
            -- goUpToLathasToFinish / talkToKingLathasAfterTemple (king_lathas.rs2:149 case defeated_iban)
            t.exec("goto-goUpToLathasToFinish", t.player.goto_tile, 2572, 3295, 0)
            t.exec("goUpToLathasToFinish", t.player.click_loc, "stairs", 1)
            t.ticks(3)
            t.check("goUpToLathasToFinish-level", here().level == 1, "now " .. here().x .. "," .. here().z .. " L" .. here().level)
            t.exec("goto-talkToKingLathasAfterTemple", t.player.goto_tile, 2578, 3292, 1)
            t.exec("talkToKingLathasAfterTemple", t.player.talk_to, "kinglathas", 1)
            t.exec("talkToKingLathasAfterTemple-dialog", t.chat.play, {
                "npc:The traveller returns",
                "player:Indeed, the quest is complete",
                "npc:Once our mages",
                "player:I will be ready",
                "npc:Your loyalty",
            })
            t.ticks(3)
            t.quest.expect_complete()
            t.finish(0)
            return
            -- LEG 8 END
        end },
    },
}

