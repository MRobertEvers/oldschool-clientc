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
    SWITCH_GAIN = 30,          -- a walk under way yields to one better by this much
    FORMATION_GAP = 3,         -- the zap formation's tiles: 2..this from her,
    FORMATION_GAP_WIDE = 5,    -- or out to this when a blast covers all of those
    REDS_NEAR = 0.37,          -- her bar at or below this: any attack slot may be the first summon
    REDS_LEAVE = 0.15,         -- her bar at or below this at a summon: the set is not worth killing (a cap: the rate below decides)
    REDS_RATE_TICKS = 24,      -- her bar's fall over this many ticks is the kit's rate
    HEAL_SWING = 8,            -- a swing on her in the absorb window: worse than three idle ticks
    CRAB_LIFE = 25, CRAB_BLAST = 3, CRAB_SIZE = 2,
    CRAB_STOP = 2,             -- C: ^tob_verzik_crab_stop_range: within this of its raider it stops, and blows 2 ticks on
    CRAB_FLOOR = 7,            -- C: ^tob_verzik_crab_min_ticks: never before this many ticks old
    CRAB_IDLE = 3,             -- C: ^tob_verzik_crab_idle_ticks: still until this old
    CRAB_LOOK = 6,             -- how far ahead a walking crab's stop is predicted (the planner looks H)
    BLAST_FAR_COST = 150,      -- the 3-tile band is a flat 7 (C: ^tob_verzik_p2_nylo_blast_far): taken when nothing nearer is clear
    SIZE = 3,
    H = 5,                    -- horizon, ticks
    WAITS = 3,                -- a plan may wait up to this many ticks before its click
    MARGIN = 0.75,            -- a challenger must beat the plan under way by this
    INF = 1e9,
    RUN_PLAN_MIN = 300,       -- run energy below this (3%: a few running ticks) plans at a walk
    ARENA = { x = 31, z = 25 },                       -- her P2 south-west tile, local (C: ^tob_verzik_arena_l[xz])
    FLOOR = { x0 = 21, x1 = 43, z0 = 15, z1 = 32 },   -- local; the crab spawn constants' extent
    HP_EAT = 49,              -- above her biggest single hit, the 48 zap (slam 45, bomb 44): at 45 one zap killed (h12 sj)
    HP_EAT_CRAB = 75,         -- with a crab within CRAB_WARN: her hit and a crab's 26 band on one tick (h18 sd: 34 + 26 at 60)
    CRAB_WARN = 5,
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

function QD.raid._vzp2_ids(weapon)
    local function sym(kind, name)
        if kind == "npc" then name = QD.raid.tob_npc_name(name) end
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
        -- THE DEATHS, as a raider sees them: the death animation. The
        -- Athanatos, the reds and a crab that blows up die with no hitsplat
        -- (`npc_die`, owner 2026-10-11), so a bar at 0 or a death splat no
        -- longer says every death.
        death_seqs = { [sym("seq", "top_spider_melee_death")] = true, [sym("seq", "top_spider_ranged_death")] = true,
                       [sym("seq", "top_spider_magic_death")] = true, [sym("seq", "tob_spider_tank_death")] = true,
                       [sym("seq", "elemental_death")] = true },
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
        run_on = sym("varp", "varp173_option_run"), run_orb = comp("orbs:runbutton"),
        combats = QD.raid._vzp2_doses("dose2combat"),
        stat_restores = QD.raid._vzp2_doses("dose2restore"),
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
    -- P2's SECOND PART: from her reds on, the two super combat doses kept
    -- through the raid are drinkable (QD.raid.tob_reserve_combats, owner
    -- 2026-10-12)
    if (S.reds_out or (b and b.health_ratio and b.health_scale and b.health_scale > 0
        and b.health_ratio / b.health_scale <= V.REDS_NEAR)) and QD.raid.tob_combat_reserve > 0 then
        QD.raid.tob_reserve_combats(0)
        QD.raid._vzp2_trace(S, F.tick, "super combat reserve released")
    end
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

-- Crabs.  A crab that comes within 2 of its raider stops and BLOWS two ticks
-- after its last step (C: ~tob_verzik_crab_tick / _crab_die, Blert's timeline
-- in tob.constant), never before 7 ticks old: the blast reads where everyone
-- stands at the end of the tick after the stop. So the stop is the warning,
-- as it is for a player popping one: a crab within 2 of any raider is a blast
-- next tick (its owner is not on the wire, so any raider's), on its footprint
-- plus 3. Within 2 is the 63 and 26 bands; 3 is a flat 7 and is only priced,
-- because from beside it one tick of running reaches 3 and no further. A crab
-- the TEAM kills blasts from its death instead (its `[ai_queue3]`, two ticks
-- after the killing splat), as before.
-- A walking crab's predicted stop and blast: { x, z, from, to } or nil.
-- C: ~tob_verzik_crab_tick - still for its first ^tob_verzik_crab_idle_ticks,
-- then a tile a tick until it starts a tick within ^..._stop_range of its
-- raider, then the blast on the tick after (never before its floor age).
function QD.raid._vzp2_crab_stop(S, F, c, rec)
    local V = QD.VZP2
    local raiders = { { x = F.me.x, z = F.me.z } }
    for _, m in ipairs(F.mates or {}) do raiders[#raiders + 1] = { x = m.x, z = m.z } end
    local owner, best = nil, nil
    for _, r in ipairs(raiders) do
        local g = QD.raid._vzp2_gap(r.x, r.z, c.x, c.z, V.CRAB_SIZE)
        if best == nil or g < best then owner, best = r, g end
    end
    local x, z = c.x, c.z
    for k = 1, (S.hm and S.hm.crab_look) or V.CRAB_LOOK do
        local t = F.tick + k
        if t - rec.born > V.CRAB_IDLE and QD.raid._vzp2_gap(owner.x, owner.z, x, z, V.CRAB_SIZE) <= V.CRAB_STOP then
            local at = math.max(t, rec.born + V.CRAB_FLOOR - 1)
            return { x = x, z = z, from = at, to = at + 1 }
        end
        if t - rec.born > V.CRAB_IDLE then
            -- its 2x2 toward the raider's tile, a step on each axis that is off
            if owner.x < x then x = x - 1 elseif owner.x > x + V.CRAB_SIZE - 1 then x = x + 1 end
            if owner.z < z then z = z - 1 elseif owner.z > z + V.CRAB_SIZE - 1 then z = z + 1 end
        end
    end
    return nil
end

function QD.raid._vzp2_crabs(S, F)
    local V = QD.VZP2
    S.kill_blasts = S.kill_blasts or {}
    local stops = {}
    for _, c in ipairs(F.crabs) do
        local rec = S.crab_seen[c.slot]
        if rec == nil then
            rec = { born = F.tick }
            S.crab_seen[c.slot] = rec
            QD.raid._vzp2_trace(S, F.tick, "crab at " .. c.x .. "," .. c.z)
        end
        -- KILLED: its bar at 0 or its death splat (nothing else hits a crab);
        -- the blast follows the death. Death on D, blast damage on D+4 (tick
        -- log), so the blast reads the end of D+2: two ticks of running. A
        -- window from D+1 made every escape impossible (p2n t299).
        if rec.died == nil and (c.health_ratio == 0 or (c.hit_damage ~= nil and c.hit_damage > 0)) then
            rec.died = F.tick
            S.kill_blasts[#S.kill_blasts + 1] = { x = c.x, z = c.z, from = F.tick + 2, to = F.tick + 3 }
            QD.raid._vzp2_trace(S, F.tick, "crab killed at " .. c.x .. "," .. c.z)
        end
        -- DETONATED: its death animation with no splat; its blast has gone off
        if rec.died == nil and S.ids.death_seqs[c.seq_id] then rec.died = F.tick end
        -- ITS STOP, read afresh every tick where the crab stands NOW: within 2
        -- of a raider it blows at the end of the next tick (or at its floor
        -- age, if later), two ticks wide because its age is counted from the
        -- tick I first saw it, which can trail its spawn by one (rf t3223). Not
        -- latched: in P3 they hatch beside a raider and walk on to another, and
        -- a latch left the warning on the spawn tile (tl t3825, 63 at gap 1).
        if rec.died == nil then
            local near = QD.raid._vzp2_gap(F.me.x, F.me.z, c.x, c.z, V.CRAB_SIZE) <= V.CRAB_STOP
            for _, m in ipairs(F.mates or {}) do
                if QD.raid._vzp2_gap(m.x, m.z, c.x, c.z, V.CRAB_SIZE) <= V.CRAB_STOP then near = true end
            end
            if near then
                local at = math.max(F.tick + 1, rec.born + V.CRAB_FLOOR - 1)
                stops[#stops + 1] = { x = c.x, z = c.z, from = at, to = at + 1 }
            else
                -- AND WHERE IT WILL STOP (owner, 2026-10-11: look further ahead
                -- so a raider is not trapped). One tile a tick toward its
                -- raider - the nearest one, its owner not being on the wire -
                -- read as standing still; the blast where it stops, so the
                -- planner leaves room to step away from it before it arrives
                -- rather than in the one tick after (h18 sd: pinned between
                -- it and her slam, 26 + her 34 at 60).
                local stop = QD.raid._vzp2_crab_stop(S, F, c, rec)
                if stop then stops[#stops + 1] = stop end
            end
        end
    end
    local keep = {}
    for _, bl in ipairs(S.kill_blasts) do if bl.to >= F.tick then keep[#keep + 1] = bl end end
    S.kill_blasts = keep
    S.blasts = {}
    for _, bl in ipairs(keep) do S.blasts[#S.blasts + 1] = bl end
    for _, bl in ipairs(stops) do S.blasts[#S.blasts + 1] = bl end
end

-- =================================================================== FLOOR
--
-- Walls are the client's: every walk is api_drive.route, the client's own
-- pathfinder over its collision map (the server's flood), so a plan's tiles
-- are the tiles the server will walk.  Her footprint is not a wall to a
-- player (players walk through npcs) but standing in it is the stomp, so it
-- is a cost, below.

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

-- The seats, and which of them take the east side (the zap formation's
-- single tile, the east red): role 3 of Normal's three; a mode's own split
-- (raid_solve_verzik_hm.lua: Hard Mode's five, three west and two east).
function QD.raid._vzp2_seats(S)
    return (S.hm and S.hm.seats) or 3
end

function QD.raid._vzp2_east(S, role)
    if S.hm and S.hm.east then return S.hm.east[role] == true end
    return role == 3
end

-- Which red is mine: the eastmost for the east seats (a mode's own split for
-- the reds phase, where no zap holds the formation: raid_solve_verzik_hm.lua).
function QD.raid._vzp2_red_east(S, role)
    if S.hm and S.hm.red_east then return S.hm.red_east[role] == true end
    return QD.raid._vzp2_east(S, role)
end

-- My side.  Two raiders stack west, one east, so every lightning hop crosses
-- her; once the reds are out (no lightning) they spread west, south, east
-- (the blood spell's 3x3).  "stand" is the melee tile, "back" one off it.
function QD.raid._vzp2_side(S, F)
    local b = F.boss
    local x0, z0 = b.x, b.z
    local side = QD.raid._vzp2_east(S, S.role) and "east" or "west"
    if S.reds_out and S.role == 2 then side = "south" end
    local stand, back
    if side == "north" then stand, back = { x = x0 + 1, z = z0 + 3 }, { x = x0 + 1, z = z0 + 4 }
    elseif side == "west" then stand, back = { x = x0 - 1, z = z0 + 1 }, { x = x0 - 2, z = z0 + 1 }
    elseif side == "east" then stand, back = { x = x0 + 3, z = z0 + 1 }, { x = x0 + 4, z = z0 + 1 }
    else stand, back = { x = x0 + 1, z = z0 - 1 }, { x = x0 + 1, z = z0 - 2 } end
    -- a mode's own tiles (raid_solve_verzik_hm.lua: Hard Mode's pools take
    -- the back tile the scythe walk stepped out to)
    if S.hm and S.hm.side then stand, back = S.hm.side(S, F, side, stand, back) end
    return side, stand, back
end

-- THE ZAP FORMATION: where each seat stands at the lightning's scan tick.
--
-- The ball's first hop goes from a random raider to the nearest raider on
-- another tile, and it is absorbed when that hop's midpoint is under her. Two
-- tiles mirrored through her centre have their midpoint ON her centre, so: two
-- seats stacked on one tile, the third on its mirror, and every possible first
-- hop crosses her. At least two tiles out, because her attack scan makes
-- anything within one of her lethal on that very tick (the "scan" term) -- the
-- reds-phase spread's stand tiles are exactly that, so a spread can never be
-- the formation, and at two out a three-way spread's diagonal pairs halve to
-- a midpoint off her.
--
-- It ADAPTS: a red's blast or a cabbage on the preferred pair moves the whole
-- pair (rl47 rg t2853: a blast held the stack off its back tile, and the ball
-- bounced (-3,-1) <-> (0,-1) for all four hops). Every seat computes the same
-- pair from what all three see on the same tick -- her tile, the hazards, the
-- blasts, the three seats' tiles -- so no message is needed: roles 1 and 2
-- take the pair's first tile, role 3 its mirror. -> my tile, or nil.
function QD.raid._vzp2_formation(S, F)
    if not F.zap_scan or not F.boss then return nil end
    if S.formation_tick == F.tick then return S.formation end
    S.formation_tick, S.formation = F.tick, nil
    local V, b = QD.VZP2, F.boss
    local seats = { [S.role] = F.me }
    for r = 1, QD.raid._vzp2_seats(S) do
        if r ~= S.role then
            local nm = QD.party.name(r)
            for _, m in ipairs(F.mates) do
                if nm and m.name and QD.party._same(m.name, nm) then seats[r] = m end
            end
        end
    end
    -- WHO IS IN THE FIGHT: a dead raider's seat is in the spectator cage,
    -- off the floor. With two left, the two take one tile each, never a
    -- stack: stacked, the ball has no hop and every zap is a full shock on
    -- them (rl56 rm: 21 one-leg zaps after a death). The lower role takes
    -- the pair's first tile.
    local fl = S.floor
    local alive = {}
    for r = 1, QD.raid._vzp2_seats(S) do
        local st = seats[r]
        if st and st.x >= fl.x0 and st.x <= fl.x1 and st.z >= fl.z0 and st.z <= fl.z1 then alive[#alive + 1] = r end
    end
    if #alive < 2 then return nil end
    local first, mirror = {}, {}
    for r = 1, QD.raid._vzp2_seats(S) do
        if QD.raid._vzp2_east(S, r) then mirror[r] = true else first[r] = true end
    end
    if #alive == 2 then first, mirror = { [alive[1]] = true }, { [alive[2]] = true } end
    if not first[S.role] and not mirror[S.role] then return nil end
    local t, left = F.zap_scan, math.max(0, F.zap_scan - F.tick)
    local cx, cz = b.x + 1, b.z + 1
    local reach = V.FORMATION_GAP
    local blocked = {}
    local function clear(x, z)
        local g = QD.raid._vzp2_gap(x, z, b.x, b.z, V.SIZE)
        if g < 2 or g > reach then return false end
        local k = x * 4096 + z
        local hit = blocked[k]
        if hit == nil then
            hit = false
            for _, h in ipairs(F.hazards) do
                if (h.ends[t] or h.ends[t + 1]) and h.x == x and h.z == z then hit = true end
            end
            for _, bl in ipairs(S.blasts) do
                if t >= bl.from and t <= bl.to and QD.raid._vzp2_gap(x, z, bl.x, bl.z, V.CRAB_SIZE) <= V.CRAB_BLAST then
                    hit = true
                end
            end
            blocked[k] = hit
        end
        return not hit
    end
    local function far(r, x, z)
        local seat = seats[r]
        if seat == nil or not (first[r] or mirror[r]) then return 0 end
        return QD.raid._vzp2_cheb(seat.x, seat.z, x, z)
    end
    -- the default pair first (the stack west, role 3 east: what the seats are
    -- already standing round), then every other pair, in a fixed order
    local cands = { { x = b.x - 2, z = b.z + 1 } }
    for dx = -V.FORMATION_GAP_WIDE, V.FORMATION_GAP_WIDE + V.SIZE - 1 do
        for dz = -V.FORMATION_GAP_WIDE, V.FORMATION_GAP_WIDE + V.SIZE - 1 do
            cands[#cands + 1] = { x = b.x + dx, z = b.z + dz }
        end
    end
    -- LATCHED for its scan tick: re-chosen every tick from where the seats had
    -- got to, the pair flipped corner to corner as they moved and nobody
    -- arrived (rg t2801-t2804). Kept while both its tiles stay clear; every
    -- seat latches on the same tick from the same reading, so they agree.
    local latch = S.formation_latch
    reach = V.FORMATION_GAP_WIDE
    if latch and latch.t == t and latch.alive == #alive
        and clear(latch.stack.x, latch.stack.z) and clear(latch.single.x, latch.single.z) then
        S.formation = mirror[S.role] and latch.single or latch.stack
        return S.formation
    end
    local best, best_cost = nil, nil
    -- (the near pairs first; further out only when a red's blast has covered
    -- every one of them -- then the seats fled to ONE safe tile together and
    -- the ball had no hop, rl54 rg t2884-t2889. Further out for every zap
    -- drew the seats to far pairs and onto each other: 36 one-leg zaps, rl56)
    for _, gap in ipairs({ V.FORMATION_GAP, V.FORMATION_GAP_WIDE }) do
        reach = gap
        for i, pt in ipairs(cands) do
            local qx, qz = 2 * cx - pt.x, 2 * cz - pt.z
            if clear(pt.x, pt.z) and clear(qx, qz) then
                local to_p, to_q, travel = 0, 0, 0
                for r = 1, QD.raid._vzp2_seats(S) do
                    local d = first[r] and far(r, pt.x, pt.z) or far(r, qx, qz)
                    if first[r] then to_p = to_p + d else to_q = to_q + d end
                    if d > travel then travel = d end
                end
                local late = math.max(0, travel - 2 * left)
                local cost = late * 100 + travel * 2 + to_p + to_q + (i == 1 and -1 or 0)
                if best_cost == nil or cost < best_cost then
                    best_cost, best = cost, { stack = { x = pt.x, z = pt.z }, single = { x = qx, z = qz } }
                end
            end
        end
        if best then break end
    end
    if best == nil then
        return nil
    end
    best.t, best.alive = t, #alive
    S.formation_latch = best
    S.formation = mirror[S.role] and best.single or best.stack
    if S.formation_traced ~= t then
        S.formation_traced = t
        QD.raid._vzp2_trace(S, F.tick, string.format("zap formation for t%d: stack %d,%d single %d,%d",
            t, best.stack.x - b.x, best.stack.z - b.z, best.single.x - b.x, best.single.z - b.z))
    end
    return S.formation
end

-- ================================================================== ARBITER
--
-- A plan: wait `w` ticks on what I am already doing, then one click (a walk
-- to `dest`, or an attack on `target`).  Simulated two tiles a tick into
-- tile_at[1..H] and scored over the horizon.  The plan under way ("no click")
-- is the incumbent; a challenger must beat it by the margin, and a walk whose
-- first step differs from a walk under way is taken only when the incumbent
-- is impossible (the design note's switch rule: no oscillation).

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

-- ===================================================================== LOOP
