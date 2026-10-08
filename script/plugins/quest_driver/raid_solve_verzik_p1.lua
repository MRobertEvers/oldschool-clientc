-- quest-driver / raid_solve_verzik_p1: VERZIK PHASE 1, SOLVED FROM SCRATCH.
--
--   t.raid.verzik_p1_solve(opts) -> result, detail, record
--
-- Written from scratch on the drive API's primitives alone.  The decisions
-- come from the wiki's Phase 1 section
-- (docs/minigames/theater_of_blood/sources/wiki_Theatre_of_Blood_Strategies
-- .wikitext:875-894, "W:" below) and the geometry and clocks from the content
-- the server runs (OSRS-Content .../minigame_tob/scripts/tob_verzik.rs2 and
-- configs/tob.constant, "C:" below).  The owner, 2026-10-08: "The players
-- dodge ALL verzik attacks by running to the pillars.  The second row of
-- pillars is useable."  The target is zero damage taken in P1.
--
-- THE LOOP, once per server tick: MEASURE what this client is shown (her row
-- and its animation, the pillars and their health bars, my tile, hitpoints,
-- special energy, the Dawnbringer on the floor or in my pack), DECIDE through
-- one state machine, ACT with the driver's ordinary input verbs (walk, attack,
-- equip, the special orb, take, drop).  It reads nothing a player could not:
-- no tick log, no server registers.
--
-- THE TWO RULES THAT MAKE IT ZERO DAMAGE
--   1. At every shot I stand in the cover box of a STANDING pillar.  C:
--      ~tob_verzik_p1_shot hunts every raider at the launch; one in a standing
--      pillar's box (~tob_verzik_hiding_pillar) costs that pillar a hit and
--      takes nothing; anyone else takes up to 137 (68 prayed).  Npcs run
--      before players in a server tick (torirs_server_world.c
--      ToriRSServer_WorldTick: phase_npcs, then phase_players), so the shot
--      reads where I stood at the END of the tick before it, and a step
--      clicked for the shot's own tick is already safe to take.
--   2. I never hide behind a pillar the next shot could bring down.  C: a
--      pillar has 185 hitpoints and a shot takes 40-60 (^tob_verzik_pillar_
--      hit_min/_max); at 0 it collapses on everyone within 3 of its centre
--      (~tob_verzik_collapse_pillar, ^tob_verzik_pillar_collapse_range).  A
--      pillar whose hitpoints are surely above 60 cannot fall to one shot, so
--      the team moves on BEFORE its pillar can fall rather than after (W:892
--      "move east once the right one collapses" -- here, once it could).
--      Every raider runs the same rule on the same health bars, so the three
--      always pick the same pillar (W:890 "hide behind one pillar together":
--      one pillar, one hit).
--   And at the end: when her shield breaks she plays her death animation and
--   every pillar collapses three ticks later (C: ~tob_verzik_leave_throne,
--   ~tob_verzik_transit_tick, ^tob_verzik_p2_hop_ticks 3; W:892 "players
--   should stay away from them"), so on the animation every raider steps to
--   the nearest tile more than 3 from every pillar centre.
--
-- THE STATES (one machine per raider):
--   OPEN    the fight's first ticks: attack at once (W:890 "spam-click
--           Verzik"), until the first shot's deadline calls me back.
--   HIDE    in the team's cover box until the shot has been fired; the
--           Dawnbringer's drop, take and wield happen here.
--   STRIKE  the window between shots: attack (the Dawnbringer holder casts
--           its specials, W:885), until the run back must start.
--   RETURN  running to the nearest tile of the team pillar's box.
--   BREAK   her shield is down: get clear of every pillar's collapse.
--   DONE    every pillar is gone.
-- ==========================================================================

QD.VZP1 = {
    ATTACK_TICKS = 14,        -- C: ^tob_verzik_p1_attack_ticks; W:884 "every 14 ticks"
    FIRST_WINDUP = 18,        -- C: ^tob_verzik_p1_first_attack (wind-up 18, bolt 21)
    PILLAR_HP = 185,          -- C: verzik_pillar_npc stat4=185 (~tob_verzik_pillar_take_hit)
    PILLAR_HIT_MAX = 60,      -- C: ^tob_verzik_pillar_hit_max
    COLLAPSE_RANGE = 3,       -- C: ^tob_verzik_pillar_collapse_range (from the centre, foot+1)
    SPEC_COST = 350,          -- wiki Dawnbringer: "consuming 35% of the wielder's special attack energy"
    RUN = 2,                  -- tiles a tick, running (wiki Energy)
    ROUTE_SLACK = 0,          -- runs are measured in path steps round the pillars, so no slack:
                              -- a tick of it shut every second-row window (11 steps each way,
                              -- 6 + 1 + 6 = 13 of a 14-tick cycle)
    HP_EAT = 60,              -- eat in cover below this (a safety net: the plan takes no hits)
    TRACE_CAP = 160,
}

-- The server tick.  The leader's client (and scriptrun) reads its embedded
-- world; a party MEMBER's client holds no world and reads the tick the
-- leader stamped on its last lockstep frame (api_drive.session()).
function QD.raid._vzp1_now()
    local r, tick = api_drive.server_tick()
    if r == "ok" then return tick end
    local session = api_drive.session()
    assert(type(session) == "table" and math.type(session.lockstep_tick) == "integer",
        "verzik_p1_solve: no server tick and no lockstep tick")
    return session.lockstep_tick
end

function QD.raid._vzp1_cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

-- Chebyshev gap from a tile to a footprint (0 = on it, 1 = beside it).
function QD.raid._vzp1_gap(x, z, fx, fz, n)
    local gx = math.max(fx - x, x - (fx + n - 1), 0)
    local gz = math.max(fz - z, z - (fz + n - 1), 0)
    return math.max(gx, gz)
end

-- The ids this plan reads, by symbol, once.
function QD.raid._vzp1_ids()
    local function sym(kind, name)
        local r, id = api_drive.symbol(kind, name)
        assert(r == "ok", "verzik_p1_solve: no " .. kind .. " named " .. name)
        return id
    end
    return {
        p1 = sym("npc", "verzik_phase1"),
        initial = sym("npc", "verzik_initial"),
        transition = sym("npc", "verzik_phase1_to2_transition"),
        pillar = sym("npc", "verzik_pillar_npc"),
        windup = sym("seq", "verzik_phase1_attack_magic"),
        death = sym("seq", "verzik_phase1_death"),
        dawn = sym("obj", "verzik_special_weapon"),
        scythe = sym("obj", "scythe_of_vitur"),
        inv = sym("inv", "inv"),
        worn = sym("inv", "worn"),
        hitpoints = sym("stat", "hitpoints"),
        energy = sym("varp", "varp300_sa_energy"),
        food = sym("obj", "anglerfish"),
        backpack = QD.raid._vzp1_component("inventory:items"),
        spec_orb = QD.raid._vzp1_component("orbs:specbutton"),
    }
end

function QD.raid._vzp1_component(name)
    local r, id = api_drive.component(name)
    assert(r == "ok", "verzik_p1_solve: no component " .. name)
    return id
end

-- ------------------------------------------------------------------ MEASURE

function QD.raid._vzp1_measure(S)
    local ids = S.ids
    local v = { pillars = {}, ground_dawn = {} }
    v.tick = QD.raid._vzp1_now()
    local tr, tile = api_drive.player_tile()
    assert(tr == "ok", "verzik_p1_solve: no tile")
    v.me = tile
    local nr, rows = api_drive.npcs(0)
    if nr ~= "ok" then rows = {} end
    for _, row in ipairs(rows) do
        local id, base = row.npc_id, row.base_npc_id
        if id == ids.p1 or base == ids.p1 then
            v.boss = row
        elseif id == ids.initial or base == ids.initial then
            v.initial = row
        elseif id == ids.transition or base == ids.transition then
            v.after = row
        elseif id == ids.pillar then
            v.pillars[#v.pillars + 1] = row
        end
    end
    local hr, hp = api_drive.skill(ids.hitpoints)
    v.hp = (hr == "ok" and hp.level) or 0
    local er, energy = api_drive.varp(ids.energy)
    v.energy = (er == "ok" and energy) or 0
    local _, pack = api_drive.inv_count(ids.inv, ids.dawn)
    local _, worn = api_drive.inv_count(ids.worn, ids.dawn)
    v.dawn_pack, v.dawn_worn = pack or 0, worn or 0
    local _, scy = api_drive.inv_count(ids.worn, ids.scythe)
    v.scythe_worn = (scy or 0) > 0
    local orr, objs = api_drive.objs(0)
    if orr == "ok" then
        for _, o in ipairs(objs) do
            if o.obj_id == ids.dawn then v.ground_dawn[#v.ground_dawn + 1] = o end
        end
    end
    return v
end

-- --------------------------------------------------------------- GEOMETRY

-- The pillars I can see, keyed by their south-west tile, west or east of her,
-- and what is known of their hitpoints: a lower bound from the hitsplats
-- seen on them.
function QD.raid._vzp1_pillars(S, v)
    local out = {}
    local b = v.boss or v.initial
    local mid = b and (b.x + math.floor(((b.size or 1) - 1) / 2)) or S.mid_x
    if b then S.mid_x = mid end
    for _, row in ipairs(v.pillars) do
        local key = row.x .. "," .. row.z
        -- a hit is the pillar's own hitsplat, which every raider is shown:
        -- counting "the pillar I stood behind" let one raider caught in the
        -- open miss a hit the others made, choose another pillar than the
        -- team, and hide alone behind a pillar on its fourth hit (s8, t174)
        if row.hit_cycle and row.hit_cycle > (S.hit_cycle_seen[key] or 0) and (row.hit_damage or -1) > 0 then
            S.hit_cycle_seen[key] = row.hit_cycle
            S.hits[key] = (S.hits[key] or 0) + 1
        end
        local hits = S.hits[key] or 0
        -- the hits I counted, never the health bar: a bar read as "above 60"
        -- let a pillar take a fourth shot and fall on the team (sa run s3,
        -- t342: 6437,82 collapsed on all three).  Three hits, 120-180 taken,
        -- and the pillar is done for the team.
        local low = QD.VZP1.PILLAR_HP - QD.VZP1.PILLAR_HIT_MAX * hits
        out[#out + 1] = {
            key = key, x = row.x, z = row.z, size = row.size or 3,
            west = mid ~= nil and row.x < mid,
            cx = row.x + 1, cz = row.z + 1,   -- C: the centre is foot + 1
            low = low, hits = hits,
            safe = low > QD.VZP1.PILLAR_HIT_MAX,
        }
    end
    -- W:892: the near row first (she sits north: larger z is nearer), west
    -- before east ("directly south-west of Verzik, then move east"), then the
    -- second row (the owner: "The second row of pillars is useable").
    table.sort(out, function(a, b)
        if a.z ~= b.z then return a.z > b.z end
        return a.x < b.x
    end)
    return out
end

-- Is (x, z) inside any footprint (a pillar or her)?
function QD.raid._vzp1_blocked(v, pillars, x, z)
    for _, p in ipairs(pillars) do
        if QD.raid._vzp1_gap(x, z, p.x, p.z, p.size) == 0 then return true end
    end
    local b = v.boss or v.after or v.initial
    if b and QD.raid._vzp1_gap(x, z, b.x, b.z, b.size or 1) == 0 then return true end
    return false
end

-- C: ~tob_verzik_behind_pillar, the cover box off a pillar's south-west tile.
function QD.raid._vzp1_cover_tiles(p)
    local t = {}
    if p.west then
        for dx = -3, 0 do for dz = -3, 0 do t[#t + 1] = { p.x + dx, p.z + dz } end end
        t[#t + 1] = { p.x + 1, p.z - 1 }
        t[#t + 1] = { p.x - 1, p.z + 1 }
    else
        for dx = 2, 4 do for dz = -2, 0 do t[#t + 1] = { p.x + dx, p.z + dz } end end
        t[#t + 1] = { p.x + 1, p.z - 1 }
        t[#t + 1] = { p.x + 3, p.z + 1 }
    end
    return t
end

function QD.raid._vzp1_behind(p, x, z)
    for _, c in ipairs(QD.raid._vzp1_cover_tiles(p)) do
        if c[1] == x and c[2] == z then return true end
    end
    return false
end

-- Clear of every standing pillar's collapse?
function QD.raid._vzp1_collapse_safe(pillars, x, z)
    for _, p in ipairs(pillars) do
        if QD.raid._vzp1_cheb(x, z, p.cx, p.cz) <= QD.VZP1.COLLAPSE_RANGE then return false end
    end
    return true
end

-- Tiles from a tile to melee reach of her.
function QD.raid._vzp1_tiles_to_her(v, x, z)
    local b = v.boss
    if not b then return 0 end
    return math.max(QD.raid._vzp1_gap(x, z, b.x, b.z, b.size or 1) - 1, 0)
end

-- PATHS.  Steps on the arena floor with the pillars and her footprint as the
-- only obstacles (a diagonal step needs both of its side tiles open, as the
-- server's pathing has it).  A run of n steps takes ceil(n / 2) ticks.
--
-- THE BUDGET.  The live client kills a script that runs more than 400,000
-- Lua instructions between two waits (torirs_plugin_lua.c
-- PLUGIN_LUA_STEP_BUDGET), so nothing here searches the whole floor per
-- tick.  The floor, the steps to her and the steps to each pillar's box only
-- change when a pillar falls or she moves, so they are searched once and
-- kept (S.geo, S.covers); a search from my own tile is only made where one
-- is needed, and only as far as it can matter.

-- The floor as a flat array: index (x - x0) * H + (z - z0) + 1.
function QD.raid._vzp1_grid(v, pillars)
    local b = v.boss or v.after or v.initial
    local x0, x1, z0, z1 = v.me.x - 8, v.me.x + 8, v.me.z - 8, v.me.z + 8
    for _, p in ipairs(pillars) do
        x0, x1 = math.min(x0, p.x - 6), math.max(x1, p.x + p.size + 6)
        z0, z1 = math.min(z0, p.z - 6), math.max(z1, p.z + p.size + 6)
    end
    if b then
        x0, x1 = math.min(x0, b.x - 3), math.max(x1, b.x + (b.size or 1) + 3)
        z1 = math.max(z1, b.z + (b.size or 1) + 3)
    end
    local W, H = x1 - x0 + 1, z1 - z0 + 1
    local open = {}
    for i = 1, W * H do open[i] = true end
    local function shut(fx, fz, n)
        for dx = 0, n - 1 do
            for dz = 0, n - 1 do
                local x, z = fx + dx - x0, fz + dz - z0
                if x >= 0 and x < W and z >= 0 and z < H then open[x * H + z + 1] = false end
            end
        end
    end
    for _, p in ipairs(pillars) do shut(p.x, p.z, p.size) end
    if b then shut(b.x, b.z, b.size or 1) end
    return { x0 = x0, z0 = z0, W = W, H = H, open = open }
end

-- Steps from the sources to every tile within `maxd` (all when nil).
function QD.raid._vzp1_bfs(G, sources, maxd)
    local W, H, open, x0, z0 = G.W, G.H, G.open, G.x0, G.z0
    local dist, qx, qz, tail, head = {}, {}, {}, 0, 1
    for _, t in ipairs(sources) do
        local x, z = t.x - x0, t.z - z0
        if x >= 0 and x < W and z >= 0 and z < H then
            local i = x * H + z + 1
            if dist[i] == nil then
                dist[i] = 0
                tail = tail + 1
                qx[tail], qz[tail] = x, z
            end
        end
    end
    maxd = maxd or 100000
    while head <= tail do
        local x, z = qx[head], qz[head]
        head = head + 1
        local i = x * H + z + 1
        local d = dist[i] + 1
        if d <= maxd then
            for dx = -1, 1 do
                local nx = x + dx
                if nx >= 0 and nx < W then
                    for dz = -1, 1 do
                        local nz = z + dz
                        if (dx ~= 0 or dz ~= 0) and nz >= 0 and nz < H then
                            local n = nx * H + nz + 1
                            if dist[n] == nil and open[n]
                                and (dx == 0 or dz == 0 or (open[nx * H + z + 1] and open[x * H + nz + 1])) then
                                dist[n] = d
                                tail = tail + 1
                                qx[tail], qz[tail] = nx, nz
                            end
                        end
                    end
                end
            end
        end
    end
    return dist
end

function QD.raid._vzp1_steps(G, dist, x, z)
    local gx, gz = x - G.x0, z - G.z0
    if gx < 0 or gx >= G.W or gz < 0 or gz >= G.H then return 999 end
    return dist[gx * G.H + gz + 1] or 999
end

-- The tiles she can be meleed from: beside her footprint, not diagonal.
function QD.raid._vzp1_ring(v)
    local b = v.boss or v.initial
    if not b then return {} end
    local n, t = b.size or 1, {}
    for i = 0, n - 1 do
        t[#t + 1] = { x = b.x + i, z = b.z - 1 }
        t[#t + 1] = { x = b.x + i, z = b.z + n }
        t[#t + 1] = { x = b.x - 1, z = b.z + i }
        t[#t + 1] = { x = b.x + n, z = b.z + i }
    end
    return t
end

-- Ticks for a run of `steps`, with `slack` ticks on top.
function QD.raid._vzp1_ticks(steps, slack)
    if steps <= 0 then return 0 end
    return math.ceil(steps / QD.VZP1.RUN) + (slack or 0)
end

-- The floor, the ring and the steps to her: kept until a pillar falls or she
-- moves.
function QD.raid._vzp1_geo(S, v, pillars)
    local b = v.boss or v.after or v.initial
    local parts = { b and (b.x .. "," .. b.z .. "," .. tostring(b.size)) or "-" }
    for _, p in ipairs(pillars) do parts[#parts + 1] = p.key end
    local sig = table.concat(parts, ";")
    if S.geo == nil or S.geo.sig ~= sig then
        local G = QD.raid._vzp1_grid(v, pillars)
        local ring = QD.raid._vzp1_ring(v)
        S.geo = { sig = sig, G = G, ring = ring, to_ring = QD.raid._vzp1_bfs(G, ring) }
        S.covers = {}
        S.item_paths = {}
    end
    return S.geo
end

-- The team's pillar: the first in W:892's order that the next shot cannot
-- bring down, and every open tile of its cover box.  Any tile of the box is
-- cover (C: ~tob_verzik_hiding_pillar asks only "in the box?"), so a raider
-- runs to the box tile nearest it, never round the pillar to a fixed one.
function QD.raid._vzp1_cover_for(S, v, pillars, p)
    local geo = S.geo
    local c = S.covers[p.key]
    if c == nil then
        local tiles = {}
        for _, t in ipairs(QD.raid._vzp1_cover_tiles(p)) do
            if not QD.raid._vzp1_blocked(v, pillars, t[1], t[2]) then
                tiles[#tiles + 1] = { x = t[1], z = t[2] }
            end
        end
        c = false
        if #tiles > 0 then
            local to_box = QD.raid._vzp1_bfs(geo.G, tiles)
            -- the shortest run back from her side, and the box tile nearest
            -- her (where the run out starts)
            local back, near, near_d = 999, nil, nil
            for _, r in ipairs(geo.ring) do back = math.min(back, QD.raid._vzp1_steps(geo.G, to_box, r.x, r.z)) end
            for _, t in ipairs(tiles) do
                local d = QD.raid._vzp1_steps(geo.G, geo.to_ring, t.x, t.z)
                if near_d == nil or d < near_d then near, near_d = t, d end
            end
            c = { pillar = p, tiles = tiles, to_box = to_box, back = back, near = near, near_steps = near_d }
        end
        S.covers[p.key] = c
    end
    return c
end

function QD.raid._vzp1_cover(S, v, pillars)
    for _, p in ipairs(pillars) do
        if p.safe then
            local c = QD.raid._vzp1_cover_for(S, v, pillars, p)
            if c then return c end
        end
    end
    return nil
end

function QD.raid._vzp1_in_cover(cover, x, z)
    for _, t in ipairs(cover.tiles) do
        if t.x == x and t.z == z then return true end
    end
    return false
end

-- The box tile my run reaches first: walk down the steps-to-the-box from my
-- own tile until it reads 0 (the tile a shortest run ends on).
function QD.raid._vzp1_box_nearest(S, v, cover)
    local G, to_box = S.geo.G, cover.to_box
    local x, z = v.me.x, v.me.z
    local d = QD.raid._vzp1_steps(G, to_box, x, z)
    local guard = 0
    while d > 0 and d < 999 and guard < 64 do
        guard = guard + 1
        local bx, bz, bd = x, z, d
        for dx = -1, 1 do
            for dz = -1, 1 do
                local nd = QD.raid._vzp1_steps(G, to_box, x + dx, z + dz)
                if nd < bd then bx, bz, bd = x + dx, z + dz, nd end
            end
        end
        if bd >= d then break end
        x, z, d = bx, bz, bd
    end
    if d == 0 then return { x = x, z = z } end
    return cover.near
end

-- Is there time, from where I stand, to reach her, swing once and be back in
-- the box before the next shot reads?  Steps, not tiles: the run bends round
-- the pillar (s8 t160: a five-tile run took seven steps).
function QD.raid._vzp1_window(S, v, cover)
    if S.shot_by == nil or cover == nil or v.boss == nil then return false end
    local out = QD.raid._vzp1_steps(S.geo.G, S.geo.to_ring, v.me.x, v.me.z)
    local back = cover.back
    local ok = v.tick + QD.raid._vzp1_ticks(out) + 1 + QD.raid._vzp1_ticks(back, QD.VZP1.ROUTE_SLACK) <= S.shot_by
    if not ok and S.state == "HIDE" and v.tick % 7 == 0 then
        QD.raid._vzp1_trace(S, v, "no window: out " .. out .. " steps, back " .. back .. ", shot by t" .. S.shot_by)
    end
    return ok
end

-- Steps from a ground item's tile, kept per tile (the Dawnbringer sits still).
function QD.raid._vzp1_item_steps(S, x, z, mx, mz)
    local key = x .. "," .. z
    local d = S.item_paths[key]
    if d == nil then
        d = QD.raid._vzp1_bfs(S.geo.G, { { x = x, z = z } })
        S.item_paths[key] = d
    end
    return QD.raid._vzp1_steps(S.geo.G, d, mx, mz)
end

-- Where to stand for the collapse (BREAK): the nearest tile clear of every
-- pillar's fall.  But a shot she wound up before the shield broke still
-- fires (C: her queue4 drains while she is still verzik_phase1, three ticks
-- after the wind-up; s12: wind-up t187, death t188, bolt t190, collapse
-- t191), so while one is pending the tile must also be in the box: the far
-- corner of a west pillar's box is 4 from its centre and from the centre of
-- the pillar below it, which is both.
function QD.raid._vzp1_escape(S, v, pillars, cover)
    local G = S.geo.G
    -- Only a shot whose wind-up I SAW is still coming: once the shield is
    -- down she winds up no more (sd t211: the next shot was only predicted,
    -- and holding the box for it kept a raider inside a pillar's fall).
    local seen = S.windups[#S.windups]
    local pending = S.shot_by ~= nil and v.tick <= S.shot_by and cover ~= nil
        and seen ~= nil and S.shot_by == seen + S.SEEN_TO_DEADLINE
    -- every tile near me clear of every fall, and steps to the nearest one;
    -- the fall comes two ticks on, so only four steps matter
    local safe = {}
    for dx = -6, 6 do
        for dz = -6, 6 do
            local x, z = v.me.x + dx, v.me.z + dz
            if not QD.raid._vzp1_blocked(v, pillars, x, z) and QD.raid._vzp1_collapse_safe(pillars, x, z) then
                safe[#safe + 1] = { x = x, z = z }
            end
        end
    end
    local to_safe = QD.raid._vzp1_bfs(G, safe, 6)
    local from_me = QD.raid._vzp1_bfs(G, { v.me }, 8)
    -- The fall reads where we stand one tick after her death animation is
    -- seen, the same offset a shot has (s14: death seen t188, collapse read
    -- the step of t189), so there are two steps of movement, and while a shot
    -- is pending the first must end in the box.  So: in the box by the shot,
    -- on a tile one run from clear ground; then clear ground.
    local candidates = pending and cover.tiles or safe
    local best, best_cost = nil, nil
    for _, t in ipairs(candidates) do
        local steps = QD.raid._vzp1_steps(G, from_me, t.x, t.z)
        local cost = steps + 10 * QD.raid._vzp1_steps(G, to_safe, t.x, t.z)
        if pending and v.tick + QD.raid._vzp1_ticks(steps) - 1 > S.shot_by then cost = cost + 100000 end
        if best_cost == nil or cost < best_cost then best, best_cost = t, cost end
    end
    return best
end

-- -------------------------------------------------------------------- CLOCK
--
-- `S.shot_by` is the last tick on which my step still counts for the next
-- shot: a walk clicked on tick k with a run of n ticks has me there after the
-- step of tick k + n - 1, and that must be no later than S.shot_by.  Seen
-- wind-up on tick k (the first tick her row shows it): the shot reads the end
-- of the tick before it, which is S.SEEN_TO_DEADLINE after k (calibrated
-- against the tick log: K in the record).  Before her first wind-up is seen,
-- the content's first-attack delay from the fight's start, taken early.

function QD.raid._vzp1_clock(S, v, pillars)
    local b = v.boss
    if b and b.seq_id == S.ids.windup and b.seq_tick ~= S.last_windup_seq_tick then
        S.last_windup_seq_tick = b.seq_tick
        S.shot_by = v.tick + S.SEEN_TO_DEADLINE
        S.windups[#S.windups + 1] = v.tick
        S.synced = true
    elseif S.shot_by ~= nil and v.tick > S.shot_by and not S.synced then
        -- Before her first wind-up is seen the clock is a guess from the
        -- fight's start, and a guess never lets anyone out: the shot it named
        -- may be late (sa, queued inputs: guessed t57, her first wind-up t62,
        -- two raiders out striking took the bolt).  Hold the deadline at now
        -- -- in cover, no window -- until a wind-up is seen.
        S.shot_by = v.tick
    elseif S.shot_by ~= nil and v.tick > S.shot_by then
        -- the shot has read us: my tile now is the tile it read (the step of
        -- tick shot_by is the last one shown).  The pillar I was behind took
        -- the hit; with none, I was in the open.
        local behind = nil
        for _, p in ipairs(pillars) do
            if QD.raid._vzp1_behind(p, v.me.x, v.me.z) then behind = p.key end
        end
        S.shots_passed[#S.shots_passed + 1] = "t" .. S.shot_by .. (behind and (" behind " .. behind) or (" OPEN at " .. v.me.x .. "," .. v.me.z))
        S.shot_by = S.shot_by + QD.VZP1.ATTACK_TICKS
    end
end

-- ------------------------------------------------------------------ DECIDE

function QD.raid._vzp1_trace(S, v, text)
    if #S.trace < QD.VZP1.TRACE_CAP then S.trace[#S.trace + 1] = "t" .. v.tick .. " " .. text end
end

function QD.raid._vzp1_go(S, v, state, why)
    if S.state ~= state then
        QD.raid._vzp1_trace(S, v, S.state .. ">" .. state .. (why and (" " .. why) or ""))
        S.state = state
        S.state_tick = v.tick
        S.pressed = false
    end
end

-- Must the run to cover start this tick?  Waiting one more tick would land me
-- after the deadline.
function QD.raid._vzp1_must_return(S, v, cover)
    if S.shot_by == nil or cover == nil then return false end
    if QD.raid._vzp1_in_cover(cover, v.me.x, v.me.z) then return false end
    local n = QD.raid._vzp1_ticks(QD.raid._vzp1_steps(S.geo.G, cover.to_box, v.me.x, v.me.z), QD.VZP1.ROUTE_SLACK)
    -- Not yet at her side, this tick's attack press steps me a run farther
    -- from the box before the next look (s11 t117: a raider 2 steps out
    -- re-wielded and pressed, ran 2 toward her, and was 4 steps out at the
    -- deadline).  At her side the swing holds me still.
    local beside = false
    for _, r in ipairs(S.ring or {}) do
        if r.x == v.me.x and r.z == v.me.z then beside = true end
    end
    if not beside then n = n + 1 end
    return (v.tick + 1) + n - 1 > S.shot_by
end

-- Whose turn the Dawnbringer is: the first holder is seat 1 (it carries it in),
-- and each time it appears on the floor the next seat in orb order takes it
-- (W:885 "drop the Dawnbringer for the next player (in orb order)").
function QD.raid._vzp1_dawn_turn(S)
    return (S.dawn_drops % S.size) + 1
end

-- My turn with the Dawnbringer, it lies in view, and the walk onto it still
-- leaves time to be back in the box before the next shot reads?  (A walk
-- inside the box is always in time.)  The clock check STRIKE makes every
-- tick still governs the walk, so a fetch can never strand me.
function QD.raid._vzp1_fetch(S, v, cover, holding, can_spec)
    if holding or not can_spec or #v.ground_dawn == 0 or QD.raid._vzp1_dawn_turn(S) ~= S.role then
        return false
    end
    if cover == nil or S.shot_by == nil then return false end
    for _, o in ipairs(v.ground_dawn) do
        if QD.raid._vzp1_in_cover(cover, o.x, o.z) then return true end
        local out = QD.raid._vzp1_ticks(QD.raid._vzp1_item_steps(S, o.x, o.z, v.me.x, v.me.z))
        local back = QD.raid._vzp1_ticks(QD.raid._vzp1_steps(S.geo.G, cover.to_box, o.x, o.z), QD.VZP1.ROUTE_SLACK)
        if v.tick + out + back <= S.shot_by then return true end
    end
    return false
end

function QD.raid._vzp1_decide(S, v, pillars, cover)
    local I = {}
    local holding = v.dawn_pack > 0 or v.dawn_worn > 0
    local can_spec = v.energy >= QD.VZP1.SPEC_COST

    -- BREAK: her shield is down (C: ~tob_verzik_leave_throne plays
    -- verzik_phase1_death; her row becomes the transition form three ticks on)
    if S.state ~= "BREAK" and S.state ~= "DONE"
        and ((v.boss and v.boss.seq_id == S.ids.death) or (v.boss == nil and v.after ~= nil)) then
        QD.raid._vzp1_go(S, v, "BREAK", "her shield broke")
    end
    if S.state == "BREAK" then
        -- done once the falls have landed: they hit two ticks after they are
        -- read (C: queue*(combat_damage_player, 2)), and the pillars turn to
        -- rubble the tick they are read
        if #pillars == 0 and v.tick >= (S.fallen_at or v.tick) + 3 then
            QD.raid._vzp1_go(S, v, "DONE", "every pillar is down")
            return I
        end
        if #pillars == 0 then
            S.fallen_at = S.fallen_at or v.tick
            return I
        end
        local t = QD.raid._vzp1_escape(S, v, pillars, cover)
        if t and (t.x ~= v.me.x or t.z ~= v.me.z) then I.walk = { x = t.x, z = t.z } end
        return I
    end
    if S.state == "DONE" then return I end

    -- the window: is it time to be in cover?
    if S.state == "OPEN" or S.state == "STRIKE" then
        if cover and QD.raid._vzp1_must_return(S, v, cover) then
            QD.raid._vzp1_go(S, v, "RETURN", "shot read after t" .. tostring(S.shot_by))
        end
    end
    if S.state == "RETURN" or S.state == "HIDE" then
        if cover == nil then
            -- no pillar can take the next shot (every one has had three):
            -- never expected in P1, the shield goes first; logged
            QD.raid._vzp1_trace(S, v, "no safe pillar")
        elseif QD.raid._vzp1_in_cover(cover, v.me.x, v.me.z) then
            if S.state ~= "HIDE" then QD.raid._vzp1_go(S, v, "HIDE", "in the box at " .. v.me.x .. "," .. v.me.z) end
        elseif QD.raid._vzp1_window(S, v, cover) then
            -- the team's pillar moved on, or the shot I ran back for has
            -- read us: go by her, and the run back ends in the (new) box
            QD.raid._vzp1_go(S, v, "STRIKE", "window to t" .. tostring(S.shot_by))
        else
            if S.state == "HIDE" then QD.raid._vzp1_go(S, v, "RETURN", "the team's pillar moved") end
            local t = QD.raid._vzp1_box_nearest(S, v, cover)
            I.walk = { x = t.x, z = t.z }
        end
    end
    -- in cover: the Dawnbringer's hand-over and wield, and food
    if S.state == "HIDE" then
        if holding and not can_spec then
            if v.dawn_worn > 0 then
                I.gear = S.ids.scythe
            else
                I.drop = S.ids.dawn
            end
        elseif QD.raid._vzp1_fetch(S, v, cover, holding, can_spec) then
            I.take = true
        elseif v.dawn_pack > 0 and can_spec then
            I.gear = S.ids.dawn
        end
        if v.hp < QD.VZP1.HP_EAT then I.eat = QD.raid._vzp1_food(S) end
        -- out into the window whenever there is time to get to her, swing
        -- once and run back before the next shot reads -- unless the
        -- Dawnbringer has a chore here first: a special (75-150) is worth
        -- several capped swings (W:883 "capping damage ... with melee at 10")
        if not (I.drop or I.take or I.gear) and QD.raid._vzp1_window(S, v, cover) then
            QD.raid._vzp1_go(S, v, "STRIKE", "window to t" .. tostring(S.shot_by))
        end
    end

    -- the window, and the opening: attack her
    if (S.state == "STRIKE" or S.state == "OPEN") and v.boss then
        if QD.raid._vzp1_fetch(S, v, cover, holding, can_spec) then
            I.take = true
        elseif v.dawn_worn > 0 and can_spec then
            if v.tick - (S.spec_armed or -100) >= 4 then I.spec = true end
            I.attack = not S.pressed or I.spec
        elseif v.dawn_worn > 0 then
            -- the orb is spent: the scythe back, and swing it (W:885)
            I.gear = S.ids.scythe
            I.attack = true
        elseif v.dawn_pack > 0 and can_spec then
            I.gear = S.ids.dawn
            I.attack = true
        else
            I.attack = not S.pressed
        end
    end
    return I
end

function QD.raid._vzp1_food(S)
    local _, n = api_drive.inv_count(S.ids.inv, S.ids.food)
    if (n or 0) > 0 then return S.ids.food end
    return nil
end

-- --------------------------------------------------------------------- ACT
--
-- Raw drive primitives only, each one packet: move_to (a walk click),
-- world_op (a menu row on an npc or a ground item), inv_op (a held item's
-- row), if_click (the special orb).  Sent in this order inside the tick, so a
-- held op (which ends an interaction) goes before the attack that follows it.

-- Item ops on a held item (all.obj): Dawnbringer ifop2=Wield, scythe
-- ifop2=Wield, anglerfish op1 Eat, and the default ifop5 Drop.
QD.VZP1.OP_EAT, QD.VZP1.OP_WIELD, QD.VZP1.OP_DROP = 1, 2, 5

function QD.raid._vzp1_held(S, obj, op)
    local cap = 28
    for slot = 0, cap - 1 do
        local r, cell = api_drive.inv_slot(S.ids.inv, slot)
        if r == "ok" and cell.obj_id == obj then
            return api_drive.inv_op(S.ids.backpack, slot, obj, cell.count, op)
        end
    end
    return "not_found", "not in the backpack"
end

function QD.raid._vzp1_act(S, v, I)
    local n = 0
    local walk = I.walk
    if walk and S.walk_target and S.walk_target.x == walk.x and S.walk_target.z == walk.z
        and v.tick - S.walk_sent < 3 then
        walk = nil -- already on its way
    end
    if I.eat then
        QD.raid._vzp1_held(S, I.eat, QD.VZP1.OP_EAT)
        S.eats = S.eats + 1
        n = n + 1
    end
    if I.gear then
        local r = QD.raid._vzp1_held(S, I.gear, QD.VZP1.OP_WIELD)
        S.gear[#S.gear + 1] = "t" .. v.tick .. " " .. I.gear .. " " .. tostring(r)
        S.pressed = false -- a held op ends the interaction
        n = n + 1
    end
    if I.drop then
        local r = QD.raid._vzp1_held(S, I.drop, QD.VZP1.OP_DROP)
        S.drops_made[#S.drops_made + 1] = "t" .. v.tick .. " " .. tostring(r)
        n = n + 1
    end
    if walk then
        api_drive.move_to(walk.x, walk.z)
        S.walk_target, S.walk_sent = walk, v.tick
        S.pressed = false
        n = n + 1
    end
    if I.take then
        local r = api_drive.world_op("obj", S.ids.dawn, 3)
        S.takes[#S.takes + 1] = "t" .. v.tick .. " " .. tostring(r)
        n = n + 1
    end
    if I.spec then
        api_drive.if_click(S.ids.spec_orb, 1)
        S.spec_armed = v.tick
        n = n + 1
    end
    if I.attack and v.boss and not walk then
        local r = api_drive.world_op("npc", v.boss.npc_id, 2, v.boss.element_id)
        if r == "ok" then
            S.pressed = true
            S.walk_target = nil
        end
        S.attacks = S.attacks + 1
        n = n + 1
    end
    if n > 0 then S.input_ticks = S.input_ticks + 1 end
end

-- One server tick, ended where the CLIENT ends it: the `server_tick` event,
-- raised once the tick's last packet (SERVER_TICK_END) has been applied.
-- Not the server's counter: it moves when the tick starts, before this
-- client has the tick's packets, and a plan woken by it reads the world one
-- tick stale (live run 2026-10-08: her wind-up of tick 73 seen at 74, every
-- run back a tile short of the box).
function QD.raid._vzp1_wait(S, from)
    return await({ event = "server_tick", match = function() return true end,
        note = "verzik_p1_solve: the tick's packets applied" }, 3)
end

-- -------------------------------------------------------------------- LOOP

-- t.raid.verzik_p1_prepare() -> prep: search the arena BEFORE the fight, one
-- search a tick, so the fight's ticks only look the answers up (the client's
-- 400,000-instruction budget per wait: the floor, the steps to her and the
-- steps to one pillar's box are each a whole-floor search).  The room is
-- laid out from the moment the trio is in it: her throne form and the six
-- pillars stand still until her shield breaks.  Pass the result to
-- verzik_p1_solve as opts.prep.
function QD.raid.verzik_p1_prepare(opts)
    opts = opts or {}
    local S = { ids = QD.raid._vzp1_ids(), hits = {}, hit_cycle_seen = {}, covers = {}, item_paths = {} }
    -- The backpack panel open: the client refuses a held item's op while its
    -- panel is not the one showing (live run: every Dawnbringer wield
    -- refused after the prayer tab was opened), and the fight never turns
    -- to another panel
    local tr, tab = api_drive.tab_by_name("inventory")
    assert(tr == "ok", "verzik_p1_prepare: no inventory tab")
    api_drive.tab(tab)
    local deadline = QD.raid._vzp1_now() + (opts.max_ticks or 30)
    while QD.raid._vzp1_now() <= deadline do
        local v = QD.raid._vzp1_measure(S)
        if (v.boss or v.initial) and #v.pillars > 0 then
            local pillars = QD.raid._vzp1_pillars(S, v)
            if S.geo == nil then
                QD.raid._vzp1_geo(S, v, pillars)
            else
                local todo = nil
                for _, p in ipairs(pillars) do
                    if S.covers[p.key] == nil then todo = p break end
                end
                if todo == nil then
                    return { geo = S.geo, covers = S.covers, item_paths = S.item_paths, pillars = #pillars }
                end
                QD.raid._vzp1_cover_for(S, v, pillars, todo)
            end
        end
        QD.raid._vzp1_wait(S, v.tick)
    end
    return nil
end

function QD.raid.verzik_p1_solve(opts)
    opts = opts or {}
    local S = {
        ids = QD.raid._vzp1_ids(),
        role = (QD_PARTY and QD_PARTY.role) or 1, size = (QD_PARTY and QD_PARTY.size) or 1,
        state = "OPEN", state_tick = 0, pressed = false,
        hits = {}, hit_cycle_seen = {}, windups = {}, shots_passed = {},
        trace = {}, gear = {}, drops_made = {}, takes = {},
        dawn_drops = 0, dawn_seen = false, attacks = 0, eats = 0, input_ticks = 0,
        walk_target = nil, walk_sent = -100,
        geo = opts.prep and opts.prep.geo or nil,
        covers = opts.prep and opts.prep.covers or {},
        item_paths = opts.prep and opts.prep.item_paths or {},
        SEEN_TO_DEADLINE = opts.seen_to_deadline or 1,
        hp_lost = 0, hits_taken = {}, specs = {},
    }
    local max_ticks = opts.max_ticks or 400
    local start = QD.raid._vzp1_now()
    S.start = start
    -- before her first wind-up: the content's delay from the fight's start,
    -- assumed to have begun `opts.started_ago` ticks before this call (the
    -- dialogue's choice is the start, and it is behind us: the leader's
    -- dialogue closes about 6 ticks after it; 8 errs early, which costs a
    -- swing and never a hit)
    S.shot_by = start - (opts.started_ago or 8) + QD.VZP1.FIRST_WINDUP + S.SEEN_TO_DEADLINE
    local last_hp, last_energy = nil, nil
    while true do
        local v = QD.raid._vzp1_measure(S)
        if v.tick - start >= max_ticks then
            return "timeout", QD.raid._vzp1_summary(S), S
        end
        if v.hp <= 0 then
            return "died", QD.raid._vzp1_summary(S), S
        end
        -- what happened since last tick: damage, and specials as the energy they spend
        if last_hp and v.hp < last_hp then
            S.hp_lost = S.hp_lost + (last_hp - v.hp)
            S.hits_taken[#S.hits_taken + 1] = "t" .. v.tick .. " -" .. (last_hp - v.hp) .. " at " .. v.me.x .. "," .. v.me.z .. " " .. S.state
        end
        if last_energy and v.energy <= last_energy - QD.VZP1.SPEC_COST + 10 then S.specs[#S.specs + 1] = v.tick end
        last_hp, last_energy = v.hp, v.energy
        -- the Dawnbringer appears on the floor: the next seat's turn
        local seen = #v.ground_dawn > 0
        if seen and not S.dawn_seen then S.dawn_drops = S.dawn_drops + 1 end
        S.dawn_seen = seen

        QD.raid._vzp1_clock(S, v, QD.raid._vzp1_pillars(S, v))
        local pillars = QD.raid._vzp1_pillars(S, v)
        local geo = QD.raid._vzp1_geo(S, v, pillars)
        S.ring = geo.ring
        local cover = QD.raid._vzp1_cover(S, v, pillars)
        if not S.geometry and v.boss and cover then
            S.geometry = true
            QD.raid._vzp1_trace(S, v, string.format("her %d,%d size %d; box of %s nearest her %d,%d (%d tiles to reach)",
                v.boss.x, v.boss.z, v.boss.size or 1, cover.pillar.key, cover.near.x, cover.near.z,
                QD.raid._vzp1_tiles_to_her(v, cover.near.x, cover.near.z)))
        end
        local I = QD.raid._vzp1_decide(S, v, pillars, cover)
        if S.state == "DONE" then
            return "ok", QD.raid._vzp1_summary(S), S
        end
        QD.raid._vzp1_act(S, v, I)
        QD.raid._vzp1_wait(S, v.tick)
    end
end

function QD.raid._vzp1_summary(S)
    return string.format(
        "p%d %s: hp lost %d (%s); wind-ups seen %s; shots %s; specials %s; dawn drops seen %d, drops %s, takes %s; attacks pressed %d; gear %s; trace %s",
        S.role, S.state, S.hp_lost, table.concat(S.hits_taken, " | "), table.concat(S.windups, ","),
        table.concat(S.shots_passed, "; "), table.concat(S.specs, ","), S.dawn_drops, table.concat(S.drops_made, " "),
        table.concat(S.takes, " "), S.attacks, table.concat(S.gear, " "), table.concat(S.trace, " | "))
end
