-- lumbridge_tidy_up: who stands where after the Lumbridge tidy-up (3 November
-- 2026, rev 242), from the rev-242.1 recordings that had each spot in view.
--   The courtyard (RSProx index 4465, 4466, 4478, respawned on 3221,3221):
--   Mikasi in x 3232-3234, z 3214-3216; no Leagues tutor (rev 241: 3226,3215),
--   no Nigel (rev 241: 3243,3201).
--   The plane-2 bank (index 4466 [751]): a banker on the banker tutor's 3208,3222.
--   The Grand Exchange (index 4474, 4466, 4471, 4473, 4475, 4477, 4478): the
--   Leagues tutor 3155,3512, Nigel 3151-3152 x 3512-3513, Nevet 3154,3514,
--   Arthur 3158,3469.
--   Varrock square (index 4466, 4477 within 2 tiles of 3209,3435): no Nevet.
local function at(t, sym, range)
    local r, row = t.npc.nearest(sym, range)
    if r == "ok" and type(row) == "table" and row.x then
        return row
    end
    return nil
end

local function present(t, name, sym, x0, x1, z0, z1)
    local row = at(t, sym, 15)
    t.check(name, row ~= nil and row.x >= x0 and row.x <= x1 and row.z >= z0 and row.z <= z1,
            row and (row.x .. "," .. row.z) or "not sent")
end

-- Every "absent" row means something only if the bot is where it was sent.
local function go(t, name, x, z, level)
    t.cheat(string.format("::goto %d %d %d", x, z, level))
    t.ticks(3)
    local _, tile = t.world.tile()
    t.check(name .. ".at", tile ~= nil and tile.x == x and tile.z == z and select(2, t.world.level()) == level,
            tile and (tile.x .. "," .. tile.z) or "no tile")
end

local function absent(t, name, sym)
    local row = at(t, sym, 15)
    t.check(name, row == nil, row and ("sent on " .. row.x .. "," .. row.z) or "")
end

return {
    id = "lumbridge_tidy_up",
    fixture = "fresh_lumbridge.ini",
    setup = {},
    run = function(t)
        go(t, "courtyard", 3221, 3221, 0)
        present(t, "courtyard.mikasi", "mikasi_lumbridge_diary", 3232, 3234, 3214, 3216)
        absent(t, "courtyard.no_league_tutor", "league_tutor")
        absent(t, "courtyard.no_nigel", "deadman_nigel")

        go(t, "bank", 3208, 3220, 2)
        present(t, "bank.banker", "banker2_new", 3208, 3208, 3222, 3222)

        go(t, "ge", 3155, 3505, 0)
        present(t, "ge.league_tutor", "league_tutor", 3155, 3155, 3512, 3512)
        present(t, "ge.nigel", "deadman_nigel_varrock", 3151, 3153, 3511, 3513)
        present(t, "ge.nevet", "events_shop", 3154, 3154, 3514, 3514)

        go(t, "ge_south", 3158, 3472, 0)
        present(t, "ge.arthur", "clue_hunter_arthur", 3158, 3158, 3469, 3469)

        go(t, "varrock_square", 3210, 3433, 0)
        absent(t, "varrock_square.no_nevet", "events_shop")
        t.expect("lumbridge_tidy_up.run", "ok", "done")
    end,
}
