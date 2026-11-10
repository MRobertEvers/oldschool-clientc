-- yama_solver: a real Yama kill, no cheats (docs/minigames/yama/solver.md is the plan).
-- Perception is what a client gets: npc rows (seq/spot stamps, tiles, head icons), map
-- graphics, locs, chat, my own tile/skills. One press round a tick; nothing here reads
-- the server. Run with --fixture test/bosses/fixtures/yama_kit.ini.
local NEAR = 40
local SEQ_REG, SEQ_SPEC, SEQ_MELEE, SEQ_PORTAL, SEQ_STOMP, SEQ_DESPAWN = 12144, 12145, 12146, 12147, 12148, 12161
local SPOT_FIRE, SPOT_SHADOW = 3246, 3243
local SPOT_ROCK, SPOT_SPIKE, SPOT_METEOR, SPOT_ROOT = 3253, 3256, 3270, 3259
local WAVE3, WAVE2, STREAK, ROCK = 3266, 3265, 3267, 3262
-- Debug rows while reading a seed; the sweep leaves them off.
local TRACE = false

local function now(t) local _, k = t.tick() return k or -1 end
local function tile(t) local r, p = t.world.tile() if r == "ok" then return p.x, p.z end end
local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end

local function row_of(t, sym, r)
    local res, row = t.npc.nearest(sym, r or NEAR)
    if res == "ok" and type(row) == "table" then return row end
    return nil
end

local function skill_of(t, name, default)
    local r, v = t.quest._raw_skill_read(name)
    if r == "ok" and type(v) == "table" then return v.level or default end
    return default
end

local function count(S, name) S.tells[name] = (S.tells[name] or 0) + 1 end
local function trace(t, S, text) if TRACE then t.ticklog.mark(text) end end

local function new_state()
    return {
        waves = {}, seen_wave = {}, tells = {}, lanes = {}, rock_tiles = {},
        style = "fire", last_style_tick = -1, phase = 1,
        next_cast = 0, next_hop = 0, msg_serial = 0, wrong = 0,
        pray_block_until = -1, glyph_used = {},
    }
end

-- The glyphs' middle tiles (offsets from the copied square's corner). Layout A: the north
-- pair and the south are fire; layout B: reversed.
local GLYPHS = {
    { dx = 26, dz = 37, a = "fire", b = "shadow" }, { dx = 36, dz = 37, a = "fire", b = "shadow" },
    { dx = 24, dz = 30, a = "shadow", b = "fire" }, { dx = 31, dz = 25, a = "fire", b = "shadow" },
    { dx = 38, dz = 30, a = "shadow", b = "fire" },
}
local GLYPH_LOC = { fire = "floorkit_summoning03_full02", shadow = "floorkit_summoning03_full01" }
-- Where to stand between attacks: north of the boss, reach of both pairs.
local HOME = { dx = 31, dz = 34 }

local function is_glyph_tile(S, x, z)
    for _, g in ipairs(GLYPHS) do
        if x == S.bx + g.dx and z == S.bz + g.dz then return true end
    end
    return false
end

-- Shadow waves: the sent table's delay per tile (template offsets), hit at
-- t0 + floor(delay / 30) on the tile I stood on at the end of the tick before.
local function wave_delay(kind, dx, dz)
    if kind == 3 then return 420 - 15 * (dx - dz) end
    return 15 * (dx + dz) - 480
end

local function wave_hits(S, dx, dz, T)
    for _, w in ipairs(S.waves) do
        local d = wave_delay(w.kind, dx, dz)
        if d >= 0 and math.floor(d / 30) == T - w.t0 then return true end
    end
    return false
end

-- Fire streaks: lanes sent as map anims, one row each, moving a side's direction.
-- `lanes[z] = { x = front, dir = +1|-1, seen = tick }`.
local function streak_danger(S, x, z, k)
    local lane = S.lanes[z]
    if not lane or k - lane.seen > 3 then return false end
    local ahead = (x - lane.x) * lane.dir
    return ahead >= -1 and ahead <= 6
end

local function tile_danger(S, x, z, k)
    local dx, dz = x - S.bx, z - S.bz
    if wave_hits(S, dx, dz, k + 2) then return true end
    if streak_danger(S, x, z, k) then return true end
    local rock = S.rock_tiles[x .. "," .. z]
    if rock and k <= rock then return true end
    return false
end

-- Yama's 5x5 footprint and the ring next to it (melee reach): tiles to keep off.
local function in_boss(S, Y, x, z)
    if not Y then return false end
    return x >= Y.x - 1 and x <= Y.x + 5 and z >= Y.z - 1 and z <= Y.z + 5
end

-- Map graphics this tick: waves (first seen = t0), streak fronts per row, a rock on my tile.
local function scan_map(t, S, k)
    local r, rows = t.world.spotanims(0)
    S.rock_here = false
    if r ~= "ok" or type(rows) ~= "table" then return end
    local counts = { [WAVE3] = 0, [WAVE2] = 0 }
    for i = 1, #rows do
        local row = rows[i]
        local id, x, z = row.spotanim_id, row.x, row.z
        if TRACE and not S.seen_ids then S.seen_ids = {} end
        if TRACE and not S.seen_ids[id] then
            S.seen_ids[id] = true
            trace(t, S, "spot " .. id .. " active " .. tostring(row.active) .. " cl " .. tostring(row.cycles_left) .. " at " .. x - S.bx .. "," .. z - S.bz .. " me " .. S.mx - S.bx .. "," .. S.mz - S.bz)
        end
        if id == WAVE3 or id == WAVE2 then
            counts[id] = counts[id] + 1
        elseif id == ROCK then
            S.rock_tiles[x .. "," .. z] = k + 4
            if x == S.mx and z == S.mz then S.rock_here = true end
        elseif id == STREAK then
            local lane = S.lanes[z]
            local dir = (x - S.bx < 31) and 1 or -1
            if not lane or k - lane.seen > 3 then
                S.lanes[z] = { x = x, dir = dir, seen = k }
            else
                if (x - lane.x) * lane.dir > 0 then lane.x = x end
                lane.seen = k
            end
        end
    end
    for _, pair in ipairs({ { WAVE3, 3 }, { WAVE2, 2 } }) do
        local id, kind = pair[1], pair[2]
        local n, last = counts[id], S.seen_wave[id] or 0
        -- A wave is 514 tiles sent at once; another can launch while one is still on the
        -- map, so it is the jump in the count that names a new wave.
        if n - last >= 200 then
            S.waves[#S.waves + 1] = { t0 = k, kind = kind }
            count(S, "wave")
            trace(t, S, "wave kind" .. kind .. " t0 " .. k .. " n " .. n .. " last " .. last)
        end
        S.seen_wave[id] = n
    end
    for i = #S.waves, 1, -1 do
        if k - S.waves[i].t0 > 40 then table.remove(S.waves, i) end
    end
end

-- New chat since the last look, by serial (the ring is newest-first).
local function scan_chat(t, S)
    local r, list = t.msg.last(12)
    if r ~= "ok" or type(list) ~= "table" then return end
    local high = S.msg_serial
    for i = 1, #list do
        local m = list[i]
        if type(m) == "table" and (m.serial or 0) > S.msg_serial then
            if m.serial > high then high = m.serial end
            local line = tostring(m.text)
            if line:find("fire-based", 1, true) then
                S.special_el = "fire"
            elseif line:find("shadow-based", 1, true) then
                S.special_el = "shadow"
            elseif line:find("mark yourself with darkness", 1, true) then
                S.mark_due = S.k + 285
            elseif line:find("You have been poisoned", 1, true) then
                S.poisoned = true
            elseif line:find("can't use protection", 1, true) then
                S.pray_block_until = S.k + 2
                count(S, "prayer_shut")
            end
        end
    end
    S.msg_serial = high
end

-- Phase 3's attack list (slots 0.., the 10-slot block repeating from slot 7): crashes on
-- slots 0, 3, 7, 10, the rest regulars.
local function p3_is_crash(i)
    local r = i
    if r >= 7 then r = 7 + (r - 7) % 10 end
    return r == 0 or r == 3 or r == 7 or r == 10
end

-- Tells on Yama's row: a regular's spot, a special's seq+spot, the portal.
local function read_tell(t, S, Y, k)
    if Y.spotanim_tick and Y.spotanim_tick ~= S.last_style_tick then
        if Y.spotanim_id == SPOT_FIRE then
            S.last_style_tick = Y.spotanim_tick
            S.style = "fire"
        elseif Y.spotanim_id == SPOT_SHADOW then
            S.last_style_tick = Y.spotanim_tick
            S.style = "shadow"
        end
    end
    -- A special: the seq, then the element from whichever client channel carries it first
    -- (the spot unless my own impact spot lands that tick, the overhead, the chat line).
    if (Y.seq_id == SEQ_SPEC or Y.seq_id == SEQ_STOMP) and Y.seq_tick ~= S.spec_tick then
        S.spec_tick = Y.seq_tick
        S.spec_pending = k
        S.spec_seq = Y.seq_id
    end
    if S.spec_pending and k - S.spec_pending > 3 then S.spec_pending = nil end
    local el = S.special_el
    S.special_el = nil
    if S.spec_pending and not el then
        if Y.spotanim_id == SPOT_ROCK or Y.spotanim_id == SPOT_METEOR then el = "fire"
        elseif Y.spotanim_id == SPOT_SPIKE or Y.spotanim_id == SPOT_ROOT then el = "shadow" end
    end
    if S.spec_pending and not el and type(Y.overhead) == "string" then
        if Y.overhead:find("infernus", 1, true) then el = "fire"
        elseif Y.overhead:lower():find("umbra", 1, true) then el = "shadow" end
    end
    if S.spec_pending and el then
        -- On the glyph by the end of the tick before the landing: rock, spike and meteor
        -- land A+6, the root A+5.
        local land = (S.spec_seq == SEQ_STOMP and el == "shadow") and 5 or 6
        S.glyph_until = S.spec_pending + land - 1
        S.spec_pending = nil
        S.glyph_want = el
        S.style = el
        count(S, el .. "_special")
        trace(t, S, "spec " .. el .. " at " .. k)
    end
    if Y.seq_id == SEQ_PORTAL and Y.seq_tick ~= S.portal_tick then
        S.portal_tick = Y.seq_tick
        S.phase = S.phase + 1
        S.p3_i = 0
        count(S, "portal")
    end
    -- Phase 3: the token stream names each attack; a regular's style alternates from the
    -- north glyphs' element, read from the spot when my own impact spot does not mask it.
    if S.phase >= 3 and (Y.seq_id == SEQ_REG or Y.seq_id == SEQ_MELEE) and Y.seq_tick ~= S.attack_tick then
        S.attack_tick = Y.seq_tick
        local i = S.p3_i or 0
        S.p3_i = i + 1
        if Y.seq_id == SEQ_REG and not p3_is_crash(i) then
            if S.p3_next == nil then S.p3_next = (S.layout_mem == "a") and "fire" or "shadow" end
            local seen = (Y.spotanim_id == SPOT_FIRE and "fire") or (Y.spotanim_id == SPOT_SHADOW and "shadow")
            S.style = seen or S.p3_next
            S.p3_next = (S.style == "fire") and "shadow" or "fire"
            trace(t, S, "p3 i " .. i .. " seen " .. tostring(seen) .. " style " .. S.style)
        else
            trace(t, S, "p3 i " .. i .. " crash/other seq " .. tostring(Y.seq_id))
        end
    end
end

local function layout_from_locs(t, S)
    if S.layout then return end
    local r, loc = t.world.loc_near("floorkit_summoning03_full02", 30)
    if r == "ok" and type(loc) == "table" then
        S.layout = (loc.tile_z - S.bz >= 34) and "a" or "b"
        S.layout_mem = S.layout
        S.style = (S.layout == "a") and "shadow" or "fire"
        count(S, "layout_" .. S.layout)
    end
end

-- The nearest lit glyph of an element, by loc (what the client shows).
local function pick_glyph(t, S, element, x, z, Y)
    local r, _, rows = t.world.loc_copies(GLYPH_LOC[element], 60)
    if r ~= "ok" or type(rows) ~= "table" then return nil end
    local best, bd
    for i = 1, #rows do
        local gx, gz = rows[i].x + 1, rows[i].z + 1
        local d = cheb(x, z, gx, gz)
        if Y and in_boss(S, Y, math.floor((x + gx) / 2), math.floor((z + gz) / 2)) then d = d + 6 end
        if not bd or d < bd then best, bd = { x = gx, z = gz }, d end
    end
    return best
end

-- The list of (ordered) drinks for prayer points; picks the first held.
local PRAYER_POTS = { "4doseprayerrestore", "3doseprayerrestore", "2doseprayerrestore", "1doseprayerrestore",
    "4dose2restore", "3dose2restore", "2dose2restore", "1dose2restore" }
local ANTIVENOM = { "extended_antivenom+4", "extended_antivenom+3", "extended_antivenom+2", "extended_antivenom+1" }

local function held(t, names)
    for _, n in ipairs(names) do
        local r, c = t.inv.count(n)
        if r == "ok" and (c or 0) > 0 then return true end
    end
    return false
end

-- One round of presses: the cast (which never waits) then the rest in one block, so a
-- press round is one tick. Returns after at least a tick has gone.
local function press_round(t, S, D)
    local k = S.k
    if D.cast and not D.walk then
        if k >= S.next_cast then
            S.next_cast = k + 5
            local c0 = now(t)
            local cr, cd = t.player.cast("dark_demonbane", D.cast, 0, 2, { quick = true })
            if TRACE and now(t) - c0 > 0 then trace(t, S, "cast took " .. (now(t) - c0)) end
            count(S, "cast")
            if TRACE and cr ~= "ok" then trace(t, S, "cast " .. tostring(cr) .. " " .. tostring(cd):sub(1, 90)) end
        end
    end
    if D.melee then
        t.player.attack(D.melee, 2, 0, { quick = true })
    end
    if D.mark then
        -- Mark of Darkness is a self-cast press with no waiting; the chat line is what
        -- says it took (scan_chat), the button is pressed again until it does.
        local cr, comp = t.player._spell_component("mark_of_darkness")
        if cr == "ok" then t.ui.invoke(comp, 1) end
        count(S, "mark_press")
    end
    if D.pray or D.eat or D.drink or D.anti or D.walk or D.equip or D.mark then
        if not (D.pray or D.eat or D.drink or D.anti or D.walk or D.equip) then
            t.ticks(1)
            return
        end
        local eat, drink, anti, walk, pray, equip = D.eat, D.drink, D.anti, D.walk, D.pray, D.equip
        local t0 = now(t)
        t.together(function()
            if pray then t.prayer.set(pray, true) end
            if equip then t.player.equip(equip) end
            if eat then t.player.eat("mantaray") end
            if drink then t.player.drink(PRAYER_POTS) end
            if anti then t.player.drink(ANTIVENOM) end
            if walk then t.player.walk_to(walk.x, walk.z) end
        end)
        if TRACE and now(t) - t0 > 1 then
            trace(t, S, "together took " .. (now(t) - t0) .. " pray " .. tostring(pray) .. " walk " .. tostring(walk and (walk.x .. "," .. walk.z)) .. " eat " .. tostring(eat))
        end
    end
    if now(t) == k then t.ticks(1) end
    if now(t) - k > 1 then count(S, "lag") end
end

local function want_prayer(t, S, D, name)
    if S.k < S.pray_block_until then return end
    local r, _, set = t.prayer.read()
    if r == "ok" and type(set) == "table" and set[name] == true then return end
    D.pray = name
end

local function survive(t, S, D, hp, prayer, Y)
    if hp <= 55 then
        local r, n = t.inv.count("mantaray")
        if r == "ok" and (n or 0) > 0 then D.eat = true end
    end
    if prayer <= 25 and held(t, PRAYER_POTS) then
        D.drink = true
    end
    if S.poisoned and held(t, ANTIVENOM) then
        D.anti = true
        S.poisoned = false
    end
end

-- A dodge step: the nearest of the tiles within two steps that no threat reads next.
local function dodge(t, S, D, x, z, danger, Y)
    local best, bs
    local hx, hz = Y and Y.x + 2 or x, Y and Y.z + 6 or z
    for ddx = -2, 2 do
        for ddz = -2, 2 do
            if ddx ~= 0 or ddz ~= 0 then
                local nx, nz = x + ddx, z + ddz
                if not danger(nx, nz) and not in_boss(S, Y, nx, nz) and not is_glyph_tile(S, nx, nz) then
                    -- The nearest safe tile, then the one nearer home (the glyphs' reach).
                    local score = math.max(math.abs(ddx), math.abs(ddz)) * 10 + math.abs(ddx) + math.abs(ddz)
                        + cheb(nx, nz, hx, hz) * 3
                    if not bs or score < bs then best, bs = { x = nx, z = nz }, score end
                end
            end
        end
    end
    if best then D.walk = best end
    return best ~= nil
end

-- Home without crossing the ring next to him (melee reach on his attack tick): via the
-- outside corner on my side, then along the north side.
local function walk_home(S, Y, x, z, hx, hz)
    local west, east = Y.x - 2, Y.x + 6
    local north, south = Y.z + 6, Y.z - 2
    if z < north then
        if z <= south then
            -- South side: head for the nearest outside corner first.
            local cx = (math.abs(x - west) <= math.abs(x - east)) and west or east
            if cheb(x, z, cx, south) > 0 then return { x = cx, z = south } end
        end
        local cx = (math.abs(x - west) <= math.abs(x - east)) and west or east
        if cheb(x, z, cx, north) > 0 then return { x = cx, z = north } end
    end
    return { x = hx, z = hz }
end

local function fight_tick(t, S, k, Y)
    local x, z = tile(t)
    if not x then t.ticks(1) return end
    S.mx, S.mz, S.k = x, z, k
    local D = {}
    if not S.mark_due or k >= S.mark_due then
        S.mark_due = k + 20
        D.mark = true
    end
    scan_map(t, S, k)
    scan_chat(t, S)
    layout_from_locs(t, S)
    read_tell(t, S, Y, k)
    if TRACE and (Y.x ~= S.yx or Y.z ~= S.yz) then
        S.yx, S.yz = Y.x, Y.z
        trace(t, S, "Y " .. Y.x - S.bx .. "," .. Y.z - S.bz .. " me " .. x - S.bx .. "," .. z - S.bz)
    end
    local hp, prayer = skill_of(t, "hitpoints", 99), skill_of(t, "prayer", 40)
    if hp <= 0 then t.ticks(1) return end
    want_prayer(t, S, D, S.style == "fire" and "protectfrommagic" or "protectfrommissiles")
    survive(t, S, D, hp, prayer, Y)

    -- A special in the air: the glyph, ahead of everything but a press that costs nothing.
    if S.glyph_want then
        if k > S.glyph_until then S.glyph_want = nil else
            local g = pick_glyph(t, S, S.glyph_want, x, z, Y)
            -- Out of reach in the ticks left (two tiles a tick): eat above the special's
            -- 80 instead of hoping.
            if (not g or cheb(x, z, g.x, g.z) > 2 * (S.glyph_until - k + 1)) and hp <= 80 then
                local r, n = t.inv.count("mantaray")
                if r == "ok" and (n or 0) > 0 then D.eat = true end
                count(S, "glyph_far")
            end
            if g then
                if x == g.x and z == g.z then
                    count(S, "glyph_stepped")
                    S.glyph_want = nil
                    S.home_after = k + 1
                else
                    D.walk = { x = g.x, z = g.z }
                    trace(t, S, "glyph walk " .. g.x - S.bx .. "," .. g.z - S.bz)
                end
            else
                count(S, "no_glyph")
                S.glyph_want = nil
            end
        end
    end

    -- Hazards (only when not running for a glyph).
    if not D.walk then
        local danger = function(nx, nz) return tile_danger(S, nx, nz, k) end
        if S.rock_here then
            count(S, "dodge_crash")
            dodge(t, S, D, x, z, function(nx, nz) return tile_danger(S, nx, nz, k) or (nx == x and nz == z) end, Y)
        elseif tile_danger(S, x, z, k) then
            count(S, "dodge")
            trace(t, S, "dodge at " .. k)
            dodge(t, S, D, x, z, danger, Y)
        end
    end

    -- Attack: flares first (their bar is the clock), Yama otherwise. Walking back home only
    -- when idle.
    local flare = row_of(t, "yama_voidflare", 30)
    if not D.walk then
        local hx, hz = Y.x + 2, Y.z + 6
        local far = cheb(x, z, hx, hz) > 2
        if (in_boss(S, Y, x, z) or far) and k >= (S.home_after or 0) then
            local w = walk_home(S, Y, x, z, hx, hz)
            if not tile_danger(S, w.x, w.z, k) then D.walk = w end
        end
    end
    D.cast = flare and "yama_voidflare" or "yama"
    press_round(t, S, D)
end

-- The Judge: hop the stones up the crossing, then hit him where his prayer lets it land.
local function next_stone(t, S, x, z, J)
    local r, _, rows = t.world.loc_copies("yama_steppingstone", 40)
    if r ~= "ok" or type(rows) ~= "table" then return nil end
    -- Surges slide south a tile a tick down their columns: a stone with one just north
    -- of it (within the hop time) is taken.
    local _, _, surges = t.npc.tiles("fire_surge_raised", 40)
    local function burnt(sx, sz)
        for i = 1, #(surges or {}) do
            local g = surges[i]
            if g.x == sx and g.z >= sz - 1 and g.z <= sz + 3 then return true end
        end
        return false
    end
    local best, bd
    for i = 1, #rows do
        local s = rows[i]
        local dz, dx = s.z - z, math.abs(s.x - x)
        if dz >= 1 and dz <= 3 and dx <= 3 and not burnt(s.x, s.z) then
            local d = math.abs(s.x - (J.x + 1)) * 2 + dz
            if not bd or d < bd then best, bd = s, d end
        end
    end
    return best
end

local function judge_tick(t, S, k, J)
    local x, z = tile(t)
    if not x then t.ticks(1) return end
    S.mx, S.mz, S.k = x, z, k
    if TRACE then trace(t, S, "jt " .. k .. " J " .. J.x - S.bx .. "," .. J.z - S.bz .. " seq " .. tostring(J.seq_id) .. " me " .. x - S.bx .. "," .. z - S.bz) end
    scan_chat(t, S)
    local D = {}
    local hp, prayer = skill_of(t, "hitpoints", 99), skill_of(t, "prayer", 40)
    survive(t, S, D, hp, prayer, nil)
    if TRACE and not S.jdump then
        S.jdump = true
        trace(t, S, "J " .. J.x - S.bx .. "," .. J.z - S.bz .. " size " .. tostring(J.size) .. " head " .. tostring(J.headicon_index))
    end
    S.layout = nil
    if not S.mark_due or k >= S.mark_due then
        S.mark_due = k + 20
        D.mark = true
    end
    -- Crossing: a stone every two ticks up to the last row, then onto the platform.
    if z < J.z - 3 then
        if k >= S.next_hop then
            local s = next_stone(t, S, x, z, J)
            if s then
                S.next_hop = k + 2
                count(S, "hop")
                t.player.click_loc("yama_steppingstone", 1, { at = { s.x, s.z }, from_here = true })
            else
                D.walk = { x = J.x, z = J.z - 1 }
            end
        end
        press_round(t, S, D)
        return
    end
    -- On the platform: two tiles under his south edge, changed on each of his casts
    -- (his fireball lands on the tile he read, a 120-cycle flight).
    if J.seq_id == 69 and J.seq_tick ~= S.jcast_tick then
        S.jcast_tick = J.seq_tick
        S.jside = not S.jside
    end
    local tx, tz = S.jside and J.x + 2 or J.x, J.z - 1
    if x ~= tx or z ~= tz then D.walk = { x = tx, z = tz } end
    -- His prayer: frame 7 melee, frame 6 magic; a hit of the style he prays lands 0.
    -- Hit with the other one; the weapon follows.
    local head = J.headicon_index
    if head == 6 then
        if S.weapon ~= "emberlight" then
            D.equip = "emberlight"
            S.weapon = "emberlight"
        elseif k >= S.next_cast and not D.walk then
            S.next_cast = k + 4
            D.melee = "yama_judge_of_yama"
            count(S, "swing")
        end
    else
        if S.weapon == "emberlight" then
            D.equip = "purging_staff"
            S.weapon = "purging_staff"
        else
            D.cast = "yama_judge_of_yama"
        end
    end
    press_round(t, S, D)
end

local function run(t)
    t.ticklog.start()
    -- One press round is one tick: a together block waits at most a tick for its effects.
    t.TOGETHER_CONFIRM_TICKS = 1
    local S = new_state()
    t.ticks(2)
    t.exec("voice.travel", t.player.talk_to, "voice_of_yama", 3)
    t.ticks(8)
    local x, z = tile(t)
    S.bx, S.bz = x - x % 64, z - z % 64
    t.check("entered", x > 6000, "landed " .. tostring(x) .. "," .. tostring(z))
    t.exec("walk.throne", t.player.walk_to, S.bx + 31, S.bz + 40, 40)
    t.player.press("yama_throne", 3, 3)
    t.ticklog.mark("fight")
    local start, deadline = now(t), now(t) + 5200
    local fought, killed = false, false
    while now(t) < deadline do
        local k = now(t)
        if TRACE and S.loop_end and k - S.loop_end > 1 then
            trace(t, S, "loop gap " .. (k - S.loop_end) .. " now " .. k)
        end
        local J = row_of(t, "yama_judge_of_yama", 60)
        local Y = row_of(t, "yama", NEAR)
        if TRACE and not Y and fought then
            local r, d = t.npc.nearest("yama", NEAR)
            trace(t, S, "no Y k " .. k .. " r " .. tostring(r) .. " d " .. tostring(d):sub(1, 80))
        end
        if J then
            fought = true
            judge_tick(t, S, k, J)
        elseif Y then
            fought = true
            if Y.seq_id == SEQ_DESPAWN then
                killed = true
                t.ticklog.mark("killed")
                break
            end
            fight_tick(t, S, k, Y)
        else
            t.ticks(1)
        end
        S.loop_end = now(t)
    end
    t.ticks(20)
    local dead = t.player.alive() ~= "ok"
    for name, n in pairs(S.tells) do t.check("tell." .. name, true, name .. " " .. n) end
    t.check("yama.killed", killed, "kill seen, ended " .. now(t) .. " dead=" .. tostring(dead))
    t.expect("yama.run", "ok", "fight ticks " .. (now(t) - start))
    t.finish(killed and not dead and 0 or 1)
end

return {
    id = "yama_solver",
    fixture = "yama_kit.ini",
    max_frames = 400000,
    setup = {},
    run = run,
}
