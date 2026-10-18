-- cox_olm_party: the Great Olm's PARTY rules, three raiders in one Olm, judged
-- off the server's tick log against what Jagex's server did with four
-- (RSProx 820; tools/cox/rsprox_olm_party.py, docs/rsprox-analysis/cox/olm.md
-- "Parties"):
--
--   1. THE HEAD COUNTS VIEWS. Two raiders on the separating row (z 36), one at
--      z 30: by view south 3, middle 2, north 2, so the head's first even step
--      turns it SOUTH. (Counted south / north of the separating row, as the
--      content did, that was 1 / 2 / 0 and it stayed middle.) Its plain autos
--      go at exactly the raiders in the view it faces.
--   2. LIGHTNING: 4 + party / 2 = 5 bolts, each down a column of its own.
--   3. THE CRYSTAL BURST: a seedling under every raider.
--   4. THE TELEPORT: one pair, its colour on both, the odd raider unpicked; the
--      pair swap tiles 10 ticks on and each is hit 1 + floor(4.3 d) the tick
--      after; then the three on one tile: no swap and no hit.
--   5. BURN WITH ME: the burn passes to the raider beside its target, never
--      to the one four tiles off.
--
-- The rotation runs on its own for 1; ::coxolmdo (cox_olm.rs2) casts 2-5 and
-- holds the head's clock from the first one on.
--
--   ./src/build_cox_opt/torirsserver --scriptrun test/raids/cox_olm_party.lua --bots 3 \
--       --name op1 --ticks 3000 --session /tmp/op --fixture test/raids/fixtures/fresh_lumbridge.ini

local kit = { "::clearinv" }
for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged" }) do
    kit[#kit + 1] = "::setlevel " .. s .. " 99"
end

-- cache ids (all.seq / all.spotanim / all.loc)
local TURN_RIGHT, TURN_LEFT = 7339, 7341          -- olm_head_turn_right / _left: front -> side
local FIREBREATH, RANGE_PROJ = 1339, 1340         -- the plain autos
local SHOCKWAVE = 1356                            -- a lightning bolt's tile
local SWAP0, SWAP3 = 1359, 1362                   -- olm_playerswap_0..3
local BURN_TRAVEL, BURN_SPREAD = 1349, 1350       -- olm_burnwithme_travel / _travel_pl
local SEEDLING, ACID_POOL = 30033, 30032          -- olm_crystal_attack_small / olm_acid_pool
local HEAD_LOC = 29881                            -- olm_head
-- the chamber frame (cox_olm.constant): the corridor ::coxolmrush stands me on
local ENTRY_LX, ENTRY_LZ = 16, 16
local WEST_LOC_X, BOLT_X0 = 4, 12

local function run(t)
    local role, size = t.party.role(), t.party.size()
    local function barrier(name, wait)
        local r, d = t.party.barrier(name, wait or 600)
        t.check("op.barrier." .. name, r == "ok", tostring(d))
        if r ~= "ok" then t.finish(1) end
        return r == "ok"
    end
    t.check("op.party", size == 3, "raiders " .. tostring(size))
    if role == 1 then
        t.check("op.ticklog", t.ticklog.start() == "ok", "")
        t.cheat("::coxolmrush", false)
        t.ticks(4)
    end
    if not barrier("built") then return end
    if role ~= 1 then
        local jr, jd = t.raid.cox_test_join()
        t.check("op.join", jr == "ok", tostring(jd))
        if jr ~= "ok" then t.finish(1) return end
    end
    if not barrier("in") then return end
    -- the chamber's origin, from the corridor tile every raider stands on
    local _, at = t.world.tile()
    local ox, oz = at.x - ENTRY_LX, at.z - ENTRY_LZ
    local function stand(lx, lz)
        t.cheat(string.format("::coxolmstand %d %d", lx, lz), false)
    end
    local function heal()
        t.cheat("::setlevel hitpoints 99", false)
    end
    -- (a wait heals every other tick: the rotation's acid pools, autos and
    -- crystals would kill a raider made to stand still -- op8 p2, t64)
    local function wait(n)
        for i = 1, n do
            t.ticks(1)
            if i % 2 == 0 then heal() end
        end
    end
    -- 1. THE HEAD'S VIEWS, every raider placed before the head has risen: the
    -- members stand in first, then the leader steps through the barrier
    -- (its op walks me in and wakes Olm) and onto its own tile
    local SPOT = { { 14, 36 }, { 16, 36 }, { 17, 30 } }
    if role ~= 1 then
        stand(SPOT[role][1], SPOT[role][2])
        t.ticks(2)
    end
    if not barrier("members_placed") then return end
    if role == 1 then
        local hr, hd = t.player.click_loc("raids_olm_barrier", 1)
        t.check("op.barrier", hr == "ok" or hr == "timeout", tostring(hr) .. " " .. tostring(hd))
        local cr, cd = t.chat.play({ "options", "choose:Step through the mystical barrier." })
        t.check("op.confirm", cr == "ok", tostring(cd))
        stand(SPOT[role][1], SPOT[role][2])
        t.ticks(1)
    end
    if not barrier("placed") then return end
    -- every raider stands, healed, while the head rises and acts on its own:
    -- the rise, then a step every 4 ticks (the first turn is the second step)
    local _, t0 = t.tick()
    wait(45)
    if role == 1 then
        local _, anims = t.ticklog.rows({ kind = "loc_anim", where = function(r) return r.tick > t0 and r.loc == HEAD_LOC end })
        local turn, wall = nil, nil
        for _, a in ipairs(anims or {}) do
            wall = wall or ((a.x - ox == WEST_LOC_X) and "west" or "east")
            -- (olm_head_turn_* 7339..7344; 7345.. are the attacks)
            if turn == nil and a.seq >= TURN_RIGHT and a.seq <= 7344 then
                turn = a
            end
        end
        local want_seq = (wall == "west") and TURN_RIGHT or TURN_LEFT
        t.check("op.head.turns_south", turn ~= nil and turn.seq == want_seq, string.format(
            "%s wall: first turn seq %s at t%s, south is %d (by the old partition the head stayed middle)",
            tostring(wall), tostring(turn and turn.seq), tostring(turn and turn.tick), want_seq))
        -- THE AUTOS: every plain auto goes at exactly the raiders in the view
        -- the head faces, on the tiles they began the tick on (the head acts
        -- before they move). The facing from the head's turns; the tiles from
        -- the log (a crystal burst shunts them).
        local function in_view(dir, lz)
            if lz < 26 or lz > 44 then return false end
            if dir == "S" then return lz <= 36 end
            if dir == "N" then return lz >= 36 end
            return lz >= 31 and lz <= 41
        end
        -- a turn's new facing, by its seq and the wall (the west wall looks
        -- east: its left is NORTH)
        local function turned_to(seq)
            local side = ({ [7339] = "right", [7340] = "front", [7341] = "left", [7342] = "front",
                            [7343] = "right", [7344] = "left" })[seq]
            if side == "front" then return "M" end
            if (side == "left") == (wall == "west") then return "N" end
            return "S"
        end
        local seen, ok_autos = {}, true
        t.ticks(1)
        local _, shots = t.ticklog.rows({ kind = "projectile", where = function(r)
            return r.tick > t0 and (r.spotanim == FIREBREATH or r.spotanim == RANGE_PROJ) end })
        t.ticks(1)
        local _, tiles = t.ticklog.rows({ kind = "player_tile", where = function(r) return r.tick >= t0 end })
        local by_tick, tile_at = {}, {}
        for _, p in ipairs(shots or {}) do
            by_tick[p.tick] = by_tick[p.tick] or {}
            local l = by_tick[p.tick]
            l[#l + 1] = string.format("%d,%d", p.dst_x - ox, p.dst_z - oz)
        end
        for _, r in ipairs(tiles or {}) do
            tile_at[r.tick] = tile_at[r.tick] or {}
            tile_at[r.tick][r.pid] = r
        end
        for tick, l in pairs(by_tick) do
            local dir = "M"
            for _, a in ipairs(anims or {}) do
                if a.tick < tick and a.seq >= TURN_RIGHT and a.seq <= 7344 then dir = turned_to(a.seq) end
            end
            local want = {}
            for _, r in pairs(tile_at[tick - 1] or {}) do
                if r.x - ox >= 11 and r.x - ox <= 21 and in_view(dir, r.z - oz) then
                    want[#want + 1] = string.format("%d,%d", r.x - ox, r.z - oz)
                end
            end
            table.sort(l)
            table.sort(want)
            local got, w = table.concat(l, " "), table.concat(want, " ")
            if got ~= w then ok_autos = false end
            seen[#seen + 1] = string.format("t%d facing %s at %s (in view %s)", tick, dir, got, w)
        end
        table.sort(seen)
        t.check("op.head.autos_at_the_view", ok_autos and #seen > 0, table.concat(seen, "; "))
    end
    if not barrier("views_judged") then return end

    -- 2. LIGHTNING
    if role == 1 then
        local _, t0 = t.tick()
        t.cheat("::coxolmdo 9", false)
        t.ticks(2)
        local _, bolts = t.ticklog.rows({ kind = "map_spotanim", where = function(r) return r.tick >= t0 and r.spotanim == SHOCKWAVE end })
        local first = nil
        for _, b in ipairs(bolts or {}) do first = (first == nil or b.tick < first) and b.tick or first end
        local cols, n, ok = {}, 0, true
        for _, b in ipairs(bolts or {}) do
            if b.tick == first then
                local c = b.x - ox - BOLT_X0
                if c < 0 or c > 9 or cols[c] then ok = false end
                if not cols[c] then n = n + 1 end
                cols[c] = true
            end
        end
        t.check("op.lightning.bolts", ok and n == 5, string.format("%d bolts on the cast tick t%s (want 4 + 3 / 2 = 5), columns 0..9 distinct %s",
            n, tostring(first), tostring(ok)))
    end
    wait(20)
    if not barrier("lightning_done") then return end

    -- 3. THE CRYSTAL BURST
    local BURST = { { 14, 33 }, { 16, 33 }, { 18, 40 } }
    stand(BURST[role][1], BURST[role][2])
    t.ticks(2)
    if not barrier("burst_placed") then return end
    if role == 1 then
        local _, t0 = t.tick()
        t.cheat("::coxolmdo 8", false)
        t.ticks(1)
        local _, locs = t.ticklog.rows({ kind = "loc_set", where = function(r) return r.loc == SEEDLING or r.loc == ACID_POOL end })
        local seeds, pools = {}, {}
        for _, l in ipairs(locs or {}) do
            local key = (l.x - ox) .. "," .. (l.z - oz)
            if l.loc == SEEDLING and l.tick >= t0 then seeds[key] = true end
            if l.loc == ACID_POOL and l.tick < t0 and l.tick >= t0 - 16 then pools[key] = true end
        end
        local ok, list = true, {}
        for _, s in ipairs(BURST) do
            local key = s[1] .. "," .. s[2]
            local want = not pools[key]
            if (seeds[key] == true) ~= want then ok = false end
            list[#list + 1] = key .. (seeds[key] and " seeded" or " none") .. (pools[key] and " (pool)" or "")
        end
        t.check("op.burst.under_each", ok, table.concat(list, "; "))
    end
    -- off the seedlings before the burst (3 ticks)
    t.ticks(1)
    stand(BURST[role][1], BURST[role][2] + 2)
    wait(8)
    if not barrier("burst_done") then return end

    -- 4. THE TELEPORT, then on one tile
    local TELE = { { 13, 30 }, { 13, 33 }, { 19, 40 } }
    for round = 1, 2 do
        if round == 1 then stand(TELE[role][1], TELE[role][2]) else stand(15, 35) end
        t.ticks(2)
        if not barrier("tele_placed_" .. round) then return end
        if role == 1 then
            local _, t0 = t.tick()
            t.cheat("::coxolmdo 10", false)
            wait(14)
            local _, sp = t.ticklog.rows({ kind = "player_spotanim", where = function(r)
                return r.tick >= t0 and r.tick <= t0 + 1 and r.spotanim >= SWAP0 and r.spotanim <= SWAP3 end })
            t.ticks(1)
            local paired, colour = {}, {}
            for _, s in ipairs(sp or {}) do paired[s.pid] = true colour[s.pid] = s.spotanim end
            local pids = {}
            for pid in pairs(paired) do pids[#pids + 1] = pid end
            table.sort(pids)
            t.check("op.tele" .. round .. ".one_pair", #pids == 2 and colour[pids[1]] == colour[pids[2]],
                "paired pids " .. table.concat(pids, ",") .. " (want two of three, one colour)")
            if #pids == 2 then
                local _, tiles = t.ticklog.rows({ kind = "player_tile", where = function(r) return r.tick >= t0 and r.tick <= t0 + 12 end })
                t.ticks(1)
                local at_t = {}
                for _, r in ipairs(tiles or {}) do
                    at_t[r.pid] = at_t[r.pid] or {}
                    at_t[r.pid][r.tick] = r
                end
                local a0, b0 = at_t[pids[1]][t0], at_t[pids[2]][t0]
                local moved = nil
                for tick = t0 + 1, t0 + 12 do
                    local a = at_t[pids[1]][tick]
                    if a and a0 and (a.x ~= a0.x or a.z ~= a0.z) then moved = tick break end
                end
                local _, hits = t.ticklog.rows({ kind = "hit_player", where = function(r) return r.tick > t0 and r.tick <= t0 + 13 end })
                local got = {}
                for _, h in ipairs(hits or {}) do
                    if h.pid == pids[1] or h.pid == pids[2] then got[#got + 1] = string.format("p%d %d@+%d", h.pid, h.raw or h.damage, h.tick - t0) end
                end
                local d = (a0 and b0) and math.max(math.abs(a0.x - b0.x), math.abs(a0.z - b0.z)) or -1
                if round == 1 then
                    local want = 1 + (43 * d) // 10
                    local a1, b1 = moved and at_t[pids[1]][moved], moved and at_t[pids[2]][moved]
                    local swapped = a1 and b1 and a1.x == b0.x and a1.z == b0.z and b1.x == a0.x and b1.z == a0.z
                    local hit_ok = 0
                    for _, h in ipairs(hits or {}) do
                        if (h.pid == pids[1] or h.pid == pids[2]) and moved and h.tick == moved + 1 and (h.raw or h.damage) == want then hit_ok = hit_ok + 1 end
                    end
                    t.check("op.tele1.swap", swapped and moved == t0 + 10, string.format(
                        "the pair %d apart moved at +%s, swapped %s (want +10)", d, tostring(moved and moved - t0), tostring(swapped)))
                    t.check("op.tele1.hit", hit_ok == 2, string.format("hits %s (want %d on each at +11)", table.concat(got, " "), want))
                else
                    local tele_hit = false
                    for _, h in ipairs(hits or {}) do
                        if (h.pid == pids[1] or h.pid == pids[2]) and h.tick == t0 + 11 and (h.raw or h.damage) >= 5 then tele_hit = true end
                    end
                    t.check("op.tele2.no_effect", moved == nil and not tele_hit and d == 0, string.format(
                        "one tile: moved %s, hits %s (none at +11 is the teleport's)", tostring(moved), table.concat(got, " ")))
                end
            end
        else
            wait(15)
        end
        if not barrier("tele_done_" .. round) then return end
    end

    -- 5. BURN WITH ME: two side by side, one four tiles off, all in view
    local BURN = { { 14, 36 }, { 15, 36 }, { 19, 36 } }
    stand(BURN[role][1], BURN[role][2])
    t.ticks(2)
    if not barrier("burn_placed") then return end
    if role == 1 then
        local _, t0 = t.tick()
        t.cheat("::coxolmdo 4", false)
        wait(30)
        local _, proj = t.ticklog.rows({ kind = "projectile", where = function(r)
            return r.tick >= t0 and (r.spotanim == BURN_TRAVEL or r.spotanim == BURN_SPREAD) end })
        t.ticks(1)
        local target, spreads = nil, {}
        for _, p in ipairs(proj or {}) do
            local dst = (p.dst_x - ox) .. "," .. (p.dst_z - oz)
            if p.spotanim == BURN_TRAVEL then target = dst
            else spreads[#spreads + 1] = (p.src_x - ox) .. "," .. (p.src_z - oz) .. "->" .. dst end
        end
        local want
        if target == "14,36" then want = "14,36->15,36"
        elseif target == "15,36" then want = "15,36->14,36"
        else want = "" end
        t.check("op.burn.spreads_beside", target ~= nil and table.concat(spreads, " ") == want, string.format(
            "burn at %s; spreads %s (want %s)", tostring(target), table.concat(spreads, " "), want == "" and "none" or want))
    end
    wait(40)
    if not barrier("burn_done", 900) then return end
    t.finish(0)
end

return {
    id = "cox_olm_party",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 200000,
    setup = kit,
    run = run,
}
