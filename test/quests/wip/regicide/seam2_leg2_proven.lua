-- Regicide (quest_regicide), written as a relay of legs (docs/quest_authoring/relay.md).
-- Prerequisites per Quest Helper: Underground Pass complete (and Biohazard, which gates its cave
-- entrance); Agility 56 and Crafting 10 are required levels (Crafting is a later leg's).

return {
    id = "regicide",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::setvar varp328_regicide_quest ^regicide_spoken_lathas", -- SCRATCH (seam2): leg 1 talks to Lathas; this copy starts at leg 2
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

            -- crossLedge: walk the mud tunnel to the ledge's east end (upass_obstacles.rs2:313) -> 2374,9638
            t.player.walk_to(2376, 9644, 40)
            t.exec("crossLedge", t.player.click_loc, "upass_ledge", 1)
            t.ticks(8)
            where("crossLedge-tile", 2374, 9638, 0)

            -- navigateMaze (seam2): the maze is five rock bridges over pits (walkway_upass_narrow_mid_top, op1 Cross,
            -- upass_obstacles.rs2:360 = LostCity upass_obstacles.rs2:291); a walk stops at each one (they block by the
            -- game), so each is clicked from its west side. A failed agility roll drops you under it (z-1, or z+1 at
            -- 2399,9632 and 2406,9632) for 5 damage; walk back round to the near side and click again.
            local function mz_here() local _, w = t.world.tile() return w end
            local function cross_rock_bridge(name, bx, bz, hops)
                local note = ""
                for attempt = 1, 6 do
                    for _, hop in ipairs(hops) do t.player.walk_to(hop[1], hop[2], 60) end
                    t.player.walk_to(bx - 1, bz, 60)
                    local before = mz_here()
                    local click_result = t.player.click_loc("walkway_upass_narrow_mid_top", 1, { at = { bx, bz } })
                    t.ticks(10)
                    local after = mz_here()
                    note = note .. "[" .. attempt .. " " .. tostring(click_result) .. " " .. before.x .. "," .. before.z .. "->" .. after.x .. "," .. after.z .. "] "
                    if after.x == bx + 1 and after.z == bz then break end
                    local _, hp = t.skill.read("hitpoints")
                    if type(hp) == "table" and hp.level and hp.level < 15 then t.player.inv_op("lobster", 1) t.ticks(2) end
                end
                local w = mz_here()
                t.check(name, w.x == bx + 1 and w.z == bz, "now " .. w.x .. "," .. w.z .. " :: " .. note)
            end
            cross_rock_bridge("navigateMaze-bridge2380", 2380, 9634, { { 2373, 9634 } })
            cross_rock_bridge("navigateMaze-bridge2387", 2387, 9631, { { 2384, 9634 }, { 2384, 9631 } })
            cross_rock_bridge("navigateMaze-bridge2392", 2392, 9627, { { 2389, 9631 }, { 2389, 9627 } })
            cross_rock_bridge("navigateMaze-bridge2399", 2399, 9632, { { 2395, 9627 }, { 2395, 9632 } })
            cross_rock_bridge("navigateMaze-bridge2406", 2406, 9637, { { 2403, 9632 }, { 2403, 9637 } })
            for _, hop in ipairs({ { 2421, 9637 }, { 2422, 9634 }, { 2422, 9610 }, { 2421, 9606 }, { 2419, 9605 } }) do
                t.player.walk_to(hop[1], hop[2], 40)
            end
            where("navigateMaze-pipeMouth", 2419, 9605, 0)

            -- goThroughPipe: upass_pipe6 at 2417,9605 crawls west; Underground Pass is complete, so the crawl lands 26
            -- tiles further west in the room where the unicorn died (upass_obstacles.rs2:410-414) -> 2387,9605
            local piped = false
            for _ = 1, 4 do
                t.player.click_loc("upass_pipe6", 1, { at = { 2417, 9605 } })
                t.ticks(16)
                if mz_here().x < 2395 then piped = true break end
            end
            local pw = mz_here()
            t.check("goThroughPipe", piped and math.abs(pw.x - 2387) <= 3 and pw.z == 9605, "after the pipe at " .. pw.x .. "," .. pw.z .. " level " .. pw.level)

            -- leaveUnicornArea: walk to the south face of upass_unicorn_doorl 2375,9611 (angle south) -> 2371,9666
            -- (upass_unicorn_tunnels.rs2:27-29)
            for _, hop in ipairs({ { 2378, 9605 }, { 2378, 9607 }, { 2375, 9607 }, { 2375, 9610 } }) do
                t.player.walk_to(hop[1], hop[2], 30)
            end
            where("goto-leaveUnicornArea-walk", 2375, 9610, 0)
            t.exec("leaveUnicornArea", t.player.click_loc, "upass_unicorn_doorl", 1, { at = { 2375, 9611 } })
            t.ticks(6)
            where("leaveUnicornArea-tile", 2371, 9666, 0)
            -- openIbansDoor: with the badges and the horn the door opens onto Iban's temple
            t.exec("goto-openIbansDoor", t.player.goto_tile, 2369, 9718, 0)
            t.exec("openIbansDoor", t.player.click_loc, "cavetempledoor2r", 1)
            t.ticks(6)
            where("openIbansDoor-tile")

            -- enterWell
            -- enterTemple (seam1): Quest Helper's line points (Regicide.java:530-551) from Iban's door landing, crossing the
            -- four collapsed bridges on the line (upass_obstacles.rs2:425; an agility roll, a fall drops to level 0), then
            -- Iban's temple doors send a Regicide player to the ruined temple (upass_tomb.rs2 open_iban_door)
            local function tpath(pts) for _, wp in ipairs(pts) do t.player.walk_to(wp[1], wp[2]) end end
            local function tbridge(name, sym, x, z) t.exec(name, t.player.click_loc, sym, 1, { at = { x, z } }); t.ticks(6) end
            tpath({ {2172,4723}, {2172,4686} })
            tbridge("crossBridgeA", "bridgecollapsed2", 2164, 4686)
            tpath({ {2161,4686}, {2161,4699}, {2157,4699}, {2154,4697} })
            tbridge("crossBridgeB", "bridgecollapsed1", 2154, 4690)
            tpath({ {2154,4686}, {2152,4685}, {2153,4682}, {2153,4678}, {2154,4676}, {2160,4676}, {2160,4670}, {2165,4670}, {2165,4667}, {2162,4667} })
            tbridge("crossBridgeC", "bridgecollapsed1", 2162, 4663)
            tpath({ {2161,4659} })
            tbridge("crossBridgeD", "bridgecollapsed2", 2161, 4654)
            t.player.walk_to(2147, 4648, 20)
            where("walkToTemple", 2147, 4648, 1)
            t.exec("enterTemple", t.player.click_loc, "upass_templedoor_closed_right", 1, { at = { 2143, 4648 } })
            t.ticks(4)
            where("enterTemple-tile", 2014, 4712, 1)
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
    },
}
