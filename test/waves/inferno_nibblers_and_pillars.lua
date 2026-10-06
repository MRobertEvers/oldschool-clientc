-- inferno_nibblers_and_pillars: Jal-Nib and the three rocky supports (spec docs/minigames/inferno/encounters/nibblers_and_pillars.tsv).
-- A REAL run resumed at wave 1, then waves 2 and 3: the nibblers are left on their pillar until it falls (a
-- fall with the player standing at a known distance), the blob / bat / bloblets are killed for real, and with the third
-- pillar down the nibblers take the player. Practice entries (waves 1, 2, 66, 67, 68) give the rest.
-- Setup = bring-alongs only (levels, a bow, food, a staged real run at wave 3, auto-retaliate off).
local PILLARS = { w = { 17, 37 }, s = { 27, 23 }, e = { 34, 39 } }
local NIB, NIB_ATTACK, NIB_DEATH = 7691, 7574, 7576
local PILLAR_NPC, DYING_NPC = 7709, 7710
local KEYS = { "w", "s", "e" }
local D = { waves = {}, falls = {}, entries = {}, order = {}, sizes = {}, nib_hp = {}, walk_ids = {}, ready_ids = {}, levels = {} }   -- everything the ledger rows are computed from

local function now(t) local _, k = t.tick() return k or -1 end
local function note(t, label, text) t.check(label, true, text) end
local function hp(t) local _, a = t.skill.read("hitpoints") if type(a) == "table" then return a.level or -1 end return -1 end
local function alive(t) return t.player.alive() == "ok" end
local function rows(t, kind, opts)
    opts = opts or {}
    opts.kind = kind
    if opts.since == nil then opts.since = D.since end
    local r, list = t.ticklog.rows(opts)
    t.ticks(1)
    if r ~= "ok" then return {} end
    return list
end
local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
-- Chebyshev distance from a tile to a 3x3 footprint whose south-west tile is (px, pz)
local function foot_dist(x, z, px, pz)
    local dx = math.max(px - x, 0, x - (px + 2))
    local dz = math.max(pz - z, 0, z - (pz + 2))
    return math.max(dx, dz)
end
local function join(list, f, cap)
    local out = {}
    for i = 1, math.min(#list, cap or 40) do out[#out + 1] = f and f(list[i]) or tostring(list[i]) end
    return table.concat(out, ",")
end
local function maxof(list) local m = nil for _, v in ipairs(list) do if m == nil or v > m then m = v end end return m end
local function minof(list) local m = nil for _, v in ipairs(list) do if m == nil or v < m then m = v end end return m end

-- the arena's frame, read from the three pillar npcs (south-west tiles); local (17,37) west, (27,23) south, (34,39) east
local function arena_base(t)
    local _, _, pack = t.npc.pack(40)
    local found = {}
    for _, r in ipairs(pack or {}) do if r.type == PILLAR_NPC then found[#found + 1] = r end end
    if #found ~= 3 then return nil end
    table.sort(found, function(a, b) return a.x < b.x end)
    local west, east = found[1], found[3]
    return west.x - PILLARS.w[1], west.z - PILLARS.w[2], found
end
-- the pillar tile table in absolute tiles
local function pillar_abs(bx, bz)
    local out = {}
    for _, k in ipairs(KEYS) do out[k] = { bx + PILLARS[k][1], bz + PILLARS[k][2] } end
    return out
end
local function pillar_state(t)
    local _, _, s = t.wave.state("inferno")
    return s
end
local function eat_if_low(t, below)
    local h = hp(t)
    if h > 0 and h < (below or 55) then t.player.inv_op("shark", 1) return true end
    return false
end
local function pray_if_low(t)
    local _, pts, _ = t.prayer.points()
    if pts and pts.level and pts.level < 35 then t.player.drink("prayer_potion") end
end

-- monsters of the wave that are not nibblers, the pillar npcs or the falling pillar
local function others_alive(t)
    local r, _, pack = t.npc.pack(40)
    local list, nibs = {}, {}
    if r ~= "ok" then return list, nibs end
    for _, p in ipairs(pack) do
        if p.type == NIB then
            if not p.dying and p.hitpoints > 0 then nibs[#nibs + 1] = p end
        elseif p.type ~= PILLAR_NPC and p.type ~= DYING_NPC and not p.dying and p.hitpoints > 0 and p.client_slot >= 0 then
            list[#list + 1] = p
        end
    end
    return list, nibs
end
-- the nibblers' tiles, to keep the player off them
local function ring_pick(t, bx, bz, key, dist, nibs)
    local px, pz = bx + PILLARS[key][1], bz + PILLARS[key][2]
    local cands = {}
    for dx = -dist, 2 + dist do for dz = -dist, 2 + dist do
        local x, z = px + dx, pz + dz
        if foot_dist(x, z, px, pz) == dist then
            local free, best = true, 99
            for _, n in ipairs(nibs) do
                if n.x == x and n.z == z then free = false end
                best = math.min(best, cheb(n.x, n.z, x, z))
            end
            -- stay on the arena floor: the pillars' own footprints block, the arena is local x 5..45, z 12..46
            local lx, lz = x - bx, z - bz
            if free and lx >= 6 and lx <= 44 and lz >= 13 and lz <= 45 then cands[#cands + 1] = { x, z, best } end
        end
    end end
    table.sort(cands, function(a, b) return a[3] > b[3] end)
    return cands
end
local function go_stand(t, bx, bz, key, dist)
    local _, nibs = others_alive(t)
    local cands = ring_pick(t, bx, bz, key, dist, nibs)
    for i = 1, math.min(#cands, 4) do
        local r = t.player.walk_to(cands[i][1], cands[i][2], 25)
        local s = pillar_state(t)
        if s and s.tile and s.tile.x == cands[i][1] and s.tile.z == cands[i][2] then return true, cands[i] end
    end
    return false, nil
end
-- kill every monster of a kind list with the player's own attacks; returns the kills made
local function kill_loop(t, label, want_nib, budget, stop_wave)
    local start, kills = now(t), 0
    while now(t) - start < budget and alive(t) do
        local s = pillar_state(t)
        if not s or not s.active or s.wave ~= stop_wave or s.alive == 0 then break end
        eat_if_low(t, 50)
        pray_if_low(t)
        local list, nibs = others_alive(t)
        local pick = list[1]
        if pick == nil and want_nib then pick = nibs[1] end
        if pick == nil then break end
        local ar, ad = t.player.attack(pick.symbol, 2, 10, { slot = pick.client_slot })
        if pick.type == NIB then
            local lv = string.match(tostring(ad), "%(level%-(%d+)%)")
            if lv then D.levels[#D.levels + 1] = tonumber(lv) end
        end
        if ar == "ok" then
            local kr = t.npc.await_dead_engaged(4, 1, { eat = { item = "shark", below = 60 } })
            if kr == "ok" then kills = kills + 1 end
        else
            t.ticks(1)
        end
    end
    return kills
end


-- the first ticks of a wave: what the nibblers' records read (hitpoints, size) and which animation is drawn while one walks
-- and while one stands
local function sample_motion(t, ticks)
    local last = {}
    for i = 1, ticks do
        local _, _, pack = t.npc.pack(40)
        for _, p in ipairs(pack or {}) do
            if i == 1 and p.type == NIB then
                D.nib_hp[#D.nib_hp + 1] = p.max_hitpoints
                D.sizes[#D.sizes + 1] = p.size
            elseif i == 1 and p.type == PILLAR_NPC then
                D.pillar_sizes = D.pillar_sizes or {}
                D.pillar_sizes[#D.pillar_sizes + 1] = p.size
                D.pillar_hp_pack = D.pillar_hp_pack or {}
                D.pillar_hp_pack[#D.pillar_hp_pack + 1] = p.max_hitpoints
            end
            if p.type == NIB and p.client_slot >= 0 and not p.dying then
                local r, row = t.npc.state(p.symbol, { slot = p.client_slot })
                local prev = last[p.slot]
                if r == "ok" and prev then
                    if prev.x ~= p.x or prev.z ~= p.z then
                        D.walk_ids[#D.walk_ids + 1] = row.anim_id
                    elseif row.anim_id ~= NIB_ATTACK then
                        D.ready_ids[#D.ready_ids + 1] = row.anim_id
                    end
                end
                last[p.slot] = { x = p.x, z = p.z }
            end
        end
        t.ticks(1)
    end
end

local SAFESPOT = { w = "inferno_safespot1", s = "inferno_safespot2", e = "inferno_safespot3" }
local CHILD = { "inferno_safespot_100", "inferno_safespot_75", "inferno_safespot_50", "inferno_safespot_25" }
local VARB = { w = "varb5655_inferno_safespot1_health", s = "varb5656_inferno_safespot2_health", e = "varb5657_inferno_safespot3_health" }
-- one wave of the chain: poll the pillars every tick, kill the non-nibblers, stand at `stand` tiles from the chewed pillar,
-- wait for it to fall. last=true: the third fall, the nibblers then take the player for BITE_TICKS with no attack.
local BITE_TICKS = 56
local function chain_wave(t, w, stand, last, bx_in, bz_in, tag, lone)
    local key_w = tag or w
    local rec = { wave = w, series = {}, loc = {}, stand = stand, tag = key_w }
    D.waves[key_w] = rec
    D.order[#D.order + 1] = key_w
    local entry = { label = 'chain' .. w, bx = bx_in, bz = bz_in, rec = rec, t0 = now(t) }
    D.entries[#D.entries + 1] = entry
    rec.entry = entry
    local seen0 = now(t)
    t.ticklog.mark("chain.wave" .. key_w)
    local prev = nil
    local bx, bz = bx_in, bz_in
    local s0 = pillar_state(t)
    rec.start = {}
    for _, k in ipairs(KEYS) do
        if s0 and s0.pillars and not s0.pillars[k].dead then rec.start[#rec.start + 1] = s0.pillars[k].hp end
    end
    sample_motion(t, 8)
    local positioned = false
    local deadline = now(t) + 600
    local pstate = nil
    while now(t) < deadline and alive(t) do
        local s = pillar_state(t)
        if not s then break end
        if s.wave ~= w then break end
        local sample = { tick = s.tick }
        local fell = nil
        for _, k in ipairs(KEYS) do
            local p = s.pillars[k]
            sample[k] = p.hp
            sample[k .. "dead"] = p.dead
            if prev and (p.hp ~= prev[k] or p.dead ~= prev[k .. "dead"]) then
                local child = -1
                if not p.dead then
                    local guess = math.min(3, math.floor((255 - p.hp) / 64))
                    for _, off in ipairs({ 0, -1, 1, -2, 2, 3 }) do
                        local c = guess + off
                        if c >= 0 and c <= 3 and child < 0 then
                            local rl, lr = t.world.loc_near(CHILD[c + 1], 80)
                            if rl == "ok" and lr.tile_x == bx + PILLARS[k][1] and lr.tile_z == bz + PILLARS[k][2] then child = c end
                        end
                    end
                end
                local rv, vv = t.var.server(VARB[k])
                rec.loc[#rec.loc + 1] = { tick = s.tick, key = k, hp = p.hp, dead = p.dead, child = child, varb = (rv == "ok" and vv) or -1 }
            end
            if prev and p.dead and not prev[k .. "dead"] then fell = k end
            if prev and p.hp < prev[k] and not p.dead and rec.key == nil then
                rec.key, rec.first_loss = k, s.tick
                entry.key = k
            end
        end
        rec.series[#rec.series + 1] = sample
        if rec.key == nil and prev == nil then
            -- a loss already on the first read: found by the scan below
            for _, k in ipairs(KEYS) do if s.pillars[k].hp < 255 and not s.pillars[k].dead then rec.key = k rec.first_loss = s.tick entry.key = k end end
        end
        prev = sample
        if fell then
            rec.fell, rec.fell_key, rec.fell_tick = true, fell, s.tick
            rec.polled_fell = s.tick
            for _, lr in ipairs(rows(t, "loc_set")) do
                if lr.loc == -1 and lr.x == bx + PILLARS[fell][1] and lr.z == bz + PILLARS[fell][2] then rec.fell_tick = lr.tick end
            end
            rec.hp_at_fall = hp(t)
            t.exec("w" .. key_w .. ".pillar_fell", t.wave.state, "inferno")
            break
        end
        if lone and rec.key and not rec.lone_done then
            -- the player's own attacks cull the pack to ONE nibbler, which then chews its pillar alone
            local guard = 0
            while alive(t) and guard < 12 do
                guard = guard + 1
                local _, nb = others_alive(t)
                if #nb <= 1 then break end
                eat_if_low(t, 50)
                pray_if_low(t)
                local ar = t.player.attack(nb[1].symbol, 2, 10, { slot = nb[1].client_slot })
                if ar == "ok" then t.npc.await_dead_engaged(4, 1, { eat = { item = "shark", below = 60 } }) else t.ticks(1) end
            end
            rec.lone_done, rec.lone_tick = true, now(t)
        end
        local list, nibs = others_alive(t)
        if #list > 0 then
            kill_loop(t, "w" .. w, false, 120, w)
        else
            if rec.key and not positioned then
                local ok, tile = go_stand(t, bx, bz, rec.key, stand)
                positioned = true
                local s2 = pillar_state(t)
                local px, pz = bx + PILLARS[rec.key][1], bz + PILLARS[rec.key][2]
                rec.stand_tile = s2 and s2.tile and { s2.tile.x, s2.tile.z } or nil
                rec.stand_dist = s2 and s2.tile and foot_dist(s2.tile.x, s2.tile.z, px, pz) or -1
                rec.hp_stand = hp(t)
                t.exec("w" .. key_w .. ".stand", t.wave.state, "inferno")
            end
            eat_if_low(t, 60)
            pray_if_low(t)
            t.ticks(1)
        end
    end
    return rec
end

local function after_fall(t, rec, last)
    local ft = rec.fell_tick or now(t)
    rec.hits, rec.nhits = {}, {}
    for _, r in ipairs(rows(t, "hit_player")) do
        if r.tick >= ft - 3 and r.tick <= ft + 3 then rec.hits[#rec.hits + 1] = { tick = r.tick, damage = r.damage, type = r.npc_type } end
    end
    for _, r in ipairs(rows(t, "hit_npc")) do
        if r.tick >= ft - 3 and r.tick <= ft + 3 then rec.nhits[#rec.nhits + 1] = { tick = r.tick, damage = r.damage, type = r.type, slot = r.slot } end
    end
    rec.hp_after = hp(t)
    if last then
        rec.entry.all_down_tick = rec.fell_tick
        t.ticklog.mark("bites.begin")
        rec.bite_from = now(t)
        local stop = now(t) + BITE_TICKS
        local shot = false
        while now(t) < stop and alive(t) do
            eat_if_low(t, 40)
            t.ticks(1)
            if not shot and #rows(t, "hit_player") > 0 and now(t) > rec.bite_from + 12 then
                t.exec("w" .. rec.wave .. ".nibbler_bites_player", t.wave.state, "inferno")
                shot = true
            end
        end
        rec.bite_to = now(t)
    end
    rec.kills = kill_loop(t, "w" .. rec.wave, true, 260, rec.wave)
end


-- ---- the ledger: every row computed from the tick log and the polled pillar series ----------------------------------
local function spec_row(t, id, ok, measured, extra, specv, grade, tol)
    -- the shot writer cuts a name at 64 characters: a long id gets no fresh frame, so its picture is the previous row's (unchanged)
    if #id < 38 then t.ticks(1) end
    local detail = "measured " .. measured .. ((extra and extra ~= "") and (", " .. extra) or "") .. " (spec " .. specv .. ", grade " .. grade .. ", tol " .. tol .. ")"
    t.check("spec.nibblers_and_pillars." .. id, ok, detail)
end
local function uniq_counts(list)
    local seen, order = {}, {}
    for _, v in ipairs(list) do
        if seen[v] == nil then seen[v] = 0 order[#order + 1] = v end
        seen[v] = seen[v] + 1
    end
    table.sort(order)
    return order, seen
end
-- "4" for a list that is all 4, else "2,4" (the distinct values); counts as free text
local function distinct_text(list)
    local order = uniq_counts(list)
    local parts = {}
    for _, v in ipairs(order) do parts[#parts + 1] = tostring(v) end
    return table.concat(parts, ",")
end
local function all_in(list, lo, hi)
    for _, v in ipairs(list) do if v < lo or v > hi then return false end end
    return #list > 0
end
local function dist_text(list)
    local order, count = uniq_counts(list)
    local parts = {}
    for _, v in ipairs(order) do parts[#parts + 1] = v .. "x" .. count[v] end
    return table.concat(parts, " ")
end
local function mode_of(list)
    local order, count = uniq_counts(list)
    local best, bc = nil, -1
    for _, v in ipairs(order) do if count[v] > bc then best, bc = v, count[v] end end
    return best, bc
end

-- tile history of one nibbler slot inside an entry window: spawn tile then every npc_tile change
local function tile_at(hist, tick)
    local x, z = nil, nil
    for _, h in ipairs(hist) do
        if h.tick <= tick then x, z = h.x, h.z else break end
    end
    return x, z
end

local function analyse(t)
    local A = {
        bites = {}, gaps = {}, dist = {}, steps = {}, first = {}, loss_single = {}, seq_at_loss = {},
        player_bites = 0, player_hits = 0, player_dmg = {}, pillar_phase_hits = 0, kill_gap = {}, defend = {}, death_seq = {},
    }
    local anims = rows(t, "npc_anim", { type = NIB })
    local tiles = rows(t, "npc_tile", { type = NIB })
    local spawns = rows(t, "npc_spawn", { type = NIB })
    local deaths = rows(t, "npc_death", { type = NIB })
    local frees = rows(t, "npc_free", { type = NIB })
    local hit_player = rows(t, "hit_player")
    local by_tick = {}
    for _, r in ipairs(spawns) do by_tick[r.tick] = true end
    local ticks = {}
    for k in pairs(by_tick) do ticks[#ticks + 1] = k end
    table.sort(ticks)
    D.groups = ticks
    for gi, e in ipairs(D.entries) do
        t.ticks(1)
        local from = nil
        for _, g in ipairs(ticks) do
            if from == nil and g >= e.t0 - 12 and g > (A.last_s or -1) then from = g end
        end
        e.S = from or e.t0
        from = e.S
        A.last_s = from
        local to = 1e9
        for _, g in ipairs(ticks) do if g > from then to = g break end end
        e.to = to
        local px, pz = nil, nil
        if e.key then px, pz = e.bx + PILLARS[e.key][1], e.bz + PILLARS[e.key][2] end
        -- tile histories
        local hist = {}
        for _, r in ipairs(spawns) do if r.tick == from then hist[r.slot] = { { tick = r.tick, x = r.x, z = r.z } } end end
        for _, r in ipairs(tiles) do
            if r.tick >= from and r.tick < to and hist[r.slot] then
                local h = hist[r.slot]
                local last = h[#h]
                local step = cheb(last.x, last.z, r.x, r.z)
                A.steps[#A.steps + 1] = step
                h[#h + 1] = { tick = r.tick, x = r.x, z = r.z }
            end
        end
        e.hist = hist
        e.death = {}
        for _, d in ipairs(deaths) do if d.tick >= from and d.tick < to then e.death[d.slot] = d.tick end end
        -- bites of this entry, by slot
        local per_slot = {}
        local first_bite = nil
        for _, r in ipairs(anims) do
            if r.tick >= from and r.tick < to and hist[r.slot] then
                if r.seq == NIB_ATTACK then
                    local phase_player = (e.all_down_tick ~= nil and r.tick > e.all_down_tick)
                    if phase_player then
                        A.player_bites = A.player_bites + 1
                    else
                        if first_bite == nil or r.tick < first_bite then first_bite = r.tick end
                        if px then
                            local x, z = tile_at(hist[r.slot], r.tick)
                            A.dist[#A.dist + 1] = foot_dist(x, z, px, pz)
                        end
                    end
                    per_slot[r.slot] = per_slot[r.slot] or {}
                    local list = per_slot[r.slot]
                    list[#list + 1] = { tick = r.tick, player = phase_player }
                end
            end
        end
        for slot, list in pairs(per_slot) do
            for i = 2, #list do
                if list[i].player == list[i - 1].player and (list[i].tick - list[i - 1].tick) < 20 then
                    A.gaps[#A.gaps + 1] = list[i].tick - list[i - 1].tick
                elseif list[i].player == list[i - 1].player then
                    A.gap_long = (A.gap_long or "") .. string.format("%d@%d ", list[i].tick - list[i - 1].tick, list[i].tick)
                end
            end
        end
        if first_bite then A.first[#A.first + 1] = first_bite - from end
        e.first_bite = first_bite
        e.nb = 0
        for _, l in pairs(per_slot) do e.nb = e.nb + #l end
        note(t, "entry." .. e.label .. (e.tagged or ""), string.format("S %s to %s t0 %s key %s all_down %s bites %d first_bite %s", tostring(e.S), tostring(e.to), tostring(e.t0), tostring(e.key), tostring(e.all_down_tick), e.nb, tostring(first_bite)))
    end
    -- hits on the player
    for _, r in ipairs(hit_player) do
        if r.npc_type == NIB then
            local all_down = nil
            for _, e in ipairs(D.entries) do
                if r.tick >= e.S and r.tick < (e.to or 1e9) then all_down = e.all_down_tick end
            end
            if all_down ~= nil and r.tick > all_down then
                A.player_hits = A.player_hits + 1
                A.player_dmg[#A.player_dmg + 1] = r.damage
            else
                A.pillar_phase_hits = A.pillar_phase_hits + 1
            end
        end
    end
    -- a kill: killing blow tick (npc_death) to the free tick of the same slot
    local free_by = {}
    for _, r in ipairs(frees) do free_by[#free_by + 1] = r end
    for _, d in ipairs(deaths) do
        for _, f in ipairs(free_by) do
            if f.slot == d.slot and f.tick >= d.tick then A.kill_gap[#A.kill_gap + 1] = f.tick - d.tick break end
        end
        for _, r in ipairs(anims) do
            if r.slot == d.slot and r.tick >= d.tick - 1 and r.tick <= d.tick + 1 and r.seq ~= NIB_ATTACK then A.death_seq[#A.death_seq + 1] = r.seq end
        end
    end
    A.anims, A.hit_player, A.deaths = anims, hit_player, deaths
    A.anim_at = {}
    for _, r in ipairs(anims) do
        A.anim_at[r.tick] = A.anim_at[r.tick] or {}
        local l = A.anim_at[r.tick]
        l[#l + 1] = r
    end
    return A
end

-- hp-loss events of one wave's polled series: {tick, key, delta}, only between samples on consecutive ticks
local function loss_events(rec)
    local events = {}
    local s = rec.series
    for i = 2, #s do
        if s[i].tick == s[i - 1].tick + 1 then
            for _, k in ipairs(KEYS) do
                local d = s[i - 1][k] - s[i][k]
                if d > 0 and not s[i][k .. "dead"] and not s[i - 1][k .. "dead"] then events[#events + 1] = { tick = s[i].tick, key = k, delta = d } end
            end
        end
    end
    return events
end
local function sample_map(rec)
    local have = {}
    for _, smp in ipairs(rec.series) do have[smp.tick] = true end
    return have
end
local function has_samples(have, a, b)
    for k = a, b do if not have[k] then return false end end
    return true
end
-- bites on a tick, within one entry's pillar phase
local function bite_counts(A, entry)
    local c = {}
    for _, r in ipairs(A.anims) do
        if r.seq == NIB_ATTACK and r.tick >= (entry.S or 0) and r.tick < (entry.to or 1e9)
            and not (entry.all_down_tick and r.tick > entry.all_down_tick) then
            c[r.tick] = (c[r.tick] or 0) + 1
        end
    end
    return c
end

local function emit_pillar_rows(t, A)
    local per_wave_distinct, hp_start, losses_all, gaps_all, single, offsets, seq_loss = {}, {}, {}, {}, {}, {}, {}
    local gaps_by_adj = { {}, {}, {} }
    for _, wk in ipairs(D.order) do
        t.ticks(1)
        local w = wk
        local rec = D.waves[wk]
        if rec and #rec.series > 0 then
            local first = rec.series[1]
            local distinct = 0
            for _, k in ipairs(KEYS) do
                if not first[k .. "dead"] then
                    hp_start[#hp_start + 1] = math.max(first[k], 0)
                    local minhp, dead = 255, false
                    for _, smp in ipairs(rec.series) do
                        minhp = math.min(minhp, smp[k])
                        if smp[k .. "dead"] then dead = true end
                    end
                    if minhp < 255 or dead then distinct = distinct + 1 end
                end
            end
            per_wave_distinct[#per_wave_distinct + 1] = distinct
            local ev = loss_events(rec)
            local counts = bite_counts(A, rec.entry)
            local by_key = {}
            for _, e in ipairs(ev) do
                by_key[e.key] = by_key[e.key] or {}
                local list = by_key[e.key]
                list[#list + 1] = e
                local n0, n1 = counts[e.tick] or 0, counts[e.tick - 1] or 0
                offsets[#offsets + 1] = (n0 > 0) and 0 or ((n1 > 0) and 1 or -1)
                if n0 == 1 then single[#single + 1] = e.delta end
                for _, r in ipairs(A.anim_at[e.tick] or {}) do seq_loss[#seq_loss + 1] = r.seq end
            end
            local wave_gaps = {}
            local have = sample_map(rec)
            for _, list in pairs(by_key) do
                for i = 2, #list do
                    if has_samples(have, list[i - 1].tick, list[i].tick) then
                        gaps_all[#gaps_all + 1] = list[i].tick - list[i - 1].tick
                        local adj = 0
                        local ent = rec.entry
                        if ent and ent.hist then
                            local px, pz = ent.bx + PILLARS[list[i].key][1], ent.bz + PILLARS[list[i].key][2]
                            for slot, h in pairs(ent.hist) do
                                local nx, nz = tile_at(h, list[i].tick)
                                local dt = ent.death and ent.death[slot]
                                if nx and not (dt and dt <= list[i].tick) and foot_dist(nx, nz, px, pz) == 1 then adj = adj + 1 end
                            end
                        end
                        local bucket = math.max(1, math.min(3, adj))
                        if adj >= 1 then gaps_by_adj[bucket][#gaps_by_adj[bucket] + 1] = list[i].tick - list[i - 1].tick end
                        wave_gaps[#wave_gaps + 1] = list[i].tick - list[i - 1].tick
                    end
                end
            end
            local wm, wc = mode_of(wave_gaps)
            A.gap_modes = (A.gap_modes or "") .. string.format("entry %s mode %s (%d of %d) ", tostring(w), tostring(wm), wc or 0, #wave_gaps)
        end
    end
    A.distinct, A.single, A.gaps_hp, A.hp_start, A.offsets, A.seq_loss = per_wave_distinct, single, gaps_all, hp_start, offsets, seq_loss
    A.gaps_adj = gaps_by_adj
end

local function emit_rows(t, A)
    emit_pillar_rows(t, A)
    local w3 = D.waves[3]
    -- target_distinct
    spec_row(t, "nibbler_pillar_target_distinct", #A.distinct >= 3 and all_in(A.distinct, 1, 1), distinct_text(A.distinct),
        "pillars chewed in waves 1,2,3 of a real run: " .. join(A.distinct), "1 count", "B", "exact")
    -- after the pillars
    local first_hit = nil
    for _, r in ipairs(A.hit_player) do
        if r.npc_type == NIB and w3 and w3.fell_tick and r.tick > w3.fell_tick and (first_hit == nil or r.tick < first_hit) then first_hit = r.tick end
    end
    spec_row(t, "nibbler_attack_player_after_pillars", A.player_hits > 0, A.player_hits > 0 and "1" or "0",
        string.format("%d hit_player rows from nibblers after the third pillar fell on tick %s, first on tick %s", A.player_hits, tostring(w3 and w3.fell_tick), tostring(first_hit)),
        "1 count", "C", "exact")
    local kills_standing = 0
    for _, d in ipairs(A.deaths) do if w3 and w3.fell_tick and d.tick < w3.fell_tick then kills_standing = kills_standing + 1 end end
    spec_row(t, "nibbler_player_hits_while_pillar_stands", A.pillar_phase_hits == 0 and kills_standing > 0, tostring(A.pillar_phase_hits),
        string.format("hit_player rows from nibblers while a pillar stood, with %d nibblers killed by the player in that time", kills_standing), "0 count", "C", "exact")
    spec_row(t, "nibbler_first_pillar_hit", all_in(A.first, 6, 14), join(A.first),
        "first bite (seq 7574) tick minus the spawn tick (the wave message), one per wave entry", "6-14 ticks", "B", "range")
    spec_row(t, "nibbler_attack_distance", #A.dist > 0 and all_in(A.dist, 1, 1), distinct_text(A.dist),
        string.format("%d of %d bites from the footprint's ring", (function() local n = 0 for _, v in ipairs(A.dist) do if v == 1 then n = n + 1 end end return n end)(), #A.dist), "1 tiles", "B", "exact")
    local stepmax = maxof(A.steps)
    spec_row(t, "nibbler_step_max", stepmax ~= nil and stepmax <= 1, tostring(stepmax), string.format("%d tile changes, none longer", #A.steps), "1 tiles", "B", "range")
    spec_row(t, "nibbler_attack_speed", #A.gaps > 0 and all_in(A.gaps, 4, 4), distinct_text(A.gaps),
        string.format("%d gaps between consecutive bites of one nibbler (idle spans of 20+ ticks left out: %s)", #A.gaps, A.gap_long or "none"), "4 ticks", "C", "exact")
    local specs = { { "pillar_hit_gap_modal", 4, "one nibbler adjacent" }, { "pillar_hit_gap_modal_two_adjacent", 2, "two adjacent" }, { "pillar_hit_gap_modal_three_adjacent", 2, "three or more adjacent" } }
    for bi, sp in ipairs(specs) do
        local gl = A.gaps_adj[bi]
        local m, mc = mode_of(gl)
        spec_row(t, sp[1], m == sp[2], tostring(m), string.format("%d of %d gaps between hp-loss ticks of one pillar with %s at the later tick, adjacency from npc_tile rows (gap x count: %s)", mc or 0, #gl, sp[3], dist_text(gl)), sp[2] .. " ticks", "B", "exact")
    end
    spec_row(t, "nibbler_hitpoints", #D.nib_hp > 0 and all_in(D.nib_hp, 10, 10), distinct_text(D.nib_hp), string.format("%d nibblers read from the pack", #D.nib_hp), "10 hp", "C", "exact")
    spec_row(t, "nibbler_combat_level", #D.levels > 0 and all_in(D.levels, 32, 32), distinct_text(D.levels), string.format("%d attack menus read '(level-N)'", #D.levels), "32 count", "A", "exact")
    spec_row(t, "nibbler_size", #D.sizes > 0 and all_in(D.sizes, 1, 1), distinct_text(D.sizes), string.format("%d nibbler records", #D.sizes), "1 tiles", "D", "exact")
    local mx = maxof(A.player_dmg)
    spec_row(t, "nibbler_max_hit", mx == 4, tostring(mx), string.format("%d hits on the player, none above", #A.player_dmg), "4 hp", "C", "exact")
    local pct = A.player_bites > 0 and (100 * A.player_hits / A.player_bites) or -1
    spec_row(t, "nibbler_accuracy_vs_player", pct == 100, string.format("%.0f", pct), string.format("%d hit_player rows (zeros counted) for %d bites after the pillars fell, no armour, no prayer", A.player_hits, A.player_bites), "100 percent", "C", "exact")
    spec_row(t, "nibbler_despawn_after_lethal_hit", all_in(A.kill_gap, 2, 5), join(A.kill_gap), "npc_death (the killing blow) to npc_free of the slot, one per kill", "2-5 ticks", "B", "range")
end

-- the multiloc child each polled damage showed: the smallest damage at which each child (75, 50, 25) was seen
local function threshold_values()
    local lo, hi, mism, nsamp = { 1e9, 1e9, 1e9, 1e9 }, { -1, -1, -1, -1 }, 0, 0
    for _, rec in pairs(D.waves) do
        for _, l in ipairs(rec.loc) do
            if not l.dead and l.child >= 0 then
                local dmg = 255 - l.hp
                nsamp = nsamp + 1
                if l.varb >= 0 and dmg > 0 and l.varb ~= dmg then mism = mism + 1 end
                lo[l.child + 1] = math.min(lo[l.child + 1], dmg)
                hi[l.child + 1] = math.max(hi[l.child + 1], dmg)
            end
        end
    end
    return lo, hi, mism, nsamp, string.format("%d,%d,%d", lo[2], lo[3], lo[4])
end
local function threshold_rows(t)
    local lo, hi, mism, nsamp, text = threshold_values()
    local near = {}
    for _, rec in pairs(D.waves) do
        for _, l in ipairs(rec.loc) do
            local dmg = 255 - l.hp
            if dmg >= 188 and dmg <= 196 then near[#near + 1] = dmg .. ":" .. l.child end
        end
    end
    table.sort(near)
    note(t, "thresholds.near192", join(near, nil, 60))
    spec_row(t, "pillar_damage_state_thresholds", text == "64,128,192", text,
        string.format("%d samples; child 100 seen up to damage %d, 75 from %d to %d, 50 from %d to %d, 25 from %d (varbit5655-7 vs 255-hp: %d mismatches)",
            nsamp, hi[1], lo[2], hi[2], lo[3], hi[3], lo[4], mism), "64,128,192 hp", "A", "exact")
end

local function emit_rows2(t, A)
    local falls = {}
    for _, wk in ipairs(D.order) do
        local rec = D.waves[wk]
        if rec and rec.fell then falls[#falls + 1] = rec end
    end
    -- pillars
    local hps, sizes = {}, {}
    for _, wk in ipairs(D.order) do local rec = D.waves[wk] if rec and rec.start then for _, v in ipairs(rec.start) do hps[#hps + 1] = v end end end
    for _, v in ipairs(D.pillar_hp_pack or {}) do hps[#hps + 1] = v end
    spec_row(t, "pillar_hitpoints", #hps > 0 and all_in(hps, 255, 255), distinct_text(hps), string.format("%d reads (the hp var at each wave start, the npc record's max)", #hps), "255 hp", "C", "exact")
    spec_row(t, "pillar_size", #(D.pillar_sizes or {}) > 0 and all_in(D.pillar_sizes, 3, 3), distinct_text(D.pillar_sizes or {}), string.format("%d pillar npc records", #(D.pillar_sizes or {})), "3 tiles", "A", "exact")
    -- pillar_moves: the pillar npcs' tile rows against their spawn tile
    local spawn_at, moved, nrows = {}, 0, 0
    for _, r in ipairs(rows(t, "npc_spawn", { type = PILLAR_NPC })) do spawn_at[r.slot] = spawn_at[r.slot] or { r.x, r.z } end
    for _, r in ipairs(rows(t, "npc_tile", { type = PILLAR_NPC })) do
        nrows = nrows + 1
        local sp = spawn_at[r.slot]
        if sp then moved = math.max(moved, cheb(sp[1], sp[2], r.x, r.z)) end
    end
    spec_row(t, "pillar_moves", true and moved == 0, tostring(moved), string.format("%d npc_tile rows of the pillar npcs against their spawn tiles", nrows), "0 tiles", "B", "exact")
    spec_row(t, "pillar_damage_per_hit", #A.single > 0 and all_in(A.single, 2, 4), distinct_text(A.single), string.format("%d single-bite ticks (one nibbler bit that tick), the pillar's hp delta", #A.single), "2-4 hp", "D", "range")
    spec_row(t, "pillar_damage_observed_per_hit", #A.single > 0 and all_in(A.single, 1, 4), distinct_text(A.single), string.format("%d single-bite ticks", #A.single), "1-4 hp", "B", "range")
    threshold_rows(t)
    -- the fall
    local hp_at = {}
    for _, rec in ipairs(falls) do
        local last = rec.series[#rec.series]
        hp_at[#hp_at + 1] = last and last[rec.fell_key] or -1
    end
    spec_row(t, "pillar_collapse_hp", #hp_at > 0 and all_in(hp_at, 0, 0), distinct_text(hp_at), string.format("%d falls, the hp var on the tick the dead flag first read 1", #hp_at), "0 hp", "D", "exact")
    local dmgd, undmg, parts = 0, 99, {}
    local radius = -1
    for _, rec in ipairs(falls) do
        local dmg = 0
        for _, h in ipairs(rec.hits or {}) do if h.type ~= NIB then dmg = dmg + h.damage end end
        parts[#parts + 1] = string.format("entry %s: player %d from the footprint, took %d", tostring(rec.tag), rec.stand_dist or -1, dmg)
        if dmg > 0 then radius = math.max(radius, rec.stand_dist or -1) else undmg = math.min(undmg, rec.stand_dist or 99) end
    end
    spec_row(t, "pillar_collapse_radius", radius == 1 and undmg > radius, tostring(radius),
        "largest Chebyshev distance from the footprint at which a fall hurt the player; " .. table.concat(parts, "; "), "1 tiles", "C", "exact")
    local mid = {}
    for _, rec in ipairs(falls) do
        local d = 0
        for _, h in ipairs(rec.hits or {}) do if h.type ~= NIB then d = d + h.damage end end
        mid[#mid + 1] = d
    end
    spec_row(t, "pillar_collapse_damage_to_player_midwave", #mid > 0, join(mid), "hp taken at the fall of each nibbler-chewed pillar (one per fall; the stand distance is in the radius row); approximation, M18", "? hp", "E", "approx")
    local mon = {}
    for _, rec in ipairs(falls) do
        local d = 0
        for _, h in ipairs(rec.nhits or {}) do d = d + h.damage end
        mon[#mon + 1] = d
    end
    spec_row(t, "pillar_collapse_damage_to_monsters", #mon > 0, join(mon), "hp the adjacent nibblers took in the fall ticks (hit_npc rows); approximation, M18", "? hp", "E", "approx")
    -- sequences
    local sl = mode_of(A.seq_loss)
    spec_row(t, "nibbler_attack_seq", sl == 7574, tostring(sl), string.format("the sequence the nibbler plays on the tick the pillar loses hp (%d hp-loss ticks)", #A.seq_loss), "7574 count", "D", "exact")
    local defend, death = {}, {}
    local dead_at = {}
    for _, d in ipairs(A.deaths) do dead_at[d.slot .. ":" .. d.tick] = true end
    for _, h in ipairs(rows(t, "hit_npc", { type = NIB })) do
        local lethal = dead_at[h.slot .. ":" .. h.tick] == true
        for tk = h.tick, h.tick + 1 do
            for _, r in ipairs(A.anim_at[tk] or {}) do
                if r.slot == h.slot and r.seq ~= NIB_ATTACK then
                    if lethal then death[#death + 1] = r.seq else defend[#defend + 1] = r.seq end
                end
            end
        end
    end
    local dset = {}
    for _, v in ipairs(defend) do dset[v] = true end
    local death_only = {}
    for _, v in ipairs(death) do if not dset[v] then death_only[#death_only + 1] = v end end
    spec_row(t, "nibbler_death_seq", #death_only > 0 and all_in(death_only, 7576, 7576), distinct_text(death_only), string.format("%d lethal hits, the sequence sent with each", #death_only), "7576 count", "D", "exact")
    spec_row(t, "nibbler_defend_seq", #defend > 0, distinct_text(defend), string.format("%d non-lethal hits on a nibbler, the sequence sent with each; approximation, M20", #defend), "? count", "E", "approx")
end

-- ---- practice entries -------------------------------------------------------------------------------------------------
local function practice_entry(t, wave, stand, tag, lone)
    t.exec("enter." .. tag, t.wave.enter, "inferno", wave, { restart = true })
    if not alive(t) then return nil end
    t.prayer.set("protectfrommissiles", true)
    local bx, bz = arena_base(t)
    if bx == nil then return nil end
    local rec = chain_wave(t, wave, stand, false, bx, bz, tag, lone)
    if rec.fell then after_fall(t, rec, false) end
    note(t, "practice." .. tag, string.format("wave %d key %s first_loss %s fell %s stand_dist %s", wave, tostring(rec.key), tostring(rec.first_loss), tostring(rec.fell_tick), tostring(rec.stand_dist)))
    return rec
end

-- T4: a pillar between the player and the bat. A ring tile the server's line of sight says the bat cannot see, held while the
-- bat comes for the player: no swing, no hit.
local function bat_safespot_attempt(t, attempt)
    t.exec("enter.safespot" .. attempt, t.wave.enter, "inferno", 1, { restart = true })
    t.prayer.set("protectfrommissiles", true)
    local bx, bz = arena_base(t)
    if bx == nil then return false end
    local _, _, pack = t.npc.pack(40)
    local bat = nil
    for _, p in ipairs(pack or {}) do if p.type == 7692 then bat = p end end
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
            if p.type == 7692 then samples = samples + 1 if p.sees_player then seen_n = seen_n + 1 end end
        end
        eat_if_low(t, 50)
        pray_if_low(t)
        t.ticks(1)
    end
    t.exec("safespot.held", t.wave.state, "inferno")
    local hits, swings = 0, 0
    for _, h in ipairs(rows(t, "hit_player")) do if h.tick >= mark and h.npc_type == 7692 then hits = hits + 1 end end
    for _, a in ipairs(rows(t, "npc_anim", { type = 7692 })) do if a.tick >= mark and a.seq == 7578 then swings = swings + 1 end end
    D.safespot = { hits = hits, swings = swings, seen = seen_n, samples = samples, tile = best }
    local ok = hits == 0 and swings == 0 and samples > 0
    local text = string.format("attempt %d: held %d ticks on tile %d,%d behind pillar %s: the bat (type 7692) made %d swings (seq 7578) and %d hit_player rows; its sees_player was true in %d of %d reads",
        attempt, now(t) - mark, best[2], best[3], best[1], swings, hits, seen_n, samples)
    if ok then t.check("technique.pillar_safespot", true, text) else note(t, "safespot.attempt" .. attempt, text) end
    return ok
end
local function bat_safespot(t)
    for attempt = 1, 8 do
        if not alive(t) then return end
        if bat_safespot_attempt(t, attempt) then kill_loop(t, "safespot", true, 220, 1) return end
    end
    t.check("technique.pillar_safespot", false, "eight attempts at a ring tile the bat could not see, each reached by the bat and swung at; see safespot.attempt rows")
end

-- T1 / T2: Ice Barrage cast at the middle nibbler of the pack as soon as the wave is up. One cast should take all three: three
-- npc_death rows on one tick, and the pillars untouched afterwards.
local function barrage_attempt(t, n)
    t.exec("enter.barrage" .. n, t.wave.enter, "inferno", 1, { restart = true })
    t.prayer.set("protectfrommissiles", true)
    pray_if_low(t)
    local _, _, pack = t.npc.pack(40)
    local nibs = {}
    for _, p in ipairs(pack or {}) do if p.type == NIB and p.client_slot >= 0 then nibs[#nibs + 1] = p end end
    if #nibs < 3 then return false, "pack has " .. #nibs .. " nibblers" end
    -- the barrage covers the 3x3 round its target: the middle one of the pack is the nibbler the other two stand next to
    local mid, cover = nil, -1
    for _, p in ipairs(nibs) do
        local c = 0
        for _, q in ipairs(nibs) do if cheb(p.x, p.z, q.x, q.z) <= 1 then c = c + 1 end end
        if c > cover then mid, cover = p, c end
    end
    if cover < 3 then
        D.barrage_skipped = (D.barrage_skipped or 0) + 1
        return false, "skip"
    end
    local pre = now(t)
    t.ticklog.mark("barrage.cast" .. n)
    local r = t.player.cast("ice_barrage", "inferno_nibbler", 10, 2, { slot = mid.client_slot })
    t.ticks(8)
    local by_tick = {}
    for _, d in ipairs(rows(t, "npc_death", { type = NIB })) do
        if d.tick >= pre then by_tick[d.tick] = (by_tick[d.tick] or 0) + 1 end
    end
    local best, best_tick = 0, -1
    for tk, c in pairs(by_tick) do if c > best then best, best_tick = c, tk end end
    local s = pillar_state(t)
    local php = {}
    for _, k in ipairs(KEYS) do php[#php + 1] = s.pillars[k].hp end
    local text = string.format("attempt %d: cast %s at the middle nibbler (slot %d), %d nibbler deaths on tick %d (cast sent on tick %d), pillar hp %s eight ticks on",
        n, tostring(r), mid.slot, best, best_tick, pre, join(php))
    local all_dead = best >= 3
    local intact = php[1] == 255 and php[2] == 255 and php[3] == 255
    D.barrage = D.barrage or {}
    D.barrage[#D.barrage + 1] = { dead = best, intact = intact }
    if all_dead and intact then
        t.check("technique.ice_barrage_pack_one_cast", true, text)
        t.check("technique.three_deaths_one_tick", true, string.format("three npc_death rows of nibblers on server tick %d from the one cast of attempt %d", best_tick, n))
    else
        note(t, "barrage.attempt" .. n, text)
    end
    kill_loop(t, "barrage" .. n, true, 220, 1)
    return all_dead and intact, text
end
local function barrage_pack(t)
    local casts = 0
    for n = 1, 40 do
        if not alive(t) or casts >= 14 then break end
        local ok, why = barrage_attempt(t, n)
        if why ~= "skip" then casts = casts + 1 end
        if ok then return end
    end
    note(t, "barrage.skipped", string.format("%d entries had no nibbler with the other two on its 3x3 (not cast); %d casts made", D.barrage_skipped or 0, casts))
    t.check("technique.ice_barrage_pack_one_cast", false, string.format("%d casts at a nibbler the pack stands round never took all three nibblers on one tick; see barrage.attempt rows", casts))
end

-- wave 67 and 68: the pillars are gone
local function no_pillars(t, wave)
    t.exec("enter.w" .. wave, t.wave.enter, "inferno", wave, { restart = true })
    t.prayer.set("protectfrommagic", true)
    local s = pillar_state(t)
    local standing = 0
    for _, k in ipairs(KEYS) do if s and s.pillars and not s.pillars[k].dead and s.pillars[k].hp > 0 then standing = standing + 1 end end
    local _, _, pack = t.npc.pack(40)
    local npcs = 0
    for _, p in ipairs(pack or {}) do if p.type == PILLAR_NPC then npcs = npcs + 1 end end
    return standing + npcs
end

local function emit_rows3(t, A)
    -- presentation of a fall: the rows the log carries on the fall tick
    local seqs, spawns, locs = {}, {}, {}
    for _, r in ipairs(rows(t, "npc_anim", { seq = 7561 })) do seqs[#seqs + 1] = r.tick end
    for _, r in ipairs(rows(t, "npc_spawn", { type = DYING_NPC })) do spawns[#spawns + 1] = r.tick end
    for _, r in ipairs(rows(t, "loc_set")) do if r.loc == -1 then locs[#locs + 1] = r.tick end end
    spec_row(t, "pillar_fall_presentation", #seqs > 0 and #spawns > 0 and #locs > 0, "7561,7710",
        string.format("per fall: seq 7561 on %d ticks, npc 7710 spawned on %d ticks, the loc removed on %d ticks; the sound is not a tick-log row (no sound kind), unread; approximation, M21", #seqs, #spawns, #locs),
        "? count", "E", "approx")
    spec_row(t, "nibbler_sounds", true, "?", "unread: the tick log carries no sound kind and no verb reads one, so no attack, hurt or death sound id is measured; approximation, M19", "? count", "E", "approx")
end

local function thresholds_exact()
    local _, _, _, _, text = threshold_values()
    return text == "64,128,192"
end

return {
    id = "inferno_nibblers_and_pillars",
    fixture = "fresh_lumbridge.ini",
    max_frames = 150000,
    setup = {
        "::clearinv", "::setlevel attack 99", "::setlevel strength 99", "::setlevel ranged 99", "::setlevel magic 99",
        "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::give twisted_bow", "::give rune_arrow 1500", "::give 4doseprayerrestore 5", "::give shark 16",
        "::give bloodrune 40", "::give deathrune 70", "::give waterrune 120",
        "::setvar varp172_option_nodef 1", "::setvar varb4070_spellbook 1",
        -- staging (labelled): a real run paused at wave 1 (seam pass 4's staging)
        "::setvar varb5646_inferno_sacrificed_firecape 2", "::setvar varp6056_inferno_paused 1", "::setvar varp6065_inferno_saved_wave 1",
    },
    run = function(t)
        t.ticklog.start()
        t.check("spec.scope", true, "mode=normal party=1")
        local er = t.player.equip("twisted_bow")
        local ar = t.player.equip("rune_arrow")
        note(t, "equip", string.format("bow %s arrows %s", tostring(er), tostring(ar)))
        t.exec("goto.entrance_edge", t.player.goto_tile, 2495, 5123, 0)
        local _, mark_text = t.ticklog.mark("run.begin")
        D.since = tonumber(string.match(tostring(mark_text), "serial (%d+)"))
        local rr, rd, rs = t.wave.resume()
        t.check("resume1", rr == "ok" and rs and rs.wave == 1 and rs.active and not rs.practice, tostring(rr) .. ": " .. tostring(rd))
        t.ticks(6)
        do
            local rs2, rd2, rec = t.npc.record("inferno_nibbler", { need = "server" })
            D.rec = rec
            note(t, "record", tostring(rs2) .. ": " .. tostring(rd2))
        end
        t.exec("prayer.missiles", t.prayer.set, "protectfrommissiles", true)
        local bx, bz = arena_base(t)
        if bx == nil then t.check("base", false, "no three pillar npcs") t.finish(1) return end
        note(t, "arena", string.format("arena base %d,%d (pillar sw tiles from the npc pack) tick %d", bx, bz, now(t)))
        D.bx, D.bz = bx, bz
        -- the real run: waves 1, 2, 3; one pillar falls in each, the player standing 1, 2 and 3 tiles from it
        local plan = { { 1, 1, false }, { 2, 2, false }, { 3, 3, true } }
        for _, p in ipairs(plan) do
            if not alive(t) then break end
            local st = pillar_state(t)
            if st.wave ~= p[1] then t.wave.await_wave(p[1], 40) end
            local rec = chain_wave(t, p[1], p[2], p[3], bx, bz)
            if rec.fell then after_fall(t, rec, p[3]) end
            note(t, "chain.w" .. p[1], string.format("key %s first_loss %s fell %s tick %s stand_dist %s hits %d nhits %d", tostring(rec.key), tostring(rec.first_loss), tostring(rec.fell_key), tostring(rec.fell_tick), tostring(rec.stand_dist), #(rec.hits or {}), #(rec.nhits or {})))
        end
        -- practice entries of wave 1: more first bites, more falls, more damage states (until every multiloc threshold is read exactly)
        if alive(t) then practice_entry(t, 1, 1, "lone", true) end
        local stands = { 1, 2, 1, 2, 1, 3, 1, 2, 1, 2, 1, 3, 1, 2, 1, 2 }
        local n = 0
        while alive(t) and n < #stands and (n < 2 or not thresholds_exact()) do
            n = n + 1
            practice_entry(t, 1, stands[n], "p" .. n)
        end
        if alive(t) then bat_safespot(t) end
        if alive(t) then barrage_pack(t) end
        local A = analyse(t)
        emit_rows(t, A)
        emit_rows2(t, A)
        emit_rows3(t, A)
        local rec = D.rec or {}
        local sv, cl = rec.server or {}, rec.client or {}
        spec_row(t, "nibbler_defence_level", sv.defence == 15, tostring(sv.defence), "server record of jalnib, read statically", "15 count", "C", "exact")
        spec_row(t, "nibbler_magic_level", sv.magic == 15, tostring(sv.magic), "server record of jalnib, read statically", "15 count", "D", "exact")
        spec_row(t, "nibbler_attack_strength_ranged_levels", sv.attack == 1 and sv.strength == 1 and sv.ranged == 1,
            string.format("%s,%s,%s", tostring(sv.attack), tostring(sv.strength), tostring(sv.ranged)), "server record of jalnib: attack, strength, ranged", "1,1,1 count", "D", "exact")
        spec_row(t, "nibbler_walk_ready_seq", cl.walkanim == 7572 and cl.readyanim == 7573,
            string.format("%s,%s", tostring(cl.walkanim), tostring(cl.readyanim)), "client cache record of the nibbler: walkanim, readyanim", "7572,7573 count", "A", "exact")
        return
    end,
}
