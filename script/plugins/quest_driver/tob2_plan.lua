-- quest-driver / tob2_plan: THE SPEC BUILDER over api_drive.aplan (ToB v2).
--
-- docs/minigames/theater_of_blood/TOB2_DESIGN.md is the spec. A room's
-- PREDICT and STRATEGY put their terms into a builder B; B.plan() hands the
-- spec to the C action planner (src/engine/world_builder/action_plan.c,
-- verb api_drive.aplan) and keeps the answer, the why decoded into the
-- term's own name.
--
-- Ticks (R1-R5, ORACLE_RULES.md): F.tick = d is the tick whose END I see;
-- plan step k is my tile at the end of d+k. A hit's window [t0, t1] names
-- the tick-ENDS on which my tile is read; its damage lands `land` ticks
-- after the read (default 1: "a hit on T reads the end of T-1"), and the
-- prayer that counts is the one in force on the landing tick (R3), a press
-- decided now being in force from now+1 (R5).
--
-- Every list is capped at the C side's size and an overflow is an ASSERT:
-- a threat silently left out is a tile planned as safe that is not
-- (CLAUDE.md: loud failure).
--
-- ============================================================ THE API (rooms)
--
-- A ROOM (tob2_run.lua): QD.T2.register(name, room) with room = {
--   ids(opts)?, init(S, opts), enter(S), predict(S, F, B),
--   strategy(S, F, B) -> intents, done(S, F) -> "ok"|nil, summary(S)? }
--   and QD.raid.<name>_v2_solve(opts) -> result, detail, S comes with it.
--   opts read by the loop: h, beam, eat_below, brew_below, boost, boost_stat,
--   swing_seqs = {seq...} (my swing animations; default: any start but
--   human_eat).
--
-- THE BUILDER B = QD.T2.builder(S, F, opts) (the loop makes it). Every term
-- takes an optional `name` (the why-trace's: B.result.why_name "pool_3 k2").
-- Damage is given UNPRAYED with an optional `style` "magic"|"ranged"|"melee"
-- and `factor`; B.plan applies the prayer in force at the landing tick.
--   B.hit{name, x0,z0,x1,z1 | x,z,size, t0, t1, sample="end"|"pass",
--         dmg_exp, dmg_max, stun, forbid, style, factor, land=1, read=land}
--         [t0, t1] = the tick-ENDS my tile is read; lands `land` ticks later
--         (the monitor's threat rule matches hp lost on that tick); the
--         overhead is READ `read` ticks after the sample (Nylocas: at the
--         swing, read=1 with a later land; Verzik P3: at landing, read=land).
--         Give it while it still lands >= now+1 (the monitor reads it).
--   B.mark{name, t_mark, r, t0, t1, sample, dmg_exp, dmg_max, stun, forbid,
--          style, factor, land, read}   lands within r of my end-of-t_mark tile;
--          a t_mark already past is sent as a hit on the tile I stood on.
--   B.watcher{name, x, z, size, t0, t1, dmg_exp, dmg_max, stun, forbid,
--             style, factor, land, read}   (sent only when given: the new aplan)
--   B.target{name, npc=row, range, speed, value, size, path={{x,z},...}
--            (k = 0..h; default: hold the row's tile), t_ok0, t_ok1,
--            needs = {gear = {obj...}, unequip = {component...}, cast = true},
--            hp, hit | swings_max}
--            hp/hit: the kill limit, swings_max = ceil(hp / hit) (expected
--            damage a swing), sent to aplan (an older binary ignores it).
--            spec_ok = true: a target the shared spec channel may dump a
--            special on (the pre-P3 hammer; melee targets only), budget
--            permitting (tob2_budget.lua SPECIAL ENERGY).
--            -> index. `needs`: what must be true before a swing at it can
--            go out; B.plan raises t_ok0 to the executive's readiness tick
--            (QD.T2.exec.ready_tick: one panel a tick, the cast clock).
--   B.held{name, t, ends_attack=true, swing_delay, heal}
--   B.chaser{name, x, z, dmg_exp, dmg_max, forbid}   (forbid: a touch is a rule, no hp charged)
--   B.zone{...}, B.pull{...}, B.goal{...}, B.edge{...}   (api_drive.plan's, hp)
--   B.prayer_factor = {magic=0, ranged=0, melee=0}   (room-settable)
--   B.prayed(style, t, factor) -> 0..1;  B.pray_at(name, t);  B.unpray_at(name, t);
--   B.overhead_at(t)
--   B.landing_damage(t, step1) -> exp, max
--   B.plan() -> B.result (the loop calls it; rooms do not)
--   B.replan(extra_held, drop_names) -> res;  B.adopt(res)   (<= 2 aplan a tick)
-- B.result = { path = {{x, z, mid, act="stay"|"walk"|"attack", tgt, swing}},
--   risk, loss, soft, value, hp_worst, why, why_name, expanded, rows (target
--   index -> npc row), tmeta (index -> {name, speed, value, range, ready}),
--   targets (the spec's), land_exp, land_max }
--
-- INTENTS (strategy's answer; the executive, tob2_exec.lua):
--   pray, pray_force, gear = {obj | {obj, two_hand}}, unequip = {component},
--   pray_off = {name...}   prayers wanted UNLIT (Bloat's Piety while hiding):
--           pressed while lit, E7's gap, pending until seen unlit; shares the
--           prayer book with a press on that tick, else yields the panel to
--           a held op; a protect prayer turned off is in B.prayed from now+1,
--   world_op = {kind = "loc"|"obj"|"npc", id|symbol (or [kind], [1]),
--           op = 1, x, z, element}   an interaction like a walk (ends an
--           attack; overrides the plan's step and a cast): the server walks
--           me to the copy on (x, z) (or the named element, or the type
--           alone) and runs the op -- a loc's, a ground item's Take (obj op
--           3), an npc's; re-sent when it changes, after a held op, after
--           PATH_STALL ticks unmoved short of it (an obj's own tile, else
--           beside); the monitor's `stall` checks the approach. Use it
--           instead of api_drive.world_op from strategy.
--   loc_op = {loc = id|symbol, op, x, z}   the same, kind "loc" (alias)
--   (the resource channel also RESCUES: when the plan's risk > 0 with
--    hp_worst <= 0 it re-plans with the best food + a brew and adopts that
--    only if the risk falls; summary "rescues asked/taken/no-gain")
--   spec, drop, eat, drink (else the resource channel's), eat_below,
--   brew_below, mode (the trace's),
--   cast = {component, npc?, tgt? (the target index), ready? (false holds
--           it), cooldown? (the old clock gate; CAST_HOLD still works)}
--   A cast goes out when the plan stands and: the cast clock allows
--   (cooldown, default CAST_COOLDOWN), ready ~= false, and -- when the plan's
--   step 1 attacks the cast's own target (cast.tgt) -- path[1].swing.
--
-- PANELS (E9): V.TAB_SETTLES = true -- every channel switches its tab and
-- acts in the same tick (prayer, backpack, worn, spellbook, in the send
-- order); false restores one panel a tick with the backpack a tick late.
--
-- STATE a room may read: S.x2 (the channels: interact.cast_sent, held,
-- prayer, swing_next = the weapon clock), S.mon (the monitor), S.t2.want,
-- S.expect.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}

QD.T2.V = {
    H = 12, BEAM = 32, H_MAX = 16, BEAM_MAX = 64,
    CLICK_COST = 0.05, MOVE_COST = 0.01,
    -- the C side's caps (TOB2_DESIGN.md "Spec")
    TARGETS_MAX = 6, HITS_MAX = 512, MARKS_MAX = 8, HELD_MAX = 8, CHASERS_MAX = 4,
    ZONES_MAX = 48, PULLS_MAX = 8, WATCHERS_MAX = 128,
    LANDING_MAX = 4096,          -- hits kept for the monitor only (read window passed)
    T_OK_FOREVER = 1000000,      -- a target with no t_ok1 accepts swings this far on
    -- the executive (tob2_exec.lua)
    PATH_STALL = 2,              -- E5: a walk not taken is re-sent after this many unmoved ticks
    RUN_PLAN_MIN = 300,          -- E10/M2: a plan runs only with this much run energy
    RUN_ON_AT = 500, RUN_GAP = 3,-- E10: run back on at >= 500, at most every 3 ticks
    PRAY_GAP = 2,                -- E7: a prayer toggle never twice inside 2 ticks unless forced
    GEAR_GAP = 2,                -- E11: gear re-sent at most every 2 ticks unless the set changed
    SPEC_GAP = 2,                -- E8: the spec orb toggle, at most every 2 ticks
    CAST_COOLDOWN = 5,           -- E2: the cast clock (ancient barrage), when the room gives none
    EAT_GAP = 3, DRINK_GAP = 3,  -- a food / a potion every 3 ticks
    RESCUE_RUN = 2,              -- consecutive ticks of rescue while the plan's hp risk persists
    SPEC_VALUE = 2,              -- a spec_ok target's swing value while a dump is planned (the hammer's special)
    TAB_SETTLES = true,          -- E9: a tab switch settles in its call (tob2_exec panel)
    -- THE HELD OPS' EFFECTS (content: player/scripts/consumption/consume_shared.rs2).
    -- Food and potions keep separate 3-tick timers, so a food and a brew go
    -- in the same tick (combo). Food adds FOOD_ATTACK_DELAY to the weapon
    -- delay ONLY while it is still running (`~consume_attack_delay`: "if your
    -- weapon is ready to attack again then eating does not add any new
    -- delay"); potions and wields add none. Heals: anglerfish
    -- stat_boost(hitpoints, 0, 22) = 22% of base (overheals); a Saradomin
    -- brew stat_boost(hitpoints, 2, 15) = 2 + 15% of base; other food
    -- FOOD_HEAL. The monitor's `held` rule checks the attack delay.
    FOOD_ATTACK_DELAY = 3, FOOD_HEAL = 20, ANGLER_PCT = 22, BREW_ADD = 2, BREW_PCT = 15,
    -- traces
    MON_TRACE = 20, RECENT = 600,
}

-- The protection overhead for a hit style.
QD.T2.PROTECT = { magic = "protectfrommagic", ranged = "protectfrommissiles", melee = "protectfrommelee" }

-- An npc row's identity across ticks (the element the world op addresses).
function QD.T2.npc_key(row)
    assert(row)
    local k = row.element_id
    if k == nil then k = row.slot end
    assert(k ~= nil, "tob2: an npc row with neither element_id nor slot")
    return k
end

-- A tile as {x=, z=} from {x, z} or {x=, z=}.
function QD.T2.tile(t)
    assert(t)
    local x, z = t.x, t.z
    if x == nil then x, z = t[1], t[2] end
    assert(math.type(x) == "integer", "tob2: a tile with no integer x")
    assert(math.type(z) == "integer", "tob2: a tile with no integer z")
    return { x = x, z = z }
end

-- My tile at the end of tick t, from the run's history (nil when unknown).
function QD.T2.tile_at(S, t)
    return S.t2 and S.t2.tiles and S.t2.tiles[t] or nil
end

-- ================================================================ BUILDER

-- B = QD.T2.builder(S, F, opts); opts = { h, beam, click_cost, move_cost,
-- run (forces the run flag), edge }.
function QD.T2.builder(S, F, opts)
    assert(S)
    assert(F)
    opts = opts or {}
    local V = QD.T2.V
    local now = F.tick
    local h = opts.h or V.H
    local beam = opts.beam or V.BEAM
    assert(h >= 1 and h <= V.H_MAX, "tob2 builder: h is 1..16")
    assert(beam >= 1 and beam <= V.BEAM_MAX, "tob2 builder: beam is 1..64")
    -- (E10/M2: a plan runs only on >= RUN_PLAN_MIN run energy with run on)
    local run = opts.run
    if run == nil then run = (F.run_on == true) and (F.run_energy or 0) >= V.RUN_PLAN_MIN end
    local B = {
        S = S, F = F, now = now, h = h,
        spec = {
            from = { x = F.me.x, z = F.me.z }, now = now, h = h, beam = beam, run = run, hp = F.hp,
            click_cost = opts.click_cost or V.CLICK_COST, move_cost = opts.move_cost or V.MOVE_COST,
            targets = {}, hits = {}, marks = {}, held = {}, chasers = {}, zones = {}, pulls = {},
            edge = opts.edge,
        },
        names = { target = {}, hit = {}, mark = {}, held = {}, chaser = {}, zone = {}, pull = {}, watcher = {} },
        tneeds = {},          -- target index -> needs (B.plan's readiness)
        watchsrc = {},        -- spec watcher index -> the room's watcher (style)
        intents = nil,        -- this tick's intents (QD.T2.schedule sets them)
        rows = {},            -- target index -> npc row
        tmeta = {},           -- target index -> { speed, value, range, name }
        hitsrc = {},          -- spec hit index -> the room's hit (style, land)
        marksrc = {},         -- spec mark index -> the room's mark
        landing = {},         -- every hit / mark given, for the monitor's `threat` rule
        -- protect prayer factors for npc autos (0 = a protect prayer blocks it);
        -- a ToB boss often halves instead: the room sets these, or `factor` per hit
        prayer_factor = { magic = 0, ranged = 0, melee = 0 },
        pray_sched = {},      -- { {t, name} } overhead changes, sorted by t
        lit_overhead = nil,   -- the protect prayer lit at the end of now
    }
    for _, name in pairs(QD.T2.PROTECT) do
        if F.lit and F.lit[name] then B.lit_overhead = name end
    end

    local function named(kind, name, idx)
        B.names[kind][idx] = name or (kind .. idx)
    end

    -- THE PRAYER SCHEDULE. R5: a press decided after seeing `now` is in force
    -- from now+1; R3: the prayer at a swing on S is the one in force at S.
    function B.pray_at(name, t)
        assert(name)
        assert(t > now, "tob2 pray_at: a press is in force from now+1 at the earliest (R5)")
        B.pray_sched[#B.pray_sched + 1] = { t = t, name = name }
        table.sort(B.pray_sched, function(a, b) return a.t < b.t end)
    end

    -- The protect overhead in force at absolute tick t (nil = none).
    -- A prayer turned OFF from tick t (intents.pray_off; R5 as a press on).
    function B.unpray_at(name, t)
        assert(name)
        assert(t > now, "tob2 unpray_at: a press is in force from now+1 at the earliest (R5)")
        B.pray_sched[#B.pray_sched + 1] = { t = t, name = name, off = true }
        table.sort(B.pray_sched, function(a, b) return a.t < b.t end)
    end

    function B.overhead_at(t)
        -- a tick already seen: the overhead that was lit at its end (R3: the
        -- prayer at a swing on S is the one in force at S)
        if t <= now and S.t2 and S.t2.overheads and S.t2.overheads[t] ~= nil then
            local o = S.t2.overheads[t]
            if o == false then return nil end
            return o
        end
        local name = B.lit_overhead
        for _, e in ipairs(B.pray_sched) do
            if t >= e.t and e.off then
                if name == e.name then name = nil end
            elseif t >= e.t then
                -- an overhead press lights it and puts out the other overheads;
                -- a non-overhead press leaves the overhead as it was
                for _, pn in pairs(QD.T2.PROTECT) do
                    if pn == e.name then name = e.name end
                end
            end
        end
        return name
    end

    -- The factor a hit of `style` ("magic"|"ranged"|"melee"|nil) gets at
    -- absolute tick t from the prayer the executive has lit / will have lit;
    -- `factor` overrides B.prayer_factor[style] for this hazard.
    function B.prayed(style, t, factor)
        if style == nil then return 1 end
        local want = QD.T2.PROTECT[style]
        assert(want, "tob2 prayed: no hit style " .. tostring(style))
        if (F.prayer or 0) <= 0 then return 1 end
        if B.overhead_at(t) ~= want then return 1 end
        local f = factor
        if f == nil then f = B.prayer_factor[style] end
        assert(f, "tob2 prayed: no prayer factor for " .. style)
        return f
    end

    -- A hit: { name, x0, z0, x1, z1 | x, z, size, t0, t1, sample = "end"|"pass",
    -- dmg_exp, dmg_max, stun, forbid, style, factor, land }. dmg_* are
    -- UNPRAYED; B.plan applies B.prayed at the landing tick. Register a hit
    -- as long as it lands on or after now+1, even when its read window has
    -- passed: the builder keeps it out of the spec and gives it to the monitor.
    function B.hit(h0)
        assert(h0)
        local x0, z0, x1, z1 = h0.x0, h0.z0, h0.x1, h0.z1
        if x0 == nil then
            assert(h0.x ~= nil, "tob2 hit " .. tostring(h0.name) .. ": no x0 and no x")
            assert(h0.z ~= nil, "tob2 hit " .. tostring(h0.name) .. ": no z")
            local n = h0.size or 1
            x0, z0, x1, z1 = h0.x, h0.z, h0.x + n - 1, h0.z + n - 1
        end
        assert(x1 >= x0, "tob2 hit " .. tostring(h0.name) .. ": x1 < x0")
        assert(z1 >= z0, "tob2 hit " .. tostring(h0.name) .. ": z1 < z0")
        local t0 = h0.t0
        assert(math.type(t0) == "integer", "tob2 hit " .. tostring(h0.name) .. ": no t0")
        local t1 = h0.t1 or t0
        assert(t1 >= t0, "tob2 hit " .. tostring(h0.name) .. ": t1 < t0")
        local sample = h0.sample or "end"
        assert(sample == "end" or sample == "pass", "tob2 hit: sample is end|pass")
        local forbid = h0.forbid == true
        local dmg_exp = h0.dmg_exp or 0
        local dmg_max = h0.dmg_max or dmg_exp
        local land = h0.land or 1
        -- the prayer is READ `read` ticks after the tile-end sample (default:
        -- at landing). Nylocas projectiles are prayed at the swing and land
        -- later (oracle P2); Verzik P3 autos are prayed at landing (P4).
        local read = h0.read or land
        local rec = { name = h0.name, x0 = x0, z0 = z0, x1 = x1, z1 = z1, t0 = t0, t1 = t1, sample = sample,
                      dmg_exp = dmg_exp, dmg_max = dmg_max, stun = h0.stun or 0, forbid = forbid,
                      style = h0.style, factor = h0.factor, land = land, read = read }
        assert(#B.landing < V.LANDING_MAX, "tob2: more than " .. V.LANDING_MAX .. " hits in one tick")
        B.landing[#B.landing + 1] = rec
        return B.spec_hit(rec)
    end

    -- (internal) a hit record into the spec when it is in the horizon:
    -- tile ends now+1 .. now+h. -> its index or nil.
    function B.spec_hit(rec)
        if rec.t1 < now + 1 or rec.t0 > now + B.h then return nil end
        assert(#B.spec.hits < V.HITS_MAX, "tob2: more than " .. V.HITS_MAX .. " hits (" .. tostring(rec.name) .. ")")
        local i = #B.spec.hits + 1
        B.spec.hits[i] = { x0 = rec.x0, z0 = rec.z0, x1 = rec.x1, z1 = rec.z1, t0 = rec.t0, t1 = rec.t1,
                           sample = rec.sample, dmg_exp = rec.dmg_exp, dmg_max = rec.dmg_max, stun = rec.stun,
                           forbid = rec.forbid }
        B.hitsrc[i] = rec
        named("hit", rec.name, i)
        return i
    end

    -- A reactive mark: lands where I stood at the end of t_mark. { name,
    -- t_mark, r, t0, t1, sample, dmg_exp, dmg_max, stun, forbid, style, factor, land }
    function B.mark(m)
        assert(m)
        assert(math.type(m.t_mark) == "integer", "tob2 mark " .. tostring(m.name) .. ": no t_mark")
        assert(math.type(m.t0) == "integer", "tob2 mark " .. tostring(m.name) .. ": no t0")
        local t1 = m.t1 or m.t0
        assert(t1 >= m.t0, "tob2 mark: t1 < t0")
        local rec = { name = m.name, t_mark = m.t_mark, r = m.r or 0, t0 = m.t0, t1 = t1,
                      sample = m.sample or "end", dmg_exp = m.dmg_exp or 0, dmg_max = m.dmg_max or m.dmg_exp or 0,
                      stun = m.stun or 0, forbid = m.forbid == true, style = m.style, factor = m.factor,
                      land = m.land or 1, read = m.read or m.land or 1, mark = true }
        assert(#B.landing < V.LANDING_MAX, "tob2: more than " .. V.LANDING_MAX .. " hits in one tick")
        B.landing[#B.landing + 1] = rec
        if t1 < now + 1 then return nil end
        if rec.t_mark < now then
            -- (aplan takes t_mark >= now: a mark whose tile is already
            -- history is a hit round that tile)
            local at = QD.T2.tile_at(S, rec.t_mark)
            assert(at, "tob2 mark " .. tostring(m.name) .. ": t_mark " .. rec.t_mark .. " is past and no tile history")
            return B.spec_hit({ name = rec.name, x0 = at.x - rec.r, z0 = at.z - rec.r, x1 = at.x + rec.r,
                                z1 = at.z + rec.r, t0 = rec.t0, t1 = rec.t1, sample = rec.sample,
                                dmg_exp = rec.dmg_exp, dmg_max = rec.dmg_max, stun = rec.stun, forbid = rec.forbid,
                                style = rec.style, factor = rec.factor, land = rec.land, read = rec.read })
        end
        assert(#B.spec.marks < V.MARKS_MAX, "tob2: more than " .. V.MARKS_MAX .. " marks (" .. tostring(m.name) .. ")")
        local i = #B.spec.marks + 1
        B.spec.marks[i] = { t_mark = rec.t_mark, r = rec.r, t0 = rec.t0, t1 = rec.t1, sample = rec.sample,
                            dmg_exp = rec.dmg_exp, dmg_max = rec.dmg_max, stun = rec.stun, forbid = rec.forbid }
        B.marksrc[i] = rec
        named("mark", m.name, i)
        return i
    end

    -- A target: { name, npc = row, range, speed, value, size, path = {{x, z},
    -- ...} (footprint SW at k = 0..h; default: hold the row's tile), t_ok0, t_ok1 }.
    -- -> its index (path[1].tgt names it).
    function B.target(t)
        assert(t)
        assert(t.npc, "tob2 target " .. tostring(t.name) .. ": no npc row")
        assert(math.type(t.range) == "integer" and t.range >= 1, "tob2 target: range is an integer >= 1")
        assert(math.type(t.speed) == "integer" and t.speed >= 1, "tob2 target: speed is an integer >= 1")
        assert(t.value ~= nil, "tob2 target " .. tostring(t.name) .. ": no value")
        assert(#B.spec.targets < V.TARGETS_MAX, "tob2: more than " .. V.TARGETS_MAX .. " targets (" .. tostring(t.name) .. ")")
        local path = {}
        if t.path then
            for _, p in ipairs(t.path) do path[#path + 1] = QD.T2.tile(p) end
        end
        if #path == 0 then path[1] = { x = t.npc.x, z = t.npc.z } end
        local i = #B.spec.targets + 1
        B.spec.targets[i] = { size = t.size or t.npc.size or 1, path = path, range = t.range, speed = t.speed,
                              value = t.value, t_ok0 = t.t_ok0 or now, t_ok1 = t.t_ok1 or (now + V.T_OK_FOREVER) }
        -- THE KILL LIMIT: swings_max = ceil(hp / hit) (Nylocas: a one-hit
        -- nylo earned plan value after another seat killed it, 985 of 5,218
        -- swings "predicted, none"). Sent whenever set; a binary without it
        -- ignores the field.
        local smax = t.swings_max
        if smax == nil and t.hp ~= nil and t.hit ~= nil then
            assert(t.hit > 0, "tob2 target " .. tostring(t.name) .. ": hit must be > 0")
            smax = math.max(0, math.ceil(t.hp / t.hit))
            smax = math.tointeger(smax) or smax
        end
        if smax ~= nil then
            assert(math.type(smax) == "integer" and smax >= 0, "tob2 target: swings_max is an integer >= 0")
            B.spec.targets[i].swings_max = smax
        end
        B.rows[i] = t.npc
        B.tmeta[i] = { name = t.name, speed = t.speed, value = t.value, range = t.range,
                       swings_max = B.spec.targets[i].swings_max, spec_ok = t.spec_ok == true }
        if t.needs ~= nil then
            assert(type(t.needs) == "table", "tob2 target " .. tostring(t.name) .. ": needs is a table")
            B.tneeds[i] = t.needs
        end
        named("target", t.name, i)
        return i
    end

    -- A watcher (the new aplan's): { name, x, z, size, t0, t1, dmg_exp,
    -- dmg_max, stun, forbid, style, factor }. Sent only when one is given,
    -- so a binary without watchers never sees the field.
    function B.watcher(w)
        assert(w)
        assert(math.type(w.x) == "integer", "tob2 watcher " .. tostring(w.name) .. ": no x")
        assert(math.type(w.z) == "integer", "tob2 watcher " .. tostring(w.name) .. ": no z")
        assert(math.type(w.t0) == "integer", "tob2 watcher " .. tostring(w.name) .. ": no t0")
        local t1 = w.t1 or w.t0
        assert(t1 >= w.t0, "tob2 watcher: t1 < t0")
        if B.spec.watchers == nil then B.spec.watchers = {} end
        assert(#B.spec.watchers < V.WATCHERS_MAX,
            "tob2: more than " .. V.WATCHERS_MAX .. " watchers (" .. tostring(w.name) .. ")")
        local i = #B.spec.watchers + 1
        local dmg_exp = w.dmg_exp or 0
        B.spec.watchers[i] = { x = w.x, z = w.z, size = w.size or 1, t0 = w.t0, t1 = t1, dmg_exp = dmg_exp,
                               dmg_max = w.dmg_max or dmg_exp, stun = w.stun or 0, forbid = w.forbid == true }
        B.watchsrc[i] = { style = w.style, factor = w.factor, t0 = w.t0, dmg_exp = dmg_exp,
                          dmg_max = w.dmg_max or dmg_exp, land = w.land or 1, read = w.read or w.land or 1 }
        named("watcher", w.name, i)
        return i
    end

    -- A scheduled held op (the resource channel's): { name, t, ends_attack,
    -- swing_delay, heal }.
    function B.held(e)
        assert(e)
        assert(math.type(e.t) == "integer", "tob2 held " .. tostring(e.name) .. ": no t")
        assert(#B.spec.held < V.HELD_MAX, "tob2: more than " .. V.HELD_MAX .. " held ops (" .. tostring(e.name) .. ")")
        local i = #B.spec.held + 1
        B.spec.held[i] = { t = e.t, ends_attack = e.ends_attack ~= false, swing_delay = e.swing_delay or 0,
                           heal = e.heal or 0 }
        named("held", e.name, i)
        return i
    end

    -- A chaser (the v1 tornado rule): { name, x, z, dmg_exp, dmg_max }.
    function B.chaser(c)
        assert(c)
        assert(#B.spec.chasers < V.CHASERS_MAX, "tob2: more than " .. V.CHASERS_MAX .. " chasers (" .. tostring(c.name) .. ")")
        local i = #B.spec.chasers + 1
        -- (forbid forwarded: action_plan.c's chaser forbid makes the touch a
        -- RULE; dropped here, Verzik P3's "a touch is a RULE" tornadoes were
        -- priced at half hp all along and the kite took 6 touches for 216 on
        -- 51 relays with plan risk 0 - integ ifin3_rl f11 t3028-3030)
        B.spec.chasers[i] = { x = c.x, z = c.z, dmg_exp = c.dmg_exp or 0, dmg_max = c.dmg_max or c.dmg_exp or 0,
                              forbid = c.forbid == true }
        named("chaser", c.name, i)
        return i
    end

    -- The v1 soft terms, costs in hp (fields as api_drive.plan's).
    local function strip(t)
        local out = {}
        for k, v in pairs(t) do if k ~= "name" then out[k] = v end end
        return out
    end
    function B.zone(z)
        assert(z)
        assert(#B.spec.zones < V.ZONES_MAX, "tob2: more than " .. V.ZONES_MAX .. " zones (" .. tostring(z.name) .. ")")
        local i = #B.spec.zones + 1
        B.spec.zones[i] = strip(z)
        named("zone", z.name, i)
        return i
    end
    function B.pull(p)
        assert(p)
        assert(#B.spec.pulls < V.PULLS_MAX, "tob2: more than " .. V.PULLS_MAX .. " pulls (" .. tostring(p.name) .. ")")
        local i = #B.spec.pulls + 1
        B.spec.pulls[i] = strip(p)
        named("pull", p.name, i)
        return i
    end
    function B.goal(g)
        assert(g)
        assert(B.spec.goal == nil, "tob2: a second goal (" .. tostring(g.name) .. ")")
        B.spec.goal = strip(g)
        B.goal_name = g.name or "goal"
    end
    function B.edge(e)
        assert(e)
        B.spec.edge = strip(e)
    end

    -- Expected / max damage LANDING on absolute tick t (the monitor's
    -- `threat`): a hit read at w lands at w + land; my tile at the end of w
    -- is the history's, or `step1` for w = now+1. Prayed at the read tick
    -- w + read (the overhead history for a tick already seen).
    function B.landing_damage(t, step1)
        local exp, max = 0, 0
        for _, r in ipairs(B.landing) do
            local w = t - r.land
            if w >= r.t0 and w <= r.t1 then
                local me = (w == now + 1) and step1 or QD.T2.tile_at(S, w)
                local on = false
                if me ~= nil then
                    if r.mark then
                        local at = (r.t_mark == now + 1) and step1 or QD.T2.tile_at(S, r.t_mark)
                        on = at ~= nil and math.max(math.abs(me.x - at.x), math.abs(me.z - at.z)) <= r.r
                    else
                        on = me.x >= r.x0 and me.x <= r.x1 and me.z >= r.z0 and me.z <= r.z1
                    end
                end
                if on then
                    local f = B.prayed(r.style, w + r.read, r.factor)
                    exp = exp + r.dmg_exp * f
                    max = max + r.dmg_max * f
                end
            end
        end
        return exp, max
    end

    -- THE PLAN: the prayer schedule applied to every hit, the actuator state
    -- from the executive, api_drive.aplan, the why named.
    function B.plan()
        assert(B.result == nil, "tob2: B.plan called twice in one tick")
        for i, src in ipairs(B.hitsrc) do
            local f = B.prayed(src.style, math.max(src.t0, now + 1) + (src.read or src.land), src.factor)
            B.spec.hits[i].dmg_exp = src.dmg_exp * f
            B.spec.hits[i].dmg_max = src.dmg_max * f
        end
        for i, src in ipairs(B.marksrc) do
            local f = B.prayed(src.style, math.max(src.t0, now + 1) + (src.read or src.land), src.factor)
            B.spec.marks[i].dmg_exp = src.dmg_exp * f
            B.spec.marks[i].dmg_max = src.dmg_max * f
        end
        for i, src in ipairs(B.watchsrc) do
            local f = B.prayed(src.style, math.max(src.t0, now + 1) + (src.read or src.land), src.factor)
            B.spec.watchers[i].dmg_exp = src.dmg_exp * f
            B.spec.watchers[i].dmg_max = src.dmg_max * f
        end
        if B.spec.watchers ~= nil and #B.spec.watchers == 0 then B.spec.watchers = nil end
        -- CHANNEL READINESS: a target that needs gear on or the cast clock
        -- accepts swings from the executive's readiness tick, no earlier
        for i, needs in pairs(B.tneeds) do
            local ready = QD.T2.exec.ready_tick(S, F, B.intents or {}, needs, B.spec.targets[i].speed)
            B.tmeta[i].ready = ready
            if ready > B.spec.targets[i].t_ok0 then B.spec.targets[i].t_ok0 = ready end
        end
        local st = QD.T2.exec.plan_state(S, F, B)
        B.spec.inter = st.inter
        B.spec.swing_ready = st.swing_ready
        B.spec.stunned_until = st.stunned_until
        B.result = B.run(B.spec)
        return B.result
    end

    -- ONE aplan call on `spec`, the answer decorated (rows, why named, the
    -- monitor's landing damage). B.plan and B.replan share it; a tick makes
    -- at most two calls (B.calls is asserted).
    function B.run(spec)
        B.calls = (B.calls or 0) + 1
        assert(B.calls <= 2, "tob2: more than two aplan calls in one tick")
        local r, res = api_drive.aplan(spec)
        assert(r == "ok", S.who .. ": api_drive.aplan answered " .. tostring(r))
        assert(res and res.path and #res.path >= 1, S.who .. ": api_drive.aplan answered no path")
        res.rows = B.rows
        res.tmeta = B.tmeta
        res.targets = spec.targets
        res.why_name = nil
        if res.why ~= nil then
            local kind, idx, k = tostring(res.why):match("(%a+)%[(%d+)%] k(%d+)")
            local list = kind and B.names[kind]
            local nm = list and list[tonumber(idx)]
            res.why_name = (nm or tostring(res.why)) .. (k and (" k" .. k) or "")
        end
        -- the monitor's threat: damage landing on now+1 (my tile at the end of now)
        local p1 = res.path[1]
        res.land_exp, res.land_max = B.landing_damage(now + 1, { x = p1.x, z = p1.z })
        S.expanded = S.expanded + (res.expanded or 0)
        return res
    end

    -- THE WHAT-IF: the same spec (after B.plan) with the held ops named in
    -- `drop` taken out and `extra` ({ {name, t, ends_attack, swing_delay,
    -- heal}, ... }) put in. -> the answer, not adopted (B.adopt does that).
    function B.replan(extra, drop)
        assert(B.result ~= nil, "tob2: B.replan before B.plan")
        local spec = {}
        for k, v in pairs(B.spec) do spec[k] = v end
        local held, names = {}, {}
        for i, e in ipairs(B.spec.held) do
            local nm = B.names.held[i]
            if not (drop and drop[nm]) then
                held[#held + 1] = e
                names[#held] = nm
            end
        end
        for _, e in ipairs(extra) do
            assert(math.type(e.t) == "integer", "tob2 replan: a held op with no t")
            assert(#held < V.HELD_MAX, "tob2: more than " .. V.HELD_MAX .. " held ops (replan " .. tostring(e.name) .. ")")
            held[#held + 1] = { t = e.t, ends_attack = e.ends_attack ~= false, swing_delay = e.swing_delay or 0,
                                heal = e.heal or 0 }
            names[#held] = e.name or ("held" .. #held)
        end
        spec.held = held
        local saved = B.names.held
        B.names.held = names
        local res = B.run(spec)
        res.held_names = names
        B.names.held = saved
        res.spec_held = held
        return res
    end

    -- Make a what-if's answer the tick's plan.
    function B.adopt(res)
        assert(res)
        B.spec.held = res.spec_held
        B.names.held = res.held_names
        B.result = res
    end

    return B
end
