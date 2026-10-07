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
            hug_tank = true, hug_seen = true, leave_straight = true, down_spec = 2,
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

-- THE BLOAT PLAN'S DECIDE (PLAY_NOTES.md "Bloat").  Walk: hide straight
-- behind the tank from where Bloat will be ("Hug the pillar and hide from
-- Bloat as it walks around the room", wiki_Theatre_of_Blood_Strategies
-- :687), Protect from Missiles lit, off any shadow ("simply don't stand on
-- the shadows", transcripts/yt_4i4lv-srJkw.md:71).  Down: in at once and
-- swing on cooldown with Piety ("As soon as Bloat deactivates ... begin
-- attacking with melee ... five attacks when close", wiki :689).  The stomp:
-- Entry tick-eats it in place (wiki :675) and clicks back on the rise ("when
-- he starts to get back up ... that's when you click back", the flinch
-- guide, ENCOUNTER_TIMING.md 3.1); Normal/Hard runs out of its reach after
-- the last swing that fits ("run away after the last attack", wiki :689).
function QD.raid._play_bloat_decide(st, v)
    local P, N, O = st.plan, st.numbers, st.origin
    local intent = { want = {}, walk = nil, attack = false }
    local b = v.boss
    if b == nil then
        return intent
    end
    if st.first_tick == nil then st.first_tick = v.tick end
    -- owner_rooms4: run kept on (QD.raid._play_run_keep, below the plan table)
    QD.raid._play_run_keep(st, v)
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
    local function floor_ok(x, z)
        local inside = x >= O.x + P.floor[1] and x <= O.x + P.floor[3] and z >= O.z + P.floor[2] and z <= O.z + P.floor[4]
        local tank = x >= O.x + P.tank[1] and x <= O.x + P.tank[3] and z >= O.z + P.tank[2] and z <= O.z + P.tank[4]
        return inside and not tank
    end
    -- where Bloat will be two ticks on (its walk is visible); the hide tile
    -- is that tile mirrored through the tank (tob_bloat.lua :1206)
    local fx, fz = b.x, b.z
    if phase == "walk" and st.prev_b ~= nil then
        fx = math.max(O.x + P.ring[1], math.min(O.x + P.ring[3], b.x + 2 * (b.x - st.prev_b.x)))
        fz = math.max(O.z + P.ring[2], math.min(O.z + P.ring[4], b.z + 2 * (b.z - st.prev_b.z)))
    end
    st.prev_b = { x = b.x, z = b.z }
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
    local in_stomp = math.max(math.abs(v.me.x - (b.x + P.stomp_centre)), math.abs(v.me.z - (b.z + P.stomp_centre)))
        <= P.stomp_range
    local hidden = math.max(math.abs(v.me.x - hide_x), math.abs(v.me.z - hide_z)) <= 1
    local on_shadow = v.shadows[v.me.x * 100000 + v.me.z] == true
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
    -- raid seam32: the most one fly lands with Protect from Missiles (W:673).
    -- The plan lights it on every walking tick and from T+32, the tick before
    -- the first fly of a rise (walk_prayers; the `list` below), so every fly
    -- the threat counts lands on a prayed raider while any prayer is left.
    -- Entry carries no fly_prayed and reads N.fly as before.
    local straight_out = N.leave_straight and st.down ~= nil and st.down.leave_at ~= nil and st.down.leave_at.tile ~= nil
    local fly = N.fly
    if N.fly_prayed ~= nil and v.prayer > 0 then fly = N.fly_prayed end
    local function threat(h)
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
    -- prayers for the NEXT tick: down prayers through the attackable window,
    -- the walk's prayer from the tick before the first fly (T+33)
    local list = P.walk_prayers
    if phase == "down" and age < P.up_age - 1 then list = P.down_prayers end
    for _, name in ipairs(list) do intent.want[name] = true end
    -- raid seam32: THE RUN-BY (W:687), the first walk, the raider in the room.
    local runby = QD.raid._play_bloat_runby(st, v, phase, intent)
    local target_x, target_z = nil, nil
    if runby then
        -- the spec is being swung: no hide walk, the attack press paths in
    elseif phase == "walk" then
        target_x, target_z = hide_x, hide_z
    elseif N.stomp_plan == "stay" then
        intent.attack = age < P.stomp_age
        if age >= P.rise_age then
            target_x, target_z = 2 * O.x + P.mirror[1] - b.x, 2 * O.z + P.mirror[2] - b.z
        end
    else
        intent.attack = age < leave_age
        -- raid seam51 (N.rise_swing): THE RISE SWING.  The stomp is T+29 and
        -- Bloat is UP (damage halved, flies) only from T+33 (ET 3.1), so the
        -- rise T+30..T+32 is a full-damage window: the reference's last swing
        -- of a down is at age 26.5 median, p90 31 (PLAY_NOTES "Bloat, Normal
        -- trio -- follows Blert"), and the flinch guide's "when he starts to
        -- get back up ... that's when you click back" is the same window from
        -- the other side.  Without it a raider swung five times a down
        -- against the reference's six (seam49 survey_5: ages 4-23 / 6-25,
        -- the leave at 26-27, nothing after).  The press is decided on ages
        -- 29 and 30: the stomp has resolved in the NPC turn of T+29 before
        -- any player moves (ET 1.1), so the way back in lands after it, the
        -- swing on T+30-31; from age 31 the raider hides for the first fly.
        -- (iteration 3: until the raider's own swing shows, through age 31:
        -- a raider that ate on the leave reached its tile on age 30 with the
        -- weapon not ready and the age-31 hide cancelled the swing, seam51
        -- survey_2 _play_bloat down2 p0 and p2)
        local rise_open = N.rise_swing and age >= P.stomp_age and age <= P.rise_age + 1
            and st.down ~= nil and st.last_swing < st.down.tick + P.stomp_age
        if rise_open then
            intent.attack = true
            if st.down ~= nil and st.down.rise == nil then st.down.rise = { tick = v.tick, age = age } end
        elseif age >= leave_age then
            target_x, target_z = 2 * O.x + P.mirror[1] - b.x, 2 * O.z + P.mirror[2] - b.z
            local lt = st.down ~= nil and st.down.leave_at ~= nil and st.down.leave_at.tile or nil
            if lt ~= nil and age <= P.stomp_age then
                target_x, target_z = lt.x, lt.z
            elseif hug_x ~= nil then
                target_x, target_z = hug_x, hug_z
            end
        end
    end
    if phase == "down" and age >= P.stomp_age - 2 and age <= P.stomp_age - 1 and st.down.pre_stomp == nil then
        st.down.pre_stomp = v.hp
    end
    -- raid seam29: the plan's own walk (the hide, the flinch, the leave) is
    -- `plan_walk`; with none, the attack press's own path and a marker under a
    -- standing raider go through the same skills (raid_play.lua _play_reach,
    -- _play_hazard, _play_safe_step).
    local plan_walk = target_x ~= nil
    if not plan_walk and intent.attack then
        local rx, rz, hold = QD.raid._play_reach(st, v, floor_ok)
        if rx ~= nil then
            target_x, target_z = rx, rz
        elseif hold then
            intent.attack = false
        end
    end
    if target_x == nil and on_shadow then
        target_x, target_z = v.me.x, v.me.z
    end
    if target_x ~= nil then
        local sx, sz, moved = QD.raid._play_hazard(st, v, target_x, target_z, floor_ok)
        sx, sz = QD.raid._play_bloat_safe_route(st, v, sx, sz, floor_ok)
        if on_shadow then st.dodges = st.dodges + 1 end
        local same = st.walk_target ~= nil and st.walk_target.x == sx and st.walk_target.z == sz
        local stuck = st.last_me ~= nil and st.last_me.x == v.me.x and st.last_me.z == v.me.z
        if (v.me.x ~= sx or v.me.z ~= sz) and (not same or stuck) then
            intent.walk = { x = sx, z = sz }
            if plan_walk and phase == "down" and st.down.flinch == nil then
                st.down.flinch = { tick = v.tick, age = age, from = { x = v.me.x, z = v.me.z } }
                st.flinches[#st.flinches + 1] = st.down.flinch
            end
        end
    end
    -- raid seam32: while the run-by's special is being swung a bite costs the
    -- swing 3 ticks (wiki Food, consume_shared.rs2:28-49), and on a free tick
    -- the library looks six ticks ahead, so a raider standing in the flies ate
    -- every other tick and never swung (_play_bloat t67-92: 25 ticks targeted,
    -- no swing).  The run-by is a few ticks in the flies by design (W:687), so
    -- it eats only for what can land in the next RUNBY_EAT_TICKS ticks.
    local supplies_threat = threat
    -- raid seam51 (N.rise_swing): on the rise the first fly is T+33, and the
    -- library's free-tick horizon (a swing's length ahead) counted it on ages
    -- 29-30, so the raider ate on its way back in and the bite cost the swing
    -- (seam51 survey_1 _play_bloat down2: p0 ate at age 30, no rise swing).
    -- Through the swing the supplies look only to T+32: what can land before
    -- the first fly, i.e. a raider low enough to die to nothing still eats.
    if N.rise_swing and phase == "down" and age >= P.stomp_age and age <= P.rise_age + 1 then
        supplies_threat = function(h) return threat(math.min(h, P.up_age - 1 - age)) end
    end
    if runby then
        supplies_threat = function(h) return threat(math.min(h, QD.RAID_PLAY_BLOAT_RUNBY_EAT_TICKS)) end
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, supplies_threat)
    if N.offence_pots and intent.drink == nil then
        intent.drink = QD.raid._play_bloat_offence(st, v, phase)
    end
    intent.attack = QD.raid._play_attack(st, v, intent.attack and intent.walk == nil)
    if N.down_spec ~= nil then
        QD.raid._play_bloat_down_spec(st, v, phase, age, intent)
    end
    return intent
end

-- raid seam49 play_tob_bloat_round2: THE DOWN'S SPECIAL.  The reference's
-- trios open a down with a special: the Dragon claws in down 2 in 12 of 19
-- rooms, the crystal halberd in down 1 in 17-18 of 19 (seam49 against_1.log,
-- weapon flags).  This content's crystal halberd special is ONE hit (its
-- large-target second hit is not reproduced: pvm_dragon_halberd.rs2:18-26,
-- no npc_size opcode), so on a 7-tick weapon it costs damage against a
-- scythe swing here; the claws (four hits, 4 ticks, pvm_dragon_claws.rs2)
-- are the special this content can land, and the energy for two (100%) is
-- spent on the first two downs, the first attack of each.  The claws go on
-- during the walk (no attack is lost: the walk has none), the special is
-- armed from the orb (varp301 read, so a second press never disarms it),
-- the attack press carries it, and the scythe goes back on the tick the
-- energy falls by the cost (varp300; DRIVER_NOTES seam10).
QD.RAID_PLAY_BLOAT_CLAWS_COST = 500
QD.RAID_PLAY_BLOAT_CLAWS_GIVE_UP = 8   -- ticks armed with no energy spent
function QD.raid._play_bloat_down_spec(st, v, phase, age, intent)
    local N = st.numbers
    local ds = st.down_spec
    if ds == nil then
        ds = { worn = false, fired = 0, log = {} }
        st.down_spec = ds
    end
    local _, energy = QD.var.varp("varp300_sa_energy")
    energy = tonumber(energy) or 0
    local function back()
        if intent.gear == nil then intent.gear = {} end
        intent.gear[#intent.gear + 1] = "scythe_of_vitur"
        ds.worn = false
        ds.armed = nil
        st.engaged = false
    end
    if ds.armed ~= nil then
        if energy <= ds.energy0 - QD.RAID_PLAY_BLOAT_CLAWS_COST then
            ds.fired = ds.fired + 1
            ds.log[#ds.log + 1] = "fired t" .. v.tick .. " age " .. age
            if st.down ~= nil then st.down.spec_fired = v.tick end
            back()
        elseif v.tick - ds.armed > QD.RAID_PLAY_BLOAT_CLAWS_GIVE_UP or (phase == "walk" and ds.armed_phase == "down") then
            ds.log[#ds.log + 1] = "gave up t" .. v.tick
            back()
        else
            local _, armed = QD.var.varp("varp301_sa_attack")
            if tonumber(armed) == 0 and intent.attack and v.tick - ds.armed >= 2 then
                intent.spec = true
            end
        end
        return
    end
    local want = ds.fired < N.down_spec and energy >= QD.RAID_PLAY_BLOAT_CLAWS_COST
    if not want then
        if ds.worn and phase == "walk" then back() end
        return
    end
    if not ds.worn then
        if phase == "walk" and intent.gear == nil then
            intent.gear = { "dragon_claws" }
            ds.worn = true
        end
        return
    end
    -- worn and wanted: arm with the down's first attack press
    if phase == "down" and intent.attack and (st.down == nil or st.down.spec_fired == nil) then
        local _, armed = QD.var.varp("varp301_sa_attack")
        if tonumber(armed) == 0 then intent.spec = true end
        ds.armed = v.tick
        ds.armed_phase = phase
        ds.energy0 = energy
        ds.log[#ds.log + 1] = "armed t" .. v.tick .. " age " .. age .. " energy " .. energy
    end
end

-- raid seam32 play_tob_bloat_normal: THE RUN-BY.  W:687 "While optional, one
-- or two players should do a run-by on the boss with a Bandos godsword special
-- to lower its Defence"; the Dragon warhammer is the drain this cache has
-- (N.runby; tob_bloat_normal.lua:5) and drains 30% of the current Defence when
-- its hit is above 0 (DRIVER_NOTES "Bloat: Defence reads 80 of 80 after a
-- Dragon warhammer special").  Who: the one raider in the room before the
-- first down (W:687-689: the rest enter on the down), on the first walk.
-- The kit goes on in one block, the special is armed from the orb with the
-- attack press the next tick, and the swing is SEEN as the energy it spends
-- (varp300, the special orb's own number).  The hammer stays on until the
-- special's splat shows on Bloat (a 0 drains nothing and is swung again while
-- the energy lasts); on that tick the scythe goes back on in the same block as
-- the walk back to the hide tile ("the scythe back the same tick").  The stomp restores Defence (W:675), so the drain serves
-- the first down.  Returns true while the run-by owns the tick (no hide walk;
-- the attack press is the approach).  st.runby is the record.
QD.RAID_PLAY_BLOAT_SPEC_COST = 500      -- the Dragon warhammer's special (DRIVER_NOTES seam10: "falls by 500 (DWH)")
QD.RAID_PLAY_BLOAT_RUNBY_GIVE_UP = 24   -- ticks from the arm with no energy spent (the seam's bound: four DWH swings)
-- Specials one run-by may spend.  1000 energy is two hammer specials, but a
-- second swing on a 0 kept the raider in the flies for 26 ticks, eating every
-- other tick and never swinging (_play_bloat t65-91, hitpoints 18-47): one.
QD.RAID_PLAY_BLOAT_RUNBY_TRIES = 1
QD.RAID_PLAY_BLOAT_RUNBY_EAT_TICKS = 3  -- the run-by's supplies horizon (the seam's choice: three flies, 45 with the prayer)
function QD.raid._play_bloat_runby(st, v, phase, intent)
    local N = st.numbers
    if N.runby == nil or st.party <= 1 or st.role ~= 1 then
        return false
    end
    local rb = st.runby
    if rb == nil then
        rb = { stage = "wait", weapon = N.runby, splats = {} }
        st.runby = rb
    end
    if rb.stage == "done" or rb.stage == "gave_up" then
        return false
    end
    local _, energy = QD.var.varp("varp300_sa_energy")
    energy = tonumber(energy) or 0
    if rb.stage == "wait" then
        -- the first walk, with the energy for one special and the hitpoints
        -- to stand in the flies while it is swung
        if phase ~= "walk" or #st.downs > 0 then
            rb.stage = "gave_up"
            rb.why = "the first down came first"
            return false
        end
        if energy < QD.RAID_PLAY_BLOAT_SPEC_COST or v.hp < 70 then
            return false
        end
        rb.stage = "equip"
        rb.equip_tick = v.tick
        rb.energy0 = energy
        intent.gear = { rb.weapon }
        intent.want.piety = true
        return true
    end
    intent.want.piety = true
    if rb.stage == "equip" then
        rb.stage = "swing"
        rb.arm_tick = v.tick
        intent.spec = true
        intent.attack = true
        st.engaged = false
        return true
    end
    -- "fired": the hammer stays on until the special's own splat shows on
    -- Bloat (the npc row's latest hitsplat, what a person sees: one tick after
    -- the swing).  A 0 drains nothing (DRIVER_NOTES seam10 "A 0 splat drains
    -- nothing: try again ... while energy is 500 or more"): with the energy
    -- for another and the walk still on, the special is armed again.  Any
    -- other splat, or none in three ticks, ends the run-by: the scythe goes
    -- back on in the same block as the walk back to the hide tile.
    if rb.stage == "fired" then
        local seen = v.boss ~= nil and v.boss.hit_cycle ~= nil and rb.cycle_at_fire ~= nil and v.boss.hit_cycle > rb.cycle_at_fire
        if seen then
            rb.splat = v.boss.hit_damage
            rb.splats[#rb.splats + 1] = tostring(v.boss.hit_damage) .. "@t" .. v.tick
        end
        if seen and rb.splat == 0 and energy >= QD.RAID_PLAY_BLOAT_SPEC_COST and phase == "walk"
            and #rb.splats < QD.RAID_PLAY_BLOAT_RUNBY_TRIES then
            rb.stage = "swing"
            rb.arm_tick = v.tick
            rb.energy0 = energy
            intent.spec = true
            intent.attack = true
            st.engaged = false
            return true
        end
        if seen or v.tick - rb.fired >= 3 or phase ~= "walk" then
            rb.stage = "done"
            intent.gear = { "scythe_of_vitur" }
            intent.want.piety = nil
            return false
        end
        return true
    end
    -- "swing": the special is spent when the orb's energy falls by its cost
    if energy <= rb.energy0 - QD.RAID_PLAY_BLOAT_SPEC_COST then
        rb.stage = "fired"
        rb.fired = v.tick
        rb.energy1 = energy
        rb.cycle_at_fire = v.boss ~= nil and v.boss.hit_cycle or nil
        return true
    end
    if v.tick - rb.arm_tick > QD.RAID_PLAY_BLOAT_RUNBY_GIVE_UP or phase ~= "walk" then
        rb.stage = "gave_up"
        rb.why = "no energy spent in " .. (v.tick - rb.arm_tick) .. " ticks (phase " .. phase .. ")"
        intent.gear = { "scythe_of_vitur" }
        intent.want.piety = nil
        return false
    end
    -- re-arm if the orb reads unarmed two ticks on and nothing was spent
    local _, armed = QD.var.varp("varp301_sa_attack")
    if tonumber(armed) == 0 and v.tick - rb.arm_tick >= 2 and (rb.rearm or 0) < 3 then
        rb.rearm = (rb.rearm or 0) + 1
        rb.arm_tick_last = v.tick
        intent.spec = true
    end
    intent.attack = true
    return true
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
