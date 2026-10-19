-- toa_crystals: the teleport crystals into Zebak's, Kephri's, Akkha's, Ba-Ba's and the Wardens'
-- rooms. For each, a solo raider enters the room (::toa), checks it has not started, uses the
-- crystal (Quick-Use; the Wardens' has only Use) and must land on the room's crystal tile with the
-- room started. Tiles from RSProx (docs/rsprox-analysis/toa/README.md): the fight tile, except
-- Kephri's crystal puts the raider a tile east of it.
local function now(t) local _, k = t.tick() return k or -1 end
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end

local function state(t)
    t.cheat("::toastate", false)
    t.ticks(1)
    local line
    -- newest first: the first toastate line is this call's
    for _, l in ipairs(t.raid.cox_messages(20)) do
        if not line and l:find("^toastate") then line = l end
    end
    if not line then return nil end
    local started = tonumber(line:match("started=(%-?%d+)"))
    local fx, fz, fp = line:match("fight=(%-?%d+),(%-?%d+),(%-?%d+)")
    return started, tonumber(fx), tonumber(fz), tonumber(fp)
end

local ROOMS = {
    { id = 3, name = "zebak", loc = "zebak_teleport", op = 2, dx = 0 },
    { id = 5, name = "kephri", loc = "kephri_teleport", op = 2, dx = 1 },
    { id = 7, name = "akkha", loc = "akkha_teleport", op = 2, dx = 0 },
    { id = 9, name = "baba", loc = "baba_teleport", op = 2, dx = 0 },
    { id = 10, name = "wardens", loc = "wardens_teleport", op = 1, dx = 0 },
}

return {
    id = "toa_crystals",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = { "::clearinv" },
    run = function(t)
        t.ticklog.start()
        for _, r in ipairs(ROOMS) do
            t.ticklog.mark("crystal." .. r.name)
            t.cheat("::toa " .. r.id, false)
            t.ticks(3)
            keep_alive(t)
            local started, fx, fz = state(t)
            t.expect("crystal." .. r.name .. ".waiting", started == 0 and "ok" or "fail",
                string.format("started=%s before the crystal", tostring(started)))
            local res, detail = t.player.click_loc(r.loc, r.op)
            t.ticks(4)
            keep_alive(t)
            local after = state(t)
            local _, tile = t.world.tile()
            local want_x, want_z = fx + r.dx, fz
            local landed = tile and tile.x == want_x and tile.z == want_z
            t.expect("crystal." .. r.name .. ".teleport", (landed and after == 1) and "ok" or "fail",
                string.format("%s %s; on %s,%s want %d,%d; started=%s; tick %d", tostring(res),
                    tostring(detail), tostring(tile and tile.x), tostring(tile and tile.z), want_x, want_z,
                    tostring(after), now(t)))
        end
    end,
}
