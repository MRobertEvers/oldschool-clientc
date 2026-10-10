-- quest-driver / raid_solve_verzik_p2: VERZIK PHASE 2, SOLVED FROM SCRATCH.
--
--   t.raid.verzik_p2_solve(opts) -> result, detail, record
--
-- Built the way the design note's sections 1, 3, 4 and 6 lay out a solver
-- ("Verzik as a solver: measure, decide, act"): each server tick MEASURE
-- builds facts that carry ticks, BEHAVIORS turn them into cost terms on my
-- position and proposals on the other channels, the ARBITER takes the argmin
-- over candidate plans for movement (each plan scored tick by tick over a
-- short horizon, the plan already under way kept unless a challenger beats it
-- by a margin) and a winner per other channel, and EMIT sends only what
-- changed, in channel order.  Nothing waits; a send that did not take is a
-- fact the next tick reads.  Drive primitives only; no raid code is reused.
-- docs/minigames/theater_of_blood/solver_lessons.md is the checklist this
-- was written against.
--
-- THE SPEC, from the content the server runs (OSRS-Content .../minigame_tob:
-- scripts/tob_verzik.rs2 "C:", configs/tob.constant).  Tick mapping: npcs run
-- before players, so anything decided on server tick T reads where players
-- stood at the END of T-1; a click decided after seeing tick d moves me during
-- tick d+1, so a plan's tile_at[k] is my tile at the end of tick d+k.
--
--   her clock      an attack every 4 ticks; the first 3 after her P2 form
--                  appears (C: ~tob_verzik_enter_p2).  The reds summon puts
--                  the next attack 12 on; the seventh attack after a summon
--                  puts the next summon 8 on (C: ~tob_verzik_p2_tick).
--   body slam      at each attack tick T, a raider ADJACENT to her (npc_range
--                  1) at the end of T-1 is slammed 75% of the time (45,
--                  stun 5); one under her is stomped (82).  -> my tile at the
--                  end of T-1 must be 2+ from her footprint.
--   urnbomb        thrown at T at each raider's tile; lands after
--                  floor((56 + 8d) / 30) ticks (d from her centre), checked in
--                  the player's queue, which runs BEFORE the player's step
--                  (torirs_server_world.c phase_player).  -> that tile is
--                  forbidden at the end of T+N-2 and T+N-1 (both readings of
--                  the queue's first decrement).
--   lightning      after FOUR CABBAGES (purples, bounces and blood spells
--                  do not count); with the reds out a due zap takes the
--                  cabbage slot. From a random raider, each hop goes to the nearest
--                  raider on ANOTHER tile; a hop whose midpoint is under her
--                  ends it on her (15-20 to her); otherwise after 4 hops the
--                  last raider's tile takes up to 48 (25 in insulated
--                  boots).  -> two raiders stacked on one side, one on the
--                  other, so every hop crosses her (W-E midpoint is her
--                  centre).
--   Athanatos      cast at a raider's tile; lands 6 ticks later (up to 78 on
--                  that tile), then heals her 9-10 every 4 ticks until a
--                  poisonous hit (a worn serpentine helm makes every hit one)
--                  bursts it into her.  Crabs spawn with the cast.
--   crabs          one per raider, spawned 2-6 tiles BEYOND its owner, walking
--                  at it one tile a tick; on contact (range 1) or at 25 ticks
--                  it dies, and a death blasts everyone within 3 (63/26/8).
--                  -> never within 3 of a crab; kite at 2 tiles a tick.
--   reds (35%)     a summon (her heal animation) puts two Matomenos beside
--                  her; for 5 ticks every hit on her heals her (tob_damage
--                  .rs2); remaining red hitpoints heal her at the next
--                  summon.  From then on 75% of her attacks are the blood
--                  spell: a 3x3 round one raider, read at the cast, dealing 0
--                  under Protect from Magic.  No lightning.
-- ==========================================================================

QD.VZP2 = {
    CADENCE = 4, FIRST_ATTACK = 3, AFTER_SUMMON = 12, AFTER_SEVENTH = 8, ATTACKS_PER_REDS = 7,
    CABBAGES_PER_ZAP = 4, ABSORB_TICKS = 5,
    REDS_NEAR = 0.37,          -- her bar at or below this: any attack slot may be the first summon
    REDS_LEAVE = 0.15,         -- her bar at or below this at a summon: the set is not worth killing (a cap: the rate below decides)
    REDS_RATE_TICKS = 24,      -- her bar's fall over this many ticks is the kit's rate
    HEAL_SWING = 8,            -- a swing on her in the absorb window: worse than three idle ticks
    CRAB_LIFE = 25, CRAB_BLAST = 3, CRAB_SIZE = 2,
    SIZE = 3,
    H = 5,                    -- horizon, ticks
    WAITS = 3,                -- a plan may wait up to this many ticks before its click
    MARGIN = 0.75,            -- a challenger must beat the plan under way by this
    INF = 1e9,
    ARENA = { x = 31, z = 25 },                       -- her P2 south-west tile, local (C: ^tob_verzik_arena_l[xz])
    FLOOR = { x0 = 21, x1 = 43, z0 = 15, z1 = 32 },   -- local; the crab spawn constants' extent
    HP_EAT = 45,
    PRAYER_SIP = 25,
    COMBAT_REDOSE = 5,        -- re-dose the super combat once the boost has decayed to +5
    TRACE_CAP = 120,
}

function QD.raid._vzp2_cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

-- Chebyshev gap from a tile to a footprint (0 under it, 1 beside it).
function QD.raid._vzp2_gap(x, z, fx, fz, n)
    return math.max(math.max(fx - x, x - (fx + n - 1), 0), math.max(fz - z, z - (fz + n - 1), 0))
end

-- Beside a footprint and not on its diagonal: where melee reaches.
function QD.raid._vzp2_beside(x, z, fx, fz, n)
    local gx = math.max(fx - x, x - (fx + n - 1), 0)
    local gz = math.max(fz - z, z - (fz + n - 1), 0)
    return (gx == 1 and gz == 0) or (gx == 0 and gz == 1)
end

function QD.raid._vzp2_now()
    local r, tick = api_drive.server_tick()
    if r == "ok" then return tick end
    local session = api_drive.session()
    assert(type(session) == "table" and math.type(session.lockstep_tick) == "integer",
        "verzik_p2_solve: no server tick and no lockstep tick")
    return session.lockstep_tick
end

function QD.raid._vzp2_ids(weapon)
    local function sym(kind, name)
        local r, id = api_drive.symbol(kind, name)
        assert(r == "ok", "verzik_p2_solve: no " .. kind .. " named " .. name)
        return id
    end
    local function comp(name)
        local r, id = api_drive.component(name)
        assert(r == "ok", "verzik_p2_solve: no component " .. name)
        return id
    end
    local _, inv_tab = api_drive.tab_by_name("inventory")
    local _, prayer_tab = api_drive.tab_by_name("prayer")
    return {
        p2 = sym("npc", "verzik_phase2"),
        before = sym("npc", "verzik_phase1_to2_transition"),
        after = sym("npc", "verzik_phase2_to3_transition"),
        athanatos = sym("npc", "tob_verzik_phase2_armourednylocas"),
        red = sym("npc", "tob_verzik_phase2_bloodnylocas"),
        crabs = { [sym("npc", "verzik_nylocas_melee")] = true, [sym("npc", "verzik_nylocas_ranged")] = true,
                  [sym("npc", "verzik_nylocas_magic")] = true },
        seq_magic = sym("seq", "verzik_phase2_attack_magic"),
        seq_melee = sym("seq", "verzik_phase2_attack_melee"),
        seq_heal = sym("seq", "verzik_phase2_heal"),
        urn = sym("spotanim", "verzik_phase2_ranged"),
        lightning = sym("spotanim", "verzik_phase2_lightning"),
        athanatos_proj = sym("spotanim", "verzik_phase2_spawn_armouredtank_proj"),
        inv = sym("inv", "inv"), worn = sym("inv", "worn"),
        hitpoints = sym("stat", "hitpoints"),
        prayer = sym("stat", "prayer"),
        restores = QD.raid._vzp2_restores(),
        helm = sym("obj", "serpentine_helm_charged"),
        -- the weapon P1 found in my hand (QD.raid._vzp1_weapon_from_hand)
        weapon = QD.raid.vz_weapon or sym("obj", weapon or "scythe_of_vitur"),
        food = sym("obj", "anglerfish"),
        foods = { sym("obj", "anglerfish"), sym("obj", "mantaray"), sym("obj", "seaturtle"), sym("obj", "shark") },
        backpack = comp("inventory:items"),
        missiles = comp("prayerbook:prayer14"), magic = comp("prayerbook:prayer13"),
        missiles_lit = sym("varbit", "varb4117_prayer_protectfrommissiles"),
        magic_lit = sym("varbit", "varb4116_prayer_protectfrommagic"),
        piety = comp("prayerbook:prayer27"), piety_lit = sym("varbit", "varb4129_prayer_piety"),
        attack = sym("stat", "attack"),
        combats = QD.raid._vzp2_doses("dose2combat"),
        brews = QD.raid._vzp2_doses("dosepotionofsaradomin"),
        inv_tab = inv_tab, prayer_tab = prayer_tab,
    }
end

function QD.raid._vzp2_trace(S, d, text)
    if #S.trace < QD.VZP2.TRACE_CAP then S.trace[#S.trace + 1] = "t" .. d .. " " .. text end
end

-- ==================================================================== MEASURE
--
-- facts = { tick, me, boss, after, athanatos, reds, crabs, mates,
--           next_attack, scans = {tick set}, zap_scan, hazards, absorb,
--           hp, lit, helm_worn }

function QD.raid._vzp2_measure(S)
    local ids, F = S.ids, { reds = {}, crabs = {}, mates = {}, hazards = S.hazards }
    F.tick = QD.raid._vzp2_now()
    local tr, tile = api_drive.player_tile()
    assert(tr == "ok", "verzik_p2_solve: no tile")
    F.me = { x = tile.x, z = tile.z }
    local nr, rows = api_drive.npcs(0)
    if nr ~= "ok" then rows = {} end
    for _, row in ipairs(rows) do
        local id = row.npc_id
        -- THE SERVER TILE. Live npc rows carry the drawn (stepping) tile in
        -- x/z and the server's in server_x/server_z; scriptrun's x IS the
        -- server tile. Read off x, both lanes decide from the same tile
        -- (2026-10-09: the two tick logs were identical for 245 ticks and
        -- parted on one P2 decision made from her stepping tile).
        if row.server_x ~= nil then row.x, row.z = row.server_x, row.server_z end
        if id == ids.p2 then F.boss = row
        elseif id == ids.before then F.before = row
        elseif id == ids.after then F.after = row
        elseif id == ids.athanatos and (row.health_ratio == nil or row.health_ratio ~= 0) then F.athanatos = row
        elseif id == ids.red and (row.health_ratio == nil or row.health_ratio ~= 0) then F.reds[#F.reds + 1] = row
        elseif ids.crabs[id] then F.crabs[#F.crabs + 1] = row
        end
    end
    local pr, players = api_drive.players()
    if pr == "ok" then
        for _, p in ipairs(players) do
            if not p.me then F.mates[#F.mates + 1] = { x = p.server_x or p.x, z = p.server_z or p.z } end
        end
    end
    local hr, hp = api_drive.skill(ids.hitpoints)
    F.hp = (hr == "ok" and hp.level) or 0
    local qr, pp = api_drive.skill(ids.prayer)
    F.prayer = (qr == "ok" and pp.level) or 0
    local _, ml = api_drive.varbit(ids.missiles_lit)
    local _, gl = api_drive.varbit(ids.magic_lit)
    local _, pl = api_drive.varbit(ids.piety_lit)
    F.lit = { missiles = ml == 1, magic = gl == 1, piety = pl == 1 }
    local ar, at = api_drive.skill(ids.attack)
    F.attack, F.attack_base = (ar == "ok" and at.level) or 0, (ar == "ok" and at.base_level) or 0
    local _, hw = api_drive.inv_count(ids.worn, ids.helm)
    F.helm_worn = (hw or 0) > 0
    -- the Dawnbringer goes with her P1 form: its last holder is left
    -- bare-handed (so p2: punching for 550 ticks) with the weapon in the pack
    local _, sw = api_drive.inv_count(ids.worn, ids.weapon)
    local _, sp = api_drive.inv_count(ids.inv, ids.weapon)
    F.weapon_held = (sw or 0) == 0 and (sp or 0) > 0
    return F
end

-- Her clock and the attack facts, from her animation (the only thing a
-- player sees).  Re-anchored on every attack and summon seen.
function QD.raid._vzp2_clock(S, F)
    local V, b = QD.VZP2, F.boss
    if b and b.pred then b = nil end
    if b and S.entered == nil then
        S.entered = F.tick
        S.next_attack = F.tick + V.FIRST_ATTACK
        QD.raid._vzp2_trace(S, F.tick, "her P2 form at " .. b.x .. "," .. b.z .. "; urn " .. S.ids.urn
            .. " athanatos " .. S.ids.athanatos_proj)
    end
    if b and b.seq_tick ~= nil and b.seq_tick ~= S.last_seq_tick and b.seq_id ~= nil and b.seq_id >= 0 then
        S.last_seq_tick = b.seq_tick
        if b.seq_id == S.ids.seq_heal then
            -- the reds summon: no scan this tick; hits on her heal her for 5
            S.next_summon = nil
            S.reds_out = true
            S.reds_attacks = 0
            S.absorb_until = F.tick + V.ABSORB_TICKS
            S.next_attack = F.tick + V.AFTER_SUMMON
            S.summons = S.summons + 1
            QD.raid._vzp2_trace(S, F.tick, "summon")
        elseif b.seq_id == S.ids.seq_magic or b.seq_id == S.ids.seq_melee then
            S.attacks = S.attacks + 1
            S.last_attack = F.tick
            if b.seq_id == S.ids.seq_melee then S.slams = S.slams + 1 end
            if S.reds_out then
                S.reds_attacks = S.reds_attacks + 1
                -- the seventh puts the next summon 8 on, and the attack after
                -- it 12 after that
                S.next_attack = F.tick + (S.reds_attacks >= V.ATTACKS_PER_REDS
                    and (V.AFTER_SEVENTH + V.AFTER_SUMMON) or V.CADENCE)
                if S.reds_attacks >= V.ATTACKS_PER_REDS then S.next_summon = F.tick + V.AFTER_SEVENTH end
            else
                S.next_attack = F.tick + V.CADENCE
            end
        end
    end
    -- the scan ticks inside the horizon: attacks at next_attack, then every 4
    F.scans = {}
    if S.next_attack then
        local t = S.next_attack
        for _ = 1, 3 do
            F.scans[t - 1] = true
            t = t + V.CADENCE
        end
    end
    F.next_attack = S.next_attack
    -- the next lightning: once four cabbages have flown since the last
    -- (`since_zap` counts cabbage casts, QD.raid._vzp2_projectiles). With
    -- the reds out it is only ever the next attack: a due zap takes the
    -- cabbage slot whenever she does not heal (Blert's reds phase: 6% zaps).
    -- This counted every attack and was never reset, and assumed no zap with
    -- the reds out: the seats spread on the diagonal, the ball ping-ponged
    -- the full four hops and the last raider took up to 48 (relay rl45, 40
    -- of 167 zaps).
    F.zap_scan = nil
    if S.next_attack then
        local ahead = V.CABBAGES_PER_ZAP - S.since_zap
        if ahead < 1 then ahead = 1 end
        if not S.reds_out or ahead == 1 then
            F.zap_scan = S.next_attack + (ahead - 1) * V.CADENCE - 1
        end
    end
    F.absorb = S.absorb_until ~= nil and F.tick < S.absorb_until
    -- the ticks a swing on her would heal her: the summon's tick and the
    -- four after (C: map_clock < summon + ^tob_verzik_p2_absorb_ticks).  Known
    -- exactly once the reds clock runs; before the first set, any attack slot
    -- once her bar is near 35% (so p2: 434 healed by swings that were already
    -- under way when she summoned)
    F.heal_ticks = {}
    local T = nil
    if S.reds_out then
        if S.next_summon and S.next_summon >= F.tick then T = S.next_summon end
    elseif b and S.next_attack and b.health_ratio and b.health_scale and b.health_scale > 0
        and b.health_ratio / b.health_scale <= V.REDS_NEAR then
        T = S.next_attack
    end
    if T then
        for t = T, T + V.ABSORB_TICKS - 1 do F.heal_ticks[t] = true end
    end
    if S.absorb_until then
        for t = F.tick, S.absorb_until - 1 do F.heal_ticks[t] = true end
    end
end

-- New projectiles become tile hazards with the ends they are forbidden on.
function QD.raid._vzp2_projectiles(S, F)
    local r, projs = api_drive.projectiles(0)
    if r ~= "ok" or F.boss == nil then return end
    local b = F.boss
    local cx, cz = b.x + 1, b.z + 1
    -- a projectile is NEW when its key was not in flight last tick: `launched`
    -- is a boolean on both lanes, so a key with it named only the first of
    -- the bombs a raider standing still is thrown each cycle (p2m)
    local now = {}
    for _, p in ipairs(projs) do
        local key = tostring(p.spotanim_id) .. ":" .. p.src_x .. "," .. p.src_z .. ">" .. p.dst_x .. "," .. p.dst_z
        now[key] = true
        if not S.seen_proj[key] and (S.proj_trace or 0) < 40 then
            S.proj_trace = (S.proj_trace or 0) + 1
            QD.raid._vzp2_trace(S, F.tick, string.format("proj new %s cycles %s target %s launched %s (%d rows)", key,
                tostring(p.cycles_left), tostring(p.target), tostring(p.launched), #projs))
        end
        if not S.seen_proj[key] then
            -- the zap's count: one per cabbage cast (one urn a raider, all on
            -- its tick), and back to none at a lightning (hops included)
            if p.spotanim_id == S.ids.urn and S.cabbage_tick ~= F.tick then
                S.cabbage_tick = F.tick
                S.since_zap = S.since_zap + 1
            elseif p.spotanim_id == S.ids.lightning then
                S.since_zap = 0
            end
            if p.spotanim_id == S.ids.urn then
                local n = math.floor((56 + 8 * QD.raid._vzp2_cheb(cx, cz, p.dst_x, p.dst_z)) / 30)
                S.hazards[#S.hazards + 1] = { x = p.dst_x, z = p.dst_z, ends = { [F.tick + n - 2] = true, [F.tick + n - 1] = true },
                    last = F.tick + n - 1, kind = "urn" }
            elseif p.spotanim_id == S.ids.athanatos_proj then
                S.hazards[#S.hazards + 1] = { x = p.dst_x, z = p.dst_z, ends = { [F.tick + 4] = true, [F.tick + 5] = true },
                    last = F.tick + 5, kind = "athanatos" }
            end
        end
    end
    S.seen_proj = now
    local keep = {}
    for _, h in ipairs(S.hazards) do if h.last >= F.tick then keep[#keep + 1] = h end end
    S.hazards = keep
    F.hazards = keep
end

-- Crabs.  Harmless until they die: on reaching their owner (range 1) or at
-- 25 ticks (C: ~tob_verzik_crab_tick), a crab stops where it is and its death
-- blasts everyone within 3 of it about four ticks later (tick log: s13 died
-- t212, blast t216; s11 t218, t222).  So no kiting: its death, seen as its
-- health bar reading 0, becomes a blast hazard on its footprint plus 3 for
-- the ticks the blast can read, and there are two ticks of running (four
-- tiles) to leave it -- the owner's tip: "the crab stops moving when it
-- starts exploding so you only need to move outside its distance".
function QD.raid._vzp2_crabs(S, F)
    for _, c in ipairs(F.crabs) do
        local rec = S.crab_seen[c.slot]
        if rec == nil then
            rec = { born = F.tick }
            S.crab_seen[c.slot] = rec
            QD.raid._vzp2_trace(S, F.tick, "crab at " .. c.x .. "," .. c.z)
        end
        -- dead: its bar at 0, or its death splat (nothing else hits a crab)
        if rec.died == nil and (c.health_ratio == 0 or (c.hit_damage ~= nil and c.hit_damage > 0)) then
            rec.died = F.tick
            -- death on D, blast damage on D+4 (tick log), so the blast reads
            -- the end of D+2: two ticks of running.  A window from D+1 made
            -- every escape impossible and the raider stood still (p2n t299).
            S.blasts[#S.blasts + 1] = { x = c.x, z = c.z, from = F.tick + 2, to = F.tick + 3 }
            QD.raid._vzp2_trace(S, F.tick, "crab dies at " .. c.x .. "," .. c.z)
        end
    end
    local keep = {}
    for _, bl in ipairs(S.blasts) do if bl.to >= F.tick then keep[#keep + 1] = bl end end
    S.blasts = keep
end

-- =================================================================== FLOOR
--
-- Walls are the client's: every walk is api_drive.route, the client's own
-- pathfinder over its collision map (the server's flood), so a plan's tiles
-- are the tiles the server will walk.  Her footprint is not a wall to a
-- player (players walk through npcs) but standing in it is the stomp, so it
-- is a cost, below.

function QD.raid._vzp2_under(F, x, z)
    local b = F.boss
    return b ~= nil and QD.raid._vzp2_gap(x, z, b.x, b.z, QD.VZP2.SIZE) == 0
end

-- The tiles a route from `from` takes, one per tick, running; cached per tick.
function QD.raid._vzp2_route(S, from, x, z, size)
    local key = from.x .. "," .. from.z .. ">" .. x .. "," .. z .. ":" .. (size or 0)
    local hit = S.routes[key]
    if hit ~= nil then return hit end
    local r, rt = api_drive.route(x, z, { run = true, size = size, from = { x = from.x, z = from.z } })
    local ticks = (r == "ok" and rt) and rt.ticks or false
    S.routes[key] = ticks
    -- the lane-parity trace: the first sixty route answers once a crab exists
    if next(S.crab_seen) ~= nil and (S.route_trace or 0) < 60 then
        S.route_trace = (S.route_trace or 0) + 1
        QD.raid._vzp2_trace(S, S.last_tick or 0, string.format("route %s -> %s arrive %s,%s = %s first %s", key, tostring(r),
            tostring(rt and rt.arrive and rt.arrive.x), tostring(rt and rt.arrive and rt.arrive.z), tostring(ticks and #ticks),
            ticks and ticks[1] and (ticks[1].x .. "," .. ticks[1].z) or "-"))
    end
    return ticks
end

-- A crab's next tile: the engine's naive step (straight at its owner, the
-- diagonal first, then either axis), which her footprint blocks -- npcs do
-- not walk through npcs (torirs_server_world.c ToriRSServer_WorldNpcWalkTo).
function QD.raid._vzp2_crab_step(F, cx, cz, tx, tz)
    local b, n = F.boss, QD.VZP2.CRAB_SIZE
    local function free(x, z)
        if b == nil then return true end
        return x + n - 1 < b.x or x > b.x + QD.VZP2.SIZE - 1 or z + n - 1 < b.z or z > b.z + QD.VZP2.SIZE - 1
    end
    local sx = (tx > cx + n - 1 and 1) or (tx < cx and -1) or 0
    local sz = (tz > cz + n - 1 and 1) or (tz < cz and -1) or 0
    if sx ~= 0 and sz ~= 0 and free(cx + sx, cz + sz) then return cx + sx, cz + sz end
    if sx ~= 0 and free(cx + sx, cz) then return cx + sx, cz end
    if sz ~= 0 and free(cx, cz + sz) then return cx, cz + sz end
    return cx, cz
end

-- ================================================================= BEHAVIORS
--
-- Each returns cost terms on (tile, end-of-tick) or a proposal.  A term is a
-- function (x, z, tick) -> cost.

-- My side.  Two raiders stack west, one east, so every lightning hop crosses
-- her; once the reds are out (no lightning) they spread west, south, east
-- (the blood spell's 3x3).  "stand" is the melee tile, "back" one off it.
function QD.raid._vzp2_side(S, F)
    local b = F.boss
    local x0, z0 = b.x, b.z
    local side = (S.role == 3) and "east" or "west"
    if S.reds_out and S.role == 2 then side = "south" end
    if side == "north" then return side, { x = x0 + 1, z = z0 + 3 }, { x = x0 + 1, z = z0 + 4 } end
    if side == "west" then return side, { x = x0 - 1, z = z0 + 1 }, { x = x0 - 2, z = z0 + 1 } end
    if side == "east" then return side, { x = x0 + 3, z = z0 + 1 }, { x = x0 + 4, z = z0 + 1 } end
    return side, { x = x0 + 1, z = z0 - 1 }, { x = x0 + 1, z = z0 - 2 }
end

-- What I hit: never her inside the absorb window; MY Matomenos once they are
-- out, then her; the Athanatos if I carry the poison and no crab is out.
function QD.raid._vzp2_target(S, F)
    if F.boss.pred then return nil, "flying" end
    -- the Athanatos first, for whoever carries the poison: alive it heals her
    -- 9-10 every 4 ticks, more than a red is worth (p2o: it outlived the reds
    -- phase and her bar climbed 898 -> 1189).  Only one that landed near
    -- her: chasing one across the room broke the lightning formation (p2j).
    if F.athanatos and F.helm_worn
        and QD.raid._vzp2_gap(F.athanatos.x, F.athanatos.z, F.boss.x, F.boss.z, QD.VZP2.SIZE) <= 6 then
        return F.athanatos, "athanatos"
    end
    -- The last set is left alone: when what is left of her goes before the
    -- next summon, a red only matters if she lives to absorb it (Blert, 20
    -- Normal trios: 43 of 92 reds died 42-45 ticks after spawning -- with
    -- her, at the end of P2 -- and every earlier set was killed)
    local frac = (F.boss.health_ratio and F.boss.health_scale and F.boss.health_scale > 0)
        and F.boss.health_ratio / F.boss.health_scale or 1
    -- ...AT THE RATE THIS KIT TAKES HER DOWN. A fixed 15% was a scythe's
    -- five ticks; a whip team at 15% cannot finish her before the absorb, so
    -- every late set healed her 300 and the sets kept coming (r14: up to 14
    -- reds and 900 healed; Blert's trios: 4 reds, all killed, no heals).
    -- The bar's fall over the last REDS_RATE_TICKS is the rate; the set is
    -- left alone only when what is left of her goes in the absorb window at
    -- that rate, with a tick of margin.
    S.frac_hist = S.frac_hist or {}
    S.frac_hist[#S.frac_hist + 1] = frac
    if #S.frac_hist > QD.VZP2.REDS_RATE_TICKS then table.remove(S.frac_hist, 1) end
    local rate = (#S.frac_hist >= 2) and math.max((S.frac_hist[1] - frac) / (#S.frac_hist - 1), 0) or 0
    local leave = math.min(QD.VZP2.REDS_LEAVE, rate * (QD.VZP2.ABSORB_TICKS - 1))
    -- the lane-parity trace: what this read saw, while reds are out
    if #F.reds > 0 and (S.target_trace or 0) < 40 then
        S.target_trace = (S.target_trace or 0) + 1
        QD.raid._vzp2_trace(S, F.tick, string.format("target read: hp %s/%s frac %.4f rate %.5f leave %.4f reds %d absorb %s",
            tostring(F.boss.health_ratio), tostring(F.boss.health_scale), frac, rate, leave, #F.reds, tostring(F.absorb)))
    end
    if #F.reds > 0 and frac <= leave then
        if F.absorb then return nil, "absorb" end
        return F.boss, "her (last set)"
    end
    if #F.reds > 0 then
        -- EACH SEAT OWNS ONE RED FOR THE SET, chosen once per summon: the
        -- east seat the eastmost, the two west seats the westmost, so all
        -- three are on a red. When mine is dead I go back to her, never to
        -- the other red: that one is across her, further than any plan's
        -- horizon reaches, so retargeting it left the seat standing still
        -- until its mates killed it (r0: seat 1 idle t290-t300 and
        -- t336-t347, both reds' sets).
        if S.red_summon ~= S.summons then S.red_summon, S.my_red = S.summons, nil end
        if S.my_red == nil then
            local pick = nil
            for _, r in ipairs(F.reds) do
                if pick == nil then pick = r
                elseif S.role == 3 and r.x > pick.x then pick = r
                elseif S.role ~= 3 and r.x < pick.x then pick = r end
            end
            S.my_red = pick.slot
        end
        for _, r in ipairs(F.reds) do
            if r.slot == S.my_red then return r, "red" end
        end
        if F.absorb then return nil, "absorb" end
        return F.boss, "her (my red done)"
    end
    if F.absorb then return nil, "absorb" end
    return F.boss, "her"
end

function QD.raid._vzp2_terms(S, F)
    local V, b = QD.VZP2, F.boss
    local terms = { names = {} }
    local function named(name) terms.names[#terms + 1] = name end
    -- under her is a wall (the floor model); beside her at a scan end is a slam
    named("scan")
    terms[#terms + 1] = function(x, z, t)
        if F.scans[t] and QD.raid._vzp2_gap(x, z, b.x, b.z, V.SIZE) <= 1 then return V.INF end
        return 0
    end
    -- bomb and Athanatos tiles at their ends
    named("hazard")
    terms[#terms + 1] = function(x, z, t)
        for _, h in ipairs(F.hazards) do
            if h.ends[t] and h.x == x and h.z == z then return V.INF end
        end
        return 0
    end
    -- crab blasts: within 3 of a dead crab on the ticks its blast can read
    if #S.blasts > 0 then
        named("blast")
        terms[#terms + 1] = function(x, z, t)
            for _, bl in ipairs(S.blasts) do
                if t >= bl.from and t <= bl.to and QD.raid._vzp2_gap(x, z, bl.x, bl.z, V.CRAB_SIZE) <= V.CRAB_BLAST then
                    return V.INF
                end
            end
            return 0
        end
    end
    -- my side: near its stand tile, and on its back tile at a lightning scan
    local _, stand, back = QD.raid._vzp2_side(S, F)
    named("side")
    terms[#terms + 1] = function(x, z, t)
        -- at a lightning scan the formation is the whole defence: every hop
        -- must cross her, so being off my side's back tile is priced like a
        -- hit, not like a step
        -- (with the reds out the spread's STAND tiles are the formation:
        -- west, south and east of her, every pair's midpoint under her)
        if F.zap_scan == t then
            local at = S.reds_out and stand or back
            return 15 * QD.raid._vzp2_cheb(x, z, at.x, at.z)
        end
        return 0.3 * QD.raid._vzp2_cheb(x, z, stand.x, stand.z)
    end
    -- damage: beside my target on the ticks it can be hit
    local target = S.target
    if target then
        local n = target.size or 1
        named("reach")
        -- the swing needs the interaction, not just the tile: standing beside
        -- her on a walk swings nothing (p2d: perfect scythe-walk tiles, one
        -- swing in 470 ticks)
        terms[#terms + 1] = function(x, z, t, k, path)
            if path[k].attacking and QD.raid._vzp2_beside(x, z, target.x, target.z, n) then return 0 end
            return 3
        end
    end
    -- a swing on her inside the absorb window heals her
    if next(F.heal_ticks) and not b.pred then
        named("heal")
        terms[#terms + 1] = function(x, z, t, k, path)
            if F.heal_ticks[t] and path[k].on == b.slot and QD.raid._vzp2_beside(x, z, b.x, b.z, V.SIZE) then
                return V.HEAL_SWING
            end
            return 0
        end
    end
    return terms
end

-- ================================================================== ARBITER
--
-- A plan: wait `w` ticks on what I am already doing, then one click (a walk
-- to `dest`, or an attack on `target`).  Simulated two tiles a tick into
-- tile_at[1..H] and scored over the horizon.  The plan under way ("no click")
-- is the incumbent; a challenger must beat it by the margin, and a walk whose
-- first step differs from a walk under way is taken only when the incumbent
-- is impossible (the design note's switch rule: no oscillation).

-- An npc row as this tick shows it, by slot (nil: gone).
function QD.raid._vzp2_live(F, row)
    if row == nil then return nil end
    if F.boss and F.boss.slot == row.slot then return F.boss end
    if F.athanatos and F.athanatos.slot == row.slot then return F.athanatos end
    for _, r in ipairs(F.reds) do if r.slot == row.slot then return r end end
    return nil
end

-- The tiles an order walks me through from `from`, one per tick.
function QD.raid._vzp2_order_ticks(S, F, from, order)
    if order == nil then return false end
    if order.mode == "attack" then
        -- the target where it stands NOW (an order keeps the row from its
        -- click; the reds walk -- p2i t569: a stale tile predicted a step the
        -- in-reach raider never took, onto nothing but a bomb)
        local tg = QD.raid._vzp2_live(F, order.target)
        if tg == nil then return false end
        -- in reach already: the server swings from here and does not step
        -- (a route to the reach can name another reach tile -- p2h t569: the
        -- plan "kept attacking" off a bomb that the raider then stood on)
        -- touching it, diagonals too: the server swings from there and does
        -- not step (si t407: a diagonal neighbour of its target was predicted
        -- to step off her side and stood still at her scan instead)
        -- Inside its footprint is not reach: the server routes out to the
        -- nearest face (under_target_routes_out), which the route below is
        -- (sa t322: a raider the red spawned on stood inside it for 12 ticks,
        -- planned as "attacking where it stands").
        if QD.raid._vzp2_gap(from.x, from.z, tg.x, tg.z, tg.size or 1) == 1 then return { from } end
        return QD.raid._vzp2_route(S, from, tg.x, tg.z, tg.size or 1)
    end
    return QD.raid._vzp2_route(S, from, order.x, order.z, 0)
end

-- tile_at[1..H]: `w` ticks of the order under way, then the plan's click.
function QD.raid._vzp2_simulate(S, F, plan)
    local path = { [0] = { x = F.me.x, z = F.me.z } }
    local cur = QD.raid._vzp2_order_ticks(S, F, path[0], S.order)
    local k = 1
    local cur_attack = S.order ~= nil and S.order.mode == "attack"
    while k <= QD.VZP2.H and k <= plan.w do
        local tl = (cur and cur[k]) or (cur and cur[#cur]) or path[k - 1]
        path[k] = { x = tl.x, z = tl.z, attacking = cur_attack, on = cur_attack and S.order.target.slot or nil }
        k = k + 1
    end
    -- the plan's click, then (if it has one) its second click w2 ticks later
    local clicks = { { at = plan.w, order = plan.order } }
    if plan.order2 then clicks[2] = { at = plan.w + plan.w2, order = plan.order2 } end
    for ci, cl in ipairs(clicks) do
        if k <= QD.VZP2.H then
            local stop = clicks[ci + 1] and clicks[ci + 1].at or QD.VZP2.H
            local ticks = QD.raid._vzp2_order_ticks(S, F, path[k - 1], cl.order)
            local j = 1
            local attacking = cl.order ~= nil and cl.order.mode == "attack"
            local on = attacking and cl.order.target.slot or nil
            while k <= QD.VZP2.H and k <= stop do
                local tl = (ticks and ticks[j]) or (ticks and ticks[#ticks]) or path[k - 1]
                path[k] = { x = tl.x, z = tl.z, attacking = attacking, on = on }
                k = k + 1
                j = j + 1
            end
        end
    end
    return path
end

-- The cost of a path, and (when impossible) the term that made it so.
function QD.raid._vzp2_score(S, F, terms, path)
    local total = 0
    for k = 1, QD.VZP2.H do
        local t = F.tick + k
        local tl = path[k]
        if QD.raid._vzp2_under(F, tl.x, tl.z) then return QD.VZP2.INF, "under her k" .. k end
        for i, term in ipairs(terms) do
            local c = term(tl.x, tl.z, t, k, path)
            if c >= QD.VZP2.INF then return QD.VZP2.INF, (terms.names[i] or "?") .. " k" .. k end
            total = total + c
        end
    end
    return total
end

function QD.raid._vzp2_same_order(a, b)
    if a == nil or b == nil then return a == b end
    if a.mode ~= b.mode then return false end
    if a.mode == "attack" then return a.target and b.target and a.target.slot == b.target.slot end
    return a.x == b.x and a.z == b.z
end

function QD.raid._vzp2_move(S, F, terms)
    local V = QD.VZP2
    local incumbent = { w = V.H + 1, order = nil }
    local inc_path = QD.raid._vzp2_simulate(S, F, incumbent)
    local inc_cost, inc_why = QD.raid._vzp2_score(S, F, terms, inc_path)
    S.inc_why = inc_why
    -- the orders worth a click: walks to tiles near me, to my side's two
    -- tiles, and an attack on my target
    local orders, waitable = {}, {}
    for dx = -2, 2 do
        for dz = -2, 2 do
            orders[#orders + 1] = { mode = "walk", x = F.me.x + dx, z = F.me.z + dz }
        end
    end
    local _, stand, back = QD.raid._vzp2_side(S, F)
    for _, t in ipairs({ stand, back }) do
        for dx = -1, 1 do
            for dz = -1, 1 do orders[#orders + 1] = { mode = "walk", x = t.x + dx, z = t.z + dz } end
        end
        -- a plan that waits and then clicks: only to my side's tiles and the
        -- attack (scythe walking is "stay, then step to back"); the budget
        waitable[#waitable + 1] = { mode = "walk", x = t.x, z = t.z }
    end
    if #S.blasts > 0 then
        -- a blast to leave: tiles four out in every direction
        for dx = -4, 4, 2 do
            for dz = -4, 4, 2 do orders[#orders + 1] = { mode = "walk", x = F.me.x + dx, z = F.me.z + dz } end
        end
    end
    if S.target then
        orders[#orders + 1] = { mode = "attack", target = S.target }
        waitable[#waitable + 1] = orders[#orders]
    end
    local best, best_cost, best_path = incumbent, inc_cost, inc_path
    -- two-click plans: in now, out (or back in) j ticks later -- scythe walking
    -- is "attack now, step to back before the scan", which no one click says
    local firsts = { { mode = "walk", x = stand.x, z = stand.z } }
    if S.target then firsts[2] = { mode = "attack", target = S.target } end
    local seconds = { { mode = "walk", x = back.x, z = back.z } }
    if S.target then seconds[2] = { mode = "attack", target = S.target } end
    for _, o1 in ipairs(firsts) do
        for j = 1, V.H - 1 do
            for _, o2 in ipairs(seconds) do
                if not QD.raid._vzp2_same_order(o1, o2) then
                    local plan = { w = 0, order = o1, w2 = j, order2 = o2 }
                    local path = QD.raid._vzp2_simulate(S, F, plan)
                    local cost, why = QD.raid._vzp2_score(S, F, terms, path)
                    cost = cost + 0.02
                    if S.snap_tick == F.tick and j <= 2 then
                        local pp = {}
                        for k = 1, #path do pp[#pp + 1] = path[k].x .. "," .. path[k].z .. (path[k].attacking and "a" or "") end
                        QD.raid._vzp2_trace(S, F.tick, string.format("cand o1 %s%s j%d o2 %s cost %.3f why %s inc %s path %s", o1.mode,
                            o1.x and (" " .. o1.x .. "," .. o1.z) or "", j, o2.mode, cost, tostring(why), tostring(S.inc_why), table.concat(pp, ">")))
                    end
                    local margin = (inc_cost < V.INF) and V.MARGIN or 0
                    if cost + margin < best_cost then best, best_cost, best_path = plan, cost, path end
                end
            end
        end
    end
    -- approach plans: walk to a tile beside an add, then attack it on
    -- arrival.  A bare attack takes the server's shortest route to reach,
    -- and beside her that is often under her (sa t323: the west red's east
    -- face is her footprint; the only attack plan was a stomp, so the raider
    -- waited 10 ticks for the geometry to change)
    if S.target and S.target ~= F.boss then
        local tg, n = S.target, S.target.size or 1
        for x = tg.x - 1, tg.x + n do
            for z = tg.z - 1, tg.z + n do
                if QD.raid._vzp2_beside(x, z, tg.x, tg.z, n) and not QD.raid._vzp2_under(F, x, z)
                    and not (x == F.me.x and z == F.me.z) then
                    local ticks = QD.raid._vzp2_route(S, F.me, x, z, 0)
                    if ticks and #ticks >= 1 and #ticks <= V.H - 1 then
                        local plan = { w = 0, order = { mode = "walk", x = x, z = z }, w2 = #ticks,
                                       order2 = { mode = "attack", target = tg } }
                        local path = QD.raid._vzp2_simulate(S, F, plan)
                        local cost = QD.raid._vzp2_score(S, F, terms, path) + 0.02
                        local margin = (inc_cost < V.INF) and V.MARGIN or 0
                        if cost + margin < best_cost then best, best_cost, best_path = plan, cost, path end
                    end
                end
            end
        end
    end
    for w = 0, V.WAITS do
        for _, order in ipairs(w == 0 and orders or waitable) do
            if not (order.mode == "walk" and QD.raid._vzp2_under(F, order.x, order.z)) then
                local plan = { w = w, order = order }
                local path = QD.raid._vzp2_simulate(S, F, plan)
                local ok = true
                -- the switch rule: a walk under way is not traded for one
                -- with another first step unless it has become impossible
                if inc_cost < V.INF and S.order and S.order.mode == "walk" and order.mode == "walk"
                    and w == 0 and (path[1].x ~= inc_path[1].x or path[1].z ~= inc_path[1].z) then
                    ok = false
                end
                if ok then
                    local cost = QD.raid._vzp2_score(S, F, terms, path) + w * 0.01
                    local margin = (inc_cost < V.INF) and V.MARGIN or 0
                    if cost + margin < best_cost then best, best_cost, best_path = plan, cost, path end
                end
            end
        end
    end
    return best, best_cost, best_path
end

-- ===================================================================== EMIT

-- A potion's doses, lowest first: the raid's br_ ones, then the plain ones
-- the ToB supply chest sells (enum_1952; relay rl9 bought brews no list knew).
function QD.raid._vzp2_doses(stem)
    local out = {}
    for _, pre in ipairs({ "br_", "" }) do
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", pre .. n .. stem)
            if r == "ok" then out[#out + 1] = id end
        end
    end
    assert(#out >= 4, "verzik_p2: no doses of " .. stem)
    return out
end

-- Super restores, then the chest's prayer potions (enum_1952).
function QD.raid._vzp2_restores()
    local out = QD.raid._vzp2_doses("dose2restore")
    for n = 1, 4 do
        local r, id = api_drive.symbol("obj", n .. "doseprayerrestore")
        if r == "ok" then out[#out + 1] = id end
    end
    return out
end

function QD.raid._vzp2_held(S, obj, op)
    for slot = 0, 27 do
        local r, cell = api_drive.inv_slot(S.ids.inv, slot)
        if r == "ok" and cell.obj_id == obj then
            return api_drive.inv_op(S.ids.backpack, slot, obj, cell.count, op)
        end
    end
    return "not_found"
end

function QD.raid._vzp2_emit(S, F, plan, intent)
    -- The panel channel: the prayer book to press a prayer (one step a tick:
    -- the panel, then the button), else the backpack.  It never holds up the
    -- interaction channel -- walking and attacking need no panel (si t407: a
    -- tick spent opening the prayer book used to send nothing at all, and the
    -- step off her scan never left; the slam landed).
    local switched = false
    if intent.pray then
        if S.tab ~= S.ids.prayer_tab then
            api_drive.tab(S.ids.prayer_tab)
            S.tab = S.ids.prayer_tab
            switched = true
        elseif F.tick - (S.pray_clicked or -10) >= 3 then
            -- a press toggles: one per three ticks, so a varbit still in
            -- flight is not answered with a second press that puts it out
            api_drive.if_click(S.ids[intent.pray], 1)
            S.pray_clicked = F.tick
            S.pray_presses[intent.pray] = (S.pray_presses[intent.pray] or 0) + 1
            QD.raid._vzp2_trace(S, F.tick, "pray " .. intent.pray)
        end
    elseif S.tab ~= S.ids.inv_tab then
        api_drive.tab(S.ids.inv_tab)
        S.tab = S.ids.inv_tab
        switched = true
    end
    -- held items (they end an interaction) only with the backpack already
    -- showing: the client refuses an op on a hidden panel
    if S.tab == S.ids.inv_tab and not switched then
        if intent.eat then
            QD.raid._vzp2_held(S, (intent.eat == true) and S.ids.food or intent.eat, 1)
            S.eats = S.eats + 1
            S.order = nil
        end
        if intent.drink then
            QD.raid._vzp2_held(S, intent.drink, 1)
            S.drinks = S.drinks + 1
            S.order = nil
        end
        if intent.gear then
            QD.raid._vzp2_held(S, intent.gear, 2)
            S.order = nil
        end
    end
    if plan.w == 0 and plan.order and not QD.raid._vzp2_same_order(plan.order, S.order) then
        local o = plan.order
        if o.mode == "walk" then
            api_drive.move_to(o.x, o.z)
        else
            local r = api_drive.world_op("npc", o.target.npc_id, 2, o.target.element_id)
            if r == "no_row" then api_drive.world_op("npc", o.target.npc_id, 1, o.target.element_id) end
        end
        S.order = o
        S.clicks = S.clicks + 1
    end
end

-- ===================================================================== LOOP

function QD.raid.verzik_p2_solve(opts)
    opts = opts or {}
    assert(opts.base, "verzik_p2_solve: opts.base (the room origin, verzik_p1_prepare's) is required")
    local V = QD.VZP2
    local S = {
        ids = QD.raid._vzp2_ids(opts.weapon),
        role = (QD_PARTY and QD_PARTY.role) or 1,
        floor = { x0 = opts.base.x + V.FLOOR.x0, x1 = opts.base.x + V.FLOOR.x1,
                  z0 = opts.base.z + V.FLOOR.z0, z1 = opts.base.z + V.FLOOR.z1 },
        hazards = {}, seen_proj = {}, crab_seen = {}, routes = {}, blasts = {}, recent = {},
        drinks = 0, attacks = 0, slams = 0, since_zap = 0, summons = 0, reds_out = false, reds_attacks = 0,
        trace = {}, hits = {}, eats = 0, clicks = 0, tab = nil, order = nil, pray_presses = {},
    }
    local start = QD.raid._vzp2_now()
    local last_hp = nil
    while true do
        local F = QD.raid._vzp2_measure(S)
        S.last_tick = F.tick
        -- the lane-parity wake trace (the first eighty wakes after her P2
        -- form): the server tick and whether this tick was decided already
        if S.entered and (S.wakes or 0) < 80 then
            S.wakes = (S.wakes or 0) + 1
            QD.raid._vzp2_trace(S, F.tick, string.format("wake srv=%s %s @%d,%d", tostring(select(2, api_drive.server_tick())),
                F.tick == S.decided_tick and "repeat" or "decide", F.me.x, F.me.z))
            S.decided_tick = F.tick
        end
        if F.tick - start > (opts.max_ticks or 600) then return "timeout", QD.raid._vzp2_summary(S), S end
        if F.hp <= 0 then return "died", QD.raid._vzp2_summary(S), S end
        if F.after then return "ok", QD.raid._vzp2_summary(S), S end
        if last_hp and F.hp < last_hp then
            S.hits[#S.hits + 1] = "t" .. F.tick .. " -" .. (last_hp - F.hp) .. " at " .. F.me.x .. "," .. F.me.z
            -- the decisions that led here, for the record
            if #S.hits <= 4 then
                QD.raid._vzp2_trace(S, F.tick, "HIT; last decisions: " .. table.concat(S.recent, " ; "))
            end
        end
        last_hp = F.hp
        local intent = {}
        S.routes = {}
        if F.boss == nil and F.before then
            -- she is flying in: her P2 tile is known (C: ^tob_verzik_arena_l[xz]),
            -- so take the side and the prayer before she lands
            F.boss = { x = opts.base.x + V.ARENA.x, z = opts.base.z + V.ARENA.z, size = V.SIZE, pred = true }
        end
        if F.boss then
            QD.raid._vzp2_clock(S, F)
            QD.raid._vzp2_projectiles(S, F)
            QD.raid._vzp2_crabs(S, F)
            S.target = QD.raid._vzp2_target(S, F)
            -- the lane-parity snapshot: everything the planner reads, on the
            -- tick the first crab is seen
            if S.snap_tick == nil and next(S.crab_seen) ~= nil then
                S.snap_tick = F.tick
                local b = F.boss
                local parts = { string.format("snap her %s,%s hp %s/%s seq %s@%s anim %s target %s absorb %s na %s reds_out %s summon %s absorb_until %s",
                    tostring(b and b.x), tostring(b and b.z), tostring(b and b.health_ratio), tostring(b and b.health_scale),
                    tostring(b and b.seq_id), tostring(b and b.seq_tick), tostring(b and b.anim_id),
                    S.target and (S.target == b and "her" or ("npc" .. tostring(S.target.slot))) or "nil", tostring(F.absorb),
                    tostring(S.next_attack), tostring(S.reds_out), tostring(S.next_summon), tostring(S.absorb_until)) }
                local sc = {}
                for t = F.tick, F.tick + 12 do if F.scans[t] then sc[#sc + 1] = tostring(t) end end
                local ht = {}
                for t = F.tick, F.tick + 12 do if F.heal_ticks[t] then ht[#ht + 1] = tostring(t) end end
                parts[#parts + 1] = "scans " .. table.concat(sc, ",") .. " heals " .. table.concat(ht, ",")
                for _, c in ipairs(F.crabs) do parts[#parts + 1] = string.format("crab s%s %s,%s hp %s", tostring(c.slot), c.x, c.z, tostring(c.health_ratio)) end
                for _, m in ipairs(F.mates) do parts[#parts + 1] = string.format("mate %s,%s", m.x, m.z) end
                parts[#parts + 1] = string.format("me %s,%s hp %s blasts %d hazards %d order %s terms %s", F.me.x, F.me.z, tostring(F.hp), #S.blasts, #(F.hazards or {}),
                    S.order and (S.order.mode .. (S.order.x and (" " .. S.order.x .. "," .. S.order.z) or (" s" .. tostring(S.order.target and S.order.target.slot)))) or "nil",
                    table.concat(QD.raid._vzp2_terms(S, F).names, ","))
                QD.raid._vzp2_trace(S, F.tick, table.concat(parts, " ; "))
            end
            -- overhead: Missiles for the bombs, Magic once the reds are out
            -- (the blood spell deals 0 under it)
            local want = S.reds_out and "magic" or "missiles"
            if not F.lit[want] then intent.pray = want
            elseif not F.lit.piety and (S.pray_presses.piety or 0) < 2 then
                -- Piety for the damage race; two refused presses and it is
                -- not this account's to use
                intent.pray = "piety"
            end
            -- the Athanatos needs a poisonous hit: the east raider wears the helm
            -- (retried until worn, every three ticks: a tick spent on the
            -- prayer panel sends no held op, p2e)
            if S.role == 3 and not F.helm_worn and F.tick - (S.helm_tried or -10) >= 3 and intent.pray == nil then
                intent.gear = S.ids.helm
                S.helm_tried = F.tick
            elseif F.weapon_held and F.tick - (S.weapon_tried or -10) >= 3 and intent.pray == nil then
                intent.gear = S.ids.weapon
                S.weapon_tried = F.tick
            end
            -- (a brew when the anglerfish are gone, as P3 does: the relay's
            -- chests sell brews, not fish, and with no fish a seat pressed
            -- an empty eat 49 times and died, relay rl6 seat 3)
            if F.hp < V.HP_EAT then
                local fish = nil
                for _, f in ipairs(S.ids.foods) do
                    local _, n = api_drive.inv_count(S.ids.inv, f)
                    if (n or 0) > 0 then fish = f break end
                end
                if fish then
                    intent.eat = fish
                elseif F.tick - (S.last_drink or -10) >= 3 then
                    for _, dose in ipairs(S.ids.brews) do
                        local _, n = api_drive.inv_count(S.ids.inv, dose)
                        if (n or 0) > 0 then intent.drink = dose break end
                    end
                    if intent.drink then S.last_drink = F.tick end
                end
            end
            -- prayer points: a super restore below the floor (the lowest dose
            -- first), one sip per three ticks (its own timer)
            if not intent.drink and F.prayer >= V.PRAYER_SIP and F.attack <= F.attack_base + V.COMBAT_REDOSE
                and F.tick - (S.last_drink or -10) >= 3 then
                for _, dose in ipairs(S.ids.combats) do
                    local _, n = api_drive.inv_count(S.ids.inv, dose)
                    if (n or 0) > 0 then intent.drink = dose break end
                end
                if intent.drink then S.last_drink = F.tick end
            end
            if F.prayer < V.PRAYER_SIP and F.tick - (S.last_drink or -10) >= 3 then
                for _, dose in ipairs(S.ids.restores) do
                    local _, n = api_drive.inv_count(S.ids.inv, dose)
                    if (n or 0) > 0 then intent.drink = dose break end
                end
                if intent.drink then S.last_drink = F.tick end
            end
            local terms = QD.raid._vzp2_terms(S, F)
            local plan, cost, ppath = QD.raid._vzp2_move(S, F, terms)
            if F.boss and not F.boss.pred and F.scans[F.tick + 1]
                and QD.raid._vzp2_gap(ppath[1].x, ppath[1].z, F.boss.x, F.boss.z, V.SIZE) <= 1 then
                QD.raid._vzp2_trace(S, F.tick, string.format("BESIDE AT SCAN: plan w%s %s cost %s k1 %d,%d next_attack %s",
                    tostring(plan.w), plan.order and plan.order.mode or "none", tostring(cost), ppath[1].x, ppath[1].z,
                    tostring(S.next_attack)))
            end
            for _, h in ipairs(F.hazards) do
                if h.x == F.me.x and h.z == F.me.z and (S.hz_logged or 0) < 12 then
                    S.hz_logged = (S.hz_logged or 0) + 1
                    local ends = {}
                    for e in pairs(h.ends) do ends[#ends + 1] = e end
                    QD.raid._vzp2_trace(S, F.tick, string.format("%s on me (ends %s): plan w%s %s cost %s k1 %d,%d inc %s",
                        h.kind, table.concat(ends, ","), tostring(plan.w), plan.order and plan.order.mode or "none",
                        tostring(cost), ppath[1].x, ppath[1].z, tostring(S.inc_why)))
                end
            end
            if cost >= V.INF and (S.last_inf or -10) < F.tick - 5 then
                S.last_inf = F.tick
                QD.raid._vzp2_trace(S, F.tick, "no safe plan at " .. F.me.x .. "," .. F.me.z .. " (" .. tostring(S.inc_why) .. ")")
            end
            QD.raid._vzp2_emit(S, F, plan, intent)
            S.recent[#S.recent + 1] = string.format("t%d @%d,%d %s w%s %s k1 %d,%d na%s scan%s%s",
                F.tick, F.me.x, F.me.z, plan == nil and "-" or "plan", tostring(plan.w),
                plan.order and (plan.order.mode .. (plan.order.x and (" " .. plan.order.x .. "," .. plan.order.z) or "")) or "keep",
                ppath[1].x, ppath[1].z, tostring(S.next_attack), F.scans[F.tick + 1] and "!" or "",
                intent.pray and (" pray:" .. intent.pray) or "")
            if #S.recent > 10 then table.remove(S.recent, 1) end
        end
        await({ event = "server_tick", match = function() return true end,
            note = "verzik_p2_solve: the tick's packets applied" }, 3)
    end
end

function QD.raid._vzp2_summary(S)
    return string.format("p%d: hits %s; attacks seen %d (slams %d), summons %d, clicks %d, eats %d, drinks %d; trace %s",
        S.role, #S.hits > 0 and table.concat(S.hits, " | ") or "none", S.attacks, S.slams, S.summons,
        S.clicks, S.eats, S.drinks, table.concat(S.trace, " | "))
end
