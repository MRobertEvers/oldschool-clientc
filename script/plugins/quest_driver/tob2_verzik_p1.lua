-- quest-driver / tob2_verzik_p1: VERZIK VITUR PHASE 1, HARD MODE, five raiders,
-- on the v2 layers (docs/minigames/theater_of_blood/TOB2_DESIGN.md; the plan:
-- docs/minigames/theater_of_blood/tob2_rooms/verzik_p1.md).
--
--   QD.raid.verzik_p1_solve(opts) -> result, detail, S
--   opts as v1's: { max_ticks, prep, weapon } (the relay ran verzik_p1_prepare)
--
-- PREDICT and STRATEGY only. The perception is v1's, CALLED
-- (raid_solve_verzik_p1.lua): her clock from the wind-up (_vzp1_clock), the
-- pillars and their hits (_vzp1_pillars), the shield break (_vzp1_break), the
-- cover box (_vzp1_box), the weapon in hand (_vzp1_weapon_from_hand). Two
-- pieces are ported because the committed v1 cannot be called for them: the
-- cover with Hard Mode's ALTERNATING front pillars (main-tree v1
-- _vzp1_cover, uncommitted) and the rubble reader (a `local` in
-- raid_solve_verzik_hm.lua).
--
-- ===================================================================== MODES
--
--   state   when                                       wields (gear)       target              other
--   wait    no P1 body                                  -                   -                   -
--   SPEC    holding the Dawnbringer, energy >= 350      {dawn}              her, range 10        spec once worn
--   PASS    holding it, energy < 350                    {weapon} if worn    NONE                 drop it in the box
--   FETCH   my turn, it lies on the floor               -                   her (low)           pull to it; take on it
--   STRIKE  otherwise                                   -                   her, range 1         -
--   break   her shield broke                            -                   -                   the falls
--
-- Only the state wields (owner 2026-10-12: "It shouldn't be fighting between
-- what to wield, it should be handled by a state machine"). The wield is the
-- executive's held channel: intents.gear -> a `held` entry at the tick it goes
-- out, ends_attack, so the plan sees the swing it costs. PASS has no target:
-- a re-wield after a spent Dawnbringer turns a ranged swing that stood still
-- into a melee one the server walks at her (main tree, seed sk seat 2 t610).
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}

QD.VZ2P1 = {
    H = 14, BEAM = 32,                    -- a 14-tick cycle: from R+1 the plan sees the next read
    -- THE ONE CURRENCY (plan file): the pillar budget is P1's clock (~22 reads,
    -- ~310 ticks); past it a read is ~170 team hp. -> 0.25 team hp a shield hp. PROBE.
    HP_PER_BOSS_HP = 0.25,
    TENTACLE = { speed = 4, exp = 8.5 },  -- capped 10 a hitsplat (C ~tob_verzik_p1_cap), def 20
    DAWN = { speed = 4, exp = 40, spec = 110, range = 10 },   -- uncapped, never misses; special 75-150
    SPEC_COST = 350,
    FALL_WINDOW = 3,                      -- a fall reads R+3 (D+2 at the break) and lands as late as +3 more (main tree)
    COLLAPSE_RANGE = 3,
    RUBBLE_LAND = 4,                      -- the shadow drawn on the pillar hit, lands 4 later (^tob_verzik_hard_debris_land_ticks)
    STARTED_AGO = 8,                      -- v1: the fight started this long before the solve
    FETCH_PULL = 2, PASS_PULL = 0.3, FETCH_HER = 0.2,
    BACK_MARGIN = 1,                      -- tiles of slack on the way back (main tree: a pillar makes the run longer than the gap)
    BACK_FAR = 16,                        -- an envelope wider than this constrains nothing
    F_KEYS = { "boss", "initial", "after", "pillar_rows", "dawn_pack", "dawn_worn", "ground_dawn", "read", "hold",
               "team", "box", "in_box" },
}
QD.VZ2P1F = QD.VZ2P1F or {}

-- ================================================================= OBSERVE

-- v1's perception once a tick (the clock and the pillar hit counts keep state).
function QD.VZ2P1F.observe(S, F)
    local M = S.t2v
    if M.obs_tick == F.tick then
        if M.obs_F ~= F then
            for _, k in ipairs(QD.VZ2P1.F_KEYS) do F[k] = M.obs_F[k] end
        end
        return
    end
    M.obs_tick, M.obs_F = F.tick, F
    local ids = S.ids
    F.pillar_rows, F.ground_dawn = {}, {}
    for _, row in ipairs(F.npcs) do
        local id, base = row.npc_id, row.base_npc_id
        if id == ids.p1 or base == ids.p1 then F.boss = row
        elseif id == ids.initial or base == ids.initial then F.initial = row
        elseif id == ids.transition or base == ids.transition then F.after = row
        elseif id == ids.pillar then F.pillar_rows[#F.pillar_rows + 1] = row
        end
    end
    F.dawn_pack = QD.raid._tob_count(S, ids.dawn) > 0
    F.dawn_worn = QD.raid._tob_worn(S, ids.dawn)
    local orr, objs = api_drive.objs(0)
    if orr == "ok" then
        for _, o in ipairs(objs) do
            if o.obj_id == ids.dawn then F.ground_dawn[#F.ground_dawn + 1] = { x = o.x, z = o.z } end
        end
    end
    -- the Dawnbringer's turns: each time it lands on the floor (v1 loop)
    local seen = #F.ground_dawn > 0
    if seen and not S.dawn_seen then S.dawn_drops = S.dawn_drops + 1 end
    S.dawn_seen = seen
    if M.last_energy and F.energy <= M.last_energy - QD.VZ2P1.SPEC_COST + 10 then M.specs[#M.specs + 1] = F.tick end
    M.last_energy = F.energy
    local b = F.boss
    if b and b.health_ratio and b.health_scale and b.health_scale > 0 then M.frac = b.health_ratio / b.health_scale end
    QD.raid._vzp1_clock(S, F)
    local pillars = QD.raid._vzp1_pillars(S, F)
    QD.raid._vzp1_break(S, F, pillars)
    -- a shot she wound up before her shield broke still fires (v1)
    S.windup_pending = S.phase ~= "FIGHT" and S.read ~= nil and F.tick <= S.read
        and S.windups[#S.windups] ~= nil and S.read == S.windups[#S.windups] + QD.VZP1.WINDUP_TO_READ
    QD.VZ2P1F.cover(S, F, pillars)
    QD.VZ2P1F.rubble(S, F)
end

-- THE COVER, Hard Mode alternating (main-tree v1 _vzp1_cover, ported: the
-- committed one keeps the first standing pillar). After each read the team's
-- pillar is the other of the first two standing; a team pillar that could
-- fall to the read's bolt (at most 60 left) falls reading R+3.
function QD.VZ2P1F.cover(S, F, pillars)
    local V, X = QD.VZP1, QD.VZ2P1
    -- (a READ, not the guess replaced by her first wind-up: probe vz1 t158,
    -- the sync flipped the team pillar two ticks before the real read and all
    -- five took the first bolt; the guess never let anyone out anyway)
    if S.prev_read and S.read ~= S.prev_read and F.tick > S.prev_read and S.t2v.prev_synced then
        for _, p in ipairs(pillars) do
            if p.key == S.team_key and p.could_fall then
                S.falls[#S.falls + 1] = { cx = p.cx, cz = p.cz, read = S.prev_read + V.HIT_TO_FALL_READ, key = p.key }
                QD.raid._tob_trace(S, F.tick, "pillar " .. p.key .. " may fall (" .. p.hits .. " hits), read t"
                    .. (S.prev_read + V.HIT_TO_FALL_READ))
            end
        end
        -- (Hard alternates the front pillars read by read; Normal keeps its
        -- pillar while it stands, as v1's cover does)
        S.t2v.switch = QD.raid.tob_mode == "hard"
        S.t2v.reads = S.t2v.reads + 1
    end
    S.prev_read = S.read
    S.t2v.prev_synced = S.synced == true
    local keep, falling = {}, {}
    for _, f in ipairs(S.falls) do
        if f.read + X.FALL_WINDOW >= F.tick then
            keep[#keep + 1] = f
            falling[f.key] = true
        end
    end
    S.falls = keep
    local standing = {}
    for _, p in ipairs(pillars) do
        if not falling[p.key] then standing[#standing + 1] = p end
    end
    -- BY IDENTITY: after a read the first standing pillar that is not the one
    -- just used; between reads the team pillar is kept while it stands and is
    -- not falling. (An index into the standing list jumped mid-cycle when a
    -- front pillar collapsed and the list shifted: probe vz1/vz8 t250, seat 3
    -- sent across the room ten ticks before the read, a 25 and a 30.)
    local team, cur = nil, nil
    for _, p in ipairs(standing) do
        if p.key == S.team_key then cur = p end
    end
    if S.t2v.switch or cur == nil then
        for _, p in ipairs(standing) do
            if team == nil and p.key ~= S.team_key then team = p end
        end
        team = team or standing[1]
    else
        team = cur
    end
    S.t2v.switch = false
    if team and team.key ~= S.team_key then QD.raid._tob_trace(S, F.tick, "team pillar " .. team.key) end
    S.team_key = team and team.key or nil
    F.team = team
    F.box, F.in_box = nil, {}
    if team then
        -- the box's core rect (C ~tob_verzik_hiding_pillar, v1 _vzp1_box): the
        -- cover zone; its two odd tiles are left out of the zone (in_box keeps them)
        if team.west then
            F.box = { x0 = team.x - 3, z0 = team.z - 3, x1 = team.x, z1 = team.z }
        else
            F.box = { x0 = team.x + 2, z0 = team.z - 2, x1 = team.x + 4, z1 = team.z }
        end
        local b = F.boss or F.initial
        for _, t in ipairs(QD.raid._vzp1_box(team)) do
            local blocked = b ~= nil and QD.raid._tob_gap(t.x, t.z, b.x, b.z, b.size or 1) == 0
            for _, p in ipairs(pillars) do
                if QD.raid._tob_gap(t.x, t.z, p.x, p.z, p.size) == 0 then blocked = true end
            end
            if not blocked then F.in_box[t.x .. "," .. t.z] = true end
        end
    end
end

-- HARD MODE'S RUBBLE (raid_solve_verzik_hm.lua's reader is a file local, so
-- it is ported): every pillar the bolt hits draws the falling-debris
-- shadow (gargboss_debris_shadow_120) on each of three tiles; a new one (or a restarted cycle) lands
-- RUBBLE_LAND ticks on, reading the tick end before.
function QD.VZ2P1F.rubble(S, F)
    local M = S.t2v
    local r, rows = api_drive.spotanims(0)
    local now = {}
    if r == "ok" then
        for _, row in ipairs(rows) do
            if row.spotanim_id == S.ids.rubble_gfx then
                local k = row.x .. "," .. row.z
                local left = row.cycles_left or 0
                local prev = M.rubble_left[k]
                if prev == nil or left > prev then
                    M.rubble[#M.rubble + 1] = { x = row.x, z = row.z, land = F.tick + QD.VZ2P1.RUBBLE_LAND }
                    M.rubbles = M.rubbles + 1
                end
                now[k] = left
            end
        end
    end
    M.rubble_left = now
    local keep = {}
    for _, rb in ipairs(M.rubble) do if rb.land >= F.tick then keep[#keep + 1] = rb end end
    M.rubble = keep
end

-- ================================================================ STRATEGY

-- The machine's state, from what I hold and whose turn it is (main-tree v1
-- _vzp1_machine; v1 _vzp1_dawn_turn).
function QD.VZ2P1F.state(S, F)
    local holding = F.dawn_pack or F.dawn_worn
    if holding and F.energy >= QD.VZ2P1.SPEC_COST then return "SPEC" end
    if holding then return "PASS" end
    -- (fetched only with a special's energy: taken without it, it was passed
    -- straight back - 33 drops and 11 takes in one P1, the swings lost on the
    -- walks, sac 2026-10-17)
    if #F.ground_dawn > 0 and QD.raid._vzp1_dawn_turn(S) == S.role and F.energy >= QD.VZ2P1.SPEC_COST then
        return "FETCH"
    end
    return "STRIKE"
end

function QD.VZ2P1F.her(S, F, B, name, range, speed, exp)
    local b = F.boss
    return B.target{ name = name, npc = b, size = b.size or 5, path = { { x = b.x, z = b.z } },
        range = range, speed = speed, value = exp * QD.VZ2P1.HP_PER_BOSS_HP }
end

-- ===================================================================== ROOM

QD.T2.rooms.verzik_p1 = {
    init = function(S, opts)
        opts = opts or {}
        local v = QD.raid._vzp1_weapon_from_hand(QD.raid._vzp1_ids(opts.weapon))
        for _, k in ipairs({ "p1", "initial", "transition", "pillar", "windup", "death", "dawn", "weapon" }) do
            S.ids[k] = v[k]
        end
        local rr, gfx = api_drive.symbol("spotanim", "gargboss_debris_shadow_120")
        assert(rr == "ok", S.who .. ": no spotanim gargboss_debris_shadow_120")
        S.ids.rubble_gfx = gfx
        S.room = "verzik_p1"
        S.trace = S.trace or {}
        -- v1's perception state (raid_solve_verzik_p1.lua verzik_p1_solve)
        S.phase, S.hits, S.hit_cycle_seen, S.falls, S.windups = "FIGHT", {}, {}, {}, {}
        S.dawn_drops, S.dawn_seen = 0, false
        local start = api_drive.tick()
        S.read = start - (opts.started_ago or QD.VZ2P1.STARTED_AGO) + QD.VZP1.FIRST_WINDUP + QD.VZP1.WINDUP_TO_READ
        S.t2.plan_opts.h = S.t2.plan_opts.h or QD.VZ2P1.H
        S.t2.plan_opts.beam = S.t2.plan_opts.beam or QD.VZ2P1.BEAM
        S.t2v = { opts = opts, modes = {}, mode = nil, rubble = {}, rubble_left = {}, rubbles = 0, specs = {},
                  reads = 0, takes = 0, frac = nil, last_energy = nil, take_sent = -10 }
    end,

    -- the backpack on show for the held ops (v1); the fight is already started
    enter = function(S)
        api_drive.tab(S.ids.inv_tab)
        S.tab = S.ids.inv_tab
    end,

    predict = function(S, F, B)
        QD.VZ2P1F.observe(S, F)
        local X = QD.VZ2P1
        local now, hz = F.tick, F.tick + B.h
        local b = F.boss or F.initial
        if b then
            B.hit{ name = "body", x = b.x, z = b.z, size = b.size or 5, t0 = now + 1, t1 = hz, forbid = true }
        end
        -- THE BOLT: in the team box at the read (a guess before her first
        -- wind-up never lets anyone out once its tick has come: v1 F.hold)
        if F.box and (S.phase == "FIGHT" or S.windup_pending) then
            local t0, t1 = nil, nil
            if F.hold then t0, t1 = now + 1, hz
            elseif F.read and F.read >= now + 1 and F.read <= hz then t0, t1 = F.read, F.read end
            if t0 then
                B.zone{ name = "cover", x0 = F.box.x0, z0 = F.box.z0, x1 = F.box.x1, z1 = F.box.z1, lo = 0, hi = 0,
                    t0 = t0, t1 = t1, tier = "lethal", require = true, cost = 0 }
            end
            -- THE POINT OF NO RETURN (v1's cover term): before the read, no
            -- further from the box than the run back covers, less a margin.
            -- Without it the beam, ranked by value until the read's own tick,
            -- dropped every node on the way back (probe vz1: seats stood at
            -- her face through four reads, cover k1..k14 every tick).
            if not F.hold and t0 and t0 > now + 1 then
                local step = B.spec.run and 2 or 1
                for t = now + 1, t0 - 1 do
                    local hi = step * (t0 - t) - X.BACK_MARGIN
                    if hi < X.BACK_FAR then
                        B.zone{ name = "cover-back", x0 = F.box.x0, z0 = F.box.z0, x1 = F.box.x1, z1 = F.box.z1, lo = 0,
                            hi = math.max(hi, 0), t0 = t, t1 = t, tier = "lethal", require = true, cost = 0 }
                    end
                end
            end
        end
        -- PILLAR FALLS (a read's could-fall pillar, every pillar at the break)
        for _, f in ipairs(S.falls) do
            local r = X.COLLAPSE_RANGE
            B.hit{ name = "fall", x0 = f.cx - r, z0 = f.cz - r, x1 = f.cx + r, z1 = f.cz + r,
                t0 = f.read, t1 = f.read + X.FALL_WINDOW, dmg_exp = 48, dmg_max = 65, stun = 5, forbid = true }
        end
        -- HARD MODE RUBBLE: [land-2, land-1]
        for _, rb in ipairs(S.t2v.rubble) do
            B.hit{ name = "rubble", x = rb.x, z = rb.z, size = 1, t0 = rb.land - 2, t1 = rb.land - 1,
                dmg_exp = 18, dmg_max = 35, stun = 3, forbid = true }
        end
    end,

    strategy = function(S, F, B)
        QD.VZ2P1F.observe(S, F)
        local X, ids, M = QD.VZ2P1, S.ids, S.t2v
        local I = {}
        local mode
        if F.boss == nil then
            mode = (S.phase == "FIGHT") and "wait" or "break"
        elseif S.phase ~= "FIGHT" then
            mode = "break"
        else
            mode = QD.VZ2P1F.state(S, F)
            if mode == "SPEC" then
                if not F.dawn_worn then I.gear = { ids.dawn } end
                I.spec = F.dawn_worn and F.energy >= X.SPEC_COST
                QD.VZ2P1F.her(S, F, B, "her-dawn", X.DAWN.range, X.DAWN.speed, X.DAWN.spec)
            elseif mode == "PASS" then
                if F.dawn_worn then
                    I.gear = { ids.weapon }
                elseif F.in_box[F.me.x .. "," .. F.me.z] then
                    I.drop = ids.dawn
                end
                if F.box then
                    B.pull{ name = "pass-box", x = F.box.x0, z = F.box.z0, size = 1, weight = X.PASS_PULL,
                        t0 = F.tick + 1, t1 = F.tick + B.h }
                end
            elseif mode == "FETCH" then
                local best, bd = nil, nil
                for _, t in ipairs(F.ground_dawn) do
                    local d = QD.raid._tob_cheb(t.x, t.z, F.me.x, F.me.z)
                    if bd == nil or d < bd or (d == bd and (t.x < best.x or (t.x == best.x and t.z < best.z))) then
                        best, bd = t, d
                    end
                end
                B.pull{ name = "fetch", x = best.x, z = best.z, size = 1, weight = X.FETCH_PULL,
                    t0 = F.tick + 1, t1 = F.tick + B.h }
                -- THE TAKE (GAP: the executive has no obj channel): standing on
                -- its tile the plan holds, and the take is one packet
                if bd == 0 and F.tick - M.take_sent >= 2 then
                    local r = api_drive.world_op("obj", ids.dawn, 3)
                    M.take_sent = F.tick
                    M.takes = M.takes + 1
                    QD.raid._tob_trace(S, F.tick, "take the Dawnbringer: " .. tostring(r))
                end
                QD.VZ2P1F.her(S, F, B, "her", 1, X.TENTACLE.speed, X.TENTACLE.exp * X.FETCH_HER)
            else
                QD.VZ2P1F.her(S, F, B, "her", 1, X.TENTACLE.speed, X.TENTACLE.exp)
            end
        end
        if mode ~= M.mode then
            QD.raid._tob_trace(S, F.tick, "mode " .. mode)
            M.mode = mode
        end
        M.modes[mode] = (M.modes[mode] or 0) + 1
        I.mode = mode
        return I
    end,

    -- her shield broke and every fall has read (main tree: D+2 .. D+5)
    done = function(S, F)
        QD.VZ2P1F.observe(S, F)
        if S.phase ~= "FIGHT" and S.break_read and F.tick > S.break_read + QD.VZ2P1.FALL_WINDOW + 1 then
            return "ok"
        end
        return nil
    end,

    summary = function(S)
        local M = S.t2v
        local modes = {}
        for k, n in pairs(M.modes) do modes[#modes + 1] = k .. "=" .. n end
        table.sort(modes)
        return string.format("verzik_p1 v2: %s; modes %s; reads %d, wind-ups %d; her frac %s; rubble %d; specials %s; dawn drops %d takes %d",
            S.phase, table.concat(modes, " "), M.reads, #S.windups, M.frac and string.format("%.3f", M.frac) or "-",
            M.rubbles, table.concat(M.specs, ","), S.dawn_drops, M.takes)
    end,
}

function QD.raid.verzik_p1_solve(opts)
    -- (Normal has no rubble: the reader sees no debris shadow and adds none)
    assert(QD.raid.tob_mode == "hard" or QD.raid.tob_mode == "normal",
        "verzik_p1_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.T2.solve("verzik_p1", opts or {})
end
