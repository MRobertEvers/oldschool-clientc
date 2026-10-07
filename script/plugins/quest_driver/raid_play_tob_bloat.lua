-- quest-driver / raid_play_tob_bloat: the Pestilent Bloat plan for t.raid.play
-- (raid_play.lua).  Moved unchanged out of raid.lua's tail (raid seam27's
-- QD.RAID_PLAY_PLANS.tob_bloat and QD.raid._play_bloat_decide) by raid seam29
-- play_library_own_files; PLAY_NOTES.md "Bloat" is its strategy table.

QD.raid._play_plan("tob_bloat", {
    room = "bloat",
    boss = { entry = "tob_bloat_story", normal = "tob_bloat", hard = "tob_bloat_hard" },
    -- ENCOUNTER_TIMING.md 3.1 (blert BLOAT_DOWN_CYCLE_TICKS = 32): DOWN on T
    -- (seq 8082), attackable T+1..T+28, STOMP T+29, rise T+30..T+32, UP T+33.
    down_seq = 8082, down_ticks = 32, stomp_age = 29, rise_age = 30, up_age = 33,
    -- tob.constant ^tob_bloat_stomp_range = 5 from Bloat's CENTRE tile (raid
    -- seam48: tob_bloat.rs2's huntall from movecoord(npc_coord, 2, 0, 2); "it
    -- will stomp the surrounding area", wiki_Pestilent_Bloat.wikitext:92;
    -- Blert's 27 Normal trio downs: footprint + 3, [M65]).  stomp_centre is
    -- the centre's offset from the south-west tile the npc row carries.
    stomp_range = 5, stomp_centre = 2,
    -- ENCOUNTER_TIMING.md 3.4: graphics 1570-1573 mark the landing tile.
    shadow_lo = 1570, shadow_hi = 1573,
    -- Geometry local to Bloat's 64x64 map square (tob_bloat.lua: floor
    -- 6424..6437 x 89..102, tank 6428..6433 x 93..98 in square 6400,64).
    -- `mirror`: the tile straight behind the tank from Bloat's centre is
    -- (mirror.x - bx, mirror.z - bz) for Bloat's south-west tile bx,bz
    -- (tob_bloat.lua :1206, 12859 - bx and 189 - bz).
    --
    -- raid seam32: the TANK is x 29..34, not 28..33.  Every Bloat tick log of
    -- this pass (b32n3*, sv*playbloat, _play_bloat: player_tile rows) has
    -- raiders standing on 6428,93..98 (16-142 ticks each) and never on any of
    -- 6429..6434 x 93..98; the old box called the west column tank and the
    -- east column floor, so a hide tile at x 6434 was unreachable and the
    -- server's route to it ran round through 6435,92 under a shadow
    -- (svcplaybloat t309-311: walk sent to 6434,97, the hand landed on the
    -- route, the stun held all three raiders for the next hand).
    floor = { 24, 25, 37, 38 }, tank = { 29, 29, 34, 34 }, mirror = { 59, 61 },
    ring = { 23, 24, 34, 35 },
    modes = {
        -- tob.constant :752 entry flies 8, :760 entry stomp 40,
        -- tob_bloat.constant :39 entry hand 25 ([video][M62]).  Entry: the
        -- stomp is tick-eaten in place (wiki :675 "It is possible to tick
        -- eat this attack"), the flinch is the click back on the rise.
        entry = { fly = 8, stomp = 40, hand = 25, stomp_plan = "stay" },
        -- tob.constant :746 flies 20, :756 stomp 80, :762 hand 50.  Normal:
        -- "Unless the boss is below 3% health, it is recommended to run
        -- away after the last attack" (wiki :689).
        --
        -- raid seam32 play_tob_bloat_normal, the trio's three additions:
        --   fly_prayed  "up to 20 damage every tick, reduced by 25% if Protect
        --               from Missiles are active" (W:673): 15 is the most one
        --               fly lands while the raider reads the prayer lit.  The
        --               supplies' threat used 20 for every unhidden tick ahead,
        --               so a raider at 120 still read "eat" (b32n3a: 99 doses
        --               and bites, most at 120 hitpoints).
        --   runby       W:687 "one or two players should do a run-by on the
        --               boss with a Bandos godsword special to lower its
        --               Defence"; the BGS is not in this cache
        --               (tob_bloat_normal.lua:5), the Dragon warhammer is the
        --               drain the trio carries (yt_4i4lv-srJkw.md:45 "a dragon
        --               warhammer is basically essential").  Done by the one
        --               raider in the room on the first walk (W:687-689: the
        --               rest enter on the first down).
        --   hand_dodge  a shadow the tick's own step leaves is not counted as
        --               a hand that lands (ET 3.4; the threat function).
        --   offence_pots a brew drains Attack and Strength (wiki Saradomin
        --               brew :56, sara_brew.rs2:6-14); a super restore puts
        --               them back (wiki Super restore) and the super combat is
        --               re-sipped on the walk (yt_4i4lv-srJkw.md:47 "three
        --               super combats" for the raid).
        --
        -- raid seam42 play_tob_bloat_follows_blert: Normal follows the Blert
        -- reference (reference/bloat_normal_3.json, 19 death-free trio rooms;
        -- PLAY_NOTES.md "Bloat, Normal trio").  NO run-by: 30 recorded Normal
        -- trio rooms hold no Dragon warhammer special and one Bandos godsword
        -- special, and the run-by kept p1 in the flies eating four times
        -- before the first down (seam42 _play_bloat t63-82, hp lost 269
        -- against the reference's 49-217).  leave_from_here: the last swing is
        -- taken as late as the raider's own distance out of the stomp allows
        -- (Blert last swing age median 26.5, p10 15, p90 31), not at the age
        -- that fits the farthest raider (24 for everyone).
        normal = { fly = 20, stomp = 80, hand = 50, stomp_plan = "leave",
            fly_prayed = 15, offence_pots = true, hand_dodge = true, leave_from_here = true,
            -- owner_rooms4 (2026-10-07): THE FAR SIDE, NOT THE NEAREST HIDDEN
            -- TILE (hug_seen off).  Every hit our seats took was a fly, 157-299 a
            -- seat against the recorded trios' median 116 (max 217): the hug
            -- (seam49) picked the hidden ring tile NEAREST his footprint, and at
            -- his rise -- he may walk either way -- the seats stood on the lane he
            -- rose in (svaplaybloat down 1: on 28-34,28 with him rising at 27,24)
            -- and were in his sight 11 of 21 rise ticks.  The recorded seats stand
            -- on the tank's far ring from him (build/blert/bloat, 90 seats: him on
            -- the west lane -> them on x 35, south -> z 35, east -> x 28, north ->
            -- z 28), in his sight a median 3 of 37 entry ticks, 1 of 13.5 rise
            -- ticks, 0 of 26 walk ticks.  The mirror tile through the tank clamped
            -- to its ring (hug_tank) is that tile.
            hug_tank = true, hug_seen = false, leave_straight = true, down_spec = 2,
            -- raid seam51 play_tob_bloat_whole: the hug scored by the walk to
            -- the reach ring, the late-walk window, and the rise swing (the
            -- block above _play_bloat_path_dist).
            hug_path = true, hug_window = 32, hug_window_first = 36, hug_window_ahead = 4, rise_swing = true,
            shadow_memory = true },
        hard = { fly = 20, stomp = 80, hand = 50, stomp_plan = "leave",
            fly_prayed = 15, runby = "dragon_warhammer", offence_pots = true, hand_dodge = true },
    },
    -- wiki :673 "reduced by 25% if Protect from Missiles are active":
    -- lit on every tick Bloat is up (the flies are sent every tick).
    walk_prayers = { "protectfrommissiles" },
    -- the offensive prayer for the attackable window; no source flicks it here.
    down_prayers = { "piety" },
    decide = "_play_bloat_decide",
})

-- owner_rooms4: RUN KEPT ON (the owner: "are all players running?"), the
-- Nylocas owner's rule (raid_play_tob_nylocas.lua, feac33107), for the four
-- rooms owner_rooms4 plays (Bloat, Sotetseg, Xarpus, Verzik): when varp173
-- reads 0, a stamina dose if run had been on and one is held (none in the
-- dose's 2 minutes, 200 ticks: wiki Stamina potion), else the run orb
-- (orbs:runbutton).  v.running is the rate a route's ticks are read at.
-- Measured on every seat of every name: the server's raider rows read run 1
-- on every tick; varp173 reads 0 on a play's first tick only (one press).
function QD.raid._play_run_keep(st, v)
    local rr, run_on = QD.var.varp("varp173_option_run")
    v.running = not (rr == "ok" and run_on == 0)
    if rr == "ok" and run_on == 1 then st.run_seen_on = true end
    if rr ~= "ok" or run_on ~= 0 then return end
    st.run_offs = (st.run_offs or 0) + 1
    local dose = nil
    for _, d in ipairs({ "1dosestamina", "2dosestamina", "3dosestamina", "4dosestamina" }) do
        local cr, cn = QD.inv.count(d)
        if dose == nil and cr == "ok" and type(cn) == "number" and cn > 0 then dose = d end
    end
    if st.run_seen_on and dose ~= nil and (st.stamina_tick == nil or v.tick - st.stamina_tick >= 200) then
        QD.player.inv_op(dose, 1, { quick = true })
        st.stamina_tick = v.tick
        st.staminas = (st.staminas or 0) + 1
    else
        local wr, w = QD.ui.widget("orbs:runbutton")
        if wr == "ok" then QD.ui.invoke(w, 1) end
        st.run_presses = (st.run_presses or 0) + 1
    end
end

-- raid seam49 play_tob_bloat_round2: THE PILLAR HUG and THE STRAIGHT LEAVE.
--
-- Where the seam42 trio hid (the mirror tile, clamped one off the tank) it was
-- eight tiles round the tank from a Bloat that went down on the far side:
-- seam49 survey_1 _play_bloat down1, all three at 6428,99 with Bloat's
-- south-west tile at 6431,88, first swing at age 5-7 against the reference's
-- 3 [1-5] (react.phase.down1).  The content says why the hug tiles exist:
-- Bloat's flies go at a raider "if ANY" tile of the 5x5's near side has line
-- of sight (tob_bloat.rs2 ~tob_bloat_sees, after Mod Ash: "a manual check to
-- see if any tile on the NPC's nearest side has line-of-sight"), and "Hug the
-- pillar and hide from Bloat as it walks around the room" (W:687).  So the
-- hide is the tank-ring tile that NO near-side tile sees, from where Bloat is
-- and from where it will be, nearest Bloat's footprint.  The pillar is the
-- only thing in the room that blocks a line (the floor is open), so a line is
-- blocked exactly when it passes over a tank tile.
QD.RAID_PLAY_BLOAT_SIZE = 5
QD.RAID_PLAY_BLOAT_SHADOW_TICKS = 3   -- a shadow's splat lands three ticks after it shows (ET 3.4)

-- A line from tile centre to tile centre is blocked when it crosses the
-- tank's tiles (the box grown by half a tile): Liang-Barsky, constant cost
-- (the hug asks it a few thousand times a tick; a sampled line spent the
-- driver's 400000-instruction budget in seam49 survey_3).
local function bloat_line_blocked(tank, ax, az, px, pz)
    local dx, dz = px - ax, pz - az
    local t0, t1 = 0, 1
    local ps = { -dx, dx, -dz, dz }
    local qs = { ax - (tank[1] - 0.5), (tank[3] + 0.5) - ax, az - (tank[2] - 0.5), (tank[4] + 0.5) - az }
    for k = 1, 4 do
        local pk, qk = ps[k], qs[k]
        if pk == 0 then
            if qk < 0 then return false end
        else
            local r = qk / pk
            if pk < 0 then
                if r > t0 then t0 = r end
            elseif r < t1 then
                t1 = r
            end
        end
    end
    return t0 < t1
end

-- tob_bloat.rs2 ~tob_bloat_sees, with the pillar as the only blocker
function QD.raid._play_bloat_sees(tank, bx, bz, px, pz)
    local last = QD.RAID_PLAY_BLOAT_SIZE - 1
    local inx = px >= bx and px <= bx + last
    local inz = pz >= bz and pz <= bz + last
    if inx and inz then return true end
    if not inx then
        local ex = (px > bx + last) and bx + last or bx
        for i = 0, last do
            if not bloat_line_blocked(tank, ex, bz + i, px, pz) then return true end
        end
    end
    if not inz then
        local ez = (pz > bz + last) and bz + last or bz
        for i = 0, last do
            if not bloat_line_blocked(tank, bx + i, ez, px, pz) then return true end
        end
    end
    return false
end

-- The hug tile: on the ring one or two off the tank, on the floor, off every
-- shadow, seen from none of Bloat's tiles `at` (now and the prediction), the
-- nearest to Bloat's footprint now (ties: the nearest to the raider).  The
-- tile held last tick is kept while it is still hidden and no more than one
-- tile farther than the best (no step for a step's sake).  nil: none hidden.
function QD.raid._play_bloat_hug(st, v, tank, at, floor_ok, dist)
    local b = v.boss
    local last = QD.RAID_PLAY_BLOAT_SIZE - 1
    local function score(x, z)
        local fd = math.max(math.max(b.x - x, 0, x - (b.x + last)), math.max(b.z - z, 0, z - (b.z + last)))
        if dist ~= nil then fd = dist[x * 100000 + z] or 99 end
        local md = math.max(math.abs(x - v.me.x), math.abs(z - v.me.z))
        return fd, md
    end
    local function ok(x, z)
        if not floor_ok(x, z) or v.shadows[x * 100000 + z] then return false end
        for _, p in ipairs(at) do
            if QD.raid._play_bloat_sees(tank, p[1], p[2], x, z) then return false end
        end
        return true
    end
    local best, bx, bz, bm = nil, nil, nil, nil
    for x = tank[1] - 2, tank[3] + 2 do
        for z = tank[2] - 2, tank[4] + 2 do
            if ok(x, z) then
                local fd, md = score(x, z)
                if best == nil or fd < best or (fd == best and md < bm) then
                    best, bx, bz, bm = fd, x, z, md
                end
            end
        end
    end
    if best == nil then return nil, nil end
    local held = st.hug_tile
    if held ~= nil and ok(held.x, held.z) and score(held.x, held.z) <= best + 1 then
        return held.x, held.z
    end
    st.hug_tile = { x = bx, z = bz }
    return bx, bz
end

-- raid seam51 play_tob_bloat_whole: THE HUG BY THE WALK.  The hug tile was
-- the hidden tile nearest Bloat's footprint in a straight line, but the way
-- from it to an attack tile runs round the tank: seam49 survey_5 _play_bloat
-- down1 had all three at 6428,98 (seven from the footprint, the run north
-- round the corner six tiles) and the leader's first swing at age 4, the
-- members' at 6, against the reference's react.phase.down1 3 [1-5]
-- (bloat_normal_3.json).  Scored by the walk instead (seam50 bloat/sim.py on
-- that run's real Bloat path: 8-10 walking tiles at the downs -> 5-6), the
-- run in is the shortest the hide allows.  The distance is a breadth-first
-- walk on the floor from the reach ring (the tiles beside the footprint's
-- four sides: a melee swing does not go diagonally), a diagonal step only
-- where both of its sides are floor (no corner cut past the tank).  Keyed
-- x * 100000 + z like the shadows.
function QD.raid._play_bloat_path_dist(bx, bz, floor_ok)
    local last = QD.RAID_PLAY_BLOAT_SIZE - 1
    local dist, queue, head = {}, {}, 1
    local function seed(x, z)
        local k = x * 100000 + z
        if dist[k] == nil and floor_ok(x, z) then
            dist[k] = 0
            queue[#queue + 1] = { x, z }
        end
    end
    for i = 0, last do
        seed(bx - 1, bz + i)
        seed(bx + last + 1, bz + i)
        seed(bx + i, bz - 1)
        seed(bx + i, bz + last + 1)
    end
    while head <= #queue do
        local x, z = queue[head][1], queue[head][2]
        head = head + 1
        local d = dist[x * 100000 + z] + 1
        for dx = -1, 1 do
            for dz = -1, 1 do
                if dx ~= 0 or dz ~= 0 then
                    local nx, nz = x + dx, z + dz
                    local k = nx * 100000 + nz
                    if dist[k] == nil and floor_ok(nx, nz)
                        and (dx == 0 or dz == 0 or (floor_ok(x + dx, z) and floor_ok(x, z + dz))) then
                        dist[k] = d
                        queue[#queue + 1] = { nx, nz }
                    end
                end
            end
        end
    end
    return dist
end

-- Where Bloat will be: its south-west tile walks the square loop round the
-- tank (local 24..35 on both axes: every Bloat npc_tile row of seam49 survey_1
-- lies on it; the lanes are five wide, Bloat's own size), one tile a step in
-- the direction it is going, turning at a corner.  The hug must stay hidden
-- from the steps ahead, not only the next two ticks: seam49 survey_2 hid in
-- the lane Bloat was turning into (taken.py t142-153 at 6435,93 as it came
-- round the south-east corner, 15 a fly each tick for twelve ticks).
QD.RAID_PLAY_BLOAT_LOOP = { 24, 24, 35, 35 }
QD.RAID_PLAY_BLOAT_AHEAD = 8   -- loop steps ahead the hug must be hidden from
QD.RAID_PLAY_BLOAT_BEHIND = 1  -- and behind (a reversal is a roll, turn chance)
function QD.raid._play_bloat_route(O, bx, bz, dx, dz, steps)
    local L = QD.RAID_PLAY_BLOAT_LOOP
    local x0, z0, x1, z1 = O.x + L[1], O.z + L[2], O.x + L[3], O.z + L[4]
    local function on(x, z)
        return x >= x0 and x <= x1 and z >= z0 and z <= z1 and (x == x0 or x == x1 or z == z0 or z == z1)
    end
    local out = {}
    local x, z = bx, bz
    for _ = 1, steps do
        if on(x + dx, z + dz) then
            x, z = x + dx, z + dz
        elseif on(x + dz, z + dx) then
            dx, dz = dz, dx
            x, z = x + dx, z + dz
        elseif on(x - dz, z - dx) then
            dx, dz = -dz, -dx
            x, z = x + dx, z + dz
        else
            break
        end
        out[#out + 1] = { x, z }
    end
    return out
end

-- The straight leave: the floor tile just outside the stomp (stomp_range + 1
-- from Bloat's centre) nearest the raider whose straight line does not cross
-- the tank, and the run's length in tiles.  The seam42/48 leave walked to the
-- mirror tile, round the tank's corner, so it needed a spare tick
-- (seam48 closer: "the run round the corner took three ticks for a three-tile
-- need"); a run straight out needs none.
function QD.raid._play_bloat_leave_tile(v, P, tank, floor_ok)
    local cx, cz = v.boss.x + P.stomp_centre, v.boss.z + P.stomp_centre
    local r = P.stomp_range + 1
    local best, tx, tz = nil, nil, nil
    for x = cx - r, cx + r do
        for z = cz - r, cz + r do
            if math.max(math.abs(x - cx), math.abs(z - cz)) == r and floor_ok(x, z) and not v.shadows[x * 100000 + z]
                and not bloat_line_blocked(tank, v.me.x, v.me.z, x, z) then
                local d = math.max(math.abs(x - v.me.x), math.abs(z - v.me.z))
                if best == nil or d < best then best, tx, tz = d, x, z end
            end
        end
    end
    return tx, tz, best
end

-- owner_rooms4 (2026-10-07): THE SERVER'S ROUTE, NOT A STRAIGHT LINE.  A walk
-- click is routed by the server's flood and a running raider ends each tick
-- two tiles along it; api_drive.route (3705bafd2) is that route from the
-- client's own collision map, every seat's answer.  The library's safe step
-- judges two straight shapes toward the tile, which go THROUGH the tank: the
-- trio walked round the tank's corner onto a shadow it had seen (_play_bloat
-- t226-229: 6430,92 -> 6428,93 was routed 6429,92 -> 6428,92 -> 6428,93, the
-- diagonal past the corner 6429,93 is closed, the tick ended on 6428,92,
-- shadowed since t226; svcplaybloat t157-160: the hide walk round the tank's
-- west side ended its tick on 6429,92).  A hand is judged on the tile the
-- raider ends the tick before its impact on (ET 3.4), three ticks after its
-- shadow (QD.RAID_PLAY_BLOAT_SHADOW_TICKS): a tile whose shadow fell on tick
-- t0 is deadly at the END of tick t0 + 2, and a walk sent on view tick V
-- moves on server tick V + 1 (the tick log's raider rows carry the input and
-- the step on one tick).  So a walk is judged on every tile its route ends a
-- tick on, each against its own tick, and then on its end tile.
--
-- A SHADOW IS DATED BY ITS ANIMATION, not by the view that first shows it.
-- On the current engine the volley of tick T reaches the seats' view on T
-- for half the volleys and on T + 1 for the rest, on every seat alike
-- (svcplaybloat: volleys t157, t163, t181, t187, t233, t237 first shown on
-- the next tick, its falling-flesh animation already a tick in: cycles_left
-- 139-140 of 168; t169, t175, t193, t199, t241 shown on their tick at 168).
-- Dated by the view, the t157 shadow read deadly a tick late and the hide
-- walked onto it (pid0 t159, hit t160).  The animation is what a person sees
-- age: tob_bloat_falling_flesh is 28 frames of 6 client cycles (all.seq),
-- QD.RAID_PLAY_CYCLES_PER_TICK a tick.
QD.RAID_PLAY_BLOAT_SHADOW_CYCLES = 168

function QD.raid._play_bloat_shadow_ages(st, v, P)
    local ages = {}
    local sr, spots = QD.world.spotanims(0)
    if sr ~= "ok" then return ages end
    for k = 1, #spots do
        local sp = spots[k]
        if sp.spotanim_id >= P.shadow_lo and sp.spotanim_id <= P.shadow_hi then
            local age = (QD.RAID_PLAY_BLOAT_SHADOW_CYCLES - sp.cycles_left + QD.RAID_PLAY_CYCLES_PER_TICK // 2)
                // QD.RAID_PLAY_CYCLES_PER_TICK
            local key = sp.x * 100000 + sp.z
            local t0 = v.tick - math.max(0, age)
            if ages[key] == nil or t0 > ages[key] then ages[key] = t0 end
        end
    end
    return ages
end

-- The tile to send the walk to this tick (the library's _play_safe_step's
-- contract): `want` when no tick of its route ends on a tile deadly on that
-- tick, else the floor tile nearest `want` (within two ticks' run) whose route
-- is clear, the shorter move breaking ties.  Sending nothing is not standing
-- still while a walk is in flight: the server keeps routing it (svcplaybloat
-- t237-240: the hide tile became the raider's own tile at t238, nothing was
-- sent, and the walk sent at t237 carried all three onto 6429,99, shadowed
-- since t237).  So with a walk in flight the no-input candidate is that
-- walk's route, and the raider's own tile is not a candidate (a click on it
-- moves nothing, and the together block would wait
-- QD.TOGETHER_CONFIRM_TICKS for a move).
function QD.raid._play_bloat_safe_route(st, v, want_x, want_z, floor_ok)
    local seen = st.shadow_seen
    if seen == nil or api_drive.route == nil then
        return QD.raid._play_safe_step(st, v, want_x, want_z, floor_ok)
    end
    local run = v.running ~= false
    local per = run and QD.RAID_PLAY_RUN_TILES or 1
    local wt = st.walk_target
    local inflight = wt ~= nil and (wt.x ~= v.me.x or wt.z ~= v.me.z)
    local function deadly(x, z, tick)
        local t0 = seen[x * 100000 + z]
        return t0 ~= nil and t0 + 2 == tick
    end
    -- hits along a route to (x, z), and its ticks; nil when there is no route
    local function hits(x, z)
        local ticks
        if x == v.me.x and z == v.me.z then
            ticks = {}
        else
            local rr, route = api_drive.route(x, z, { run = run })
            if rr ~= "ok" or #route.ticks == 0 then return nil end
            if route.arrive.x ~= x or route.arrive.z ~= z then return nil end
            ticks = route.ticks
        end
        local count = 0
        for k = 1, #ticks do
            if deadly(ticks[k].x, ticks[k].z, v.tick + k) then count = count + 1 end
        end
        local t0 = seen[x * 100000 + z]
        if t0 ~= nil and t0 + 2 >= v.tick + #ticks + 1 then count = count + 1 end
        return count, #ticks
    end
    st.route_asks = (st.route_asks or 0) + 1
    if floor_ok(want_x, want_z) and not (inflight and want_x == v.me.x and want_z == v.me.z) then
        if hits(want_x, want_z) == 0 then return want_x, want_z, false end
    end
    -- the candidates, best score first; the first whose route is clear wins
    local list = {}
    local reach = 2 * per
    for dx = -reach, reach do
        for dz = -reach, reach do
            local x, z = v.me.x + dx, v.me.z + dz
            if floor_ok(x, z) and not (inflight and dx == 0 and dz == 0) then
                list[#list + 1] = { x = x, z = z,
                    score = math.max(math.abs(want_x - x), math.abs(want_z - z)) * 10 + math.max(math.abs(dx), math.abs(dz)) }
            end
        end
    end
    if inflight and math.max(math.abs(wt.x - v.me.x), math.abs(wt.z - v.me.z)) > reach then
        list[#list + 1] = { x = wt.x, z = wt.z, score = math.max(math.abs(want_x - wt.x), math.abs(want_z - wt.z)) * 10 }
    end
    table.sort(list, function(a, b)
        if a.score ~= b.score then return a.score < b.score end
        return a.x * 100000 + a.z < b.x * 100000 + b.z
    end)
    local fallback, fallback_hits = nil, nil
    for _, c in ipairs(list) do
        local h = hits(c.x, c.z)
        if h == 0 then
            st.route_dodges = (st.route_dodges or 0) + 1
            return c.x, c.z, true
        end
        if h ~= nil and (fallback_hits == nil or h < fallback_hits) then fallback, fallback_hits = c, h end
    end
    st.route_unclear = (st.route_unclear or 0) + 1
    if fallback ~= nil then return fallback.x, fallback.z, true end
    return v.me.x, v.me.z, true
end

-- ==========================================================================
-- raid seam53 play_state_machines (2026-10-07): BLOAT'S ROOM AS DECLARED
-- MACHINES, on raid_sm.lua.
--
-- The owner, 2026-10-07: "all the rooms should be explicit state machines.
-- Create an agent for each one", and, on how a role is modelled: named
-- states; each state handles all events and transitions forward AND
-- backward; the event handlers subscribed per state; the per-tick contract
-- Events + State -> Intents, with an executor reconciling the intents per
-- channel.
--
-- Before this the room was one 300-line `decide`.  His cycle was a string
-- ("walk"/"down") and an `age`, and what the raider owed the tick was an
-- if/elseif chain on that age against three computed ones (leave_age,
-- stomp_age, rise_age + 1), with the run-by's six stages in a seventh field
-- and the claws' four in an eighth.  Nothing named the states and nothing
-- said how they connected: adding one meant finding every comparison that
-- bounded its neighbours.
--
-- THE SAME PLAY, DECLARED.  Nothing in the behaviour changed with this port
-- -- build/seam_state/sm_bloat/progress.md has the five seed-survey names,
-- their room ticks and their per-seat damage taken, before it and after it.
-- What changed is where the play is written:
--
--   HIS CYCLE            bloat_cycle      active -> down -> stomp -> rising
--                                         -> active; dead
--   THE RAIDER'S ROLE    bloat_raider     outside, run_by, hiding,
--                                         attacking, leaving, rise_swing,
--                                         tick_eat, stomp_eat, flinch
--   THE STARTER'S DRAIN  bloat_runby      waiting -> equipping -> swinging
--                                         -> fired -> done; gave_up
--   THE DOWN'S CLAWS     bloat_down_spec  stowed -> worn -> armed ->
--                                         stowed; spent
--
-- THREE STEPS A TICK, in the old body's own order:
--   1. THE FACTS (QD.raid._bloat_facts): the readings the old decide took
--      before its chain -- his animation and its age, the shadow memory, the
--      hide tile, the leave age, the fly damage, the threat function.
--      Lifted verbatim: this is the part that was measured.
--   2. THE EVENTS (QD.raid._bloat_events), derived ONCE a tick through
--      QD.raid.sm_events, so every machine on the raider sees the same set
--      and none of them can grow a private reading of the tick: his
--      animation, the hazards, and the one duty this tick asks of the
--      raider.
--   3. THE MACHINES, and then THE EXECUTOR.  A state puts a target tile and
--      an attack on the tick's intent; the plan's executor turns the target
--      into a walk through the hazard step, the route dodge and the press
--      (raid_play.lua _play_reach / _play_hazard, _play_bloat_safe_route),
--      and the supplies, the prayers and the gear reconcile per channel as
--      they always have.
--
-- WHY THE DUTY IS AN EVENT.  Bloat's raider has no hysteresis, and that is
-- measured, not assumed: what it owes this tick is a function of his cycle
-- and of its own distance out (the leave age is read from the tile it
-- stands on; the rise window is open until its own swing shows).  So the
-- derivation raises exactly ONE duty a tick -- flies, swing_window,
-- leave_window, rise_window, tick_eat_window, stomp_eat_window,
-- flinch_window -- and every state handles every one of them: its own by
-- doing the work and staying put, another's by doing that work and GOING
-- there.  The declaration is then the whole transition table, forward and
-- backward: `leaving` goes back to `attacking` if the leave age moves out
-- from under it (a hand dodge changes the distance), `rise_swing` goes back
-- to `leaving` the tick its swing shows, and any state goes to `outside`
-- when his row leaves the raider's view.
--
-- WHAT A STATE WRITES is its own: the hide's Protect from Missiles, the
-- down's Piety, the claws on the walk and the scythe back, the hammer and
-- Piety of the run-by.  The one prayer rule they share is in
-- QD.raid._bloat_pray, because its boundary is a tick of his cycle and not
-- a state's choice (T+32, the tick before the flies resume, lights the
-- walk's prayer).
-- ==========================================================================

-- THE TICK'S FACTS.  Every reading the states and the executor need, taken
-- once, in the order the old decide took them; the seam comments stay on the
-- lines they belong to.  `intent` is this tick's intent table, which the
-- threat function reads for the hand dodge (a shadow the tick's own step
-- leaves is not a hand that lands).
function QD.raid._bloat_facts(st, v, intent)
    assert(st, "_bloat_facts: st")
    assert(v, "_bloat_facts: v")
    assert(intent, "_bloat_facts: intent")
    assert(v.boss, "_bloat_facts: Bloat is in view (the caller's question)")
    local P, N, O = st.plan, st.numbers, st.origin
    local b = v.boss
    local f = { tick = v.tick }
    -- raid seam51 (N.shadow_memory): A SHADOW IS A HAND FOR THREE TICKS.  The
    -- telegraph (1570-1573) is gone from the client's spotanims before its
    -- splat (1576) lands three ticks after it (ET 3.4; seam51 survey_1 and
    -- _3 _play_bloat: shadow 6437,97 seen t235, splat t238), so v.shadows
    -- forgot it and the hide walked the raider back onto it the tick after
    -- its dodge (t236 6437,95 -> t237 6437,97): the leader took 41 and the
    -- stun at down3's first tick, first swing age 10.  A tile stays marked
    -- for the hide, the hazard step and the safe step until its splat.
    if N.shadow_memory then
        st.shadow_seen = st.shadow_seen or {}
        -- owner_rooms4: dated by the animation (the block above
        -- _play_bloat_shadow_ages); a shadow whose splat has landed is off
        -- the list though its animation still shows (168 cycles, 5.6 ticks)
        local ages = QD.raid._play_bloat_shadow_ages(st, v, P)
        for k, t0 in pairs(ages) do
            if st.shadow_seen[k] == nil or t0 > st.shadow_seen[k] then st.shadow_seen[k] = t0 end
        end
        for k in pairs(v.shadows) do
            if ages[k] == nil and st.shadow_seen[k] == nil then st.shadow_seen[k] = v.tick end
            v.shadows[k] = nil
        end
        for k, t0 in pairs(st.shadow_seen) do
            if v.tick - t0 > QD.RAID_PLAY_BLOAT_SHADOW_TICKS then
                st.shadow_seen[k] = nil
            else
                v.shadows[k] = true
            end
        end
    end
    -- HIS ANIMATION and its age: the one reading of his cycle, which the
    -- events turn into the cycle machine's state (ENCOUNTER_TIMING.md 3.1).
    local phase, age = "walk", -1
    if b.seq_id == P.down_seq then
        local a = v.api_now - b.seq_tick
        if a >= 0 and a <= P.down_ticks then
            phase, age = "down", a
        end
    end
    if phase == "down" and st.down_key ~= b.seq_tick then
        st.down_key = b.seq_tick
        st.down = { tick = v.tick - age, seen_age = age, bx = b.x, bz = b.z, index = #st.downs + 1 }
        st.downs[#st.downs + 1] = st.down
    end
    st.phase = phase
    f.phase, f.age = phase, age
    f.floor_ok = function(x, z)
        local inside = x >= O.x + P.floor[1] and x <= O.x + P.floor[3] and z >= O.z + P.floor[2] and z <= O.z + P.floor[4]
        local tank = x >= O.x + P.tank[1] and x <= O.x + P.tank[3] and z >= O.z + P.tank[2] and z <= O.z + P.tank[4]
        return inside and not tank
    end
    local floor_ok = f.floor_ok
    -- where Bloat will be two ticks on (its walk is visible); the hide tile
    -- is that tile mirrored through the tank (tob_bloat.lua :1206)
    local fx, fz = b.x, b.z
    if phase == "walk" and st.prev_b ~= nil then
        fx = math.max(O.x + P.ring[1], math.min(O.x + P.ring[3], b.x + 2 * (b.x - st.prev_b.x)))
        fz = math.max(O.z + P.ring[2], math.min(O.z + P.ring[4], b.z + 2 * (b.z - st.prev_b.z)))
    end
    st.prev_b = { x = b.x, z = b.z }
    f.fx, f.fz = fx, fz
    local hide_x, hide_z = 2 * O.x + P.mirror[1] - fx, 2 * O.z + P.mirror[2] - fz
    -- raid seam42 (N.hug_tank): "Hug the pillar" (W:687).  The mirror tile is
    -- two tiles off the tank (seam42 _play_bloat: all three at 6428,101, nine
    -- from Bloat's footprint, first swing at age 6-7); the recorded Normal
    -- trios hid at seven (Blert walk distance mode 7, build/blert/bloat) and
    -- swung first at age 3 (reference react.phase.down1 3 [1-6]).  The tile
    -- one off the tank on the same line is still behind it.
    if N.hug_tank then
        hide_x = math.max(O.x + P.tank[1] - 1, math.min(O.x + P.tank[3] + 1, hide_x))
        hide_z = math.max(O.z + P.tank[2] - 1, math.min(O.z + P.tank[4] + 1, hide_z))
    end
    -- raid seam49 (N.hug_seen): the hug tile Bloat cannot see, from where it
    -- is, one tick on and two (the helper above); the mirror stays the
    -- fallback when no ring tile is hidden.  After the stomp (the rise) the
    -- same tile, from where Bloat stands.
    local tank = { O.x + P.tank[1], O.z + P.tank[2], O.x + P.tank[3], O.z + P.tank[4] }
    f.tank = tank
    local hug_x, hug_z = nil, nil
    if N.hug_seen and (phase == "walk" or age > P.stomp_age) then
        local at = { { b.x, b.z } }
        local dx = (fx > b.x and 1) or (fx < b.x and -1) or 0
        local dz = (fz > b.z and 1) or (fz < b.z and -1) or 0
        if dx ~= 0 and dz ~= 0 then dz = 0 end
        if dx ~= 0 or dz ~= 0 then st.bloat_dir = { dx, dz } end
        local dir = st.bloat_dir
        -- raid seam51 (N.hug_window): the down comes on a walk's tick 34-42
        -- (ENCOUNTER_TIMING.md 3.2; the first walk 38-46); from the window's
        -- approach the hide answers only the next N.hug_window_ahead steps and
        -- none behind, so the raider holds the hidden tile nearest the down
        -- (seam50 sim.py, walk age 30 and four steps: 5-6 walking tiles at the
        -- downs -> 3 in some).  The walk's age counts from the last rise (UP,
        -- T+33) or, before the first down, from the plan's first tick.
        local n_ahead, n_behind = QD.RAID_PLAY_BLOAT_AHEAD, QD.RAID_PLAY_BLOAT_BEHIND
        if N.hug_window ~= nil and phase == "walk" then
            -- (iteration 2: from walk age 30 the window cost 24 fly hits and
            -- a hand, 382 damage, in seam51 survey_1 _play_bloat; it opens two
            -- ticks before the earliest down now, six on the first walk,
            -- whose clock starts before the plan's first tick)
            local from, open_at = st.first_tick, N.hug_window_first or N.hug_window
            if #st.downs > 0 then from, open_at = st.downs[#st.downs].tick + P.up_age, N.hug_window end
            st.walk_age = v.tick - from
            if st.walk_age >= open_at then
                n_ahead, n_behind = N.hug_window_ahead, 0
            end
        end
        if dir ~= nil then
            local ahead = QD.raid._play_bloat_route(O, b.x, b.z, dir[1], dir[2], n_ahead)
            for k = 2, #ahead, 2 do at[#at + 1] = ahead[k] end
            if n_behind > 0 then
                for _, p in ipairs(QD.raid._play_bloat_route(O, b.x, b.z, -dir[1], -dir[2], n_behind)) do at[#at + 1] = p end
            end
        end
        local dist = nil
        if N.hug_path then dist = QD.raid._play_bloat_path_dist(b.x, b.z, floor_ok) end
        hug_x, hug_z = QD.raid._play_bloat_hug(st, v, tank, at, floor_ok, dist)
        if hug_x ~= nil and phase == "walk" then
            hide_x, hide_z = hug_x, hug_z
        end
    end
    f.hide_x, f.hide_z, f.hug_x, f.hug_z = hide_x, hide_z, hug_x, hug_z
    local in_stomp = math.max(math.abs(v.me.x - (b.x + P.stomp_centre)), math.abs(v.me.z - (b.z + P.stomp_centre)))
        <= P.stomp_range
    local hidden = math.max(math.abs(v.me.x - hide_x), math.abs(v.me.z - hide_z)) <= 1
    local on_shadow = v.shadows[v.me.x * 100000 + v.me.z] == true
    f.in_stomp, f.hidden, f.on_shadow = in_stomp, hidden, on_shadow
    local leave_age = P.stomp_age - 1 - math.ceil((P.stomp_range + 1) / QD.RAID_PLAY_RUN_TILES)
    -- raid seam42 (N.leave_from_here): the stomp is a huntall of stomp_range
    -- round Bloat's south-west tile (tob_bloat.rs2:823), so a raider is out of
    -- it after (stomp_range + 1 - its own distance from that tile) tiles, not
    -- after stomp_range + 1 from anywhere.  One tick more than that run: the
    -- old age's own arithmetic on the raider's distance (no spare tick) let
    -- the stomp land on 2-3 raiders a run in seam42 survey_5 (_play_bloat
    -- t193, t264: the run round the tank's corner is longer than the
    -- distance); with the tick none landed in survey_4.  The farthest case
    -- (distance 0) is the old age.
    -- raid seam48: the hunt is from the centre now (d_sw is the distance
    -- from Bloat's centre tile).  The spare tick stays: the fixer's -1 let
    -- the stomp land on p0 at every down of the closer's survey (_play_bloat
    -- t123, t197: a hand dodge west then the run north round the corner took
    -- three ticks for a three-tile need), so the run is still not straight.
    if N.leave_from_here and phase == "down" then
        local d_sw = math.max(math.abs(v.me.x - (b.x + P.stomp_centre)), math.abs(v.me.z - (b.z + P.stomp_centre)))
        local need = math.max(0, P.stomp_range + 1 - d_sw)
        -- raid seam49 (N.leave_straight): the run is straight out to the
        -- nearest tile past the stomp's reach that the tank does not stand
        -- in front of, so it takes exactly its length: no spare tick.
        local spare, leave_tile = 2, nil
        if N.leave_straight then
            local lx, lz, ld = QD.raid._play_bloat_leave_tile(v, P, tank, floor_ok)
            if lx ~= nil then
                need, spare, leave_tile = ld, 1, { x = lx, z = lz }
            end
        end
        leave_age = math.max(leave_age, P.stomp_age - spare - math.ceil(need / QD.RAID_PLAY_RUN_TILES))
        if st.down ~= nil and st.down.leave_at == nil and age >= leave_age then
            st.down.leave_at = { age = age, d_sw = d_sw, tile = leave_tile }
        end
        -- raid seam48 (closer): a raider who has started the run keeps
        -- running.  leave_age is read from the tile it stands on, so a step
        -- out (a hand dodge) pushed the age a tick later and the next tick's
        -- attack press walked it back in (_play_bloat p0 t120-t121).
        if st.down ~= nil and st.down.leave_at ~= nil then
            leave_age = math.min(leave_age, st.down.leave_at.age)
        end
    end
    f.leave_age = leave_age
    -- raid seam32: the most one fly lands with Protect from Missiles (W:673).
    -- The plan lights it on every walking tick and from T+32, the tick before
    -- the first fly of a rise (walk_prayers; QD.raid._bloat_pray), so every
    -- fly the threat counts lands on a prayed raider while any prayer is
    -- left.  Entry carries no fly_prayed and reads N.fly as before.
    local straight_out = N.leave_straight and st.down ~= nil and st.down.leave_at ~= nil and st.down.leave_at.tile ~= nil
    local fly = N.fly
    if N.fly_prayed ~= nil and v.prayer > 0 then fly = N.fly_prayed end
    f.straight_out, f.fly = straight_out, fly
    -- raid seam51 (N.rise_swing): THE RISE SWING is open while his rise is a
    -- full-damage window and this raider's own swing has not shown in it
    -- (the state `rise_swing`, whose block below says why ages 29-31).
    f.rise_open = (N.rise_swing and N.stomp_plan ~= "stay" and phase == "down"
        and age >= P.stomp_age and age <= P.rise_age + 1
        and st.down ~= nil and st.last_swing < st.down.tick + P.stomp_age) or false
    -- the hitpoints the stomp found, for the harness's row
    if phase == "down" and age >= P.stomp_age - 2 and age <= P.stomp_age - 1 and st.down.pre_stomp == nil then
        st.down.pre_stomp = v.hp
    end
    f.threat = function(h)
        local total = 0
        for k = 1, h do
            if phase == "walk" then
                if not hidden then total = total + fly end
            else
                local a = age + k
                -- raid seam42 (N.leave_from_here): a raider who will leave in
                -- time is not hit by the stomp; counting it while it still
                -- swings ate a bite at 85-99 hitpoints on a down's last swings
                -- (seam42 _play_bloat t125, t218: all three raiders)
                -- raid seam51 (N.leave_straight): the straight leave is
                -- timed to its own length and took no stomp in 6 of 6 downs
                -- of seam51 survey_1-2 (tech.leave_before_stomp), so a
                -- leaving raider is not caught either; counting it ate a bite
                -- on the leave (age 26-27) whose delay cost the rise swing
                -- (survey_2 _play_bloat down2: p0 and p2)
                local caught = in_stomp and (not N.leave_from_here or (age >= leave_age and not straight_out))
                if a == P.stomp_age and (N.stomp_plan == "stay" or caught) then total = total + N.stomp end
                if a >= P.up_age then total = total + fly end
            end
        end
        -- raid seam32 (N.hand_dodge): a hand is judged on the tile the raider
        -- ends the tick before its impact on (ET 3.4), so a shadow this tick's
        -- step leaves is not a hand that lands; counting it ate a bite and a
        -- brew at 99-115 hitpoints on every shadow (b32n3b t152, t189)
        local leaving = N.hand_dodge and intent.walk ~= nil and (intent.walk.x ~= v.me.x or intent.walk.z ~= v.me.z)
        if on_shadow and not leaving then total = total + N.hand end
        return total
    end
    st.bl = f
    return f
end

-- THE EVENTS, derived ONCE a tick from the facts and the tick view, here and
-- nowhere else (QD.raid.sm_events caches the list on the raider), so the four
-- machines see one reading of the tick.  A state that does not name an event
-- ignores it, and what it ignores is visible in the declaration by what is
-- absent.
--
--   tick              {tick, hp}          every tick, by the layer's convention
--   orb               {energy}            the special-attack orb, read once
--   boss_absent       {}                  his row is not in this raider's view
--   HIS ANIMATION, one of (ENCOUNTER_TIMING.md 3.1):
--   walks             {x, z, to_x, to_z}  up and walking the ring; to_* is
--                                         where he will be two ticks on
--   down_anim         {age, index}        down and attackable, T+0..T+28
--   stomp             {age, in_stomp}     his stomp, T+29
--   rises             {age}               the rise, T+30..T+32
--   THE HAZARDS:
--   flies             {hidden, fly, x, z} every tick he is up, every raider
--                                         with line of sight takes 10-20
--                                         (x0.75 prayed) and the hit SPREADS
--                                         -- the test is re-run from each
--                                         raider hit to every other raider
--                                         (ET 3.3).  This seat's screen can
--                                         answer only his line to itself, so
--                                         `hidden` is that; the spread is why
--                                         the hide is worth its tiles.  x, z
--                                         is the tile he cannot see.
--   limbs             {count}             the falling limbs live this tick
--                                         (their shadows, dated by animation)
--   limb_underfoot    {x, z}              one of them is on my own tile
--   THE RAIDER'S DUTY, exactly one a tick:
--   swing_window      {age}               in and swinging (the leave is not due)
--   leave_window      {age}               out of the stomp's reach
--   rise_window       {age}               back in for the rise swing
--   tick_eat_window   {age}               Entry: swing, the stomp is eaten
--   stomp_eat_window  {age}               Entry: T+29, stand and eat it
--   flinch_window     {age}               Entry: the click back on the rise
-- (`flies` is the walk's duty as well as its hazard: the tick set is the
-- same -- every tick he is up is a tick to be out of his sight.)
function QD.raid._bloat_events(st, v)
    assert(st, "_bloat_events: st")
    assert(v, "_bloat_events: v")
    local P, N = st.plan, st.numbers
    local out = {}
    local function raise(name, e)
        e = e or {}
        e.name = name
        out[#out + 1] = e
    end
    raise("tick", { tick = v.tick, hp = v.hp })
    -- the orb before his animation: the run-by's swinging state reads the
    -- energy it spent before it reads the phase it gave up on, which is the
    -- order the old body checked them in
    local _, energy = QD.var.varp("varp300_sa_energy")
    raise("orb", { energy = tonumber(energy) or 0 })
    if v.boss == nil then
        raise("boss_absent", {})
        return out
    end
    local f = st.bl
    assert(f ~= nil, "_bloat_events: st.bl (call after QD.raid._bloat_facts)")
    assert(f.tick == v.tick, "_bloat_events: st.bl is last tick's reading")
    local b, age = v.boss, f.age
    if f.phase == "walk" then
        raise("walks", { x = b.x, z = b.z, to_x = f.fx, to_z = f.fz })
    elseif age < P.stomp_age then
        raise("down_anim", { age = age, index = st.down ~= nil and st.down.index or nil })
    elseif age == P.stomp_age then
        raise("stomp", { age = age, in_stomp = f.in_stomp })
    else
        raise("rises", { age = age })
    end
    if f.phase == "walk" then
        raise("flies", { hidden = f.hidden, fly = f.fly, x = f.hide_x, z = f.hide_z })
    elseif N.stomp_plan == "stay" then
        if age < P.stomp_age then
            raise("tick_eat_window", { age = age })
        elseif age < P.rise_age then
            raise("stomp_eat_window", { age = age })
        else
            raise("flinch_window", { age = age })
        end
    elseif f.rise_open then
        raise("rise_window", { age = age })
    elseif age < f.leave_age then
        raise("swing_window", { age = age })
    else
        raise("leave_window", { age = age, leave_age = f.leave_age })
    end
    local limbs = 0
    for _ in pairs(v.shadows) do limbs = limbs + 1 end
    raise("limbs", { count = limbs })
    if f.on_shadow then raise("limb_underfoot", { x = v.me.x, z = v.me.z }) end
    return out
end

-- HIS CYCLE (ENCOUNTER_TIMING.md 3.1, the plan table's down_seq/down_ticks/
-- stomp_age/rise_age/up_age).  He walks the ring round the tank throwing
-- flies every tick; he goes down (seq 8082, T), and is attackable and
-- harmless T+1..T+28; he stomps T+29; he rises T+30..T+32 and is up again
-- T+33, when the flies resume and damage to him is halved once more.  `dead`
-- is his row gone from this raider's view -- the play itself ends on his
-- npc_death row (raid_play.lua _play_tick), so this state is what a seat
-- sees before it has crossed the barrier and after the kill, and it goes
-- back on his next animation because a gone row is not a death
-- (raid seam31 FAULT 4).
--
-- The machine states where he is; it answers no intent.  The raider's own
-- machine below reacts to the same animation events with the duty they put
-- on it, which is why this one holds no play.
QD.raid.sm_declare("bloat_cycle", {
    start = "active",
    states = {
        -- up and walking: the flies are in the air every tick
        active = { on = {
            walks     = function(c, ev) return nil end,            -- still walking
            down_anim = function(c, ev) return nil, "down" end,     -- he goes down
            stomp     = function(c, ev) return nil, "stomp" end,
            rises     = function(c, ev) return nil, "rising" end,
            boss_absent = function(c, ev) return nil, "dead" end,
        } },
        -- down and attackable, T+0..T+28
        down = { on = {
            down_anim = function(c, ev) return nil end,             -- still down
            stomp     = function(c, ev) return nil, "stomp" end,
            rises     = function(c, ev) return nil, "rising" end,
            walks     = function(c, ev) return nil, "active" end,   -- up early (a cut-short down)
            boss_absent = function(c, ev) return nil, "dead" end,
        } },
        -- T+29: 40-80 to everything within stomp_range of his centre, and
        -- his Defence back to base (W:675)
        stomp = { on = {
            stomp     = function(c, ev) return nil end,
            rises     = function(c, ev) return nil, "rising" end,
            down_anim = function(c, ev) return nil, "down" end,     -- a fresh down
            walks     = function(c, ev) return nil, "active" end,
            boss_absent = function(c, ev) return nil, "dead" end,
        } },
        -- T+30..T+32, the flinch window: full damage, no flies yet
        rising = { on = {
            rises     = function(c, ev) return nil end,
            walks     = function(c, ev) return nil, "active" end,   -- UP, T+33
            down_anim = function(c, ev) return nil, "down" end,     -- a fresh down
            stomp     = function(c, ev) return nil, "stomp" end,
            boss_absent = function(c, ev) return nil, "dead" end,
        } },
        -- his row is not in view: before the crossing, and after the kill
        dead = { on = {
            boss_absent = function(c, ev) return nil end,
            walks     = function(c, ev) return nil, "active" end,
            down_anim = function(c, ev) return nil, "down" end,
            stomp     = function(c, ev) return nil, "stomp" end,
            rises     = function(c, ev) return nil, "rising" end,
        } },
    },
})

-- THE PRAYERS, the one rule the raider's states share, because its boundary
-- is a tick of HIS cycle and not a state's choice: the down prayers through
-- the attackable window (Piety -- no source flicks it here), and the walk's
-- Protect from Missiles from T+32, the tick before the first fly of the rise
-- (W:673 "reduced by 25% if Protect from Missiles are active"; the flies are
-- sent every tick he is up, so the prayer must already be lit on T+33).
function QD.raid._bloat_pray(c)
    assert(c, "_bloat_pray: c")
    local P, f = c.P, c.f
    local list = P.walk_prayers
    if f.phase == "down" and f.age < P.up_age - 1 then list = P.down_prayers end
    for _, name in ipairs(list) do c.intent.want[name] = true end
end

-- THE DUTIES.  One function a duty: what the raider writes on the tick's
-- intent when it owes that duty, whatever state it was in when the duty came
-- (c.go is the plan's own walk target, which the executor at the end of the
-- decide turns into a walk through the hazard step and the route dodge).
-- The states below subscribe these, so the work is written once and the
-- declaration is only about where each duty leads.

-- HIDE: out of his sight behind the tank ("Hug the pillar and hide from
-- Bloat as it walks around the room", W:687), off every shadow, with
-- Protect from Missiles lit.  owner_rooms4: the tile is the mirror through
-- the tank clamped to its ring -- THE FAR SIDE from him, not the nearest
-- hidden tile, which is what the recorded trios stand on (the plan table's
-- hug_tank/hug_seen block).
function QD.raid._bloat_hide(c, ev)
    assert(c, "_bloat_hide: c")
    assert(ev, "_bloat_hide: ev")
    QD.raid._bloat_pray(c)
    c.go.x, c.go.z = ev.x, ev.z
end

-- SWING: "As soon as Bloat deactivates ... begin attacking with melee"
-- (W:689), on cooldown, with Piety.  No target of its own: the attack
-- press's own path walks the raider in (_play_reach).
function QD.raid._bloat_swing(c, ev)
    assert(c, "_bloat_swing: c")
    assert(ev, "_bloat_swing: ev")
    QD.raid._bloat_pray(c)
    c.intent.attack = true
end

-- LEAVE: "it is recommended to run away after the last attack" (W:689), out
-- to the straight-leave tile past the stomp's reach while the stomp is still
-- ahead (raid seam49 N.leave_straight), else the mirror tile through the
-- tank, else the hug tile when one is hidden.
function QD.raid._bloat_leave(c, ev)
    assert(c, "_bloat_leave: c")
    assert(ev, "_bloat_leave: ev")
    local P, O, st, f = c.P, c.O, c.st, c.f
    QD.raid._bloat_pray(c)
    c.intent.attack = false
    local b = c.v.boss
    local x, z = 2 * O.x + P.mirror[1] - b.x, 2 * O.z + P.mirror[2] - b.z
    local lt = st.down ~= nil and st.down.leave_at ~= nil and st.down.leave_at.tile or nil
    if lt ~= nil and f.age <= P.stomp_age then
        x, z = lt.x, lt.z
    elseif f.hug_x ~= nil then
        x, z = f.hug_x, f.hug_z
    end
    c.go.x, c.go.z = x, z
end

-- RISE SWING (raid seam51, N.rise_swing).  The stomp is T+29 and he is UP
-- (damage halved, flies) only from T+33, so the rise T+30..T+32 is a
-- full-damage window: the reference's last swing of a down is at age 26.5
-- median, p90 31, and the flinch guide's "when he starts to get back up ...
-- that's when you click back" is the same window from the other side.
-- Without it a raider swung five times a down against the reference's six
-- (seam49 survey_5).  The press is decided on ages 29 and 30 -- the stomp
-- resolves in the NPC turn of T+29 before any player moves (ET 1.1), so the
-- way back in lands after it and the swing comes on T+30-31 -- and through
-- age 31 until the raider's own swing shows (iteration 3: a raider that ate
-- on the leave reached its tile on age 30 with the weapon not ready, and the
-- age-31 hide cancelled the swing, seam51 survey_2 down2 p0 and p2).  From
-- age 32 the duty is the leave again, for the first fly.
function QD.raid._bloat_rise(c, ev)
    assert(c, "_bloat_rise: c")
    assert(ev, "_bloat_rise: ev")
    QD.raid._bloat_pray(c)
    c.intent.attack = true
end

-- ENTRY'S STOMP PLAN (N.stomp_plan == "stay").  The stomp is tick-eaten in
-- place (wiki :675 "It is possible to tick eat this attack") and the flinch
-- is the click back on the rise (ENCOUNTER_TIMING.md 3.1), so Entry has
-- three down duties where Normal has three of its own: swing, stand, click
-- back.
function QD.raid._bloat_tick_eat(c, ev)
    assert(c, "_bloat_tick_eat: c")
    assert(ev, "_bloat_tick_eat: ev")
    QD.raid._bloat_pray(c)
    c.intent.attack = true
end

function QD.raid._bloat_stomp_eat(c, ev)
    assert(c, "_bloat_stomp_eat: c")
    assert(ev, "_bloat_stomp_eat: ev")
    QD.raid._bloat_pray(c)
    c.intent.attack = false
end

function QD.raid._bloat_flinch(c, ev)
    assert(c, "_bloat_flinch: c")
    assert(ev, "_bloat_flinch: ev")
    local P, O = c.P, c.O
    QD.raid._bloat_pray(c)
    c.intent.attack = false
    local b = c.v.boss
    c.go.x, c.go.z = 2 * O.x + P.mirror[1] - b.x, 2 * O.z + P.mirror[2] - b.z
end

-- A LIMB UNDERFOOT, for a state whose duty sets no target of its own: the
-- raider's own tile becomes the target, so the hazard step and the safe
-- route step it off (raid_play.lua _play_hazard; a hand is judged on the
-- tile the raider ends the tick before its impact on, ET 3.4).  The states
-- that DO set a target ignore this event on purpose: their tile was already
-- chosen off every live shadow (the hug's own test, the leave tile's, and
-- the route dodge's).
function QD.raid._bloat_step_off(c, ev)
    assert(c, "_bloat_step_off: c")
    assert(ev, "_bloat_step_off: ev")
    c.go.step_off = true
end

-- the duty handlers: the work, and the state the duty belongs to.  A state
-- that owes its own duty stays put (the layer ignores a transition to the
-- state the machine is already in), so one handler serves both readings.
local function duty_hide(c, ev) QD.raid._bloat_hide(c, ev) return nil, "hiding" end
local function duty_swing(c, ev) QD.raid._bloat_swing(c, ev) return nil, "attacking" end
local function duty_leave(c, ev) QD.raid._bloat_leave(c, ev) return nil, "leaving" end
local function duty_rise(c, ev) QD.raid._bloat_rise(c, ev) return nil, "rise_swing" end
local function duty_tick_eat(c, ev) QD.raid._bloat_tick_eat(c, ev) return nil, "tick_eat" end
local function duty_stomp_eat(c, ev) QD.raid._bloat_stomp_eat(c, ev) return nil, "stomp_eat" end
local function duty_flinch(c, ev) QD.raid._bloat_flinch(c, ev) return nil, "flinch" end
local function duty_outside(c, ev) return nil, "outside" end
local function step_off(c, ev) QD.raid._bloat_step_off(c, ev) end

-- THE RAIDER'S ROLE.  Every state handles every duty: its own by doing the
-- work and staying, another's by doing that work and going there -- so the
-- nine `on` tables below are the room's whole transition table, forward and
-- backward.
--
--   outside     his row is not in view: the seats waiting outside the
--               barrier (p2/p3 click it and the plan's first ticks run while
--               they cross) and the ticks after the kill.  The tick's intent
--               is empty, which is what the old body's early return did.
--   run_by      the starter's Defence drain owns the tick (bloat_runby
--               below): no hide walk, because the attack press IS the
--               approach.  Entered by force from the decide, with the reason
--               -- the run-by's own machine knows whether it owns the tick,
--               and that is not a reading the derivation can take.  Left by
--               the next duty, like any other state.
--   hiding      the walk: the far-side ring tile he cannot see, Protect from
--               Missiles lit.
--   attacking   the attackable window: in and swinging with Piety.
--   leaving     the run out of the stomp's reach, before it lands.
--   rise_swing  the way back in for the full-damage rise.
--   tick_eat /  Entry's stomp plan: swing, stand and eat the stomp, then
--   stomp_eat / click back on the rise.
--   flinch
QD.raid.sm_declare("bloat_raider", {
    start = "outside",
    states = {
        outside = { on = {
            boss_absent = function(c, ev) return nil end,
            flies = duty_hide, swing_window = duty_swing, leave_window = duty_leave,
            rise_window = duty_rise, tick_eat_window = duty_tick_eat,
            stomp_eat_window = duty_stomp_eat, flinch_window = duty_flinch,
        } },
        run_by = { on = {
            boss_absent = duty_outside,
            flies = duty_hide, swing_window = duty_swing, leave_window = duty_leave,
            rise_window = duty_rise, tick_eat_window = duty_tick_eat,
            stomp_eat_window = duty_stomp_eat, flinch_window = duty_flinch,
            -- it sets no target of its own while the special is swung
            limb_underfoot = step_off,
        } },
        hiding = { on = {
            boss_absent = duty_outside,
            flies = duty_hide, swing_window = duty_swing, leave_window = duty_leave,
            rise_window = duty_rise, tick_eat_window = duty_tick_eat,
            stomp_eat_window = duty_stomp_eat, flinch_window = duty_flinch,
        } },
        attacking = { on = {
            boss_absent = duty_outside,
            flies = duty_hide, swing_window = duty_swing, leave_window = duty_leave,
            rise_window = duty_rise, tick_eat_window = duty_tick_eat,
            stomp_eat_window = duty_stomp_eat, flinch_window = duty_flinch,
            limb_underfoot = step_off,
        } },
        leaving = { on = {
            boss_absent = duty_outside,
            flies = duty_hide, swing_window = duty_swing, leave_window = duty_leave,
            rise_window = duty_rise, tick_eat_window = duty_tick_eat,
            stomp_eat_window = duty_stomp_eat, flinch_window = duty_flinch,
        } },
        rise_swing = {
            -- the record the harness reads, written once per down on the way
            -- in (the old body's st.down.rise)
            enter = function(c, ev)
                local st, v = c.st, c.v
                if st.down ~= nil and st.down.rise == nil then
                    st.down.rise = { tick = v.tick, age = c.f.age }
                end
            end,
            on = {
                boss_absent = duty_outside,
                flies = duty_hide, swing_window = duty_swing, leave_window = duty_leave,
                rise_window = duty_rise, tick_eat_window = duty_tick_eat,
                stomp_eat_window = duty_stomp_eat, flinch_window = duty_flinch,
                limb_underfoot = step_off,
            },
        },
        tick_eat = { on = {
            boss_absent = duty_outside,
            flies = duty_hide, swing_window = duty_swing, leave_window = duty_leave,
            rise_window = duty_rise, tick_eat_window = duty_tick_eat,
            stomp_eat_window = duty_stomp_eat, flinch_window = duty_flinch,
            limb_underfoot = step_off,
        } },
        stomp_eat = { on = {
            boss_absent = duty_outside,
            flies = duty_hide, swing_window = duty_swing, leave_window = duty_leave,
            rise_window = duty_rise, tick_eat_window = duty_tick_eat,
            stomp_eat_window = duty_stomp_eat, flinch_window = duty_flinch,
            limb_underfoot = step_off,
        } },
        flinch = { on = {
            boss_absent = duty_outside,
            flies = duty_hide, swing_window = duty_swing, leave_window = duty_leave,
            rise_window = duty_rise, tick_eat_window = duty_tick_eat,
            stomp_eat_window = duty_stomp_eat, flinch_window = duty_flinch,
        } },
    },
})

-- raid seam32 play_tob_bloat_normal, ported to raid_sm 2026-10-07: THE
-- STARTER'S RUN-BY.  W:687 "While optional, one or two players should do a
-- run-by on the boss with a Bandos godsword special to lower its Defence";
-- the Dragon warhammer is the drain this cache has (N.runby;
-- tob_bloat_normal.lua:5) and drains 30% of the current Defence when its hit
-- is above 0 (DRIVER_NOTES "Bloat: Defence reads 80 of 80 after a Dragon
-- warhammer special").  Who: the one raider in the room before the first
-- down (W:687-689: the rest enter on the down), on the first walk -- the
-- CALLER's question, asked once in the decide.  The stomp restores Defence
-- (W:675), so the drain serves the first down.
--
-- A SPECIAL IS SEEN AS THE ENERGY IT SPENDS (varp300, the special orb's own
-- number), so this machine is driven by the `orb` event, and one handler a
-- state keeps each stage's checks in the order the old stage field read them
-- (the energy before the phase it gives up on).  The phase it gives up on is
-- the facts' (c.f.phase), the derivation's single reading, not one of its
-- own.  While a state owns the tick it sets rb.owns, and the decide then
-- forces the raider's own machine into `run_by`: no hide walk, because the
-- attack press is the approach.
--
--   waiting    -> equipping  the energy for one special and the hitpoints to
--                            stand in the flies: the hammer goes on
--   equipping  -> swinging   the special armed from the orb with the attack
--                            press
--   swinging   -> fired      the orb's energy fell by the cost
--   fired      -> swinging    a 0 splat drains nothing: swing again while the
--                            energy and the walk last (RUNBY_TRIES)
--   fired      -> done       the splat showed, or none in three ticks: the
--                            scythe back on in the same block as the walk
--                            back to the hide tile
--   any        -> gave_up    the first down came first, or no energy spent in
--                            RUNBY_GIVE_UP ticks
QD.RAID_PLAY_BLOAT_SPEC_COST = 500      -- the Dragon warhammer's special (DRIVER_NOTES seam10: "falls by 500 (DWH)")
QD.RAID_PLAY_BLOAT_RUNBY_GIVE_UP = 24   -- ticks from the arm with no energy spent (the seam's bound: four DWH swings)
-- Specials one run-by may spend.  1000 energy is two hammer specials, but a
-- second swing on a 0 kept the raider in the flies for 26 ticks, eating every
-- other tick and never swinging (_play_bloat t65-91, hitpoints 18-47): one.
QD.RAID_PLAY_BLOAT_RUNBY_TRIES = 1
QD.RAID_PLAY_BLOAT_RUNBY_EAT_TICKS = 3  -- the run-by's supplies horizon (the seam's choice: three flies, 45 with the prayer)

QD.raid.sm_declare("bloat_runby", {
    start = "waiting",
    states = {
        waiting = {
            note = "the first walk, waiting for the energy and the hitpoints",
            on = {
                orb = function(c, ev)
                    local st, v, rb = c.st, c.v, c.rb
                    if c.f.phase ~= "walk" or #st.downs > 0 then
                        rb.why = "the first down came first"
                        return nil, "gave_up"
                    end
                    if ev.energy < QD.RAID_PLAY_BLOAT_SPEC_COST or v.hp < 70 then
                        return nil
                    end
                    rb.equip_tick = v.tick
                    rb.energy0 = ev.energy
                    QD.raid._bloat_pray(c)
                    c.intent.gear = { rb.weapon }
                    c.intent.want.piety = true
                    rb.owns = true
                    return nil, "equipping"
                end,
            },
        },
        equipping = {
            note = "the hammer in hand; the special is armed with the next attack press",
            on = {
                orb = function(c, ev)
                    local v, rb = c.v, c.rb
                    QD.raid._bloat_pray(c)
                    c.intent.want.piety = true
                    rb.arm_tick = v.tick
                    c.intent.spec = true
                    c.intent.attack = true
                    c.st.engaged = false
                    rb.owns = true
                    return nil, "swinging"
                end,
                limb_underfoot = step_off,
            },
        },
        swinging = {
            note = "armed and swinging: the special is spent when the orb's energy falls by its cost",
            on = {
                orb = function(c, ev)
                    local v, rb, f = c.v, c.rb, c.f
                    QD.raid._bloat_pray(c)
                    c.intent.want.piety = true
                    if ev.energy <= rb.energy0 - QD.RAID_PLAY_BLOAT_SPEC_COST then
                        rb.fired = v.tick
                        rb.energy1 = ev.energy
                        rb.cycle_at_fire = v.boss ~= nil and v.boss.hit_cycle or nil
                        rb.owns = true
                        return nil, "fired"
                    end
                    if v.tick - rb.arm_tick > QD.RAID_PLAY_BLOAT_RUNBY_GIVE_UP or f.phase ~= "walk" then
                        rb.why = "no energy spent in " .. (v.tick - rb.arm_tick) .. " ticks (phase " .. f.phase .. ")"
                        c.intent.gear = { "scythe_of_vitur" }
                        c.intent.want.piety = nil
                        return nil, "gave_up"
                    end
                    -- re-arm if the orb reads unarmed two ticks on and nothing was spent
                    local _, armed = QD.var.varp("varp301_sa_attack")
                    if tonumber(armed) == 0 and v.tick - rb.arm_tick >= 2 and (rb.rearm or 0) < 3 then
                        rb.rearm = (rb.rearm or 0) + 1
                        rb.arm_tick_last = v.tick
                        c.intent.spec = true
                    end
                    c.intent.attack = true
                    rb.owns = true
                end,
                limb_underfoot = step_off,
            },
        },
        fired = {
            note = "the hammer stays on until the special's own splat shows on Bloat",
            on = {
                orb = function(c, ev)
                    local v, rb, f = c.v, c.rb, c.f
                    QD.raid._bloat_pray(c)
                    c.intent.want.piety = true
                    local b = v.boss
                    local seen = b ~= nil and b.hit_cycle ~= nil and rb.cycle_at_fire ~= nil
                        and b.hit_cycle > rb.cycle_at_fire
                    if seen then
                        rb.splat = b.hit_damage
                        rb.splats[#rb.splats + 1] = tostring(b.hit_damage) .. "@t" .. v.tick
                    end
                    -- a 0 splat drains nothing (DRIVER_NOTES seam10): with the
                    -- energy for another and the walk still on, arm again
                    if seen and rb.splat == 0 and ev.energy >= QD.RAID_PLAY_BLOAT_SPEC_COST
                        and f.phase == "walk" and #rb.splats < QD.RAID_PLAY_BLOAT_RUNBY_TRIES then
                        rb.arm_tick = v.tick
                        rb.energy0 = ev.energy
                        c.intent.spec = true
                        c.intent.attack = true
                        c.st.engaged = false
                        rb.owns = true
                        return nil, "swinging"
                    end
                    if seen or v.tick - rb.fired >= 3 or f.phase ~= "walk" then
                        c.intent.gear = { "scythe_of_vitur" }
                        c.intent.want.piety = nil
                        return nil, "done"
                    end
                    rb.owns = true
                end,
                limb_underfoot = step_off,
            },
        },
        -- the drain is done, or it never happened: the raider plays the room
        -- like every other seat from here (its own machine owns every tick)
        done = { note = "the scythe back on, the drain spent" },
        gave_up = { note = "the first down came first, or no energy was spent" },
    },
})

-- st.runby is the record the harness reads; rb.owns is this tick's answer.
function QD.raid._bloat_run_by(st, v, c, events)
    assert(st, "_bloat_run_by: st")
    assert(v, "_bloat_run_by: v")
    assert(c, "_bloat_run_by: c")
    assert(type(events) == "table", "_bloat_run_by: events")
    local rb = st.runby
    if rb == nil then
        rb = { weapon = st.numbers.runby, splats = {} }
        st.runby = rb
    end
    rb.owns = false
    c.rb = rb
    local m = QD.raid.sm_run(st, v, "bloat_runby", c, events)
    rb.stage = m.state
    return rb.owns
end

-- raid seam49 play_tob_bloat_round2, ported to raid_sm 2026-10-07: THE
-- DOWN'S SPECIAL.  The reference's trios open a down with a special: the
-- Dragon claws in down 2 in 12 of 19 rooms, the crystal halberd in down 1 in
-- 17-18 of 19 (seam49 against_1.log, weapon flags).  This content's crystal
-- halberd special is ONE hit (its large-target second hit is not reproduced:
-- pvm_dragon_halberd.rs2:18-26, no npc_size opcode), so on a 7-tick weapon it
-- costs damage against a scythe swing here; the claws (four hits, 4 ticks,
-- pvm_dragon_claws.rs2) are the special this content can land, and the energy
-- for two (100%) is spent on the first two downs, the first attack of each.
--
-- THE GEAR CHANGES BELONG TO THE STATES: the claws go on during the walk (no
-- attack is lost: the walk has none), the special is armed from the orb with
-- the down's first attack press (varp301 read, so a second press never
-- disarms it), and the scythe goes back on the tick the energy falls by the
-- cost (varp300; DRIVER_NOTES seam10).
--
--   stowed -> worn   a walk tick, the energy for a special, and nothing else
--                    claiming the gear channel this tick
--   worn   -> armed  the down's first attack press carries the special
--   armed  -> stowed the energy fell (fired), or it never did (given up):
--                    the scythe back on
--   armed  -> spent  the last of N.down_spec specials has fired
QD.RAID_PLAY_BLOAT_CLAWS_COST = 500
QD.RAID_PLAY_BLOAT_CLAWS_GIVE_UP = 8   -- ticks armed with no energy spent

-- the scythe back on, in the same block as whatever walk this tick carries
function QD.raid._bloat_claws_back(c)
    assert(c, "_bloat_claws_back: c")
    local intent = c.intent
    if intent.gear == nil then intent.gear = {} end
    intent.gear[#intent.gear + 1] = "scythe_of_vitur"
    c.ds.worn = false
    c.ds.armed = nil
    c.st.engaged = false
end

-- is another special still owed, and is the orb holding its cost?
local function claws_wanted(c, ev)
    return c.ds.fired < c.N.down_spec and ev.energy >= QD.RAID_PLAY_BLOAT_CLAWS_COST
end

QD.raid.sm_declare("bloat_down_spec", {
    start = "stowed",
    states = {
        stowed = {
            note = "the scythe in hand, the claws carried",
            on = {
                orb = function(c, ev)
                    if not claws_wanted(c, ev) then return nil end
                    if c.f.phase == "walk" and c.intent.gear == nil then
                        c.intent.gear = { "dragon_claws" }
                        c.ds.worn = true
                        return nil, "worn"
                    end
                end,
            },
        },
        worn = {
            note = "the claws in hand, waiting for the down's first attack press",
            on = {
                orb = function(c, ev)
                    local st, v, ds, f = c.st, c.v, c.ds, c.f
                    if not claws_wanted(c, ev) then
                        if f.phase == "walk" then
                            QD.raid._bloat_claws_back(c)
                            return nil, "stowed"
                        end
                        return nil
                    end
                    if f.phase == "down" and c.intent.attack
                        and (st.down == nil or st.down.spec_fired == nil) then
                        local _, armed = QD.var.varp("varp301_sa_attack")
                        if tonumber(armed) == 0 then c.intent.spec = true end
                        ds.armed = v.tick
                        ds.armed_phase = f.phase
                        ds.energy0 = ev.energy
                        ds.log[#ds.log + 1] = "armed t" .. v.tick .. " age " .. f.age .. " energy " .. ev.energy
                        return nil, "armed"
                    end
                end,
            },
        },
        armed = {
            note = "the special armed; it is SEEN as the energy it spends",
            on = {
                orb = function(c, ev)
                    local st, v, ds, f = c.st, c.v, c.ds, c.f
                    if ev.energy <= ds.energy0 - QD.RAID_PLAY_BLOAT_CLAWS_COST then
                        ds.fired = ds.fired + 1
                        ds.log[#ds.log + 1] = "fired t" .. v.tick .. " age " .. f.age
                        if st.down ~= nil then st.down.spec_fired = v.tick end
                        QD.raid._bloat_claws_back(c)
                        if ds.fired >= c.N.down_spec then return nil, "spent" end
                        return nil, "stowed"
                    end
                    if v.tick - ds.armed > QD.RAID_PLAY_BLOAT_CLAWS_GIVE_UP
                        or (f.phase == "walk" and ds.armed_phase == "down") then
                        ds.log[#ds.log + 1] = "gave up t" .. v.tick
                        QD.raid._bloat_claws_back(c)
                        return nil, "stowed"
                    end
                    local _, armed = QD.var.varp("varp301_sa_attack")
                    if tonumber(armed) == 0 and c.intent.attack and v.tick - ds.armed >= 2 then
                        c.intent.spec = true
                    end
                end,
            },
        },
        spent = { note = "every special this plan spends has fired" },
    },
})

-- st.down_spec is the record the harness reads (its log lines and the count)
function QD.raid._bloat_down_spec(st, v, c, events)
    assert(st, "_bloat_down_spec: st")
    assert(v, "_bloat_down_spec: v")
    assert(c, "_bloat_down_spec: c")
    assert(type(events) == "table", "_bloat_down_spec: events")
    local ds = st.down_spec
    if ds == nil then
        ds = { worn = false, fired = 0, log = {} }
        st.down_spec = ds
    end
    c.ds = ds
    local m = QD.raid.sm_run(st, v, "bloat_down_spec", c, events)
    ds.state = m.state
end

-- THE BLOAT PLAN'S DECIDE (PLAY_NOTES.md "Bloat"): the facts, the events,
-- the machines, and then the executor.  The strategy the states carry out is
-- unchanged.  Walk: hide straight behind the tank from where Bloat will be
-- ("Hug the pillar and hide from Bloat as it walks around the room",
-- wiki_Theatre_of_Blood_Strategies :687), Protect from Missiles lit, off any
-- shadow ("simply don't stand on the shadows", yt_4i4lv-srJkw.md:71).  Down:
-- in at once and swing on cooldown with Piety ("As soon as Bloat
-- deactivates ... begin attacking with melee ... five attacks when close",
-- wiki :689).  The stomp: Entry tick-eats it in place (wiki :675) and clicks
-- back on the rise ("when he starts to get back up ... that's when you click
-- back", the flinch guide, ENCOUNTER_TIMING.md 3.1); Normal/Hard runs out of
-- its reach after the last swing that fits ("run away after the last
-- attack", wiki :689).
function QD.raid._play_bloat_decide(st, v)
    local P, N, O = st.plan, st.numbers, st.origin
    local intent = { want = {}, walk = nil, attack = false }
    local c = { st = st, v = v, P = P, N = N, O = O, intent = intent, go = { step_off = false } }
    if v.boss == nil then
        -- he is not in this raider's view: a seat still crossing the barrier,
        -- and the ticks after his death row.  The machines are told so and
        -- the tick's intent stays empty, which is what the old body's early
        -- return did.
        local events = QD.raid.sm_events(st, v, QD.raid._bloat_events)
        QD.raid.sm_run(st, v, "bloat_cycle", c, events)
        QD.raid.sm_run(st, v, "bloat_raider", c, events)
        return intent
    end
    if st.first_tick == nil then st.first_tick = v.tick end
    -- owner_rooms4: run kept on (QD.raid._play_run_keep, below the plan table)
    QD.raid._play_run_keep(st, v)
    local f = QD.raid._bloat_facts(st, v, intent)
    c.f = f
    local events = QD.raid.sm_events(st, v, QD.raid._bloat_events)
    QD.raid.sm_run(st, v, "bloat_cycle", c, events)
    -- WHOSE RUN-BY IT IS, is the caller's question (CLAUDE.md: the existence
    -- test goes where the knowledge is): the starter of a party, in a mode
    -- that carries a drain weapon.  Normal carries none -- the 30 recorded
    -- Normal trio rooms hold no Dragon warhammer special (raid seam42).
    local owns = false
    if N.runby ~= nil and st.party > 1 and st.role == 1 then
        owns = QD.raid._bloat_run_by(st, v, c, events)
    end
    if owns then
        QD.raid.sm_force(st, v, "bloat_raider", "run_by", c, "runby_owns")
    else
        QD.raid.sm_run(st, v, "bloat_raider", c, events)
    end
    -- THE EXECUTOR.  raid seam29: the plan's own walk (the hide, the leave,
    -- the flinch) is `plan_walk`; with none, the attack press's own path and
    -- a marker under a standing raider go through the same skills
    -- (raid_play.lua _play_reach, _play_hazard, _play_bloat_safe_route).
    local target_x, target_z = c.go.x, c.go.z
    local plan_walk = target_x ~= nil
    if not plan_walk and intent.attack then
        local rx, rz, hold = QD.raid._play_reach(st, v, f.floor_ok)
        if rx ~= nil then
            target_x, target_z = rx, rz
        elseif hold then
            intent.attack = false
        end
    end
    if target_x == nil and c.go.step_off then
        target_x, target_z = v.me.x, v.me.z
    end
    if target_x ~= nil then
        local sx, sz = QD.raid._play_hazard(st, v, target_x, target_z, f.floor_ok)
        sx, sz = QD.raid._play_bloat_safe_route(st, v, sx, sz, f.floor_ok)
        if f.on_shadow then st.dodges = st.dodges + 1 end
        local same = st.walk_target ~= nil and st.walk_target.x == sx and st.walk_target.z == sz
        local stuck = st.last_me ~= nil and st.last_me.x == v.me.x and st.last_me.z == v.me.z
        if (v.me.x ~= sx or v.me.z ~= sz) and (not same or stuck) then
            intent.walk = { x = sx, z = sz }
            if plan_walk and f.phase == "down" and st.down.flinch == nil then
                st.down.flinch = { tick = v.tick, age = f.age, from = { x = v.me.x, z = v.me.z } }
                st.flinches[#st.flinches + 1] = st.down.flinch
            end
        end
    end
    -- THE SUPPLIES' HORIZON, per state.  raid seam32: while the run-by's
    -- special is being swung a bite costs the swing 3 ticks (wiki Food,
    -- consume_shared.rs2:28-49), and on a free tick the library looks six
    -- ticks ahead, so a raider standing in the flies ate every other tick and
    -- never swung (_play_bloat t67-92: 25 ticks targeted, no swing).  The
    -- run-by is a few ticks in the flies by design (W:687), so it eats only
    -- for what can land in the next RUNBY_EAT_TICKS ticks.
    -- raid seam51 (N.rise_swing): on the rise the first fly is T+33, and the
    -- library's free-tick horizon (a swing's length ahead) counted it on ages
    -- 29-30, so the raider ate on its way back in and the bite cost the swing
    -- (seam51 survey_1 _play_bloat down2: p0 ate at age 30, no rise swing).
    -- Through the swing the supplies look only to T+32: what can land before
    -- the first fly, i.e. a raider low enough to die to nothing still eats.
    local supplies_threat = f.threat
    if N.rise_swing and f.phase == "down" and f.age >= P.stomp_age and f.age <= P.rise_age + 1 then
        supplies_threat = function(h) return f.threat(math.min(h, P.up_age - 1 - f.age)) end
    end
    if owns then
        supplies_threat = function(h) return f.threat(math.min(h, QD.RAID_PLAY_BLOAT_RUNBY_EAT_TICKS)) end
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, supplies_threat)
    if N.offence_pots and intent.drink == nil then
        intent.drink = QD.raid._play_bloat_offence(st, v, f.phase)
    end
    intent.attack = QD.raid._play_attack(st, v, intent.attack and intent.walk == nil)
    if N.down_spec ~= nil then
        QD.raid._bloat_down_spec(st, v, c, events)
    end
    return intent
end

-- raid seam32: THE OFFENCE POTIONS (N.offence_pots).  A Saradomin brew drains
-- Attack and Strength (wiki Saradomin brew :56; br_potion.rs2:78-79), a
-- super restore brings them back to base (wiki Super restore; br_potion.rs2
-- :93-94), and the super combat's boost is re-sipped on a walk when it has
-- worn down (yt_4i4lv-srJkw.md:47).  Read from the raider's own skills (what a
-- person sees in the stats tab).  Returns a potion name or nil.
QD.RAID_PLAY_COMBAT_POTS = { "1dose2combat", "2dose2combat", "3dose2combat", "4dose2combat" }
function QD.raid._play_bloat_offence(st, v, phase)
    if v.tick - st.last_drink < QD.RAID_PLAY_DRINK_DELAY then
        return nil
    end
    local ar, att = QD.skill.read("attack")
    if ar ~= "ok" then
        return nil
    end
    local base = att.base or att.base_level or 99
    local list = nil
    if att.level < base then
        list = QD.RAID_PLAY_RESTORES
    elseif phase == "walk" and att.level < base + 10 then
        list = QD.RAID_PLAY_COMBAT_POTS
    end
    if list == nil then
        return nil
    end
    for _, name in ipairs(list) do
        local cr, n = QD.inv.count(name)
        if cr == "ok" and n > 0 then
            st.offence_pots = (st.offence_pots or 0) + 1
            return name
        end
    end
    return nil
end
