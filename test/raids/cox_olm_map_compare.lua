-- cox_olm_map_compare: is the Olm chamber INSTANCE the template it copies?
--
-- The same views, tile for tile, from the original square (m50_89 plane 0,
-- un-instanced in the cache) and from the raid's copy of it (~cox_place_olm_chamber:
-- 5x7 zones from m50_89 + (16, 8) onto the Olm shelf, plane 2), with Olm NOT
-- woken so nothing but the map is in either picture. Shots come in pairs,
-- "src" then "inst", for a side-by-side comparison.
--
--   python3 tools/raid_gate/run.py cox_olm_map_compare --no-publish

-- chamber-local tiles (the template less (16, 8)) and the views from each
local SPOTS = {
    { 16, 30, "south end" }, { 16, 36, "centre" }, { 16, 42, "north end" },
}
local VIEWS = { { 0, "N" }, { 512, "W" }, { 1024, "S" }, { 1536, "E" } }

local function src_coord(lx, lz)
    local x, z = 3216 + lx, 5704 + lz
    return string.format("0_%d_%d_%d_%d", x // 64, z // 64, x % 64, z % 64)
end

local function shoot(t, label)
    for _, v in ipairs(VIEWS) do
        t.drive.camera(v[1], 260, 900)
        t.ticks(3)
        t.shot(label .. " look " .. v[2])
    end
end

return {
    id = "cox_olm_map_compare",
    fixture = "fresh_lumbridge.ini",
    frames = 4000,
    setup = { "::maxstats", "::godmode" },
    run = function(t)
        t.check("scope", true, "template m50_89 vs the Olm shelf instance")
        -- THE SOURCE: the template square as the cache has it
        for _, s in ipairs(SPOTS) do
            t.cheat("::tele " .. src_coord(s[1], s[2]), false)
            t.ticks(6)
            local r, tile = t.world.tile()
            t.check("src.tile", r == "ok", string.format("%s at %d,%d,%d", s[3], tile.x, tile.z, tile.level))
            shoot(t, "src " .. s[3])
        end
        -- THE INSTANCE: the raid's copy, Olm asleep (the corridor cheat does not
        -- spawn him; the barrier does)
        t.cheat("::coxolmrush", false)
        t.ticks(6)
        local r0, corr = t.world.tile()
        t.check("inst.corridor", r0 == "ok", string.format("%d,%d,%d", corr.x, corr.z, corr.level))
        -- the corridor tile is chamber-local (16, 16)
        local ox, oz = corr.x - 16, corr.z - 16
        for _, s in ipairs(SPOTS) do
            local x, z = ox + s[1], oz + s[2]
            t.cheat(string.format("::tele %d_%d_%d_%d_%d", corr.level, x // 64, z // 64, x % 64, z % 64), false)
            t.ticks(6)
            local r, tile = t.world.tile()
            t.check("inst.tile", r == "ok", string.format("%s at %d,%d,%d", s[3], tile.x, tile.z, tile.level))
            shoot(t, "inst " .. s[3])
        end
        t.check("done", true, "")
    end,
}
