-- quest-driver / raid_solve_tob_maiden: THE MAIDEN OF SUGADINTI, Normal trio.
--
--   t.raid.maiden_solve(opts) -> result, detail, record
--
-- Called by every seat at the room's entry (the corridor side of the
-- barrier). It starts the room (the leader) or crosses once the leader is in
-- (a member), and plays the fight on the shared loop (raid_solve_tob.lua):
-- MEASURE -> CLOCK -> CONTEXT -> PLAN -> ORDER + EMIT, one decision a server
-- tick. It returns "ok" at her death. The plan is ROOM_SOLVERS.md 4.1.
--
-- THE SPEC is docs/minigames/theater_of_blood/solver_specs/maiden.md. Tiles
-- are local to the room's origin O = her south-west tile - (26,28). Tick
-- mapping (lesson 2): her decision on T reads where raiders stood at the end
-- of T-1; my click after seeing d moves me during d+1, so plan step k is my
-- tile at the end of d+k.
--
--   her clock     an attack every 10 from A1 = start + 9; a blackstorm or a
--                 blood throw (none on the two attacks after a throw).
--   blackstorm    at the raider nearest her centre (29,31), ties to the
--                 leader; 0..36 (+3.5 a leak), Protect from Magic read at the
--                 throw halves it -> Protect from Magic on every seat.
--   blood throw   a pool on each raider's end-of-B-1 tile, two extras round
--                 the furthest a tick later; a pool lands floor((50+15d)/30)
--                 after B (extras +25 cycles) and hurts 11 ticks, read at the
--                 end of the tick before -> forbid it from land-1 to land+9.
--   blood spawns  a trail (loc tob_maiden_blood) on their tile every tick, 30
--                 ticks a trail -> forbid the trails; never end a tick where a
--                 spawn will stand; kill them.
--   crabs         six at each of 70/50/30%, still on the spawn tick, then a
--                 tile a tick toward her south-east tile; a crab whose SW tile
--                 enters x24..32 z26..34 LEAKS (heals her twice its hp).
--   freeze        Ice Barrage freezes a crab and its 3x3 for 32 ticks; the
--                 chance is on the caster's magic accuracy roll (Void and
--                 Augury count): the void cast set (+52 with the defender and
--                 boots off) freezes every time.
--
-- THE ROLES (wiki_Theatre_of_Blood_Strategies.wikitext :597-653): seat 1, the
-- leader, is the FREEZER (it leads the orb order); seats 2 and 3 are DPS.
-- The DPS MELEE her in the Learner kit's own worn set, void melee with the
-- abyssal tentacle and Piety (the owner, 2026-10-09: "tentacle whip melee void
-- for the dps roles"; the guide's :646 reserves melee for a scythe -- the
-- owner's ruling stands), opening with a dragon warhammer special (:617-640).
-- The freezer shoots her from range with the void ranged switch and the
-- blowpipe (:646 "the freezer(s) should range Maiden from a distance") and
-- freezes the crabs with Ice Barrage in the void mage set and Augury (:248-249,
-- :651-653); the DPS kill the strays first, then the frozen clump, then her
-- (:651).
-- ==========================================================================

QD.MAIDEN = {
    BODY = { x = 26, z = 28 }, SIZE = 6,
    ARENA = { x0 = 23, z0 = 18, x1 = 51, z1 = 42 },
    LEAK = { x0 = 24, z0 = 26, x1 = 32, z1 = 34 },     -- a crab's SW tile inside: it leaks
    POOL_LIFE = 10,                                     -- land .. land+10, read the tick before
    TRAIL_LIFE = 30,
    PIPE_RANGE = 5,                                     -- toxic_blowpipe weapon_attackrange
    SPELL_RANGE = 10,
    CAST_EVERY = 5,                                     -- Ice Barrage's attack speed
    FREEZE_TICKS = 32,
    SAVE_ETA = 2,                                       -- a crab further than this from its leak can still be frozen
    -- THE STATIONS. The storm goes to whoever is nearest her centre (29,31) by
    -- Chebyshev. Her 6x6 body is x26..31 z28..33, so every melee tile on her
    -- east face (x32) and north face (z34) is 3 from the centre and every one
    -- on her south face (z27) and west face (x25) is 4: the TANK melees from
    -- the east face, the other DPS from the south face (near the crabs' arrival
    -- at her south-east tile), and the two swap every TANK_STORMS storms
    -- (every seat counts the same storms). The freezer shoots from gap 5
    -- (Chebyshev 7: never the tank).
    STATION = { [1] = { x = 36, z = 31 } },
    TANK_TILE = { x = 32, z = 31 }, OFF_TILE = { x = 30, z = 27 },
    TANK_STORMS = 6,
    -- THE FREEZER'S WAVE: within PRE_WAVE permille above a threshold it puts the
    -- cast set on and stands where both 1s are in spell range (the guide's
    -- solo freezer "hovers S1's spawn"), so the first barrage goes out at +1.
    THRESHOLDS = { 700, 500, 300 }, PRE_WAVE = 40,
    PRE_POS = { x = 38, z = 30 },
    OPENER_TICKS = 30,                                  -- the hammer opener's cap
    H = 8, BEAM = 32,
    POOL_COST = 12, TRAIL_COST = 9, SLUG_COST = 9,
    SLUG_NEAR = 10,
    WEAR_SHIELD = 5, WEAR_FEET = 10,
}

local function gap(x, z, fx, fz, n) return QD.raid._tob_gap(x, z, fx, fz, n) end
local function cheb(ax, az, bx, bz) return QD.raid._tob_cheb(ax, az, bx, bz) end

function QD.raid._maiden_ids()
    local ids = QD.raid._tob_common_ids("maiden_solve")
    local room = QD.raid._tob_symbols("maiden_solve", {
        { "m100", "npc", "tob_maiden_100" }, { "m70", "npc", "tob_maiden_70" },
        { "m50", "npc", "tob_maiden_50" }, { "m30", "npc", "tob_maiden_30" },
        { "dying_a", "npc", "tob_maiden_dying_a" }, { "dying_b", "npc", "tob_maiden_dying_b" },
        { "crab", "npc", "maiden_elemental" }, { "slug", "npc", "maiden_blood_slug" },
        { "seq_storm", "seq", "maiden_attack_special" }, { "seq_blood", "seq", "maiden_attack_blood" },
        { "blood_proj", "spotanim", "maiden_blood_proj" },
        { "freeze_gfx", "spotanim", "ice_barrage_impact" },
        { "trail", "loc", "tob_maiden_blood" },
        { "archer_helm", "obj", "game_pest_archer_helm" }, { "pipe", "obj", "toxic_blowpipe_loaded" },
        { "anguish", "obj", "zenyte_necklace_enchanted" }, { "assembler", "obj", "avas_assembler" },
        { "mage_helm", "obj", "game_pest_mage_helm" }, { "trident", "obj", "toxic_tots_charged" },
        { "occult", "obj", "occult_necklace" }, { "imbued_cape", "obj", "ma2_saradomin_cape" },
        { "defender", "obj", "dragon_parryingdagger" }, { "boots", "obj", "dragon_boots" },
        { "hammer", "obj", "dragon_warhammer" },
        { "melee_helm", "obj", "game_pest_melee_helm" }, { "tentacle", "obj", "abyssal_tentacle" },
        { "torture", "obj", "zenyte_amulet_enchanted" }, { "fire_cape", "obj", "tzhaar_cape_fire" },
        { "barrage", "component", "magic_spellbook:ice_barrage" },
        { "hud", "varbit", "varb6448_tob_client_waveprogress_val" },
    })
    for k, v in pairs(room) do ids[k] = v end
    ids.forms = { [ids.m100] = true, [ids.m70] = true, [ids.m50] = true, [ids.m30] = true }
    ids.range_set = { ids.archer_helm, ids.pipe, ids.anguish, ids.assembler, ids.boots }
    ids.melee_set = { ids.melee_helm, ids.tentacle, ids.torture, ids.fire_cape, ids.defender }
    ids.cast_set = { ids.mage_helm, ids.trident, ids.occult, ids.imbued_cape }
    return ids
end

-- ==================================================================== MEASURE

function QD.raid._maiden_measure(S, F)
    local ids = S.ids
    F.crabs, F.slugs = {}, {}
    for _, row in ipairs(F.npcs) do
        local id = row.npc_id
        if ids.forms[id] then F.boss = row
        elseif id == ids.dying_a or id == ids.dying_b then F.dying = row
        elseif id == ids.crab and row.health_ratio ~= 0 then F.crabs[#F.crabs + 1] = row
        elseif id == ids.slug and row.health_ratio ~= 0 then F.slugs[#F.slugs + 1] = row
        end
    end
    local _, permille = api_drive.varbit(ids.hud)
    F.permille = permille or 1000
    if F.boss then
        local form = F.boss.npc_id
        F.next_threshold = (form == ids.m100 and 700) or (form == ids.m70 and 500) or (form == ids.m50 and 300) or nil
    end
    if F.boss and S.base == nil then
        S.base = { x = F.boss.x - QD.MAIDEN.BODY.x, z = F.boss.z - QD.MAIDEN.BODY.z }
        QD.raid._tob_trace(S, F.tick, "room origin " .. S.base.x .. "," .. S.base.z)
    end
end

function QD.raid._maiden_abs(S, t)
    return { x = S.base.x + t.x, z = S.base.z + t.z }
end

-- ====================================================================== CLOCK

-- Her attacks, from her animation: a blood throw's tick B counts every pool.
function QD.raid._maiden_clock(S, F)
    local b = F.boss
    if b and b.seq_tick ~= nil and b.seq_tick ~= S.last_seq_tick then
        S.last_seq_tick = b.seq_tick
        if b.seq_id == S.ids.seq_blood then
            S.blood_tick = F.tick
            S.throws = S.throws + 1
        elseif b.seq_id == S.ids.seq_storm then
            S.storms = S.storms + 1
        end
    end
end

-- The pools in flight: each blood projectile names its tile on the throw
-- tick. Keyed by tile and throw tick (lesson 20).
function QD.raid._maiden_pools(S, F)
    local V = QD.MAIDEN
    local r, projs = api_drive.projectiles(0)
    if r == "ok" then
        local bx, bz = S.base.x + V.BODY.x, S.base.z + V.BODY.z
        for _, p in ipairs(projs) do
            if p.spotanim_id == S.ids.blood_proj then
                local B = (S.blood_tick and F.tick - S.blood_tick <= 3) and S.blood_tick or F.tick
                local key = p.dst_x .. "," .. p.dst_z .. "@" .. B
                if not S.pool_seen[key] then
                    S.pool_seen[key] = true
                    local d = gap(p.dst_x, p.dst_z, bx, bz, V.SIZE)
                    -- the plain pool's flight and the extra's: both windows
                    local land = B + (50 + 15 * d) // 30
                    local land_extra = B + (75 + 15 * d) // 30
                    S.pools[#S.pools + 1] = { x = p.dst_x, z = p.dst_z, t0 = land - 1, t1 = land_extra + V.POOL_LIFE - 1 }
                    S.pool_probe[#S.pool_probe + 1] = string.format("B%d %d,%d d%d land%d", B, p.dst_x, p.dst_z, d, land)
                end
            end
        end
    end
    local keep = {}
    for _, pl in ipairs(S.pools) do if pl.t1 >= F.tick then keep[#keep + 1] = pl end end
    S.pools = keep
end

-- The trails (loc rows, by tile and first sight) and each spawn's last step.
function QD.raid._maiden_trails(S, F)
    local V = QD.MAIDEN
    local r, rows = api_drive.locs(2 * V.H + 2)
    local here = {}
    if r == "ok" then
        for _, row in ipairs(rows) do
            if row.loc_id == S.ids.trail or row.resolved_loc_id == S.ids.trail then
                local k = row.x .. "," .. row.z
                here[k] = true
                if S.trails[k] == nil then S.trails[k] = { x = row.x, z = row.z, seen = F.tick } end
            end
        end
    end
    for k in pairs(S.trails) do
        if not here[k] then S.trails[k] = nil end
    end
    for _, sl in ipairs(F.slugs) do
        local prev = S.slug_last[sl.slot]
        sl.dx, sl.dz = 0, 0
        if prev then sl.dx, sl.dz = sl.x - prev.x, sl.z - prev.z end
        -- its next tile along its last step: an attack order on a spawn is
        -- kept only while that tile is in reach (lesson 47; seed m5s/so: the
        -- freezer followed one onto its trails)
        sl.nx, sl.nz = sl.x + sl.dx, sl.z + sl.dz
        S.slug_last[sl.slot] = { x = sl.x, z = sl.z }
    end
end

-- The crabs: each one's ticks to its leak, its next tile, and whether it is
-- frozen. Frozen is seen as the barrage's impact graphic on it (a new
-- spotanim tick) and holds 32 ticks from there; a crab that steps is not
-- frozen whatever was seen.
function QD.raid._maiden_crabs(S, F)
    local V = QD.MAIDEN
    local L = V.LEAK
    local x0, z0, x1, z1 = S.base.x + L.x0, S.base.z + L.z0, S.base.x + L.x1, S.base.z + L.z1
    local tx, tz = S.base.x + V.BODY.x + V.SIZE - 1, S.base.z + V.BODY.z    -- her south-east tile
    for _, c in ipairs(F.crabs) do
        local rec = S.crab_rec[c.slot]
        if rec == nil then
            rec = { born = F.tick, x = c.x, z = c.z }
            S.crab_rec[c.slot] = rec
            S.crab_probe[#S.crab_probe + 1] = string.format("slot%d born t%d at %d,%d", c.slot, F.tick, c.x - S.base.x, c.z - S.base.z)
        end
        if c.spotanim_id == S.ids.freeze_gfx and c.spotanim_tick ~= nil and c.spotanim_tick ~= rec.gfx_tick then
            rec.gfx_tick = c.spotanim_tick
            if rec.frozen_until == nil or rec.frozen_until < F.tick then rec.frozen_until = F.tick + V.FREEZE_TICKS end
        end
        if (rec.x ~= c.x or rec.z ~= c.z) and rec.frozen_until and rec.frozen_until > F.tick then
            rec.frozen_until = nil   -- it stepped: not frozen
        end
        rec.x, rec.z = c.x, c.z
        c.frozen = rec.frozen_until ~= nil and rec.frozen_until > F.tick
        c.eta = math.max(math.max(x0 - c.x, c.x - x1, 0), math.max(z0 - c.z, c.z - z1, 0))
        if c.frozen then
            c.nx, c.nz = c.x, c.z
        elseif F.tick > rec.born then
            -- one greedy step toward her south-east tile, diagonal first
            c.nx = c.x + ((tx > c.x) and 1 or ((tx < c.x) and -1 or 0))
            c.nz = c.z + ((tz > c.z) and 1 or ((tz < c.z) and -1 or 0))
        else
            c.nx, c.nz = c.x, c.z
        end
    end
end

-- ==================================================================== TARGET

local function by_eta(a, b) return a.eta < b.eta or (a.eta == b.eta and a.slot < b.slot) end

-- The freezer's primary (every seat computes it, so the DPS leave it): the
-- unfrozen crab that can still be saved (ticks to its leak >= SAVE_ETA: a
-- barrage decided now lands next tick, after its step) with the fewest ticks
-- left, ties to the most unfrozen crabs in its 3x3 (the clump), then slot.
-- With none, the biggest frozen clump, barraged for damage.
function QD.raid._maiden_freeze_target(S, F)
    local best, best_eta, best_n = nil, 999, -1
    for _, c in ipairs(F.crabs) do
        if not c.frozen and c.eta >= QD.MAIDEN.SAVE_ETA then
            local n = 0
            for _, o in ipairs(F.crabs) do
                if not o.frozen and cheb(o.x, o.z, c.x, c.z) <= 1 then n = n + 1 end
            end
            if c.eta < best_eta or (c.eta == best_eta and (n > best_n or (n == best_n and c.slot < best.slot))) then
                best, best_eta, best_n = c, c.eta, n
            end
        end
    end
    if best then return best, "freeze" end
    local n_best, pick = -1, nil
    for _, c in ipairs(F.crabs) do
        if c.frozen then
            local n = 0
            for _, o in ipairs(F.crabs) do if cheb(o.x, o.z, c.x, c.z) <= 1 then n = n + 1 end end
            if n > n_best or (n == n_best and c.slot < pick.slot) then n_best, pick = n, c end
        end
    end
    if pick then return pick, "clump" end
    return nil, nil
end

-- The DPS target: both DPS on the most urgent crab the freezer is not
-- covering (focus fire: one raider did not kill a 75-hitpoint crab in the ten
-- ticks a 2 takes to leak, seed m2), then the frozen crabs by ticks to leak,
-- then a spawn within reach, then her.
function QD.raid._maiden_dps_target(S, F)
    local covered = QD.raid._maiden_freeze_target(S, F)
    local loose, frozen = {}, {}
    for _, c in ipairs(F.crabs) do
        if c.frozen then frozen[#frozen + 1] = c
        elseif c ~= covered then loose[#loose + 1] = c end
    end
    table.sort(loose, by_eta)
    table.sort(frozen, by_eta)
    if #loose > 0 then return loose[1], "crab" end
    if #frozen > 0 then return frozen[1], "frozen" end
    -- the blood spawns are the freezer's (in blowpipe reach): a melee DPS
    -- chasing a spawn's random walk spent 150 ticks off her (seed m3)
    return F.boss, "her"
end

-- My station: the freezer's own; a DPS on the tank tile on its turn to tank,
-- else on the off tile.
function QD.raid._maiden_station(S)
    local V = QD.MAIDEN
    if S.role == 1 then return V.STATION[1] end
    local tank = 2 + ((S.storms // V.TANK_STORMS) % 2)
    if tank == S.role then return V.TANK_TILE end
    return V.OFF_TILE
end

-- ================================================================= CONTEXT

function QD.raid._maiden_spec(S, F, target, range, station)
    local V = QD.MAIDEN
    local A = V.ARENA
    local spec, names, add = QD.raid._tob_spec(S, F, {
        h = V.H, beam = V.BEAM,
        edge = { x0 = S.base.x + A.x0, z0 = S.base.z + A.z0, x1 = S.base.x + A.x1, z1 = S.base.z + A.z1,
                 margin = 1, weight = 0.3 },
    })
    local horizon = F.tick + V.H
    add.zone("body", { x = S.base.x + V.BODY.x, z = S.base.z + V.BODY.z, size = V.SIZE, lo = 0, hi = 0, tier = "lethal" })
    for _, pl in ipairs(S.pools) do
        if pl.t0 <= horizon then
            add.forbid("pool", { x = pl.x, z = pl.z, t0 = pl.t0, t1 = pl.t1, tier = "damage", cost = V.POOL_COST })
        end
    end
    local trails = {}
    for _, tr in pairs(S.trails) do
        if cheb(tr.x, tr.z, F.me.x, F.me.z) <= 2 * V.H + 1 then trails[#trails + 1] = tr end
    end
    table.sort(trails, function(a, b)
        return cheb(a.x, a.z, F.me.x, F.me.z) < cheb(b.x, b.z, F.me.x, F.me.z)
    end)
    for _, tr in ipairs(trails) do
        -- a trail laid on L still hurt on L+30 (seed m5s/sg: laid t180, a hit
        -- on t210), so the forbid runs to the end of L+30 at least, and on
        -- while the loc is still there
        add.forbid("trail", { x = tr.x, z = tr.z, t0 = tr.seen - 1,
            t1 = math.max(tr.seen + V.TRAIL_LIFE + 1, F.tick + 1), tier = "damage", cost = V.TRAIL_COST })
    end
    for _, sl in ipairs(F.slugs) do
        if cheb(sl.x, sl.z, F.me.x, F.me.z) <= V.SLUG_NEAR then
            -- a spawn steps a tile a tick toward a destination nobody sees:
            -- its next tile is in its 3x3, the one after in its 5x5; a raider
            -- ending a tick there takes the trail it lays next tick
            -- its tile NOW is a trail next tick, certainly: forbidden for a
            -- trail's life (seeds m6s sf/sg/so: the freezer stepped onto the
            -- tile a spawn had just left)
            add.forbid("slug-tile", { x = sl.x, z = sl.z, t0 = F.tick + 1, t1 = F.tick + V.TRAIL_LIFE + 1,
                tier = "damage", cost = V.SLUG_COST })
            add.zone("slug", { x = sl.x, z = sl.z, size = 1, lo = 0, hi = 1,
                t0 = F.tick + 1, t1 = F.tick + 1, tier = "damage", cost = 3 })
            add.zone("slug", { x = sl.x, z = sl.z, size = 1, lo = 0, hi = 2,
                t0 = F.tick + 2, t1 = F.tick + 2, tier = "damage", cost = 3 })
        end
    end
    if target then
        -- a walking crab is judged where it will be next tick (lesson 47)
        local t = { x = target.nx or target.x, z = target.nz or target.z, size = target.size }
        add.reach("reach", t, range)
    end
    if station then
        local st = QD.raid._maiden_abs(S, station)
        add.pull({ x = st.x, z = st.z, size = 1, weight = 0.3, t0 = F.tick, t1 = horizon })
    end
    return spec, names
end

-- ======================================================================= GEAR

-- The objs of `set` not worn now.
function QD.raid._maiden_missing(S, set)
    local out = {}
    for _, obj in ipairs(set) do
        if not QD.raid._tob_worn(S, obj) then out[#out + 1] = obj end
    end
    return out
end

-- ===================================================================== STEP

function QD.raid._maiden_step(S, F)
    local V, ids = QD.MAIDEN, S.ids
    QD.raid._maiden_measure(S, F)
    if F.dying or (S.seen_boss and F.boss == nil) then
        QD.raid._tob_trace(S, F.tick, "her death")
        return "ok"
    end
    if F.boss == nil or S.base == nil then
        QD.raid._tob_emit(S, F, nil, {})
        return nil
    end
    if not S.seen_boss then
        S.seen_boss = true
        S.fight_start = F.tick
    end
    QD.raid._maiden_clock(S, F)
    QD.raid._maiden_pools(S, F)
    QD.raid._maiden_trails(S, F)
    QD.raid._maiden_crabs(S, F)
    local intent = {}
    local freezer = (S.role == 1)
    local target, kind, range = nil, nil, V.PIPE_RANGE
    -- THE HAMMER OPENER (the DPS): the warhammer in hand and the special
    -- armed until a special has gone (energy fell) or the opener's cap
    if not freezer and S.opener ~= "done" then
        if S.opener == nil then
            S.opener = "hammer"
            S.opener_energy = F.energy
        end
        -- both specials (50% each) or the cap
        if F.energy < 500 or F.tick - S.fight_start > V.OPENER_TICKS then
            S.opener = "done"
            QD.raid._tob_trace(S, F.tick, "hammer opener done (energy " .. F.energy .. ")")
        end
    end
    local want_set, boost
    local want_off = 0
    local station = nil
    if not freezer and S.opener == "hammer" then
        target, kind, range = F.boss, "hammer", 1
        want_set = QD.raid._tob_worn(S, ids.hammer) and {} or { ids.hammer }
        intent.spec = QD.raid._tob_worn(S, ids.hammer) and F.energy >= 500
        boost = "piety"
    elseif freezer then
        local t, k = QD.raid._maiden_freeze_target(S, F)
        local prewave = t == nil and F.next_threshold ~= nil and F.permille <= F.next_threshold + V.PRE_WAVE
        if prewave then
            -- the wave is near: the cast set on, in place, no swing
            target, kind, range = nil, "prewave", V.SPELL_RANGE
            want_set = QD.raid._maiden_missing(S, ids.cast_set)
            boost = "augury"
            local off = {}
            if QD.raid._tob_worn(S, ids.defender) then off[#off + 1] = ids.worn_slot[V.WEAR_SHIELD] end
            if QD.raid._tob_worn(S, ids.boots) then off[#off + 1] = ids.worn_slot[V.WEAR_FEET] end
            if F.tick - (S.gear_sent or -10) >= 2 then intent.unequip = off end
            station = V.PRE_POS
        elseif t then
            target, kind, range = t, k, V.SPELL_RANGE
            want_set = QD.raid._maiden_missing(S, ids.cast_set)
            boost = "augury"
            -- the defender and the boots come off for the full roll (:249)
            local off = {}
            if QD.raid._tob_worn(S, ids.defender) then off[#off + 1] = ids.worn_slot[V.WEAR_SHIELD] end
            if QD.raid._tob_worn(S, ids.boots) then off[#off + 1] = ids.worn_slot[V.WEAR_FEET] end
            if F.tick - (S.gear_sent or -10) >= 2 then intent.unequip = off end
            want_off = #off
        else
            target, kind = F.boss, "her"
            -- a blood spawn near me before her: its trail is everyone's hazard
            local bd = nil
            for _, sl in ipairs(F.slugs) do
                local dd = cheb(sl.x, sl.z, F.me.x, F.me.z)
                if dd <= V.SLUG_NEAR and (bd == nil or dd < bd or (dd == bd and sl.slot < target.slot)) then
                    target, kind, bd = sl, "slug", dd
                end
            end
            want_set = QD.raid._maiden_missing(S, ids.range_set)
            boost = "rigour"
            if kind == "her" then station = QD.raid._maiden_station(S) end
        end
    else
        target, kind = QD.raid._maiden_dps_target(S, F)
        want_set = QD.raid._maiden_missing(S, ids.melee_set)
        boost = "piety"
        range = 1
        if kind == "her" then station = QD.raid._maiden_station(S) end
    end
    if #want_set > 0 and F.tick - (S.gear_sent or -10) >= 2 then intent.gear = want_set end
    QD.raid._tob_supplies(S, F, { overhead = "protectfrommagic", boost = boost,
        boost_stat = freezer and "ranged" or "attack" }, intent)
    if kind ~= S.target_kind then
        QD.raid._tob_trace(S, F.tick, "target " .. tostring(kind) .. (target and (" slot " .. tostring(target.slot)) or ""))
        S.target_kind = kind
    end
    local spec, names = QD.raid._maiden_spec(S, F, target, range, station)
    local plan = QD.raid._tob_plan(S, F, spec, names)
    local d = QD.raid._tob_order(S, F, plan, { target = target, range = range })
    -- THE CAST: in reach, the cast set worn, and the last cast's 5 ticks gone
    if freezer and (kind == "freeze" or kind == "clump") and d.order and d.order.mode == "attack" then
        -- the barrage is never an attack press: a plan that moves walks, a
        -- plan that stands casts when the cast is ready
        if d.x ~= F.me.x or d.z ~= F.me.z then
            d.order = { mode = "walk", x = d.x, z = d.z }
        else
            d.order = nil
        end
        local ready = d.order == nil and #want_set == 0 and want_off == 0
            and F.tick >= (S.next_cast or 0)
        if ready then
            intent.cast = { npc = target, component = ids.barrage }
            S.order = nil            -- a cast is one cast: the next one is a new order
            S.next_cast = F.tick + V.CAST_EVERY
            local cand = {}
            for _, c in ipairs(F.crabs) do
                cand[#cand + 1] = c.slot .. ":" .. c.eta .. (c.frozen and "f" or "")
            end
            S.cast_log[#S.cast_log + 1] = "t" .. F.tick .. " " .. kind .. " slot" .. target.slot
                .. " eta" .. target.eta .. " [" .. table.concat(cand, " ") .. "]"
        end
    end
    QD.raid._tob_emit(S, F, d.order, intent)
    QD.raid._tob_recent(S, F, d)
    return nil
end

-- ===================================================================== LOOP

function QD.raid.maiden_solve(opts)
    opts = opts or {}
    local S = QD.raid._tob_state("maiden_solve", QD.raid._maiden_ids(), opts, {
        pools = {}, pool_seen = {}, trails = {}, slug_last = {}, crab_rec = {},
        pool_probe = {}, crab_probe = {}, cast_log = {},
        throws = 0, storms = 0,
    })
    QD.raid._tob_start(S, nil, opts.start_ticks)
    return QD.raid._tob_run(S, QD.raid._maiden_step, function(s)
        return QD.raid._tob_summary(s, string.format("storms %d, throws %d; pools %s; crabs %s; casts %s",
            s.storms, s.throws, table.concat(s.pool_probe, " | "), table.concat(s.crab_probe, " | "),
            table.concat(s.cast_log, " ")))
    end)
end

-- The ids the test's tick-log measures read (a test file has no api_drive).
function QD.raid.maiden_symbols()
    return QD.raid._tob_symbols("maiden_symbols", {
        { "m100", "npc", "tob_maiden_100" }, { "m70", "npc", "tob_maiden_70" },
        { "m50", "npc", "tob_maiden_50" }, { "m30", "npc", "tob_maiden_30" },
        { "dying_a", "npc", "tob_maiden_dying_a" }, { "crab", "npc", "maiden_elemental" },
        { "slug", "npc", "maiden_blood_slug" }, { "seq_storm", "seq", "maiden_attack_special" },
        { "seq_blood", "seq", "maiden_attack_blood" },
        { "pool_gfx", "spotanim", "maiden_lingering_blood" },
        { "freeze_gfx", "spotanim", "ice_barrage_impact" }, { "splash_gfx", "spotanim", "failedspell_impact" },
    })
end
