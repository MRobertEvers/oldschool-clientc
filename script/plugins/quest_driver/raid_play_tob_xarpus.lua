-- quest-driver / raid_play_tob_xarpus: the Xarpus plan for t.raid.play
-- (raid_play.lua), its own driver part (raid seam29 play_library_own_files),
-- written by raid seam30 play_tob_xarpus.  PLAY_NOTES.md "Xarpus" is its
-- strategy table.  Entry, solo, melee (the scythe), from the sources line by
-- line; the abbreviations in the comments:
--   E   docs/minigames/theater_of_blood/sources/wiki_Theatre_of_Blood_Entry_Mode.wikitext
--   W   .../sources/wiki_Theatre_of_Blood_Strategies.wikitext
--   A   .../sources/wiki_Guide_Advanced_Theatre_of_Blood.wikitext
--   ET  docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md
--   X   docs/minigames/theater_of_blood/encounters/xarpus.tsv (our spec)
--   S   OSRS-Content/.../minigame_tob/scripts/tob_xarpus.rs2 and tob_damage.rs2
--       (what our server does; read to know WHEN a rule bites, never fed to
--       a decision as a hidden value)
--
-- What the plan SEES, all of it what a person at the screen sees: the
-- exhumed skeletons on the floor (loc tob_xarpus_exhumed), the acid pools on
-- the floor (loc tob_xarpus_acidpool), the acid in flight (projectile 1555,
-- its destination), his spit animation (seq 8059) and its tick, the square
-- he faces and the tick he turned (the npc row's face_x/face_z/face_tick),
-- his health bar, its own hitpoints, tile and swings.  What it never reads:
-- the tick log's server rows (loc_set, npc_face, hit rows), ::tobboss, the
-- quadrant register.

QD.raid._play_plan("tob_xarpus", {
    room = "xarpus",
    -- the combat form (phases 2 and 3); phase 1 is the feeding form, read by
    -- the decide itself (the library's boss row is absent until the stand-up)
    boss = { entry = "tob_xarpus_combat_story", normal = "tob_xarpus_combat", hard = "tob_xarpus_combat_hard" },
    feeding = { entry = "tob_xarpus_feeding_story", normal = "tob_xarpus_feeding", hard = "tob_xarpus_feeding_hard" },
    exhumed_loc = "tob_xarpus_exhumed",   -- X xarpus.av.exhumed_open.loc (32743)
    pool_loc = "tob_xarpus_acidpool",     -- X xarpus.av.splat_land.loc (32744)
    acid_proj = 1555,                     -- X xarpus.av.spit.proj / splat_chain.proj
    spit_seq = 8059,                      -- X xarpus.av.spit.seq
    -- X xarpus.p2.first_spit 7 (blert FIRST_P2_TURN_TICK = 7, ET 6.3) and
    -- xarpus.p2.cadence 4 (TICKS_PER_TURN_P2 = 4): the rhythm a person counts
    -- ("you step back every 4 ticks, based on Xarpus' attack speed", A:203)
    first_spit = 7, cadence = 4,
    -- X xarpus.p3.turn_cadence 8: "He will rotate every 8 ticks" (W:851)
    turn_cadence = 8,
    -- X xarpus.p3.screech_pct_entry 22.5 ("Xarpus will screech when below
    -- 22.5% health", E:212); the bar is read with a margin (it is 30 pixels)
    screech_pct = 25,
    -- the arena floor local to the room's 64x64 square (tob_xarpus.lua's
    -- 6427..6441 x 92..106 in square 6400,64) and the phase 1 waiting tile:
    -- "players are recommended to stand in the centre of the arena to quickly
    -- intercept any exhumed that appear" (W:831), on the tile south of him
    -- that is a melee tile once he stands up (tob_xarpus.lua :74 6434,95 is
    -- one further out)
    floor = { 27, 28, 41, 42 }, wait = { 34, 32 },
    -- "Protection prayers have no effect during the fight, so prayer points
    -- can be used instead to boost damage" (E:201): Piety from the stand-up
    walk_prayers = { "piety" },
    down_prayers = {},
    modes = {
        -- X xarpus.p2.max_hit.entry 6 (a splash or a pool tick), two chains
        -- per later spit (X xarpus.p2.chain_count 1,2), so up to three
        -- landings a cycle; X xarpus.p3.retaliate_min_entry 38 is never taken
        -- by this plan (it never swings into his gaze), the margin is a pool
        entry = { splash = 6, landings = 3, p3_margin = 12 },
        normal = { splash = 11, landings = 3, p3_margin = 24 },
        hard = { splash = 11, landings = 3, p3_margin = 24 },
    },
    decide = "_play_xarpus_decide",
})

-- quadrant of a tile relative to his centre: the server's own split
-- (S tob_xarpus_quadrant_from: "dz > 0" north, "dx > 0" east, so the centre
-- row is south and the centre column west).  W:851 "If a player attacks from
-- a corner that Xarpus is looking at, he will retaliate".
function QD.raid._xarpus_quadrant(cx, cz, x, z)
    local dx, dz = x - cx, z - cz
    if dz > 0 then
        if dx > 0 then return "NE" end
        return "NW"
    end
    if dx > 0 then return "SE" end
    return "SW"
end

-- What the room shows this tick (pools, exhumed, acid in flight).
function QD.raid._xarpus_see(st, v)
    local P = st.plan
    v.pools = {}
    local pr, _, prow = QD.world.loc_copies(P.pool_loc, 0)
    if pr == "ok" and prow ~= nil then
        for _, r in ipairs(prow) do v.pools[r.x * 100000 + r.z] = true end
    end
    v.exhumed = {}
    local er, _, erow = QD.world.loc_copies(P.exhumed_loc, 0)
    if er == "ok" and erow ~= nil then
        for _, r in ipairs(erow) do v.exhumed[#v.exhumed + 1] = { x = r.x, z = r.z } end
    end
    -- acid in flight: its destination and its 3x3 are where it will land
    -- ("These deal damage in a 3x3 area", E:207)
    v.incoming = {}
    local jr, projs = QD.world.projectiles(0)
    if jr == "ok" and type(projs) == "table" then
        for _, p in ipairs(projs) do
            if p.spotanim_id == P.acid_proj then
                v.incoming[#v.incoming + 1] = { x = p.dst_x, z = p.dst_z, ticks = math.floor((p.cycles_left or 0) / 30) }
            end
        end
    end
    v.splash = {}
    for _, p in ipairs(v.incoming) do
        for dx = -1, 1 do
            for dz = -1, 1 do v.splash[(p.x + dx) * 100000 + (p.z + dz)] = true end
        end
    end
end

-- THE XARPUS PLAN'S DECIDE (PLAY_NOTES.md "Xarpus").
function QD.raid._play_xarpus_decide(st, v)
    local P, N, O = st.plan, st.numbers, st.origin
    if st.xa == nil then
        st.xa = { covers = {}, dodges = {}, cover_now = nil, phase = 1, spits = {}, last_spit_seq = -1,
                  turns = {}, p3_face_base = nil, potion = false, stops = 0, moves3 = 0, waits3 = 0,
                  u_tick = nil, p3_tick = nil, lines = {} }
    end
    local X = st.xa
    QD.raid._xarpus_see(st, v)
    local intent = { want = {}, walk = nil, attack = false }
    local function floor_ok(x, z)
        return x >= O.x + P.floor[1] and x <= O.x + P.floor[3] and z >= O.z + P.floor[2] and z <= O.z + P.floor[4]
    end
    local b = v.boss

    -- ===== PHASE 1: the exhumed =====
    -- "To prevent the exhumed from healing Xarpus, players simply need to
    -- stand on top of them until they return to the ground" (W:831); "Simply
    -- run around the arena standing on any exhumeds that appear" (E:204).
    if b == nil then
        if X.u_tick ~= nil then
            return intent
        end
        local target = nil
        local best = nil
        for _, e in ipairs(v.exhumed) do
            local d = math.max(math.abs(e.x - v.me.x), math.abs(e.z - v.me.z))
            if best == nil or d < best then best, target = d, e end
        end
        if target ~= nil then
            local key = target.x * 100000 + target.z
            if X.cover_now == nil or X.cover_now.key ~= key then
                X.cover_now = { key = key, x = target.x, z = target.z, seen = v.tick }
            end
            if v.me.x == target.x and v.me.z == target.z then
                if X.cover_now.arrive == nil then
                    X.cover_now.arrive = v.tick
                    X.covers[#X.covers + 1] = { rise = X.cover_now.seen, x = target.x, z = target.z, arrive = v.tick, at_x = v.me.x, at_z = v.me.z }
                end
                -- the quiet phase 1: the super combat potion, drunk while
                -- standing on an exhumed (tob_xarpus.lua :120-123 drinks it on
                -- the fifth; the boost lasts the fight)
                if not X.potion and #X.covers >= 5 then
                    local cr, n = QD.inv.count("4dose2combat")
                    if cr == "ok" and n > 0 then intent.drink = "4dose2combat" end
                    X.potion = true
                end
            else
                local same = st.walk_target ~= nil and st.walk_target.x == target.x and st.walk_target.z == target.z
                if not same then intent.walk = { x = target.x, z = target.z } end
            end
        else
            local wx, wz = O.x + P.wait[1], O.z + P.wait[2]
            local same = st.walk_target ~= nil and st.walk_target.x == wx and st.walk_target.z == wz
            if (v.me.x ~= wx or v.me.z ~= wz) and not same then intent.walk = { x = wx, z = wz } end
        end
        return intent
    end

    -- ===== PHASES 2 AND 3: the combat form =====
    if X.u_tick == nil then
        X.u_tick = v.tick
        X.next_spit = v.tick + P.first_spit
    end
    intent.want.piety = true
    local n = b.size or 5
    local BX0, BZ0, BX1, BZ1 = b.x, b.z, b.x + n - 1, b.z + n - 1
    local cx, cz = b.x + math.floor(n / 2), b.z + math.floor(n / 2)
    local function in_foot(x, z) return x >= BX0 and x <= BX1 and z >= BZ0 and z <= BZ1 end
    local function edge(x, z)
        local ox = math.max(BX0 - x, x - BX1, 0)
        local oz = math.max(BZ0 - z, z - BZ1, 0)
        return math.max(ox, oz) == 1 and (ox == 0 or oz == 0)
    end
    local function clean(x, z)
        local k = x * 100000 + z
        return floor_ok(x, z) and not in_foot(x, z) and not v.pools[k] and not v.splash[k]
    end
    -- the nearest clean melee tile (an edge tile: melee reaches a 5x5 from a
    -- tile sharing an edge with it, raid_play.lua _play_reach) whose quadrant
    -- `allow(q)` accepts, ranked by the run to it; nil when none.  The
    -- attack press's own path is the server's and goes through anything
    -- (raid seam29 _play_reach), so the plan walks to the tile first and
    -- presses from it: "never stand in one" (E:207).
    -- A walk's route on open floor was seen both ways (raid_play.lua
    -- _play_safe_step: diagonal first, and straight along the longer axis
    -- first); players walk through npcs, and a tick ended inside his
    -- footprint draws the pebble stomp and skips his spit ("Do not get too
    -- close to Xarpus, or he will throw pebbles", E:212; xa30d t194: a run
    -- from 6431,99 toward the north side ended on 6432,101).  A route is
    -- good when neither shape steps inside the footprint or onto acid.
    local function sign(k) if k > 0 then return 1 elseif k < 0 then return -1 end return 0 end
    local function route_ok(tx, tz)
        for _, straight in ipairs({ false, true }) do
            local x, z = v.me.x, v.me.z
            local guard = 0
            while (x ~= tx or z ~= tz) and guard < 30 do
                local dx, dz = tx - x, tz - z
                if straight and math.abs(dx) > math.abs(dz) then
                    x = x + sign(dx)
                elseif straight and math.abs(dz) > math.abs(dx) then
                    z = z + sign(dz)
                else
                    x, z = x + sign(dx), z + sign(dz)
                end
                if in_foot(x, z) or v.pools[x * 100000 + z] then return false end
                guard = guard + 1
            end
        end
        return true
    end
    local function nearest_edge(allow, prefer)
        local bt, bd = nil, nil
        for x = BX0 - 1, BX1 + 1 do
            for z = BZ0 - 1, BZ1 + 1 do
                if edge(x, z) and clean(x, z) and route_ok(x, z) then
                    local q = QD.raid._xarpus_quadrant(cx, cz, x, z)
                    if allow(q) then
                        local d = math.max(math.abs(x - v.me.x), math.abs(z - v.me.z)) * 10 + ((prefer ~= nil and q ~= prefer) and 5 or 0)
                        if bd == nil or d < bd then bd, bt = d, { x = x, z = z } end
                    end
                end
            end
        end
        return bt
    end
    local offset = v.tick - v.api_now
    -- his spit, seen: the rhythm is a grid (X xarpus.p2.cadence 4, grade A:
    -- "every 4 ticks", ET 6.3) anchored on the stand-up; a spit is SEEN on
    -- the server tick the plan first reads its animation (the client's own
    -- seq_tick is in client ticks and wanders by one against the server's:
    -- xa30a saw 147 as 146), up to two ticks late (a dodge's two steps take
    -- the ticks after S-1), and the grid moves only when a spit is seen
    -- before its slot or later than that
    if b.seq_id == P.spit_seq and b.seq_tick ~= X.last_spit_seq then
        X.last_spit_seq = b.seq_tick
        local s = v.tick
        local slot = X.next_spit
        if slot - P.cadence >= X.u_tick and math.abs(s - (slot - P.cadence)) < math.abs(s - slot) then slot = slot - P.cadence end
        local lag = s - slot
        X.seen_lag = X.seen_lag or {}
        X.seen_lag[lag] = (X.seen_lag[lag] or 0) + 1
        if lag >= 0 and lag <= 2 then
            X.spits[#X.spits + 1] = slot
        else
            X.spits[#X.spits + 1] = s
            X.next_spit = s
            X.regrid = (X.regrid or 0) + 1
        end
        if X.next_spit <= X.spits[#X.spits] then X.next_spit = X.spits[#X.spits] + P.cadence end
    end
    while X.next_spit ~= nil and X.next_spit < v.tick - 1 do
        -- a slot with no spit seen two ticks after it: with the bar low, that
        -- is the screech ("Xarpus will screech ... and will stop launching
        -- poison around the arena", E:212)
        local missed = X.next_spit
        X.next_spit = X.next_spit + P.cadence
        local low = (b.health_scale or 0) > 0 and b.health_ratio * 100 <= P.screech_pct * b.health_scale
        if X.phase == 2 and low and (#X.spits == 0 or X.spits[#X.spits] < missed - 1) then
            X.phase = 3
            X.p3_tick = v.tick
            X.p3_face_base = b.face_tick
        end
    end
    if X.phase == 1 then X.phase = 2 end
    local myq = QD.raid._xarpus_quadrant(cx, cz, v.me.x, v.me.z)
    local function threat(h)
        if X.phase == 3 then return N.p3_margin end
        return N.splash * N.landings * math.max(1, math.ceil(h / P.cadence)) + N.splash
    end

    if X.phase == 2 then
        -- ---- PHASE 2: the spit ----
        -- The scan reads the tile "AS OF THE END OF T-1" (ET 6.2) and the acid
        -- lands on that tile with a 3x3 splash (E:207, X xarpus.p2.splat_radius
        -- 1).  The dodge: two one-tile steps, the first sent on S-1 so it
        -- resolves on S (after his scan: he aims at the tile just left), the
        -- second resolving on S+1, so the splat lands two tiles from where the
        -- player now stands -- the Entry page's "moving ... exactly 2 tiles at
        -- a time to avoid the poison" (E:207), timed "just before you see the
        -- projectile" (E:207), with the melee player's step on the spit rhythm
        -- (A:203, W:844) -- tob_xarpus.lua :445-471's recipe, unchanged.
        -- "never stand in one" (a pool hurts its own tile, E:207 "dealing some
        -- damage if you stand or run over it"): the second tile is a clean
        -- melee tile when one exists, the first not a pool when it can be.
        local S = X.next_spit
        if v.tick == S - 1 then
            local px, pz = v.me.x, v.me.z
            local best = nil
            for dx = -2, 2 do
                for dz = -2, 2 do
                    if math.max(math.abs(dx), math.abs(dz)) == 2 then
                        local qx, qz = px + dx, pz + dz
                        if floor_ok(qx, qz) and not in_foot(qx, qz) then
                            local qk = qx * 100000 + qz
                            local pen = ((v.pools[qk] or v.splash[qk]) and 20 or 0)
                            for ax = -1, 1 do
                                for az = -1, 1 do
                                    local mx, mz = px + ax, pz + az
                                    if not (ax == 0 and az == 0) and math.max(math.abs(qx - mx), math.abs(qz - mz)) <= 1 then
                                        if floor_ok(mx, mz) and not in_foot(mx, mz) then
                                            local mk = mx * 100000 + mz
                                            local qox = math.max(BX0 - qx, qx - BX1, 0)
                                            local qoz = math.max(BZ0 - qz, qz - BZ1, 0)
                                            local qd = math.max(qox, qoz)
                                            local score = pen + ((v.pools[mk] or v.splash[mk]) and 10 or 0)
                                            if qd == 1 and (qox == 0 or qoz == 0) then
                                                score = score + 0
                                            elseif qd == 1 then
                                                score = score + 4
                                            else
                                                score = score + 8 + qd
                                            end
                                            if best == nil or score < best.score then
                                                best = { score = score, t1x = mx, t1z = mz, t2x = qx, t2z = qz }
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
            if best ~= nil then
                local r1, d1 = QD.player.step_tick(best.t1x, best.t1z)
                st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
                local _, mid = QD.tick()
                local r2, d2 = QD.player.step_tick(best.t2x, best.t2z)
                st.inputs[mid] = (st.inputs[mid] or 0) + 1
                X.dodges[#X.dodges + 1] = { S = S, k = #X.dodges + 1, from_x = px, from_z = pz, r1 = r1, d1 = d1,
                    r2 = r2, d2 = d2, score = best.score, t2x = best.t2x, t2z = best.t2z }
                st.dodges = st.dodges + 1
                st.engaged = false
                st.walk_target = nil
                -- the press brings the next swing from the new tile (A:211
                -- "you swing while running in")
                intent.attack = true
                intent.attack = QD.raid._play_attack(st, v, true)
                return intent
            end
        end
        -- between dodges: swing on cooldown from a melee tile; a pool or a
        -- landing under the player is stepped off (its 3x3 is judged on the
        -- tile held at the end of the tick before it lands, ET 1.2)
        local here = v.me.x * 100000 + v.me.z
        local danger = v.pools[here] or v.splash[here]
        if (danger or not edge(v.me.x, v.me.z)) and v.tick < S - 1 then
            local tgt = nearest_edge(function() return true end, nil)
            if tgt ~= nil then
                local same = st.walk_target ~= nil and st.walk_target.x == tgt.x and st.walk_target.z == tgt.z
                local stuck = st.last_me ~= nil and st.last_me.x == v.me.x and st.last_me.z == v.me.z
                if not same or stuck then intent.walk = tgt end
                X.edge_walks = (X.edge_walks or 0) + 1
            end
        end
        intent.attack = intent.walk == nil
        intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
        -- no bite on the tick before a dodge: the dodge's step must be the
        -- tick's move (tob_xarpus.lua :946 counts a dodge late after an eat)
        if v.tick >= S - 2 then intent.eat = nil end
        intent.attack = QD.raid._play_attack(st, v, intent.attack)
        return intent
    end

    -- ---- PHASE 3: the gaze ----
    -- "If a player attacks from a corner that Xarpus is looking at, he will
    -- retaliate back with an unblockable poison hit dealing 50-80 damage for
    -- each hitsplat" (W:851); "Simply stay in one location, attacking Xarpus
    -- once with Melee when he turns to face one of the other quadrants, then
    -- click on the ground to stop attacking" (E:212).  He turns every 8 ticks
    -- and "will never look in the same corner twice" (W:851-853).  Our server
    -- punishes a hit from the faced quadrant only after his first turn and
    -- never on the turn's own tick (S tob_damage.rs2:430-437), which is the
    -- 22121 window arithmetic of A:225-227 (a 5-tick weapon fits 1 or 2
    -- swings in each 8-tick gaze).
    local face_q, turn_tick = nil, nil
    if b.face_tick ~= nil and b.face_tick >= 0 and X.p3_face_base ~= nil and b.face_tick > X.p3_face_base then
        face_q = QD.raid._xarpus_quadrant(cx, cz, b.face_x, b.face_z)
        turn_tick = b.face_tick + offset
        if X.turns[#X.turns] == nil or X.turns[#X.turns].tick ~= turn_tick then
            X.turns[#X.turns + 1] = { tick = turn_tick, q = face_q }
        end
    end
    local next_swing = QD.raid._play_next_swing(st, v)
    -- a swing is allowed when it lands before his next turn: the turn tick
    -- itself is safe (S tob_damage.rs2:433), one tick is kept as margin for
    -- a turn read a tick late
    local window_end = (turn_tick ~= nil) and (turn_tick + P.turn_cadence - 1) or (X.p3_tick + P.turn_cadence - 2)
    local function allowed(q) return face_q == nil or q ~= face_q end
    local here3 = v.me.x * 100000 + v.me.z
    local on_acid = v.pools[here3] or v.splash[here3]
    local can_swing = next_swing <= window_end
    local target = nil
    local want_attack = false
    if on_acid or (face_q ~= nil and myq == face_q) then
        -- off a pool, or he looks at the player's quadrant: to the nearest
        -- clean melee tile of a quadrant he is not looking at ("players should
        -- be moving to where he last looked if possible", W:853: the quadrant
        -- he looked at last is preferred)
        local prev = (#X.turns >= 2) and X.turns[#X.turns - 1].q or nil
        target = nearest_edge(allowed, prev)
        X.moves3 = X.moves3 + 1
        if target == nil then
            -- no clean melee tile is reachable around him this tick: never
            -- swing from here (the stop below takes the player one tile out)
            can_swing = false
        end
    end
    if target ~= nil or want_attack then
        -- decided above
    elseif can_swing and not (on_acid or (face_q ~= nil and myq == face_q)) then
        if edge(v.me.x, v.me.z) then
            want_attack = true
        else
            -- back onto a clean melee tile of this quadrant (or another he
            -- is not looking at) before the press
            target = nearest_edge(allowed, myq)
        end
    elseif st.engaged then
        -- stop: "click on the ground to stop attacking" (E:212) -- one tile
        -- out from him, in the same quadrant, off the acid
        local bx, bz, bd = nil, nil, nil
        for dx = -1, 1 do
            for dz = -1, 1 do
                local x, z = v.me.x + dx, v.me.z + dz
                if (dx ~= 0 or dz ~= 0) and clean(x, z) and QD.raid._xarpus_quadrant(cx, cz, x, z) == myq then
                    local ox = math.max(BX0 - x, x - BX1, 0)
                    local oz = math.max(BZ0 - z, z - BZ1, 0)
                    local d = (math.max(ox, oz) == 2 and 0 or 3) + math.abs(dx) + math.abs(dz)
                    if bd == nil or d < bd then bx, bz, bd = x, z, d end
                end
            end
        end
        if bx ~= nil then
            target = { x = bx, z = bz }
            X.stops = X.stops + 1
        end
    else
        X.waits3 = X.waits3 + 1
    end
    if target ~= nil then
        local same = st.walk_target ~= nil and st.walk_target.x == target.x and st.walk_target.z == target.z
        local stuck = st.last_me ~= nil and st.last_me.x == v.me.x and st.last_me.z == v.me.z
        if (v.me.x ~= target.x or v.me.z ~= target.z) and (not same or stuck) then intent.walk = target end
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    intent.attack = QD.raid._play_attack(st, v, want_attack and intent.walk == nil)
    return intent
end
