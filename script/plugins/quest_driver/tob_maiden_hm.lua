-- quest-driver / tob_maiden_hm: THE MAIDEN OF SUGADINTI, HARD MODE, five
-- raiders in the Learner/Void kit.
--
--   t.raid.maiden_hm_solve(opts) -> result, detail, record
--
-- The Normal solver's machinery (tob_maiden.lua: the measure, her clock, the
-- pools, the trails, the crab tracker, the freezer's barrage slots, the
-- planner spec, the entry) with Hard Mode's rules on top
-- (wiki Theatre_of_Blood/Hard_Mode :313-317):
--
--   crabs         ALL TEN at every threshold (blert: 10 at 2-5 raiders), 100
--                 hitpoints each at five. Every one is frozen one to five steps
--                 from its leak, and a freeze leaves five ticks of immunity
--                 behind it (wiki_Freeze :7, the engine's NPC_FREEZE), so a
--                 crab that thaws walks into her before a second barrage can
--                 hold it: each has to DIE inside its first freeze.
--   blood spawns  invulnerable; a barrage still freezes one, and a frozen
--                 spawn lays no new trail.
--   trails        never fade (the shared spec forbids a trail for as long as
--                 its loc is seen).
--   focus         from her third storm in a row on one raider they hit harder
--                 (tob_maiden.rs2 ^tob_maiden_hard_focus_hits): the tank turns
--                 over every TANK_STORMS.
--
-- THE ROLES ("Freezers often follow the convention of N1234, S12", :315 -
-- here each freezer takes all of its side):
--   seat 1  the NORTH freezer: barrage slots N1 +1, N2 +6, N3 +11, the 4s +16
--           from x=41, then its side's first thaw (a barrage on a clump, the
--           blowpipe on a lone crab), a thawed crab out of its immunity, and
--           between waves the blood spawns on its half, else her (blowpipe).
--   seat 2  the SOUTH freezer, the same for S1..S4 from x=40.
--   seats 3-5  DPS: the hammer opener, the tentacle on her between waves (one
--           tanks her east face, turned over every TANK_STORMS); in a wave the
--           blowpipe on the crabs the freezers are not on, shared out by thaw.
-- ==========================================================================

QD.MAIDEN_HM = {
    FREEZE = {
        [1] = { { at = 1, last = 5, aim = { "N1" } }, { at = 6, last = 8, aim = { "N2" } },
                { at = 11, last = 12, aim = { "N3" } }, { at = 16, last = 16, aim = { "N4o", "N4i" } } },
        [2] = { { at = 1, last = 5, aim = { "S1" } }, { at = 6, last = 8, aim = { "S2" } },
                { at = 11, last = 12, aim = { "S3" } }, { at = 16, last = 16, aim = { "S4o", "S4i" } } },
    },
    -- the freezers' wave stations (both reach every spawn point within 10)
    -- and their stations between waves (gap 5: the blowpipe on her)
    FREEZER_STATION = { [1] = { x = 41, z = 30 }, [2] = { x = 40, z = 30 } },
    STATION = { [1] = { x = 36, z = 31 }, [2] = { x = 36, z = 29 } },
    -- the DPS: the tank on her east face (Chebyshev 3 from her centre, the
    -- storm's pick), the other two on the south and west faces (4); where
    -- they wait for a wave (her north-east and south-east corners)
    DPS = { 3, 4, 5 },
    TANK_TILE = { x = 32, z = 31 }, OFF_TILES = { { x = 30, z = 27 }, { x = 25, z = 30 } },
    TANK_STORMS = 2,
    TANK_BREW = 75,
    WAVE_TILE = { [3] = { x = 32, z = 33 }, [4] = { x = 31, z = 34 }, [5] = { x = 32, z = 28 } },
    IMMUNE = 5,
    -- THE FREEZERS' PREWAVE, nearer the threshold than Normal's 100 permille:
    -- five raiders' damage is slow on her through her heals, and a freezer
    -- holding its cast set at the station for 124 ticks froze no spawn while
    -- their trails covered the floor (m5a sf: 1669 trail tiles, three seats out
    -- of their planner budget). A spawn is frozen first until SLUG_PREWAVE.
    PRE_WAVE = 40, SLUG_PREWAVE = 20,
}

local function cheb(ax, az, bx, bz) return QD.raid._tob_cheb(ax, az, bx, bz) end

-- ==================================================================== MEASURE

-- Hard Mode's bookkeeping on top of the shared measure: each crab's thaw and
-- its immunity after it; each blood spawn's freeze.
function QD.raid._maiden_hm_marks(S, F)
    local V = QD.MAIDEN
    for _, c in ipairs(F.crabs) do
        local rec = S.crab_rec[c.slot]
        if rec.frozen_until ~= nil and rec.frozen_until > (rec.thaw_seen or -1) then rec.thaw_seen = rec.frozen_until end
        c.thaw = rec.frozen_until
        c.immune = not c.frozen and rec.thaw_seen ~= nil and F.tick < rec.thaw_seen + QD.MAIDEN_HM.IMMUNE
    end
    for _, sl in ipairs(F.slugs) do
        local m = S.slug_freeze[sl.slot] or {}
        if sl.spotanim_sent_id == S.ids.freeze_gfx and sl.spotanim_tick ~= nil and sl.spotanim_tick ~= m.gfx then
            m.gfx = sl.spotanim_tick
            if m.till == nil or m.till < F.tick then m.till = F.tick + V.FREEZE_TICKS end
        end
        if (sl.dx ~= 0 or sl.dz ~= 0) and m.till and m.till > F.tick and (m.gfx == nil or m.gfx < F.tick - 1) then
            m.till = nil
        end
        sl.frozen = m.till ~= nil and m.till > F.tick
        S.slug_freeze[sl.slot] = m
    end
end

-- The wave, on the tick its crabs appear: every point, every wave; my side's
-- barrage slots (a DPS seat has none).
function QD.raid._maiden_hm_wave(S, F)
    local n = 0
    for _, c in ipairs(F.crabs) do
        if c.born == F.tick then
            assert(c.point, "maiden_hm_solve: a crab spawned on no spawn point, at "
                .. (c.x - S.base.x) .. "," .. (c.z - S.base.z))
            n = n + 1
        end
    end
    if n == 0 then return end
    S.waves = S.waves + 1
    S.wave = { t0 = F.tick, key = "hard " .. n, plan = { freeze = QD.MAIDEN_HM.FREEZE[S.role] or {}, dps = {} },
               next = 1, n = S.waves }
    S.wave_log[#S.wave_log + 1] = "w" .. S.waves .. " t" .. F.tick .. " " .. n .. " crabs"
    QD.raid._tob_trace(S, F.tick, "wave " .. S.waves .. " (" .. n .. " crabs)")
end

-- ===================================================================== ROLES

-- Which side of her a tile is on: seat 1 the north (z at or above her centre
-- row), seat 2 the south.
local function side_of(S, z)
    local V = QD.MAIDEN
    return (z >= S.base.z + V.BODY.z + V.SIZE // 2) and 1 or 2
end

-- The crab freezer `role` works on after its slots: its side's first thaw.
function QD.raid._maiden_hm_side_first(S, F, role)
    local first = nil
    for _, c in ipairs(F.crabs) do
        if side_of(S, c.z) == role
            and (first == nil or (c.thaw or 0) < (first.thaw or 0) or ((c.thaw or 0) == (first.thaw or 0) and c.slot < first.slot)) then
            first = c
        end
    end
    return first
end

local function crowd(F, c)
    local n = 0
    for _, o in ipairs(F.crabs) do
        if cheb(o.x, o.z, c.x, c.z) <= 1 then n = n + 1 end
    end
    return n
end

-- THE FREEZER after its slots: a thawed crab out of its immunity (the freeze
-- holds it), then its side's first thaw - a barrage on a clump (the damage
-- lands on all of them), the blowpipe on a lone crab - then the other side's.
-- Nil with no crab up.
function QD.raid._maiden_hm_freezer(S, F)
    local R = QD.raid._maiden_slots(S, F)
    if R then return R end
    local hot = nil
    for _, c in ipairs(F.crabs) do
        if not c.frozen and not c.immune and (hot == nil or c.eta < hot.eta or (c.eta == hot.eta and c.slot < hot.slot)) then hot = c end
    end
    if hot then return { target = hot, kind = "clump" } end
    local first = QD.raid._maiden_hm_side_first(S, F, S.role) or QD.raid._maiden_hm_side_first(S, F, 3 - S.role)
    if first == nil then return nil end
    if crowd(F, first) >= 2 then return { target = first, kind = "clump" } end
    return { target = first, kind = "pipe" }
end

-- ===================================================================== STEP

-- ===================================================================== LOOP
