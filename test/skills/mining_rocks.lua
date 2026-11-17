-- Mining coverage and respawn, against RSProx (OSRS-Content skill_mining
-- mine.dbrow / rocks.loc headers cite every value):
--   * prif_mine_ironrock1 mines and depletes to prif_mine_rocks1_empty
--     (index 1102 [12712]); before, "Mine" on it did nothing.
--   * a coal vein (lotr_mine_wall_coal_l) depletes to lotr_mine_wall_depleted_l
--     for 49 ticks (index 3046 [1071]: 2791,4438 mined from the tile south).
--   * Dwarven Mine iron respawns in 9 ticks (index 3122 [6915..6924]);
--     Mining Guild resource-area iron in 4 (index 3114, 3020,9721).
local function count(t, name)
    local _, n = t.inv.count(name)
    return n or 0
end

-- Mine the rock at (x, z) until an ore lands; returns the respawn gap in ticks
-- read from the tick log's loc_set rows on that tile (depleted -> restored).
local function mine(t, label, loc, x, z, ore, tele)
    t.cheat(tele)
    t.ticks(3)
    local before = count(t, ore)
    t.exec(label .. ".click", t.player.click_loc, loc, 1, { at = { x, z, 0 } })
    for _ = 1, 60 do
        if count(t, ore) > before then break end
        t.ticks(1)
    end
    t.check(label .. ".mined", count(t, ore) == before + 1,
        ore .. " " .. tostring(before) .. " -> " .. tostring(count(t, ore)))
    t.ticks(60)
    local _, rows = t.ticklog.rows({ kind = "loc_set", where = function(r) return r.x == x and r.z == z end })
    local ticks = {}
    for i = 1, #(rows or {}) do ticks[#ticks + 1] = rows[i].tick end
    if #ticks >= 2 then
        return ticks[2] - ticks[1], #ticks
    end
    return nil, #ticks
end

return {
    id = "mining_rocks",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel mining 99",
        "::setlevel hitpoints 99",
        "::give rune_pickaxe 1",
    },
    run = function(t)
        t.ticks(2)
        t.ticklog.start()

        local gap, n = mine(t, "prif", "prif_mine_ironrock1", 3286, 12464, "iron_ore", "::tele 0_51_194_22_49")
        t.check("prif.respawn", gap == 9, "gap " .. tostring(gap) .. " over " .. tostring(n) .. " loc rows")

        gap, n = mine(t, "vein", "lotr_mine_wall_coal_l", 2791, 4438, "coal", "::tele 0_43_69_39_21")
        t.check("vein.respawn", gap == 49, "gap " .. tostring(gap) .. " over " .. tostring(n) .. " loc rows")

        gap, n = mine(t, "dwarf", "ironrock2", 3036, 9775, "iron_ore", "::tele 0_47_152_29_47")
        t.check("dwarf.respawn", gap == 9, "gap " .. tostring(gap) .. " over " .. tostring(n) .. " loc rows")

        gap, n = mine(t, "guild", "ironrock1", 3020, 9721, "iron_ore", "::tele 0_47_151_13_56")
        t.check("guild.respawn", gap == 4, "gap " .. tostring(gap) .. " over " .. tostring(n) .. " loc rows")

        -- Lead (Sailing islands): unbound before; respawn 9 (index 3443, 50/50).
        gap, n = mine(t, "lead", "leadrock1", 2615, 8598, "lead_ore", "::tele 0_40_134_55_21")
        t.check("lead.respawn", gap == 9, "gap " .. tostring(gap) .. " over " .. tostring(n) .. " loc rows")
    end,
}
