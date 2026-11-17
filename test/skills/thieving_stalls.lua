-- Stall thieving against RSProx and the pinned wiki (OSRS-Content
-- skill_thieving/configs/stalls/stealing.dbrow cites each value):
--   * Fortis silk (fortis_market_stall_silk) steals silk and empties to
--     fortis_market_stall (index 1099), refilling 7 ticks later.
--   * Prifddinas silver (prif_marketstall_silver): "You steal a piece of
--     silver.", 205 xp, emptied to prif_marketstall_empty for 31 ticks.
--   * Mor Ul Rek's gem counter gives a gem and empties (wiki; RSProx 1138).
--   * Keldagrim's bakery sends "You attempt to steal some bread from the
--     baker's stall." (index 213 [6382]).
-- Before 2026-11-17 none of the three answered "Steal-from".
local function count(t, name)
    local _, n = t.inv.count(name)
    return n or 0
end

local function xp(t)
    local result, reading = t.skill.read("thieving")
    if result ~= "ok" or type(reading) ~= "table" then return nil end
    return reading.experience
end

local function steal(t, label, loc, x, z, level, tele)
    t.cheat(tele)
    t.ticks(3)
    t.exec(label .. ".click", t.player.click_loc, loc, 2, { at = { x, z, level } })
    t.ticks(4)
end

local function loc_rows(t, x, z)
    local _, rows = t.ticklog.rows({ kind = "loc_set", where = function(r) return r.x == x and r.z == z end })
    return rows or {}
end

return {
    id = "thieving_stalls",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel thieving 99",
        "::setlevel hitpoints 99",
    },
    run = function(t)
        t.ticks(2)
        t.ticklog.start()

        -- Fortis silk
        steal(t, "fortis", "fortis_market_stall_silk", 1675, 3114, 0, "::tele 0_26_48_12_44")
        t.check("fortis.silk", count(t, "silk") == 1, "silk " .. tostring(count(t, "silk")))
        t.ticks(12)
        local rows = loc_rows(t, 1675, 3114)
        local gap = #rows >= 2 and (rows[2].tick - rows[1].tick) or nil
        t.check("fortis.refill", gap == 7, "loc rows " .. tostring(#rows) .. ", gap " .. tostring(gap))

        -- Prifddinas silver
        local before = xp(t)
        steal(t, "prif", "prif_marketstall_silver", 3281, 6097, 0, "::tele 0_51_95_17_15")
        t.check("prif.ore", count(t, "silver_ore") == 1, "silver_ore " .. tostring(count(t, "silver_ore")))
        t.check("prif.line", t.msg.expect("You steal a piece of silver.") == "ok", "line")
        local after = xp(t)
        local gained = (before and after) and (after - before) or nil
        t.check("prif.xp", gained == 205 or gained == 2050, "xp gained " .. tostring(gained))
        t.ticks(34)
        local prow = loc_rows(t, 3281, 6097)
        local pgap = #prow >= 2 and (prow[2].tick - prow[1].tick) or nil
        t.check("prif.refill", pgap == 31, "loc rows " .. tostring(#prow) .. ", gap " .. tostring(pgap))

        -- Keldagrim bakery
        steal(t, "keld", "dwarf_market_bakery", 2891, 10210, 0, "::tele 0_45_159_11_32")
        t.check("keld.attempt", t.msg.expect("You attempt to steal some bread from the baker's stall.") == "ok", "attempt line")
        local baked = count(t, "cake") + count(t, "bread") + count(t, "chocolate_slice")
        t.check("keld.loot", baked == 1, "baked goods " .. tostring(baked))

        -- Mor Ul Rek gem counter
        local GEMS = { "uncut_sapphire", "sapphire", "uncut_emerald", "emerald", "uncut_ruby", "ruby",
            "uncut_diamond", "diamond", "uncut_dragonstone", "dragonstone", "uncut_onyx", "onyx" }
        steal(t, "tzhaar", "tzhaar_shopcounter_gem", 2522, 5071, 0, "::tele 0_39_79_27_15")
        local gems = 0
        for _, g in ipairs(GEMS) do gems = gems + count(t, g) end
        t.check("tzhaar.gem", gems == 1, "gems " .. tostring(gems))
        t.check("tzhaar.empties", #loc_rows(t, 2522, 5071) >= 1, "loc rows " .. tostring(#loc_rows(t, 2522, 5071)))
    end,
}
