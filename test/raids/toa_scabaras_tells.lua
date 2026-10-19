-- toa_scabaras_tells: the Path of Scabaras for the tick log. A solo raider (kept alive with hitpoint
-- boosts) crosses the north barrier by its Quick-Pass, solves all four pad puzzles in the order the
-- room laid them (west north, the crawl hole, west south, east south, the stepping stone, east
-- north), then matches the memory game's pairs. One wrong simon-says plate, one wrong obelisk and
-- one memory mismatch are made on purpose, so the room's faults are in the log too. Scarabs and
-- rock falls run throughout. Before any of it, the raider steps onto a plate of
-- an east pad, which does not work until the barrier into its section opens:
-- "This device won't function yet." and nothing changes on the pad (the solo
-- recording's one). `::toascab` reads the room's hidden state (the layout, the tablet's
-- number, the lights-out board, the simon-says sequence, the obelisk order, the memory boards).
-- The tick log is what docs/rsprox-analysis/toa/scabaras.md compares against RSProx.
local function now(t) local _, k = t.tick() return k or -1 end
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        keep_alive(t)
        left = left - step
    end
end
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

-- The room's state, from the newest ::toascab lines (`cox_messages` lists the
-- newest first: the first match of each key wins).
local function scab(t)
    t.cheat("::toascab", false)
    t.ticks(1)
    local st = {}
    local function nums(text)
        local out = {}
        for v in text:gmatch("%-?%d+") do out[#out + 1] = tonumber(v) end
        return out
    end
    for _, line in ipairs(t.raid.cox_messages(60)) do
        local x, z = line:match("^toascab origin (%-?%d+) (%-?%d+)")
        if x and not st.ox then st.ox, st.oz = tonumber(x), tonumber(z) end
        local l = line:match("^toascab layout (.*)$")
        if l and not st.layout then st.layout = nums(l) end
        local n = line:match("^toascab target (%-?%d+)")
        if n and not st.target then st.target = tonumber(n) end
        n = line:match("^toascab lights (%-?%d+)")
        if n and not st.lights then st.lights = tonumber(n) end
        l = line:match("^toascab simon ([%d ]+) state")
        if l and not st.simon then st.simon = nums(l) end
        l = line:match("^toascab order (.*)$")
        if l and not st.order then st.order = nums(l) end
        l = line:match("^toascab mem ([%d ]+) matched")
        if l and not st.mem then st.mem = nums(l) end
        n = line:match("^toascab mem .* matched (%d+)")
        if n and not st.matched then st.matched = tonumber(n) end
    end
    return st
end

local PADS = { { 20, 24 }, { 20, 36 }, { 37, 24 }, { 37, 36 } }   -- WS, WN, ES, EN board south-west
local RING = { { 0, 4 }, { 2, 4 }, { 4, 4 }, { 4, 2 }, { 4, 0 }, { 2, 0 }, { 0, 0 }, { 0, 2 } }
local DIAMOND = { { 0, 2 }, { 1, 1 }, { 1, 3 }, { 2, 0 }, { 2, 2 }, { 2, 4 }, { 3, 1 }, { 3, 3 }, { 4, 2 } }
local OBELISK = { { 0, -1 }, { 2, -1 }, { 0, 5 }, { 2, 5 }, { 4, 5 } }
local GRID = { "93729", "76411", "86385", "62724", "19543" }   -- north row first

local function add_value(dx, dz) return tonumber(GRID[5 - dz]:sub(dx + 1, dx + 1)) end

return {
    id = "toa_scabaras_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel attack 99",
        "::setlevel strength 99", "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 4", false)
        wait(t, 3)
        local st = scab(t)
        local ox, oz = st.ox, st.oz
        local function abs(lx, lz) return ox + lx, oz + lz end
        local function tele(lx, lz)
            local x, z = abs(lx, lz)
            t.cheat(string.format("::tele 0_%d_%d_%d_%d", math.floor(x / 64), math.floor(z / 64), x % 64, z % 64), false)
            t.ticks(2)
        end
        local function step(lx, lz)
            local x, z = abs(lx, lz)
            t.player.walk_to(x, z, 12)
            t.ticks(1)
        end

        t.ticklog.mark("scab.cross")
        local bx, bz = abs(12, 38)
        t.expect("scab.barrier", t.player.click_loc("toa_path_barrier", 2, { at = { bx, bz } }))
        wait(t, 4)

        local function solve_lights(pad)
            local px, pz = PADS[pad][1], PADS[pad][2]
            st = scab(t)
            local board = st.lights
            local press = nil
            for set = 0, 255 do
                local b = board
                for k = 0, 7 do
                    if (set >> k) & 1 == 1 then
                        for _, j in ipairs({ (k + 7) % 8, k, (k + 1) % 8 }) do b = b ~ (1 << j) end
                    end
                end
                if b == 255 then press = set break end
            end
            tele(px + 2, pz + 2)
            for k = 0, 7 do
                if press and (press >> k) & 1 == 1 then
                    step(px + RING[k + 1][1], pz + RING[k + 1][2])
                    step(px + 2, pz + 2)
                end
            end
            wait(t, 2)
        end

        local function solve_addition(pad)
            local px, pz = PADS[pad][1], PADS[pad][2]
            -- from the west, so the walk to the tablet never crosses the board
            tele(px - 3, pz + 4)
            local tx, tz = abs(px - 2, pz + 4)
            t.expect("scab.tablet", t.player.click_loc("toa_scabaras_totaltiles_tablet", 1, { at = { tx, tz } }))
            wait(t, 2)
            st = scab(t)
            local want = st.target
            local path = nil
            local seen = {}
            local function dfs(x, z, sum, acc)
                if path then return end
                sum = sum + add_value(x, z)
                if sum > want then return end
                acc[#acc + 1] = { x, z }
                seen[x * 5 + z] = true
                if sum == want then
                    path = {}
                    for i, v in ipairs(acc) do path[i] = v end
                else
                    for _, d in ipairs({ { 1, 0 }, { 0, 1 }, { 0, -1 }, { -1, 0 } }) do
                        local nx, nz = x + d[1], z + d[2]
                        if nx >= 0 and nx <= 4 and nz >= 0 and nz <= 4 and not seen[nx * 5 + nz] then
                            dfs(nx, nz, sum, acc)
                        end
                    end
                end
                seen[x * 5 + z] = nil
                acc[#acc] = nil
            end
            for z = 0, 4 do dfs(0, z, 0, {}) end
            if not path then
                t.expect("scab.addition_path", "fail", "no path sums to " .. tostring(want))
                return
            end
            tele(px - 1, pz + path[1][2])
            for _, p in ipairs(path) do step(px + p[1], pz + p[2]) end
            wait(t, 2)
        end

        local function simon_walk(px, pz, tiles)
            for _, i in ipairs(tiles) do
                local d = DIAMOND[i + 1]
                tele(px + d[1] - 1, pz + d[2])
                step(px + d[1], pz + d[2])
                -- the plate counts on the tick after arriving: stand there across it
                t.ticks(1)
            end
        end

        local function solve_simon(pad)
            local px, pz = PADS[pad][1], PADS[pad][2]
            local bx2, bz2 = abs(px - 2, pz + 4)
            -- the push answers with a sound and the show, not a chat line: not graded
            t.player.click_loc("toa_scabaras_simonsays_button", 1, { at = { bx2, bz2 } })
            wait(t, 8)
            st = scab(t)
            -- one wrong plate first: the puzzle's fault and its hit
            local wrong = (st.simon[1] + 1) % 9
            t.ticklog.mark("scab.simon_wrong")
            simon_walk(px, pz, { wrong })
            wait(t, 4)
            t.player.click_loc("toa_scabaras_simonsays_button", 1, { at = { bx2, bz2 } })
            wait(t, 8)
            st = scab(t)
            simon_walk(px, pz, st.simon)
            wait(t, 2)
        end

        local function hit_obelisk(px, pz, i)
            local x, z = abs(px + OBELISK[i + 1][1], pz + OBELISK[i + 1][2])
            t.player.talk_to("toa_scabaras_guesser_obelisk", 2, { at = { x, z } })
            wait(t, 5)
        end

        local function solve_obelisks(pad)
            local px, pz = PADS[pad][1], PADS[pad][2]
            st = scab(t)
            tele(px + 2, pz + 2)
            t.ticklog.mark("scab.obelisk_wrong")
            hit_obelisk(px, pz, st.order[2])
            for _, i in ipairs(st.order) do hit_obelisk(px, pz, i) end
            wait(t, 3)
        end

        local function solve(pad)
            local kind = st.layout[pad]
            t.ticklog.mark("scab.pad" .. pad .. "." .. kind)
            if kind == 0 then solve_lights(pad)
            elseif kind == 1 then solve_addition(pad)
            elseif kind == 2 then solve_simon(pad)
            else solve_obelisks(pad) end
            st = scab(t)
        end

        -- an east pad before its section is open: both are shut until Puzzle 1 / 2
        t.ticklog.mark("scab.dead_device")
        do
            local plate = nil
            for _, pad in ipairs({ 3, 4 }) do
                local kind = st.layout[pad]
                local px, pz = PADS[pad][1], PADS[pad][2]
                if kind == 0 or kind == 1 then plate = { px, pz }
                elseif kind == 2 then plate = { px, pz + 2 } end
                if plate then break end
            end
            local lights = st.lights
            tele(plate[1] - 1, plate[2])
            step(plate[1], plate[2])
            wait(t, 2)
            st = scab(t)
            if said(t, "This device won't function yet.") and st.lights == lights then
                t.expect("scab.dead_device", "ok", "told, and nothing changed")
            else
                t.expect("scab.dead_device", "fail", string.format("lights %s -> %s", tostring(lights), tostring(st.lights)))
            end
        end

        solve(2)   -- west north
        t.ticklog.mark("scab.hole")
        tele(28, 37)
        local hx, hz = abs(28, 35)
        t.expect("scab.hole", t.player.click_loc("toa_scabaras_spawn_hole", 1, { at = { hx, hz } }))
        wait(t, 4)
        solve(1)   -- west south
        solve(3)   -- east south
        t.ticklog.mark("scab.stone")
        tele(40, 29)
        local sx, sz = abs(40, 32)
        t.expect("scab.stone", t.player.click_loc("toa_scabaras_stepping_stone", 1, { at = { sx, sz } }))
        wait(t, 5)
        solve(4)   -- east north

        t.ticklog.mark("scab.memory")
        st = scab(t)
        local function slot_tile(north, s)
            return 49 + 2 * (s % 3), (north and 36 or 24) + 2 * math.floor(s / 3)
        end
        local function open(north, s)
            local v = st.mem[(north and 9 or 0) + s + 1]
            local lx, lz = slot_tile(north, s)
            tele(lx - 1, lz)
            local x, z = abs(lx, lz)
            t.player.click_loc("toa_scabaras_memorygame_button" .. v, 1, { at = { x, z } })
            wait(t, 2)
        end
        local matched = {}
        for v = 1, 9 do matched[v] = ((st.matched >> (v - 1)) & 1) == 1 end
        local function slot_of(north, v)
            for s = 0, 8 do if st.mem[(north and 9 or 0) + s + 1] == v then return s end end
        end
        local first = true
        for v = 1, 9 do
            if not matched[v] then
                if first then
                    -- one mismatch: v on the south board against another number on the north
                    local other = nil
                    for w = 1, 9 do if w ~= v and not matched[w] then other = w break end end
                    t.ticklog.mark("scab.mem_wrong")
                    open(false, slot_of(false, v))
                    open(true, slot_of(true, other or v))
                    wait(t, 2)
                    first = false
                end
                open(false, slot_of(false, v))
                open(true, slot_of(true, v))
                wait(t, 2)
            end
        end
        wait(t, 4)
        t.ticklog.mark("scab.done")
        if said(t, "Challenge complete: Path of Scabaras") then
            t.expect("scab.run", "ok", string.format("complete on tick %d", now(t)))
        else
            t.expect("scab.run", "fail", "the room did not complete")
        end
    end,
}
