-- quest-driver / tob2_run: THE ROOM LOOP (ToB v2).
--
-- One tick (TOB2_DESIGN.md):
--
--   monitor.check   last tick's prediction vs this tick's measurement
--   resources       eat / drink / boost prayer (v1 _tob_supplies underneath)
--   B = builder     the spec
--   room.predict    threats into B
--   room.strategy   targets / goals / zones into B; -> intents
--   schedule        the prayer press into B's prayer schedule (R5), the held
--                   ops into B.held (E4/M6) at the tick they will go out
--   B.plan          api_drive.aplan
--   exec.emit       the channels
--   room.done       "ok" ends the room
--
-- A room is QD.T2.rooms[name] = { ids(opts)?, init(S, opts), enter(S),
-- predict(S, F, B), strategy(S, F, B) -> intents, done(S, F), summary(S)? };
-- QD.T2.register(name, room) also gives the relay QD.raid.<name>_solve.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}

function QD.T2.register(name, room)
    assert(name)
    assert(room)
    QD.T2.rooms[name] = room
    QD.raid[name .. "_solve"] = function(opts) return QD.T2.solve(name, opts) end
end

-- ================================================================ RESOURCES

-- The resource channel's decision, BEFORE the plan: { eat, drink,
-- drink_heal, pray } from v1's supplies with the room's wants
-- (S.t2.want = { eat_below, brew_below, boost, boost_stat }).
function QD.T2.resources(S, F)
    local want = S.t2.want
    local res = {}
    -- THE BUDGET (tob2_budget.lua): a kind whose stock is at or under the
    -- later rooms' reserve is not offered to the supplies at all (its id
    -- list emptied for this one call). Absent on a binary whose parts list
    -- predates the file: then no reserve.
    local saved = S.ids
    if QD.T2.budget and QD.T2.budget.filtered_ids then
        S.ids = QD.T2.budget.filtered_ids(S, F)
    end
    QD.raid._tob_supplies(S, F, { eat_below = want.eat_below, brew_below = want.brew_below,
                                  boost = want.boost, boost_stat = want.boost_stat,
                                  prayer_sip = want.prayer_sip }, res)
    S.ids = saved
    return res
end

-- The intents, completed: the resources fill what the room left empty.
function QD.T2.merge(S, intents, res)
    if intents.eat == nil then intents.eat = res.eat end
    if intents.drink == nil then intents.drink = res.drink end
    if intents.pray == nil then intents.pray = res.pray end
    -- the room's floors, for the next tick's resource decision
    if intents.eat_below then S.t2.want.eat_below = intents.eat_below end
    if intents.brew_below then S.t2.want.brew_below = intents.brew_below end
    S.t2.want.prayer_sip = intents.prayer_sip
    return intents
end

-- Does the prayer channel take the panel this tick (E9, refined: a boost
-- press yields to a held op)?
function QD.T2.prayer_takes_panel(S, F, intents)
    return QD.T2.exec.prayer_due(S, F, intents.pray, intents.pray_force)
        and not QD.T2.exec.prayer_yields(S, F, intents)
end

-- The plan step a backpack op decided now lands on: now+1, or now+2 when
-- the prayer takes the panel or the backpack is not showing (E9).
function QD.T2.held_tick(S, F, intents)
    if QD.T2.V.TAB_SETTLES then return F.tick + 1 end
    if QD.T2.prayer_takes_panel(S, F, intents) or S.tab ~= S.ids.inv_tab then return F.tick + 2 end
    return F.tick + 1
end

-- A food's held entry: the attack delay it adds ONLY to a running weapon
-- delay (consume_shared.rs2 ~consume_attack_delay). The C model pushes
-- swing_ready to max(swing_ready, t + swing_delay); an eat at step t runs at
-- clock t-1, so a delay still running (W > t-1) becomes W + 3, a ready
-- weapon gets nothing.
function QD.T2.food_held(S, F, B, obj, t, name)
    local st = QD.T2.exec.plan_state(S, F, B)
    local delay = 0
    if st.swing_ready > t - 1 and S.x2.swing_next ~= nil then
        delay = st.swing_ready + QD.T2.V.FOOD_ATTACK_DELAY - t
    end
    return { name = name or "eat", t = t, ends_attack = true, swing_delay = delay, heal = QD.T2.heal_of(S, F, obj) }
end

-- What the executive will send, into the plan: the prayer press (R5: in force
-- from now+1) and every held op at the tick it will go out (E9). Every
-- backpack op ends the attack (E4/M6); food delays a running weapon clock,
-- potions and wields do not (QD.T2.V's held-op notes).
function QD.T2.schedule(S, F, B, intents)
    local now = F.tick
    B.intents = intents   -- B.plan's channel readiness reads them
    local praying = QD.T2.prayer_takes_panel(S, F, intents)
    if praying then B.pray_at(intents.pray, now + 1) end
    -- a protect prayer the room turns off (intents.pray_off) is out from
    -- now+1 for B.prayed; assumed to get its press (it shares the book with
    -- a press on, and yields only to a held op)
    for _, name in ipairs(intents.pray_off or {}) do
        if QD.T2.exec.prayer_off_due(S, F, name) then B.unpray_at(name, now + 1) end
    end
    local held_t = QD.T2.held_tick(S, F, intents)
    local backpack = false
    if intents.eat then
        B.held(QD.T2.food_held(S, F, B, intents.eat, held_t))
        backpack = true
    end
    if intents.drink then
        B.held({ name = "drink", t = held_t, ends_attack = true, swing_delay = 0,
                 heal = QD.T2.heal_of(S, F, intents.drink) })
        backpack = true
    end
    if QD.T2.exec.gear_due(S, F, intents) then
        B.held({ name = "wield", t = held_t, ends_attack = true, swing_delay = 0 })
        backpack = true
    end
    if intents.drop then
        B.held({ name = "drop", t = held_t, ends_attack = true, swing_delay = 0 })
        backpack = true
    end
    -- THE SPEC CHANNEL'S PLAN: a dump the executive may make this tick
    -- (budget.dump_due, the hammer in the pack, a melee spec_ok target, no
    -- gear change and no special or cast of the room's): the wield as a held
    -- entry, the special as the target's value x SPEC_VALUE.
    S.x2.dump_plan = nil
    if QD.T2.budget and QD.T2.budget.dump_due and S.x2.dump == nil and not intents.spec and not intents.cast
        and not QD.T2.exec.gear_due(S, F, intents) then
        local Bu = QD.T2.budget
        if Bu._dump_id == nil then
            local r, id = api_drive.symbol("obj", Bu.DUMP_WEAPON)
            assert(r == "ok", "tob2 spec: no obj " .. Bu.DUMP_WEAPON)
            Bu._dump_id = id
        end
        if QD.raid._tob_count(S, Bu._dump_id) > 0 and Bu.dump_due(S, Bu.SPEC_COST[Bu.DUMP_WEAPON]) then
            local any = false
            for i, m in pairs(B.tmeta) do
                if m.spec_ok and (m.range or 1) <= 1 then
                    any = true
                    B.spec.targets[i].value = B.spec.targets[i].value * QD.T2.V.SPEC_VALUE
                end
            end
            if any then
                B.held({ name = "spec-wield", t = now + 1, ends_attack = true, swing_delay = 0 })
                S.x2.dump_plan = { weapon = Bu._dump_id }
            end
        end
    end
    if QD.T2.exec.unequip_due(S, F, intents) then
        local t = (not QD.T2.V.TAB_SETTLES and (praying or backpack)) and (now + 2) or (now + 1)
        B.held({ name = "unequip", t = t, ends_attack = true, swing_delay = 0 })
    end
end

-- ================================================================== RESCUE

-- NOTHING ACTED ON PREDICTED DEATH (mb46 p3, t401-415: the plan said risk
-- 8-20 and hp_worst -12..-70 for fourteen ticks -- two blackstorms in flight
-- at it, undodgeable -- while the resource channel kept to its thresholds,
-- one brew in three ticks; dead at t415). One currency: when the plan's
-- risk comes from hp_worst (not a forbid term), heal with everything this
-- tick allows -- the best food AND a brew (separate timers, a combo eat),
-- each only off its cooldown -- and ASK THE PLAN: the same spec with those
-- held ops, one more aplan. Adopted only when the risk falls (below); the
-- intents then carry the eat and the drink.
function QD.T2.rescue(S, F, B, intents)
    local r = B.result
    local d = F.tick
    -- the plan's risk by cause (action_plan.c): risk_rule (forbids, lethal
    -- zones) a heal never changes, risk_hp (hp_worst <= 0) it can; an older
    -- binary answers risk alone
    local split = r.risk_hp ~= nil and r.risk_rule ~= nil
    local hp_risk = split and r.risk_hp or (((r.hp_worst or 1) <= 0) and (r.risk or 0) or 0)
    if hp_risk <= 0 or r.hp_worst == nil or r.hp_worst > 0 then
        S.t2.rescue_run = 0
        return
    end
    -- at most RESCUE_RUN consecutive ticks of rescue while the hp risk
    -- persists (food + brew, then what the timers allow next tick)
    if (S.t2.rescue_run or 0) >= QD.T2.V.RESCUE_RUN then return end
    local H = S.x2.held
    local t = QD.T2.held_tick(S, F, intents)
    local extra, drop = {}, {}
    local eat, drink = intents.eat, intents.drink
    if eat == nil and d >= H.food_until then
        eat = QD.raid._tob_first(S, S.ids.food)
        if eat then extra[#extra + 1] = QD.T2.food_held(S, F, B, eat, t, "rescue-eat") end
    end
    local brewing = false
    for _, id in ipairs(S.ids.brews) do if id == drink then brewing = true end end
    if not brewing and d >= H.drink_until then
        local brew = QD.raid._tob_first(S, S.ids.brews)
        if brew then
            -- (a restore or a boost this tick gives way: hp first)
            if drink ~= nil then drop.drink = true end
            drink = brew
            extra[#extra + 1] = { name = "rescue-brew", t = t, ends_attack = true, swing_delay = 0,
                                  heal = QD.T2.heal_of(S, F, brew) }
        end
    end
    S.t2.rescues = S.t2.rescues or { asked = 0, taken = 0, none = 0 }
    local R = S.t2.rescues
    if #extra == 0 then
        R.none = R.none + 1
        return
    end
    R.asked = R.asked + 1
    local r2 = B.replan(extra, drop)
    -- THE ADOPT RULE, BY CAUSE. Without the split: only when the total risk
    -- falls (a forbid-driven plan gets the same risk and a higher hp_worst;
    -- adopting that was Bloat's waste, 7-13 brews and fish at full hp).
    -- With it (relay f32, relay_r12_128: a Sotetseg seat at 74 foresaw two
    -- ricochets of 50 on one tick, hp_worst -26; one brew could not lift it
    -- above 0, nothing was adopted, and it died): risk_rule unchanged (a heal
    -- cannot touch it) and either risk_hp falls, or it is equal and
    -- hp_worst rises by at least half the heal -- a burst one heal cannot
    -- fully cover still gets the heal.
    local adopt
    if split and r2.risk_rule ~= nil and r2.risk_hp ~= nil then
        local heal = 0
        for _, e in ipairs(extra) do heal = heal + (e.heal or 0) end
        local same_rule = math.abs(r2.risk_rule - r.risk_rule) < 1e-6
        adopt = same_rule and (r2.risk_hp < r.risk_hp - 1e-6
            or (math.abs(r2.risk_hp - r.risk_hp) < 1e-6 and (r2.hp_worst or 0) >= (r.hp_worst or 0) + 0.5 * heal))
    else
        adopt = (r2.risk or 0) < (r.risk or 0)
    end
    if not adopt then
        R.refused = (R.refused or 0) + 1
        return
    end
    S.t2.rescue_run = (S.t2.rescue_run or 0) + 1
    R.taken = R.taken + 1
    B.adopt(r2)
    intents.eat, intents.drink = eat, drink
    if eat then S.last_eat = d end
    if drink then S.last_drink = d end
    QD.raid._tob_trace(S, d, string.format("rescue %s%s: risk %s hw %s -> risk %s hw %s",
        eat and ("eat " .. eat .. " ") or "", drink and ("drink " .. drink) or "", tostring(r.risk),
        tostring(r.hp_worst), tostring(r2.risk), tostring(r2.hp_worst)))
end

-- ==================================================================== TRACE

-- One line a tick into the recent ring; mode changes and risky plans into
-- the trace.
function QD.T2.trace_tick(S, F, B, intents)
    local r = B.result
    local p1 = r.path[1]
    local mode = intents.mode or "-"
    local line = string.format("t%d %s @%d,%d %s>%d,%d%s r%s l%.1f v%.1f hw%s%s", F.tick, mode, F.me.x, F.me.z,
        tostring(p1.act), p1.x, p1.z, p1.swing and "!" or "", tostring(r.risk), r.loss or 0, r.value or 0,
        tostring(r.hp_worst), r.why_name and (" " .. r.why_name) or "")
    S.recent[#S.recent + 1] = line
    if #S.recent > QD.T2.V.RECENT then table.remove(S.recent, 1) end
    -- THE DECISION LOG: every seat's plan, every tick, as a tick-log mark, so
    -- a run is read after the fact (the ring above keeps only the last few);
    -- "refused" while the log is off
    if api_drive.ticklog_mark ~= nil then
        api_drive.ticklog_mark("t2 p" .. tostring(F.pid) .. " " .. line
            .. string.format(" s%.1f i%s sr%s le%.0f lm%.0f hp%d", r.soft or 0, tostring(B.spec.inter), tostring(B.spec.swing_ready),
                r.land_exp or 0, r.land_max or 0, F.hp or -1)
            -- the boosted Attack / Strength (levels over base: the budget's
            -- monitor, the P3 "st" probe made permanent)
            .. string.format(" a%ds%d", F.levels and F.levels.attack and (F.levels.attack.level - F.levels.attack.base) or 0,
                F.levels and F.levels.strength and (F.levels.strength.level - F.levels.strength.base) or 0))
    end
    if mode ~= S.t2.mode then
        QD.raid._tob_trace(S, F.tick, "mode " .. tostring(S.t2.mode) .. " -> " .. mode)
        S.t2.mode = mode
    end
    if (r.risk or 0) > 0 and (S.t2.last_risk or -10) < F.tick - 5 then
        S.t2.last_risk = F.tick
        QD.raid._tob_trace(S, F.tick, "risk " .. tostring(r.risk) .. " at " .. F.me.x .. "," .. F.me.z
            .. " (" .. tostring(r.why_name) .. ")")
    end
end

-- ===================================================================== STEP

function QD.T2.step(S, F, room)
    -- my tile at the end of this tick: the history marks and the monitor read
    S.t2.tiles[F.tick] = { x = F.me.x, z = F.me.z }
    S.t2.tiles[F.tick - 64] = nil
    -- the protect overhead lit at the end of this tick (B.overhead_at for a
    -- read tick already seen; false = none)
    S.t2.overheads = S.t2.overheads or {}
    local lit = false
    for _, pn in pairs(QD.T2.PROTECT) do
        if F.lit and F.lit[pn] and (F.prayer or 0) > 0 then lit = pn end
    end
    S.t2.overheads[F.tick] = lit
    S.t2.overheads[F.tick - 64] = nil
    if QD.T2.budget and QD.T2.budget.tick then QD.T2.budget.tick(S, F) end
    QD.T2.monitor.check(S, F)
    local res = QD.T2.resources(S, F)
    local B = QD.T2.builder(S, F, S.t2.plan_opts)
    room.predict(S, F, B)
    local intents = room.strategy(S, F, B)
    assert(type(intents) == "table", S.who .. ": the room's strategy answered no intents")
    QD.T2.merge(S, intents, res)
    QD.T2.schedule(S, F, B, intents)
    B.plan()
    QD.T2.rescue(S, F, B, intents)
    QD.T2.exec.emit(S, F, B.result, intents)
    S.t2.last = B.result
    QD.T2.trace_tick(S, F, B, intents)
    return room.done(S, F)
end

function QD.T2.summary(S, room)
    -- (the monitor's counts first: the relay cuts the detail)
    local extra = QD.T2.monitor.counts(S) .. "; " .. ((room.summary and room.summary(S)) or "")
    if QD.T2.budget and QD.T2.budget.summary then extra = extra .. "; " .. QD.T2.budget.summary(S) end
    local R = S.t2.rescues
    if R then
        extra = extra .. string.format("; rescues asked %d taken %d no-gain %d none-left %d", R.asked, R.taken,
            R.refused or 0, R.none)
    end
    return QD.raid._tob_summary(S, extra .. "; " .. QD.T2.monitor.summary(S))
end

-- ==================================================================== SOLVE

-- QD.T2.solve(room_name, opts) -> result ("ok"|"died"|"timeout"), detail, S
function QD.T2.solve(room_name, opts)
    opts = opts or {}
    local room = QD.T2.rooms[room_name]
    assert(room, "tob2: no room named " .. tostring(room_name))
    assert(room.init, "tob2 " .. room_name .. ": no init")
    assert(room.enter, "tob2 " .. room_name .. ": no enter")
    assert(room.predict, "tob2 " .. room_name .. ": no predict")
    assert(room.strategy, "tob2 " .. room_name .. ": no strategy")
    assert(room.done, "tob2 " .. room_name .. ": no done")
    local who = "tob2_" .. room_name
    local ids = (room.ids and room.ids(opts)) or QD.raid._tob_common_ids(who, opts)
    local S = QD.raid._tob_state(who, ids, opts, {
        t2 = { room = room_name, tiles = {}, mode = nil, last = nil,
               plan_opts = { h = opts.h, beam = opts.beam },
               want = { eat_below = opts.eat_below, brew_below = opts.brew_below,
                        boost = opts.boost, boost_stat = opts.boost_stat } },
    })
    QD.T2.exec.init(S)
    QD.T2.monitor.init(S)
    if opts.swing_seqs then
        S.mon.swing_seqs = {}
        for _, s in ipairs(opts.swing_seqs) do S.mon.swing_seqs[s] = true end
    end
    room.init(S, opts)
    room.enter(S)
    return QD.raid._tob_run(S, function(s, F) return QD.T2.step(s, F, room) end,
        function(s) return QD.T2.summary(s, room) end)
end
