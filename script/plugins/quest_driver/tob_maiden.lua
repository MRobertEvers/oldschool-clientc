-- quest-driver / tob_maiden: THE MAIDEN OF SUGADINTI, Normal trio.
--
--   t.raid.maiden_solve(opts) -> result, detail, record
--
-- Called by every seat at the room's entry (the corridor side of the
-- barrier). It starts the room (the leader) or crosses once the leader is in
-- (a member), and plays the fight on the shared loop (tob.lua):
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
-- THE ROLES (ROOM_SOLVERS.md 4.1.1): seat 1, the leader, is the FREEZER (it
-- leads the orb order, :597); seats 2 and 3 are DPS. Each wave is handled by
-- its spawn pattern's entry in QD.MAIDEN_WAVES (tob_maiden_waves.lua,
-- generated from 200 Blert rooms by tools/gen_maiden_wave_plans.py): the
-- freezer's barrage SLOTS (+1 a 1, +6 a 2, +11 the 3s adjacent in one 3x3,
-- +16 the 4s on one tile) from (41,30), then barrages on the biggest clump;
-- the DPS's KILL ORDER (both on N1, then N2, then the frozen ones in thaw
-- order) from her north-east corner. The DPS melee in the kit's void set with
-- the abyssal tentacle and Piety (the owner, 2026-10-09), opening with a
-- dragon warhammer special (:617-640); the freezer ranges her with the
-- blowpipe on Rapid between waves (:646) and barrages in the void mage set with
-- Augury, the defender and boots off (:248-249).
--
-- MEASURE -> DECIDE -> ACT each tick: MEASURE the npcs, her clock, pools,
-- trails, crabs and the wave; DECIDE the role's target, station, gear and
-- whether a slot's cast is due; ACT through the shared planner, order and emit.
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
    -- THE CAST LAG: how many ticks after the decision a press resolves --
    -- 0 on scriptrun (the bot sees tick T's npc phase and its press resolves
    -- in T's player phase: seed v2a/sa, the barrage on the spawn tick 239),
    -- one on the live client (it perceives a tick late, lesson 2). Measured
    -- from the first freeze's impact tick on the crab cast at; until then
    -- CAST_LAG_GUESS. A cast saves a crab still 1 + lag steps outside the
    -- leak box (the crab steps before the cast resolves; frozen from the next
    -- npc phase).
    CAST_LAG_GUESS = 0,
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
    -- PRE_WAVE: 40 permille was crossed in a tick late in the fight, so the
    -- cast set went on after the spawn and every slot slipped one (seed
    -- v2b/sa wave 3: the 4s' cast at +17, all three leaked)
    THRESHOLDS = { 700, 500, 300 }, PRE_WAVE = 100,
    -- THE WAVE STATIONS (Blert, 561 one-freezer waves; ROOM_SOLVERS.md 4.1.1):
    -- the freezer on x=41 (the one column with every spawn point within the
    -- spell's 10: S1, N1, S4o and N4o exactly 10 from (41,30)), the DPS on her
    -- north-east corner where N1 arrives ((32,33) 187, (31,34) 182).
    FREEZER_STATION = { x = 41, z = 30 },
    DPS_WAVE_TILE = { [2] = { x = 32, z = 33 }, [3] = { x = 31, z = 34 } },
    -- the spawn points (a crab's SW tile on its first tick), the scuffed
    -- tiles (one east and one further out, a whole wave at a time) naming the
    -- same point; and the order a wave's key lists them in
    POINTS = {
        ["37,40"] = "N1", ["41,40"] = "N2", ["45,40"] = "N3", ["49,38"] = "N4i", ["49,40"] = "N4o",
        ["37,20"] = "S1", ["41,20"] = "S2", ["45,20"] = "S3", ["49,22"] = "S4i", ["49,20"] = "S4o",
        ["38,41"] = "N1", ["42,41"] = "N2", ["46,41"] = "N3", ["50,39"] = "N4i", ["50,41"] = "N4o",
        ["38,19"] = "S1", ["42,19"] = "S2", ["46,19"] = "S3", ["50,21"] = "S4i", ["50,19"] = "S4o",
    },
    POINT_ORDER = { "N1", "N2", "N3", "N4i", "N4o", "S1", "S2", "S3", "S4i", "S4o" },
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
        { "rapid_slot", "component", "combat_interface:style_slot_1" },
        { "com_mode", "varp", "varp43_com_mode" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "maiden_solve: no combat tab")
    ids.combat_tab = combat_tab
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
                    -- to the pool's EXPIRY and a tick past it: an expiring
                    -- pool can become a blood spawn on its own tile, which lays
                    -- its trail on that tick (~tob_maiden_bloodspawn_chance_at);
                    -- a DPS back on its station as the pool's last hurt ended
                    -- took that trail, 8 of the 10 non-npc hits of sweep v2c
                    S.pools[#S.pools + 1] = { x = p.dst_x, z = p.dst_z, t0 = land - 1, t1 = land_extra + V.POOL_LIFE + 2,
                        expiry = land + V.POOL_LIFE }
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
            rec = { born = F.tick, x = c.x, z = c.z,
                    point = V.POINTS[(c.x - S.base.x) .. "," .. (c.z - S.base.z)] }
            S.crab_rec[c.slot] = rec
            S.crab_probe[#S.crab_probe + 1] = string.format("slot%d born t%d at %d,%d", c.slot, F.tick, c.x - S.base.x, c.z - S.base.z)
        end
        -- the SENT graphic and its tick (the packet's facts), never the drawn
        -- spotanim_id: that waits out the graphic's delay on the client's
        -- own frame clock, a tick later live than on scriptrun for the same
        -- packet (seed mxp, 2026-10-09: "cast lag 1" live, 0 on scriptrun)
        if c.spotanim_sent_id == S.ids.freeze_gfx and c.spotanim_tick ~= nil and c.spotanim_tick ~= rec.gfx_tick then
            rec.gfx_tick = c.spotanim_tick
            if rec.frozen_until == nil or rec.frozen_until < F.tick then
                rec.frozen_until = F.tick + V.FREEZE_TICKS
                S.freeze_log[#S.freeze_log + 1] = tostring(rec.point) .. "+" .. (c.spotanim_tick - rec.born)
                local probe = S.lag_probe
                if probe and probe.slot == c.slot then
                    local lag = c.spotanim_tick - probe.tick
                    assert(lag >= 0 and lag <= 2, "maiden_solve: a barrage decided on t" .. probe.tick
                        .. " landed on t" .. c.spotanim_tick)
                    if S.cast_lag ~= lag then
                        QD.raid._tob_trace(S, F.tick, "cast lag " .. lag .. " (was " .. S.cast_lag .. ")")
                    end
                    S.cast_lag = lag
                    S.lag_probe = nil
                end
            end
        end
        if (rec.x ~= c.x or rec.z ~= c.z) and rec.frozen_until and rec.frozen_until > F.tick then
            rec.frozen_until = nil   -- it stepped: not frozen
        end
        rec.x, rec.z = c.x, c.z
        c.point, c.born = rec.point, rec.born
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

-- ====================================================================== WAVE

-- The wave, on the tick its crabs appear: its points, its key, and the
-- table's handler for that pattern (tob_maiden_waves.lua,
-- generated from Blert by tools/gen_maiden_wave_plans.py; ROOM_SOLVERS.md
-- 4.1.1). Every seat reads the same rows and so holds the same handler.
function QD.raid._maiden_wave(S, F)
    local have, n = {}, 0
    for _, c in ipairs(F.crabs) do
        if c.born == F.tick then
            assert(c.point, "maiden_solve: a crab spawned on no spawn point, at "
                .. (c.x - S.base.x) .. "," .. (c.z - S.base.z))
            have[c.point] = true
            n = n + 1
        end
    end
    if n == 0 then return end
    local key = {}
    for _, p in ipairs(QD.MAIDEN.POINT_ORDER) do
        if have[p] then key[#key + 1] = p end
    end
    key = table.concat(key, " ")
    local plan = QD.MAIDEN_WAVES[key]
    assert(plan, "maiden_solve: no wave handler for [" .. key .. "]")
    S.waves = S.waves + 1
    S.wave = { t0 = F.tick, key = key, plan = plan, next = 1, n = S.waves }
    S.wave_log[#S.wave_log + 1] = "w" .. S.waves .. " t" .. F.tick .. " [" .. key .. "]"
    QD.raid._tob_trace(S, F.tick, "wave " .. S.waves .. " [" .. key .. "] " .. plan.blert)
end

-- ===================================================================== ROLES

-- The crab a slot can still save: of this wave, on one of the slot's points
-- (primary first), alive, unfrozen, and 1 + lag steps outside the leak box
-- (the crab's own step comes before my press resolves).
local function slot_crab(S, F, slot)
    for _, p in ipairs(slot.aim) do
        for _, c in ipairs(F.crabs) do
            if c.point == p and c.born == S.wave.t0 and not c.frozen and c.eta >= 1 + S.cast_lag then
                return c
            end
        end
    end
    return nil
end

-- THE FREEZER. The wave's handler names its barrage slots: each slot's cast
-- resolves on t0 + at (decided cast_lag ticks before) at the slot's crab, or the
-- slot lapses when none of its crabs can be saved. After the slots, a
-- barrage every 5 ticks on the biggest clump, for damage (a frozen crab is
-- not frozen again). Nil with no crab up.
function QD.raid._maiden_freezer(S, F)
    local V = QD.MAIDEN
    local W = S.wave
    while W and W.plan.freeze[W.next] do
        local slot = W.plan.freeze[W.next]
        local c = slot_crab(S, F, slot)
        if c then
            local resolve = F.tick + S.cast_lag
            local due = resolve >= W.t0 + slot.at
            -- A LATE SLOT gives way: cast now, and each later slot comes a
            -- barrage's 5 ticks after the one before it; a later slot that
            -- would then miss its `last` and takes as many crabs wins, and
            -- this one is dropped (seed v2b/sa wave 3: a 2 cast at +2 pushed
            -- the three 4s' cast to +17)
            local late = false
            if due and resolve > W.t0 + slot.at then
                for j = W.next + 1, #W.plan.freeze do
                    local later = W.plan.freeze[j]
                    local at = math.max(W.t0 + later.at, resolve + V.CAST_EVERY * (j - W.next))
                    if at > W.t0 + later.last and #later.aim >= #slot.aim then late = true end
                end
            end
            if not late then
                -- no station pull once the wave is up: a step back onto
                -- (41,30) on a slot's tick walks instead of casting (seed
                -- v2d/sd: stood on (40,30) after a pool, stepped on the tick
                -- S1 was due, and the slot was dropped)
                return { target = c, kind = due and "slot" or "slot-wait", slot = slot }
            end
            S.cast_log[#S.cast_log + 1] = "w" .. W.n .. " slot+" .. slot.at .. " " .. slot.aim[1] .. " dropped late t" .. F.tick
            W.next = W.next + 1
            goto continue
        end
        S.cast_log[#S.cast_log + 1] = "w" .. W.n .. " slot+" .. slot.at .. " " .. slot.aim[1] .. " lapsed t" .. F.tick
        W.next = W.next + 1
        ::continue::
    end
    local best, best_n = nil, 0
    for _, c in ipairs(F.crabs) do
        local n = 0
        for _, o in ipairs(F.crabs) do
            if cheb(o.x, o.z, c.x, c.z) <= 1 then n = n + 1 end
        end
        if n > best_n or (n == best_n and c.slot < best.slot) then best, best_n = c, n end
    end
    if best then return { target = best, kind = "clump" } end
    return nil
end

-- THE DPS. Both on the first crab up in the handler's kill order (N1, N2
-- unless a slot freezes it, then the frozen ones in the order they thaw); a
-- crab left from an earlier wave first, by ticks to its leak. Nil with no
-- crab up.
function QD.raid._maiden_dps(S, F)
    local rank = {}
    if S.wave then
        for i, p in ipairs(S.wave.plan.dps) do rank[p] = i end
    end
    local best, best_r = nil, nil
    for _, c in ipairs(F.crabs) do
        local r = 0
        if S.wave and c.born == S.wave.t0 then r = rank[c.point] or 99 end
        if best == nil or r < best_r or (r == best_r and (c.eta < best.eta or (c.eta == best.eta and c.slot < best.slot))) then
            best, best_r = c, r
        end
    end
    if best then return { target = best, kind = "crab", station = QD.MAIDEN.DPS_WAVE_TILE[S.role] } end
    return nil
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
            -- the spawn an expiring pool may become steps on its first tick,
            -- onto any tile of the pool's 3x3, and lays its trail there (seed
            -- v2d/sj t317: spawned on (32,31), stepped onto the DPS on (32,30))
            add.zone("pool-spawn", { x = pl.x, z = pl.z, size = 1, lo = 0, hi = 1,
                t0 = pl.expiry, t1 = pl.expiry + 2, tier = "damage", cost = V.SLUG_COST })
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
            -- its next step can be onto any tile of its 3x3, mine included: a
            -- raider standing beside a spawn took the trail it laid on the
            -- raider's own tile (seed v2a/sa t138, t390), the soft 3 this was
            -- outweighed by the reach pull
            add.zone("slug", { x = sl.x, z = sl.z, size = 1, lo = 0, hi = 1,
                t0 = F.tick + 1, t1 = F.tick + 1, tier = "damage", cost = V.SLUG_COST })
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
    QD.raid._maiden_wave(S, F)
    local intent = {}
    -- THE CONTENT PROBE (opts.ignore_crabs, test/raids/tob_maiden_probe.lua):
    -- every seat melees her and nothing else, so every crab walks its whole
    -- path to its leak untouched -- the calibration of the waves against Blert
    -- (ROOM_SOLVERS.md 4.7), not a way to play the room
    if S.ignore_crabs then
        local want_set = QD.raid._maiden_missing(S, ids.melee_set)
        if #want_set > 0 and F.tick - (S.gear_sent or -10) >= 2 then intent.gear = want_set end
        QD.raid._tob_supplies(S, F, { overhead = "protectfrommagic", boost = "piety", boost_stat = "attack" }, intent)
        local station = QD.raid._maiden_station(S)
        if S.role == 1 then station = V.TANK_TILE end
        local spec, names = QD.raid._maiden_spec(S, F, F.boss, 1, station)
        local plan = QD.raid._tob_plan(S, F, spec, names)
        local d = QD.raid._tob_order(S, F, plan, { target = F.boss, range = 1 })
        QD.raid._tob_emit(S, F, d.order, intent)
        QD.raid._tob_recent(S, F, d)
        return nil
    end
    local freezer = (S.role == 1)
    -- THE PIPE ON RAPID (the owner, 2026-10-09): once the blowpipe is in
    -- hand, the combat tab's style_slot_1 (combat_tab.rs2 writes
    -- varp43_com_mode = 1; the thrown row, combat.dbrow weapon_thrown_table,
    -- is Accurate, Rapid, Longrange) until varp43 reads 1. By slot, not by
    -- name: the names are a clientscript's text and scriptrun runs none, so
    -- QD.ui.style finds no 'Rapid' there and the lanes would part. Once: the
    -- server clamps varp43 to a new weapon's style count on wield
    -- (combat_stats.rs2:410) and slot 1 is inside the trident's row, so the
    -- swaps to the cast set and back keep Rapid. The press is an IF_BUTTON:
    -- it ends no attack, so the tick decides on as usual.
    if freezer and not S.pipe_rapid and QD.raid._tob_worn(S, ids.pipe) then
        local mr, mode = api_drive.varp(ids.com_mode)
        assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
        if mode == 1 then
            S.pipe_rapid = true
            QD.raid._tob_trace(S, F.tick, "blowpipe on Rapid")
        elseif F.tick - (S.rapid_sent or -10) >= 2 then
            S.rapid_presses = (S.rapid_presses or 0) + 1
            assert(S.rapid_presses <= 3, S.who .. ": three Rapid presses and varp43_com_mode reads " .. tostring(mode))
            QD.raid._tob_tab(S, ids.combat_tab)
            local pr, pd = api_drive.if_click(ids.rapid_slot, 1)
            assert(pr == "ok", S.who .. ": the Rapid press answered " .. tostring(pr) .. " " .. tostring(pd))
            S.rapid_sent = F.tick
        end
    end
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
    local station, slot = nil, nil
    local prewave = #F.crabs == 0 and F.next_threshold ~= nil and F.permille <= F.next_threshold + V.PRE_WAVE
    if not freezer and S.opener == "hammer" then
        target, kind, range = F.boss, "hammer", 1
        want_set = QD.raid._tob_worn(S, ids.hammer) and {} or { ids.hammer }
        intent.spec = QD.raid._tob_worn(S, ids.hammer) and F.energy >= 500
        boost = "piety"
    elseif freezer then
        -- DECIDE, THE FREEZER: the wave handler's slot, the clump, or (no
        -- crab up) the prewave set-up or her
        local R = QD.raid._maiden_freezer(S, F)
        if R or prewave then
            if R then
                target, kind, range, station = R.target, R.kind, V.SPELL_RANGE, R.station
                slot = R.slot
            else
                -- the wave is near: the cast set on, at the station, no swing
                target, kind, range, station = nil, "prewave", V.SPELL_RANGE, V.FREEZER_STATION
            end
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
            -- a spawn's attack order walks me after it the tick it steps out
            -- of reach, onto its own trails (seed v2a/sa t411): shoot it only
            -- from a tile a step inside the blowpipe's reach
            if kind == "slug" then range = V.PIPE_RANGE - 1 end
            want_set = QD.raid._maiden_missing(S, ids.range_set)
            boost = "rigour"
            if kind == "her" then station = QD.raid._maiden_station(S) end
        end
    else
        -- DECIDE, THE DPS: the wave handler's kill order, else her (on the
        -- wave tiles once the wave is near)
        local R = QD.raid._maiden_dps(S, F)
        if R then
            target, kind, station = R.target, R.kind, R.station
        else
            target, kind = F.boss, "her"
            station = prewave and V.DPS_WAVE_TILE[S.role] or QD.raid._maiden_station(S)
        end
        want_set = QD.raid._maiden_missing(S, ids.melee_set)
        boost = "piety"
        range = 1
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
    -- ACT, THE CAST: a slot's or a clump's crab in reach, the cast set worn,
    -- the last cast's 5 ticks gone, and a slot's tick come (a slot waiting
    -- for its tick holds the station instead)
    local casting = kind == "slot" or kind == "slot-wait" or kind == "clump"
    if freezer and casting and d.order and d.order.mode == "attack" then
        -- the barrage is never an attack press: a plan that moves walks, a
        -- plan that stands casts when the cast is ready
        if d.x ~= F.me.x or d.z ~= F.me.z then
            d.order = { mode = "walk", x = d.x, z = d.z }
        else
            d.order = nil
        end
        local ready = kind ~= "slot-wait" and d.order == nil and #want_set == 0 and want_off == 0
            and F.tick >= (S.next_cast or 0)
        if ready then
            intent.cast = { npc = target, component = ids.barrage }
            S.order = nil            -- a cast is one cast: the next one is a new order
            S.next_cast = F.tick + V.CAST_EVERY
            local what = kind
            if kind == "slot" then
                -- the cast's tick after the spawn: the press resolves cast_lag on
                what = "w" .. S.wave.n .. " slot+" .. slot.at .. " cast+" .. (F.tick + S.cast_lag - S.wave.t0)
                S.wave.next = S.wave.next + 1
            end
            S.lag_probe = { tick = F.tick, slot = target.slot }
            S.cast_log[#S.cast_log + 1] = "t" .. F.tick .. " " .. what .. " " .. tostring(target.point)
                .. " eta" .. target.eta
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
        throws = 0, storms = 0, ignore_crabs = opts.ignore_crabs == true,
        waves = 0, wave_log = {}, freeze_log = {}, cast_lag = QD.MAIDEN.CAST_LAG_GUESS,
    })
    QD.raid._tob_start(S, nil, opts.start_ticks)
    return QD.raid._tob_run(S, QD.raid._maiden_step, function(s)
        return QD.raid._tob_summary(s, string.format("storms %d, throws %d; waves %s; casts %s; froze %s",
            s.storms, s.throws, table.concat(s.wave_log, " | "), table.concat(s.cast_log, " | "),
            table.concat(s.freeze_log, " ")))
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
