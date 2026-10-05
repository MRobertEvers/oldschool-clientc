-- inferno_melee: Jal-ImKot (spec docs/minigames/inferno/encounters/melee.tsv, sidecar melee.scope.tsv).
-- ONE practice entry of wave 9 (the first wave with a meleer), fought for real with a bow, Ice Barrage and the prayer book:
--   P1 a prayed stand from the wave start (its swings before offset 30), then out of its reach before the swing that
--      would fall at offset 30 or later; P2 a pillar between (else a kite) until it digs at offset 50; the prayer is
--      pressed in the six ticks after it resurfaces; P3 a held stand, flicks on the swing tick and presses one tick late;
--   P4 out of reach again, Ice Barrage holds it, the player waits beside a pillar; the attack click after it surfaces;
--   P5 the kill with the bow under Protect from Melee.
-- Every shot is taken inside the fight on the tick of its event (the swing, the dig, the press, the click, the death);
-- the ledger rows after the fight are t.expect rows computed from the tick log that name those shots.
-- Setup = bring-alongs only (levels, a bow, food, restores, runes, the Ancient spellbook, auto-retaliate off).
local MELEE, MELEE_SYMBOL = 7697, "inferno_creature_melee"
local NIB, NIB_SYMBOL = 7691, "inferno_nibbler"
local ATTACK, DEFEND, DEATH, DIGDOWN, DIGUP = 7597, 7598, 7599, 7600, 7601
local WAVE = 9
local BASE_X, BASE_Z = 6400, 64
-- the pillars' south-west tiles, region-local (inferno.constant ^inferno_pillar_*), 3x3 each
local PILLARS = { { name = "west", x = 17, z = 37 }, { name = "south", x = 27, z = 23 }, { name = "east", x = 34, z = 39 } }
local D = { since = nil, shots = {}, levels = {}, sizes = {}, ph = {}, size = 4, notes = {} }

local function now(t) local _, k = t.tick() return k or -1 end
local function alive(t) return t.player.alive() == "ok" end
local function hp(t) local _, a = t.skill.read("hitpoints") if type(a) == "table" then return a.level or -1 end return -1 end
local function note(t, label, text) t.expect(label, "ok", text) end
local function rows(t, kind, opts)
    opts = opts or {}
    opts.kind = kind
    if opts.since == nil then opts.since = D.since end
    local r, list = t.ticklog.rows(opts)
    if r ~= "ok" then return {} end
    return list
end
local function join(list, f, cap)
    local out = {}
    for i = 1, math.min(#list, cap or 40) do out[#out + 1] = f and f(list[i]) or tostring(list[i]) end
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
local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
-- Chebyshev distance from a tile to a 4x4 footprint whose south-west tile is (mx, mz)
local function foot_dist(x, z, mx, mz)
    local n = D.size - 1
    local dx = math.max(mx - x, 0, x - (mx + n))
    local dz = math.max(mz - z, 0, z - (mz + n))
    return math.max(dx, dz)
end
local function spec_row(t, id, ok, measured, extra, specv, grade, tol)
    local detail = "measured " .. measured .. ((extra and extra ~= "") and (", " .. extra) or "") .. " (spec " .. specv .. ", grade " .. grade .. ", tol " .. tol .. ")"
    t.expect("spec.melee." .. id, ok and "ok" or "fail", detail)
end
-- a text row: the measured text runs to the first ';', the evidence follows it
local function text_row(t, id, ok, measured, extra, specv, grade)
    t.expect("spec.melee." .. id, ok and "ok" or "fail", "measured " .. measured .. "; " .. extra .. " (spec " .. specv .. ", grade " .. grade .. ", tol exact)")
end
local step_out
local function find_melee(t)
    local _, _, pack, raw = t.npc.pack(60)
    for _, p in ipairs(pack or {}) do
        if p.type == MELEE then
            if p.client_slot and p.client_slot >= 0 then D.cslot = p.client_slot end
            return p, raw, pack
        end
    end
    return nil, raw, pack
end
-- the melee's client slot as a selector option (refreshed by every pack read)
local function msel(extra)
    local o = extra or {}
    if D.cslot and D.cslot >= 0 then o.slot = D.cslot end
    return o
end
-- turn the camera to the melee: yaw runs counter-clockwise from north (512 faces west), so the bearing is atan(-dx, dz)
local function aim(t, zoom, pitch, side)
    local m, raw = find_melee(t)
    if not m or not raw or not raw.player_x then return false end
    local px, pz = raw.player_x, raw.player_z
    local dx, dz = (m.x + 1.5) - px, (m.z + 1.5) - pz
    local base = 0
    if math.abs(dx) + math.abs(dz) > 0.6 then base = math.atan(-dx, dz) end
    -- the camera sits behind the player: of five turns take the one whose camera side has no pillar near the player
    local best, bs = 0, -1
    -- 'side': look across the line from the player to the melee, so a pillar between them hides neither
    for _, turn in ipairs(side and { 512, -512, 384, -384 } or { 0, 256, -256, 512, -512 }) do
        local th = base + turn * 2 * math.pi / 2048
        local cx, cz = math.sin(th), -math.cos(th)
        local score = math.pi
        -- the side panel covers the right of the 3D view: a side view keeps the melee on the screen's left
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
    t.drive.camera(yaw, pitch or 383, zoom or 1000)
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
local function eat_if_low(t, below)
    local h = hp(t)
    if h > 0 and h < (below or 50) then t.player.inv_op("shark", 1) return true end
    return false
end
local function pray_if_low(t)
    local _, _, pts = t.prayer.points()
    if pts and pts.level and pts.level < 25 then t.player.drink("prayer_potion") return true end
    return false
end
-- the player's gap to the melee's footprint now (99 when either is unread)
local function gap_now(t)
    local m, raw = find_melee(t)
    if not m or not raw or not raw.player_x then return 99 end
    return foot_dist(raw.player_x, raw.player_z, m.x, m.z)
end
local function melee_anims(t, from, seq)
    local out = {}
    for _, a in ipairs(rows(t, "npc_anim", { type = MELEE })) do
        if a.tick >= (from or 0) and (seq == nil or a.seq == seq) then out[#out + 1] = a end
    end
    return out
end
local function last_of(list) return list[#list] end
local function player_xz(t)
    local _, raw = find_melee(t)
    if raw and raw.player_x then return raw.player_x, raw.player_z end
    return nil, nil
end
local function run_on(t)
    local wr, w = t.ui.widget("orbs:runbutton")
    if wr == "ok" then t.ui.invoke(w, 1) end
end
-- open tiles to run to, region-local (the arena's floor clear of the pillars)
local CANDIDATES = { {20,30},{25,45},{35,45},{45,35},{45,25},{35,19},{22,22},{30,30},{40,44},{16,44},{42,30},{30,45},{14,30},{30,16},{24,40},{38,26} }
local function flee_tile(m, px, pz, any)
    local best, bv = nil, -1e9
    for _, c in ipairs(CANDIDATES) do
        local cx, cz = BASE_X + c[1], BASE_Z + c[2]
        local fd = foot_dist(cx, cz, m.x, m.z)
        local v = fd - 0.3 * cheb(cx, cz, px, pz)
        -- away from the melee: the run must not cross its footprint
        local away = any or (cx - px) * (px - (m.x + 1.5)) + (cz - pz) * (pz - (m.z + 1.5)) >= 0
        if fd >= 5 and away and v > bv and (cx ~= px or cz ~= pz) then best, bv = { cx, cz }, v end
    end
    if not best and not any then return flee_tile(m, px, pz, true) end
    return best
end
-- a tile with a pillar between it and the melee: one tile off the pillar's far side on the axis the melee comes along
local function pillar_spot(m, px, pz)
    local best, bd = nil, 1e9
    local mcx, mcz = m.x + 1.5, m.z + 1.5
    for _, p in ipairs(PILLARS) do
        local cx, cz = BASE_X + p.x + 1, BASE_Z + p.z + 1
        local vx, vz = cx - mcx, cz - mcz
        local tx, tz
        if math.abs(vx) >= math.abs(vz) then tx, tz = cx + (vx > 0 and 5 or -5), cz else tx, tz = cx, cz + (vz > 0 and 5 or -5) end
        local fd = foot_dist(tx, tz, m.x, m.z)
        local d = cheb(tx, tz, px, pz)
        if fd >= 4 and d < bd then best, bd = { tx, tz, p.name }, d end
    end
    return best
end
-- keep out of the melee's reach until stop() answers; a pillar spot first (when 'pillar'), a run to an open tile when it closes in
local function hold_out(t, stop, budget, opts)
    opts = opts or {}
    local start = now(t)
    local H = { moves = 0, spot = nil, held_from = nil, samples = {} }
    while alive(t) and now(t) - start < budget do
        if stop and stop() then break end
        local m, raw = find_melee(t)
        if m and raw and raw.player_x then
            local px, pz = raw.player_x, raw.player_z
            local fd = foot_dist(px, pz, m.x, m.z)
            H.samples[#H.samples + 1] = { tick = now(t), px = px, pz = pz, mx = m.x, mz = m.z, fd = fd }
            if opts.on_tick then opts.on_tick(m, raw, H) end
            if opts.pillar and H.spot == nil and fd >= 4 then
                H.spot = pillar_spot(m, px, pz)
                if H.spot then t.player.walk_to(H.spot[1], H.spot[2], 1) H.moves = H.moves + 1 end
            end
            if fd <= 1 then t.prayer.set("protectfrommelee", true) H.caught = (H.caught or 0) + 1 end
            eat_if_low(t, 50)
            if fd <= (opts.keep or 2) then
                local f = flee_tile(m, px, pz)
                if f then t.player.walk_to(f[1], f[2], 1) H.moves = H.moves + 1 end
                if H.spot then H.spot.broken = now(t) end
            end
        end
        t.ticks(1)
    end
    return H
end

-- the one entry: wave 9, read the melee's pack row at the wave's begin and shoot it standing on its spawn tile
local function enter(t)
    local r, d = t.wave.enter("inferno", WAVE)
    D.enter_result, D.enter_detail = r, tostring(d)
    D.begin = tonumber(string.match(tostring(d), "wave begun by server tick (%d+)"))
    local m = find_melee(t)
    if m then
        D.hp0, D.cslot, D.wslot = m.hitpoints, m.client_slot, m.slot
        D.sizes[#D.sizes + 1] = m.size
        D.size = m.size or 4
        D.spawn_seen = { x = m.x, z = m.z }
    end
    local sr, st = t.npc.state(msel())
    if sr == "ok" and type(st) == "table" and st.size then D.sizes[#D.sizes + 1] = st.size end
    t.ticklog.mark("enter.wave9")
    -- the client draws an npc from 15 tiles: wait (at most 12 ticks) for the melee walking in from its spawn
    -- the client draws an npc from 15 tiles: on this tick the melee is still beyond that, so this frame is the arena at
    -- the wave's begin; the first frame with the melee in it is the P1 swing shot
    aim(t, 2600, 383, true)
    t.check("entry.wave9_begun_melee_beyond_draw_range", r == "ok" and m ~= nil and D.begin ~= nil,
        string.format("%s; melee pack row at the wave's begin %s; its footprint stands %d tiles off, at the edge of the 15 the client draws, so it is not in this frame (first seen in shot p1.prayer_up_as_the_melee_swings)", D.enter_detail:sub(1, 160), m and string.format("slot %d at %d,%d size %s hitpoints %s", m.slot, m.x, m.z, tostring(m.size), tostring(m.hitpoints)) or "none", gap_now(t)))
    D.shots.entry = "entry.wave9_begun_melee_beyond_draw_range (server tick " .. now(t) .. ")"
    return r == "ok" and m ~= nil and D.begin ~= nil
end

-- the nibblers: one Ice Barrage at the one the other two stand beside, so the pillars stand for the fight
local function barrage_nibblers(t)
    local _, _, pack = t.npc.pack(40)
    local nibs = {}
    for _, p in ipairs(pack or {}) do if p.type == NIB and p.client_slot and p.client_slot >= 0 then nibs[#nibs + 1] = p end end
    local mid, cover = nil, -1
    for _, p in ipairs(nibs) do
        local c = 0
        for _, q in ipairs(nibs) do if cheb(p.x, p.z, q.x, q.z) <= 1 then c = c + 1 end end
        if c > cover then mid, cover = p, c end
    end
    D.nib = { count = #nibs, cover = cover, tick = now(t) }
    if mid then
        local r, d = t.player.cast("ice_barrage", NIB_SYMBOL, 6, 2, { slot = mid.client_slot })
        D.nib.result, D.nib.detail = tostring(r), tostring(d):sub(1, 120)
    end
end

local function on_pillar(x, z)
    for _, p in ipairs(PILLARS) do
        local sx, sz = BASE_X + p.x, BASE_Z + p.z
        if x >= sx and x <= sx + 2 and z >= sz and z <= sz + 2 then return true end
    end
    return false
end
-- the nearest tile beside the footprint on a side (not a corner: a melee swing wants a side), off the pillars
local function side_tile(m, px, pz)
    local n = D.size - 1
    local best, bd = nil, 1e9
    for i = 0, n do
        for _, c in ipairs({ { m.x + i, m.z - 1 }, { m.x + i, m.z + n + 1 }, { m.x - 1, m.z + i }, { m.x + n + 1, m.z + i } }) do
            local d = cheb(c[1], c[2], px, pz)
            if not on_pillar(c[1], c[2]) and d < bd then best, bd = c, d end
        end
    end
    if best then return best[1], best[2] end
    return nil, nil
end
-- after a dig the melee surfaces with the player under its footprint: one step to the nearest side tile (the player's move)
step_out = function(t, G)
    local m, raw = find_melee(t)
    if not m or not raw or not raw.player_x then return end
    local px, pz = raw.player_x, raw.player_z
    G.under = foot_dist(px, pz, m.x, m.z) == 0
    G.land_seen = { m.x, m.z }
    G.step = { from = { px, pz }, tick = now(t) }
    if not G.under then return end
    -- the nearest tile on a side of the footprint (one step off an edge, more from its middle)
    local tx, tz = side_tile(m, px, pz)
    if tx then
        local r = t.player.walk_to(tx, tz, 3)
        G.step.result, G.step.to = tostring(r), { tx, tz }
    end
end

-- P1: Protect from Melee up, walk to the melee and take its swings until the next one would fall at offset 30 or later
local function engage_early(t)
    local bg = D.begin
    local P = { from = now(t) }
    D.ph.p1 = P
    t.prayer.set("protectfrommelee", true)
    -- the nibblers first (technique 1): one bow press at the nearest; it is also the player's first fight of the session
    local _, _, pack = t.npc.pack(40)
    local nib, nd = nil, 1e9
    for _, q in ipairs(pack or {}) do if q.type == NIB and q.client_slot and q.client_slot >= 0 then local d = q.gap_player or 99 if d < nd then nib, nd = q, d end end end
    if nib then
        local nr, ndt = t.player.attack(NIB_SYMBOL, 2, 3, { slot = nib.client_slot, quick = true })
        P.nibbler = tostring(nr) .. " at slot " .. nib.slot .. " (" .. tostring(ndt):sub(1, 60) .. ")"
    end
    local shot = false
    while alive(t) and now(t) < bg + 40 do
        local sw = melee_anims(t, bg, ATTACK)
        local L = last_of(sw)
        if L and not shot then
            shoot(t, "p1_swing", "p1.prayer_up_as_the_melee_swings", 900)
            shot = true
        end
        if L and L.tick + 4 >= bg + 30 then P.last = L.tick break end
        if not L and now(t) >= bg + 27 then P.none = true break end
        -- its first swing fixes a 4-tick grid; when the grid misses offset 29, one pause out of reach moves it there
        if L and not P.paused then
            P.paused = true
            local o = L.tick - bg
            local shift = (29 - o) % 4
            local m, raw = find_melee(t)
            if shift ~= 0 and o + 4 + shift <= 29 and m and raw and raw.player_x then
                local px, pz = raw.player_x, raw.player_z
                local ox, oz = px, pz
                if pz > m.z + D.size - 1 then oz = pz + 1 elseif pz < m.z then oz = pz - 1 elseif px < m.x then ox = px - 1 else ox = px + 1 end
                P.shift, P.pause_from = shift, L.tick
                local r1 = t.player.step_tick(ox, oz)
                while alive(t) and now(t) < L.tick + 2 + shift do t.ticks(1) end
                local r2 = t.player.step_tick(px, pz)
                P.pause = string.format("swing on offset %d; out to %d,%d (%s), back to %d,%d (%s) on tick %d", o, ox, oz, tostring(r1), px, pz, tostring(r2), now(t))
            end
        end
        local m, raw = find_melee(t)
        if m and raw and raw.player_x then
            local px, pz = raw.player_x, raw.player_z
            local still = P.lastm ~= nil and P.lastm[1] == m.x and P.lastm[2] == m.z
            P.lastm = { m.x, m.z }
            local gap = foot_dist(px, pz, m.x, m.z)
            if gap == 0 then
                -- under its footprint it cannot swing and does not step clear (ENG-90): step out to a side
                local tx, tz = side_tile(m, px, pz)
                if tx then t.player.walk_to(tx, tz, 1) end
                P.stepped_out = (P.stepped_out or 0) + 1
            elseif not L and still and gap > 1 and now(t) >= bg + 4 then
                local tx, tz = side_tile(m, px, pz)
                if tx then t.player.walk_to(tx, tz, 1) end
            end
        end
        t.ticks(1)
    end
    P.swings = {}
    for _, a in ipairs(melee_anims(t, bg, ATTACK)) do P.swings[#P.swings + 1] = a.tick - bg end
    P.to = now(t)
end

-- out of reach (a pillar spot when asked) until its next dig after 'from'; shoot it going down and coming up
local function watch_dig(t, k, from, budget, hold_opts)
    local G = { k = k, from = from }
    G.hold = hold_out(t, function() return #melee_anims(t, from, DIGDOWN) > 0 end, budget, hold_opts)
    local dd = melee_anims(t, from, DIGDOWN)[1]
    if not dd then return G end
    G.dig = dd.tick
    shoot(t, "dig" .. k .. "_down", "dig" .. k .. ".melee_digging_down_7600", math.min(2800, 1200 + 130 * gap_now(t)), 383, true)
    -- nothing can hit the player while it is under ground: the prayer goes off until the resurface window
    t.prayer.set("protectfrommelee", false)
    local guard = now(t)
    while alive(t) and now(t) - guard < 12 do
        local u = melee_anims(t, G.dig, DIGUP)[1]
        if u then G.up = u.tick break end
        t.ticks(1)
    end

    return G
end
-- technique 18: the six ticks after it resurfaces are the window to switch the prayer; the first swing is shot as it lands
local function pray_window(t, G)
    if not G.up then return end
    local target = G.up + 3
    local r, d, info
    if now(t) < target then r, d, info = t.prayer.set_on_tick("protectfrommelee", true, target) end
    local issued = info and info.issued
    if r ~= "ok" then
        r, d = t.prayer.set("protectfrommelee", true)
        if r == "ok" and not string.find(tostring(d), "no press", 1, true) then issued = now(t) end
    end
    G.press = { result = tostring(r), tick = now(t), planned = target, in_force = info and info.in_force or (issued and issued + 1), issued = issued, detail = tostring(d):sub(1, 100) }
    -- the melee half out of the ground as the press lands (the window: resurface + 3)
    shoot(t, "dig" .. G.k .. "_up", "dig" .. G.k .. ".melee_resurfaced_prayer_pressed_in_the_window", 1100)
    if G.step_after then step_out(t, G) end
    if G.click_after then G.click_after(t, G) end
    aim(t, 900)
    local ar = t.npc.await_anim(msel(), ATTACK, 8)
    if ar == "ok" then shoot(t, "dig" .. G.k .. "_swing", "dig" .. G.k .. ".prayer_lit_as_its_first_swing_after_resurfacing", 900) end
end
-- P3: a held stand under Protect from Melee for n swings
local function held_stand(t, n, key)
    local S = { from = now(t) }
    D.ph[key] = S
    t.prayer.set("protectfrommelee", true)
    while alive(t) and #melee_anims(t, S.from, ATTACK) < n and now(t) - S.from < 50 do
        if not eat_if_low(t, 60) then pray_if_low(t) end
        t.ticks(1)
    end
    S.to = now(t)
end
-- flicks on the swing tick A: 'on time' = pressed on A-1 and off on A (in force on A); 'late' = pressed on A (in force A+1)
local function flick_series(t, count, late, shots, tag)
    local out, last, from = {}, -1, now(t)
    t.prayer.set("protectfrommelee", false)
    while #out < count and alive(t) and now(t) - from < 140 do
        if hp(t) < 75 then
            -- not enough hitpoints to take an unprayed 49: the prayer up and sharks before the next press
            t.prayer.set("protectfrommelee", true)
            local n = 0
            while alive(t) and hp(t) < 85 and n < 5 do t.player.inv_op("shark", 1) t.ticks(3) n = n + 1 end
            t.ticklog.mark(string.format("%s ate %d hp%d", tag, n, hp(t)))
            t.prayer.set("protectfrommelee", false)
            last = now(t) + 1
        end
        pray_if_low(t)
        local L = last_of(melee_anims(t, from - 8, ATTACK))
        local n = now(t)
        local A = L and (L.tick + 4 * math.ceil((n + 2 - L.tick) / 4)) or nil
        if A and A > last then
            last = A
            local press = late and (A + 1) or A
            local rec = { swing = A, flick = press, hp = hp(t) }
            local _, _, pts = t.prayer.points()
            t.ticklog.mark(string.format("%s A%d hp%d pp%s", tag, A, rec.hp, tostring(pts and pts.level)))
            local k = #out + 1
            if shots and shots[k] then
                local r1 = t.prayer.set_on_tick("protectfrommelee", true, press - 1)
                if late then
                    shoot(t, tag .. k, tag .. k .. ".press_on_the_swing_tick_is_late", 900)
                else
                    aim(t, 900)
                    local ar = t.npc.await_anim(msel(), ATTACK, 4)
                    if ar == "ok" then shoot(t, tag .. k, tag .. k .. ".prayer_lit_as_the_melee_swings", 900) end
                end
                local r2 = t.prayer.set_on_tick("protectfrommelee", false, press)
                if r2 ~= "ok" then t.prayer.set("protectfrommelee", false) end
                rec.result = tostring(r1)
            else
                rec.result = tostring((t.prayer.flick("protectfrommelee", press)))
            end
            out[#out + 1] = rec
        else
            t.ticks(1)
        end
    end
    t.ticks(5)
    local sw, hits = {}, {}
    for _, a in ipairs(melee_anims(t, from, ATTACK)) do sw[a.tick] = true end
    for _, h in ipairs(rows(t, "hit_player")) do if h.npc_type == MELEE and h.tick >= from then hits[h.tick] = h.damage end end
    for _, r in ipairs(out) do r.swung = sw[r.swing] == true r.damage = hits[r.swing] end
    return out, from
end

-- a tile beside a pillar whose (-3,-3) landing box the pillar covers, out of the melee's reach
local function box_spot(m, px, pz)
    local best, bd = nil, 1e9
    for _, p in ipairs(PILLARS) do
        local sx, sz = BASE_X + p.x, BASE_Z + p.z
        for _, q in ipairs({ { sx + 3, sz + 3 }, { sx + 3, sz + 1 }, { sx + 1, sz + 3 }, { sx + 3, sz + 2 }, { sx + 2, sz + 3 } }) do
            local d = cheb(q[1], q[2], px, pz)
            if foot_dist(q[1], q[2], m.x, m.z) >= 4 and d < bd then best, bd = { q[1], q[2], p.name }, d end
        end
    end
    return best
end
-- P4: out of reach 26 ticks after its last swing, Ice Barrage on it (technique 20), then wait on a box spot for the dig
local function barrage_dig(t, k)
    local L = last_of(melee_anims(t, D.begin, ATTACK))
    local B = { last_swing = L and L.tick or now(t), casts = {} }
    D.ph.barrage = B
    t.prayer.set("protectfrommelee", false)
    -- behind a pillar until 24 ticks after its last swing (the dig falls 40-60 after it)
    -- the freeze lasts 32 ticks: cast from 28 ticks after its last swing so it covers the whole 40-60 the dig can fall in
    B.hold = hold_out(t, function() return now(t) >= B.last_swing + 28 end, 45, { keep = 2, pillar = true })
    for attempt = 1, 7 do
        if now(t) >= B.last_swing + 52 or #melee_anims(t, B.last_swing + 1, DIGDOWN) > 0 then break end
        if gap_now(t) <= 2 then hold_out(t, function() return gap_now(t) >= 5 end, 8, { keep = 4 }) end
        aim(t, 1100)
        local c = { tick = now(t), gap = gap_now(t) }
        local r, d = t.player.cast("ice_barrage", MELEE_SYMBOL, 6, 2, msel())
        c.result, c.detail = tostring(r), tostring(d):sub(1, 120)
        shoot(t, "barrage" .. attempt, "p4.ice_barrage_cast_" .. attempt .. "_on_the_melee", math.min(2800, 1200 + 130 * gap_now(t)), 383, true)
        t.ticks(3)
        for _, h in ipairs(rows(t, "hit_npc", { type = MELEE })) do if h.tick >= c.tick and not c.hit then c.hit, c.damage = h.tick, h.damage end end
        for _, sa in ipairs(rows(t, "npc_spotanim", { type = MELEE })) do if sa.tick >= c.tick and not c.spot then c.spot = sa.spotanim end end
        B.casts[#B.casts + 1] = c
        if c.hit then B.hit_tick, B.held = c.hit, true break end
        t.ticks(2)
    end
    -- frozen: wait on a tile in the open whose first landing box a pillar covers; else back behind a pillar
    local m, raw = find_melee(t)
    if B.held and m and raw and raw.player_x then
        B.frozen_at = { m.x, m.z }
        B.spot = box_spot(m, raw.player_x, raw.player_z)
        if B.spot then t.player.walk_to(B.spot[1], B.spot[2], 12) end
        local _, raw2 = find_melee(t)
        if raw2 and raw2.player_x then B.arrived = { raw2.player_x, raw2.player_z, now(t) } end
    end
    local G = watch_dig(t, k, B.last_swing + 1, B.last_swing + 75 - now(t), { keep = 2, pillar = not B.held })
    B.dig = G.dig
    return G
end
-- technique 19: the attack click on a melee that has just surfaced on the player; the tile before and after
local function click_after_resurface(t, G)
    local C = { tick = now(t) }
    local _, raw = find_melee(t)
    C.before = raw and raw.player_x and { raw.player_x, raw.player_z } or nil
    aim(t, 900)
    local ar, ad = t.player.attack(MELEE_SYMBOL, 2, 3, msel({ quick = true }))
    t.check("technique.attack_click_on_a_melee_just_surfaced", ar == "ok" or ar == "timeout", "the press on the melee that surfaced on the player: " .. tostring(ar) .. " " .. tostring(ad):sub(1, 300))
    D.shots.click = "technique.attack_click_on_a_melee_just_surfaced (server tick " .. now(t) .. ")"
    local lv = string.match(tostring(ad), "%(level%-(%d+)%)")
    if lv then D.levels[#D.levels + 1] = tonumber(lv) end
    C.result = tostring(ar)
    G.click = C
end
-- P5: the kill with the bow under Protect from Melee; the death animation is shot as it plays
local function kill(t)
    local K = { from = now(t) }
    D.ph.kill = K
    t.prayer.set("protectfrommelee", true)
    local ar, ad = t.player.attack(MELEE_SYMBOL, 2, 4, msel({ quick = true }))
    K.attack = tostring(ar) .. " " .. tostring(ad):sub(1, 200)
    local lv = string.match(tostring(ad), "%(level%-(%d+)%)")
    if lv then D.levels[#D.levels + 1] = tonumber(lv) end
    local pressed, nhit = now(t), 0
    for _ = 1, 200 do
        if not alive(t) then break end
        local sr, st = t.npc.state(msel())
        if sr == "ok" and type(st) == "table" and st.anim_id == DEATH and not K.shot then
            shoot(t, "death", "kill.melee_in_its_death_animation_7599", 1100)
            K.shot = true
        end
        local dr = rows(t, "npc_death", { type = MELEE })
        if #dr > 0 then
            K.death = dr[1].tick
            if not K.shot then
                aim(t, 1100)
                local wr = t.npc.await_anim(msel(), DEATH, 3)
                if wr == "ok" or (sr == "ok") then shoot(t, "death", "kill.melee_in_its_death_animation_7599", 1100) K.shot = true end
            end
            break
        end
        local hn = #rows(t, "hit_npc", { type = MELEE })
        if hn > nhit then nhit = hn pressed = now(t) end
        if eat_if_low(t, 55) then
            t.ticks(1)
            t.player.attack(MELEE_SYMBOL, 2, 2, msel({ quick = true }))
            pressed = now(t)
        elseif now(t) - pressed > 8 then
            t.player.attack(MELEE_SYMBOL, 2, 2, msel({ quick = true }))
            pressed = now(t)
        end
        pray_if_low(t)
        t.ticks(1)
    end
    t.ticks(6)
    K.to = now(t)
end

local function static_reads(t)
    local S = { seq = {} }
    local _
    D.static = S
    S.r, _, S.rec = t.npc.record(MELEE_SYMBOL)
    S.r2, _, S.rec2 = t.npc.record(MELEE_SYMBOL, { need = "server" })
    S.r3, _, S.rec3 = t.npc.record("inferno_creature_melee_small", { need = "server" })
    for _, id in ipairs({ 7595, 7596, 7597, 7599, 7600, 7601 }) do
        local sr, _, len = t.seq.length(id)
        S.seq[id] = { r = sr, len = len }
    end
end

local function fight(t)
    D.digs = {}
    engage_early(t)
    -- P2: out of reach behind a pillar until the first dig
    t.prayer.set("protectfrommelee", false)
    -- P2: out of its reach, then to the far side of a pillar from it (techniques 4 and 6)
    hold_out(t, function() return gap_now(t) >= 3 end, 6, { keep = 3 })
    do
        local m, raw = find_melee(t)
        if m and raw and raw.player_x then
            local sp = pillar_spot(m, raw.player_x, raw.player_z)
            D.ph.p2spot = sp
            if sp then t.player.walk_to(sp[1], sp[2], 16) end
        end
        t.prayer.set("protectfrommelee", false)
    end
    local G1 = watch_dig(t, 1, D.begin, 75, { keep = 2, on_tick = function(m, raw, H)
        local sp = D.ph.p2spot
        H.spot = H.spot or sp
        if sp and not D.shots.pillar and raw.player_x == sp[1] and raw.player_z == sp[2] and foot_dist(raw.player_x, raw.player_z, m.x, m.z) >= 3 then
            local s = H.samples[#H.samples - 4]
            if s and s.mx == m.x and s.mz == m.z and s.px == raw.player_x and s.pz == raw.player_z then
                local lr, ld, seen = t.world.los("player", m)
                D.ph.p2los = tostring(lr) .. " seen " .. tostring(seen) .. ": " .. tostring(ld):sub(1, 120)
                shoot(t, "pillar", "p2.pillar_between_the_player_and_the_melee", 2000, 383, true)
                H.shot_tick = now(t)
            end
        end
    end })
    D.digs[1] = G1
    G1.step_after = true
    pray_window(t, G1)
    t.ticklog.mark("phase.stand1")
    held_stand(t, 5, "stand1")
    D.ph.on_time, D.ph.on_from = flick_series(t, 6, false, { [2] = true, [4] = true, [5] = true }, "flick_on_time")
    D.ph.late, D.ph.late_from = flick_series(t, 5, true, { [1] = true }, "flick_late")
    t.prayer.set("protectfrommelee", true)
    t.ticklog.mark("phase.recover hp" .. hp(t))
    for _ = 1, 6 do if hp(t) < 85 then t.player.inv_op("shark", 1) t.ticks(3) end end
    t.ticklog.mark("phase.stand2 hp" .. hp(t))
    D.ph.stand2_from = now(t)
    held_stand(t, 3, "stand2")
    t.ticklog.mark("phase.barrage hp" .. hp(t))
    local G2 = barrage_dig(t, 2)
    D.digs[2] = G2
    if G2.up then
        G2.click_after = click_after_resurface
        pray_window(t, G2)
    end
    t.ticklog.mark("phase.kill hp" .. hp(t))
    kill(t)
end

-- ===== after the fight: the ledger, computed from the tick log =====
local function at_or_before(list, tick)
    local best = nil
    for _, r in ipairs(list) do if r.tick <= tick then best = r else break end end
    return best
end
local function within(list, from, upto)
    local out = {}
    for _, r in ipairs(list) do if r.tick >= from and (upto == nil or r.tick <= upto) then out[#out + 1] = r end end
    return out
end
local function gaps_of(list)
    local g = {}
    for i = 2, #list do g[#g + 1] = list[i].tick - list[i - 1].tick end
    return g
end
-- a row list as a by-tick track (the latest row at or before each tick), so a lookup is one index
local function track(list, t0, t1)
    local out, cur, i = {}, nil, 1
    for tk = t0, t1 do
        while list[i] and list[i].tick <= tk do cur = list[i] i = i + 1 end
        out[tk] = cur
    end
    return out
end
local function collect(t)
    local L = {}
    L.anims = rows(t, "npc_anim", { type = MELEE })
    L.hits = {}
    for _, h in ipairs(rows(t, "hit_player")) do if h.npc_type == MELEE then L.hits[#L.hits + 1] = h end end
    L.ptile = rows(t, "player_tile")
    L.spawns = rows(t, "npc_spawn", { type = MELEE })
    L.ntile = {}
    -- an npc that never moved has no npc_tile row: seed the track from its npc_spawn row
    if L.spawns[1] then L.ntile[1] = { tick = L.spawns[1].tick, x = L.spawns[1].x, z = L.spawns[1].z } end
    for _, r in ipairs(rows(t, "npc_tile", { type = MELEE })) do L.ntile[#L.ntile + 1] = r end
    L.hitn = rows(t, "hit_npc", { type = MELEE })
    L.deaths = rows(t, "npc_death", { type = MELEE })
    L.frees = rows(t, "npc_free", { type = MELEE })
    L.proj = rows(t, "projectile")
    L.spot = rows(t, "npc_spotanim", { type = MELEE })
    L.swings = {}
    L.digs, L.ups = {}, {}
    for _, a in ipairs(L.anims) do
        if a.seq == ATTACK then L.swings[#L.swings + 1] = a end
        if a.seq == DIGDOWN then L.digs[#L.digs + 1] = a end
        if a.seq == DIGUP then L.ups[#L.ups + 1] = a end
    end
    L.spawn = L.spawns[1] and L.spawns[1].tick or D.begin
    L.t1 = now(t)
    L.pt = track(L.ptile, D.begin - 2, L.t1)
    L.nt = track(L.ntile, D.begin - 2, L.t1)
    return L
end
-- the player's gap to the footprint the npc scanned from on tick T: the player's tile of T-1, the melee's tile of T
local function reach(L, tick)
    local p = L.pt[tick - 1]
    local m = L.nt[tick]
    if not p or not m then return nil end
    return foot_dist(p.x, p.z, m.x, m.z), p, m
end
local function next_swing(L, after)
    for _, a in ipairs(L.swings) do if a.tick > after then return a end end
    return nil
end
local function last_swing_before(L, tick)
    local best = nil
    for _, a in ipairs(L.swings) do if a.tick < tick then best = a end end
    return best
end
local function hit_on(L, tick)
    for _, h in ipairs(L.hits) do if h.tick == tick then return h end end
    return nil
end
-- the casts of this fight: the melee's spotanims on the ticks of a cast are the spell's, not the melee's
local function cast_ticks()
    local c = {}
    for _, x in ipairs((D.ph.barrage or {}).casts or {}) do c[#c + 1] = x.tick end
    return c
end
local function emit_fight(t, L)
    t.ticks(1)
    local A = {}
    -- hitpoints: the pack row at the wave's begin, and the hit_npc rows on its slot summed to its npc_death
    local sum, death = 0, L.deaths[1] and L.deaths[1].tick or nil
    for _, h in ipairs(L.hitn) do if death == nil or h.tick <= death then sum = sum + h.damage end end
    spec_row(t, "hitpoints", D.hp0 == 75 and death ~= nil and sum >= 75, tostring(D.hp0), "the npc pack's hitpoints on the wave's begin tick; the " .. #L.hitn .. " hit_npc rows on it summed to " .. sum .. " up to its npc_death on tick " .. tostring(death) .. " (a killing splat is not clamped to the hitpoints left)", "75 hp", "A", "exact")
    local lu = uniq(D.levels)
    spec_row(t, "combat_level", #D.levels >= 2 and lu == "240", lu, #D.levels .. " attack presses: the menu row '(level-N)' the press read", "240 count", "A", "exact")
    spec_row(t, "size", #D.sizes >= 2 and uniq(D.sizes) == "4", uniq(D.sizes), #D.sizes .. " reads (t.npc.pack and t.npc.state size) of the melee at the wave's begin", "4 tiles", "A", "exact")
    local so = L.spawns[1] and (L.spawns[1].tick - D.begin) or nil
    spec_row(t, "spawn_tick", so == 0, tostring(so), "the melee's npc_spawn row (tick " .. tostring(L.spawns[1] and L.spawns[1].tick) .. ", tile " .. tostring(L.spawns[1] and (L.spawns[1].x .. "," .. L.spawns[1].z)) .. ") minus the wave's begin tick " .. tostring(D.begin) .. " (t.wave.enter's 'wave begun by server tick N'); one melee in wave 9", "0 ticks", "B", "exact")
    -- aggressive: swings before the player's first attack press
    local first_press = (D.digs[2] and D.digs[2].click and D.digs[2].click.tick) or (D.ph.kill and D.ph.kill.from) or 1e9
    local unprov = 0
    for _, a in ipairs(L.swings) do if a.tick < first_press then unprov = unprov + 1 end end
    spec_row(t, "aggressive", unprov >= 5, unprov >= 1 and "1" or "0", unprov .. " melee swings (npc_anim " .. ATTACK .. ") before the player's first attack press on tick " .. tostring(first_press) .. ", the first on tick " .. tostring(L.swings[1] and L.swings[1].tick) .. " (auto-retaliate off)", "1 count", "D", "exact")
    -- every melee animation, projectile and graphic of the fight
    local okseq = { [ATTACK] = true, [DEFEND] = true, [DEATH] = true, [DIGDOWN] = true, [DIGUP] = true }
    local odd = 0
    for _, a in ipairs(L.anims) do if not okseq[a.seq] then odd = odd + 1 end end
    local nproj, nown = 0, 0
    for _, r in ipairs(L.proj) do
        local p0, p1 = at_or_before(L.ptile, r.tick), at_or_before(L.ptile, r.tick - 1)
        if (p0 and r.src_x == p0.x and r.src_z == p0.z) or (p1 and r.src_x == p1.x and r.src_z == p1.z) then nown = nown + 1 else nproj = nproj + 1 end
    end
    local spell_spot, other_spot = 0, 0
    local ct = cast_ticks()
    for _, sa in ipairs(L.spot) do
        local mine = false
        for _, c in ipairs(ct) do if sa.tick >= c and sa.tick <= c + 6 then mine = true end end
        if mine then spell_spot = spell_spot + 1 else other_spot = other_spot + 1 end
    end
    spec_row(t, "melee_attacks_only", #L.anims > 20 and odd == 0 and nproj == 0, tostring(odd + nproj), #L.anims .. " melee npc_anim rows (" .. hist((function() local q = {} for _, a in ipairs(L.anims) do q[#q + 1] = a.seq end return q end)()) .. "): none outside the swing, defend, death and dig sequences; " .. nproj .. " projectile rows not fired from the player's tile (" .. nown .. " are the player's arrows)", "0 count", "B", "exact")
    spec_row(t, "projectile_or_graphic", nproj == 0 and other_spot == 0, tostring(nproj + other_spot), nproj .. " projectile rows not from the player's tile, " .. other_spot .. " npc_spotanim rows on the melee off the player's casts (" .. spell_spot .. " are the player's Ice Barrage on it)", "0 count", "D", "exact")
    t.ticks(1)
    -- the cadence: consecutive swings with no dig between them
    local gaps, segs = {}, {}
    local prev = nil
    for _, a in ipairs(L.anims) do
        if a.seq == DIGDOWN then prev = nil
        elseif a.seq == ATTACK then
            if prev then gaps[#gaps + 1] = a.tick - prev end
            prev = a.tick
        end
    end
    A.gaps = gaps
    -- the cadence proper: a gap counts when the player stood in its reach on every tick of it (it could have swung)
    local four, out = {}, {}
    prev = nil
    for _, a in ipairs(L.anims) do
        if a.seq == DIGDOWN then prev = nil
        elseif a.seq == ATTACK then
            if prev then
                local all = true
                for tk = prev + 1, a.tick do local d = reach(L, tk) if d == nil or d > 1 then all = false end end
                if all then four[#four + 1] = a.tick - prev else out[#out + 1] = a.tick - prev end
            end
            prev = a.tick
        end
    end
    spec_row(t, "attack_speed", #four >= 20 and uniq(four) == "4", uniq(four), hist(four) .. "; the gaps between consecutive melee swings (npc_anim " .. ATTACK .. ") with the player in its reach on every tick between (player_tile T-1 against npc_tile T); " .. #out .. " gaps spanning a step out of reach left out: " .. join(out), "4 ticks", "C", "exact")
    spec_row(t, "attack_gap_minimum", #gaps >= 20 and minof(gaps) == 4, tostring(minof(gaps)), "the smallest of " .. #gaps .. " gaps between consecutive swings of the fight (" .. hist(gaps) .. ")", "4 ticks", "B", "exact")
    local dl = {}
    for _, h in ipairs(L.hits) do
        local last = last_swing_before(L, h.tick + 1)
        dl[#dl + 1] = last and (h.tick - last.tick) or 99
    end
    t.ticks(1)
    spec_row(t, "hit_delay", #dl >= 20 and uniq(dl) == "0", uniq(dl), hist(dl) .. "; each melee hit_player row's tick minus the tick of the latest melee swing", "0 ticks", "D", "exact")
    local rd = {}
    for _, a in ipairs(L.swings) do local d = reach(L, a.tick) if d then rd[#rd + 1] = d end end
    spec_row(t, "attack_range", #rd >= 20 and maxof(rd) == 1, tostring(maxof(rd)), #rd .. " swings: the Chebyshev gap from the player's tile on the tick before (player_tile) to the 4x4 footprint (npc_tile, seeded from npc_spawn): " .. hist(rd), "1 tiles", "C", "exact")
    local sq = {}
    for _, h in ipairs(L.hits) do for _, a in ipairs(L.anims) do if a.tick == h.tick then sq[#sq + 1] = a.seq end end end
    spec_row(t, "attack_seq", #sq >= 20 and uniq(sq) == tostring(ATTACK), uniq(sq), #sq .. " melee npc_anim rows on a tick that carries a melee hit_player row", "7597 count", "B", "exact")
    return A
end
local RUNGS = { { -3, -3, "nw3" }, { 0, 0, "under" }, { -3, 0, "w3" }, { 0, -3, "n3" }, { -1, -1, "nw1" } }
local function box_blocked(bx, bz, others, fallen, dtick)
    for _, p in ipairs(PILLARS) do
        local sx, sz = BASE_X + p.x, BASE_Z + p.z
        local down = fallen and fallen[p.name] and fallen[p.name] <= dtick
        if not down and sx <= bx + 3 and sx + 2 >= bx and sz <= bz + 3 and sz + 2 >= bz then return "pillar " .. p.name end
    end
    for _, o in ipairs(others or {}) do
        if o.x <= bx + 3 and o.x >= bx and o.z <= bz + 3 and o.z >= bz then return "npc " .. o.slot end
    end
    return nil
end
local function emit_digs(t, L)
    t.ticks(1)
    local G = {}
    -- the other npcs' tiles (nibblers; the pillars are static boxes) for the landing ladder
    local otiles = {}
    for _, r in ipairs(rows(t, "npc_spawn")) do if r.type ~= MELEE then otiles[#otiles + 1] = { tick = r.tick, slot = r.slot, x = r.x, z = r.z } end end
    for _, r in ipairs(rows(t, "npc_tile")) do if r.type ~= MELEE then otiles[#otiles + 1] = { tick = r.tick, slot = r.slot, x = r.x, z = r.z } end end
    -- a pillar that fell (loc_set loc -1 on its tile) no longer covers a box
    local fallen = {}
    for _, r in ipairs(rows(t, "loc_set")) do
        if r.loc == -1 then
            for _, p in ipairs(PILLARS) do if r.x == BASE_X + p.x and r.z == BASE_Z + p.z then fallen[p.name] = r.tick end end
        end
    end
    G.fallen = fallen
    for i, dd in ipairs(L.digs) do
        local g = { d = dd.tick }
        for _, u in ipairs(L.ups) do if u.tick > dd.tick and not g.u then g.u = u.tick end end
        local ns = next_swing(L, g.u or dd.tick)
        g.next = ns and ns.tick or nil
        local p = at_or_before(L.ptile, dd.tick - 1)
        local m0 = at_or_before(L.ntile, dd.tick)
        for _, r in ipairs(L.ntile) do if r.tick > dd.tick and m0 and (r.x ~= m0.x or r.z ~= m0.z) and not g.land then g.land = r end end
        if p and g.land then
            g.off = { g.land.x - p.x, g.land.z - p.z }
            g.p = p
            local last = {}
            for _, o in ipairs(otiles) do if o.tick <= dd.tick then last[o.slot] = o end end
            local others = {}
            for _, o in pairs(last) do others[#others + 1] = o end
            g.why = {}
            for _, r in ipairs(RUNGS) do
                local b = box_blocked(p.x + r[1], p.z + r[2], others, fallen, dd.tick)
                if not b then g.expect = r break end
                g.why[#g.why + 1] = r[3] .. " blocked by " .. b
            end
            for _, r in ipairs(RUNGS) do if g.off[1] == r[1] and g.off[2] == r[2] then g.rung = r[3] end end
        end
        local last = last_swing_before(L, dd.tick)
        g.prev = last and last.tick or nil
        -- the gap to the footprint on each tick from its last swing (or its spawn) to the dig
        -- the first tick it could swing again (attack delay 4) to the dig: the gap it read on each (player T-1, melee T)
        local from = last and (last.tick + 4) or (L.spawn + 1)
        local mn = nil
        for tk = from, dd.tick - 1 do local d = reach(L, tk) if d and (mn == nil or d < mn) then mn = d end end
        g.min_gap, g.window = mn, from .. ".." .. (dd.tick - 1)
        g.dmg = 0
        for _, h in ipairs(L.hits) do if h.tick >= dd.tick and h.tick <= (g.u or dd.tick + 6) then g.dmg = g.dmg + 1 end end
        G[i] = g
    end
    return G
end
local function emit_dig_rows(t, L, G)
    t.ticks(1)
    local n = #G
    local g1, g2 = G[1] or {}, G[2] or {}
    -- the trigger: every dig fell with the player out of its reach since its last swing; in reach it swung and never dug
    local unreach, txt = 0, {}
    for _, g in ipairs(G) do
        if g.min_gap and g.min_gap >= 2 then unreach = unreach + 1 end
        txt[#txt + 1] = string.format("dig %d (npc_anim %d): gap to the footprint at least %s on ticks %s", g.d, DIGDOWN, tostring(g.min_gap), g.window)
    end
    local stand_from = g1.next or 0
    local stand_to = g2.prev or 0
    local dug_in_stand = 0
    for _, dd in ipairs(L.digs) do if dd.tick > stand_from and dd.tick < stand_to then dug_in_stand = dug_in_stand + 1 end end
    local nstand = #within(L.swings, stand_from, stand_to)
    local trig = n >= 2 and unreach == n and dug_in_stand == 0 and stand_to - stand_from > 60
    text_row(t, "dig_trigger", trig, trig and "unreachable" or "reachable", table.concat(txt, "; ") .. "; in its reach from tick " .. stand_from .. " to " .. stand_to .. " (" .. (stand_to - stand_from) .. " ticks, " .. nstand .. " swings, the dig clock's 40-60 passed) it never dug", "unreachable", "D")
    local fo = g1.d and (g1.d - L.spawn) or nil
    spec_row(t, "dig_first_tick", fo == 50, tostring(fo), "the first npc_anim " .. DIGDOWN .. " row (tick " .. tostring(g1.d) .. ") minus the melee's npc_spawn tick " .. L.spawn .. ", the player out of reach from tick " .. tostring(g1.window), "50 ticks", "C", "exact")
    -- the wave-start block: the swings before the first dig, as offsets from the spawn
    local pre = {}
    for _, a in ipairs(L.swings) do if g1.d and a.tick < g1.d then pre[#pre + 1] = a.tick - L.spawn end end
    local olast = maxof(pre)
    local post = nil
    for _, a in ipairs(L.swings) do if a.tick - L.spawn >= 30 and not post then post = a.tick - L.spawn end end
    local blk = (fo == 50 and olast ~= nil) and (olast + 1) or nil
    spec_row(t, "dig_timer_reset_block", blk == 30, tostring(blk), "the lower edge, measured: swings at spawn offsets " .. join(pre) .. " (npc_anim " .. ATTACK .. " rows) left the first dig on offset " .. tostring(fo) .. ", so a swing as late as offset " .. tostring(olast) .. " did not reset the clock (ticks 0-" .. tostring(olast) .. " blocked); the upper edge: the first swing at offset 30 or later, offset " .. tostring(post) .. ", and every one after it moved the next dig to 40-60 ticks after the last swing (dig_timer_reset_by_attack); one practice entry holds one wave start, so a swing on offset 30 itself is not in this run", "30 ticks", "D", "exact")
    local rs = (g2.d and g2.prev) and (g2.d - g2.prev) or nil
    local rb = rs ~= nil and rs >= 40 and rs <= 60 and g1.d ~= nil and g2.d - g1.d > 60
    spec_row(t, "dig_timer_reset_by_attack", rb, rb and "1" or "0", "the second dig fell on tick " .. tostring(g2.d) .. ", " .. tostring(rs) .. " ticks after the melee's last swing (tick " .. tostring(g2.prev) .. ") and " .. tostring(g2.d and g1.d and (g2.d - g1.d)) .. " after the first dig, past the 40-60 the first dig set: its swings from tick " .. tostring(g1.next) .. " reset the clock", "1 count", "C", "exact")
    local dn, pd, dl, nd = {}, {}, {}, 0
    for _, g in ipairs(G) do
        if g.next and g.d then dn[#dn + 1] = g.next - g.d end
        if g.next and g.u then pd[#pd + 1] = g.next - g.u end
        if g.land then dl[#dl + 1] = g.land.tick - g.d end
        nd = nd + g.dmg
    end
    spec_row(t, "dig_to_next_attack", #dn >= 2 and uniq(dn) == "12", join(dn), "ticks from each dig (npc_anim " .. DIGDOWN .. ") to the melee's next swing (npc_anim " .. ATTACK .. "), " .. #dn .. " digs (" .. join(G, function(g) return tostring(g.d) .. "->" .. tostring(g.next) end) .. ")", "12 ticks", "C", "+-0")
    spec_row(t, "post_dig_attack_delay", #pd >= 2 and uniq(pd) == "6", join(pd), "ticks from each resurface (npc_anim " .. DIGUP .. ") to the next swing, " .. #pd .. " digs (" .. join(G, function(g) return tostring(g.u) .. "->" .. tostring(g.next) end) .. ")", "6 ticks", "C", "exact")
    spec_row(t, "dig_landing_delay", #dl >= 2 and uniq(dl) == "3", join(dl), "ticks from each dig row to the melee's first npc_tile change after it, " .. #dl .. " digs", "3 ticks", "B", "exact")
    spec_row(t, "dig_no_damage_during_anim", n >= 2 and nd == 0, tostring(nd), "melee hit_player rows from each dig to its resurface (" .. join(G, function(g) return tostring(g.d) .. ".." .. tostring(g.u) end) .. ")", "0 count", "A", "exact")
    -- the landing: the npc_tile row after the dig against the player's tile on the tick before the dig
    local match, onladder, parts = 0, 0, {}
    for _, g in ipairs(G) do
        if g.off then
            if g.rung then onladder = onladder + 1 end
            if g.expect and g.off[1] == g.expect[1] and g.off[2] == g.expect[2] then match = match + 1 end
            parts[#parts + 1] = string.format("dig %d: player %d,%d on tick %d, melee %d,%d on tick %d = offset (%d,%d) %s, the ladder's first free rung %s%s", g.d, g.p.x, g.p.z, g.p.tick, g.land.x, g.land.z, g.land.tick, g.off[1], g.off[2], tostring(g.rung), g.expect and g.expect[3] or "?", (#g.why > 0) and (" (" .. table.concat(g.why, ", ") .. ")") or "")
        end
    end
    local lok = n >= 2 and match == n
    text_row(t, "dig_landing", lok, lok and "ladder_nw3_under_w3_n3_nw1" or "off_ladder", table.concat(parts, "; ") .. "; " .. match .. " of " .. n .. " landings on the first rung whose 4x4 no pillar or npc covers", "ladder_nw3_under_w3_n3_nw1", "D")
    local bok = n >= 2 and onladder == n
    text_row(t, "dig_landing_blert", bok, bok and "ladder_observed" or "off_ladder", onladder .. " of " .. n .. " landing offsets from the player's tile on the tick before the dig are rungs of the observed ladder: " .. join(G, function(g) return g.off and ("(" .. g.off[1] .. "," .. g.off[2] .. ") " .. tostring(g.rung)) or "?" end), "ladder_observed", "B")
    local dd, uu = {}, {}
    for _, a in ipairs(L.digs) do dd[#dd + 1] = a.seq end
    for _, a in ipairs(L.ups) do uu[#uu + 1] = a.seq end
    spec_row(t, "dig_down_seq", #dd >= 2 and uniq(dd) == "7600", uniq(dd), #dd .. " npc_anim rows that start a dig: the melee's row 6 ticks before each resurface, its tile changed 3 ticks after it", "7600 count", "B", "exact")
    spec_row(t, "dig_up_seq", #uu >= 2 and uniq(uu) == "7601", uniq(uu), #uu .. " npc_anim rows 6 ticks after a dig row; approximation, M34", "7601 count", "E", "approx")
    return { stand_from = stand_from, stand_to = stand_to, nstand = nstand, dug_in_stand = dug_in_stand }
end
local function emit_prayer(t, L, G)
    t.ticks(1)
    local P = {}
    -- the swings the prayer was in force on: P1, the stands, the on-time flicks, each first swing after a resurface, the kill
    local prot, pt = {}, {}
    local function add(from, upto)
        for _, a in ipairs(within(L.swings, from, upto)) do
            local h = hit_on(L, a.tick)
            if h and not pt[a.tick] then pt[a.tick] = true prot[#prot + 1] = h.damage end
        end
    end
    local p1 = D.ph.p1 or {}
    add(D.begin, p1.to or D.begin)
    for _, k in ipairs({ "stand1", "stand2" }) do local s = D.ph[k] if s then add(s.from + 1, s.to) end end
    for _, r in ipairs(D.ph.on_time or {}) do if r.swung and r.damage ~= nil and not pt[r.swing] then pt[r.swing] = true prot[#prot + 1] = r.damage end end
    for _, g in ipairs(G) do if g.next then add(g.next, g.next) end end
    if D.ph.kill and L.deaths[1] then add(D.ph.kill.from + 2, L.deaths[1].tick) end
    P.prot = prot
    spec_row(t, "damage_when_protected", #prot >= 20 and maxof(prot) == 0, tostring(maxof(prot)), #prot .. " melee hit_player rows on swings with Protect from Melee in force (the wave-start stand, the held stands, the on-time flicks, the first swing after each resurface, the kill): " .. hist(prot), "0 hp", "D", "exact")
    local on_ok, on_n, late_pos, late_n = 0, 0, 0, 0
    for _, r in ipairs(D.ph.on_time or {}) do if r.swung then on_n = on_n + 1 if r.damage == 0 then on_ok = on_ok + 1 end end end
    local ld = {}
    for _, r in ipairs(D.ph.late or {}) do if r.swung and r.damage ~= nil then late_n = late_n + 1 ld[#ld + 1] = r.damage if r.damage > 0 then late_pos = late_pos + 1 end end end
    P.on_ok, P.on_n, P.late_pos, P.late_n, P.ld = on_ok, on_n, late_pos, late_n, ld
    local rok = on_n >= 5 and on_ok == on_n and late_pos >= 3
    spec_row(t, "prayer_read_tick", rok, rok and "0" or "?", on_ok .. " of " .. on_n .. " swings with the prayer pressed on A-1 and off on A (in force on the swing tick only) took 0 (swing ticks " .. join(D.ph.on_time or {}, function(r) return tostring(r.swing) end) .. "); " .. late_pos .. " of " .. late_n .. " pressed on A (in force A+1) took damage " .. join(ld) .. " (swing ticks " .. join(D.ph.late or {}, function(r) return tostring(r.swing) end) .. "): the hit reads the prayer on the swing tick", "0 ticks", "D", "exact")
    -- the hit-reaction and the death: the melee's animation on the tick a non-lethal hit_npc lands, and after its npc_death
    local defend, dseq = {}, {}
    local death = L.deaths[1] and L.deaths[1].tick or nil
    for _, h in ipairs(L.hitn) do
        if death == nil or h.tick < death then
            for _, a in ipairs(L.anims) do if a.tick >= h.tick - 3 and a.tick <= h.tick and a.seq ~= ATTACK and a.seq ~= DIGDOWN and a.seq ~= DIGUP then defend[#defend + 1] = a.seq end end
        end
    end
    for _, a in ipairs(L.anims) do if death and a.tick > death and a.tick <= death + 2 then dseq[#dseq + 1] = a.seq end end
    spec_row(t, "defend_seq", #defend >= 2 and uniq(defend) == "7598", uniq(defend), #defend .. " npc_anim rows of the melee (not a swing or a dig) from 3 ticks before a non-lethal hit_npc row on it to the hit's tick (the reaction is sent as the arrow leaves, with a client delay, and again on the hit); approximation, M34", "7598 count", "E", "approx")
    spec_row(t, "death_seq", #dseq >= 1 and uniq(dseq) == "7599", uniq(dseq), "the npc_anim rows on the 2 ticks after its npc_death (tick " .. tostring(death) .. "), shot " .. shotname("death") .. "; approximation, M34", "7599 count", "E", "approx")
    return P
end
local function emit_static(t, L, P)
    t.ticks(1)
    local S = D.static or {}
    local sv = (S.rec2 and S.rec2.server) or {}
    local cl = (S.rec and S.rec.client) or {}
    local b = sv.bonus or {}
    local src = "t.npc.record(inferno_creature_melee) server half (the content block its combat rolls with)"
    spec_row(t, "attack_level", S.r2 == "ok" and sv.attack == 210, tostring(sv.attack), src, "210 count", "A", "exact")
    spec_row(t, "strength_level", sv.strength == 290, tostring(sv.strength), src, "290 count", "A", "exact")
    spec_row(t, "defence_level", sv.defence == 120, tostring(sv.defence), src, "120 count", "A", "exact")
    local rm = tostring(sv.ranged) .. "," .. tostring(sv.magic)
    spec_row(t, "ranged_magic_levels", rm == "220,120", rm, src .. ": ranged then magic", "220,120 count", "A", "exact")
    local bn = table.concat({ tostring(b.strengthbonus), tostring(b.stabdefence), tostring(b.slashdefence), tostring(b.crushdefence), tostring(b.magicdefence) }, ",")
    spec_row(t, "strength_bonus_and_defence_bonuses", bn == "40,65,65,65,30", bn, src .. ": strength bonus then stab, slash, crush, magic defence", "40,65,65,65,30 count", "A", "exact")
    local cls = #P.prot >= 20 and maxof(P.prot) == 0 and P.late_pos >= 3
    spec_row(t, "attack_style_slash", cls and S.r2 == "ok", cls and "1" or "0", "its one attack is melee: " .. #P.prot .. " hits soaked to 0 by Protect from Melee and " .. P.late_pos .. " unprayed hits landed; the record's damagetype " .. tostring(sv.damagetype) .. " names no style, and its stab, slash and crush attack bonuses read " .. tostring(b.stabattack) .. "," .. tostring(b.slashattack) .. "," .. tostring(b.crushattack) .. "; the slash is the content's own ~inferno_hit_player(0, ^slash_style, 49) (inferno_ai.rs2 [ai_opplayer2,inferno_creature_melee])", "1 count", "C", "exact")
    local function cyc(id) local q = S.seq and S.seq[id]; return q and q.r == "ok" and q.len and q.len.cycles or nil end
    spec_row(t, "attack_anim_length", cyc(7597) == 60, tostring(cyc(7597)), "t.seq.length(7597) cycles as the client steps them", "60 cycles", "A", "exact")
    spec_row(t, "dig_down_anim_length", cyc(7600) == 112, tostring(cyc(7600)), "t.seq.length(7600) cycles", "112 cycles", "A", "exact")
    spec_row(t, "dig_up_anim_length", cyc(7601) == 105, tostring(cyc(7601)), "t.seq.length(7601) cycles", "105 cycles", "A", "exact")
    spec_row(t, "death_anim_length", cyc(7599) == 139, tostring(cyc(7599)), "t.seq.length(7599) cycles; its npc_death to npc_free gap in the log is " .. tostring(L.deaths[1] and L.frees[1] and (L.frees[1].tick - L.deaths[1].tick)) .. " ticks", "139 cycles", "A", "exact")
    spec_row(t, "ready_seq", cl.readyanim == 7595, tostring(cl.readyanim), "rec.client.readyanim (" .. tostring(cl.readyanim_name) .. ")", "7595 count", "A", "exact")
    spec_row(t, "walk_seq", cl.walkanim == 7596, tostring(cl.walkanim), "rec.client.walkanim (" .. tostring(cl.walkanim_name) .. ")", "7596 count", "A", "exact")
    local m1 = cl.models and cl.models[1]
    spec_row(t, "model", m1 == 33010, tostring(m1), "rec.client.models[1], " .. tostring(cl.models and #cl.models) .. " model(s)", "33010 count", "A", "exact")
    local sm = S.rec3 and S.rec3.server or {}
    local types = {}
    for _, r in ipairs(rows(t, "npc_spawn")) do if r.type == MELEE or r.type == 12594 then types[#types + 1] = r.type end end
    spec_row(t, "small_record", S.r3 == "ok" and sm.id == 12594 and #types >= 1 and uniq(types) == tostring(MELEE), tostring(sm.id), "record inferno_creature_melee_small reads id " .. tostring(sm.id) .. " size " .. tostring(sm.size) .. "; the wave's melee npc_spawn rows are type " .. uniq(types) .. ", never 12594", "12594 count", "A", "exact")
    local snd = tostring(sv.attack_sound) .. "," .. tostring(sv.defend_sound) .. "," .. tostring(sv.death_sound)
    spec_row(t, "sounds", snd == "608,610,609", snd, "attack, defend, death sound ids of the server block (" .. tostring(sv.attack_sound_name) .. ", " .. tostring(sv.defend_sound_name) .. ", " .. tostring(sv.death_sound_name) .. "), the lizard-cleric family's; approximation, M35", "? count", "E", "approx")
end
local function emit_techniques(t, L, G, P, R)
    t.ticks(1)
    local G1, G2 = D.digs[1] or {}, D.digs[2] or {}
    local g1, g2 = G[1] or {}, G[2] or {}
    -- 4 / 6: a pillar between: the player on its spot, the melee's tile unchanged, no swing until the dig
    local H = G1.hold or {}
    local on, still, mn, first = 0, 0, nil, nil
    for _, s in ipairs(H.samples or {}) do
        if H.spot and s.px == H.spot[1] and s.pz == H.spot[2] and (g1.d == nil or s.tick < g1.d) then
            on = on + 1
            first = first or s
            if s.mx == first.mx and s.mz == first.mz then still = still + 1 end
            if mn == nil or s.fd < mn then mn = s.fd end
        end
    end
    local sw_on = first and #within(L.swings, first.tick, g1.d or first.tick) or -1
    t.expect("technique.pillar_safespot", (on >= 8 and still == on and sw_on == 0 and (mn or 0) >= 2) and "ok" or "fail", string.format("the player stood on %s (three tiles off the %s pillar's far side) %d ticks from tick %s to the dig on %s: the melee's tile stayed %s on %d of %d pack reads, gap at least %s, %d swings; line of sight from the player %s; shot %s",
        H.spot and (H.spot[1] .. "," .. H.spot[2]) or "?", H.spot and H.spot[3] or "?", on, first and tostring(first.tick) or "?", tostring(g1.d), first and (first.mx .. "," .. first.mz) or "?", still, on, tostring(mn), sw_on, tostring(D.ph.p2los), shotname("pillar")))
    -- 18: the six ticks after it resurfaces are for the prayer
    local pr = {}
    local pok = 0
    for i, g in ipairs(G) do
        local GG = D.digs[i] or {}
        local h = g.next and hit_on(L, g.next)
        local press = GG.press
        local good = press and press.result == "ok" and press.issued ~= nil and g.u and g.next and press.tick <= g.next - 2 and h and h.damage == 0
        if good then pok = pok + 1 end
        pr[#pr + 1] = string.format("dig %s: resurfaced %s, Protect from Melee pressed on tick %s (planned %s, %s, issued %s, in force %s), first swing %s took %s", tostring(g.d), tostring(g.u), press and tostring(press.tick) or "-", press and tostring(press.planned) or "-", press and press.result or "-", press and tostring(press.issued) or "-", press and tostring(press.in_force) or "-", tostring(g.next), h and tostring(h.damage) or "-")
    end
    t.expect("technique.pray_in_the_resurface_window", (pok >= 2 and pok == #G) and "ok" or "fail", table.concat(pr, "; ") .. "; shots " .. shotname("dig1_up") .. ", " .. shotname("dig1_swing") .. ", " .. shotname("dig2_swing"))
    -- 18, the other half: in its reach the clock resets on every swing, so it never digs
    t.expect("technique.engage_so_it_never_digs", (R.stand_to - R.stand_from > 60 and R.dug_in_stand == 0 and R.nstand >= 15) and "ok" or "fail", string.format("from tick %d to %d (%d ticks) the player stood in its reach: %d swings (npc_anim %d), %d digs, while the first dig's clock (40-60) ran out at %s; shot %s", R.stand_from, R.stand_to, R.stand_to - R.stand_from, R.nstand, ATTACK, R.dug_in_stand, tostring(g1.d and (g1.d + 60)), shotname("flick_on_time2")))
    -- 19: the attack click on a melee that surfaced on the player: where the click took the player
    local C = G2.click or {}
    local pb = C.tick and at_or_before(L.ptile, C.tick) or nil
    local pa = C.tick and at_or_before(L.ptile, C.tick + 3) or nil
    local mb = C.tick and at_or_before(L.ntile, C.tick + 3) or nil
    local ga = (pa and mb) and foot_dist(pa.x, pa.z, mb.x, mb.z) or nil
    t.expect("technique.do_not_trust_the_footprint", (C.result == "ok" or C.result == "timeout") and pb ~= nil and pa ~= nil and "ok" or "fail", string.format("the melee surfaced on tick %s with the player under its footprint (landing (%s)); the attack click on tick %s (%s) took the player from %s to %s (player_tile rows), gap %s to its footprint %s: %s; shot %s",
        tostring(g2.u), g2.off and (g2.off[1] .. "," .. g2.off[2]) or "?", tostring(C.tick), tostring(C.result), pb and (pb.x .. "," .. pb.z) or "?", pa and (pa.x .. "," .. pa.z) or "?", tostring(ga), mb and (mb.x .. "," .. mb.z) or "?", (ga == 1) and "inside its reach, the vulnerable spot" or "out of its reach", shotname("click")))
    -- 20: Ice Barrage holds it, and the dig still comes
    local B = D.ph.barrage or {}
    local casts = join(B.casts or {}, function(c) return tostring(c.tick) .. ":" .. (c.hit and ("hit " .. tostring(c.damage)) or ("splash " .. tostring(c.spot))) end)
    local held, moved = B.held, 0
    if B.hit_tick and g2.d then for _, r in ipairs(L.ntile) do if r.tick > B.hit_tick and r.tick < g2.d then moved = moved + 1 end end end
    local fok = held and g2.d ~= nil and moved == 0 and g2.d - B.hit_tick <= 32
    t.expect("technique.barrage_holds_it_and_it_still_digs", fok and "ok" or "fail", string.format("Ice Barrage casts on the melee (tick:result) %s; frozen from tick %s at %s, the player waited at %s; npc_tile rows between the freeze and the dig on %s: %d; shots %s, %s", casts, tostring(B.hit_tick), B.frozen_at and (B.frozen_at[1] .. "," .. B.frozen_at[2]) or "-", B.arrived and (B.arrived[1] .. "," .. B.arrived[2]) or "-", tostring(g2.d), moved, shotname("barrage1"), shotname("dig2_down")))
    -- the prayer on the swing tick
    t.expect("technique.prayer_up_on_the_swing_tick", (P.on_n >= 5 and P.on_ok == P.on_n and P.late_pos >= 3) and "ok" or "fail", string.format("%d of %d flicks pressed on A-1 (in force on swing tick A, off on A) took 0; %d of %d presses on A (in force A+1) took %s; shots %s, %s, %s", P.on_ok, P.on_n, P.late_pos, P.late_n, join(P.ld), shotname("flick_on_time2"), shotname("flick_on_time4"), shotname("flick_late1")))
end

return {
    id = "inferno_melee",
    fixture = "fresh_lumbridge.ini",
    max_frames = 24000,
    setup = {
        "::clearinv", "::setlevel attack 99", "::setlevel strength 99", "::setlevel ranged 99", "::setlevel magic 99",
        "::setlevel hitpoints 99", "::setlevel defence 70", "::setlevel prayer 99", "::setlevel agility 99",
        "::give twisted_bow", "::give rune_arrow 1500", "::give 4doseprayerrestore 3", "::give shark 16",
        "::give ancestral_hat", "::give ancestral_robe_top", "::give ancestral_robe_bottom", "::give occult_necklace",
        "::give bloodrune 40", "::give deathrune 70", "::give waterrune 120",
        "::setvar varp172_option_nodef 1", "::setvar varb4070_spellbook 1",
    },
    run = function(t)
        t.ticklog.start()
        t.check("spec.scope", true, "mode=normal party=1")
        local er = t.player.equip("twisted_bow")
        local ar = t.player.equip("rune_arrow")
        local gear = {}
        for _, g in ipairs({ "ancestral_hat", "ancestral_robe_top", "ancestral_robe_bottom", "occult_necklace" }) do gear[#gear + 1] = tostring((t.player.equip(g))) end
        D.gear = table.concat(gear, ",")
        local lv = {}
        for _, k in ipairs({ "magic", "ranged", "hitpoints", "defence", "prayer" }) do local _, a = t.skill.read(k) lv[#lv + 1] = k .. " " .. tostring(type(a) == "table" and a.level) .. "/" .. tostring(type(a) == "table" and a.base_level) end
        D.gear = D.gear .. "; " .. table.concat(lv, ", ")
        local _, mark_text = t.ticklog.mark("run.begin")
        D.since = tonumber(string.match(tostring(mark_text), "serial (%d+)"))
        run_on(t)
        note(t, "setup.equipped", string.format("bow %s arrows %s magic gear %s; tick log from serial %s", tostring(er), tostring(ar), D.gear, tostring(D.since)))
        if not enter(t) then t.blocked("t.wave.enter inferno 9 did not settle: " .. tostring(D.enter_detail):sub(1, 200)) return end
        fight(t)
        if not alive(t) then return end
        static_reads(t)
        local L = collect(t)
        local G = emit_digs(t, L)
        note(t, "fight.summary", string.format("wave begun %s, melee spawned %s on %s; swings %d, digs %s, resurfaces %s, death %s; casts %s; hitpoints at the end %d; P1 %s",
            tostring(D.begin), tostring(L.spawn), L.spawns[1] and (L.spawns[1].x .. "," .. L.spawns[1].z) or "?", #L.swings, join(L.digs, function(a) return tostring(a.tick) end), join(L.ups, function(a) return tostring(a.tick) end), tostring(L.deaths[1] and L.deaths[1].tick), join((D.ph.barrage or {}).casts or {}, function(c) return tostring(c.tick) .. ":" .. tostring(c.hit ~= nil) end), hp(t), tostring((D.ph.p1 or {}).pause) .. "; nibbler press " .. tostring((D.ph.p1 or {}).nibbler) .. "; stepped out from under " .. tostring((D.ph.p1 or {}).stepped_out)))
        emit_fight(t, L)
        local R = emit_dig_rows(t, L, G)
        local P = emit_prayer(t, L, G)
        emit_static(t, L, P)
        emit_techniques(t, L, G, P, R)
    end,
}
