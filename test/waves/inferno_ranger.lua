-- inferno_ranger: Jal-Xil (spec docs/minigames/inferno/encounters/ranger.tsv, sidecar ranger.scope.tsv).
-- ONE practice entry of wave 18 (the first wave with a ranger: one ranger and three nibblers), fought for real with a bow
-- and the prayer book, the ranger kept alive until every phase is done, then killed:
--   P0 Protect from Missiles held from the entry; P1 the nibblers shot first (the pillars stay up for P5);
--   P2 a distance sweep (stood 2..10 tiles off its footprint, two swings at each) under Protect from Missiles;
--   P3 stood on the footprint's corner: Missiles held (its melee lands), then Protect from Melee (the melee blocked);
--   P4 one-tick flicks of Missiles on the swing tick, then the same press one tick late (the hit lands);
--   P5 a ring tile behind a pillar it cannot see; P6 the kill with the bow under Protect from Missiles.
-- Every shot is taken inside the fight on the tick of its event (the swing, the press, the click, the death) with the camera
-- turned on the ranger; the ledger rows after the fight are t.expect rows computed from the tick log that name those shots.
-- Setup = bring-alongs only (levels, a bow, brews, restores, food, auto-retaliate off).
local RANGER, RANGER_SYMBOL = 7698, "inferno_creature_ranger"
local RANGED_SEQ, MELEE_SEQ, DEFEND_SEQ, DEATH_SEQ, PROJ = 7605, 7604, 7607, 7606, 1377
local WAVE = 18
local BASE_X, BASE_Z = 6400, 64
-- the pillars' south-west tiles, region-local (inferno.constant ^inferno_pillar_*), 3x3 each
local PILLARS = { { name = "west", x = 17, z = 37 }, { name = "south", x = 27, z = 23 }, { name = "east", x = 34, z = 39 } }
local D = { since = nil, shots = {}, levels = {}, s = {}, ph = {}, flicks = {}, pray_log = {}, sweep = {}, notes = {} }

local function now(t) local _, k = t.tick() return k or -1 end
local function alive(t) return t.player.alive() == "ok" end
local function hp(t) local _, a = t.skill.read("hitpoints") if type(a) == "table" then return a.level or -1 end return -1 end
local function note(t, label, text) t.expect(label, "ok", text) end
-- in-fight read of the tick log (no tick spent); since = a serial
local function rows_fast(t, kind, opts)
    opts = opts or {}
    opts.kind = kind
    if opts.since == nil then opts.since = D.since end
    local r, list = t.ticklog.rows(opts)
    if r ~= "ok" then return {} end
    return list
end
-- post-fight read: one tick between groups keeps the per-resume instruction budget
local function rows(t, kind, opts)
    local list = rows_fast(t, kind, opts)
    t.ticks(1)
    return list
end
local function mark(t, label)
    local _, txt = t.ticklog.mark(label)
    return tonumber(string.match(tostring(txt), "serial (%d+)"))
end
local function join(list, f, cap)
    local out = {}
    for i = 1, math.min(#list, cap or 80) do out[#out + 1] = f and f(list[i]) or tostring(list[i]) end
    if #list > (cap or 80) then out[#out + 1] = "..." end
    return table.concat(out, ",")
end
local function maxof(list) local m = nil for _, v in ipairs(list) do if m == nil or v > m then m = v end end return m end
local function minof(list) local m = nil for _, v in ipairs(list) do if m == nil or v < m then m = v end end return m end
local function uniq(list)
    local seen, order = {}, {}
    for _, v in ipairs(list) do
        if seen[v] == nil then seen[v] = 0 order[#order + 1] = v end
        seen[v] = seen[v] + 1
    end
    table.sort(order, function(a, b) return tostring(a) < tostring(b) end)
    local parts = {}
    for _, v in ipairs(order) do parts[#parts + 1] = tostring(v) end
    return table.concat(parts, ","), seen
end
local function hist(list)
    local _, c = uniq(list)
    local ks = {}
    for k in pairs(c) do ks[#ks + 1] = k end
    table.sort(ks, function(a, b) return tostring(a) < tostring(b) end)
    local parts = {}
    for _, k in ipairs(ks) do parts[#parts + 1] = tostring(k) .. "x" .. c[k] end
    return "n=" .. #list .. " " .. table.concat(parts, " ")
end
-- Chebyshev distance from a tile to a square footprint whose south-west tile is (px, pz)
local function foot_dist(x, z, px, pz, size)
    size = size or 3
    local dx = math.max(px - x, 0, x - (px + size - 1))
    local dz = math.max(pz - z, 0, z - (pz + size - 1))
    return math.max(dx, dz), dx, dz
end
local function spec_row(t, id, ok, measured, extra, specv, grade, tol)
    local detail = "measured " .. measured .. ((extra and extra ~= "") and (", " .. extra) or "") .. " (spec " .. specv .. ", grade " .. grade .. ", tol " .. tol .. ")"
    t.expect("spec.ranger." .. id, ok and "ok" or "fail", detail)
end
local function set_pray(t, name)
    local t0 = now(t)
    local r = t.prayer.set(name, true)
    D.pray_log[#D.pray_log + 1] = { t0 = t0, t1 = now(t), p = name }
    return r
end
local function find_ranger(t)
    local _, _, pack, raw = t.npc.pack(60)
    for _, p in ipairs(pack or {}) do
        if p.type == RANGER and not p.dying then
            if p.client_slot and p.client_slot >= 0 then D.cslot = p.client_slot end
            return p, raw, pack
        end
    end
    return nil, raw, pack
end
-- turn the camera to the ranger: yaw runs counter-clockwise from north (512 faces west), so the bearing is atan(-dx, dz)
local function aim(t, zoom, pitch, side)
    local m, raw = find_ranger(t)
    if not m or not raw or not raw.player_x then return false end
    local px, pz = raw.player_x, raw.player_z
    local dx, dz = (m.x + 1) - px, (m.z + 1) - pz
    local base = 0
    if math.abs(dx) + math.abs(dz) > 0.6 then base = math.atan(-dx, dz) end
    -- the camera sits behind the player: of the turns take the one whose camera side has no pillar near the player;
    -- 'side' looks across the line from the player to the ranger, so a pillar between them hides neither
    local best, bs = 0, -1
    for _, turn in ipairs(side and { 512, -512, 384, -384 } or { 0, 256, -256, 512, -512 }) do
        local th = base + turn * 2 * math.pi / 2048
        local cx, cz = math.sin(th), -math.cos(th)
        local score = math.pi
        if side and (dx * math.cos(th) + dz * math.sin(th)) > 0 then score = -1 end
        for _, p in ipairs(PILLARS) do
            local wx, wz = BASE_X + p.x + 1 - px, BASE_Z + p.z + 1 - pz
            local wl = math.sqrt(wx * wx + wz * wz)
            if wl > 0 and wl < 9 then
                local ang = math.acos(math.max(-1, math.min(1, (wx * cx + wz * cz) / wl)))
                if ang < score then score = ang end
            end
        end
        if score >= 0 and score > bs + 0.35 then best, bs = turn, score end
    end
    local yaw = (math.floor(base / (2 * math.pi) * 2048) + best) % 2048
    t.drive.camera(yaw, pitch or 383, zoom or 1200)
    return true
end
-- a shot taken at its event; the later ledger row names it
local function shoot(t, key, name, zoom, pitch, side)
    aim(t, zoom, pitch, side)
    local r = t.shot(name)
    D.shots[key] = name .. " (server tick " .. now(t) .. ")"
    return r
end
local function shotname(key) return D.shots[key] or ("no shot " .. key) end
-- one reading per tick: the ranger's tile, its drawn animation, whether it sees the player, and the player's tile
local function sample(t, tag)
    local m, raw = find_ranger(t)
    local s = { tag = tag, tick = now(t) }
    if m and raw then
        s.rx, s.rz, s.px, s.pz, s.sees, s.size, s.type, s.hp = m.x, m.z, raw.player_x, raw.player_z, m.sees_player, m.size, m.type, m.hitpoints
        s.range = m.attackrange
        if raw.player_x then s.dist, s.dx, s.dz = foot_dist(raw.player_x, raw.player_z, m.x, m.z, m.size) end
    end
    local sr, st = t.npc.state(RANGER_SYMBOL)
    if sr == "ok" and type(st) == "table" then s.anim = st.anim_id s.state_size = st.size end
    D.s[#D.s + 1] = s
    D.by_tick = D.by_tick or {}
    D.by_tick[s.tick] = s
    return s, m
end
local function count(t, name) local _, n = t.inv.count(name) return type(n) == "number" and n or 0 end
local function supplies(t)
    local _, _, b = t.inv.doses("saradomin_brew")
    local _, _, r = t.inv.doses("super_restore")
    return (type(b) == "table" and b.doses or 0), (type(r) == "table" and r.doses or 0), count(t, "shark")
end
local function restore(t)
    local r = t.player.drink("super_restore")
    D.brews_since = 0
    return r
end
-- brews first (a dose heals 16 and drains the combat stats), a super restore after every second brew, sharks last
local function heal_to(t, floor)
    local n = 0
    while alive(t) and hp(t) < floor and n < 10 do
        n = n + 1
        local brews = supplies(t)
        if brews > 0 then
            t.player.drink("saradomin_brew")
            D.brews_since = (D.brews_since or 0) + 1
            if D.brews_since >= 2 then restore(t) end
        elseif count(t, "shark") > 0 then
            t.player.inv_op("shark", 1)
            t.ticks(3)
        else
            break
        end
    end
end
local function pray_if_low(t)
    local _, _, pts = t.prayer.points()
    if pts and pts.level and pts.level < 30 then restore(t) end
end
local function upkeep(t, floor)
    local healed = false
    if hp(t) < (floor or 60) then heal_to(t, 90) healed = true end
    pray_if_low(t)
    return healed
end
-- ---------------------------------------------------------------- the fight
-- P0: Protect from Missiles up before the entry, held; the first ranged swing photographed as it starts
local function p0_entry(t)
    set_pray(t, "protectfrommissiles")
    local r, d = t.wave.enter("inferno", WAVE)
    D.begin = tonumber(string.match(tostring(d), "wave begun by server tick (%d+)"))
    local m = find_ranger(t)
    if m then D.entry = { hp = m.hitpoints, size = m.size, range = m.attackrange, x = m.x, z = m.z, type = m.type } end
    local e = D.entry or {}
    aim(t, 1500, 300)
    t.check("enter.wave18", r == "ok" and m ~= nil, string.format("%s; ranger type %s at %s,%s (local %s,%s) hp %s size %s range %s", tostring(d), tostring(e.type), tostring(e.x), tostring(e.z), tostring((e.x or 0) - BASE_X), tostring((e.z or 0) - BASE_Z), tostring(e.hp), tostring(e.size), tostring(e.range)))
    D.shots.entry = "enter.wave18 (server tick " .. now(t) .. ")"
    if r ~= "ok" then return false end
    local _, _, rec = t.npc.record(RANGER_SYMBOL, { need = "server" })
    D.rec = type(rec) == "table" and rec or {}
    D.ph.watch = { from = now(t) }
    local prev, shot = nil, false
    while alive(t) and now(t) - D.ph.watch.from < 16 do
        local s = sample(t, "watch")
        if s.anim == RANGED_SEQ and prev ~= RANGED_SEQ and not shot then
            shoot(t, "held", "p0.missiles_up_ranger_swings", 1200, 300)
            shot = true
        end
        prev = s.anim
        upkeep(t, 60)
        t.ticks(1)
    end
    D.ph.watch.to = now(t)
    return true
end
-- P1: the three nibblers die first (technique 1) so the pillars stand for the safespot
local function p1_nibblers(t)
    D.ph.nib = { from = now(t) }
    local kills = 0
    for _ = 1, 8 do
        if not alive(t) or kills >= 3 then break end
        upkeep(t, 60)
        local _, _, pack = t.npc.pack(40)
        local pick = nil
        for _, p in ipairs(pack or {}) do
            if p.symbol == "inferno_nibbler" and p.client_slot and p.client_slot >= 0 and not p.dying then pick = p break end
        end
        if pick == nil then break end
        local ar = t.player.attack(pick.symbol, 2, 10, { slot = pick.client_slot })
        if ar == "ok" then
            local kr = t.npc.await_dead_engaged(4, 1, { eat = { item = "shark", below = 50 } })
            if kr == "ok" then kills = kills + 1 end
        else
            t.ticks(1)
        end
    end
    D.ph.nib.to = now(t)
    D.nibs = kills
    aim(t, 1500, 300)
    t.check("p1.nibblers_dead", kills >= 3, string.format("%d nibblers killed by bow between server ticks %d and %d under Protect from Missiles; the ranger kept shooting", kills, D.ph.nib.from, D.ph.nib.to))
    D.shots.nib = "p1.nibblers_dead (server tick " .. now(t) .. ")"
end
-- local tile in the open arena and off every pillar (one tile of margin)
local function open_tile(lx, lz)
    if lx < 12 or lx > 48 or lz < 16 or lz > 50 then return false end
    for _, p in ipairs(PILLARS) do
        if lx >= p.x - 1 and lx <= p.x + 3 and lz >= p.z - 1 and lz <= p.z + 3 then return false end
    end
    return true
end
-- the tile d tiles off the footprint's edge in direction dir, on the footprint's middle row or column
local function off_tile(m, dir, d)
    local lx, lz = m.x - BASE_X, m.z - BASE_Z
    if dir[1] == 1 then return lx + 2 + d, lz + 1 end
    if dir[1] == -1 then return lx - d, lz + 1 end
    if dir[2] == 1 then return lx + 1, lz + 2 + d end
    return lx + 1, lz - d
end
-- P2: stood at a series of distances off the footprint under Protect from Missiles, two ranged swings at each
local SWEEP = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 15, 16 }
local function p2_sweep(t)
    local m = find_ranger(t)
    if m == nil then return end
    local best, room = nil, -1
    for _, dir in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        local n = 0
        for d = 1, 16 do
            local lx, lz = off_tile(m, dir, d)
            if not open_tile(lx, lz) then break end
            n = d
        end
        if n > room then best, room = dir, n end
    end
    D.sweep_dir, D.sweep_room = best, room
    D.ph.sweep = { from = now(t) }
    for _, d in ipairs(SWEEP) do
        if not alive(t) or d > room then break end
        local m2 = find_ranger(t) or m
        local lx, lz = off_tile(m2, best, d)
        t.player.walk_to(BASE_X + lx, BASE_Z + lz, 20)
        local from = now(t)
        local n, prev = 0, nil
        local rec = { want = d, from = from, dists = {} }
        while alive(t) and now(t) - from < 14 do
            local s = sample(t, "sweep")
            if s.anim == RANGED_SEQ and prev ~= RANGED_SEQ then
                n = n + 1
                rec.dists[#rec.dists + 1] = s.dist
                if (d == 4 or d == 10) and n == 1 then shoot(t, "sweep" .. d, "p2.swing_from_" .. d .. "_tiles", 1600, 300) end
            end
            prev = s.anim
            if n >= 2 then break end
            upkeep(t, 60)
            t.ticks(1)
        end
        rec.to, rec.swings = now(t), n
        D.sweep[#D.sweep + 1] = rec
    end
    D.ph.sweep.to = now(t)
end
-- the footprint's four corner tiles (one tile beyond it in x and in z)
local function diag_tiles(m)
    local s = m.size or 3
    return { { m.x - 1, m.z + s }, { m.x + s, m.z + s }, { m.x - 1, m.z - 1 }, { m.x + s, m.z - 1 } }
end
local function stand_diagonal(t)
    local m = find_ranger(t)
    if m == nil then return false, "no ranger" end
    for _, c in ipairs(diag_tiles(m)) do
        if open_tile(c[1] - BASE_X, c[2] - BASE_Z) then
            t.player.walk_to(c[1], c[2], 16)
            local s = sample(t, "diag")
            if s.dist == 1 and s.dx == 1 and s.dz == 1 then return true, string.format("%d,%d", c[1], c[2]) end
        end
    end
    return false, "no corner reached"
end
-- every ranger hit since a serial, and the ranger's attack rows, for an in-fight stop rule
local function hits_since(t, ser)
    local out = {}
    for _, h in ipairs(rows_fast(t, "hit_player", { since = ser })) do if h.npc_type == RANGER then out[#out + 1] = h end end
    return out
end
local function melee_hits_since(t, ser)
    local mt, out = {}, {}
    for _, a in ipairs(rows_fast(t, "npc_anim", { since = ser, type = RANGER })) do if a.seq == MELEE_SEQ then mt[a.tick] = true end end
    local n = 0
    for _ in pairs(mt) do n = n + 1 end
    for _, h in ipairs(hits_since(t, ser)) do if mt[h.tick] then out[#out + 1] = h.damage end end
    return out, n
end
local function ranged46(t) for _, h in ipairs(hits_since(t, D.since)) do if h.damage >= 46 then return true end end return false end
-- hold one prayer on the corner until the stop rule holds or the budget ends
local function hold_corner(t, tag, prayer, budget, stop, shot_key, shot_name)
    local from, ser = now(t), mark(t, "corner." .. tag)
    set_pray(t, prayer)
    local prev, shot, i = nil, false, 0
    while alive(t) and now(t) - from < budget do
        local s = sample(t, tag)
        if s.anim == MELEE_SEQ and prev ~= MELEE_SEQ and not shot then
            shoot(t, shot_key, shot_name, 900, 300)
            shot = true
        end
        prev = s.anim
        if s.dist ~= nil and not (s.dist == 1 and s.dx == 1 and s.dz == 1) then stand_diagonal(t) end
        if hp(t) < 50 then
            if prayer ~= "protectfrommissiles" then set_pray(t, "protectfrommissiles") end
            heal_to(t, 95)
            if prayer ~= "protectfrommissiles" then set_pray(t, prayer) end
        elseif hp(t) < 70 then
            heal_to(t, 90)
        end
        i = i + 1
        if i % 6 == 0 then
            pray_if_low(t)
            if stop(t, ser) then break end
        end
        t.ticks(1)
    end
    return { from = from, to = now(t), ser = ser }
end
-- P3: stood on the corner; Missiles held until its melee has rolled its 19 (or 30 melee swings), then Protect from Melee
local function p3_corner(t)
    local ok, where = stand_diagonal(t)
    D.ph.corner_ok, D.ph.corner_where = ok, where
    D.ph.c1 = hold_corner(t, "c1", "protectfrommissiles", 300, function(tt, ser)
        local dm, n = melee_hits_since(tt, ser)
        return (maxof(dm) or 0) >= 19 or n >= 30
    end, "c1", "p3.melee_swing_on_the_corner_missiles_up")
    D.ph.c2 = hold_corner(t, "c2", "protectfrommelee", 220, function(tt, ser)
        local _, n = melee_hits_since(tt, ser)
        local brews = supplies(tt)
        return n >= 6 and (ranged46(tt) or brews < 24)
    end, "c2", "p3.melee_swing_blocked_melee_up")
    set_pray(t, "protectfrommissiles")
    heal_to(t, 90)
end
local function last_ranged_swing(t)
    local L = nil
    for _, a in ipairs(rows_fast(t, "npc_anim", { type = RANGER })) do if a.seq == RANGED_SEQ then L = a.tick end end
    return L
end
local function wait_tick(t, tick)
    local g = 0
    while now(t) < tick and g < 8 do t.ticks(1) g = g + 1 end
end
-- one flick against the 4-tick swing on tick S. late=false: Protect from Missiles pressed on S-1 (in force on S only) and
-- off on S. late=true: pressed ON S (in force S+1, the tick after the swing) and off on S+1. manual: the two presses are
-- made with set_on_tick and the shot is taken between them, on the swing tick with the prayer lit
local function flick_cycle(t, late, manual)
    t.prayer.set("protectfrommissiles", true)
    heal_to(t, 85)
    pray_if_low(t)
    if not alive(t) then return nil end
    local L = last_ranged_swing(t)
    if L == nil then t.ticks(2) return nil end
    local swing = L + 4 * math.ceil((now(t) + 5 - L) / 4)
    local off_r = t.prayer.set_on_tick("protectfrommissiles", false, swing - 2)
    local on_tick = late and swing or swing - 1
    local rec = { swing = swing, late = late, off = off_r, manual = manual, on_tick = on_tick }
    if manual then
        local r1, d1, i1 = t.prayer.set_on_tick("protectfrommissiles", true, on_tick)
        wait_tick(t, swing)
        local key = late and "late" or "flick"
        local nm = late and "p4.late_press_lit_after_the_swing" or "p4.flick_lit_on_the_swing_tick"
        shoot(t, key, nm, 1000, 300)
        D.shots[key] = D.shots[key] .. ", swing tick " .. swing
        local r2, d2 = t.prayer.set_on_tick("protectfrommissiles", false, on_tick + 1)
        if r2 ~= "ok" then t.prayer.set("protectfrommissiles", false) end
        rec.result = (r1 == "ok" and r2 == "ok") and "ok" or ("manual " .. tostring(r1) .. "/" .. tostring(r2))
        rec.info = i1
        rec.detail = tostring(d1)
    else
        local fr, fd, info = t.prayer.flick("protectfrommissiles", on_tick + 1)
        rec.result, rec.info, rec.detail = fr, info, tostring(fd)
    end
    rec.hp = hp(t)
    D.flicks[#D.flicks + 1] = rec
    return rec
end
-- P4: off the corner (four tiles out), six flicks on the swing tick, then the press one tick late until the max is seen
local function p4_flicks(t)
    local m = find_ranger(t)
    if m and D.sweep_dir then
        local lx, lz = off_tile(m, D.sweep_dir, 4)
        t.player.walk_to(BASE_X + lx, BASE_Z + lz, 20)
    end
    D.ph.flicks = { from = now(t) }
    for i = 1, 6 do flick_cycle(t, false, i <= 1) end
    local late_n = 0
    while alive(t) and late_n < 24 do
        local brews = supplies(t)
        if late_n >= 6 and (ranged46(t) or brews < 8) then break end
        if brews == 0 and count(t, "shark") == 0 then break end
        local r = flick_cycle(t, true, late_n == 0)
        if r then late_n = late_n + 1 end
        for _ = 1, 3 do sample(t, "flick") t.ticks(1) end
    end
    t.prayer.set("protectfrommissiles", true)
    D.ph.flicks.to = now(t)
end
-- P5: a ring tile behind a pillar, chosen from the server's own line of sight (t.world.los), held while the ranger looks
-- for a way round; a failed hold is tried again from where the ranger now stands (no re-entry)
local function sgn(v) if v > 0 then return 1 elseif v < 0 then return -1 end return 0 end
local function safespot_attempt(t, attempt)
    local m = find_ranger(t)
    if m == nil then return false end
    local best, bestd, tried = nil, 1e9, {}
    local ncx, ncz = m.x + 1, m.z + 1
    for _, p in ipairs(PILLARS) do
        local cx, cz = BASE_X + p.x + 1, BASE_Z + p.z + 1
        local dx, dz = cx - ncx, cz - ncz
        local cands = {}
        if math.abs(dx) >= math.abs(dz) then cands[1] = { cx + 2 * sgn(dx), cz } cands[2] = { cx, cz + 2 * sgn(dz) }
        else cands[1] = { cx, cz + 2 * sgn(dz) } cands[2] = { cx + 2 * sgn(dx), cz } end
        for _, h in ipairs(cands) do
            local _, _, seen = t.world.los({ x = h[1], z = h[2], level = 0 }, m)
            tried[#tried + 1] = string.format("%s:%d,%d=%s", p.name, h[1] - BASE_X, h[2] - BASE_Z, tostring(seen))
            local off = math.min(math.abs(dx), math.abs(dz))
            local d = off * 100 + math.max(math.abs(h[1] - BASE_X - 30), math.abs(h[2] - BASE_Z - 32))
            if seen == false and d < bestd then best, bestd = { p.name, h[1], h[2] }, d end
        end
    end
    if best == nil then D.notes[#D.notes + 1] = "attempt " .. attempt .. ": no ring tile hides the player: " .. join(tried) return false end
    t.player.walk_to(best[2], best[3], 40)
    local from, ser = now(t), mark(t, "safespot.hold" .. attempt)
    local seen_n, samples, shot = 0, 0, false
    while now(t) - from < 24 and alive(t) do
        local s = sample(t, "safe")
        if s.sees ~= nil then samples = samples + 1 if s.sees then seen_n = seen_n + 1 end end
        if not shot and now(t) - from >= 6 and seen_n == 0 then
            shoot(t, "safe", "p5.behind_the_pillar_ranger_blind", 2200, 383, true)
            shot = true
        end
        upkeep(t, 50)
        t.ticks(1)
    end
    local swings = 0
    for _, a in ipairs(rows_fast(t, "npc_anim", { since = ser, type = RANGER })) do
        if a.tick > from and (a.seq == RANGED_SEQ or a.seq == MELEE_SEQ) then swings = swings + 1 end
    end
    local late_hits = 0
    for _, h in ipairs(hits_since(t, ser)) do if h.tick >= from + 6 then late_hits = late_hits + 1 end end
    local ok = swings == 0 and late_hits == 0 and samples >= 10 and seen_n == 0 and shot
    D.safe = { ok = ok, hits = late_hits, swings = swings, seen = seen_n, samples = samples, tile = best, from = from, to = now(t), attempt = attempt, ser = ser }
    D.notes[#D.notes + 1] = string.format("attempt %d: %d ticks on %d,%d (local %d,%d) behind pillar %s: %d swings, %d hits from tick %d, sees_player true %d of %d; tried %s",
        attempt, now(t) - from, best[2], best[3], best[2] - BASE_X, best[3] - BASE_Z, best[1], swings, late_hits, from + 6, seen_n, samples, join(tried))
    return ok
end
local function p5_safespot(t)
    D.ph.safe = { from = now(t) }
    for attempt = 1, 5 do
        if not alive(t) then break end
        if safespot_attempt(t, attempt) then break end
    end
    D.ph.safe.to = now(t)
end
-- P6: the kill with the bow under Protect from Missiles; the click, a defend and the death photographed as they happen
local function press_attack(t)
    local ar, ad = t.player.attack(RANGER_SYMBOL, 2, 10, D.cslot and { slot = D.cslot } or nil)
    local lv = string.match(tostring(ad), "%(level%-(%d+)%)")
    if lv then D.levels[#D.levels + 1] = tonumber(lv) end
    return ar, ad
end
local function p6_kill(t)
    set_pray(t, "protectfrommissiles")
    heal_to(t, 90)
    D.ph.kill = { from = now(t) }
    aim(t, 1200, 300)
    local ar, ad = press_attack(t)
    t.check("p6.attack_click", ar == "ok" or ar == "timeout", "the bow's Attack on the ranger: " .. tostring(ar) .. " " .. tostring(ad))
    D.shots.click = "p6.attack_click (server tick " .. now(t) .. ")"
    local last_hp, still, def_shot, death_shot = nil, 0, false, false
    while alive(t) and now(t) - D.ph.kill.from < 260 do
        local s = sample(t, "kill")
        if s.anim == DEFEND_SEQ and not def_shot then shoot(t, "defend", "p6.ranger_defends_the_arrow", 1000, 300) def_shot = true end
        if s.anim == DEATH_SEQ and not death_shot then shoot(t, "death", "p6.ranger_death_animation", 1000, 300) death_shot = true end
        local sr = t.npc.state(RANGER_SYMBOL)
        if sr ~= "ok" and s.rx == nil then break end
        if s.hp ~= nil then
            if s.hp == last_hp then still = still + 1 else still = 0 end
            last_hp = s.hp
            if still >= 10 then press_attack(t) still = 0 end
        end
        if upkeep(t, 55) and s.rx ~= nil then press_attack(t) still = 0 end
        t.ticks(1)
    end
    D.ph.kill.to = now(t)
    D.death_shot = death_shot
end
-- ---------------------------------------------------------------- analysis (after the fight, from the tick log)
local function sample_near(tick, radius)
    local bt = D.by_tick or {}
    for r = 0, radius do
        local s = bt[tick - r]
        if s and s.dist ~= nil then return s end
        s = bt[tick + r]
        if s and s.dist ~= nil then return s end
    end
    return nil
end
-- the player stood still across the swing: the reads on A-1 and A give one distance
local function still_dist(tick)
    local a, b = (D.by_tick or {})[tick - 1], (D.by_tick or {})[tick]
    if a and b and a.dist ~= nil and a.dist == b.dist and a.px == b.px and a.pz == b.pz then return b.dist end
    return nil
end
local function prayer_at(tick)
    local cur = nil
    for _, e in ipairs(D.pray_log) do
        if e.t0 <= tick then
            if e.t1 < tick - 1 then cur = e.p else return nil end
        end
    end
    return cur
end
local function inside(tick, ph) return ph and ph.from and tick >= ph.from and tick < (ph.to or 1e9) end
local function collect(t)
    local L = {}
    L.anims = rows(t, "npc_anim", { type = RANGER })
    L.hits = {}
    for _, h in ipairs(rows(t, "hit_player")) do if h.npc_type == RANGER then L.hits[#L.hits + 1] = h end end
    L.proj = {}
    for _, p in ipairs(rows(t, "projectile")) do if p.target == -1 then L.proj[#L.proj + 1] = p end end
    L.hitn = rows(t, "hit_npc", { type = RANGER })
    L.deaths = rows(t, "npc_death", { type = RANGER })
    L.spawns = rows(t, "npc_spawn", { type = RANGER })
    L.proj_by_tick = {}
    for _, p in ipairs(L.proj) do
        L.proj_by_tick[p.tick] = L.proj_by_tick[p.tick] or {}
        table.insert(L.proj_by_tick[p.tick], p)
    end
    local hit_ticks = {}
    for _, h in ipairs(L.hits) do hit_ticks[h.tick] = true end
    L.ranged, L.melee, L.other = {}, {}, {}
    for _, a in ipairs(L.anims) do
        if L.proj_by_tick[a.tick] then L.ranged[#L.ranged + 1] = a
        elseif hit_ticks[a.tick] then L.melee[#L.melee + 1] = a
        else L.other[#L.other + 1] = a end
    end
    L.swings = {}
    for _, a in ipairs(L.ranged) do L.swings[#L.swings + 1] = a end
    for _, a in ipairs(L.melee) do L.swings[#L.swings + 1] = a end
    table.sort(L.swings, function(x, y) return x.tick < y.tick end)
    -- every hit to the swing that threw it: ranged 3..6 ticks after, melee on the same tick
    local used, at = {}, {}
    for hi, h in ipairs(L.hits) do at[h.tick] = at[h.tick] or {} table.insert(at[h.tick], hi) end
    local function take(tick)
        for _, hi in ipairs(at[tick] or {}) do if not used[hi] then used[hi] = true return hi end end
        return nil
    end
    L.pair = {}
    for _, a in ipairs(L.melee) do
        local hi = take(a.tick)
        if hi then L.pair[a.tick] = { hit = L.hits[hi], delay = 0 } end
    end
    for _, a in ipairs(L.ranged) do
        for _, dl in ipairs({ 3, 4, 5, 6 }) do
            local hi = take(a.tick + dl)
            if hi then L.pair[a.tick] = { hit = L.hits[hi], delay = dl } break end
        end
    end
    L.unpaired = 0
    for hi, _ in ipairs(L.hits) do if not used[hi] then L.unpaired = L.unpaired + 1 end end
    return L
end
-- runs of consecutive per-tick reads of one drawn animation, bounded by reads of another on both sides
local function anim_runs(seq)
    local runs = {}
    local i, n = 1, #D.s
    while i <= n do
        if D.s[i].anim == seq and i > 1 and D.s[i - 1].tick == D.s[i].tick - 1 and D.s[i - 1].anim ~= seq then
            local j = i
            while j < n and D.s[j + 1].tick == D.s[j].tick + 1 and D.s[j + 1].anim == seq do j = j + 1 end
            if j < n and D.s[j + 1].tick == D.s[j].tick + 1 then runs[#runs + 1] = j - i + 1 end
            i = j + 1
        else
            i = i + 1
        end
    end
    return runs
end
local function emit_identity(t, L)
    local types, sizes, ssizes, ranges, hps = {}, {}, {}, {}, {}
    for _, s in ipairs(D.s) do
        if s.type then types[#types + 1] = s.type end
        if s.size then sizes[#sizes + 1] = s.size end
        if s.state_size then ssizes[#ssizes + 1] = s.state_size end
        if s.range then ranges[#ranges + 1] = s.range end
        if s.hp then hps[#hps + 1] = s.hp end
    end
    local e = D.entry or {}
    local tu = uniq(types)
    spec_row(t, "npc_id", tu == "7698" and #types >= 50, tu, "type of the ranger's npc pack row on " .. #types .. " per-tick reads (spawn row type " .. join(L.spawns, function(r) return tostring(r.type) end) .. ")", "7698 count", "A", "exact")
    local sv = (D.rec or {}).server or {}
    spec_row(t, "hitpoints", e.hp == 125 and maxof(hps) == 125, tostring(e.hp), "npc pack hitpoints on the entry tick before any hit; the highest of " .. #hps .. " per-tick reads " .. tostring(maxof(hps)) .. ", the lowest " .. tostring(minof(hps)) .. "; server record " .. tostring(sv.hitpoints), "125 hp", "A", "exact")
    local lu = uniq(D.levels)
    local cl = ((D.rec or {}).client or {}).combat_level
    spec_row(t, "combat_level", lu == "370" and #D.levels >= 1, lu, "the Attack row's menu text '(level-N)' on " .. #D.levels .. " presses (shot " .. shotname("click") .. "); client record combat_level " .. tostring(cl), "370 count", "A", "exact")
    local su, ssu = uniq(sizes), uniq(ssizes)
    spec_row(t, "size", su == "3" and ssu == "3", su, "npc pack size on " .. #sizes .. " per-tick reads and t.npc.state size " .. ssu .. " on " .. #ssizes, "3 tiles", "A", "exact")
    -- range: the farthest a swing was made from with the player standing still, and what it did at 16
    local far, at16 = 0, {}
    for _, a in ipairs(L.ranged) do
        local d = still_dist(a.tick)
        if d and d > far then far = d end
    end
    for _, r in ipairs(D.sweep) do if r.want >= 15 then at16[#at16 + 1] = "asked " .. r.want .. ": " .. r.swings .. " swings from " .. join(r.dists) end end
    local ru = uniq(ranges)
    local mv = far == 15 and "15" or ru
    spec_row(t, "attack_range", ru == "15" and far <= 15, mv, "npc pack attackrange " .. ru .. " on " .. #ranges .. " reads; the farthest ranged swing made with the player standing still was " .. far .. " tiles off the footprint (sweep room " .. tostring(D.sweep_room) .. "; " .. (#at16 > 0 and table.concat(at16, "; ") or "no hold at 15 or 16") .. ")", "15 tiles", "C", "exact")
end
local function emit_cadence(t, L)
    -- gaps between consecutive swings; 'seen' = every read between them had line of sight
    local gaps, all = {}, {}
    for i = 2, #L.swings do
        local a, b = L.swings[i - 1], L.swings[i]
        local g = b.tick - a.tick
        all[#all + 1] = g
        local blind = false
        for k = a.tick, b.tick do local s = (D.by_tick or {})[k] if s and s.sees == false then blind = true end end
        if not blind then gaps[#gaps + 1] = g end
    end
    local gu = uniq(gaps)
    spec_row(t, "attack_speed", #gaps >= 40 and gu == "4", gu, hist(gaps) .. " gaps between consecutive ranged and melee swings (npc_anim rows) with line of sight read on every tick between", "4 ticks", "C", "exact")
    spec_row(t, "attack_gap_minimum", #all >= 40 and minof(all) == 4, tostring(minof(all)), "the smallest of all " .. hist(all) .. " gaps, the safespot's blind gaps included", "4 ticks", "B", "exact")
    local first = L.swings[1]
    local far_n, far_ranged = 0, 0
    for _, a in ipairs(L.swings) do
        local sm = sample_near(a.tick, 1)
        if sm and sm.dist >= 2 then far_n = far_n + 1 if L.proj_by_tick[a.tick] then far_ranged = far_ranged + 1 end end
    end
    local ok = first and first.seq == RANGED_SEQ and far_n == far_ranged and far_n >= 20
    spec_row(t, "attack_style_ranged_primary", ok, ok and "1" or "0", string.format("its first swing (tick %s, %s after the wave began on %s) is seq %s with projectiles; %d of %d swings made from two or more tiles are ranged; %d ranged swings against %d melee over the fight (shot %s)",
        tostring(first and first.tick), tostring(first and D.begin and first.tick - D.begin), tostring(D.begin), tostring(first and first.seq), far_ranged, far_n, #L.ranged, #L.melee, shotname("held")), "1 count", "B", "exact")
    local all_seq, other_seq = {}, {}
    for _, a in ipairs(L.swings) do all_seq[#all_seq + 1] = a.seq end
    for _, a in ipairs(L.other) do other_seq[#other_seq + 1] = a.seq end
    local au = uniq(all_seq)
    spec_row(t, "melee_style_crush", au == "7604,7605", au == "7604,7605" and "1" or "0", "attack animations " .. au .. " over " .. #L.swings .. " swings (no third); the other animations it played: " .. uniq(other_seq), "1 count", "B", "exact")
end
local function dmg_of(L, swings, keep)
    local out = {}
    for _, a in ipairs(swings) do
        local pr = L.pair[a.tick]
        if pr and (keep == nil or keep(a)) then out[#out + 1] = pr.hit.damage end
    end
    return out
end
local function missiles_held(tick)
    return inside(tick, { from = D.ph.watch.from, to = (D.ph.c1 or {}).to }) or inside(tick, { from = (D.ph.safe or {}).from, to = (D.ph.kill or {}).to })
end
local function emit_hits(t, L)
    local rd, md = dmg_of(L, L.ranged), dmg_of(L, L.melee)
    local unp = dmg_of(L, L.ranged, function(a) return inside(a.tick, D.ph.c2) or false end)
    for _, f in ipairs(D.flicks) do if f.late and L.pair[f.swing] and L.proj_by_tick[f.swing] then unp[#unp + 1] = L.pair[f.swing].hit.damage end end
    local rmax, mmax = maxof(rd) or -1, maxof(md) or -1
    spec_row(t, "max_hit_ranged", rmax == 46, tostring(rmax), "the largest of " .. #rd .. " ranged hit_player rows paired to their swing; the " .. #unp .. " unprayed ones (Protect from Melee up on the corner, the late presses): " .. join(unp), "46 hp", "C", "exact")
    spec_row(t, "max_hit_melee", mmax == 19, tostring(mmax), "the largest of " .. #md .. " melee hit_player rows on their swing's tick: " .. join(md), "19 hp", "C", "exact")
    local diag, adj = 0, 0
    for _, a in ipairs(L.melee) do
        local sm = sample_near(a.tick, 1)
        if sm and sm.dist == 1 then adj = adj + 1 if sm.dx == 1 and sm.dz == 1 then diag = diag + 1 end end
    end
    spec_row(t, "melee_range_diagonal_counts", diag >= 2, diag >= 2 and "1" or "0", diag .. " of " .. #L.melee .. " melee swings made with the player on the footprint's corner tile, one tile beyond it in x and in z (shot " .. shotname("c1") .. ")", "1 count", "C", "exact")
    local held = dmg_of(L, L.ranged, function(a) return missiles_held(a.tick) end)
    local on = {}
    for _, f in ipairs(D.flicks) do if not f.late and L.pair[f.swing] and L.proj_by_tick[f.swing] then on[#on + 1] = L.pair[f.swing].hit.damage held[#held + 1] = L.pair[f.swing].hit.damage end end
    local hu = uniq(held)
    spec_row(t, "damage_when_protected_missiles", hu == "0" and #held >= 30, join(held, nil, 400), #held .. " ranged hits whose swing tick had Protect from Missiles in force (held from the entry to the corner's melee phase, from the safespot to the kill, and " .. #on .. " flicks on the swing tick)", "0 hp", "C", "exact")
    local blocked = {}
    for _, a in ipairs(L.melee) do
        local pr = L.pair[a.tick]
        if pr and inside(a.tick, D.ph.c2) and prayer_at(a.tick) == "protectfrommelee" then blocked[#blocked + 1] = pr.hit.damage end
    end
    spec_row(t, "melee_blocked_by_protect_melee", uniq(blocked) == "0" and #blocked >= 4, join(blocked), #blocked .. " melee hits on the corner with Protect from Melee up and Missiles not (shot " .. shotname("c2") .. ")", "0 hp", "C", "exact")
    -- delay by distance: swings made with the player standing still
    local delays, by = {}, {}
    for _, a in ipairs(L.ranged) do
        local pr, d = L.pair[a.tick], still_dist(a.tick)
        if pr and d then delays[#delays + 1] = pr.delay by[#by + 1] = "d" .. d .. ":" .. pr.delay end
    end
    local _, bc = uniq(by)
    local keys = {}
    for k in pairs(bc) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) local x, y = tonumber(string.match(a, "d(%d+)")), tonumber(string.match(b, "d(%d+)")) if x ~= y then return x < y end return a < b end)
    local parts = {}
    for _, k in ipairs(keys) do parts[#parts + 1] = k .. "x" .. bc[k] end
    local nd = {}
    for _, k in ipairs(keys) do nd[string.match(k, "d(%d+)")] = true end
    local ndn = 0
    for _ in pairs(nd) do ndn = ndn + 1 end
    D.delay_text = table.concat(parts, " ")
    spec_row(t, "hit_delay_by_distance", ndn >= 6, uniq(delays), "ticks from the swing's npc_anim row to its hit_player row over " .. #delays .. " ranged swings made with the player standing still, by distance off the footprint (distance:ticks x count) " .. D.delay_text .. " (shots " .. shotname("sweep4") .. ", " .. shotname("sweep10") .. "); approximation, M37", "?", "E", "approx")
end
local function emit_prayer(t, L)
    local on, late = {}, {}
    for _, f in ipairs(D.flicks) do
        local pr = L.pair[f.swing]
        if pr and L.proj_by_tick[f.swing] and f.result == "ok" then
            if f.late then late[#late + 1] = pr.hit.damage else on[#on + 1] = pr.hit.damage end
        end
    end
    local late_pos = 0
    for _, v in ipairs(late) do if v > 0 then late_pos = late_pos + 1 end end
    D.flick_res = { on = on, late = late, late_pos = late_pos }
    local ok = #on >= 4 and uniq(on) == "0" and #late >= 6 and late_pos == #late
    spec_row(t, "prayer_read_tick", ok, ok and "0" or "?", string.format("Protect from Missiles in force on the swing tick only (pressed S-1, off on S; the hit lands 3-6 ticks later with the prayer down): %d hits took %s; in force from S+1 only (pressed on S): %d of %d hits took damage %s (shots %s, %s)",
        #on, join(on), late_pos, #late, join(late), shotname("flick"), shotname("late")), "0 ticks", "C", "exact")
end
local function emit_projectiles(t, L)
    local sp, per, flight, byd = {}, {}, {}, {}
    for _, a in ipairs(L.ranged) do
        local list = L.proj_by_tick[a.tick]
        per[#per + 1] = #list
        for _, p in ipairs(list) do sp[#sp + 1] = p.spotanim end
        local p, d = list[1], still_dist(a.tick)
        if p and p.start_cycle and p.end_cycle then
            flight[#flight + 1] = p.end_cycle - p.start_cycle
            if d then byd[#byd + 1] = string.format("d%02d:%d-%d", d, p.start_cycle, p.end_cycle) end
        end
    end
    local spu = uniq(sp)
    spec_row(t, "projectile_spotanim", spu == "1377" and #sp >= 60, spu, #sp .. " projectile rows aimed at the player on the ranger's swing ticks (shot " .. shotname("sweep10") .. ")", "1377 count", "D", "exact")
    spec_row(t, "projectiles_per_attack", uniq(per) == "2" and #per >= 30, uniq(per), hist(per) .. ": projectile rows per ranged swing tick", "2 count", "D", "exact")
    spec_row(t, "projectile_shape", #byd >= 12, uniq(flight), "flight in client cycles (end_cycle - start_cycle of a swing's first projectile) " .. hist(flight) .. "; start-end by distance off the footprint, standing still: " .. uniq(byd) .. "; approximation, M40", "?", "E", "approx")
    local seqs, ms = {}, {}
    for _, a in ipairs(L.ranged) do seqs[#seqs + 1] = a.seq end
    for _, a in ipairs(L.melee) do ms[#ms + 1] = a.seq end
    spec_row(t, "attack_ranged_seq", uniq(seqs) == "7605" and #seqs >= 30, uniq(seqs), #seqs .. " ranger npc_anim rows on a tick that carries its projectile rows (shot " .. shotname("held") .. ")", "7605 count", "B", "exact")
    spec_row(t, "attack_melee_seq", uniq(ms) == "7604" and #ms >= 5, uniq(ms), #ms .. " ranger npc_anim rows with no projectile and a hit_player row on the same tick (shot " .. shotname("c1") .. ")", "7604 count", "B", "exact")
    local rr, mr = anim_runs(RANGED_SEQ), anim_runs(MELEE_SEQ)
    spec_row(t, "attack_ranged_anim_length", #rr >= 10 and uniq(rr) == "3", uniq(rr), hist(rr) .. ": runs of consecutive per-tick t.npc.state reads of drawn animation 7605 between reads of another", "3 ticks", "A", "exact")
    spec_row(t, "attack_melee_anim_length", #mr >= 3 and uniq(mr) == "2", uniq(mr), hist(mr) .. ": runs of consecutive per-tick reads of drawn animation 7604", "2 ticks", "A", "exact")
end
local function emit_rest(t, L)
    local checked, hidden = 0, 0
    for _, a in ipairs(L.swings) do
        local s0, s1 = (D.by_tick or {})[a.tick - 1], (D.by_tick or {})[a.tick]
        if s0 and s1 and s0.sees ~= nil and s1.sees ~= nil then
            checked = checked + 1
            if s0.sees == false and s1.sees == false then hidden = hidden + 1 end
        end
    end
    local blind = 0
    for _, s in ipairs(D.s) do if s.tag == "safe" and s.sees == false then blind = blind + 1 end end
    spec_row(t, "attack_needs_line_of_sight", hidden == 0 and checked >= 30 and blind >= 10, tostring(hidden), hidden .. " of " .. checked .. " swings made with sees_player false on the swing tick and the tick before; " .. blind .. " per-tick reads behind the pillar read false (shot " .. shotname("safe") .. ")", "0 count", "C", "exact")
    local defend, death_seq = {}, {}
    for _, a in ipairs(L.other) do
        local at_death = false
        for _, d in ipairs(L.deaths) do if a.tick >= d.tick and a.tick <= d.tick + 2 then at_death = true end end
        if at_death then death_seq[#death_seq + 1] = a.seq
        else
            for _, h in ipairs(L.hitn) do if a.tick - h.tick == 0 or a.tick - h.tick == 1 then defend[#defend + 1] = a.seq break end end
        end
    end
    spec_row(t, "defend_seq", #defend >= 3, uniq(defend), hist(defend) .. ": ranger npc_anim rows within a tick of a hit_npc row on it (" .. #L.hitn .. " hits), the death excluded (shot " .. shotname("defend") .. "); approximation, M39", "7607 count", "E", "approx")
    spec_row(t, "death_seq", #death_seq >= 1 and #L.deaths >= 1, uniq(death_seq), hist(death_seq) .. ": the animation sent in the two ticks after each of the " .. #L.deaths .. " npc_death rows (wave 18 has one ranger and the test enters once) (shot " .. shotname("death") .. "); approximation, M39", "7606 count", "E", "approx")
end
local function dump(v, d)
    d = d or 0
    if type(v) ~= "table" then return tostring(v) end
    if d > 2 then return "{..}" end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    local out = {}
    for _, k in ipairs(keys) do out[#out + 1] = tostring(k) .. "=" .. dump(v[k], d + 1) end
    return "{" .. table.concat(out, ",") .. "}"
end
local function emit_record_rows(t)
    local rec = D.rec or {}
    local sv, cl = rec.server or {}, rec.client or {}
    local bn = sv.bonus or {}
    local src = "t.npc.record server block (the content the combat rolls with)"
    spec_row(t, "defence_level", sv.defence == 60, tostring(sv.defence), src .. ": defence " .. tostring(sv.defence), "60 count", "A", "exact")
    spec_row(t, "attack_strength_levels", sv.attack == 140 and sv.strength == 180, tostring(sv.attack) .. "," .. tostring(sv.strength), src .. ": attack " .. tostring(sv.attack) .. " strength " .. tostring(sv.strength), "140,180 count", "A", "exact")
    spec_row(t, "ranged_magic_levels", sv.ranged == 250 and sv.magic == 90, tostring(sv.ranged) .. "," .. tostring(sv.magic), src .. ": ranged " .. tostring(sv.ranged) .. " magic " .. tostring(sv.magic), "250,90 count", "A", "exact")
    spec_row(t, "ranged_attack_bonus", bn.rangeattack == 40, tostring(bn.rangeattack), src .. ": bonus " .. dump(bn), "40 count", "A", "exact")
    local rs, rsname
    for id, v in pairs(cl.params or {}) do
        local nm = tostring((cl.param_names or {})[id])
        if string.find(nm, "ammo", 1, true) or string.find(nm, "rangebonus", 1, true) then rs, rsname = v, nm end
    end
    spec_row(t, "ranged_strength_bonus", rs == 50, tostring(rs), "client npc record param " .. tostring(rsname) .. "; server bonus strengthbonus " .. tostring(bn.strengthbonus) .. "; client params " .. dump(cl.params), "50 count", "A", "exact")
    local zero = (bn.stabdefence == 0 and bn.slashdefence == 0 and bn.crushdefence == 0 and bn.magicdefence == 0 and bn.rangedefence == 0) and 1 or 0
    spec_row(t, "defence_bonuses_zero", zero == 1, tostring(zero), src .. ": bonus " .. dump(bn), "1 count", "A", "exact")
    local m1 = cl.models and cl.models[1]
    spec_row(t, "model", m1 == 33014, tostring(m1), "client npc record models: " .. dump(cl.models), "33014 count", "A", "exact")
    spec_row(t, "ready_walk_seq", cl.readyanim == 7602 and cl.walkanim == 7603, tostring(cl.readyanim) .. "," .. tostring(cl.walkanim), "client npc record readyanim " .. tostring(cl.readyanim) .. " (" .. tostring(cl.readyanim_name) .. ") walkanim " .. tostring(cl.walkanim) .. " (" .. tostring(cl.walkanim_name) .. ")", "7602,7603 count", "A", "exact")
    local snd = tostring(sv.attack_sound) .. "," .. tostring(sv.defend_sound) .. "," .. tostring(sv.death_sound)
    spec_row(t, "sounds", sv.attack_sound ~= nil, snd, "server record attack, defend, death sound (the tick log has no sound kind); approximation, M38", "?", "E", "approx")
end
local function emit_techniques(t, L)
    local fr = D.flick_res or { on = {}, late = {}, late_pos = 0 }
    local cost, n = 0, 0
    for _, f in ipairs(D.flicks) do
        if not f.late and not f.manual and f.info and f.info.points_before and f.info.points_after then cost = cost + (f.info.points_before - f.info.points_after) n = n + 1 end
    end
    t.expect("technique.one_tick_flick", (#fr.on >= 4 and uniq(fr.on) == "0" and cost <= 1) and "ok" or "fail", string.format("against the lone ranger, Protect from Missiles lit only across the swing tick (on S-1, off S): %d hits took %s; the %d t.prayer.flick cycles cost %d prayer points; pressed one tick late the same hit landed %d of %d times (%s); shot %s shows the prayer lit as the ranger swings, %s the late press",
        #fr.on, join(fr.on), n, cost, fr.late_pos, #fr.late, join(fr.late), shotname("flick"), shotname("late")))
    local held = dmg_of(L, L.ranged, function(a) return missiles_held(a.tick) end)
    local unp = dmg_of(L, L.ranged, function(a) return inside(a.tick, D.ph.c2) or false end)
    local sum = 0
    for _, v in ipairs(unp) do sum = sum + v end
    t.expect("technique.pray_by_danger", (#held >= 30 and uniq(held) == "0" and #unp >= 3 and sum > 0) and "ok" or "fail", string.format("wave 18's only ranged attacker is the ranger (max 46; the nibblers bite the pillars): Protect from Missiles up from the entry, %d of its ranged hits landed %s; with Protect from Melee up instead its ranged hits were %s (shots %s, %s)",
        #held, uniq(held), join(unp), shotname("held"), shotname("c2")))
    local sf = D.safe or {}
    t.expect("technique.pillar_safespot", sf.ok and "ok" or "fail", string.format("attempt %s: %s ticks on %s behind the %s pillar from tick %s: %s swings and %s hit_player rows (from hold+6), sees_player true in %s of %s per-tick reads; shot %s; %s",
        tostring(sf.attempt), tostring((sf.to or 0) - (sf.from or 0)), sf.tile and (sf.tile[2] .. "," .. sf.tile[3]) or "?", sf.tile and sf.tile[1] or "?", tostring(sf.from), tostring(sf.swings), tostring(sf.hits), tostring(sf.seen), tostring(sf.samples), shotname("safe"), table.concat(D.notes, " | ")))
    local far_m, adj_m = 0, 0
    for _, a in ipairs(L.melee) do
        local sm = sample_near(a.tick, 1)
        if sm and sm.dist >= 2 then far_m = far_m + 1 elseif sm then adj_m = adj_m + 1 end
    end
    t.expect("technique.do_not_stand_beside_it", (far_m == 0 and adj_m >= 5 and #L.ranged >= 30) and "ok" or "fail", string.format("%d melee swings (seq %d) came with the player on the footprint's edge or corner and %d with the player two or more tiles off, against %d ranged swings; shot %s",
        adj_m, MELEE_SEQ, far_m, #L.ranged, shotname("c1")))
end
return {
    id = "inferno_ranger",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::setlevel attack 99", "::setlevel strength 99", "::setlevel ranged 99", "::setlevel magic 99",
        "::setlevel hitpoints 99", "::setlevel defence 60", "::setlevel prayer 99",
        "::give twisted_bow", "::give rune_arrow 1500", "::give 4dosepotionofsaradomin 14", "::give 4dose2restore 7",
        "::give shark 5", "::setvar varp172_option_nodef 1",
    },
    run = function(t)
        t.ticklog.start()
        t.check("spec.scope", true, "mode=normal party=1")
        t.player.equip("twisted_bow")
        t.player.equip("rune_arrow")
        local wr, w = t.ui.widget("orbs:runbutton")
        if wr == "ok" then t.ui.invoke(w, 1) end
        D.since = mark(t, "run.begin")
        if not p0_entry(t) then return end
        p1_nibblers(t)
        p2_sweep(t)
        p3_corner(t)
        p4_flicks(t)
        p5_safespot(t)
        if D.rec == nil or D.rec.client == nil then local _, _, rec = t.npc.record(RANGER_SYMBOL) if type(rec) == "table" then D.rec = rec end end
        p6_kill(t)
        t.ticks(2)
        local L = collect(t)
        local b, r, s = supplies(t)
        note(t, "fight.summary", string.format("phases %s; brews %d doses, restores %d doses, sharks %d left; hits not paired to a swing %d; sweep %s",
            dump(D.ph), b, r, s, L.unpaired, join(D.sweep, function(x) return x.want .. ":" .. x.swings end)))
        emit_identity(t, L) t.ticks(1)
        emit_cadence(t, L) t.ticks(1)
        emit_hits(t, L) t.ticks(1)
        emit_prayer(t, L) t.ticks(1)
        emit_projectiles(t, L) t.ticks(1)
        emit_rest(t, L) t.ticks(1)
        emit_record_rows(t) t.ticks(1)
        emit_techniques(t, L)
    end,
}
