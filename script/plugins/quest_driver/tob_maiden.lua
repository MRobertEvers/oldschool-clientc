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
    ENTRY = { x = 52, z = 30 },                 -- east of the barrier (x 49)
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
    -- (only the trail rows, filtered in C: api_drive.locs_of)
    local r, rows = api_drive.locs_of(2 * V.H + 2, { S.ids.trail })
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
                -- (S.hm has two freezers: a freeze landing more than two
                -- ticks after my cast on that crab is the other one's)
                if probe and probe.slot == c.slot and S.hm and c.spotanim_tick - probe.tick > 2 then
                    S.lag_probe = nil
                    probe = nil
                end
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
        -- it stepped: not frozen (S.hm, the Hard Mode solver tob_maiden_hm.lua:
        -- unless the freeze's graphic is this measure's own, when the step was
        -- the npc phase BEFORE the barrage landed)
        if (rec.x ~= c.x or rec.z ~= c.z) and rec.frozen_until and rec.frozen_until > F.tick
            and not (S.hm and rec.gfx_tick ~= nil and rec.gfx_tick >= F.tick - 1) then
            rec.frozen_until = nil
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

-- The wave handler's barrage slots (shared with tob_maiden_hm.lua): the slot
-- due or waiting, or nil once every slot has gone or lapsed.
function QD.raid._maiden_slots(S, F)
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
    return nil
end

-- ================================================================= CONTEXT

-- ======================================================================= GEAR

-- The objs of `set` not worn now.
function QD.raid._maiden_missing(S, set)
    local out = {}
    for _, obj in ipairs(set) do
        -- (a piece the seat does not carry is not missing: the Hard Mode kit
        -- leaves a seat's off-style boosters home, tob_relay.lua)
        if not QD.raid._tob_worn(S, obj) and QD.raid._tob_count(S, obj) > 0 then out[#out + 1] = obj end
    end
    return out
end

-- ===================================================================== STEP

-- ==================================================================== ENTRY

-- THE ENTRY PHASE: the lobby lands the party at the door end of the walkway,
-- (79,31) in the room's two-square instance (tob.constant) - in the SECOND
-- square, so its local x is 15, and the room's own square is the one west of
-- it. The walk to (52,30) puts the barrier (x 49) in view; then the shared
-- start, as Nylocas does from its arrival.
function QD.raid._maiden_enter(S, opts)
    local tr, me = api_drive.player_tile()
    assert(tr == "ok", "maiden_solve: no tile")
    local base_x = me.x - me.x % 64
    if me.x % 64 < QD.MAIDEN.ARENA.x0 then base_x = base_x - 64 end
    local ex, ez = base_x + QD.MAIDEN.ENTRY.x, me.z - me.z % 64 + QD.MAIDEN.ENTRY.z
    local deadline = api_drive.tick() + 60
    QD.raid._tob_trace(S, api_drive.tick(), "phase entry")
    while true do
        local F = QD.raid._tob_measure(S)
        if F.me.x == ex and F.me.z == ez then break end
        assert(F.tick <= deadline, "maiden_solve: never reached the entry tile from " .. F.me.x .. "," .. F.me.z)
        if F.tick - (S.walk_sent or -10) >= 3 then
            api_drive.move_to(ex, ez)
            S.walk_sent = F.tick
        end
        await({ event = "server_tick", match = function() return true end, note = "maiden_solve: to the entry" }, 3)
    end
    QD.raid._tob_start(S, nil, opts.start_ticks)
end

-- ===================================================================== LOOP
