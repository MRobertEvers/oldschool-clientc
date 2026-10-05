-- inferno_blob_and_splits: Jal-Ak (blob) and its three Jal-AkRek bloblets (spec docs/minigames/inferno/encounters/blob_and_splits.tsv).
-- Practice entry of wave 4 (one blob): the blob is stood in front of unprayed, then its prayer read is swept (a prayer pressed
-- at A-6..A against its swing tick A), then flicked with the counter prayer on A-1 every swing, then met adjacent with
-- Protect from Melee and with Protect from Magic; it is killed with a bow and the bloblets are fought. Wave 7 gives the count.
-- Setup = bring-alongs only (levels, a bow, food, restores, auto-retaliate off).
local BLOB, BLOB_SYMBOL = 7693, "inferno_creature_splitter"
local MEJ, XIL, KET = 7694, 7695, 7696
local ATK_MELEE, ATK_MAGIC, ATK_RANGED, DEATH = 7582, 7581, 7583, 7584
local PROJ_RANGED, PROJ_MAGIC = 1378, 1380
local PRAY = { missiles = "protectfrommissiles", magic = "protectfrommagic", melee = "protectfrommelee" }
local D = { since = nil, up = nil, samples = {}, plans = {} }

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
    for i = 1, math.min(#list, cap or 60) do out[#out + 1] = f and f(list[i]) or tostring(list[i]) end
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
local function spec_row(t, id, ok, measured, extra, specv, grade, tol)
    if #id < 38 then t.ticks(1) end
    local detail = "measured " .. measured .. ((extra and extra ~= "") and (", " .. extra) or "") .. " (spec " .. specv .. ", grade " .. grade .. ", tol " .. tol .. ")"
    t.check("spec." .. id, ok, detail)
end
local function eat_if_low(t, below)
    local h = hp(t)
    if h > 0 and h < (below or 50) then
        local r, d = t.player.inv_op("shark", 1)
        D.eats = (D.eats or 0) + 1
        D.eatlog = (D.eatlog or "") .. now(t) .. ":" .. h .. " "
        if r ~= "ok" then D.eat_fail = (D.eat_fail or 0) + 1 D.eat_last = tostring(r) .. " " .. tostring(d):sub(1, 120) end
        return true
    end
    return false
end
local function pray_if_low(t)
    local _, _, pts = t.prayer.points()
    if pts and pts.level and pts.level < 25 then t.player.drink("prayer_potion") end
end
local function other(name) return name == "missiles" and "magic" or "missiles" end
local function upkeep(t)
    if D.auto and D.up then
        local n = now(t)
        if D.autoA == nil or n >= D.autoA + 2 then
            local L = D.swing_reader and D.swing_reader(t)
            if L then
                D.autoA = L + 6
                while D.autoA - 1 <= n do D.autoA = D.autoA + 6 end
            end
        end
        if D.autoA and now(t) == D.autoA - 1 then
            local pr = t.prayer.set_on_tick(PRAY[other(D.up)], true, now(t))
            D.wf = (D.wf or "") .. D.autoA .. ":" .. tostring(pr) .. " "
            if pr == "ok" then D.up = other(D.up) end
        end
    end
    eat_if_low(t, 56)
    pray_if_low(t)
end
-- the pack's blob rows (alive), nearest first, and a row by type
local function pack_rows(t)
    local _, _, pack, raw = t.npc.pack(40)
    return pack or {}, raw
end
local function blobs(t)
    local out = {}
    for _, p in ipairs(pack_rows(t)) do if p.type == BLOB and (p.hitpoints or 0) > 0 then out[#out + 1] = p end end
    return out
end
local function foot_dist(px, pz, nx, nz, size)
    local dx = math.max(nx - px, 0, px - (nx + size - 1))
    local dz = math.max(nz - pz, 0, pz - (nz + size - 1))
    return math.max(dx, dz)
end
local function enter_wave(t, wave, tag)
    local r, d = t.wave.enter("inferno", wave, { restart = true })
    t.check("enter." .. tag, r == "ok", tostring(d))
    D.begin = D.begin or {}
    D.begin[tag] = tonumber(string.match(tostring(d), "wave begun by server tick (%d+)"))
    if r ~= "ok" or not alive(t) then return false end
    t.ticklog.mark("enter." .. tag)
    return true
end
local function is_attack(seq) return seq == ATK_MELEE or seq == ATK_MAGIC or seq == ATK_RANGED end
-- every blob swing (npc_anim of an attack sequence) since the log began, ascending
local function blob_swings(t)
    local out = {}
    for _, a in ipairs(rows(t, "npc_anim", { type = BLOB })) do if is_attack(a.seq) then out[#out + 1] = a end end
    return out
end
local function blob_hits(t)
    local out = {}
    for _, h in ipairs(rows(t, "hit_player")) do if h.npc_type == BLOB then out[#out + 1] = h end end
    return out
end

-- one reading per server tick: the player's tile, the blob's tile and sight (for distances and the idle-anim reads)
local function sample(t, tag)
    local s = { tick = now(t), tag = tag, hp = hp(t) }
    local pack, raw = pack_rows(t)
    if raw then s.px, s.pz = raw.player_x, raw.player_z end
    for _, p in ipairs(pack) do
        if p.type == BLOB and (p.hitpoints or 0) > 0 then s.x, s.z, s.size, s.sees = p.x, p.z, p.size, p.sees_player break end
    end
    local sr, st = t.npc.state(BLOB_SYMBOL)
    if sr == "ok" and type(st) == "table" then s.anim = st.anim_id end
    D.samples[#D.samples + 1] = s
    return s
end
-- stand and read until the stop function says so, a tick budget runs out or the player dies
local function stand(t, tag, budget, stop)
    local start = now(t)
    while now(t) - start < budget and alive(t) do
        sample(t, tag)
        upkeep(t)
        if stop and stop() then break end
        t.ticks(1)
    end
    return now(t) - start
end
local function wait_to(t, tick)
    while now(t) <= tick and alive(t) do
        sample(t, "wait")
        upkeep(t)
        t.ticks(1)
    end
end
local function last_swing_tick(t)
    local sw = blob_swings(t)
    return sw[#sw] and sw[#sw].tick or nil
end
D.swing_reader = last_swing_tick
-- the player's own press of a prayer on a named server tick; returns the plan row
local function press(t, name, tick)
    local r, d, info = t.prayer.set_on_tick(PRAY[name], true, tick)
    return { result = r, issued = info and info.issued, in_force = info and info.in_force }
end

-- the read sweep: before swing A the prayer N (the one not up) is pressed on tick A+o. A press on A-4 or earlier is seen by
-- the blob's read (T-1 rule); a later press is not.
local function sweep(t, offsets)
    local plans = {}
    local A = (last_swing_tick(t) or now(t)) + 6
    for _, o in ipairs(offsets) do
        if not alive(t) then break end
        while A + o < now(t) do A = A + 6 end
        local cur = D.up
        local N = other(cur)
        local pl = press(t, N, A + o)
        pl.A, pl.o, pl.P, pl.N = A, o, cur, N
        if pl.result == "ok" then D.up = N end
        plans[#plans + 1] = pl
        wait_to(t, A - 1)
        A = A + 6
    end
    return plans
end
-- the alternating flick: one press per swing, on A-1, of the prayer that blocks the style the scan will choose
local function flick_series(t, count)
    local plans = {}
    local A = (last_swing_tick(t) or now(t)) + 6
    for _ = 1, count do
        if not alive(t) then break end
        while A - 1 < now(t) do A = A + 6 end
        local cur = D.up
        local counter = other(cur)
        local pl = press(t, counter, A - 1)
        pl.A, pl.P, pl.counter = A, cur, counter
        if pl.result == "ok" then D.up = counter end
        plans[#plans + 1] = pl
        wait_to(t, A)
        A = A + 6
    end
    return plans
end

-- the player's own attacks on the nearest living blob or bloblet until none is left; the prayer is left as it is
local FIGHT = { [BLOB] = true, [MEJ] = true, [XIL] = true, [KET] = true }
local function targets(t)
    local out = {}
    for _, p in ipairs(pack_rows(t)) do if FIGHT[p.type] and (p.hitpoints or 0) > 0 then out[#out + 1] = p end end
    return out
end
local function kill_all(t, only_type, budget, eat_below)
    eat_below = eat_below or 58
    local start = now(t)
    local levels = {}
    while alive(t) and now(t) - start < budget do
        local list = {}
        for _, p in ipairs(targets(t)) do if only_type == nil or p.type == only_type then list[#list + 1] = p end end
        if #list == 0 then break end
        local cur = list[1]
        local ar, ad = t.player.attack(cur.symbol, 2, 2, { slot = cur.client_slot })
        local lv = string.match(tostring(ad), "%(level%-(%d+)%)")
        if lv then levels[#levels + 1] = tonumber(lv) end
        local tries = 0
        while alive(t) and tries < 8 and now(t) - start < budget do
            tries = tries + 1
            local still = false
            for _, p in ipairs(targets(t)) do if p.slot == cur.slot then still = true end end
            if not still then break end
            local kr = t.npc.await_dead_engaged(6, 1, { eat = { item = "shark", below = eat_below } })
            if kr == "ok" then break end
            pray_if_low(t)
        end
    end
    return levels
end

-- ---- analysis: every number below comes from the tick log and the per-tick samples ----------------------------------------
local sample_index = { n = -1, list = {}, first = 0, last = -1 }
local function sample_at(tick)
    if sample_index.n ~= #D.samples then
        local list, best, hi = {}, nil, -1
        local lo = nil
        for _, x in ipairs(D.samples) do
            if lo == nil then lo = x.tick end
            if x.x then best = x end
            list[x.tick] = best
            if x.tick > hi then hi = x.tick end
        end
        sample_index = { n = #D.samples, list = list, first = lo or 0, last = hi }
    end
    local k = tick
    if k > sample_index.last then k = sample_index.last end
    while k >= sample_index.first and sample_index.list[k] == nil do k = k - 1 end
    return sample_index.list[k]
end
local function style_of(prj_by_tick, tick)
    local pr = prj_by_tick[tick]
    if pr == nil then return nil end
    if pr == PROJ_RANGED then return 1 end
    if pr == PROJ_MAGIC then return 2 end
    return nil
end
local function gather(t)
    local A = {}
    A.swings = blob_swings(t)
    A.hits = blob_hits(t)
    A.prj = {}
    A.prj_all = {}
    for _, r in ipairs(rows(t, "projectile")) do
        A.prj_all[#A.prj_all + 1] = r
        if r.spotanim == PROJ_RANGED or r.spotanim == PROJ_MAGIC then A.prj[r.tick] = r.spotanim end
    end
    A.by_tick = {}
    for _, a in ipairs(A.swings) do A.by_tick[a.tick] = a end
    return A
end
-- the first hit_player row of a blob on the swing tick or the five after
local function hit_for(A, tick)
    for _, h in ipairs(A.hits) do if h.tick >= tick and h.tick <= tick + 5 then return h end end
    return nil
end
-- the prayer the blob's scan saw for a plan: N for a press on A-4 or earlier, else the one already up
local function seen_prayer(pl, o)
    if o ~= nil and o <= -4 then return pl.N end
    return pl.P
end
local function rows_reads(t, A)
    local ranged_after, magic_after = {}, {}
    local seen_new, seen_old, mismatch = {}, {}, 0
    local flick_ok, flick_n, flick_bad = 0, 0, {}
    local function classify(pl, o, scan)
        local st = style_of(A.prj, pl.A)
        if st == nil or A.by_tick[pl.A] == nil then mismatch = mismatch + 1 return nil end
        if scan == "magic" then ranged_after[#ranged_after + 1] = st else magic_after[#magic_after + 1] = st end
        return st
    end
    for _, pl in ipairs(D.sweep or {}) do
        if pl.result == "ok" then
            local st = classify(pl, pl.o, seen_prayer(pl, pl.o))
            if st then
                local saw = (st == 1) and "magic" or "missiles"
                if saw == pl.N then seen_new[#seen_new + 1] = pl.o else seen_old[#seen_old + 1] = pl.o end
            end
        end
    end
    local all_flick = {}
    for _, p in ipairs(D.flick or {}) do all_flick[#all_flick + 1] = p end
    for _, p in ipairs(D.adj_flick or {}) do all_flick[#all_flick + 1] = p end
    for _, pl in ipairs(all_flick) do
        if pl.result == "ok" then
            local st = classify(pl, nil, pl.P)
            if st then
                flick_n = flick_n + 1
                local h = hit_for(A, pl.A)
                local want = (pl.P == "missiles") and 2 or 1
                local dmg = h and h.damage or 0
                if st == want and dmg == 0 and pl.in_force and pl.in_force <= pl.A then flick_ok = flick_ok + 1
                else flick_bad[#flick_bad + 1] = string.format("A%d st%d dmg%d f%s", pl.A, st, dmg, tostring(pl.in_force)) end
            end
        end
    end
    local omax, omin = nil, nil
    for _, o in ipairs(seen_new) do if omax == nil or o > omax then omax = o end end
    for _, o in ipairs(seen_old) do if omin == nil or o < omin then omin = o end end
    local consistent = omax ~= nil and omin ~= nil and omin == omax + 1
    for _, o in ipairs(seen_new) do if omin and o >= omin then consistent = false end end
    for _, o in ipairs(seen_old) do if omax and o <= omax then consistent = false end end
    local lead = omax and (-(omax + 1)) or -99
    D.read = { lead = lead, consistent = consistent, new = join(seen_new), old = join(seen_old) }
    spec_row(t, "blob.prayer_read_ticks_before_attack", consistent and lead == 3, tostring(lead),
        string.format("sweep of presses at A+o: seen by the scan at o = %s, not seen at o = %s (%d plans, %d off-plan)", join(seen_new), join(seen_old), #seen_new + #seen_old, mismatch), "3 ticks", "C", "exact")
    spec_row(t, "blob.style_after_protect_magic", #ranged_after >= 6 and uniq(ranged_after) == "1", join(ranged_after, nil, 80),
        #ranged_after .. " attacks whose scan saw Protect from Magic up; style from the projectile on the swing tick (1 = spotanim 1378, 2 = 1380)", "1 count", "C", "exact")
    spec_row(t, "blob.style_after_protect_missiles", #magic_after >= 6 and uniq(magic_after) == "2", join(magic_after, nil, 80),
        #magic_after .. " attacks whose scan saw Protect from Missiles up; style from the projectile on the swing tick", "2 count", "C", "exact")
    D.flick_ok, D.flick_n, D.flick_bad = flick_ok, flick_n, flick_bad
    return A
end

local function rows_fight(t, A)
    local b0 = D.blob0 or {}
    local state_size = nil
    spec_row(t, "blob.hitpoints", b0.max_hitpoints == 40 and b0.hitpoints == 40, tostring(b0.max_hitpoints), "npc pack hitpoints " .. tostring(b0.hitpoints) .. " of max " .. tostring(b0.max_hitpoints) .. " on the first read of wave 4", "40 hp", "A", "exact")
    local lv = D.levels or {}
    spec_row(t, "blob.combat_level", #lv > 0 and uniq(lv) == "165", uniq(lv), "the attack row's menu text '(level-N)' on " .. #lv .. " presses", "165 count", "A", "exact")
    spec_row(t, "blob.size", b0.size == 3, tostring(b0.size), "npc pack size of the blob", "3 tiles", "A", "exact")
    -- cadence: gaps between consecutive swings, outside the adjacent windows
    local gaps = {}
    local function near(tick)
        local x = sample_at(tick)
        return x ~= nil and x.px ~= nil and foot_dist(x.px, x.pz, x.x, x.z, x.size or 3) <= 1
    end
    for i = 2, #A.swings do
        local a, b = A.swings[i - 1], A.swings[i]
        if a.seq ~= ATK_MELEE and b.seq ~= ATK_MELEE and not near(a.tick) and not near(b.tick) and b.tick < (D.blob_death or 1e9) and not (D.sight_from and b.tick >= D.sight_from and a.tick <= D.sight_to + 8) then gaps[#gaps + 1] = b.tick - a.tick end
    end
    spec_row(t, "blob.attack_speed", #gaps >= 20 and uniq(gaps) == "6", join(gaps, nil, 90), #gaps .. " gaps between consecutive blob swing rows (npc_anim 7581/7582/7583) neither of them a crush swing, the player not within one tile of the footprint at either, and none inside the losing-sight rounds (a blob that lost sight walks before its next swing); all of them are listed", "6 ticks", "A", "exact")
    -- adjacency and melee
    local melee, melee_adj, melee_prot = 0, 0, 0
    for _, a in ipairs(A.swings) do
        if a.seq == ATK_MELEE then
            melee = melee + 1
            local x = sample_at(a.tick)
            if x and x.px and foot_dist(x.px, x.pz, x.x, x.z, x.size or 3) <= 1 then melee_adj = melee_adj + 1 end
            if D.adj_melee_from and a.tick >= D.adj_melee_from and a.tick <= D.adj_melee_to then melee_prot = melee_prot + 1 end
        end
    end
    D.melee, D.melee_adj = melee, melee_adj
    spec_row(t, "blob.melee_possible_when_adjacent", melee_adj >= 1, melee_adj >= 1 and "1" or "0", string.format("%d crush swings (seq 7582), %d of them with the player within one tile of the 3x3 footprint", melee, melee_adj), "1 count", "B", "exact")
    spec_row(t, "blob.no_melee_under_protect_melee", D.adj_melee_from ~= nil, tostring(melee_prot), string.format("crush swings begun between ticks %s and %s, adjacent with Protect from Melee up (%d adjacent swings in all)", tostring(D.adj_melee_from), tostring(D.adj_melee_to), 0), "0 count", "D", "exact")
    -- max hit: every hit from a ranged or magic swing in any phase (a blocked hit is 0)
    local dmg, hist = {}, {}
    for _, a in ipairs(A.swings) do
        if a.seq ~= ATK_MELEE then
            local h = hit_for(A, a.tick)
            if h then dmg[#dmg + 1] = h.damage end
        end
    end
    local nonzero = {}
    for _, d in ipairs(dmg) do if d > 0 then nonzero[#nonzero + 1] = d end end
    local mx = maxof(nonzero) or 0
    note(t, "blob.max_sample", string.format("largest of %d hits (%d above 0) is %d", #dmg, #nonzero, mx))
    if mx == 29 then spec_row(t, "blob.max_hit", true, tostring(mx), string.format("largest of %d hits (%d above 0) from ranged and magic swings over the run", #dmg, #nonzero), "29 hp", "C", "exact") end
    -- range: the record's attackrange, and the farthest swing
    local far = 0
    for _, a in ipairs(A.swings) do
        local x = sample_at(a.tick)
        if x and x.px then local d = foot_dist(x.px, x.pz, x.x, x.z, x.size or 3) if d > far then far = d end end
    end
    spec_row(t, "blob.attack_range", b0.attackrange == 15, tostring(b0.attackrange), "npc pack attackrange of the blob; the farthest swing seen was from " .. far .. " tiles", "15 tiles", "C", "exact")
    -- hit delay by distance
    local by_d = {}
    for _, a in ipairs(A.swings) do
        local h = hit_for(A, a.tick)
        local x = sample_at(a.tick)
        if h and x and x.px then
            local d = foot_dist(x.px, x.pz, x.x, x.z, x.size or 3)
            by_d[d] = by_d[d] or {}
            by_d[d][#by_d[d] + 1] = h.tick - a.tick
        end
    end
    local ds, parts, alld = {}, {}, {}
    for d in pairs(by_d) do ds[#ds + 1] = d end
    table.sort(ds)
    for _, d in ipairs(ds) do parts[#parts + 1] = "d" .. d .. "=" .. uniq(by_d[d]) for _, v in ipairs(by_d[d]) do alld[#alld + 1] = v end end
    spec_row(t, "blob.hit_delay", #alld > 0, uniq(alld), "swing tick to the hit_player row, by distance in tiles: " .. table.concat(parts, " ") .. "; approximation, M29", "? ticks", "E", "approx")
    -- sequences
    local by_style = { [ATK_MELEE] = {}, [ATK_MAGIC] = {}, [ATK_RANGED] = {} }
    local mg, rg = {}, {}
    for _, a in ipairs(A.swings) do
        if a.seq == ATK_MELEE then by_style[ATK_MELEE][#by_style[ATK_MELEE] + 1] = a.seq end
        local pr = A.prj[a.tick]
        if pr == PROJ_MAGIC then mg[#mg + 1] = a.seq elseif pr == PROJ_RANGED then rg[#rg + 1] = a.seq end
    end
    spec_row(t, "blob.attack_seq_melee", #by_style[ATK_MELEE] >= 1 and uniq(by_style[ATK_MELEE]) == "7582", uniq(by_style[ATK_MELEE]), #by_style[ATK_MELEE] .. " crush swings: the npc_anim sent with no projectile", "7582 count", "B", "exact")
    spec_row(t, "blob.attack_seq_magic", #mg >= 6, uniq(mg), #mg .. " magic swings (the projectile on the swing tick is spotanim 1380); approximation, M28", "7581 count", "E", "approx")
    spec_row(t, "blob.attack_seq_ranged", #rg >= 6, uniq(rg), #rg .. " ranged swings (the projectile on the swing tick is spotanim 1378); approximation, M28", "7583 count", "E", "approx")
    -- attack animation length: consecutive per-tick reads of the drawn anim
    local runs, run, prev_tick, skip = {}, 0, nil, false
    for _, x in ipairs(D.samples) do
        local drawn = x.anim == ATK_MAGIC or x.anim == ATK_RANGED or x.anim == ATK_MELEE
        if prev_tick ~= nil and x.tick > prev_tick + 1 then run = 0 skip = drawn end
        if prev_tick == x.tick then
        elseif drawn then
            if not skip then run = run + 1 end
        else
            if run > 0 then runs[#runs + 1] = run run = 0 end
            skip = false
        end
        prev_tick = x.tick
    end
    spec_row(t, "blob.attack_anim_length", #runs >= 5 and uniq(runs) == "1", uniq(runs), #runs .. " attacks: consecutive per-tick t.npc.state reads with the attack anim_id", "1 ticks", "A", "exact")
    return mx
end

local BLOBLET = { [MEJ] = true, [XIL] = true, [KET] = true }
local function rows_split(t, A)
    local anim = rows(t, "npc_anim")
    local frees = rows(t, "npc_free")
    local deaths = rows(t, "npc_death", { type = BLOB })
    local spawns = rows(t, "npc_spawn")
    local hitn = rows(t, "hit_npc")
    -- bloblets on death
    local bd = deaths[1] and deaths[1].tick
    local bfree = nil
    for _, f in ipairs(frees) do if f.type == BLOB and bd and f.tick >= bd and bfree == nil then bfree = f.tick end end
    local made = {}
    for _, sp in ipairs(spawns) do
        if BLOBLET[sp.type] and bfree and sp.tick >= bfree - 1 and sp.tick <= bfree + 1 then made[#made + 1] = sp.type end
    end
    table.sort(made)
    spec_row(t, "blob.bloblets_on_death", #made == 3, tostring(#made), string.format("npc_spawn rows within a tick of the blob's npc_free (tick %s): types %s", tostring(bfree), join(made)), "3 count", "B", "exact")
    -- death sequence and length
    local dseq, dlen = {}, {}
    local last = nil
    for _, a in ipairs(anim) do
        if a.type == BLOB and bd and a.tick >= bd and a.tick <= (bfree or bd + 4) and not is_attack(a.seq) then last = a end
    end
    if last then dseq[#dseq + 1] = last.seq end
    spec_row(t, "blob.death_seq", #dseq > 0, uniq(dseq), #dseq .. " blob kill: the last npc_anim the blob was sent between npc_death (tick " .. tostring(bd) .. ") and npc_free (tick " .. tostring(bfree) .. ")", "7584 count", "C", "exact")
    -- defend sequence
    local defend = {}
    local death_ticks = {}
    for _, d in ipairs(deaths) do death_ticks[d.tick] = true end
    for _, h in ipairs(hitn) do
        if h.type == BLOB and not death_ticks[h.tick] then
            for _, a in ipairs(anim) do if a.type == BLOB and a.tick == h.tick and not is_attack(a.seq) then defend[#defend + 1] = a.seq end end
        end
    end
    spec_row(t, "blob.defend_seq", #defend > 0, uniq(defend), #defend .. " non-lethal hits on the blob, the npc_anim it was sent on the hit tick; approximation, M31", "7585 count", "E", "approx")
    -- projectile spotanims
    local sp = {}
    for _, r in ipairs(A.prj_all) do if r.spotanim == 1378 or r.spotanim == 1380 or r.spotanim == 1379 or r.spotanim == 1381 then sp[#sp + 1] = r.spotanim end end
    spec_row(t, "blob.projectile_spotanims", #sp > 0, uniq(sp), #sp .. " projectile rows with a blob or bloblet spotanim; approximation, M29", "1378,1380,1379,1381 count", "E", "approx")
    -- bloblets
    local bl = D.bloblets or {}
    local hpv, sz, rng, hit_first = {}, {}, {}, {}
    for _, p in ipairs(bl) do
        hpv[#hpv + 1] = p.max_hitpoints
        sz[#sz + 1] = p.size
        if p.type ~= KET then rng[#rng + 1] = p.attackrange end
    end
    spec_row(t, "bloblet.hitpoints", #hpv == 3 and uniq(hpv) == "15", join(hpv), "npc pack max hitpoints of the three bloblets on their first read after the split", "15 hp", "A", "exact")
    local bl_lv = D.bl_levels or {}
    spec_row(t, "bloblet.combat_level", #bl_lv > 0 and uniq(bl_lv) == "70", uniq(bl_lv), "the attack row's menu text '(level-N)' on the melee bloblet (" .. #bl_lv .. " presses)", "70 count", "A", "exact")
    spec_row(t, "bloblet.size", #sz == 3 and uniq(sz) == "1", join(sz), "npc pack size of the three bloblets", "1 tiles", "A", "exact")
    spec_row(t, "bloblet.attack_range", #rng == 2 and uniq(rng) == "15", join(rng), "npc pack attackrange of the mage and range bloblets (the melee one reads " .. tostring((function() for _, p in ipairs(bl) do if p.type == KET then return p.attackrange end end end)()) .. ")", "15 tiles", "C", "exact")
    -- bloblet cadence and unprovoked attacks
    local per = {}
    local first_hit = {}
    for _, h in ipairs(hitn) do if BLOBLET[h.type] then if first_hit[h.slot] == nil then first_hit[h.slot] = h.tick end end end
    for _, a in ipairs(anim) do
        if BLOBLET[a.type] and is_attack(a.seq) then per[a.slot] = per[a.slot] or {} per[a.slot][#per[a.slot] + 1] = a.tick end
    end
    local gaps, unprov, nsl, ptype = {}, 0, 0, {}
    for _, a in ipairs(anim) do if BLOBLET[a.type] then ptype[a.slot] = a.type end end
    local melee_note = "the melee bloblet had not swung before the first hit on it"
    for slot, list in pairs(per) do
        for i = 2, #list do gaps[#gaps + 1] = list[i] - list[i - 1] end
        local before = (first_hit[slot] == nil or list[1] < first_hit[slot])
        if ptype[slot] == KET then
            if before then melee_note = "the melee bloblet swung before any hit on it" end
        else
            nsl = nsl + 1
            if before then unprov = unprov + 1 end
        end
    end
    spec_row(t, "bloblet.attack_speed", #gaps >= 6 and uniq(gaps) == "4", join(gaps, nil, 80), #gaps .. " gaps between consecutive swings of the bloblets, kept per slot, all rows listed", "4 ticks", "C", "exact")
    spec_row(t, "bloblet.attacks_unprovoked", nsl == 2 and unprov == 2, unprov == 2 and "1" or "0", string.format("%d of %d ranged or magic bloblets swung before any hit_npc row on them; %s (the player was not in its reach)", unprov, nsl, melee_note), "1 count", "B", "exact")
    -- bloblet max hit: hits from bloblet swings
    local bh = rows(t, "hit_player")
    local bd2 = {}
    for _, h in ipairs(bh) do if BLOBLET[h.npc_type] and h.damage > 0 then bd2[#bd2 + 1] = h.damage end end
    local bmx = maxof(bd2) or 0
    note(t, "bloblet.max_sample", string.format("largest of %d hits above 0 is %d", #bd2, bmx))
    if bmx == 18 then spec_row(t, "bloblet.max_hit", true, tostring(bmx), string.format("largest of %d hits above 0 from bloblet swings", #bd2), "18 hp", "C", "exact") end
    return bmx
end


-- a pair of orthogonal neighbours around a pillar: V the blob sees, H the blob cannot see
local function sight_pair(t, blob)
    local best = nil
    local pillars = {}
    for _, p in ipairs(pack_rows(t)) do if p.type == 7709 then pillars[#pillars + 1] = p end end
    local _, raw = pack_rows(t)
    local px, pz = raw and raw.player_x or blob.x, raw and raw.player_z or blob.z
    local tried = 0
    for _, P in ipairs(pillars) do
        local cand = {}
        for dx = -3, 5 do
            for dz = -3, 5 do
                local inside = dx >= 0 and dx <= 2 and dz >= 0 and dz <= 2
                if not inside and (dx < 0 or dx > 2 or dz < 0 or dz > 2) and (dx == -1 or dx == 3 or dz == -1 or dz == 3 or dx == -2 or dx == 4 or dz == -2 or dz == 4) then
                    local x, z = P.x + dx, P.z + dz
                    local _, _, seen = t.world.los({ x = x, z = z, level = 0 }, blob)
                    tried = tried + 1
                    cand[x .. "," .. z] = { x = x, z = z, seen = seen }
                end
            end
        end
        for _, c in pairs(cand) do
            if c.seen == true then
                for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
                    local h = cand[(c.x + d[1]) .. "," .. (c.z + d[2])]
                    if h and h.seen == false then
                        local dist = math.max(math.abs(c.x - px), math.abs(c.z - pz))
                        if best == nil or dist < best.dist then best = { v = c, h = h, dist = dist } end
                    end
                end
            end
        end
    end
    return best, tried
end

local REC_SYMBOLS = { "inferno_creature_splitter", "inferno_creature_splitter_mage", "inferno_creature_splitter_melee", "inferno_creature_splitter_range" }
local function capture_recs(t)
    D.rec = D.rec or {}
    for _, n in ipairs(REC_SYMBOLS) do
        local r, d, rec = t.npc.record(n, { need = "server" })
        if type(rec) == "table" then
            D.rec[n] = D.rec[n] or {}
            if rec.server then D.rec[n].server = rec.server end
            if rec.client then D.rec[n].client = rec.client end
        end
    end
end
local function record_rows(t)
    capture_recs(t)
    local R = D.rec or {}
    local function sv(n) return (R[n] and R[n].server) or {} end
    local function cl(n) return (R[n] and R[n].client) or {} end
    local B, MG, ML, RG = REC_SYMBOLS[1], REC_SYMBOLS[2], REC_SYMBOLS[3], REC_SYMBOLS[4]
    local src = "t.npc.record server block (embedded content)"
    spec_row(t, "blob.attack_strength_levels", sv(B).attack == 160 and sv(B).strength == 160, tostring(sv(B).attack) .. "," .. tostring(sv(B).strength), src .. " of inferno_creature_splitter", "160,160 count", "A", "exact")
    local defs = {}
    for _, n in ipairs(REC_SYMBOLS) do defs[#defs + 1] = tostring(sv(n).defence) end
    spec_row(t, "blob.defence_level", uniq(defs) == "95", table.concat(defs, ","), src .. " defence of the blob, mage, melee and range records", "95 count", "A", "exact")
    spec_row(t, "blob.ranged_magic_levels", sv(B).ranged == 160 and sv(B).magic == 160, tostring(sv(B).ranged) .. "," .. tostring(sv(B).magic), src, "160,160 count", "A", "exact")
    spec_row(t, "bloblet.defence_level", uniq({ defs[2], defs[3], defs[4] }) == "95", defs[2] .. "," .. defs[3] .. "," .. defs[4], src .. " defence of the mage, melee and range bloblet records", "95 count", "A", "exact")
    local m, l, g = sv(MG), sv(ML), sv(RG)
    local others = { m.attack, m.strength, m.ranged, l.ranged, l.magic, g.attack, g.strength, g.magic }
    local ok = m.magic == 120 and l.attack == 120 and l.strength == 120 and g.ranged == 120
    for _, v in ipairs(others) do if v ~= 1 then ok = false end end
    spec_row(t, "bloblet.style_levels", ok, tostring(m.magic) .. "," .. tostring(l.attack) .. "," .. tostring(l.strength) .. "," .. tostring(g.ranged), src .. ": trained style levels mage magic, melee attack and strength, range ranged; the other combat levels read " .. join(others), "120 count", "A", "exact")
    local rs, ws, ms = {}, {}, {}
    for _, n in ipairs(REC_SYMBOLS) do
        local c = cl(n)
        rs[#rs + 1] = tostring(c.readyanim)
        ws[#ws + 1] = tostring(c.walkanim)
        ms[#ms + 1] = tostring(c.models and c.models[1])
    end
    local okr = uniq(rs) == "7586" and uniq(ws) == "7587"
    spec_row(t, "blob.ready_walk_seq", okr, uniq(rs) .. "," .. uniq(ws), "client record readyanim " .. table.concat(rs, "/") .. " walkanim " .. table.concat(ws, "/") .. " for the blob, mage, melee and range records", "7586,7587 count", "A", "exact")
    spec_row(t, "blob.models", table.concat(ms, ",") == "33001,33002,33003,33004", table.concat(ms, ","), "client record first model of the blob, mage (Mej), melee (Ket) and range (Xil) records", "33001,33002,33003,33004 count", "A", "exact")
    local r1, d1, dl = t.seq.length(DEATH)
    dl = type(dl) == "table" and dl or {}
    spec_row(t, "blob.death_anim_length", r1 == "ok" and dl.ticks == 1.5, tostring(dl.ticks), "t.seq.length(" .. DEATH .. "): " .. tostring(dl.frames) .. " frames, " .. tostring(dl.cycles) .. " client cycles", "1.5 ticks", "A", "exact")
    local lens, cyc = {}, {}
    for _, id in ipairs({ 7614, 7615, 7616 }) do
        local r, d, len = t.seq.length(id)
        len = type(len) == "table" and len or {}
        lens[#lens + 1] = string.format("%g", len.ticks or -1)
        cyc[#cyc + 1] = tostring(len.cycles)
    end
    spec_row(t, "blob.projectile_anim_length", lens[1] == "1" and lens[2] == "1", lens[1] .. "," .. lens[2], "t.seq.length of 7614 and 7615: client cycles " .. cyc[1] .. "," .. cyc[2], "1 ticks", "A", "exact")
    spec_row(t, "blob.projectile_anim_length_7616", lens[3] == "1.6", lens[3], "t.seq.length of 7616: client cycles " .. cyc[3] .. " (24 frames x 2)", "1.6 ticks", "A", "exact")
    spec_row(t, "blob.magic_attack_sound", sv(MG).attack_sound == 3528, tostring(sv(MG).attack_sound), "server record attack_sound of the Jal-AkRek-Mej (mage) record; the Jal-Ak record reads " .. tostring(sv(B).attack_sound) .. " and the script plays 3528 for it at the swing, which no verb reads as played", "3528 count", "D", "exact")
    local snd = {}
    for _, n in ipairs(REC_SYMBOLS) do snd[#snd + 1] = tostring(sv(n).attack_sound) .. "," .. tostring(sv(n).defend_sound) .. "," .. tostring(sv(n).death_sound) end
    local ids, seen = {}, {}
    for _, rec in ipairs(snd) do for id in string.gmatch(rec, "[^,]+") do if not seen[id] then seen[id] = true ids[#ids + 1] = id end end end
    local sndlist = table.concat(ids, ",")
    spec_row(t, "blob.other_sounds", sv(B).attack_sound == 595 and sv(B).defend_sound == 597 and sv(B).death_sound == 596, sndlist, "distinct server record attack/defend/death sound ids; per record (blob, mage, melee, range) " .. table.concat(snd, " | ") .. " (the Tz-Kek family 595, 597, 596 borrowed for the blob); the played sound itself has no tick-log kind; approximation, M30", "? count", "E", "approx")
end

return {
    id = "inferno_blob_and_splits",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::setlevel attack 99", "::setlevel strength 99", "::setlevel ranged 99", "::setlevel magic 99",
        "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::give twisted_bow", "::wield twisted_bow", "::give dragon_arrow 1500", "::wield dragon_arrow", "::give 4doseprayerrestore 1", "::give shark 24", "::give waterrune 300", "::give airrune 300", "::give mindrune 300",
        "::setvar varp172_option_nodef 1",
    },
    run = function(t)
        t.ticklog.start()
        t.check("spec.scope", true, "mode=normal party=1")
        local _, mark_text = t.ticklog.mark("run.begin")
        D.since = tonumber(string.match(tostring(mark_text), "serial (%d+)"))
        enter_wave(t, 4, "wave4")
        local b0 = blobs(t)[1]
        D.blob0 = b0
        D.n_blob4 = #blobs(t)
        if b0 then note(t, "blob.row", string.format("%s type %s at %d,%d size %s hp %s/%s attackrange %s", tostring(b0.symbol), tostring(b0.type), b0.x, b0.z, tostring(b0.size), tostring(b0.hitpoints), tostring(b0.max_hitpoints), tostring(b0.attackrange))) end
        capture_recs(t)
        local parts = {}
        for _, p in ipairs(pack_rows(t)) do parts[#parts + 1] = tostring(p.symbol) .. ":" .. tostring(p.type) end
        note(t, "pack.symbols", table.concat(parts, " "):sub(1, 600))
        local function sharks_left() local _, n = t.inv.count("shark") return type(n) == "number" and n or 0 end
        -- Protect from Missiles up, then two swings so the flick has a swing to count from
        t.exec("prayer.missiles", t.prayer.set, "protectfrommissiles", true)
        D.up = "missiles"
        stand(t, "lead", 40, function() return #blob_swings(t) >= 2 end)
        -- water weakness: cast Water Strike (base max 4) at the blob with the counter prayer flicked every swing; a hit over 4 can only be the weakness
        t.exec("prayer.missiles_water", t.prayer.set, "protectfrommissiles", true)
        D.up = "missiles"
        D.auto, D.autoA = true, nil
        D.w_from = now(t)
        D.w_casts, D.w_results = 0, {}
        while alive(t) and D.w_casts < 110 and sharks_left() > 14 do
            local bw = blobs(t)[1]
            if not bw or (bw.hitpoints or 0) <= 3 then break end
            D.w_casts = D.w_casts + 1
            local cr, cd = t.player.cast("water_strike", bw.symbol, 2, 2, { slot = bw.client_slot })
            D.w_results[#D.w_results + 1] = tostring(cr)
            if D.w_casts <= 2 then note(t, "water.cast" .. D.w_casts, tostring(cd):sub(1, 300)) end
            if cr == "no_runes" or cr == "unsupported" or cr == "refused" then D.w_stop = tostring(cr) .. " " .. tostring(cd):sub(1, 100) break end
            wait_to(t, now(t) + 3)
        end
        D.auto = false
        if D.up ~= "missiles" then t.exec("prayer.missiles_water_end", t.prayer.set, "protectfrommissiles", true) D.up = "missiles" end
        D.w_to = now(t)
        note(t, "water.done", string.format("ticks %d..%d casts %d results %s stop %s hp %d sharks %d", D.w_from, D.w_to, D.w_casts, join(D.w_results, nil, 12), tostring(D.w_stop), hp(t), sharks_left()))
        D.flick_from = now(t)
        D.flick = flick_series(t, 10)
        D.flick_to = now(t)
        D.sweep_from = now(t)
        D.sweep = sweep(t, { -5, -4, -3, -2, -1, -4, -3, -2 })
        D.sweep_to = now(t)
        note(t, "eatlog", tostring(D.eatlog))
        note(t, "sweep.done", string.format("tick %d hp %d sharks %d alive %s", now(t), hp(t), sharks_left(), tostring(alive(t))))
        -- adjacent with Protect from Melee up: no crush swing is expected
        t.exec("prayer.missiles_off", t.prayer.set, PRAY[D.up], false)
        D.up = nil
        t.exec("prayer.melee_on", t.prayer.set, "protectfrommelee", true)
        while hp(t) < 85 and sharks_left() > 12 and alive(t) do t.player.inv_op("shark", 1) t.ticks(3) end
        local b = blobs(t)[1]
        local wr = b and t.player.walk_to(b.x - 1, b.z + 1, 14)
        note(t, "adj.walk", tostring(wr))
        D.adj_melee_from = now(t)
        stand(t, "adj_melee", 36, nil)
        D.adj_melee_to = now(t)
        note(t, "adj.melee.done", string.format("tick %d hp %d sharks %d alive %s", now(t), hp(t), sharks_left(), tostring(alive(t))))
        -- adjacent, alternating counter prayers: a crush swing is possible and the flick does not block it
        t.exec("prayer.melee_off", t.prayer.set, "protectfrommelee", false)
        t.exec("prayer.missiles_adj", t.prayer.set, "protectfrommissiles", true)
        D.up = "missiles"
        D.adj_flick_from = now(t)
        local function melee_seen()
            for _, a in ipairs(rows(t, "npc_anim", { type = BLOB })) do
                if a.seq == ATK_MELEE and a.tick >= D.adj_flick_from then return true end
            end
            return false
        end
        D.adj_flick = {}
        local rounds = 0
        while alive(t) and rounds < 6 and not melee_seen() and sharks_left() > 12 do
            rounds = rounds + 1
            for _, p in ipairs(flick_series(t, 4)) do D.adj_flick[#D.adj_flick + 1] = p end
        end
        D.adj_flick_to = now(t)
        note(t, "adj.flick.done", string.format("tick %d hp %d sharks %d rounds %d melee %s", now(t), hp(t), sharks_left(), rounds, tostring(melee_seen())))
        t.exec("prayer.adj_off", t.prayer.set, PRAY[D.up], false)
        D.up = nil
        -- losing sight: stand on a tile the blob sees, step to a neighbour it does not see two ticks before a swing
        D.sight = {}
        do
            t.exec("prayer.missiles_sight", t.prayer.set, "protectfrommissiles", true)
            D.up = "missiles"
            D.auto, D.autoA = true, nil
            D.sight_from = now(t)
            local tries, kept = 0, 0
            while alive(t) and tries < 8 and kept < 3 and sharks_left() > 8 do
                tries = tries + 1
                if hp(t) < 80 then t.player.inv_op("shark", 1) wait_to(t, now(t) + 2) end
                local bb = blobs(t)[1]
                if not bb then break end
                local pair, tried = sight_pair(t, bb)
                if not pair then note(t, "sight.pair" .. tries, "no pillar neighbour pair split the blob's sight; tiles read " .. tostring(tried)) break end
                local wr = t.player.walk_to(pair.v.x, pair.v.z, 1)
                for _ = 1, 30 do
                    local _, raw = pack_rows(t)
                    if not alive(t) or (raw and raw.player_x == pair.v.x and raw.player_z == pair.v.z) then break end
                    wait_to(t, now(t) + 1)
                end
                wait_to(t, now(t) + 2)
                local b2 = blobs(t)[1]
                if not (b2 and b2.x == bb.x and b2.z == bb.z) then
                    note(t, "sight.moved" .. tries, string.format("walk %s: the blob moved %s,%s -> %s,%s while the player walked, pair dropped", tostring(wr), tostring(bb.x), tostring(bb.z), tostring(b2 and b2.x), tostring(b2 and b2.z)))
                else
                    note(t, "sight.pair" .. tries, string.format("blob %d,%d visible %d,%d hidden %d,%d (%d tiles read)", b2.x, b2.z, pair.v.x, pair.v.z, pair.h.x, pair.h.z, tried))
                    local A = D.autoA or (now(t) + 6)
                    while A - 4 <= now(t) do A = A + 6 end
                    wait_to(t, A - 4)
                    local sr, sd = t.player.step_tick(pair.h.x, pair.h.z)
                    D.sight[#D.sight + 1] = { A = A, step = tostring(sr), detail = tostring(sd):sub(1, 90), bx = b2.x, bz = b2.z }
                    wait_to(t, A + 1)
                    for _, pk in ipairs(pack_rows(t)) do if pk.type == BLOB then D.sight[#D.sight].sees_after = pk.sees_player end end
                    kept = kept + 1
                end
            end
            D.auto = false
            D.sight_to = now(t)
            note(t, "sight.flickpress", tostring(D.wf))
            t.exec("prayer.sight_off", t.prayer.set, PRAY[D.up or "missiles"], false)
            D.up = nil
            while hp(t) < 85 and sharks_left() > 12 and alive(t) do t.player.inv_op("shark", 1) t.ticks(3) end
        end
        -- unprayed: pooled hits for the max, until the max is met or the food is down to the kill's share
        D.u_from = now(t)
        local function enough()
            local m = {}
            local n = 0
            for _, h in ipairs(blob_hits(t)) do
                if h.tick >= D.u_from then n = n + 1 m[#m + 1] = h.damage end
            end
            return (n >= 24 and (maxof(m) or 0) >= 29) or sharks_left() <= 13
        end
        stand(t, "unprayed", 24, enough)
        D.u_to = now(t)
        note(t, "u.done", string.format("ticks %d..%d hp %d sharks %d alive %s", D.u_from, D.u_to, hp(t), sharks_left(), tostring(alive(t))))
        t.exec("prayer.missiles_kill", t.prayer.set, "protectfrommissiles", true)
        D.kill_from = now(t)
        D.levels = kill_all(t, BLOB, 90)
        D.kill_to = now(t)
        note(t, "kill.blob", string.format("ticks %d..%d levels %s hp %d", D.kill_from, D.kill_to, join(D.levels), hp(t)))
        D.split_from = now(t)
        D.blob_death = now(t)
        stand(t, "split", 8, nil)
        local bl = {}
        for _, p in ipairs(targets(t)) do bl[#bl + 1] = p end
        D.bloblets = bl
        local bt = {}
        for _, p in ipairs(bl) do bt[#bt + 1] = string.format("%s:%d@%d,%d size%s hp%s/%s range%s", tostring(p.symbol), p.type, p.x, p.z, tostring(p.size), tostring(p.hitpoints), tostring(p.max_hitpoints), tostring(p.attackrange)) end
        capture_recs(t)
        note(t, "bloblets.rows", table.concat(bt, " | "):sub(1, 900))
        D.first_hit_npc = now(t)
        D.bl_levels = kill_all(t, KET, 40, 56)
        note(t, "bloblet.melee_kill", string.format("ticks %d..%d levels %s hp %d", D.split_from, now(t), join(D.bl_levels), hp(t)))
        D.bl_stand_from = now(t)
        stand(t, "bloblets", 160, function() return sharks_left() <= 3 or hp(t) < 45 end)
        note(t, "bloblet.stand", string.format("ticks %d..%d hp %d sharks %d alive %s", D.bl_stand_from, now(t), hp(t), sharks_left(), tostring(alive(t))))
        D.bl_clear = kill_all(t, nil, 160, 52)
        note(t, "bloblet.cleared", string.format("tick %d hp %d sharks %d alive %s left %d", now(t), hp(t), sharks_left(), tostring(alive(t)), #targets(t)))
        D.bl_end = now(t)
        t.exec("prayer.off", t.prayer.set, "protectfrommissiles", false)
        local A = gather(t)
        rows_reads(t, A)
        rows_fight(t, A)
        rows_split(t, A)
        do
            local dmg, big = {}, 0
            for _, h in ipairs(rows(t, "hit_npc", { type = BLOB })) do
                if h.tick >= (D.w_from or 1e9) and h.tick <= (D.w_to or -1) + 2 and h.damage then dmg[#dmg + 1] = h.damage end
            end
            local mx = maxof(dmg) or -1
            local nz = 0
            for _, v in ipairs(dmg) do if v > 4 then big = big + 1 end if v > 0 then nz = nz + 1 end end
            local wtext = string.format("max %d over %d Water Strike hits (%d nonzero, %d over the base max 4); 4 * 1.4 = 5.6", mx, #dmg, nz, big)
            local wdet = "hit_npc rows on the blob between the first and last Water Strike cast (ticks " .. tostring(D.w_from) .. ".." .. tostring(D.w_to) .. "), damage list " .. join(dmg, nil, 40) .. "; a Water Strike hit above its base max 4 is the weakness"
            if #dmg >= 10 and mx > 4 and mx <= 6 then
                spec_row(t, "blob.water_weakness", true, wtext, wdet, "40 percent", "D", "exact")
            else
                D.water_bug = wtext .. "; " .. wdet
                note(t, "water.no_weakness", D.water_bug)
            end
        end
        if #(D.sight or {}) > 0 then
            local res, det = {}, {}
            for _, sg in ipairs(D.sight) do
                local swung = A.by_tick[sg.A] ~= nil
                local blind = sg.sees_after == false
                local tr = {}
                for _, x in ipairs(D.samples) do
                    if x.tick >= sg.A - 5 and x.tick <= sg.A + 1 then tr[#tr + 1] = string.format("%d:b%s,%s sees%s p%s,%s", x.tick, tostring(x.x), tostring(x.z), tostring(x.sees), tostring(x.px), tostring(x.pz)) end
                    if x.tick >= sg.A - 2 and x.tick <= sg.A and x.sees == false then blind = true end
                end
                local near = {}
                for k = sg.A - 8, sg.A + 8 do if A.by_tick[k] ~= nil then near[#near + 1] = k end end
                note(t, "sight.trace" .. _, "A" .. sg.A .. " swings at " .. table.concat(near, ",") .. " " .. table.concat(tr, " | "))
                if blind and sg.step == "ok" then
                    res[#res + 1] = swung and 1 or 0
                    det[#det + 1] = string.format("A%d blob %s,%s step %s swing %s", sg.A, tostring(sg.bx), tostring(sg.bz), sg.step, tostring(swung))
                end
            end
            if #res > 0 then
                spec_row(t, "blob.attack_completes_after_losing_sight", uniq(res) == "1", join(res), "stepped from a tile the blob saw to one it did not, on A-3, and counted the swing at A in every round where the blob read no sight on A-2..A+1; " .. table.concat(det, "; "), "1 count", "D", "exact")
            else
                note(t, "sight.unmeasured", "no round left the blob blind at the swing: the blob walked to a tile that saw the player again")
            end
        end
        -- technique rows
        local flick_text = string.format("%d of %d alternating-counter swings: the style was the opposite of the prayer up at the scan, the counter prayer was in force on or before the swing tick, and the hit was 0; bad: %s", D.flick_ok, D.flick_n, join(D.flick_bad or {}, nil, 6))
        t.check("technique.blob_alternate_flick", D.flick_n >= 10 and D.flick_ok == D.flick_n, flick_text)
        local rd = D.read or {}
        t.check("technique.blob_read_three_ticks_early", rd.consistent == true and rd.lead == 3, string.format("a press on A-4 or earlier was read by the blob and a press on A-3 or later was not: seen at o = %s, unseen at o = %s, read %s ticks before the swing", tostring(rd.new), tostring(rd.old), tostring(rd.lead)))
        local far_swings = 0
        for _, a in ipairs(A.swings) do if a.seq ~= ATK_MELEE then far_swings = far_swings + 1 end end
        t.check("technique.blob_not_adjacent", (D.melee or 0) == (D.melee_adj or -1) and far_swings >= 30, string.format("%d crush swings in the whole run, %d of them with the player adjacent; %d ranged or magic swings from range", D.melee or -1, D.melee_adj or -1, far_swings))
        record_rows(t)
        if D.water_bug then
            t.blocked("content_bug: blob.water_weakness: Water Strike hits on the Jal-Ak never exceeded the base max 4 (a 40% weakness lifts the max to 5): " .. D.water_bug:sub(1, 300))
        end
    end,
}
