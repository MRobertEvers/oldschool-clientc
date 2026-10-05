-- r6 probe (b56 final sample, wanted lua:783): the pos6 swamp-caves EXIT branch has never
-- been drawn. Enter the caves through the real entrance exactly as enter_pool(_, 19) does,
-- reach the end-of-caves tile, then run the test's pos6 exit block VERBATIM (walk_route
-- copied unchanged) back to the rope and up, and assert the surface tile.
return {
    id = "wanted_cave_exit",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give rune_scimitar 1",
        "::wield rune_scimitar",
        "::give slayer_helm 1",
        "::wield slayer_helm",
        "::give candle_lantern_lit 1",
        "::give rope 1",
        "::give shark 4",
    },
    run = function(t)
        local function tile_text(r, tt)
            return r == "ok" and (tt.x .. "," .. tt.z .. "," .. tt.level) or tostring(r)
        end

        -- copied unchanged from test/quests/wanted.lua
        local function walk_route(prefix, way)
            for wi, wp in ipairs(way) do
                local cw = t.player.walk_to(wp[1], wp[2], 40)
                if cw ~= "ok" then
                    local wr, wt = t.world.tile()
                    if not (wr == "ok" and math.abs(wt.x - wp[1]) <= 2 and math.abs(wt.z - wp[2]) <= 2) then
                        t.step(prefix .. wi, "FAIL", "walk_to " .. wp[1] .. "," .. wp[2] .. " -> " .. tostring(cw) .. " at " .. tile_text(wr, wt))
                    end
                end
            end
        end

        -- the probe may goto its starting point: the pool 19 hop (POOL[19])
        t.exec("probe.goto", t.player.goto_tile, 3170, 3176, 0)
        -- enter_pool(pfx, 19), copied unchanged with pfx = "pos4"
        local pfx = "pos4"
        t.exec(pfx .. ".goDownToLumbridgeSwampCaves", t.player.click_loc, "goblin_cave_entrance", 1)
        t.ticks(4)
        do local er, et = t.world.tile(); t.check(pfx .. ".in_caves", er == "ok" and et.z > 9000, tile_text(er, et)) end
        walk_route(pfx .. ".caveWalk", {{3158, 9573}, {3146, 9573}, {3149, 9564}, {3157, 9560}, {3164, 9555}, {3174, 9557}, {3186, 9557}, {3194, 9553}, {3203, 9556}, {3212, 9559}, {3221, 9556}})
        t.exec(pfx .. ".crossSteppingStone", t.player.click_loc, "swamp_cave_steppingstone_b", 1)
        t.ticks(6)
        local sw = t.player.walk_to(3222, 9548, 20)
        local swr, swt = t.world.tile()
        t.check(pfx .. ".walkToEndOfCaves", swr == "ok" and swt.z < 9554, "walk_to the end of the caves (south of the stepping stone at 3221,9554) -> " .. tostring(sw) .. " tile " .. tile_text(swr, swt))
        do
            local zr, zt = t.world.tile()
            t.check("probe.in_pool_zone", zr == "ok" and zt.x >= 3216 and zt.x <= 3239 and zt.z >= 9540 and zt.z <= 9555,
                "tile " .. tile_text(zr, zt) .. " in the Lumbridge Swamp Caves zone x 3216-3239 z 9540-9555")
        end

        -- the pos6_id == 19 exit block of test/quests/wanted.lua, copied unchanged
        t.exec("pos6.recrossSteppingStone", t.player.click_loc, "swamp_cave_steppingstone_b", 1)
        t.ticks(6)
        -- the entry route reversed: the rope is reached only by the west detour round the
        -- cave-wall block at 3168-3170,9563-9565 (reach.py, every leg closed-door reachable)
        walk_route("pos6.caveWalkBack", {{3221, 9556}, {3212, 9559}, {3203, 9556}, {3194, 9553}, {3186, 9557}, {3174, 9557}, {3164, 9555}, {3157, 9560}, {3149, 9564}, {3146, 9573}, {3158, 9573}})
        -- probe only: the route really ended at its last waypoint before the rope click
        do
            local pr, pt = t.world.tile()
            t.check("probe.atRopeWaypoint", pr == "ok" and math.abs(pt.x - 3158) <= 2 and math.abs(pt.z - 9573) <= 2,
                "after pos6.caveWalkBack -> " .. tile_text(pr, pt) .. " (want within 2 of 3158,9573)")
        end
        t.exec("pos6.climbRopeOut", t.player.click_loc, "swamp_cave_climbing_rope", 1)
        t.ticks(4)
        do
            local ur, ut = t.world.tile()
            t.check("pos6.back_outside", ur == "ok" and ut.z < 9000, "tile " .. tile_text(ur, ut) .. " on the surface before the goto to Aubury")
        end
    end,
}
