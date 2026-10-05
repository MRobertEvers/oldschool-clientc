-- inferno_bat: Jal-MejRah (spec docs/minigames/inferno/encounters/bat.tsv). Practice entries of wave 1 (one bat, three nibblers).
-- The bat is stood in front of unprayed (hit sizes, drain, run energy), then with Protect from Missiles up, then flicked
-- (prayer up on the swing tick only), then killed with a bow; a pillar safespot gives the line-of-sight rows.
-- Setup = bring-alongs only (levels, a bow, food, restores, auto-retaliate off).
local BAT, BAT_SYMBOL = 7692, "inferno_creature_harpie"
local ATTACK, DEFEND, DEATH, PROJ = 7578, 7579, 7580, 1382
local PILLAR_NPC = 7709
local PILLARS = { w = { 17, 37 }, s = { 27, 23 }, e = { 34, 39 } }
local KEYS = { "w", "s", "e" }
local D = { since = nil, pos = {}, energy = {}, drained = {}, levels = {} }

local function now(t) local _, k = t.tick() return k or -1 end
local function note(t, label, text) t.check(label, true, text) end
local function alive(t) return t.player.alive() == "ok" end
local function hp(t) local _, a = t.skill.read("hitpoints") if type(a) == "table" then return a.level or -1 end return -1 end
local function rows(t, kind, opts)
    opts = opts or {}
    opts.kind = kind
    if opts.since == nil then opts.since = D.since end
    local r, list = t.ticklog.rows(opts)
    t.ticks(1)
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
    local seen, order, n = {}, {}, {}
    for _, v in ipairs(list) do
        if seen[v] == nil then seen[v] = 0 order[#order + 1] = v end
        seen[v] = seen[v] + 1
    end
    table.sort(order)
    local parts = {}
    for _, v in ipairs(order) do parts[#parts + 1] = tostring(v) end
    return table.concat(parts, ","), seen
end
-- Chebyshev distance from a tile to a 2x2 footprint whose south-west tile is (px, pz)
local function foot_dist(x, z, px, pz)
    local dx = math.max(px - x, 0, x - (px + 1))
    local dz = math.max(pz - z, 0, z - (pz + 1))
    return math.max(dx, dz)
end
local function spec_row(t, id, ok, measured, extra, specv, grade, tol)
    if #id < 38 then t.ticks(1) end
    local detail = "measured " .. measured .. ((extra and extra ~= "") and (", " .. extra) or "") .. " (spec " .. specv .. ", grade " .. grade .. ", tol " .. tol .. ")"
    t.check("spec.bat." .. id, ok, detail)
end
local function energy(t)
    local r, text = t.ui.text("orbs:runenergy_text")
    local n = tonumber(string.match(tostring(text), "(%d+)"))
    return n
end
local function eat_if_low(t, below)
    local h = hp(t)
    if h > 0 and h < (below or 50) then t.player.inv_op("shark", 1) return true end
    return false
end
local function pray_if_low(t)
    local _, _, pts = t.prayer.points()
    if pts and pts.level and pts.level < 30 then t.player.drink("prayer_potion") end
end
local function arena_base(t)
    local _, _, pack = t.npc.pack(40)
    local found = {}
    for _, r in ipairs(pack or {}) do if r.type == PILLAR_NPC then found[#found + 1] = r end end
    if #found ~= 3 then return nil end
    table.sort(found, function(a, b) return a.x < b.x end)
    return found[1].x - PILLARS.w[1], found[1].z - PILLARS.w[2], found
end
local function find_bat(t)
    local _, _, pack, raw = t.npc.pack(40)
    for _, p in ipairs(pack or {}) do if p.type == BAT then return p, raw, pack end end
    return nil, raw, pack
end

local STATS = { "attack", "strength", "defence", "ranged", "magic" }
-- one reading per server tick while the player stands: the bat's tile and sight, run energy, the five combat stats, the bat's drawn anim
local function sample(t, tag)
    local tk = now(t)
    local bat, raw = find_bat(t)
    local s = { tick = tk, tag = tag, energy = energy(t) }
    if bat then s.x, s.z, s.sees, s.hp, s.size = bat.x, bat.z, bat.sees_player, bat.hitpoints, bat.size end
    if raw then s.px, s.pz = raw.player_x, raw.player_z end
    local lv = {}
    for _, n in ipairs(STATS) do
        local _, a = t.skill.read(n)
        lv[#lv + 1] = (type(a) == "table") and ((a.level or -1) .. "/" .. (a.base_level or -1)) or "?"
    end
    s.stats = table.concat(lv, " ")
    local sr, st = t.npc.state(BAT_SYMBOL)
    if sr == "ok" and type(st) == "table" then s.anim = st.anim_id end
    D.pos[#D.pos + 1] = s
    return s
end
-- stand and read until the stop function says so, a tick budget runs out or the player dies
local function stand(t, tag, budget, stop)
    local start = now(t)
    while now(t) - start < budget and alive(t) do
        sample(t, tag)
        if eat_if_low(t, 55) then t.ticks(1) end
        pray_if_low(t)
        if stop and stop() then break end
        t.ticks(1)
    end
    return now(t) - start
end
-- the bat's rows since the log began
local function bat_hits(t, from, upto)
    local out = {}
    for _, h in ipairs(rows(t, "hit_player")) do
        if h.npc_type == BAT and h.tick >= from and (upto == nil or h.tick < upto) then out[#out + 1] = h end
    end
    return out
end
local function bat_swings(t, from, upto)
    local out = {}
    for _, a in ipairs(rows(t, "npc_anim", { type = BAT })) do
        if a.seq == ATTACK and a.tick >= from and (upto == nil or a.tick < upto) then out[#out + 1] = a end
    end
    return out
end
local function gaps_of(list)
    local g = {}
    for i = 2, #list do g[#g + 1] = list[i].tick - list[i - 1].tick end
    return g
end
local function enter_wave(t, tag)
    local r, d = t.wave.enter("inferno", 1, { restart = true })
    t.check("enter." .. tag, r == "ok", tostring(d))
    D.begin = D.begin or {}
    D.begin[tag] = tonumber(string.match(tostring(d), "wave begun by server tick (%d+)"))
    if r ~= "ok" or not alive(t) then return false end
    t.ticklog.mark("enter." .. tag)
    return true
end
-- flicks: prayer ON the tick before the swing tick and OFF on it (late = false), or one tick late (late = true). The hit of the
-- swing is read from hit_player rows within three ticks of the swing.
local function flick_series(t, count, late)
    local out = {}
    local last = -1
    local guard = now(t)
    while #out < count and alive(t) and now(t) - guard < 160 do
        eat_if_low(t, 60)
        local sw = bat_swings(t, D.p1_from)
        local L = sw[#sw] and sw[#sw].tick or -1
        if L > last and now(t) <= L + 1 then
            last = L
            local A = L + 3 + (late and 1 or 0)
            local fr, fd, info = t.prayer.flick("protectfrommissiles", A)
            out[#out + 1] = { swing = L + 3, flick = A, result = fr, info = info, hp = hp(t) }
            eat_if_low(t, 60)
        else
            t.ticks(1)
        end
    end
    t.ticks(5)
    local hits = bat_hits(t, D.p1_from)
    local sw = bat_swings(t, D.p1_from)
    local swing_set = {}
    for _, a in ipairs(sw) do swing_set[a.tick] = true end
    for _, r in ipairs(out) do
        r.swung = swing_set[r.swing] == true
        r.damage = nil
        for _, h in ipairs(hits) do
            if h.tick >= r.swing and h.tick <= r.swing + 3 and r.damage == nil then r.damage = h.damage end
        end
    end
    return out
end

local function pillar_state(t)
    local _, _, s = t.wave.state("inferno")
    return s
end
local function bat_safespot_attempt(t, attempt)
    t.exec("enter.safespot" .. attempt, t.wave.enter, "inferno", 1, { restart = true })
    t.prayer.set("protectfrommissiles", true)
    local bx, bz = arena_base(t)
    if bx == nil then return false end
    local _, _, pack = t.npc.pack(40)
    local bat = nil
    for _, p in ipairs(pack or {}) do if p.type == BAT then bat = p end end
    if bat == nil then note(t, "safespot.bat" .. attempt, "no bat in wave 1") return false end
    local best, bestd, tried = nil, 1e9, {}
    local function sgn(v) if v > 0 then return 1 elseif v < 0 then return -1 end return 0 end
    local ps = pillar_state(t)
    local ncx, ncz = bat.x, bat.z
    for _, k in ipairs(KEYS) do
        local cx, cz = bx + PILLARS[k][1] + 1, bz + PILLARS[k][2] + 1
        local dx, dz = cx - ncx, cz - ncz
        local cands = {}
        if math.abs(dx) >= math.abs(dz) then cands[1] = { cx + 2 * sgn(dx), cz } cands[2] = { cx, cz + 2 * sgn(dz) }
        else cands[1] = { cx, cz + 2 * sgn(dz) } cands[2] = { cx + 2 * sgn(dx), cz } end
        for _, h in ipairs(cands) do
            if h[1] ~= cx or h[2] ~= cz then
                local _, _, seen = t.world.los({ x = h[1], z = h[2], level = 0 }, bat)
                tried[#tried + 1] = string.format("%s:%d,%d=%s", k, h[1] - bx, h[2] - bz, tostring(seen))
                local off = math.min(math.abs(dx), math.abs(dz))
                local d = off * 100 + math.max(math.abs(h[1] - bx - 30), math.abs(h[2] - bz - 32))
                if seen == false and d < bestd then best, bestd = { k, h[1], h[2] }, d end
            end
        end
    end
    if best == nil then note(t, "safespot.pick" .. attempt, "no ring tile hides the player from the bat") return false end
    local wr = t.player.walk_to(best[2], best[3], 40)
    local s = pillar_state(t)
    note(t, "safespot.pick", string.format("pillar %s tile %d,%d (local %d,%d), walk %s, at %s; bat was at %d,%d", best[1], best[2], best[3], best[2] - bx, best[3] - bz, tostring(wr), tostring(s and s.tile_text), bat.x, bat.z))
    local mark = now(t)
    t.ticklog.mark("safespot.hold")
    local seen_n, samples = 0, 0
    while now(t) - mark < 24 and alive(t) do
        local _, _, pk = t.npc.pack(40)
        for _, p in ipairs(pk or {}) do
            if p.type == BAT then samples = samples + 1 if p.sees_player then seen_n = seen_n + 1 end end
        end
        eat_if_low(t, 50)
        pray_if_low(t)
        t.ticks(1)
    end
    t.exec("safespot.held", t.wave.state, "inferno")
    local hits, swings = 0, 0
    for _, h in ipairs(rows(t, "hit_player")) do if h.tick >= mark and h.npc_type == BAT then hits = hits + 1 end end
    for _, a in ipairs(rows(t, "npc_anim", { type = BAT })) do if a.tick >= mark and a.seq == ATTACK then swings = swings + 1 end end
    D.safespot = { hits = hits, swings = swings, seen = seen_n, samples = samples, tile = best }
    local ok = hits == 0 and swings == 0 and samples > 0
    local text = string.format("attempt %d: held %d ticks on tile %d,%d behind pillar %s: the bat (type 7692) made %d swings (seq 7578) and %d hit_player rows; its sees_player was true in %d of %d reads",
        attempt, now(t) - mark, best[2], best[3], best[1], swings, hits, seen_n, samples)
    if ok then t.check("technique.pillar_safespot", true, text) else note(t, "safespot.attempt" .. attempt, text) end
    return ok
end

-- the player's own attacks on the bat until it dies; Protect from Missiles up
local function kill_bat(t, label)
    local from = now(t)
    local res = { from = from }
    local ar, ad = t.player.attack(BAT_SYMBOL, 2, 10)
    res.attack = tostring(ar)
    local lv = string.match(tostring(ad), "%(level%-(%d+)%)")
    if lv then D.levels[#D.levels + 1] = tonumber(lv) end
    res.menu = tostring(ad):sub(1, 160)
    local tries = 0
    while alive(t) and tries < 12 do
        tries = tries + 1
        local b = find_bat(t)
        if b == nil then break end
        local kr = t.npc.await_dead_engaged(4, 1, { eat = { item = "shark", below = 55 } })
        if kr == "ok" then break end
        eat_if_low(t, 50)
        pray_if_low(t)
        t.player.attack(BAT_SYMBOL, 2, 10)
    end
    t.ticks(4)
    res.to = now(t)
    return res
end
local function bat_safespot(t)
    for attempt = 1, 8 do
        if not alive(t) then return false end
        if bat_safespot_attempt(t, attempt) then
            return true
        end
    end
    t.check("technique.pillar_safespot", false, "eight attempts at a ring tile the bat could not see, each reached by the bat; see safespot.attempt rows")
    return false
end

-- technique 7: the player opens fire at once from the arena entrance; the bat is outside its range of four and has to fly in
local function bat_outranged(t)
    enter_wave(t, "outranged")
    t.prayer.set("protectfrommissiles", true)
    local mark = now(t)
    local _, raw0 = find_bat(t)
    local ptx, ptz = raw0 and raw0.player_x or 0, raw0 and raw0.player_z or 0
    D.out_from = mark
    local k = kill_bat(t, "outranged")
    D.out_to = now(t)
    local hn = rows(t, "hit_npc", { type = BAT })
    local first_swing = nil
    for _, a in ipairs(bat_swings(t, mark)) do if first_swing == nil then first_swing = a.tick end end
    local tiles = rows(t, "npc_tile", { type = BAT })
    local function bat_at(tick)
        local best = nil
        for _, r in ipairs(tiles) do if r.tick <= tick then best = r end end
        return best
    end
    local before, dists = 0, {}
    for _, h in ipairs(hn) do
        if h.tick >= mark and (first_swing == nil or h.tick < first_swing) then
            before = before + 1
            local b = bat_at(h.tick)
            if b then dists[#dists + 1] = foot_dist(ptx, ptz, b.x, b.z) end
        end
    end
    local ok = before >= 1 and (first_swing == nil or first_swing > mark)
    t.check("technique.bat_outranged", ok, string.format("player at %d,%d fired from tick %d: %d hit_npc rows landed before the bat's first swing (tick %s), the bat stood %s tiles from the player at those hits (its range is 4; a shot lands after the bat has flown in)", ptx, ptz, mark, before, tostring(first_swing), join(dists)))
    return k
end

-- ---- analysis: every number below comes from the tick log and the per-tick samples of the stand phases ----------------------
local function sample_at(tick)
    local best = nil
    for _, s in ipairs(D.pos) do if s.tick <= tick and s.x then best = s end end
    return best
end
local function pair_hits(swings, hits)
    -- each swing takes the first unused hit row on its tick or the three after
    local out, used = {}, {}
    for _, a in ipairs(swings) do
        local got = nil
        for i, h in ipairs(hits) do
            if not used[i] and h.tick >= a.tick and h.tick <= a.tick + 3 then used[i] = true got = h break end
        end
        out[#out + 1] = { swing = a.tick, hit = got }
    end
    return out
end
local function analyse(t)
    local A = {}
    local sw = bat_swings(t, D.since)
    local ht = bat_hits(t, D.since)
    A.swings, A.hits = sw, ht
    -- the stand phases
    local p1s, p1h, p2s, p2h = {}, {}, {}, {}
    for _, a in ipairs(sw) do
        if a.tick >= D.p1_from and a.tick < D.p1_to - 3 then p1s[#p1s + 1] = a end
        if a.tick >= D.p2_from + 2 and a.tick < D.p2_to - 3 then p2s[#p2s + 1] = a end
    end
    for _, h in ipairs(ht) do
        if h.tick >= D.p1_from and h.tick < D.p1_to then p1h[#p1h + 1] = h end
    end
    A.p1s, A.p1h, A.p2s = p1s, p1h, p2s
    A.p1pairs = pair_hits(p1s, p1h)
    -- projectiles: one per swing, on the swing tick
    local prj = rows(t, "projectile")
    A.prj_by_tick = {}
    for _, r in ipairs(prj) do if r.spotanim == PROJ then A.prj_by_tick[r.tick] = r end end
    A.prj = prj
    return A
end

local function in_ranges(tick, ranges)
    for _, r in ipairs(ranges) do if tick >= r[1] and tick <= r[2] then return true end end
    return false
end
local function stat_drop(stats_text)
    -- "99/99 99/99 60/60 ..." -> the largest base - level over the five stats (negative when boosted)
    local worst = 0
    for lv, base in string.gmatch(stats_text or "", "(%-?%d+)/(%-?%d+)") do
        local d = tonumber(base) - tonumber(lv)
        if d > worst then worst = d end
    end
    return worst
end
local function emit_rows(t, A)
    local bat0 = D.bat0 or {}
    -- identity
    spec_row(t, "hitpoints", bat0.max_hitpoints == 25 and bat0.hitpoints == 25, tostring(bat0.max_hitpoints), "npc pack hitpoints " .. tostring(bat0.hitpoints) .. " of max " .. tostring(bat0.max_hitpoints) .. " on the first tick of the wave", "25 hp", "A", "exact")
    local lv = D.levels[1]
    spec_row(t, "combat_level", lv == 85, tostring(lv), "the attack row's menu text '(level-N)' on " .. #D.levels .. " presses: " .. join(D.levels), "85 count", "A", "exact")
    spec_row(t, "size", bat0.size == 2, tostring(bat0.size), "npc pack size, and t.npc.state size " .. tostring(D.state_size), "2 tiles", "A", "exact")
    -- cadence
    local g1 = gaps_of(A.p1s)
    local g2 = gaps_of(A.p2s)
    local all = {}
    for _, v in ipairs(g1) do all[#all + 1] = v end
    for _, v in ipairs(g2) do all[#all + 1] = v end
    local ug = uniq(all)
    spec_row(t, "attack_speed", #all > 20 and ug == "3", ug, #all .. " gaps between consecutive npc_anim 7578 rows of one bat standing in range (" .. #g1 .. " unprayed, " .. #g2 .. " prayed)", "3 ticks", "A", "exact")
    spec_row(t, "attack_gap_minimum", minof(all) == 3, tostring(minof(all)), "smallest of " .. #all .. " gaps", "3 ticks", "B", "exact")
    -- damage
    local dmg = {}
    for _, p in ipairs(A.p1pairs) do if p.hit then dmg[#dmg + 1] = p.hit.damage end end
    local mx = maxof(dmg)
    spec_row(t, "max_hit", mx == 19, tostring(mx), #dmg .. " unprayed hit_player rows, damage 0.." .. tostring(mx) .. ", mean " .. string.format("%.1f", #dmg > 0 and (function() local sum = 0 for _, d in ipairs(dmg) do sum = sum + d end return sum / #dmg end)() or 0), "19 hp", "C", "exact")
    local protected = {}
    local ranges = { { D.p2_from + 1, D.p2_to - 3 }, { D.kill_from, D.kill.to - 3 }, { D.out_from, D.out_to - 3 } }
    for _, p in ipairs(pair_hits(A.swings, A.hits)) do
        if p.hit and in_ranges(p.swing, ranges) then protected[#protected + 1] = p.hit.damage end
    end
    local up = uniq(protected)
    spec_row(t, "damage_when_protected", #protected >= 10 and up == "0", up, #protected .. " bat hit_player rows whose swing came after Protect from Missiles was in force", "0 hp", "D", "exact")
    local chance_nz = 0
    for _, d in ipairs(dmg) do if d > 0 then chance_nz = chance_nz + 1 end end
    local pct = #dmg > 0 and math.floor(100 * chance_nz / #dmg + 0.5) or -1
    spec_row(t, "hit_chance_unprayed", pct >= 0, tostring(pct), string.format("%d of %d unprayed hits did more than 0 against defence 60 and no armour; approximation, M23", chance_nz, #dmg), "? percent", "E", "approx")
    -- style, range, sight
    local with_proj, total = 0, 0
    for _, a in ipairs(A.swings) do total = total + 1 if A.prj_by_tick[a.tick] then with_proj = with_proj + 1 end end
    spec_row(t, "attack_style_ranged_only", total > 20 and with_proj == total, (total > 0 and with_proj == total) and "1" or "0", string.format("%d of %d swings launched a spotanim %d projectile on the swing tick", with_proj, total, PROJ), "1 count", "C", "exact")
    spec_row(t, "melee_attacks", total > 20 and total - with_proj == 0, tostring(total - with_proj), string.format("swings with no projectile (a melee swing): %d of %d", total - with_proj, total), "0 count", "B", "exact")
    local dist = {}
    for _, a in ipairs(A.p1s) do
        local s = sample_at(a.tick)
        if s and s.px then dist[#dist + 1] = foot_dist(s.px, s.pz, s.x, s.z) end
    end
    local ud = uniq(dist)
    spec_row(t, "attack_range", #dist > 20 and maxof(dist) == 4, tostring(maxof(dist)), #dist .. " swings, footprint-to-player distance at the swing tick: " .. ud, "4 tiles", "C", "exact")
    local moved, first = 0, A.p1s[1]
    local anchor = first and sample_at(first.tick)
    for _, s in ipairs(D.pos) do
        if first and s.tag == "unprayed" and s.tick > first.tick and s.x and anchor and (s.x ~= anchor.x or s.z ~= anchor.z) then moved = moved + 1 end
    end
    spec_row(t, "attack_stops_approach_in_range", first ~= nil and moved == 0, (first ~= nil and moved == 0) and "1" or "0", string.format("after its first swing (tick %s, bat at %s,%s, sees_player %s) the bat's tile changed on %d of the later unprayed reads", tostring(first and first.tick), tostring(anchor and anchor.x), tostring(anchor and anchor.z), tostring(anchor and anchor.sees), moved), "1 count", "D", "exact")
    local ss = D.safespot or {}
    spec_row(t, "attack_needs_line_of_sight", ss.swings == 0 and (ss.samples or 0) > 0 and ss.seen == 0, tostring(ss.swings), string.format("swings (npc_anim 7578) in a %d-read hold behind a pillar with sees_player true on %s reads", ss.samples or 0, tostring(ss.seen)), "0 count", "C", "exact")
    -- hit delay
    local delays = {}
    for _, p in ipairs(pair_hits(A.swings, A.hits)) do if p.hit then delays[#delays + 1] = p.hit.tick - p.swing end end
    spec_row(t, "hit_delay", #delays > 20, uniq(delays), #delays .. " swings paired with their hit_player row (first hit within three ticks), swing tick to hit tick", "1-2 ticks", "D", "range")
end

local function emit_rows2(t, A)
    -- prayer read tick and the flick technique (rows 17 and prayer_read_tick)
    local on_zero, on_n = 0, 0
    for _, r in ipairs(D.on_time or {}) do
        if r.swung and r.damage ~= nil then on_n = on_n + 1 if r.damage == 0 then on_zero = on_zero + 1 end end
    end
    local late_pos, late_n = 0, 0
    for _, r in ipairs(D.late or {}) do
        if r.swung and r.damage ~= nil then late_n = late_n + 1 if r.damage > 0 then late_pos = late_pos + 1 end end
    end
    local cost = (D.pts0 or 0) - (D.pts1 or 0)
    local read_ok = on_n >= 8 and on_zero == on_n and late_n >= 4 and late_pos >= late_n - 1
    spec_row(t, "prayer_read_tick", read_ok, read_ok and "0" or "?", string.format("prayer up on the swing tick only (on tick A-1, off on A): %d of %d hits 0 damage; the same press one tick later (on A, off A+1): %d of %d hits did damage; approximation, M22", on_zero, on_n, late_pos, late_n), "? ticks", "E", "approx")
    t.check("technique.one_tick_flick", on_n >= 8 and on_zero == on_n and cost <= 1, string.format("%d flicks (prayer up only across the swing tick, t.prayer.flick) of a lone bat: %d of %d hits for 0, Protect from Missiles points %s -> %s over the run of flicks (cost %d)", #(D.on_time or {}), on_zero, on_n, tostring(D.pts0), tostring(D.pts1), cost))
    local prayed_hit = true
    t.check("technique.pray_by_danger", on_n >= 8 and on_zero == on_n, string.format("wave 1 holds one ranged attacker (the bat, max 19) and three nibblers on a pillar: Protect from Missiles up across %d of %d bat swings, 0 damage each", on_zero, on_n))
    -- run energy
    local e1, e2 = {}, {}
    for _, s in ipairs(D.pos) do
        if s.energy then
            if s.tag == "unprayed" then e1[#e1 + 1] = s elseif s.tag == "prayed" then e2[#e2 + 1] = s end
        end
    end
    local regen = 0
    if #e2 > 5 then regen = (e2[#e2].energy - e2[3].energy) / (e2[#e2].tick - e2[3].tick) end
    local drop = (#e1 > 5) and (e1[1].energy - e1[#e1].energy) or 0
    local nh = 0
    for _, w in ipairs(A.p1s) do if w.tick >= e1[1].tick and w.tick < e1[#e1].tick then nh = nh + 1 end end
    local per = nh > 0 and (drop + regen * (e1[#e1].tick - e1[1].tick)) / nh or -1
    spec_row(t, "run_drain_per_hit", math.floor(per + 0.5) == 3, tostring(math.floor(per + 0.5)), string.format("energy %d -> %d over ticks %d..%d with %d hits (swings that landed), plus the %.2f per tick it regained while a prayer blocked the hits (%.2f per hit)", e1[1].energy, e1[#e1].energy, e1[1].tick, e1[#e1].tick, nh, regen, per), "3 count", "C", "exact")
    local falls = 0
    for i = 4, #e2 do if e2[i].energy < e2[i - 1].energy then falls = falls + 1 end end
    spec_row(t, "run_drain_blocked_by_missiles", #e2 > 20 and falls == 0, (#e2 > 20 and falls == 0) and "1" or "0", string.format("with Protect from Missiles up energy %d -> %d over %d reads (ticks %d..%d), %d reads lower than the one before, %d swings", e2[1].energy, e2[#e2].energy, #e2, e2[1].tick, e2[#e2].tick, falls, #A.p2s), "1 count", "A", "exact")
    -- stat drain
    local d1, d2 = 0, 0
    local worst1 = 0
    for _, s in ipairs(D.pos) do
        local d = stat_drop(s.stats)
        if s.tag == "unprayed" and d > 0 then d1 = d1 + 1 if d > worst1 then worst1 = d end end
        if s.tag == "prayed" and d > 0 then d2 = d2 + 1 end
    end
    spec_row(t, "stat_drain_occurs_unprayed", d1 > 0, d1 > 0 and "1" or "0", string.format("%d unprayed hits (damage > 0) and %d reads of attack, strength, defence, ranged, magic below base; CONTENT_BUGS BAT-DRAIN", #A.p1h, d1), "1 count", "A", "exact")
    spec_row(t, "stat_drain_amount", worst1 == 1, tostring(worst1), string.format("largest drop below base in any of the five combat stats over %d unprayed reads", #e1), "1 count", "C", "exact")
    spec_row(t, "stat_drain_blocked_by_missiles", d1 > 0 and d2 == 0, (d1 > 0 and d2 == 0) and "1" or "0", string.format("%d prayed reads below base against %d unprayed (blocked is only proved when the unprayed hits do drain)", d2, d1), "1 count", "A", "exact")
end

local function emit_rows3(t, A)
    -- spawn tick
    local sp = {}
    local spawns = rows(t, "npc_spawn", { type = BAT })
    for tag, b in pairs(D.begin or {}) do
        local first = nil
        for _, r in ipairs(spawns) do
            if b and r.tick >= b - 2 and r.tick <= b + 20 and (first == nil or r.tick < first) then first = r.tick end
        end
        if first then sp[#sp + 1] = first - b end
    end
    table.sort(sp)
    spec_row(t, "spawn_tick", #sp >= 2, join(sp), #sp .. " wave entries: first bat npc_spawn row tick minus the wave's begin tick (the wave message, 'wave begun by server tick N' of t.wave.enter)", "0 ticks", "B", "exact")
    -- animation sequences
    local atk = {}
    for _, a in ipairs(A.swings) do atk[#atk + 1] = a.seq end
    local au = uniq(atk)
    local anim_rows = rows(t, "npc_anim", { type = BAT })
    local deaths = rows(t, "npc_death", { type = BAT })
    local frees = rows(t, "npc_free", { type = BAT })
    local hitn = rows(t, "hit_npc", { type = BAT })
    local death_tick = {}
    for _, d in ipairs(deaths) do death_tick[d.tick] = true end
    local defend, death_seq, dlen = {}, {}, {}
    for _, a in ipairs(anim_rows) do
        if a.seq ~= ATTACK then
            local at_death = false
            for _, d in ipairs(deaths) do if a.tick > d.tick and a.tick <= d.tick + 2 then at_death = true end end
            if at_death then
                death_seq[#death_seq + 1] = a.seq
                for _, f in ipairs(frees) do if f.tick >= a.tick and f.tick <= a.tick + 4 then dlen[#dlen + 1] = f.tick - a.tick break end end
            else
                local on_hit = false
                for _, h in ipairs(hitn) do if h.tick == a.tick and not death_tick[h.tick] then on_hit = true end end
                if on_hit then defend[#defend + 1] = a.seq end
            end
        end
    end
    local swing_seq = {}
    for _, a in ipairs(anim_rows) do if A.prj_by_tick[a.tick] then swing_seq[#swing_seq + 1] = a.seq end end
    local su = uniq(swing_seq)
    spec_row(t, "attack_seq", su == "7578" and #swing_seq > 20, su, #swing_seq .. " npc_anim rows on a tick that carries the bat's spotanim " .. PROJ .. " projectile", "7578 count", "B", "exact")
    spec_row(t, "defend_seq", #defend > 0, uniq(defend), #defend .. " non-lethal hits on the bat, the npc_anim it was sent on the hit tick; approximation, M25", "7579 count", "E", "approx")
    spec_row(t, "death_seq", #death_seq > 0, uniq(death_seq), #death_seq .. " kills, the npc_anim sent after npc_death; approximation, M25", "7580 count", "E", "approx")
    spec_row(t, "death_anim_length", #dlen > 0, uniq(dlen), #dlen .. " kills: npc_anim death row tick to npc_free tick", "2 ticks", "A", "exact")
    -- attack animation length: consecutive per-tick reads of the drawn anim
    local runs, run = {}, 0
    for _, s in ipairs(D.pos) do
        if s.tag == "unprayed" then
            if s.anim == ATTACK then run = run + 1 else if run > 0 then runs[#runs + 1] = run run = 0 end end
        end
    end
    spec_row(t, "attack_anim_length", #runs >= 5 and uniq(runs) == "1", uniq(runs), #runs .. " attacks: consecutive per-tick t.npc.state reads with anim_id 7578", "1 ticks", "A", "exact")
    -- projectile
    local sp1, flights, by_dist = {}, {}, {}
    for _, a in ipairs(A.swings) do
        local r = A.prj_by_tick[a.tick]
        if r then sp1[#sp1 + 1] = r.spotanim flights[#flights + 1] = (r.end_cycle or 0) - (r.start_cycle or 0) end
    end
    spec_row(t, "projectile_spotanim", #sp1 > 20 and uniq(sp1) == tostring(PROJ), uniq(sp1), #sp1 .. " projectile rows on bat swing ticks", PROJ .. " count", "D", "exact")
    local fu = uniq(flights)
    spec_row(t, "projectile_shape", #flights > 20, fu, #flights .. " bat projectiles: end_cycle minus start_cycle in client cycles at the standing distance of 4 (flight cycles per distance); approximation, M27", "? count", "E", "approx")
end

local function summarize(t, label, from, upto)
    local sw = bat_swings(t, from, upto)
    local ht = bat_hits(t, from, upto)
    local dmg = {}
    for _, h in ipairs(ht) do dmg[#dmg + 1] = h.damage end
    note(t, label, string.format("ticks %d..%s swings %d gaps %s hits %d damage %s", from, tostring(upto), #sw, join(gaps_of(sw), nil, 60), #ht, join(dmg, nil, 80)))
    return sw, ht
end

return {
    id = "inferno_bat",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::setlevel attack 99", "::setlevel strength 99", "::setlevel ranged 99", "::setlevel magic 99",
        "::setlevel hitpoints 99", "::setlevel defence 60", "::setlevel prayer 99",
        "::give twisted_bow", "::give rune_arrow 1500", "::give 4doseprayerrestore 2", "::give shark 25",
        "::setvar varp172_option_nodef 1",
    },
    run = function(t)
        t.ticklog.start()
        t.check("spec.scope", true, "mode=normal party=1")
        local er = t.player.equip("twisted_bow")
        local ar = t.player.equip("rune_arrow")
        note(t, "equip", string.format("bow %s arrows %s", tostring(er), tostring(ar)))
        local _, mark_text = t.ticklog.mark("run.begin")
        D.since = tonumber(string.match(tostring(mark_text), "serial (%d+)"))
        enter_wave(t, "unprayed")
        local b0 = find_bat(t)
        D.bat0 = b0
        local sr, st = t.npc.state(BAT_SYMBOL)
        if sr == "ok" and type(st) == "table" then D.state_size = st.size end
        local from = now(t)
        D.p1_from = from
        local function enough()
            local ht = bat_hits(t, from)
            local m = {}
            for _, h in ipairs(ht) do m[#m + 1] = h.damage end
            return #ht >= 30 and (maxof(m) or 0) >= 19
        end
        local spent = stand(t, "unprayed", 240, enough)
        D.p1_to = now(t)
        summarize(t, "p1.summary", from, D.p1_to + 1)
        note(t, "p1.series", string.format("ticks %d first %s last %s", spent, join({ D.pos[1].energy, D.pos[#D.pos].energy }), D.pos[#D.pos].stats))
        D.p2_from = now(t)
        t.exec("prayer.missiles", t.prayer.set, "protectfrommissiles", true)
        D.p2_from = now(t)
        stand(t, "prayed", 45, nil)
        D.p2_to = now(t)
        summarize(t, "p2.summary", D.p2_from, D.p2_to + 1)
        local parts = {}
        for i = 1, #D.pos, 4 do local s = D.pos[i] parts[#parts + 1] = string.format("%d:%s@%s,%s e%s a%s", s.tick, s.tag, tostring(s.x), tostring(s.z), tostring(s.energy), tostring(s.anim)) end
        note(t, "p.sample", table.concat(parts, " "):sub(1, 1500))
        t.exec("prayer.off", t.prayer.set, "protectfrommissiles", false)
        t.ticks(4)
        D.flick_from = now(t)
        local _, _, p0 = t.prayer.points()
        local on_time = flick_series(t, 10, false)
        local _, _, p1 = t.prayer.points()
        D.on_time, D.pts0, D.pts1 = on_time, p0 and p0.level, p1 and p1.level
        local parts2 = {}
        for _, r in ipairs(on_time) do parts2[#parts2 + 1] = string.format("%s/%s:%s:%s", tostring(r.flick), tostring(r.result), tostring(r.swung), tostring(r.damage)) end
        note(t, "flick.on_time", string.format("n %d prayer %s -> %s; %s", #on_time, tostring(p0 and p0.level), tostring(p1 and p1.level), table.concat(parts2, " ")))
        local late = flick_series(t, 6, true)
        D.late = late
        local parts3 = {}
        for _, r in ipairs(late) do parts3[#parts3 + 1] = string.format("%s/%s:%s:%s hp%s", tostring(r.flick), tostring(r.result), tostring(r.swung), tostring(r.damage), tostring(r.hp)) end
        note(t, "flick.late", string.format("n %d; %s", #late, table.concat(parts3, " ")))
        t.exec("prayer.missiles2", t.prayer.set, "protectfrommissiles", true)
        D.kill_from = now(t)
        local k = kill_bat(t, "main")
        note(t, "kill.main", string.format("attack %s menu %s ticks %d..%d", k.attack, k.menu, k.from, k.to))
        D.kill = k
        local anims = {}
        for _, a in ipairs(rows(t, "npc_anim", { type = BAT })) do anims[#anims + 1] = a.seq .. "@" .. a.tick end
        note(t, "kill.anims", join(anims, nil, 200):sub(1, 700))
        local dd = {}
        for _, kk in ipairs({ "npc_death", "npc_free", "hit_npc" }) do
            for _, r in ipairs(rows(t, kk, { type = BAT })) do dd[#dd + 1] = kk .. "@" .. r.tick end
        end
        note(t, "kill.rows", table.concat(dd, " "):sub(1, 600))
        local prj = {}
        for _, r in ipairs(rows(t, "projectile")) do if r.spotanim == PROJ and #prj < 60 then prj[#prj + 1] = string.format("%d:%s-%s", r.tick, tostring(r.start_cycle), tostring(r.end_cycle)) end end
        note(t, "proj.rows", table.concat(prj, " "):sub(1, 700))
        if alive(t) then bat_safespot(t) end
        if alive(t) then bat_outranged(t) end
        local A = analyse(t)
        emit_rows(t, A)
        emit_rows2(t, A)
        emit_rows3(t, A)
        t.blocked("content_bug: BAT-DRAIN (CONTENT_BUGS.md): 0 of 27 unprayed bat hits lowered any of attack, strength, defence, ranged or magic: rows stat_drain_occurs_unprayed, stat_drain_amount, stat_drain_blocked_by_missiles fail against their spec. seam gaps, rows left unmeasured: (1) no verb reads an npc's defence, attack, strength, ranged or magic level (rows defence_level, attack_strength_levels, ranged_magic_levels) or its model id (model); (2) the walk and ready animation of a bat is never drawn as anim_id (always -1 between swings; row ready_walk_seq); (3) no verb reads a projectile's own sequence length (projectile_anim_length) or a sound (sounds, approx M26); (4) stat_drain_boosts_bat needs the bat's levels (as 1) and a drain; (5) resurrectable and resurrected_hitpoints need a wave 35+ Jal-Zek revive of a bat, which a bow-only practice entry does not survive (the mager unit reads them)")
        return
    end,
}
