-- quest-driver / tob_sotetseg_hm: SOTETSEG, HARD MODE, five raiders.
--
--   t.raid.sotetseg_hm_solve(opts) -> result, detail, record
--
-- The Normal solver (tob_sotetseg.lua: his clock, the balls and ricochets,
-- the protection block, the slot zones) with Hard Mode's two changes
-- (wiki Theatre_of_Blood/Hard_Mode :340-349, Sotetseg :101-102):
--
--   the maze     "all but one player" goes into the realm and "the maze [is]
--                divided amongst them, based on orb order". Each runner is
--                shown only its band of the path: four parent locs
--                41750-41753, lit for the runner whose varbit 12269 holds its
--                band (tob_sotetseg.rs2 ~tob_sote_light_path). A runner walks
--                its own band and, everywhere else, only tiles a runner has
--                stood on: the band before it is walked by its owner and
--                followed, then it takes the lead on its own ("Each player
--                will run a portion of the maze ... transition between
--                players quickly", yt_9coCVByPHCw 5:00). The one left up top
--                waits off the arena grid until the maze ends: the arena's
--                tornado rises only for a step past its third row.
--   death balls  two at once, at two targets: "the players targeted by the
--                balls should stand next to the death-dot tile, while all
--                other players not targeted by them should stand on the
--                death-dot tile" (:346). Here EVERYONE stands on the dot,
--                targets too: a ball is split among the raiders within 1 of
--                its target (`~tob_sote_death_ball`, ^tob_sote_ball_split_
--                radius), so with both targets beside it each ball split four
--                ways and the three on the dot took a share of both - up to
--                94 of a 188 pair; all five on it split each five ways, 75 at
--                most, and nobody has to drink to full for it.
-- ==========================================================================

-- Hard Mode's overrides over a copy of the Normal table: a copy, not a
-- metatable (the plugin sandbox has no setmetatable; QD.SOTE is complete
-- once its file has loaded, and nothing writes its fields at run time).
QD.SOTE_HM = {
    -- five faces: west, east, north-centre and the north face's two ends
    STATION = { [1] = { x = 12, z = 40 }, [2] = { x = 18, z = 40 }, [3] = { x = 15, z = 43 },
                [4] = { x = 13, z = 43 }, [5] = { x = 17, z = 43 } },
    DOT = { x = 15, z = 35 },
    EXIT_X = 32,
    -- (on the dot a raider takes a fifth of BOTH balls, 75 at most: brews
    -- to 80 while one is incoming - 99, for the four-way split, spent a seat's
    -- brews in this room, all1 sb: 28 doses into the Nylocas, 1 out of here)
    DEATH_EAT = 80,
    -- every seat's Dragon Warhammer specials as he wakes, before the tentacle
    HAMMER_OPENER = true, OPENER_TICKS = 24,
    CARROT = 3,
}
for k, v in pairs(QD.SOTE) do if QD.SOTE_HM[k] == nil then QD.SOTE_HM[k] = v end end

function QD.raid._sote_hm_ids(S)
    local ids = S.ids
    if ids.band_varbit then return end
    local function sym(kind, name)
        local r, id = api_drive.symbol(kind, name)
        assert(r == "ok", "sotetseg_hm_solve: no " .. kind .. " named " .. name)
        return id
    end
    ids.band_varbit = sym("varbit", "varb12269_tob_sotetseg_hard_tiles_visible")
    ids.hard_lit = sym("loc", "tob_sotetseg_hard_lighttile")
    ids.parent = {}
    for k = 1, 4 do ids.parent[k] = sym("loc", "tob_sotetseg_hard_lighttile_parent_" .. k) end
end

-- THE TWO DEATH BALLS: the dot south of him, everyone on it, over each ball's
-- read window. -> true while one is incoming
function QD.raid._sote_hm_death_zones(S, F, add)
    local V = S.V
    local B = S.base
    local balls = {}
    for _, P in ipairs(S.incoming) do
        if P.kind == "death" and P.land >= F.tick then balls[#balls + 1] = P end
    end
    if #balls == 0 then return false end
    table.sort(balls, function(a, b) return a.pid < b.pid end)
    local dot = { x = B.x + V.DOT.x, z = B.z + V.DOT.z }
    local t0, t1 = nil, nil
    for _, P in ipairs(balls) do
        t0 = math.min(t0 or P.land, math.max(F.tick + 1, P.land - 2))
        t1 = math.max(t1 or P.land, P.land)
    end
    local tile = dot
    add.zone("death-hold", { x = tile.x, z = tile.z, size = 1, lo = 0, hi = 0, require = true,
        t0 = t0, t1 = t1, tier = "lethal", cost = 60 })
    add.pull({ x = tile.x, z = tile.z, size = 1, weight = 1.0, t0 = F.tick, t1 = t1 })
    return true
end

-- UP TOP: off the arena grid, south of it, until the maze ends.
function QD.raid._sote_hm_waiter(S, F, add)
    local V = S.V
    local B = S.base
    local G = V.GRID
    -- a runner back through the portal while the others finish (the maze
    -- ends on the first runner's portal, Blert Hard; the content's 4-tick
    -- check): beside him, off the grid (tob_sotetseg.lua _sote_back)
    if S.ran_maze == S.mazes then return QD.raid._sote_back(S, F, add) end
    for x = G.x0, G.x1 do
        for z = G.z0, G.z1 do
            add.forbid("grid", { x = B.x + x, z = B.z + z, t0 = F.tick, t1 = F.tick + V.H, tier = "lethal", cost = 50 })
        end
    end
    return { x = B.x + V.WAIT.x, z = B.z + V.WAIT.z }
end

-- IN THE REALM: my band's lit tiles and every tile a runner has stood on are
-- the path I know; the rest of the grid is forbidden. I go to the furthest
-- tile I know (behind whoever leads where I cannot see, then up my own band),
-- and off the north end once the path I know reaches the last row: the pull
-- is the exit and the unknown tiles are forbidden.
function QD.raid._sote_hm_runner(S, F, add)
    local V, ids = S.V, S.ids
    local R = V.REALM
    S.ran_maze = S.mazes
    local bx, bz = F.me.x - F.me.x % 64, F.me.z - F.me.z % 64
    local _, band = api_drive.varbit(ids.band_varbit)
    band = band or 0
    local known = {}
    local lr, rows = api_drive.locs(0)
    local lit = 0
    if lr == "ok" then
        for _, row in ipairs(rows) do
            if row.level == 3 and (row.resolved_loc_id == ids.hard_lit or (band > 0 and row.loc_id == ids.parent[band])) then
                known[row.x .. "," .. row.z] = { x = row.x, z = row.z }
                lit = lit + 1
            end
        end
    end
    if lit > 0 and S.band_logged ~= S.mazes then
        S.band_logged = S.mazes
        S.path_log[#S.path_log + 1] = "band" .. band .. ":" .. lit
        QD.raid._tob_trace(S, F.tick, "my band " .. band .. ": " .. lit .. " lit tiles")
    end
    -- the runners' tiles this maze (mine included): on the realm grid only
    -- A RUN'S MIDDLE TILE TOO: a runner two tiles on since last tick ran
    -- through one the server chose, the long axis first (a 2x2 move its
    -- diagonal), as the waiters up top read the glow (tob_sotetseg.lua); left
    -- out, the trail had a hole every second tile and nobody behind a runner
    -- could follow it before the tornado came up the path (s5e sb t212)
    S.rtrail = S.rtrail or {}
    S.rlast = S.rlast or {}
    local function on_grid(x, z)
        local lx, lz = x - bx, z - bz
        return lx >= R.x0 and lx <= R.x1 and lz >= R.z0 and lz <= R.z1
    end
    for _, rd in ipairs(F.raiders) do
        if on_grid(rd.x, rd.z) then
            S.rtrail[rd.x .. "," .. rd.z] = { x = rd.x, z = rd.z }
            local prev = S.rlast[rd.pid]
            if prev then
                local dx, dz = rd.x - prev.x, rd.z - prev.z
                local ax, az = math.abs(dx), math.abs(dz)
                if math.max(ax, az) == 2 then
                    local mx, mz
                    if ax == 2 and az == 2 then mx, mz = dx // 2, dz // 2
                    elseif ax == 2 then mx, mz = dx // 2, 0
                    else mx, mz = 0, dz // 2 end
                    if on_grid(prev.x + mx, prev.z + mz) then
                        S.rtrail[(prev.x + mx) .. "," .. (prev.z + mz)] = { x = prev.x + mx, z = prev.z + mz }
                    end
                end
            end
        end
        S.rlast[rd.pid] = { x = rd.x, z = rd.z }
    end
    for k, t in pairs(S.rtrail) do known[k] = t end
    -- THE PATH IN ITS OWN ORDER: steps from its row-0 tiles over the tiles I
    -- know (the path runs sideways and even back, so a straight pull - to the
    -- exit, or to the furthest tile - has a local minimum wherever it does:
    -- s5g sf, the whole party held on (33,30) with band 2 running on east
    -- to (36,30), until the tornado came up behind it)
    local dist, queue = {}, {}
    for k, t in pairs(known) do
        if t.z - bz == R.z0 then dist[k] = 0 queue[#queue + 1] = t end
    end
    table.sort(queue, function(a, b) return a.x < b.x or (a.x == b.x and a.z < b.z) end)
    local head, far = 1, nil
    while head <= #queue do
        local t = queue[head]
        head = head + 1
        local d = dist[t.x .. "," .. t.z]
        local fdd = far and dist[far.x .. "," .. far.z]
        if far == nil or d > fdd or (d == fdd and (t.x < far.x or (t.x == far.x and t.z < far.z))) then far = t end
        -- (eight ways: a runner cuts a corner diagonally and only the tiles a
        -- tick ends on are judged, so the trail it leaves joins diagonally -
        -- s5i sa, the party held on row 0 behind a corner cut)
        for _, o in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } }) do
            local k = (t.x + o[1]) .. "," .. (t.z + o[2])
            if known[k] and dist[k] == nil then
                dist[k] = d + 1
                queue[#queue + 1] = known[k]
            end
        end
    end
    local mine = dist[F.me.x .. "," .. F.me.z] or -1
    for x = R.x0, R.x1 do
        for z = R.z0, R.z1 do
            if not known[(bx + x) .. "," .. (bz + z)] then
                add.forbid("unknown", { x = bx + x, z = bz + z, t0 = F.tick, t1 = F.tick + V.H, tier = "lethal", cost = 50 })
            end
        end
    end
    for _, tn in ipairs(F.tornadoes) do
        add.zone("tornado", { x = tn.x - 1, z = tn.z - 1, size = (tn.size or 3) + 2, lo = 0, hi = 0,
            t0 = F.tick + 1, t1 = F.tick + 3, tier = "lethal", cost = 50 })
    end
    local nk = 0
    for _ in pairs(known) do nk = nk + 1 end
    local why = string.format("runner band %d knows %d, far %s", band, nk,
        far and ((far.x - bx) .. "," .. (far.z - bz)) or "-")
    if why ~= S.runner_why then
        S.runner_why = why
        QD.raid._tob_trace(S, F.tick, why .. " at " .. (F.me.x - bx) .. "," .. (F.me.z - bz))
    end
    -- THE PULL: CARROT steps ahead of me along the path I know (never a goal
    -- I can stand still in front of: a pull to the furthest known tile let the
    -- plan wait a tick and run two for the same arrival, every tick - s5a sc),
    -- and the exit once that path reaches the last row
    local goal = nil
    local last_row = R.z1 - R.z0
    local fd = far and dist[far.x .. "," .. far.z] or -1
    if far and far.z - bz - R.z0 >= last_row and mine >= fd - V.CARROT then
        -- the path's top tile; the step off it is the portal's own walk
        -- (QD.raid._sote_portal)
        goal = { x = far.x, z = far.z }
    elseif far then
        local want = math.min(fd, mine + V.CARROT)
        for k, d in pairs(dist) do
            local t = known[k]
            if d == want and (goal == nil or t.x < goal.x or (t.x == goal.x and t.z < goal.z)) then goal = t end
        end
    end
    -- the first band's runner holds on row 2 for the rest to land behind it
    local row = F.me.z - bz - R.z0
    if band == 1 then
        if row == 2 then S.row2_ticks = (S.row2_ticks or 0) + 1 elseif row < 2 then S.row2_ticks = 0 end
        local stop = nil
        if row < 0 then stop = 1 elseif row <= 2 and (S.row2_ticks or 0) < V.ROW2_HOLD then stop = 3 end
        if stop then
            for x = R.x0, R.x1 do
                add.forbid("hold", { x = bx + x, z = bz + R.z0 + stop, t0 = F.tick + 1, t1 = F.tick + 1,
                    tier = "lethal", cost = 50 })
            end
        end
    end
    return goal
end

function QD.raid._sote_hm_step(S, F)
    local V = S.V
    QD.raid._sote_hm_ids(S)
    QD.raid._sote_measure(S, F)
    if F.boss then S.seen_boss = true end
    local phase = QD.raid._sote_phase(S, F)
    if phase == "done" then
        QD.raid._tob_trace(S, F.tick, "his death")
        return "ok"
    end
    if phase ~= "maze_runner" then S.rtrail, S.rlast = nil, nil end
    if S.base == nil then
        QD.raid._tob_emit(S, F, nil, {})
        return nil
    end
    local intent = {}
    local spec, names, add = QD.raid._tob_spec(S, F, { h = V.H, beam = V.BEAM })
    local target, station = QD.raid._sote_fight_zones(S, F, add, phase)
    if phase == "death_ball" and QD.raid._sote_hm_death_zones(S, F, add) then station = nil end
    if phase == "maze_waiter" then station = QD.raid._sote_hm_waiter(S, F, add) end
    if phase == "maze_runner" then station = QD.raid._sote_hm_runner(S, F, add) end
    if phase == "maze_runner" and QD.raid._sote_portal(S, F) then phase, station = "maze_portal", nil end
    return QD.raid._sote_act(S, F, spec, names, add, phase, target, 1, station, intent)
end

function QD.raid.sotetseg_hm_solve(opts)
    assert(QD.raid.tob_mode == "hard", "sotetseg_hm_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.raid._sote_run("sotetseg_hm_solve", QD.SOTE_HM, QD.raid._sote_hm_step, opts)
end
