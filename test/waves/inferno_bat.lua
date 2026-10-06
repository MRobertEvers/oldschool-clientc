-- inferno_bat: Jal-MejRah (spec docs/minigames/inferno/encounters/bat.tsv). ONE practice entry of wave 2 (two bats, three
-- nibblers; bat.scope.tsv: "practice entry on wave 2"), fought for real from the entry tile:
--   1. Protect from Missiles up at once (pray by danger) and the player opens fire on a bat still flying in (outranged);
--   2. the other bat is stood in front of with the prayer up, then behind a pillar (safespot), then unprayed (hit sizes,
--      run energy, stat drain), then flicked (prayer up only across its swing tick), then flicked one tick late;
--   3. the prayer back up on its swing tick and the bat killed with the bow.
-- Every shot is taken inside the fight on the tick of its event; the ledger rows after the fight are t.expect rows that
-- name those shots. Setup = bring-alongs only (levels, a bow, food, one prayer potion, auto-retaliate off).
local BAT, BAT_SYMBOL = 7692, "inferno_creature_harpie"
local ATTACK, DEFEND, DEATH, PROJ = 7578, 7579, 7580, 1382
local PILLAR_NPC = 7709
local PILLARS = { w = { 17, 37 }, s = { 27, 23 }, e = { 34, 39 } }
local KEYS = { "w", "s", "e" }
local WAVE = 2
local D = { since = nil, pos = {}, levels = {}, shots = {}, prot = {} }

local function now(t) local _, k = t.tick() return k or -1 end
local function note(t, label, text) t.expect(label, "ok", text) end
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
    local seen, order = {}, {}
    for _, v in ipairs(list) do
        if seen[v] == nil then seen[v] = 0 order[#order + 1] = v end
        seen[v] = seen[v] + 1
    end
    table.sort(order)
    local parts = {}
    for _, v in ipairs(order) do parts[#parts + 1] = tostring(v) end
    return table.concat(parts, ","), seen
end
-- Chebyshev distance from a tile to the bat's footprint whose south-west tile is (px, pz); the footprint's size is read
-- from the pack row at entry (D.size), never assumed
local function foot_dist(x, z, px, pz)
    local n = D.size - 1
    local dx = math.max(px - x, 0, x - (px + n))
    local dz = math.max(pz - z, 0, z - (pz + n))
    return math.max(dx, dz)
end
local function spec_row(t, id, ok, measured, extra, specv, grade, tol)
    local detail = "measured " .. measured .. ((extra and extra ~= "") and (", " .. extra) or "") .. " (spec " .. specv .. ", grade " .. grade .. ", tol " .. tol .. ")"
    t.expect("spec.bat." .. id, ok and "ok" or "fail", detail)
end
-- a shot taken at its event; the later ledger row names it
local function shoot(t, key, name)
    local r = t.shot(name)
    D.shots[key] = name .. " (server tick " .. now(t) .. ")"
    return r
end
local function shotname(key) return D.shots[key] or ("no shot " .. key) end
local function energy(t)
    local _, text = t.ui.text("orbs:runenergy_text")
    return tonumber(string.match(tostring(text), "(%d+)"))
end
local function sharks(t) local _, n = t.inv.count("shark") return tonumber(n) or 0 end
local function eat_if_low(t, below)
    local h = hp(t)
    if h > 0 and h < (below or 50) and sharks(t) > 0 then t.player.inv_op("shark", 1) return true end
    return false
end
local function pray_if_low(t)
    local _, pts, _ = t.prayer.points()
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
-- every bat in the pack; with a world slot, that one bat
local function bats(t)
    local _, _, pack, raw = t.npc.pack(40)
    local out = {}
    for _, p in ipairs(pack or {}) do if p.type == BAT then out[#out + 1] = p end end
    return out, raw
end
local function find_bat(t, wslot)
    local list, raw = bats(t)
    for _, p in ipairs(list) do if wslot == nil or p.slot == wslot then return p, raw end end
    return nil, raw
end
-- the same as rows() but with no tick wait: for the small kinds read inside the fight loop (hits, deaths, the bats' anims)
local function rows0(t, kind, opts)
    opts = opts or {}
    opts.kind = kind
    if opts.since == nil then opts.since = D.since end
    local r, list = t.ticklog.rows(opts)
    if r ~= "ok" then return {} end
    return list
end
local function bat_hits(t, from, upto, wslot)
    local out = {}
    for _, h in ipairs(rows0(t, "hit_player")) do
        if h.npc_type == BAT and h.tick >= from and (upto == nil or h.tick < upto) and (wslot == nil or h.npc_slot == wslot) then out[#out + 1] = h end
    end
    return out
end
local function bat_swings(t, from, upto, wslot)
    local out = {}
    for _, a in ipairs(rows0(t, "npc_anim", { type = BAT })) do
        if a.seq == ATTACK and a.tick >= from and (upto == nil or a.tick < upto) and (wslot == nil or a.slot == wslot) then out[#out + 1] = a end
    end
    return out
end
local function gaps_of(list)
    local g = {}
    for i = 2, #list do g[#g + 1] = list[i].tick - list[i - 1].tick end
    return g
end
-- turn the camera so a bat is in frame: yaw units are 2048 to a circle
local function aim(t, wslot, zoom, turn, pitch)
    local bat, raw = find_bat(t, wslot)
    if not bat or not raw then return false end
    local dx, dz = bat.x + (bat.size - 1) / 2 - raw.player_x, bat.z + (bat.size - 1) / 2 - raw.player_z
    -- camera yaw runs counter-clockwise from north (512 faces west), so the bearing is taken from (-dx, dz)
    local yaw = (math.floor((math.atan(-dx, dz) / (2 * math.pi)) * 2048) + (turn or 0)) % 2048
    t.drive.camera(yaw, pitch or 383, zoom or 900)
    return true
end
local STATS = { "attack", "strength", "defence", "ranged", "magic" }
local function stat_levels(t)
    local lv, txt = {}, {}
    for _, n in ipairs(STATS) do
        local _, a = t.skill.read(n)
        local l = (type(a) == "table") and (a.level or -1) or -1
        lv[#lv + 1] = l
        txt[#txt + 1] = l .. "/" .. ((type(a) == "table") and (a.base_level or -1) or -1)
    end
    return lv, table.concat(txt, " ")
end
-- one reading per server tick while the player stands: bat B's tile and sight, run energy, the five combat stats, its drawn anim
local function sample(t, tag)
    local tk = now(t)
    local bat, raw = find_bat(t, D.B.slot)
    local s = { tick = tk, tag = tag, energy = energy(t) }
    if bat then s.x, s.z, s.sees, s.hp, s.size = bat.x, bat.z, bat.sees_player, bat.hitpoints, bat.size end
    if raw then s.px, s.pz = raw.player_x, raw.player_z end
    s.lv, s.stats = stat_levels(t)
    local sr, st = t.npc.state({ slot = D.B.cslot })
    if sr == "ok" and type(st) == "table" then s.anim = st.anim_id end
    local prev = D.pos[#D.pos]
    D.pos[#D.pos + 1] = s
    -- the first tick all five combat stats fell together: shoot it with the bat in frame
    if tag == "unprayed" and prev and prev.lv and not D.shots.drain then
        local fell = 0
        for i = 1, 5 do if s.lv[i] < prev.lv[i] then fell = fell + 1 end end
        if fell == 5 then aim(t, D.B.slot) shoot(t, "drain", "unprayed.stat_drain_all_five_fell_bat_in_frame") end
    end
    return s
end
-- stand and read until the stop function says so, a tick budget runs out, the bat is gone or the player dies
local function stand(t, tag, budget, stop)
    local start = now(t)
    while now(t) - start < budget and alive(t) do
        local s = sample(t, tag)
        if s.x == nil then break end
        if eat_if_low(t, 55) then t.ticks(1) end
        pray_if_low(t)
        if stop and stop() then break end
        t.ticks(1)
    end
    return now(t) - start
end
-- wait for bat B's next swing and shoot it as it is drawn
local function swing_shot(t, key, name, budget)
    aim(t, D.B.slot)
    local r, _, tk = t.npc.await_anim({ slot = D.B.cslot }, ATTACK, budget or 8)
    if r == "ok" then shoot(t, key, name) return true end
    return false
end
-- the player's own attacks on one bat until it dies: the first arrow that lands and the dying bat are shot on their tick
local function kill_bat(t, b, label, first_hit_name)
    local res = { from = now(t), hits = 0 }
    -- a low camera facing the bat, so a bat still far off is on screen for the press
    aim(t, b.slot, 700, 0, 200)
    local ar, ad = t.player.attack(BAT_SYMBOL, 2, 8, { slot = b.cslot, quick = true })
    if ar ~= "ok" and ar ~= "timeout" then
        aim(t, b.slot, 500, 0, 160)
        ar, ad = t.player.attack(BAT_SYMBOL, 2, 8, { slot = b.cslot, quick = true })
    end
    if ar ~= "ok" and ar ~= "timeout" then
        -- the fast path could not frame the bat: the quest press (it aims and walks itself)
        local first = tostring(ad)
        ar, ad = t.player.attack(BAT_SYMBOL, 2, 10, { slot = b.cslot })
        ad = tostring(ad) .. " [after a fast press that answered: " .. first:sub(1, 200) .. "]"
    end
    t.check(label .. ".attack_press", ar == "ok" or ar == "timeout", tostring(ad))
    res.attack = tostring(ar)
    res.detail = tostring(ad)
    local lv = string.match(tostring(ad), "%(level%-(%d+)%)")
    if lv then D.levels[#D.levels + 1] = tonumber(lv) end
    local pressed = now(t)
    local g = 0
    while alive(t) and g < 80 do
        g = g + 1
        aim(t, b.slot, 2000, 0)
        local sr, st = t.npc.state({ slot = b.cslot })
        if sr == "ok" and type(st) == "table" and st.anim_id == DEATH and not res.death_shot then
            shoot(t, label .. ".dying", "death." .. label .. ".bat_in_death_animation_7580")
            res.death_shot = true
        end
        local hn = 0
        for _, h in ipairs(rows0(t, "hit_npc")) do if h.slot == b.slot and h.tick >= res.from then hn = hn + 1 end end
        if hn > res.hits then
            if res.hits == 0 and first_hit_name then shoot(t, label .. ".first_hit", first_hit_name) end
            res.hits = hn
            pressed = now(t)
        end
        for _, d in ipairs(rows0(t, "npc_death")) do if d.slot == b.slot and d.tick >= res.from then res.death_tick = d.tick end end
        if res.death_tick then
            if res.death_shot or sr ~= "ok" or now(t) > res.death_tick + 3 then break end
            if not res.death_shot then
                aim(t, b.slot, 2000, 0)
                local wr = t.npc.await_anim({ slot = b.cslot }, DEATH, 3)
                if wr == "ok" then shoot(t, label .. ".dying", "death." .. label .. ".bat_in_death_animation_7580") res.death_shot = true end
            end
        else
            if eat_if_low(t, 50) then
                t.ticks(1)
                t.player.attack(BAT_SYMBOL, 2, 2, { slot = b.cslot, quick = true })
                pressed = now(t)
            elseif now(t) - pressed > 8 then
                t.player.attack(BAT_SYMBOL, 2, 2, { slot = b.cslot, quick = true })
                pressed = now(t)
            end
            pray_if_low(t)
            t.ticks(1)
        end
    end
    t.ticks(3)
    res.to = now(t)
    return res
end
-- flicks on bat B: prayer ON the tick before its swing tick and OFF on it (late = false), or one tick late (late = true). The
-- flicks numbered in `shots` (after an earlier flick, so the splat beside the bat is a blue 0) are pressed as set_on_tick ON at A-1, the swing awaited and shot with the prayer lit, then OFF
-- on A; the rest by t.prayer.flick. The hit of each swing is read from bat B's hit_player rows within three ticks of it.
local function flick_series(t, count, late, shots, tag)
    local out, last, guard = {}, -1, now(t)
    local from = now(t)
    while #out < count and alive(t) and now(t) - guard < 200 and find_bat(t, D.B.slot) do
        eat_if_low(t, 60)
        pray_if_low(t)
        D.fe = D.fe or {}
        D.fe[#D.fe + 1] = { tick = now(t), energy = energy(t), tag = tag }
        local sw = bat_swings(t, from, nil, D.B.slot)
        local L = sw[#sw] and sw[#sw].tick or -1
        if L > last and now(t) <= L + 1 then
            last = L
            local A = L + 3 + (late and 1 or 0)
            local rec = { swing = L + 3, flick = A, hp = hp(t) }
            local k = #out + 1
            if shots and shots[k] then
                aim(t, D.B.slot)
                local r1, _, info = t.prayer.set_on_tick("protectfrommissiles", true, A - 1)
                local ar = t.npc.await_anim({ slot = D.B.cslot }, ATTACK, 4)
                if ar == "ok" then shoot(t, tag .. k, tag .. ".flick" .. k .. "_prayer_lit_as_bat_swings") end
                local r2 = t.prayer.set_on_tick("protectfrommissiles", false, A)
                if r2 ~= "ok" then t.prayer.set("protectfrommissiles", false) end
                rec.result, rec.manual, rec.off = r1, true, r2
            else
                local fr = t.prayer.flick("protectfrommissiles", A)
                rec.result = fr
            end
            out[#out + 1] = rec
        else
            t.ticks(1)
        end
    end
    t.ticks(5)
    local hits = bat_hits(t, from, nil, D.B.slot)
    local swing_set = {}
    for _, a in ipairs(bat_swings(t, from, nil, D.B.slot)) do swing_set[a.tick] = true end
    for _, r in ipairs(out) do
        r.swung = swing_set[r.swing] == true
        for _, h in ipairs(hits) do
            if h.tick >= r.swing and h.tick <= r.swing + 3 and r.damage == nil then r.damage = h.damage end
        end
    end
    return out
end

local function sgn(v) if v > 0 then return 1 elseif v < 0 then return -1 end return 0 end
-- technique 4: stand one tile off a pillar on the far side of bat B's straight line, with the prayer up; hold while the bat sits
-- behind the pillar. An attempt that the bat flies round is noted and the next is planned from its new tile.
local function safespot(t)
    for attempt = 1, 6 do
        local b, raw = find_bat(t, D.B.slot)
        if not alive(t) or b == nil then return false end
        local bx, bz = arena_base(t)
        if bx == nil then note(t, "safespot.base" .. attempt, "the three pillar npcs were not all in the pack") return false end
        local _, _, ws = t.wave.state("inferno")
        local best, bestd, tried = nil, 1e9, {}
        for _, k in ipairs(KEYS) do
            local pl = ws and ws.pillars and ws.pillars[k]
            if pl and not pl.dead and (pl.hp or 0) >= 120 then
                local cx, cz = bx + PILLARS[k][1] + 1, bz + PILLARS[k][2] + 1
                local dx, dz = cx - (b.x + (b.size - 1) / 2), cz - (b.z + (b.size - 1) / 2)
                local cands = { { cx + 2 * sgn(dx), cz }, { cx, cz + 2 * sgn(dz) } }
                for _, h in ipairs(cands) do
                    if h[1] ~= cx or h[2] ~= cz then
                        local _, _, seen = t.world.los({ x = h[1], z = h[2], level = 0 }, b)
                        tried[#tried + 1] = string.format("%s:%d,%d=%s", k, h[1] - bx, h[2] - bz, tostring(seen))
                        local off = math.min(math.abs(dx), math.abs(dz))
                        local d = off * 100 + math.max(math.abs(h[1] - raw.player_x), math.abs(h[2] - raw.player_z))
                        if seen == false and d < bestd then best, bestd = { k, h[1], h[2] }, d end
                    end
                end
            end
        end
        if best == nil then
            note(t, "safespot.pick" .. attempt, "no pillar tile hides the player from the bat at " .. b.x .. "," .. b.z .. ": " .. table.concat(tried, " "))
            t.ticks(3)
        else
            local wr = t.player.walk_to(best[2], best[3], 40)
            local mark = now(t)
            D.shots.safespot = nil
            t.ticklog.mark("safespot.hold" .. attempt)
            local seen_n, samples, blocked_in_range = 0, 0, 0
            while now(t) - mark < 24 and alive(t) do
                local s = sample(t, "safespot")
                if s.x == nil then break end
                samples = samples + 1
                if s.sees then seen_n = seen_n + 1 end
                local dist = s.px and foot_dist(s.px, s.pz, s.x, s.z) or 99
                if not s.sees and dist <= 5 then blocked_in_range = blocked_in_range + 1 end
                if not s.sees and dist <= 5 and now(t) - mark >= 4 and not D.shots.safespot then
                    aim(t, D.B.slot, 1600, 1600)
                    shoot(t, "safespot", "technique.pillar_safespot.bat_blocked_behind_pillar")
                end
                eat_if_low(t, 50)
                pray_if_low(t)
                t.ticks(1)
            end
            local hits = #bat_hits(t, mark, nil, D.B.slot)
            local swings = #bat_swings(t, mark, nil, D.B.slot)
            local text = string.format("attempt %d: held %d ticks on tile %d,%d (local %d,%d) behind pillar %s (walk %s): bat B made %d swings (seq 7578) and %d hit_player rows; sees_player true on %d of %d reads, in reach (gap <= 5) with no sight on %d; candidates %s",
                attempt, now(t) - mark, best[2], best[3], best[2] - bx, best[3] - bz, best[1], tostring(wr), swings, hits, seen_n, samples, blocked_in_range, table.concat(tried, " "))
            local ok = hits == 0 and swings == 0 and samples >= 20 and seen_n == 0 and D.shots.safespot ~= nil
            note(t, "safespot.attempt" .. attempt, text)
            if ok then
                D.safespot = { hits = hits, swings = swings, seen = seen_n, samples = samples, blocked = blocked_in_range, text = text, from = mark, to = now(t) }
                return true
            end
        end
    end
    return false
end

-- ---- analysis: every number below comes from the tick log and the per-tick samples of the stand phases ----------------------
local function sample_at(tick)
    local best = nil
    for _, s in ipairs(D.pos) do if s.tick <= tick and s.x then best = s end end
    return best
end
-- each swing takes the first unused hit row of the same bat on its tick or the three after
local function pair_hits(swings, hits)
    local out, used = {}, {}
    for _, a in ipairs(swings) do
        local got = nil
        for i, h in ipairs(hits) do
            if not used[i] and h.npc_slot == a.slot and h.tick >= a.tick and h.tick <= a.tick + 3 then used[i] = true got = h break end
        end
        out[#out + 1] = { swing = a.tick, slot = a.slot, hit = got }
    end
    return out
end
local function in_ranges(tick, ranges)
    for _, r in ipairs(ranges) do if tick >= r[1] and tick <= r[2] then return true end end
    return false
end
local function analyse(t)
    local A = {}
    A.swings = bat_swings(t, 0)
    A.hits = bat_hits(t, 0)
    A.bs = bat_swings(t, 0, nil, D.B.slot)
    local p1s, p1h, p2s = {}, {}, {}
    for _, a in ipairs(A.bs) do
        if a.tick >= D.p1_from and a.tick < D.p1_to - 3 then p1s[#p1s + 1] = a end
        if a.tick >= D.p2_from + 2 and a.tick < D.p2_to - 3 then p2s[#p2s + 1] = a end
    end
    for _, h in ipairs(bat_hits(t, D.p1_from, D.p1_to, D.B.slot)) do p1h[#p1h + 1] = h end
    A.p1s, A.p1h, A.p2s = p1s, p1h, p2s
    A.p1pairs = pair_hits(p1s, p1h)
    A.pairs = pair_hits(A.swings, A.hits)
    -- projectiles: the count launched on each tick, against the bat swings of that tick
    A.prj = {}
    A.prj_n = {}
    for _, r in ipairs(rows(t, "projectile")) do
        if r.spotanim == PROJ then A.prj[#A.prj + 1] = r A.prj_n[r.tick] = (A.prj_n[r.tick] or 0) + 1 end
    end
    A.swing_n = {}
    for _, a in ipairs(A.swings) do A.swing_n[a.tick] = (A.swing_n[a.tick] or 0) + 1 end
    return A
end

local function emit_identity(t, A)
    local hpl, sizes = {}, {}
    for _, b in ipairs(D.bat0 or {}) do hpl[#hpl + 1] = b.hitpoints sizes[#sizes + 1] = b.size end
    local okhp = #hpl == 2
    for _, b in ipairs(D.bat0 or {}) do if b.hitpoints ~= 25 then okhp = false end end
    spec_row(t, "hitpoints", okhp, join(hpl), "t.npc.pack hitpoints of each of the wave's " .. #hpl .. " bats on the first tick after the wave began (health ratio " .. join(D.bat0 or {}, function(b) return tostring(b.health_ratio) end) .. ")", "25 hp", "A", "exact")
    local lvok = #D.levels > 0
    for _, v in ipairs(D.levels) do if v ~= 85 then lvok = false end end
    spec_row(t, "combat_level", lvok, join(D.levels), "the attack row's menu text '(level-N)' on " .. #D.levels .. " attack presses on the two bats", "85 count", "A", "exact")
    local szok = #sizes == 2
    for _, v in ipairs(sizes) do if v ~= 2 then szok = false end end
    spec_row(t, "size", szok, join(sizes), "t.npc.pack size of each bat; t.npc.state size of bat B " .. tostring(D.state_size), "2 tiles", "A", "exact")
    -- cadence of bat B while it stood in reach (prayed and unprayed stands)
    local g1, g2 = gaps_of(A.p1s), gaps_of(A.p2s)
    local all = {}
    for _, v in ipairs(g2) do all[#all + 1] = v end
    for _, v in ipairs(g1) do all[#all + 1] = v end
    spec_row(t, "attack_speed", #all > 20 and maxof(all) == 3 and minof(all) == 3, join(all, nil, 80), #all .. " gaps between consecutive npc_anim 7578 rows of bat B standing in reach (" .. #g2 .. " prayed, " .. #g1 .. " unprayed)", "3 ticks", "A", "exact")
    local ga = gaps_of(A.bs)
    spec_row(t, "attack_gap_minimum", minof(ga) == 3, tostring(minof(ga)), "smallest of all " .. #ga .. " gaps between bat B's consecutive npc_anim 7578 rows over the whole fight (gaps " .. uniq(ga) .. ")", "3 ticks", "B", "exact")
end
local function emit_damage(t, A)
    local dmg = {}
    for _, p in ipairs(A.p1pairs) do if p.hit then dmg[#dmg + 1] = p.hit.damage end end
    local mx = maxof(dmg)
    local sum = 0
    for _, d in ipairs(dmg) do sum = sum + d end
    spec_row(t, "max_hit", mx == 19, tostring(mx), #dmg .. " unprayed hit_player rows of bat B (each paired with its swing), damage " .. join(dmg, nil, 60) .. ", mean " .. string.format("%.1f", #dmg > 0 and sum / #dmg or 0), "19 hp", "C", "exact")
    local protected, where = {}, {}
    for _, p in ipairs(A.pairs) do
        if p.hit and in_ranges(p.swing, D.prot) then protected[#protected + 1] = p.hit.damage end
    end
    for _, r in ipairs(D.prot) do where[#where + 1] = r[3] .. " " .. r[1] .. ".." .. r[2] end
    spec_row(t, "damage_when_protected", #protected >= 10 and maxof(protected) == 0, join(protected, nil, 80), #protected .. " bat hit_player rows whose swing came while Protect from Missiles was in force (" .. table.concat(where, ", ") .. ")", "0 hp", "D", "exact")
    local nz = 0
    for _, d in ipairs(dmg) do if d > 0 then nz = nz + 1 end end
    local pct = #dmg > 0 and math.floor(100 * nz / #dmg + 0.5) or -1
    spec_row(t, "hit_chance_unprayed", pct >= 0, tostring(pct), string.format("%d of %d unprayed hits did more than 0 against defence 60 and no armour; approximation, M23", nz, #dmg), "? percent", "E", "approx")
    -- style: every swing launched a projectile on its tick
    local with_proj, total = 0, 0
    for tick, n in pairs(A.swing_n) do
        total = total + n
        with_proj = with_proj + math.min(n, A.prj_n[tick] or 0)
    end
    spec_row(t, "attack_style_ranged_only", total > 20 and with_proj == total, (total > 0 and with_proj == total) and "1" or "0", string.format("%d of %d bat swings (both bats) launched a spotanim %d projectile on the swing tick", with_proj, total, PROJ), "1 count", "C", "exact")
    spec_row(t, "melee_attacks", total > 20 and total - with_proj == 0, tostring(total - with_proj), string.format("swings with no projectile on their tick (a melee swing): %d of %d", total - with_proj, total), "0 count", "B", "exact")
    -- reach: footprint-to-player gap at every swing of bat B that has a reading
    local dist = {}
    for _, a in ipairs(A.bs) do
        local s = sample_at(a.tick)
        if s and s.px and s.tick >= a.tick - 1 then dist[#dist + 1] = foot_dist(s.px, s.pz, s.x, s.z) end
    end
    spec_row(t, "attack_range", #dist > 20 and maxof(dist) == 4, tostring(maxof(dist)), #dist .. " swings of bat B read on their tick or the one before, footprint-to-player gap " .. uniq(dist) .. " (the largest is the reach)", "4 tiles", "C", "exact")
    local moved, first = 0, A.p1s[1]
    local anchor = first and sample_at(first.tick)
    local n_after = 0
    for _, s in ipairs(D.pos) do
        if first and s.tag == "unprayed" and s.tick > first.tick and s.x and anchor then
            n_after = n_after + 1
            if s.x ~= anchor.x or s.z ~= anchor.z then moved = moved + 1 end
        end
    end
    spec_row(t, "attack_stops_approach_in_range", first ~= nil and n_after > 20 and moved == 0, (first ~= nil and moved == 0) and "1" or "0", string.format("after bat B's first unprayed swing (tick %s, at %s,%s, gap %s, sees_player %s) its tile changed on %d of %d later reads while it swung", tostring(first and first.tick), tostring(anchor and anchor.x), tostring(anchor and anchor.z), tostring(anchor and anchor.px and foot_dist(anchor.px, anchor.pz, anchor.x, anchor.z)), tostring(anchor and anchor.sees), moved, n_after), "1 count", "D", "exact")
    -- line of sight: every swing of bat B against the sight read on the tick before it; and the reads with the bat in reach but blind
    local blind_swings, blind_reads = 0, 0
    for _, a in ipairs(A.bs) do
        local s = sample_at(a.tick - 1)
        if s and s.tick == a.tick - 1 and s.sees == false then blind_swings = blind_swings + 1 end
    end
    for _, s in ipairs(D.pos) do
        if s.x and s.px and s.sees == false and foot_dist(s.px, s.pz, s.x, s.z) <= 4 then blind_reads = blind_reads + 1 end
    end
    local ss = D.safespot or {}
    spec_row(t, "attack_needs_line_of_sight", blind_reads >= 10 and blind_swings == 0, tostring(blind_swings), string.format("swings of bat B on a tick after a read with sees_player false; %d reads had the bat within its reach of 4 with no sight (the pillar hold: %d swings, %d hits over %d reads); shot %s", blind_reads, ss.swings or -1, ss.hits or -1, ss.samples or 0, shotname("safespot")), "0 count", "C", "exact")
    local delays = {}
    for _, p in ipairs(A.pairs) do if p.hit then delays[#delays + 1] = p.hit.tick - p.swing end end
    local dok = #delays > 20 and minof(delays) >= 1 and maxof(delays) <= 2
    spec_row(t, "hit_delay", dok, uniq(delays), #delays .. " bat swings paired with their own hit_player row (same npc slot, within three ticks): " .. select(1, (function() local u, c = uniq(delays) local o = {} for k, v in pairs(c) do o[#o + 1] = k .. " ticks x" .. v end table.sort(o) return table.concat(o, ", ") end)()), "1-2 ticks", "D", "range")
end
local function flick_counts()
    local on_zero, on_n, late_pos, late_n, ond, lated = 0, 0, 0, 0, {}, {}
    for _, r in ipairs(D.on_time or {}) do
        if r.swung and r.damage ~= nil then on_n = on_n + 1 ond[#ond + 1] = r.damage if r.damage == 0 then on_zero = on_zero + 1 end end
    end
    for _, r in ipairs(D.late or {}) do
        if r.swung and r.damage ~= nil then late_n = late_n + 1 lated[#lated + 1] = r.damage if r.damage > 0 then late_pos = late_pos + 1 end end
    end
    return on_zero, on_n, late_pos, late_n, ond, lated
end
local function emit_prayer_and_drain(t, A)
    local on_zero, on_n, late_pos, late_n, ond, lated = flick_counts()
    local read_ok = on_n >= 8 and on_zero == on_n and late_n >= 4 and late_pos >= late_n - 1
    spec_row(t, "prayer_read_tick", read_ok, read_ok and "0" or "?", string.format("the read is on the swing tick: prayer up across the swing tick only (on A-1, off on A): %d of %d hits 0 (damage %s); the same press one tick later (on A, off A+1): %d of %d hits did damage (%s); approximation, M22", on_zero, on_n, join(ond), late_pos, late_n, join(lated)), "? ticks", "E", "approx")
    -- run energy: drop over the unprayed stand while the energy was above one hit's drain, corrected by the regain read with the prayer up
    local e1p, e2p = {}, {}
    for _, sm in ipairs(D.pos) do
        if sm.energy then
            if sm.tag == "unprayed" and sm.energy >= 4 then e1p[#e1p + 1] = sm elseif sm.tag == "prayed" then e2p[#e2p + 1] = sm end
        end
    end
    -- the regain rate: the reads through the on-time flicks (prayer up on every swing tick, so no drain), energy below 100
    local fe = {}
    for _, r in ipairs(D.fe or {}) do if r.tag == "flick" and r.energy and r.energy < 100 then fe[#fe + 1] = r end end
    local regen, regen_text = 0, "no flick reads"
    if #fe > 5 then
        regen = (fe[#fe].energy - fe[1].energy) / math.max(1, fe[#fe].tick - fe[1].tick)
        regen_text = string.format("energy %d -> %d over ticks %d..%d of the on-time flicks", fe[1].energy, fe[#fe].energy, fe[1].tick, fe[#fe].tick)
    end
    local per, nh, span, steps = -1, 0, 0, {}
    if #e1p > 2 then
        for _, a in ipairs(A.p1s) do if a.tick > e1p[1].tick and a.tick <= e1p[#e1p].tick then nh = nh + 1 end end
        span = e1p[#e1p].tick - e1p[1].tick
        per = nh > 0 and (e1p[1].energy - e1p[#e1p].energy + regen * span) / nh or -1
        for i = 2, #e1p do local d = e1p[i - 1].energy - e1p[i].energy if d > 0 then steps[#steps + 1] = d end end
    end
    spec_row(t, "run_drain_per_hit", nh >= 15 and math.floor(per + 0.5) == 3, string.format("%d", math.floor(per + 0.5)), string.format("%.2f per swing (rounded); energy %s -> %s over ticks %s..%s with %d bat B swings in that span, plus %.3f per tick regained over %d ticks (the rate read while every swing met Protect from Missiles: %s); the falls between reads: %s", per, tostring(e1p[1] and e1p[1].energy), tostring(e1p[#e1p] and e1p[#e1p].energy), tostring(e1p[1] and e1p[1].tick), tostring(e1p[#e1p] and e1p[#e1p].tick), nh, regen, span, regen_text, uniq(steps)), "3 count", "C", "exact")
    local falls, nsteps = 0, 0
    for i = 2, #D.pos do
        local a, b = D.pos[i - 1], D.pos[i]
        if a.tag == "prayed" and b.tag == "prayed" and a.energy and b.energy then nsteps = nsteps + 1 if b.energy < a.energy then falls = falls + 1 end end
    end
    spec_row(t, "run_drain_blocked_by_missiles", nsteps > 20 and falls == 0 and #A.p2s >= 8, (nsteps > 20 and falls == 0) and "1" or "0", string.format("with Protect from Missiles up, %d consecutive energy read pairs, %d fell, across %d swings of bat B", nsteps, falls, #A.p2s), "1 count", "A", "exact")
    -- stat drain: each step down of one stat between consecutive reads of one phase
    local function stat_steps(tag)
        local ev = {}
        for i = 2, #D.pos do
            local a, b = D.pos[i - 1], D.pos[i]
            if a.tag == tag and b.tag == tag and a.lv and b.lv then
                for k = 1, 5 do if b.lv[k] < a.lv[k] then ev[#ev + 1] = { tick = b.tick, stat = STATS[k], drop = a.lv[k] - b.lv[k] } end end
            end
        end
        return ev
    end
    local e1, e2 = stat_steps("unprayed"), stat_steps("prayed")
    local per_tick = {}
    for _, e in ipairs(e1) do per_tick[e.tick] = (per_tick[e.tick] or 0) + 1 end
    local ticks_drained, alls, tl = 0, 0, {}
    for tk, n in pairs(per_tick) do ticks_drained = ticks_drained + 1 tl[#tl + 1] = tk .. "x" .. n if n == 5 then alls = alls + 1 end end
    table.sort(tl)
    local amounts = {}
    for _, e in ipairs(e1) do amounts[#amounts + 1] = e.drop end
    spec_row(t, "stat_drain_occurs_unprayed", ticks_drained > 0, ticks_drained > 0 and "1" or "0", string.format("%d drain ticks (tick x stats fell: %s) over %d unprayed swings of bat B; %d of them dropped all five combat stats together; shot %s", ticks_drained, table.concat(tl, " "), #A.p1s, alls, shotname("drain")), "1 count", "A", "exact")
    spec_row(t, "stat_drain_amount", #e1 > 0 and maxof(amounts) == 1 and minof(amounts) == 1, join(amounts, nil, 60), string.format("%d per-stat drops between consecutive per-tick reads over %d drain ticks", #e1, ticks_drained), "1 count", "C", "exact")
    spec_row(t, "stat_drain_blocked_by_missiles", ticks_drained > 0 and #e2 == 0 and #A.p2s >= 8, (ticks_drained > 0 and #e2 == 0) and "1" or "0", string.format("%d stat drops while Protect from Missiles was up across %d swings of bat B, against %d drain ticks unprayed", #e2, #A.p2s, ticks_drained), "1 count", "A", "exact")
end
local function emit_presentation(t, A)
    local sp = {}
    for _, r in ipairs(rows(t, "npc_spawn", { type = BAT })) do
        if D.begin and r.tick >= D.begin - 2 and r.tick <= D.begin + 20 then sp[#sp + 1] = r.tick - D.begin end
    end
    local spok = #sp == 2
    for _, v in ipairs(sp) do if v ~= 0 then spok = false end end
    spec_row(t, "spawn_tick", spok, join(sp), #sp .. " bats of the one wave-2 entry: npc_spawn row tick minus the wave's begin tick " .. tostring(D.begin) .. " ('wave begun by server tick N' of t.wave.enter, the wave message)", "0 ticks", "B", "exact")
    local anim_rows = rows(t, "npc_anim", { type = BAT })
    local deaths = rows(t, "npc_death", { type = BAT })
    local frees = rows(t, "npc_free", { type = BAT })
    local hitn = rows(t, "hit_npc", { type = BAT })
    local death_at, free_at = {}, {}
    for _, d in ipairs(deaths) do death_at[d.slot] = d.tick end
    for _, f in ipairs(frees) do free_at[f.slot] = f.tick end
    local defend, death_seq, dlen = {}, {}, {}
    for _, a in ipairs(anim_rows) do
        if a.seq ~= ATTACK then
            local dt = death_at[a.slot]
            if dt and a.tick > dt and a.tick <= (free_at[a.slot] or dt + 6) then
                death_seq[#death_seq + 1] = a.seq
                if free_at[a.slot] then dlen[#dlen + 1] = free_at[a.slot] - a.tick end
            else
                -- a defend: sent as the arrow is loosed, with a delay; the bat's hit_npc row lands within four ticks after
                for _, h in ipairs(hitn) do if h.slot == a.slot and h.tick >= a.tick and h.tick <= a.tick + 4 then defend[#defend + 1] = a.seq break end end
            end
        end
    end
    local with_attack, without = 0, 0
    for tick, n in pairs(A.prj_n) do if A.swing_n[tick] then with_attack = with_attack + math.min(n, A.swing_n[tick]) else without = without + n end end
    local seqs = {}
    for _, a in ipairs(A.swings) do seqs[#seqs + 1] = a.seq end
    local su = (without == 0 and with_attack > 20) and uniq(seqs) or "?"
    spec_row(t, "attack_seq", su == "7578", su, with_attack .. " bat projectile launches each on a tick with an npc_anim 7578 row of a bat, " .. without .. " on a tick with none", "7578 count", "B", "exact")
    spec_row(t, "defend_seq", #defend > 0, uniq(defend), #defend .. " npc_anim rows of a bat outside its death that a hit_npc row on it followed within four ticks (sent as the arrow is loosed, with a delay); approximation, M25", "7579 count", "E", "approx")
    spec_row(t, "death_seq", #death_seq >= 2, uniq(death_seq), #death_seq .. " kills: the npc_anim each dying bat was sent between its npc_death row and its npc_free row; shots " .. shotname("batA.dying") .. ", " .. shotname("batB.dying") .. "; approximation, M25", "7580 count", "E", "approx")
    spec_row(t, "death_anim_length", #dlen > 0 and maxof(dlen) == 2 and minof(dlen) == 2, join(dlen), #dlen .. " kills: npc_anim death row tick to the bat's npc_free tick", "2 ticks", "A", "exact")
    local runs, run = {}, 0
    for _, s in ipairs(D.pos) do
        if s.tag == "unprayed" or s.tag == "prayed" then
            if s.anim == ATTACK then run = run + 1 elseif run > 0 then runs[#runs + 1] = run run = 0 end
        end
    end
    local lr, _, len = t.seq.length(ATTACK)
    len = type(len) == "table" and len or {}
    spec_row(t, "attack_anim_length", lr == "ok" and len.ticks == 1, tostring(len.ticks), "t.seq.length(7578 " .. tostring(len.name) .. "): " .. tostring(len.frames) .. " frames, " .. tostring(len.cycles) .. " client cycles; beside it " .. #runs .. " swings of bat B read per tick with anim_id 7578 on runs of " .. uniq(runs) .. " reads", "1 ticks", "A", "exact")
    local sp1, flights = {}, {}
    for _, r in ipairs(A.prj) do if A.swing_n[r.tick] then sp1[#sp1 + 1] = r.spotanim flights[#flights + 1] = (r.end_cycle or 0) - (r.start_cycle or 0) end end
    spec_row(t, "projectile_spotanim", #sp1 > 20 and uniq(sp1) == tostring(PROJ), uniq(sp1), #sp1 .. " projectile rows launched on bat swing ticks", PROJ .. " count", "D", "exact")
    spec_row(t, "projectile_shape", #flights > 20, uniq(flights), #flights .. " bat projectiles: end_cycle minus start_cycle in client cycles (mostly at the standing gap of 4); approximation, M27", "? count", "E", "approx")
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
local function emit_record(t, A)
    local r, detail, rec = t.npc.record(BAT_SYMBOL)
    local sv = (type(rec) == "table" and rec.server) or {}
    local cl = (type(rec) == "table" and rec.client) or {}
    local src = "npc record, server block (embedded content) and client block (cache)"
    spec_row(t, "defence_level", sv.defence == 55, tostring(sv.defence), src .. ": server defence " .. tostring(sv.defence) .. ", client defence-bonus params stabdefence " .. tostring(cl.params and cl.params[5]), "55 count", "A", "exact")
    spec_row(t, "attack_strength_levels", sv.attack == 0 and sv.strength == 0, tostring(sv.attack) .. "," .. tostring(sv.strength), src .. ": server attack " .. tostring(sv.attack) .. " strength " .. tostring(sv.strength), "0,0 count", "A", "exact")
    spec_row(t, "ranged_magic_levels", sv.ranged == 120 and sv.magic == 120, tostring(sv.ranged) .. "," .. tostring(sv.magic), src .. ": server ranged " .. tostring(sv.ranged) .. " magic " .. tostring(sv.magic), "120,120 count", "A", "exact")
    local m1 = cl.models and cl.models[1]
    spec_row(t, "model", m1 == 33018 and cl.models[2] == nil, tostring(m1), "client npc record (cache) models: " .. dump(cl.models), "33018 count", "A", "exact")
    spec_row(t, "ready_walk_seq", cl.readyanim == 7577 and cl.walkanim == 7577, tostring(cl.readyanim) .. "," .. tostring(cl.walkanim), "client npc record readyanim " .. tostring(cl.readyanim) .. " (" .. tostring(cl.readyanim_name) .. ") walkanim " .. tostring(cl.walkanim) .. " (" .. tostring(cl.walkanim_name) .. ")", "7577 count", "A", "exact")
    local snd = tostring(sv.attack_sound) .. "," .. tostring(sv.defend_sound) .. "," .. tostring(sv.death_sound)
    spec_row(t, "sounds", sv.attack_sound ~= nil and sv.defend_sound ~= nil and sv.death_sound ~= nil, snd, "server record attack_sound " .. tostring(sv.attack_sound) .. " (" .. tostring(sv.attack_sound_name) .. ") defend " .. tostring(sv.defend_sound) .. " (" .. tostring(sv.defend_sound_name) .. ") death " .. tostring(sv.death_sound) .. " (" .. tostring(sv.death_sound_name) .. "); approximation, M26", "? count", "E", "approx")
    local r2, d2, len = t.seq.length(7614)
    len = type(len) == "table" and len or {}
    spec_row(t, "projectile_anim_length", r2 == "ok" and len.ticks == 1, tostring(len.ticks), "t.seq.length(7614 " .. tostring(len.name) .. "): " .. tostring(len.frames) .. " frames, " .. tostring(len.cycles) .. " client cycles", "1 ticks", "A", "exact")
end
-- the last tile row at or before a tick (rows sorted by tick)
local function tile_at(list, tick)
    local best = nil
    for _, r in ipairs(list) do if r.tick <= tick then best = r else break end end
    return best
end
local function emit_techniques(t, A)
    -- 7: bat A shot from beyond its reach: arrows land while it has not swung
    local o = D.out or {}
    local a_sw = bat_swings(t, 0, nil, D.A.slot)
    local first_sw = a_sw[1] and a_sw[1].tick
    local ptiles = rows(t, "player_tile")
    local btiles = {}
    for _, r in ipairs(rows(t, "npc_spawn", { type = BAT })) do if r.slot == D.A.slot then btiles[#btiles + 1] = r end end
    for _, r in ipairs(rows(t, "npc_tile", { type = BAT })) do if r.slot == D.A.slot then btiles[#btiles + 1] = r end end
    table.sort(btiles, function(a, b) return a.tick < b.tick end)
    local before, dists, ticks = 0, {}, {}
    for _, h in ipairs(rows(t, "hit_npc", { type = BAT })) do
        if h.slot == D.A.slot and (first_sw == nil or h.tick <= first_sw) then
            local p, b = tile_at(ptiles, h.tick - 1), tile_at(btiles, h.tick - 1)
            before = before + 1
            ticks[#ticks + 1] = h.tick .. ":" .. h.damage
            if p and b then dists[#dists + 1] = foot_dist(p.x, p.z, b.x, b.z) end
        end
    end
    local far = #dists > 0 and minof(dists) > 4
    local text = string.format("bat A at gap %s from the entry tile when the attack was pressed; %d arrows landed before it swung (hit_npc tick:damage %s) at gaps %s (its reach is 4); its first swing %s, its death tick %s, %d swings in all; shots: row batA.attack_press, %s, %s",
        tostring(o.press_gap), before, table.concat(ticks, " "), join(dists), tostring(first_sw), tostring(o.death_tick), #a_sw, shotname("batA.first_hit"), shotname("batA.dying"))
    t.expect("technique.bat_outranged", (before >= 1 and far) and "ok" or "fail", text)
    -- 4: the pillar hold
    local ss = D.safespot
    t.expect("technique.pillar_safespot", ss and "ok" or "fail", ss and (ss.text .. "; shot " .. shotname("safespot")) or "no attempt held the bat behind a pillar")
    -- the one-tick flick
    local on_zero, on_n = flick_counts()
    local cost = (D.pts0 or 0) - (D.pts1 or 0)
    t.expect("technique.one_tick_flick", (on_n >= 8 and on_zero == on_n and cost <= 1) and "ok" or "fail", string.format("%d flicks of bat B (prayer up only across its swing tick: on A-1, off A): %d of %d swings hit 0; prayer points %s -> %s over the whole series, the two shot flicks included (cost %d); shots %s, %s", #(D.on_time or {}), on_zero, on_n, tostring(D.pts0), tostring(D.pts1), cost, shotname("flick3"), shotname("flick4")))
    -- 9: pray by danger: wave 2 holds two ranged attackers (the bats, max 19) and three nibblers on a pillar, so Missiles from entry
    local dmg, n = {}, 0
    for _, p in ipairs(A.pairs) do
        if p.hit and in_ranges(p.swing, { D.prot[1] }) then n = n + 1 dmg[#dmg + 1] = p.hit.damage end
    end
    t.expect("technique.pray_by_danger", (n >= 8 and maxof(dmg) == 0) and "ok" or "fail", string.format("wave 2: two bats (ranged, max 19) and three nibblers (on a pillar); Protect from Missiles pressed on entry (tick %s) and held to the end of the pillar hold (tick %s): %d bat swings in that window hit %s; shots row entry.protect_from_missiles, %s", tostring(D.prot[1] and D.prot[1][1]), tostring(D.prot[1] and D.prot[1][2]), n, join(dmg, nil, 60), shotname("prayed")))
end

local function topup(t, upto)
    local g = 0
    while alive(t) and hp(t) < upto and sharks(t) > 0 and g < 4 do t.player.inv_op("shark", 1) t.ticks(2) g = g + 1 end
    pray_if_low(t)
end

return {
    id = "inferno_bat",
    fixture = "fresh_lumbridge.ini",
    max_frames = 15000,
    setup = {
        "::clearinv", "::setlevel attack 99", "::setlevel strength 99", "::setlevel ranged 99", "::setlevel magic 99",
        "::setlevel hitpoints 99", "::setlevel defence 60", "::setlevel prayer 99",
        "::give twisted_bow", "::give rune_arrow 1500", "::give 4doseprayerrestore 1", "::give shark 25",
        "::setvar varp172_option_nodef 1",
    },
    run = function(t)
        t.ticklog.start()
        t.check("spec.scope", true, "mode=normal party=1")
        t.exec("equip.bow", t.player.equip, "twisted_bow")
        t.exec("equip.arrows", t.player.equip, "rune_arrow")
        local _, mark_text = t.ticklog.mark("run.begin")
        D.since = tonumber(string.match(tostring(mark_text), "serial (%d+)"))
        -- the one entry
        local er, ed = t.exec("enter.wave2", t.wave.enter, "inferno", WAVE)
        D.begin = tonumber(string.match(tostring(ed), "wave begun by server tick (%d+)"))
        if er ~= "ok" then return end
        t.exec("entry.protect_from_missiles", t.prayer.set, "protectfrommissiles", true)
        local open_from = now(t) + 1
        local list, raw = bats(t)
        D.bat0 = list
        if #list ~= 2 or raw == nil then t.expect("entry.bats", "fail", #list .. " bats in the pack after the wave-2 entry") return end
        D.home = { raw.player_x, raw.player_z }
        D.size = list[1].size
        -- bat A: the farther bat (it is fired on from the bow's reach of 10, well outside its own 4, whenever the press lands);
        -- bat B, the nearer, is fought after
        local function gap(b) return foot_dist(raw.player_x, raw.player_z, b.x, b.z) end
        table.sort(list, function(a, b) return gap(a) > gap(b) end)
        local ia = 1
        local a, b = list[ia], list[3 - ia]
        D.A = { slot = a.slot, cslot = a.client_slot }
        D.B = { slot = b.slot, cslot = b.client_slot }
        note(t, "entry.bats", string.format("player at %d,%d; bat A slot %d (client %d) at %d,%d gap %d; bat B slot %d (client %d) at %d,%d gap %d; hitpoints %s,%s",
            raw.player_x, raw.player_z, a.slot, a.client_slot, a.x, a.z, gap(a), b.slot, b.client_slot, b.x, b.z, gap(b), tostring(a.hitpoints), tostring(b.hitpoints)))
        aim(t, D.B.slot, 1100, 0, 160)
        shoot(t, "entry", "entry.wave2_bat_b_flying_in")
        -- technique 7: open fire at once on bat A, still out of its reach
        D.out = kill_bat(t, D.A, "batA", "technique.bat_outranged.arrow_lands_bat_a_out_of_reach")
        D.out.press_gap = gap(a)
        if not alive(t) then return end
        -- bat B with the prayer up: the stand, then the pillar
        local sr, st = t.npc.state({ slot = D.B.cslot })
        if sr == "ok" and type(st) == "table" then D.state_size = st.size end
        D.p2_from = now(t)
        swing_shot(t, "prayed", "pray_by_danger.bat_b_swings_into_protect_from_missiles", 24)
        stand(t, "prayed", 54)
        D.p2_to = now(t)
        topup(t, 80)
        t.ticklog.mark("safespot")
        safespot(t)
        D.prot[1] = { open_from, now(t), "entry to the end of the pillar hold" }
        if not alive(t) or find_bat(t, D.B.slot) == nil then return end
        t.player.walk_to(D.home[1], D.home[2], 40)
        aim(t, D.B.slot)
        t.npc.await_anim({ slot = D.B.cslot }, ATTACK, 24)
        -- unprayed: hit sizes, run energy, the stat drain
        t.ticklog.mark("unprayed")
        t.exec("unprayed.protect_off", t.prayer.set, "protectfrommissiles", false)
        D.p1_from = now(t)
        local function enough()
            local ht = bat_hits(t, D.p1_from, nil, D.B.slot)
            local m = {}
            for _, h in ipairs(ht) do m[#m + 1] = h.damage end
            if #ht >= 24 and (maxof(m) or 0) >= 19 and D.shots.drain then return true end
            if #ht >= 42 then return true end
            return sharks(t) < 6 and hp(t) < 70
        end
        stand(t, "unprayed", 170, enough)
        D.p1_to = now(t)
        t.prayer.set("protectfrommissiles", true)
        local s1, s2 = D.pos[1], D.pos[#D.pos]
        for _, s in ipairs(D.pos) do if s.tag == "unprayed" then s1 = s break end end
        note(t, "unprayed.stats", string.format("combat stats level/base at the first unprayed read %s (tick %d), at the last %s (tick %d)", tostring(s1.stats), s1.tick, tostring(s2.stats), s2.tick))
        note(t, "unprayed.food", string.format("hp %d, sharks %d after the unprayed stand (ticks %d..%d)", hp(t), sharks(t), D.p1_from, D.p1_to))
        topup(t, 85)
        -- flicks on time (two shot as the bat swings), then late
        t.prayer.set("protectfrommissiles", false)
        t.ticklog.mark("flick.on_time")
        local _, p0, _ = t.prayer.points()
        D.flick_from = now(t)
        D.on_time = flick_series(t, 12, false, { [3] = true, [4] = true }, "flick")
        local _, p1, _ = t.prayer.points()
        D.pts0, D.pts1 = p0 and p0.level, p1 and p1.level
        t.ticklog.mark("flick.late")
        D.late = flick_series(t, 4, true, nil, "late")
        t.prayer.set("protectfrommissiles", true)
        D.flick_to = now(t)
        local parts = {}
        for _, r in ipairs(D.on_time) do parts[#parts + 1] = string.format("%d:%s:%s:%s", r.flick, tostring(r.result), tostring(r.swung), tostring(r.damage)) end
        local parts2 = {}
        for _, r in ipairs(D.late) do parts2[#parts2 + 1] = string.format("%d:%s:%s:%s", r.flick, tostring(r.result), tostring(r.swung), tostring(r.damage)) end
        note(t, "flick.series", "on time (A:result:swung:damage) " .. table.concat(parts, " ") .. "; late " .. table.concat(parts2, " ") .. string.format("; ticks %d..%d, prayer %s -> %s", D.flick_from, D.flick_to, tostring(D.pts0), tostring(D.pts1)))
        topup(t, 80)
        -- the kill: Protect from Missiles back on as bat B swings, then the bow
        if alive(t) and find_bat(t, D.B.slot) then
            aim(t, D.B.slot)
            t.npc.await_anim({ slot = D.B.cslot }, ATTACK, 8)
            t.exec("kill.protect_on_as_bat_b_swings", t.prayer.set, "protectfrommissiles", true)
            D.kill_from = now(t)
            local kb = kill_bat(t, D.B, "batB", nil)
            D.prot[2] = { D.kill_from + 1, kb.to - 3, "the kill" }
        end
        note(t, "fight.end", string.format("hp %d, sharks %d, alive %s, tick %d", hp(t), sharks(t), tostring(alive(t)), now(t)))
        local A = analyse(t)
        emit_identity(t, A)
        emit_damage(t, A)
        emit_prayer_and_drain(t, A)
        emit_presentation(t, A)
        emit_record(t, A)
        emit_techniques(t, A)
        note(t, "gap.bat.stat_drain_boosts_bat", "blocked by TEST-7 (docs/minigames/waves_loop/CONTENT_BUGS.md, a driver seam): bat.stat_drain_boosts_bat needs the bat's CURRENT levels after it drains the player; t.npc.record is static (DRIVER_NOTES.md seam pass 7) and no read-only verb reads a live npc's levels")
        return
    end,
}
