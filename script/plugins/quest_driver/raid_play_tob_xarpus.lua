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
--
-- ==========================================================================
-- raid seam53 play_state_machines: THE ROOM IS DECLARED AS STATE MACHINES
-- (raid_sm.lua, QD.raid.sm_declare), 2026-10-07.  The owner: "all the rooms
-- should be explicit state machines."
--
-- Nothing about what this plan DOES changed in that port.  What changed is
-- where the decision lives.  Before, one `decide` held the whole room: his
-- phase in `X.phase` (1, 2, 3) with the transitions written inline (`if
-- X.phase == 1 then X.phase = 2 end`, a `while` loop that set 3), his death
-- as an early `return` on a nil boss row, and each seat's mode as a chain of
-- conditions recomputed per tick (`mode = "out" / "press" / "in"` from the
-- tick's distance to the next spit; four `if/elseif` branches for the gaze).
-- Reading it, you could not say what the states were without running the
-- arithmetic in your head, and a new one meant finding every branch that
-- named its neighbours.
--
-- Now there are FIVE machines, declared as data, and this file reads as
-- what the room is:
--
--   xarpus_room     feeding -> standup -> spit -> gaze -> dead   (his phases)
--   xarpus_exhumed  waiting / hunting / covering                 (phase 1 seat)
--   xarpus_spit_solo    melee / clearing / dodging               (phase 2 seat, solo)
--   xarpus_spit_trio    in_melee / stepping_out / holding_out / pressing_in
--   xarpus_gaze     swinging / relocating / stopping / holding    (phase 3 seat)
--
-- THE PER-TICK CONTRACT is the owner's, unchanged: Events + State -> Intents,
-- and raid_play.lua's executor reconciles the intents per channel.  The
-- events are derived ONCE a tick, in ONE place -- QD.raid._xarpus_events --
-- so every machine on the raider sees the same reading of the tick, and no
-- state derives a private view of it.  The derivation says WHAT HAPPENED
-- (his form changed, a spit was seen, a slot passed with no spit, he turned,
-- an exhumed of mine is up, this tile is in his gaze); the states say WHAT TO
-- DO.  The intents are mutated onto the tick's one intent table, which the
-- decide returns, exactly as the hand-rolled body did.
--
-- TRANSITIONS ARE EXPLICIT BOTH WAYS.  Every forward edge has the backward
-- edge beside it: `spit` goes to `gaze` on the screech and `gaze` goes back
-- to `spit` if he ever launches poison again (the screech read was wrong --
-- X.regaze counts it, and it is 0 in every measured run); `standup` goes
-- back to `feeding` if his combat row leaves before the fight begins; `dead`
-- goes back to the phase it came from if his row returns.  A seat's cycle is
-- a ring: every state of it names every one of the four cycle events, so
-- the step-back rhythm reads as the ring it is instead of as arithmetic on
-- `d`.
--
-- PER-SEAT, NOT PER-ROLE-KIND: the three seats of the Normal trio are three
-- INSTANCES of the same declarations (all three are melee, the scythe, the
-- same rhythm), differing only in the data their role indexes -- the wait
-- tile P.trio.waits[st.role], and the side of him that is theirs (`mine`).
-- Each raider runs its own client, so each holds its own instance of
-- xarpus_room, xarpus_exhumed, xarpus_spit_trio and xarpus_gaze.
--
-- NO DAWNBRINGER HERE.  The Dawnbringer is Verzik's (it is picked up off
-- Xarpus' floor on the way out, test/raids/_play_entry.lua POST.xarpus
-- "xarpus.dawnbringer"); nothing in this plan carries or swings it.  The
-- special weapon this plan does carry is the Dragon warhammer, for the
-- Defence drain, and it is declared in the trio's step-back state.
-- ==========================================================================

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
        -- raid seam34x: X xarpus.p2.max_hit.normal 11; X xarpus.p3.screech_pct
        -- 25 ("Upon reaching ~25% of his health, Xarpus will screech", W:851),
        -- read with the Entry margin (+2.5 of a 30-pixel bar)
        normal = { splash = 11, landings = 3, p3_margin = 24, screech_pct = 27.5 },
        hard = { splash = 11, landings = 3, p3_margin = 24, screech_pct = 27.5 },
    },
    -- raid seam34x play_tob_xarpus_normal: THE TRIO (PLAY_NOTES.md "Xarpus,
    -- Normal trio").  Phase 1: the exhumed in turn, each raider waiting on
    -- its own tile between them (34,32 south -- the Entry wait -- 31,35 west,
    -- 37,35 east: melee tiles of the 5x5 he stands up into, local 32..36 x
    -- 33..37).  Phase 2: one stack, formed on `home` (the Entry wait tile).
    -- raid seam42: `spread` -- phase 2 on the three wait tiles, the spit's
    -- own target staying in (xarpus_spit_trio's THE SPREAD; Blert reference).
    trio = { waits = { { 34, 32 }, { 31, 35 }, { 37, 35 } }, home = { 34, 32 }, specs = 2, spread = true,
             p1_nearest = true },
    trace_trio = false,
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
                v.incoming[#v.incoming + 1] = { x = p.dst_x, z = p.dst_z, sx = p.src_x, sz = p.src_z, ticks = math.floor((p.cycles_left or 0) / 30) }
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

-- ==========================================================================
-- THE TICK'S GEOMETRY.  His footprint moves (he stands up into a 5x5 whose
-- corner the npc row gives), so every one of these is a function of THIS
-- tick's boss row; before the port they were locals of the decide, and the
-- declared states now read them off the tick's context.  The arithmetic is
-- the hand-rolled body's, line for line.
-- ==========================================================================
function QD.raid._xarpus_geometry(st, v)
    assert(st, "_xarpus_geometry: st")
    assert(v, "_xarpus_geometry: v")
    local P, N, O, X = st.plan, st.numbers, st.origin, st.xa
    local b = v.boss
    local g = {}
    g.floor_ok = function(x, z)
        return x >= O.x + P.floor[1] and x <= O.x + P.floor[3] and z >= O.z + P.floor[2] and z <= O.z + P.floor[4]
    end
    -- the threat a supply decision is judged against: in phase 2 a spit's
    -- splash and its chains over the ticks to the next slot, in phase 3 the
    -- pool margin (this plan never swings into his gaze, so X
    -- xarpus.p3.retaliate_min_entry is never taken)
    g.threat = function(h)
        if X.phase == 3 then return N.p3_margin end
        return N.splash * N.landings * math.max(1, math.ceil(h / P.cadence)) + N.splash
    end
    if b == nil then return g end
    local n = b.size or 5
    local BX0, BZ0, BX1, BZ1 = b.x, b.z, b.x + n - 1, b.z + n - 1
    g.BX0, g.BZ0, g.BX1, g.BZ1 = BX0, BZ0, BX1, BZ1
    g.cx, g.cz = b.x + math.floor(n / 2), b.z + math.floor(n / 2)
    g.in_foot = function(x, z) return x >= BX0 and x <= BX1 and z >= BZ0 and z <= BZ1 end
    g.edge = function(x, z)
        local ox = math.max(BX0 - x, x - BX1, 0)
        local oz = math.max(BZ0 - z, z - BZ1, 0)
        return math.max(ox, oz) == 1 and (ox == 0 or oz == 0)
    end
    g.clean = function(x, z)
        local k = x * 100000 + z
        return g.floor_ok(x, z) and not g.in_foot(x, z) and not v.pools[k] and not v.splash[k]
    end
    -- A walk's route on open floor was seen both ways (raid_play.lua
    -- _play_safe_step: diagonal first, and straight along the longer axis
    -- first); players walk through npcs, and a tick ended inside his
    -- footprint draws the pebble stomp and skips his spit ("Do not get too
    -- close to Xarpus, or he will throw pebbles", E:212; xa30d t194: a run
    -- from 6431,99 toward the north side ended on 6432,101).  A route is
    -- good when neither shape steps inside the footprint or onto acid.
    local function sign(k) if k > 0 then return 1 elseif k < 0 then return -1 end return 0 end
    g.route_ok = function(tx, tz)
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
                if g.in_foot(x, z) or v.pools[x * 100000 + z] then return false end
                guard = guard + 1
            end
        end
        return true
    end
    -- the nearest clean melee tile (an edge tile: melee reaches a 5x5 from a
    -- tile sharing an edge with it, raid_play.lua _play_reach) whose quadrant
    -- `allow(q)` accepts, ranked by the run to it; nil when none.  The
    -- attack press's own path is the server's and goes through anything
    -- (raid seam29 _play_reach), so the plan walks to the tile first and
    -- presses from it: "never stand in one" (E:207).
    --
    -- raid seam35e play_tob_entry_relay: `loose` drops the route test.  The
    -- relay's svdplayentry ran a P2 of 95 ticks (the room test's is 61): in
    -- P3 every clean melee tile of a quadrant he was not watching lay behind
    -- a pool on both route shapes, nearest_edge answered nil on every tick,
    -- and the raider stood still for 700 ticks with Xarpus at 22 percent.
    -- P3 asks loose only after the strict answer is nil, so a room where a
    -- clean route exists plays exactly as before.
    g.nearest_edge = function(allow, prefer, loose)
        local bt, bd = nil, nil
        for x = BX0 - 1, BX1 + 1 do
            for z = BZ0 - 1, BZ1 + 1 do
                if g.edge(x, z) and g.clean(x, z) and (loose or g.route_ok(x, z)) then
                    local q = QD.raid._xarpus_quadrant(g.cx, g.cz, x, z)
                    if allow(q) then
                        local d = math.max(math.abs(x - v.me.x), math.abs(z - v.me.z)) * 10 + ((prefer ~= nil and q ~= prefer) and 5 or 0)
                        if bd == nil or d < bd then bd, bt = d, { x = x, z = z } end
                    end
                end
            end
        end
        return bt
    end
    return g
end

-- THE TRIO'S OWN TICK STATE, created on first use: the step-back ledger the
-- trio states and the derivation share (its counters are the harness rows).
function QD.raid._xarpus_trio_state(X)
    assert(X, "_xarpus_trio_state: X")
    X.trio = X.trio or { outs = 0, ins = 0, no_out = 0, home_walks = 0, holds = 0, off_acid = 0, out_list = {} }
    return X.trio
end

-- ==========================================================================
-- THE TRIO'S TOOLS, the same per-tick functions the hand-rolled
-- _xarpus_p2_trio held as locals: the step-back tile, whose side a tile is,
-- and the melee tile to run back to.
-- ==========================================================================
function QD.raid._xarpus_trio_tools(st, v, g)
    assert(st, "_xarpus_trio_tools: st")
    assert(v, "_xarpus_trio_tools: v")
    assert(g, "_xarpus_trio_tools: g")
    local P, O, X = st.plan, st.origin, st.xa
    local T = QD.raid._xarpus_trio_state(X)
    local t = {}
    local function fdist(x, z)
        return math.max(g.BX0 - x, x - g.BX1, g.BZ0 - z, z - g.BZ1, 0)
    end
    t.fdist = fdist
    t.bad = function(x, z)
        local k = x * 100000 + z
        return (not g.floor_ok(x, z)) or g.in_foot(x, z) or v.pools[k] or v.splash[k]
    end
    -- the spread's sides: a tile is this raider's when its own wait tile is
    -- the nearest of the three (ties to the lower role), so two raiders never
    -- share a step-back tile or a melee tile, and a chain or a spit aimed at
    -- one never lands beside another
    t.mine = function(x, z)
        if not P.trio.spread then return true end
        local function d2(w) local a, b = x - (O.x + w[1]), z - (O.z + w[2]); return a * a + b * b end
        local own = d2(P.trio.waits[st.role])
        for r, w in ipairs(P.trio.waits) do
            if r ~= st.role and r <= st.party then
                local o = d2(w)
                if o < own or (o == own and r < st.role) then return false end
            end
        end
        return true
    end
    -- the step-back tile from (fx, fz): two or three out from his footprint,
    -- clean, within one run tick (raid seam42: a function, so the spread can
    -- ask it of a melee tile before standing there)
    t.out_from = function(fx, fz, loose)
        local prev = T.prev
        local best, bs, bk = nil, nil, nil
        for dx = -2, 2 do
            for dz = -2, 2 do
                local x, z = fx + dx, fz + dz
                local fd = fdist(x, z)
                local reach = math.max(math.abs(dx), math.abs(dz))
                if reach > 0 and (fd == 2 or fd == 3) and not t.bad(x, z) and (loose or t.mine(x, z)) then
                    local s = ((fd == 3) and 0 or 20) + reach
                    -- one from the last step-back tile only when nothing else
                    -- is clean (a spit aimed there splashes this one)
                    if prev ~= nil and math.max(math.abs(prev.x - x), math.abs(prev.z - z)) < 2 then s = s + 40 end
                    -- toward fresh floor: every puddle already within two of
                    -- the tile counts against it, so the stack walks on round
                    -- him to a side it has not used instead of filling one
                    for ax = -2, 2 do
                        for az = -2, 2 do
                            if v.pools[(x + ax) * 100000 + (z + az)] and fdist(x + ax, z + az) >= 2 then s = s + 2 end
                        end
                    end
                    if reach == 2 then
                        -- the run's middle tile is passed, never stood on at
                        -- a tick's end (a landing is judged on that): only a
                        -- puddle there hurts ("running over it", E:207)
                        local mx = fx + ((dx > 0) and 1 or ((dx < 0) and -1 or 0))
                        local mz = fz + ((dz > 0) and 1 or ((dz < 0) and -1 or 0))
                        if v.pools[mx * 100000 + mz] or g.in_foot(mx, mz) then s = nil end
                    end
                    -- the same tile for every raider: ties by the tile itself
                    local k = x * 100000 + z
                    if s ~= nil and (bs == nil or s < bs or (s == bs and k < bk)) then bs, best, bk = s, { x = x, z = z }, k end
                end
            end
        end
        return best
    end
    -- raid seam42: the run back in ends on the melee tile nearest the
    -- step-back tile.  When that one is no longer this raider's to use (no
    -- clean step-back tile left in reach of it, or another raider's side),
    -- walk to the nearest one that is instead and swing from there: a
    -- raider who has to move along the row while the next step back is
    -- due is a tick late for it, and the chain lands on the melee tile.
    t.nearest_edge_to = function(fx, fz, want)
        local bt, bd, bk = nil, nil, nil
        for x = g.BX0 - 1, g.BX1 + 1 do
            for z = g.BZ0 - 1, g.BZ1 + 1 do
                if g.edge(x, z) and not t.bad(x, z) and (not want or (t.mine(x, z) and t.out_from(x, z) ~= nil)) then
                    local dd = math.max(math.abs(x - fx), math.abs(z - fz))
                    local k = x * 100000 + z
                    if bd == nil or dd < bd or (dd == bd and k < bk) then bt, bd, bk = { x = x, z = z }, dd, k end
                end
            end
        end
        return bt
    end
    return t
end

-- ==========================================================================
-- PHASE 1's BOOKKEEPING, which is derivation and not decision: which exhumed
-- are up, which sank, and -- in the trio -- whose each one is.
--
-- "players simply need to stand on top of them until they return to the
-- ground ... stand in the centre of the arena to quickly intercept any
-- exhumed that appear" (W:831; 12 exhumed in trios, W:829; X
-- xarpus.p1.exhumed_count.normal 12 at party 3, spawn_gap.normal 8,
-- open_ticks.normal 11).
--
-- THE ORDER THEY ARE TAKEN IN.  raid seam42: the raider whose wait tile is
-- nearest takes it, unless that raider already holds one still up (then the
-- nearest free one).  Real trios each cover a part of the room (nearest
-- other raider 5 [3-6] tiles away, Xarpus 4 [2-4]) and let 96 [50-182] hp of
-- heal orbs through; the turn-by-turn split (every raider 24 ticks apart,
-- which is what `p1_nearest = false` keeps) let 204 through because the
-- owner was often across the room (reference/xarpus_normal_3.json
-- outcome.phase.start.boss_heal).
-- ==========================================================================
function QD.raid._xarpus_exhumed_track(st, v)
    local P, X = st.plan, st.xa
    X.ex_seen = X.ex_seen or {}
    X.ex_count = X.ex_count or 0
    X.ex_list = X.ex_list or {}
    local live, fresh, sank = {}, {}, {}
    for _, e in ipairs(v.exhumed) do
        local key = e.x * 100000 + e.z
        live[key] = true
        local rec = X.ex_seen[key]
        if rec == nil or rec.gone ~= nil then fresh[#fresh + 1] = { key = key, x = e.x, z = e.z } end
    end
    for _, rec in ipairs(X.ex_list) do
        if rec.gone == nil and not live[rec.key] then
            rec.gone = v.tick
            sank[#sank + 1] = rec
        end
    end
    table.sort(fresh, function(a, b) return a.key < b.key end)
    local risen = {}
    for _, f in ipairs(fresh) do
        X.ex_count = X.ex_count + 1
        local owner = ((X.ex_count - 1) % st.party) + 1
        if P.trio.p1_nearest then
            local busy = {}
            for _, rec in ipairs(X.ex_list) do
                if rec.gone == nil then busy[rec.owner] = true end
            end
            local best, bd, bfree = nil, nil, nil
            for r = 1, st.party do
                local w = P.trio.waits[r] or P.wait
                local a, b = f.x - (st.origin.x + w[1]), f.z - (st.origin.z + w[2])
                local dd = a * a + b * b
                local free = not busy[r]
                if best == nil or (free and not bfree) or (free == bfree and dd < bd) then
                    best, bd, bfree = r, dd, free
                end
            end
            owner = best
        end
        local rec = { key = f.key, k = X.ex_count, rise = v.tick, x = f.x, z = f.z, owner = owner }
        X.ex_seen[f.key] = rec
        X.ex_list[#X.ex_list + 1] = rec
        risen[#risen + 1] = rec
    end
    return risen, sank
end

-- THE EXHUMED THIS SEAT IS FOR, this tick: the trio's own (the lowest-k
-- still up that the split gave this raider), or -- solo -- the nearest one on
-- the floor ("Simply run around the arena standing on any exhumeds that
-- appear", E:204).  Returned in one shape so the covering state writes one
-- row: `rec` is the record that remembers the arrival, `rise` the tick it
-- came up, `k` its number in the room (nil solo, which has no queue).
function QD.raid._xarpus_cover_target(st, v)
    local X = st.xa
    if (st.party or 1) > 1 then
        local target = nil
        for _, rec in ipairs(X.ex_list or {}) do
            if rec.gone == nil and rec.owner == st.role and (target == nil or rec.k < target.k) then target = rec end
        end
        if target == nil then return nil end
        return { rec = target, x = target.x, z = target.z, rise = target.rise, k = target.k }
    end
    local target, best = nil, nil
    for _, e in ipairs(v.exhumed) do
        local d = math.max(math.abs(e.x - v.me.x), math.abs(e.z - v.me.z))
        if best == nil or d < best then best, target = d, e end
    end
    if target == nil then return nil end
    local key = target.x * 100000 + target.z
    if X.cover_now == nil or X.cover_now.key ~= key then
        X.cover_now = { key = key, x = target.x, z = target.z, seen = v.tick }
    end
    return { rec = X.cover_now, x = target.x, z = target.z, rise = X.cover_now.seen }
end

-- ==========================================================================
-- THE EVENTS, derived once a tick in this one place (QD.raid.sm_events calls
-- this at most once per tick and caches the list, so every machine on the
-- raider sees the same reading).  Every mutation of the tick GRID lives
-- here -- the stand-up anchor, the spits seen, the slots that passed, the
-- turns -- because all of that is what happened, not what to do.
--
-- `ev.name`s, in the order they are raised:
--   form_feeding  his combat row is not on the screen (he is lying down
--                 healing before the stand-up, or he is dead after it)
--   form_combat   his combat row is on the screen
--   cover_due     an exhumed this seat is for is up and it is not under me
--   cover_stood   an exhumed this seat is for is up and I am on it
--   cover_clear   none of this seat's exhumed is up
--   spit          his spit animation was seen (ev.slot, ev.lag)
--   slot_missed   a spit slot passed with no spit seen (ev.low: his bar is
--                 under the screech margin; ev.silent: no spit at all near
--                 that slot -- the two together are the screech)
--   step_out      the trio's cycle: 3 ticks to the next spit, step back
--   hold_out      2 ticks to it, hold the step-back tile (ev.N the slot)
--   press_in      1 tick to it, run back in and swing
--   back_in       no step back is due this tick (or the spit in flight is
--                 this raider's own, so it holds its melee tile: THE SPREAD)
--   spit_due      solo: this is the tick before the slot, dodge now
--   on_tile       solo: my tile is clean and in melee
--   off_tile      solo: my tile is acid, or out of melee, with time to move
--   turn          he turned (ev.q the quadrant he now stares at)
--   gaze_hits_me  he stares at my quadrant, or I stand in acid: not a tile
--                 to swing from
--   gaze_elsewhere he stares elsewhere and my tile is clean
--   swing_ok      my next swing lands before his next turn
--   swing_late    it does not
--   tick          raised last, every tick, carrying the tick's facts
--                 (ev.S the next spit slot, ev.face_q, ev.turn_tick,
--                 ev.window_end, ev.can_swing): the state holding the floor
--                 when it arrives is the one that acts.
-- ==========================================================================
function QD.raid._xarpus_events(st, v, c)
    assert(st, "_xarpus_events: st")
    assert(v, "_xarpus_events: v")
    assert(c, "_xarpus_events: c")
    local P, N, X, g = st.plan, st.numbers, st.xa, c.g
    local b = v.boss
    local list = {}
    local function raise(name, ev)
        ev = ev or {}
        ev.name = name
        list[#list + 1] = ev
    end

    -- ---- HIS FORM: the change that bounds every phase ----
    if b == nil then
        raise("form_feeding")
        -- phase 1, and only phase 1: the exhumed.  After the stand-up an
        -- absent row is his death and there is nothing on the floor to read.
        if X.u_tick == nil then
            if (st.party or 1) > 1 then QD.raid._xarpus_exhumed_track(st, v) end
            local cover = QD.raid._xarpus_cover_target(st, v)
            c.cover = cover
            if cover == nil then
                raise("cover_clear")
            elseif v.me.x == cover.x and v.me.z == cover.z then
                raise("cover_stood")
            else
                raise("cover_due")
            end
        end
        raise("tick")
        return list
    end
    raise("form_combat")
    -- the stand-up anchors the grid: X xarpus.p2.first_spit 7 (blert
    -- FIRST_P2_TURN_TICK = 7, ET 6.3)
    if X.u_tick == nil then
        X.u_tick = v.tick
        X.next_spit = v.tick + P.first_spit
    end

    -- ---- HIS SPIT, SEEN ----
    -- the rhythm is a grid (X xarpus.p2.cadence 4, grade A: "every 4 ticks",
    -- ET 6.3) anchored on the stand-up; a spit is SEEN on the server tick the
    -- plan first reads its animation (the client's own seq_tick is in client
    -- ticks and wanders by one against the server's: xa30a saw 147 as 146),
    -- up to two ticks late (a dodge's two steps take the ticks after S-1),
    -- and the grid moves only when a spit is seen before its slot or later
    -- than that
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
        raise("spit", { slot = X.spits[#X.spits], lag = lag })
    end
    while X.next_spit ~= nil and X.next_spit < v.tick - 1 do
        -- a slot with no spit seen two ticks after it: with the bar low, that
        -- is the screech ("Xarpus will screech ... and will stop launching
        -- poison around the arena", E:212).  The state machine owns what it
        -- MEANS; this says only that the slot passed and how his bar read.
        local missed = X.next_spit
        X.next_spit = X.next_spit + P.cadence
        local low = (b.health_scale or 0) > 0 and b.health_ratio * 100 <= (N.screech_pct or P.screech_pct) * b.health_scale
        raise("slot_missed", { missed = missed, low = low,
            silent = (#X.spits == 0 or X.spits[#X.spits] < missed - 1) })
    end

    -- ---- THE SEAT'S RHYTHM ----
    local S = X.next_spit
    -- which rhythm the seat is on is X.phase's question, as it was in the
    -- hand-rolled body: the room machine has not run yet this tick (it runs
    -- on these very events), and on the stand-up tick it still reads
    -- `feeding` while the fight has already begun.  X.phase is 1 or 2 here
    -- for the spit, 3 after the screech transition set it.
    local in_spit = X.phase ~= 3
    if in_spit and (st.party or 1) > 1 then
        -- the trio's cycle, from the tick's distance to the slot: step back
        -- on S-3, hold on S-2, press back in on S-1, melee otherwise
        local T = QD.raid._xarpus_trio_state(X)
        local d = S - v.tick
        local mode
        if d == 3 or d == 2 then
            mode = "out"
        elseif d == 1 then
            mode = "press"
        else
            mode = "in"
        end
        if P.trio.spread then
            if d == 3 and T.cur ~= nil and T.cur.N == S - P.cadence then
                -- the spit's own acid, thrown from him (a chain still in
                -- flight from the last landing is thrown from that landing's
                -- tile, and it is aimed at the same step-back tile: never
                -- read as the spit)
                for _, p in ipairs(v.incoming) do
                    if p.x == T.cur.x and p.z == T.cur.z and p.sx ~= nil and g.in_foot(p.sx, p.sz) and T.stay ~= S then
                        T.stay = S
                        T.stays = (T.stays or 0) + 1
                    end
                end
            end
            if T.stay == S and mode ~= "in" then mode = "in" end
        end
        if P.trace_trio then
            api_drive.report(string.format("xtrio r%d t%d N%d d%d %s me %d,%d hp %d eng %s",
                st.role, v.tick, S, d, mode, v.me.x, v.me.z, v.hp, tostring(st.engaged)))
        end
        if mode == "press" then
            raise("press_in", { N = S })
        elseif mode == "in" then
            raise("back_in", { N = S })
        elseif d == 3 then
            raise("step_out", { N = S })
        else
            raise("hold_out", { N = S })
        end
    elseif in_spit then
        -- solo: the dodge is sent on S-1 (the two steps resolve on S and
        -- S+1), and between dodges a tile that is acid or out of melee is
        -- left while there is still time to move
        if v.tick == S - 1 then raise("spit_due", { S = S }) end
        local here = v.me.x * 100000 + v.me.z
        local danger = v.pools[here] or v.splash[here]
        if (danger or not g.edge(v.me.x, v.me.z)) and v.tick < S - 1 then
            raise("off_tile", { S = S })
        else
            raise("on_tile", { S = S })
        end
    end

    -- ---- HIS GAZE ----
    -- "If a player attacks from a corner that Xarpus is looking at, he will
    -- retaliate back with an unblockable poison hit dealing 50-80 damage for
    -- each hitsplat" (W:851).  He turns every 8 ticks and "will never look in
    -- the same corner twice" (W:851-853).  Our server punishes a hit from the
    -- faced quadrant only after his first turn and never on the turn's own
    -- tick (S tob_damage.rs2:430-437), which is the 22121 window arithmetic
    -- of A:225-227 (a 5-tick weapon fits 1 or 2 swings in each 8-tick gaze).
    local offset = v.tick - v.api_now
    local face_q, turn_tick = nil, nil
    if b.face_tick ~= nil and b.face_tick >= 0 and X.p3_face_base ~= nil and b.face_tick > X.p3_face_base then
        face_q = QD.raid._xarpus_quadrant(g.cx, g.cz, b.face_x, b.face_z)
        turn_tick = b.face_tick + offset
        if X.turns[#X.turns] == nil or X.turns[#X.turns].tick ~= turn_tick then
            X.turns[#X.turns + 1] = { tick = turn_tick, q = face_q }
            raise("turn", { q = face_q, tick = turn_tick })
        end
    end
    local myq = QD.raid._xarpus_quadrant(g.cx, g.cz, v.me.x, v.me.z)
    local here3 = v.me.x * 100000 + v.me.z
    local on_acid = v.pools[here3] or v.splash[here3]
    if on_acid or (face_q ~= nil and myq == face_q) then
        raise("gaze_hits_me", { face_q = face_q, myq = myq, on_acid = on_acid and true or false })
    else
        raise("gaze_elsewhere", { face_q = face_q, myq = myq })
    end
    -- a swing is allowed when it lands before his next turn: the turn tick
    -- itself is safe (S tob_damage.rs2:433), one tick is kept as margin for
    -- a turn read a tick late.  X.p3_tick is set by the screech transition,
    -- which has not run yet on the screech tick itself -- the same tick the
    -- hand-rolled body read as v.tick there.
    local window_end = (turn_tick ~= nil) and (turn_tick + P.turn_cadence - 1)
        or ((X.p3_tick or v.tick) + P.turn_cadence - 2)
    local can_swing = QD.raid._play_next_swing(st, v) <= window_end
    if can_swing then raise("swing_ok", { window_end = window_end }) else raise("swing_late", { window_end = window_end }) end

    raise("tick", { S = S, face_q = face_q, turn_tick = turn_tick,
        window_end = window_end, can_swing = can_swing, myq = myq })
    return list
end

-- ==========================================================================
-- HIS PHASES.  The room's own machine: one per raider, every raider reading
-- it off his form on its own screen.
--
--   feeding   he lies on the floor healing while the exhumed drain the pools
--             ("Xarpus will be lying down, healing himself", E:204)
--   standup   the tick his combat row appears -- the form change that ends
--             phase 1 and anchors the spit grid; one tick long by
--             construction, the fight begins in it
--   spit      the main fight: he spits acid every 4 ticks
--   gaze      the screech into his final phase: he stops launching poison,
--             turns, and stares ("Xarpus will screech ... and will stop
--             launching poison around the arena", E:212)
--   dead      his row is gone after the stand-up: nothing is sent
--
-- X.phase (1/2/3) is kept in step by the enter hooks, because the harness
-- rows read it (test/raids/_play_xarpus.lua play.gaze_kept).
-- ==========================================================================
QD.raid.sm_declare("xarpus_room", {
    start = "feeding",
    states = {
        feeding = {
            note = "he lies healing; the exhumed are the fight",
            on = {
                form_combat = function(c, ev) return nil, "standup" end,
                -- his row stays absent: nothing to transition to
                form_feeding = function(c, ev) return nil end,
            },
        },
        standup = {
            note = "the form change; the fight begins on this very tick",
            on = {
                -- the fight begins in the same tick the form changed, as the
                -- hand-rolled `if X.phase == 1 then X.phase = 2 end` did:
                -- `tick` is raised last, so the seat's phase 2 body runs now
                tick = function(c, ev) c.intent.want.piety = true; return nil, "spit" end,
                -- backward: his combat row left again before the fight began
                form_feeding = function(c, ev) return nil, "feeding" end,
            },
        },
        spit = {
            note = "the main fight: a spit every 4 ticks",
            enter = function(c, ev, prev) c.X.phase = 2 end,
            on = {
                tick = function(c, ev) c.intent.want.piety = true; return nil end,
                -- THE SCREECH: a slot passed with no spit while his bar is
                -- under the margin (X xarpus.p3.screech_pct_entry 22.5,
                -- E:212; read with a margin -- the bar is 30 pixels)
                slot_missed = function(c, ev)
                    if not ev.low then return nil end
                    if not ev.silent then return nil end
                    c.X.p3_tick = c.v.tick
                    c.X.p3_face_base = c.v.boss.face_tick
                    return nil, "gaze"
                end,
                form_feeding = function(c, ev) return nil, "dead" end,
            },
        },
        gaze = {
            note = "he turns and stares; no more poison in the air",
            enter = function(c, ev, prev) c.X.phase = 3 end,
            on = {
                tick = function(c, ev) c.intent.want.piety = true; return nil end,
                -- backward: he launched poison again, so the screech read was
                -- wrong.  X.regaze is 0 in every measured run (5 of 5 trio,
                -- 5 of 5 solo Entry); the edge is declared, not inferred.
                spit = function(c, ev)
                    c.X.regaze = (c.X.regaze or 0) + 1
                    return nil, "spit"
                end,
                form_feeding = function(c, ev) return nil, "dead" end,
            },
        },
        dead = {
            note = "his row is gone after the stand-up: he is down",
            enter = function(c, ev, prev) c.X.resume = prev end,
            on = {
                -- nothing is sent on a tick with no boss row
                tick = function(c, ev) return nil end,
                -- backward: his row came back (a read that dropped a frame);
                -- the phase it left is the phase it returns to
                form_combat = function(c, ev) return nil, c.X.resume or "spit" end,
            },
        },
    },
})

-- ==========================================================================
-- THE EXHUMED SEAT (phase 1), one instance per raider.  "To prevent the
-- exhumed from healing Xarpus, players simply need to stand on top of them
-- until they return to the ground" (W:831).
--
--   waiting   none of this seat's exhumed is up: hold the wait tile ("stand
--             in the centre of the arena to quickly intercept any exhumed
--             that appear", W:831)
--   hunting   one is up and it is not under me: run to it
--   covering  I am standing on it, draining it
-- ==========================================================================
local function xarpus_walk_to(c, x, z, with_stuck)
    local st, v = c.st, c.v
    local same = st.walk_target ~= nil and st.walk_target.x == x and st.walk_target.z == z
    local stuck = with_stuck and st.last_me ~= nil and st.last_me.x == v.me.x and st.last_me.z == v.me.z
    if (v.me.x ~= x or v.me.z ~= z) and (not same or stuck) then c.intent.walk = { x = x, z = z } end
end

QD.raid.sm_declare("xarpus_exhumed", {
    start = "waiting",
    states = {
        waiting = {
            note = "between its own: the wait tile",
            on = {
                cover_due = function(c, ev) return nil, "hunting" end,
                -- one came up under my feet
                cover_stood = function(c, ev) return nil, "covering" end,
                tick = function(c, ev)
                    local P = c.P
                    local w = ((c.st.party or 1) > 1 and (P.trio.waits[c.st.role] or P.wait)) or P.wait
                    xarpus_walk_to(c, c.O.x + w[1], c.O.z + w[2], false)
                    return nil
                end,
            },
        },
        hunting = {
            note = "run to it before it drains into him",
            on = {
                cover_stood = function(c, ev) return nil, "covering" end,
                cover_clear = function(c, ev) return nil, "waiting" end,
                tick = function(c, ev)
                    assert(c.cover, "xarpus_exhumed hunting: no cover target (cover_clear leaves this state)")
                    -- the trio re-presses when a run did not move it (two
                    -- raiders crossing); the solo plan never did, and does
                    -- not start now
                    xarpus_walk_to(c, c.cover.x, c.cover.z, (c.st.party or 1) > 1)
                    return nil
                end,
            },
        },
        covering = {
            note = "standing on it: it heals him for nothing",
            on = {
                cover_due = function(c, ev) return nil, "hunting" end,
                cover_clear = function(c, ev) return nil, "waiting" end,
                tick = function(c, ev)
                    local st, v, X, cv = c.st, c.v, c.X, c.cover
                    assert(cv, "xarpus_exhumed covering: no cover target (cover_clear leaves this state)")
                    if cv.rec.arrive == nil then
                        cv.rec.arrive = v.tick
                        X.covers[#X.covers + 1] = { rise = cv.rise, x = cv.x, z = cv.z, arrive = v.tick,
                                                    at_x = v.me.x, at_z = v.me.z, k = cv.k }
                    end
                    -- the quiet phase 1: the super combat potion, drunk while
                    -- standing on an exhumed (tob_xarpus.lua :120-123 drinks
                    -- it on the fifth; the boost lasts the fight).  Solo
                    -- only: the trio's kit drinks it in its entry.
                    if (st.party or 1) == 1 and not X.potion and #X.covers >= 5 then
                        local cr, n = QD.inv.count("4dose2combat")
                        if cr == "ok" and n > 0 then c.intent.drink = "4dose2combat" end
                        X.potion = true
                    end
                    return nil
                end,
            },
        },
    },
})

-- ==========================================================================
-- THE SPIT SEAT, SOLO (phase 2).  The scan reads the tile "AS OF THE END OF
-- T-1" (ET 6.2) and the acid lands on that tile with a 3x3 splash (E:207, X
-- xarpus.p2.splat_radius 1).
--
--   melee     swinging on cooldown from a melee tile
--   clearing  this tile is acid or out of melee, and there is time to move:
--             to the nearest clean melee tile ("never stand in one", E:207)
--   dodging   the dodge was sent this tick; its two steps are the tick's move
-- ==========================================================================

-- the tail every phase 2 body ends with, the hand-rolled body's own: the
-- press is held when the tick carries a step, the supplies are judged
-- against the splash threat, and no bite is taken on the dodge's own tick.
local function xarpus_p2_finish(c, S)
    local st, v = c.st, c.v
    c.intent.attack = c.intent.walk == nil
    c.intent.eat, c.intent.drink, c.intent.need = QD.raid._play_supplies(st, v, c.g.threat)
    -- raid seam35e play_tob_entry_relay: ONLY the dodge's own tick.  The step
    -- is its own tick (the dodge returns before this line), so a bite here
    -- never displaces it; the relay's Xarpus (a P2 past the room test's 61
    -- ticks, the arena full of acid) chose the bandage at 24, 13 and 2
    -- hitpoints, sent only the brew, and died with ten bandages.
    if v.tick == S - 1 then c.intent.eat = nil end
    c.intent.attack = QD.raid._play_attack(st, v, c.intent.attack)
end

-- THE DODGE: two one-tile steps, the first sent on S-1 so it resolves on S
-- (after his scan: he aims at the tile just left), the second resolving on
-- S+1, so the splat lands two tiles from where the player now stands -- the
-- Entry page's "moving ... exactly 2 tiles at a time to avoid the poison"
-- (E:207), timed "just before you see the projectile" (E:207), with the melee
-- player's step on the spit rhythm (A:203, W:844) -- tob_xarpus.lua :445-471's
-- recipe, unchanged.  "never stand in one" (a pool hurts its own tile, E:207
-- "dealing some damage if you stand or run over it"): the second tile is a
-- clean melee tile when one exists, the first not a pool when it can be.
-- Nil when no two-step lands anywhere on the floor: then this tick is an
-- ordinary melee tick, which is what the hand-rolled body did by falling
-- through.
local function xarpus_dodge(c, ev)
    local st, v, X, g = c.st, c.v, c.X, c.g
    local S = ev.S
    local px, pz = v.me.x, v.me.z
    local best = nil
    for dx = -2, 2 do
        for dz = -2, 2 do
            if math.max(math.abs(dx), math.abs(dz)) == 2 then
                local qx, qz = px + dx, pz + dz
                if g.floor_ok(qx, qz) and not g.in_foot(qx, qz) then
                    local qk = qx * 100000 + qz
                    local pen = ((v.pools[qk] or v.splash[qk]) and 20 or 0)
                    for ax = -1, 1 do
                        for az = -1, 1 do
                            local mx, mz = px + ax, pz + az
                            if not (ax == 0 and az == 0) and math.max(math.abs(qx - mx), math.abs(qz - mz)) <= 1 then
                                if g.floor_ok(mx, mz) and not g.in_foot(mx, mz) then
                                    local mk = mx * 100000 + mz
                                    local qox = math.max(g.BX0 - qx, qx - g.BX1, 0)
                                    local qoz = math.max(g.BZ0 - qz, qz - g.BZ1, 0)
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
    if best == nil then return false end
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
    -- the press brings the next swing from the new tile (A:211 "you swing
    -- while running in")
    c.intent.attack = QD.raid._play_attack(c.st, c.v, true)
    return true
end

QD.raid.sm_declare("xarpus_spit_solo", {
    start = "melee",
    states = {
        melee = {
            note = "swing on cooldown from a melee tile",
            on = {
                spit_due = function(c, ev)
                    if xarpus_dodge(c, ev) then return nil, "dodging" end
                    return nil
                end,
                off_tile = function(c, ev) return nil, "clearing" end,
                tick = function(c, ev)
                    xarpus_p2_finish(c, ev.S)
                    return nil
                end,
            },
        },
        clearing = {
            note = "off the acid, back into melee",
            on = {
                -- the dodge is due whatever this tile is: it is the move that
                -- takes the player out of the splash
                spit_due = function(c, ev)
                    if xarpus_dodge(c, ev) then return nil, "dodging" end
                    return nil
                end,
                on_tile = function(c, ev) return nil, "melee" end,
                tick = function(c, ev)
                    local X, g = c.X, c.g
                    -- a pool or a landing under the player is stepped off (its
                    -- 3x3 is judged on the tile held at the end of the tick
                    -- before it lands, ET 1.2)
                    local tgt = g.nearest_edge(function() return true end, nil)
                    if tgt ~= nil then
                        xarpus_walk_to(c, tgt.x, tgt.z, true)
                        X.edge_walks = (X.edge_walks or 0) + 1
                    end
                    xarpus_p2_finish(c, ev.S)
                    return nil
                end,
            },
        },
        dodging = {
            note = "the two steps are this tick's move",
            on = {
                -- the steps were sent by the transition; nothing else goes
                -- out on this tick, and the next one is an ordinary melee tick
                tick = function(c, ev) return nil, "melee" end,
            },
        },
    },
})

-- ==========================================================================
-- THE SPIT SEAT, TRIO (phase 2): the step-back ring.  What our server does,
-- measured in the trio's own tick log (xn34c t145-169): the spit on S aims at
-- the target's tile at the end of S-1 (ET 1.2) and lands on S+3 (projectile
-- end cycle 102-112); the landing throws an orb at each other raider -- one
-- for the phase's first spit, two after ("The first player will splatter to
-- the next player in sequence, while the remaining players will splatter to
-- the next two players in sequence", W:836; S ~tob_xarpus_land_splat) --
-- aimed at their tiles at the end of S+2 and landing 2-3 ticks later (end
-- cycle 65-95).  Every landing leaves a permanent puddle and hurts a 3x3 (X
-- splat_radius 1, splat_lifetime never).  The target is drawn at random (S
-- ~tob_xarpus_spit).
--
-- THE RING, as a person counts it: "Melee users can preemptively step back
-- for 1 tick when Xarpus attacks with poison.  If on the correct timing, no
-- poison will splatter next to the boss" (W:844); "you step back every 4
-- ticks, based on Xarpus' attack speed" (A:203).  Our step back is two ticks
-- long, the ends of S+2 and S+3 = S'-1 (the chain is aimed at the first, the
-- next spit at the second), on a tile three out from his footprint when one
-- is clean (two from every melee tile, so a landing there never splashes the
-- raider in melee), and two from the last step-back tile (the spit aimed
-- there lands while the raider stands on the next).  The press on S'-1 runs
-- back in and swings ("you swing while running in", A:211).
--
--   in_melee      swinging; the tile under me is mine and has a step back
--   stepping_out  3 ticks to the slot: out to the step-back tile
--   holding_out   2 ticks to it: hold that tile
--   pressing_in   1 tick to it: run back in and swing
--
-- THE SPREAD (raid seam42 play_tob_xarpus_follows_blert).  Real Normal trios
-- do not stack in phase 2: each raider holds its own side (the nearest other
-- raider 5 tiles away, [3-6]; 0 of the spit ticks in 20 rooms had two raiders
-- on one tile; reference/xarpus_normal_3.json role.melee*.phase.phase1.
-- dist_raider) and swings about every 5.2 ticks (21.5 scythe swings in 112
-- ticks), "With a 5 tick weapon ... players will only delay an attack once in
-- their cycle" (W:844).  So each raider forms on its own phase 1 wait tile
-- (south, west, east), and the raider the spit in flight was aimed at stays
-- in -- the derivation raises back_in for it: the next spit never picks
-- whoever it hit last (S ~tob_xarpus_spit, Near-Reality's validTargets
-- filter) and a chain never comes back to the spit's own target (S
-- ~tob_xarpus_chain_to), so neither the next aim (end of S+3) nor this spit's
-- chains (end of S+2) can be on it.  Whose it was, the raider reads off the
-- screen: the acid in flight lands on the tile it stepped back to for that
-- spit, and on its side nobody else stands there.
--
-- EVERY STATE OF THE RING NAMES EVERY ONE OF THE FOUR CYCLE EVENTS, so the
-- ring is the declaration and not arithmetic on `d`: forward round the ring,
-- and backward whenever the grid moves under it (a spit seen early or late
-- regrids the slot, and the cycle event for the new slot arrives in whatever
-- state the raider is standing in).
-- ==========================================================================
local function xarpus_cycle_to_out(c, ev) return nil, "stepping_out" end
local function xarpus_cycle_to_hold(c, ev)
    -- the hold is only a hold when the step back for THIS slot was taken; if
    -- the grid moved, the step is still owed and is taken now
    local T = c.X.trio
    if T ~= nil and T.cur ~= nil and T.cur.N == ev.N then return nil, "holding_out" end
    return nil, "stepping_out"
end
local function xarpus_cycle_to_press(c, ev) return nil, "pressing_in" end
local function xarpus_cycle_to_in(c, ev) return nil, "in_melee" end
local XARPUS_CYCLE = {
    step_out = xarpus_cycle_to_out,
    hold_out = xarpus_cycle_to_hold,
    press_in = xarpus_cycle_to_press,
    back_in = xarpus_cycle_to_in,
}
local function xarpus_cycle_on(tick_handler)
    local on = { tick = tick_handler }
    for name, handler in pairs(XARPUS_CYCLE) do on[name] = handler end
    return on
end

QD.raid.sm_declare("xarpus_spit_trio", {
    start = "in_melee",
    states = {
        stepping_out = {
            note = "S-3: out to the step-back tile, the hammer on with it",
            on = xarpus_cycle_on(function(c, ev)
                local st, v, X, P, g, t = c.st, c.v, c.X, c.P, c.g, c.t
                local T = X.trio
                local best = t.out_from(v.me.x, v.me.z)
                -- its own side used up: any clean step-back tile in reach (a
                -- puddle on the floor two out is the lesser harm than one on
                -- a melee tile)
                if best == nil and P.trio.spread then best = t.out_from(v.me.x, v.me.z, true) end
                -- the Defence drain: "All players should have their
                -- defence-draining weapon equipped ... The team should have at
                -- least two successful hammer/maul specials" (W:831, W:839;
                -- Xarpus Defence 250, X xarpus.defence.normal).  The hammer
                -- goes on with this step back (one block: "the scythe back
                -- the same tick", PLAY_NOTES Loadouts), its special rides the
                -- press back in, and the scythe goes back on with the next
                -- step back.  Two specials a raider (50% each).
                local _, energy = QD.var.varp("varp300_sa_energy")
                energy = tonumber(energy) or 0
                if T.dwh_on then
                    c.intent.gear = { "scythe_of_vitur" }
                    T.dwh_on = false
                elseif (T.specs or 0) < P.trio.specs and energy >= 500 then
                    local hr, hn = QD.inv.count("dragon_warhammer")
                    if hr == "ok" and hn > 0 then
                        c.intent.gear = { "dragon_warhammer" }
                        T.dwh_cycle = X.next_spit
                    end
                end
                if best ~= nil then
                    c.intent.walk = best
                    T.cur = { N = X.next_spit, x = best.x, z = best.z }
                    T.prev = { x = best.x, z = best.z }
                    T.outs = T.outs + 1
                    if #T.out_list < 40 then T.out_list[#T.out_list + 1] = string.format("%d:%d,%d", v.tick, best.x, best.z) end
                    st.walk_target = nil
                else
                    T.no_out = T.no_out + 1
                end
                c.intent.eat, c.intent.drink, c.intent.need = QD.raid._play_supplies(st, v, g.threat)
                c.intent.attack = false
                return nil
            end),
        },
        holding_out = {
            note = "S-2: hold it (the chain is aimed here, the next spit at the next one)",
            on = xarpus_cycle_on(function(c, ev)
                local st, v, X, t = c.st, c.v, c.X, c.t
                local T = X.trio
                local cur = T.cur
                assert(cur, "xarpus_spit_trio holding_out: no step-back tile (only hold_out with T.cur.N == N enters)")
                T.holds = T.holds + 1
                if (v.me.x ~= cur.x or v.me.z ~= cur.z) and not t.bad(cur.x, cur.z) then
                    c.intent.walk = { x = cur.x, z = cur.z }
                end
                c.intent.eat, c.intent.drink, c.intent.need = QD.raid._play_supplies(st, v, c.g.threat)
                c.intent.attack = false
                return nil
            end),
        },
        pressing_in = {
            note = "S-1: run back in and swing (the special rides this press)",
            on = xarpus_cycle_on(function(c, ev)
                local st, v, X, P, t = c.st, c.v, c.X, c.P, c.t
                local T = X.trio
                if P.trio.spread then
                    local m = t.nearest_edge_to(v.me.x, v.me.z, false)
                    if m ~= nil and (not t.mine(m.x, m.z) or t.out_from(m.x, m.z) == nil) then
                        local bt = t.nearest_edge_to(v.me.x, v.me.z, true)
                        if bt ~= nil then
                            T.ins = T.ins + 1
                            T.repath = (T.repath or 0) + 1
                            st.engaged = false
                            c.intent.walk = bt
                            c.intent.attack = false
                            return nil
                        end
                    end
                end
                T.ins = T.ins + 1
                st.engaged = false
                if T.dwh_cycle == X.next_spit then
                    local _, energy = QD.var.varp("varp300_sa_energy")
                    c.intent.spec = true
                    T.dwh_on = true
                    T.specs = (T.specs or 0) + 1
                    T.spec_log = (T.spec_log or "") .. string.format(" t%d e%s", v.tick, tostring(energy))
                end
                c.intent.attack = QD.raid._play_attack(st, v, true)
                return nil
            end),
        },
        in_melee = {
            note = "swinging from its own side's melee tile",
            on = xarpus_cycle_on(function(c, ev)
                local st, v, X, P, g, t = c.st, c.v, c.X, c.P, c.g, c.t
                local T = X.trio
                local home = (P.trio.spread and P.trio.waits[st.role]) or P.trio.home
                local hx, hz = c.O.x + home[1], c.O.z + home[2]
                local here = v.me.x * 100000 + v.me.z
                local move = nil
                if X.spits[1] == nil and (v.me.x ~= hx or v.me.z ~= hz) then
                    -- before the first spit each raider forms on its own wait
                    -- tile (the south one is the Entry wait tile)
                    move = { x = hx, z = hz }
                    T.home_walks = T.home_walks + 1
                elseif not g.edge(v.me.x, v.me.z) and not st.engaged then
                    -- a press that did not land (a click "covered" by another
                    -- raider's models: xn34g t161) leaves this raider on the
                    -- step-back tile: press again now, the same run in the
                    -- others made a tick ago
                    T.repress = (T.repress or 0) + 1
                    c.intent.attack = QD.raid._play_attack(st, v, true)
                    return nil
                elseif v.pools[here] or v.splash[here] or not g.edge(v.me.x, v.me.z)
                       or (P.trio.spread and (not t.mine(v.me.x, v.me.z) or t.out_from(v.me.x, v.me.z) == nil)) then
                    -- raid seam42: in the spread a raider alone on a melee
                    -- tile with no clean step-back tile left in reach would
                    -- take the next spit or chain on the melee tile itself: it
                    -- moves on to one that has one
                    local need_out = P.trio.spread == true
                    local bt, bd, bk = nil, nil, nil
                    for x = g.BX0 - 1, g.BX1 + 1 do
                        for z = g.BZ0 - 1, g.BZ1 + 1 do
                            if g.edge(x, z) and not t.bad(x, z) and (not need_out or (t.mine(x, z) and t.out_from(x, z) ~= nil)) then
                                local dd = math.max(math.abs(x - v.me.x), math.abs(z - v.me.z))
                                local k = x * 100000 + z
                                if bd == nil or dd < bd or (dd == bd and k < bk) then bt, bd, bk = { x = x, z = z }, dd, k end
                            end
                        end
                    end
                    if bt ~= nil and (bt.x ~= v.me.x or bt.z ~= v.me.z) then
                        move = bt
                        T.off_acid = T.off_acid + 1
                    end
                end
                if move ~= nil then
                    xarpus_walk_to(c, move.x, move.z, true)
                    c.intent.attack = false
                    return nil
                end
                c.intent.eat, c.intent.drink, c.intent.need = QD.raid._play_supplies(st, v, g.threat)
                if c.intent.eat ~= nil and v.hp >= 40 then c.intent.eat = nil end
                c.intent.attack = QD.raid._play_attack(st, v, true)
                return nil
            end),
        },
    },
})

-- ==========================================================================
-- THE GAZE SEAT (phase 3).  "Simply stay in one location, attacking Xarpus
-- once with Melee when he turns to face one of the other quadrants, then
-- click on the ground to stop attacking" (E:212).
--
--   swinging    he stares elsewhere, my tile is clean, and my next swing
--               lands before his next turn: swing
--   relocating  he stares at my quadrant, or I stand in acid: to the nearest
--               clean melee tile of a quadrant he is not watching ("players
--               should be moving to where he last looked if possible", W:853)
--   stopping    the swing would land after his turn and I am engaged: click
--               the ground one tile out to stop attacking (E:212)
--   holding     nothing to send: hold the tile
--
-- THE PRECEDENCE IS THE SUBSCRIPTION.  The hand-rolled body was an
-- if/elseif chain, in this order: his gaze first, then the swing window,
-- then the stop.  Here each state names only the events it yields to --
-- `relocating` does not name swing_ok or swing_late, so his gaze wins; the
-- stopping and holding states do not name gaze_elsewhere, so a safe tile
-- alone does not make them swing.  The order the events are raised in
-- (gaze, then window, then tick) is the order the chain tested them.
-- ==========================================================================

-- the tail every gaze body ends with, the hand-rolled body's own
local function xarpus_p3_finish(c, target, want_attack)
    local st, v = c.st, c.v
    if target ~= nil then xarpus_walk_to(c, target.x, target.z, true) end
    c.intent.eat, c.intent.drink, c.intent.need = QD.raid._play_supplies(st, v, c.g.threat)
    c.intent.attack = QD.raid._play_attack(st, v, want_attack and c.intent.walk == nil)
end

-- the stop: "click on the ground to stop attacking" (E:212) -- one tile out
-- from him, in the same quadrant, off the acid.  Nil when no neighbour will
-- do, which is the `holding` body.
local function xarpus_p3_stop_tile(c, myq)
    local v, g = c.v, c.g
    local bx, bz, bd = nil, nil, nil
    for dx = -1, 1 do
        for dz = -1, 1 do
            local x, z = v.me.x + dx, v.me.z + dz
            if (dx ~= 0 or dz ~= 0) and g.clean(x, z) and QD.raid._xarpus_quadrant(g.cx, g.cz, x, z) == myq then
                local ox = math.max(g.BX0 - x, x - g.BX1, 0)
                local oz = math.max(g.BZ0 - z, z - g.BZ1, 0)
                local d = (math.max(ox, oz) == 2 and 0 or 3) + math.abs(dx) + math.abs(dz)
                if bd == nil or d < bd then bx, bz, bd = x, z, d end
            end
        end
    end
    if bx == nil then return nil end
    return { x = bx, z = bz }
end

-- the stop-or-wait body, which `relocating` also falls through to when there
-- is nowhere clean to go (the hand-rolled body did the same by leaving
-- `target` nil and letting the chain reach the stop)
local function xarpus_p3_stop_or_wait(c, ev)
    local X = c.X
    if c.st.engaged then
        local target = xarpus_p3_stop_tile(c, ev.myq)
        if target ~= nil then X.stops = X.stops + 1 end
        xarpus_p3_finish(c, target, false)
        return "stopping"
    end
    X.waits3 = X.waits3 + 1
    xarpus_p3_finish(c, nil, false)
    return "holding"
end

QD.raid.sm_declare("xarpus_gaze", {
    -- `relocating` is the state a raider enters phase 3 in: the gaze events
    -- of the screech tick itself move it on, exactly as the hand-rolled
    -- body's chain decided that tick from scratch.
    start = "relocating",
    states = {
        swinging = {
            note = "he looks away: one swing, from a clean melee tile",
            on = {
                gaze_hits_me = function(c, ev) return nil, "relocating" end,
                swing_late = function(c, ev)
                    if c.st.engaged then return nil, "stopping" end
                    return nil, "holding"
                end,
                tick = function(c, ev)
                    local g = c.g
                    local function allowed(q) return ev.face_q == nil or q ~= ev.face_q end
                    if g.edge(c.v.me.x, c.v.me.z) then
                        xarpus_p3_finish(c, nil, true)
                        return nil
                    end
                    -- back onto a clean melee tile of this quadrant (or
                    -- another he is not looking at) before the press
                    xarpus_p3_finish(c, g.nearest_edge(allowed, ev.myq) or g.nearest_edge(allowed, ev.myq, true), false)
                    return nil
                end,
            },
        },
        relocating = {
            note = "out of his gaze, off the acid",
            on = {
                gaze_elsewhere = function(c, ev) return nil, "swinging" end,
                tick = function(c, ev)
                    local X, g = c.X, c.g
                    local function allowed(q) return ev.face_q == nil or q ~= ev.face_q end
                    -- the quadrant he looked at last is preferred (W:853)
                    local prev = (#X.turns >= 2) and X.turns[#X.turns - 1].q or nil
                    local target = g.nearest_edge(allowed, prev) or g.nearest_edge(allowed, prev, true)
                    X.moves3 = X.moves3 + 1
                    if target ~= nil then
                        xarpus_p3_finish(c, target, false)
                        return nil
                    end
                    -- no clean melee tile is reachable around him this tick:
                    -- never swing from here; drop the aggro one tile out
                    return nil, xarpus_p3_stop_or_wait(c, ev)
                end,
            },
        },
        stopping = {
            note = "the swing would land in his gaze: stop attacking",
            on = {
                gaze_hits_me = function(c, ev) return nil, "relocating" end,
                swing_ok = function(c, ev) return nil, "swinging" end,
                swing_late = function(c, ev)
                    if c.st.engaged then return nil end
                    return nil, "holding"
                end,
                tick = function(c, ev) return nil, xarpus_p3_stop_or_wait(c, ev) end,
            },
        },
        holding = {
            note = "nothing to send: hold the tile",
            on = {
                gaze_hits_me = function(c, ev) return nil, "relocating" end,
                swing_ok = function(c, ev) return nil, "swinging" end,
                swing_late = function(c, ev)
                    if c.st.engaged then return nil, "stopping" end
                    return nil
                end,
                tick = function(c, ev) return nil, xarpus_p3_stop_or_wait(c, ev) end,
            },
        },
    },
})

-- ==========================================================================
-- THE XARPUS PLAN'S DECIDE (PLAY_NOTES.md "Xarpus"): the view, the events,
-- his machine, and the seat's machine for the phase his machine is in.
-- ==========================================================================
function QD.raid._play_xarpus_decide(st, v)
    assert(st, "_play_xarpus_decide: st")
    assert(v, "_play_xarpus_decide: v")
    if st.xa == nil then
        st.xa = { covers = {}, dodges = {}, cover_now = nil, phase = 1, spits = {}, last_spit_seq = -1,
                  turns = {}, p3_face_base = nil, potion = false, stops = 0, moves3 = 0, waits3 = 0,
                  u_tick = nil, p3_tick = nil, lines = {} }
    end
    local X = st.xa
    -- owner_rooms4: every seat runs (raid_play_tob_bloat.lua QD.raid._play_run_keep)
    if (st.party or 1) > 1 then QD.raid._play_run_keep(st, v) end
    QD.raid._xarpus_see(st, v)
    -- THE TICK'S CONTEXT, the author's table the layer passes through
    -- untouched: the view, the geometry, the one intent table every state
    -- mutates, and the plan's own rows.
    local c = { st = st, v = v, X = X, P = st.plan, N = st.numbers, O = st.origin,
                intent = { want = {}, walk = nil, attack = false } }
    c.g = QD.raid._xarpus_geometry(st, v)
    -- ONE DERIVATION PER DECIDE, and every machine of this decide reads that
    -- one list -- which is the property that matters ("no machine can derive
    -- its own private view of the tick").  NOT QD.raid.sm_events: its cache
    -- is keyed on v.tick, and the play loop calls a decide TWICE for one
    -- server tick whenever the tick advanced inside _play_send (measured
    -- here: tick 45 of the solo room, two decides, the second handed a
    -- cached list and an empty context).  The hand-rolled body recomputed
    -- every one of these reads on every call, so the port does too -- and
    -- each of them is idempotent within a tick by construction (a spit is
    -- consumed by X.last_spit_seq, a passed slot by X.next_spit, an exhumed
    -- by X.ex_seen, a turn by its tick, the spread's stay by T.stay).
    local events = QD.raid._xarpus_events(st, v, c)

    -- HIS PHASES first: a form change or the screech moves the room before
    -- the seat acts on the same tick's events.
    local room = QD.raid.sm_run(st, v, "xarpus_room", c, events)
    assert(room.state ~= "standup",
        "xarpus_room: the stand-up tick must reach the fight (its tick handler goes to spit)")
    if room.state == "feeding" then
        QD.raid.sm_run(st, v, "xarpus_exhumed", c, events)
    elseif room.state == "spit" then
        if (st.party or 1) > 1 then
            c.t = QD.raid._xarpus_trio_tools(st, v, c.g)
            QD.raid.sm_run(st, v, "xarpus_spit_trio", c, events)
        else
            QD.raid.sm_run(st, v, "xarpus_spit_solo", c, events)
        end
    elseif room.state == "gaze" then
        QD.raid.sm_run(st, v, "xarpus_gaze", c, events)
    end
    -- `dead` sends nothing: the intent leaves as it came.
    return c.intent
end
