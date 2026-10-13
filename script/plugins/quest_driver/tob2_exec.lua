-- quest-driver / tob2_exec: THE EXECUTIVE (ToB v2), one state machine per
-- output channel (TOB2_DESIGN.md "EXECUTE").
--
--   run       on | off                       E10
--   prayer    set | pending(until d+1)       E7, R5
--   held      ready | cooldown(until)        E4, E9, E11 (eat, drink, wield, drop)
--   worn      (unequip, the worn tab)        E4, E9, E11
--   special   off | armed                    E8
--   interact  idle | walking(x,z) | attacking(npc) | casting(npc, spell)
--                                            E1, E2, E3, E4, E5, E6
--
-- Per tick the channels send in order run, prayer, held, worn, special,
-- interaction (v1 _tob_emit's order, lesson 32). A channel re-clicks only
-- when its intention changes or its expectation failed. Each records what it
-- sent and what it expects to see next tick into S.expect for the monitor.
--
-- The plan's path[1] is the interaction's intention: { act = "stay"|"walk"|
-- "attack", x, z, mid, tgt = target index (result.rows[tgt] is the npc row),
-- swing }. The room's intents: { pray, pray_force, gear = {obj | {obj,
-- two_hand}}, unequip = {component...}, spec, cast = {component, npc,
-- cooldown}, eat, drink, drop }.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.exec = QD.T2.exec or {}

function QD.T2.exec.init(S)
    assert(S)
    S.x2 = {
        interact = { state = "idle", x = nil, z = nil, npc = nil, key = nil, tgt = nil, sent = -10,
                     last_tile = nil, unmoved = 0, cast_sent = -100, spell = nil },
        held = { state = "ready", food_until = -10, drink_until = -10, gear_key = nil, gear_sent = -10,
                 unequip_key = nil, unequip_sent = -10 },
        prayer = { state = "set", pending = nil, pending_until = -10, pressed = {}, off_pending = {} },
        special = { state = "off", pressed = -10 },
        run = { state = "on", pressed = -10 },
        -- THE WEAPON CLOCK, in the server's clock (map_clock): the content's
        -- %varp5732_action_delay. A swing stamped at clock c sets it to c +
        -- the weapon's speed; the monitor stamps the swings it sees.
        swing_last = nil,       -- the server clock of my last measured swing
        swing_speed = nil,      -- the speed of what I attack / cast with now
        swing_next = nil,       -- W: the first clock my next swing may go out (nil: unknown)
        stunned_until = -1,     -- a room that measures a stun sets this (M5)
        sent = {},              -- this tick's sends { ch, what, r }
        panel_used = false,     -- E9: one panel channel a tick
    }
end

-- The actuator state the plan starts from (B.plan): the attack under way as
-- a target index of this tick's builder, the swing clock, the stun.
function QD.T2.exec.plan_state(S, F, B)
    local X = S.x2
    assert(X, "tob2: QD.T2.exec.init was not called")
    local I = X.interact
    local inter = nil
    if I.state == "attacking" and I.key ~= nil then
        for i, row in ipairs(B.rows) do
            if QD.T2.npc_key(row) == I.key then inter = i break end
        end
    end
    -- the swing clock (S.x2.swing_next, the server's weapon delay W): a swing
    -- goes out at plan step t iff t >= W. An auto swing in tick T's player
    -- phase runs at clock T; a press that swings on arrival runs in the input
    -- phase between d and d+1 at clock d, and when d < W it swings in tick
    -- d+1's phase instead -- either way the swing lands on step d+1 iff
    -- d+1 >= W. With no swing measured, the next tick.
    local ready = F.tick + 1
    if X.swing_next ~= nil then ready = math.max(ready, X.swing_next) end
    return { inter = inter, swing_ready = ready, stunned_until = X.stunned_until }
end

-- ------------------------------------------------------------------ helpers

local function x2_send(S, F, ch, what, r)
    local X = S.x2
    X.sent[#X.sent + 1] = { ch = ch, what = what, r = r }
    if r ~= "ok" then QD.T2.monitor.note(S, "refused", F.tick, ch .. " " .. what .. ": " .. tostring(r)) end
end

-- E9: one panel channel a tick; the tab switched in its call. `needs_shown`:
-- the op only once its tab ALREADY shows (the backpack: the client refuses
-- an op on a hidden panel, P2 HM) -- a switch this tick defers it a tick.
-- -> true when the op may go now.
--
-- QD.T2.V.TAB_SETTLES (default true, Nylocas report 2026-10-12): v1's
-- lesson 32 -- "a tab settles in its call, so a press in the same tick
-- finds it" -- so every channel switches its own tab and acts in the same
-- tick, in the send order (run, prayer, held, worn, special, interaction):
-- prayer tab, press, inventory tab, op. The one-panel rule cost 3 ticks a
-- Vasilias colour change per seat and 6 at a Prinkipas change (ny1
-- t282-287). The client's refusal of an op on a HIDDEN panel (v1 P2) is
-- met: the switch comes first in the same call. false = the old rule.
function QD.T2.exec.panel(S, tab, needs_shown)
    local X = S.x2
    if QD.T2.V.TAB_SETTLES then
        X.panel_used = true
        X.panel_tab = tab
        if S.tab ~= tab then
            api_drive.tab(tab)
            S.tab = tab
        end
        return true
    end
    if X.panel_used then return false end
    X.panel_used = true
    X.panel_tab = tab
    if S.tab == tab then return true end
    api_drive.tab(tab)
    S.tab = tab
    return not needs_shown
end

-- The gear the held channel will wield this tick (E11), or nil: the listed
-- objs not worn, re-sent at most every GEAR_GAP ticks unless the set changed.
function QD.T2.exec.gear_due(S, F, intents)
    local gear = intents.gear
    if gear == nil or #gear == 0 then return nil end
    local list, keys = {}, {}
    for _, g in ipairs(gear) do
        local e = (type(g) == "table") and g or { obj = g }
        assert(math.type(e.obj) == "integer", "tob2 gear: an entry with no obj id")
        if not QD.raid._tob_worn(S, e.obj) then
            list[#list + 1] = e
            keys[#keys + 1] = tostring(e.obj)
        end
    end
    if #list == 0 then return nil end
    local key = table.concat(keys, ",")
    local H = S.x2.held
    if key == H.gear_key and F.tick - H.gear_sent < QD.T2.V.GEAR_GAP then return nil end
    return list, key
end

-- The unequips due this tick (E11's spacing on the worn tab), or nil.
function QD.T2.exec.unequip_due(S, F, intents)
    local un = intents.unequip
    if un == nil or #un == 0 then return nil end
    local keys = {}
    for _, c in ipairs(un) do keys[#keys + 1] = tostring(c) end
    local key = table.concat(keys, ",")
    local H = S.x2.held
    if key == H.unequip_key and F.tick - H.unequip_sent < QD.T2.V.GEAR_GAP then return nil end
    return un, key
end

-- Will the prayer channel press `name` this tick? (E7: a toggle, pressed
-- only to change what is lit, never twice inside PRAY_GAP unless forced.)
function QD.T2.exec.prayer_due(S, F, name, force)
    if name == nil then return false end
    assert(S.ids.prayers[name], "tob2 prayer: no prayer " .. tostring(name) .. " in the ids")
    if (F.prayer or 0) <= 0 then return false end
    if F.lit[name] then return false end
    local P = S.x2.prayer
    if force then return true end
    if P.pending == name and F.tick <= P.pending_until then return false end
    return F.tick - (P.pressed[name] or -10) >= QD.T2.V.PRAY_GAP
end

-- Will the prayer channel press `name` OFF this tick? (intents.pray_off;
-- the same toggle rules as a press on, E7: only while it is lit, never
-- twice inside PRAY_GAP, pending until it is seen unlit.)
function QD.T2.exec.prayer_off_due(S, F, name)
    assert(S.ids.prayers[name], "tob2 pray_off: no prayer " .. tostring(name) .. " in the ids")
    if not F.lit[name] then return false end
    local P = S.x2.prayer
    if F.tick <= (P.off_pending[name] or -10) then return false end
    return F.tick - (P.pressed[name] or -10) >= QD.T2.V.PRAY_GAP
end

-- E9 refined: one panel a tick, the prayer first -- unless the press is not
-- a protection overhead (a boost: Piety, Rigour, Augury) and not forced,
-- and a held op wants the backpack this tick: then the held op goes and the
-- boost waits a tick (v2p2 seat 2 t181: Rigour took the panel, the blowpipe
-- wield waited for the tab, and three ticks went by in the barrage set).
function QD.T2.exec.prayer_yields(S, F, intents)
    if QD.T2.V.TAB_SETTLES then return false end   -- both go in the tick
    if intents.pray_force then return false end
    for _, pn in pairs(QD.T2.PROTECT) do
        if pn == intents.pray then return false end
    end
    return (intents.eat or intents.drink or intents.drop or QD.T2.exec.gear_due(S, F, intents)) ~= nil
end

-- CHANNEL READINESS: the first plan step at which a swing at a target with
-- `needs` = { gear = {obj...}, unequip = {component...}, cast = true } can
-- go out, from the executive's own rules: one panel a tick (E9) -- the
-- prayer press first unless it yields, the backpack a tick after its tab
-- switch, then the worn tab, then the spellbook for the cast -- and the
-- cast clock (a cast decided at d runs at clock d: d - cast_sent >= speed).
-- An attack (no panel) goes in the wield's own tick, after the wield.
-- needs.unequip: the components still to take off (the room lists only
-- the worn ones, as it does for intents.unequip).
-- (Maiden HM: the barrage set took ~3 ticks to put on and the room put
-- the 5-tick clock in t_ok0 by hand; leaks came from the plan not knowing.)
function QD.T2.exec.ready_tick(S, F, intents, needs, speed)
    assert(needs)
    local V = QD.T2.V
    local ids = S.ids
    local now = F.tick
    local ready = now + 1
    local slot = now + 1
    if QD.T2.exec.prayer_due(S, F, intents.pray, intents.pray_force)
        and not QD.T2.exec.prayer_yields(S, F, intents) then
        slot = slot + 1
    end
    local missing = false
    for _, g in ipairs(needs.gear or {}) do
        local obj = (type(g) == "table") and g.obj or g
        assert(math.type(obj) == "integer", "tob2 needs.gear: an entry with no obj id")
        if not QD.raid._tob_worn(S, obj) then missing = true end
    end
    -- (TAB_SETTLES: every panel op goes in the tick, so no slot advances)
    local step = V.TAB_SETTLES and 0 or 1
    if missing then
        if S.tab ~= ids.inv_tab then slot = slot + step end   -- E9 (old rule): the tab shows a tick after its switch
        ready = math.max(ready, slot)
        slot = slot + step
    end
    if needs.unequip ~= nil and #needs.unequip > 0 then
        ready = math.max(ready, slot)
        slot = slot + step
    end
    if needs.cast then
        ready = math.max(ready, slot)
        ready = math.max(ready, S.x2.interact.cast_sent + (speed or V.CAST_COOLDOWN) + 1)
    end
    return ready
end

-- The world op the room asks for, normalised: { mode = "loc", kind, id, op,
-- x, z, element, near (the arrival distance: 0 on an obj's tile, 1 beside a
-- loc or an npc), key }. intents.world_op wins over the loc_op alias.
function QD.T2.exec.world_op_want(intents)
    local W = intents.world_op
    if W == nil then
        local L = intents.loc_op
        W = { kind = "loc", id = L.loc or L.loc_id or L[1], op = L.op, x = L.x, z = L.z, element = L.element }
    end
    local kind = W.kind or "loc"
    assert(kind == "loc" or kind == "obj" or kind == "npc", "tob2 world_op: kind is loc|obj|npc, not " .. tostring(kind))
    local id = W.id or W[kind] or W[1]
    assert(id ~= nil, "tob2 world_op: no id or symbol")
    if type(id) == "string" then
        local name = (kind == "npc") and QD.raid.tob_npc_name(id) or id
        local r, sid = api_drive.symbol(kind, name)
        assert(r == "ok", "tob2 world_op: no " .. kind .. " named " .. name)
        id = sid
    end
    assert(math.type(id) == "integer", "tob2 world_op: id is an integer or a symbol")
    local op = W.op or 1
    return { mode = "loc", kind = kind, id = id, loc = id, op = op, x = W.x, z = W.z, element = W.element,
             near = (kind == "obj") and 0 or 1,
             key = kind .. ":" .. id .. ":" .. op .. ":" .. tostring(W.x) .. "," .. tostring(W.z) }
end

-- Send a world op: the named element, else the copy on (x, z) (its element
-- from the kind's listing, as QD.raid.tob_loc_op), else the type alone.
-- -> the verb's answer ("not_found" when no copy stands on (x, z)).
function QD.T2.exec.world_op_send(w)
    if w.element ~= nil then return api_drive.world_op(w.kind, w.id, w.op, w.element) end
    if w.x == nil then return api_drive.world_op(w.kind, w.id, w.op) end
    local lr, rows
    if w.kind == "loc" then
        lr, rows = api_drive.locs(40)
    elseif w.kind == "obj" then
        lr, rows = api_drive.objs(40)
    else
        lr, rows = api_drive.npcs(40)
    end
    if lr ~= "ok" then return lr end
    for _, rw in ipairs(rows) do
        local id_ok, rx, rz
        if w.kind == "loc" then
            id_ok, rx, rz = rw.loc_id == w.id or rw.resolved_loc_id == w.id, rw.x, rw.z
        elseif w.kind == "obj" then
            id_ok, rx, rz = rw.obj_id == w.id, rw.x, rw.z
        else
            id_ok, rx, rz = rw.npc_id == w.id or rw.base_npc_id == w.id, rw.server_x or rw.x, rw.server_z or rw.z
        end
        if id_ok and rx == w.x and rz == w.z then
            local tid = (w.kind == "loc") and rw.loc_id or (w.kind == "obj" and rw.obj_id or rw.npc_id)
            return api_drive.world_op(w.kind, tid, w.op, rw.element_id)
        end
    end
    return "not_found"
end

-- May a room's own special go out (intents.spec)? Through the budget:
-- the cost is intents.spec_cost, else the worn weapon's (budget.SPEC_COST),
-- else 500. Dawnbringer is exempt: P1 cannot end without its specials.
-- Absent budget (an old parts list): yes.
function QD.T2.exec.spec_gate(S, F, intents)
    local Bu = QD.T2.budget
    if Bu == nil or Bu.spec_ok == nil then return true end
    if Bu._spec_ids == nil then
        Bu._spec_ids = {}
        for name, cost in pairs(Bu.SPEC_COST) do
            local r, id = api_drive.symbol("obj", name)
            if r == "ok" then Bu._spec_ids[id] = { name = name, cost = cost } end
        end
    end
    local cost = intents.spec_cost
    if cost == nil then
        local wr, wcell = api_drive.inv_slot(S.ids.worn, 3)
        local w = wr == "ok" and wcell and Bu._spec_ids[wcell.obj_id] or nil
        if w and w.name == "dawnbringer" then return true end
        cost = (w and w.cost) or 500
    end
    return Bu.spec_ok(S, cost)
end

-- The hitpoints a food or a potion gives (consume_shared.rs2 / food.rs2 /
-- sara_brew.rs2): anglerfish 22% of base, a brew 2 + 15% of base, any other
-- food FOOD_HEAL, any other potion 0.
function QD.T2.heal_of(S, F, obj)
    if obj == nil then return 0 end
    local V = QD.T2.V
    if QD.T2._angler == nil then
        local r, id = api_drive.symbol("obj", "anglerfish")
        assert(r == "ok", "tob2: no obj named anglerfish")
        QD.T2._angler = id
    end
    local base = F.hp_base or 99
    if obj == QD.T2._angler then return base * V.ANGLER_PCT // 100 end
    for _, id in ipairs(S.ids.brews) do
        if id == obj then return V.BREW_ADD + base * V.BREW_PCT // 100 end
    end
    for _, id in ipairs(S.ids.food) do
        if id == obj then return V.FOOD_HEAL end
    end
    return 0
end

-- Is a free backpack slot there for a two-handed wield over a shield?
function QD.T2.exec.free_slots(S)
    local n = 0
    for slot = 0, 27 do
        local r, cell = api_drive.inv_slot(S.ids.inv, slot)
        if r ~= "ok" or cell == nil or cell.obj_id == nil or cell.obj_id < 0 then n = n + 1 end
    end
    return n
end

-- ===================================================================== EMIT

-- result = B.result (path, rows, tmeta, land_exp, land_max); intents as above.
function QD.T2.exec.emit(S, F, result, intents)
    assert(S)
    assert(F)
    assert(result)
    assert(intents)
    local V = QD.T2.V
    local X = S.x2
    assert(X, "tob2: QD.T2.exec.init was not called")
    local ids = S.ids
    local d = F.tick
    X.sent = {}
    X.panel_used = false
    local p1 = result.path[1]
    assert(p1, "tob2 emit: a plan with no step 1")
    local I = X.interact

    -- ------------------------------------------------------------ RUN (E10)
    -- The game turns run off at 0 energy and nothing turns it back on: back
    -- on at >= RUN_ON_AT run energy, at most every RUN_GAP ticks.
    X.run.state = F.run_on and "on" or "off"
    if not F.run_on and (F.run_energy or 0) >= V.RUN_ON_AT and d - X.run.pressed >= V.RUN_GAP then
        x2_send(S, F, "run", "on", api_drive.if_click(ids.run_orb, 1))
        X.run.pressed = d
    end

    -- --------------------------------------------------------- PRAYER (E7, R5)
    -- E9: the prayer goes first on the panel; a press is a toggle, sent only
    -- to change what is lit; in force from d+1 (R5): pending until then.
    local P = X.prayer
    if P.pending ~= nil and (F.lit[P.pending] or d > P.pending_until + 1) then
        P.pending = nil
        P.state = "set"
    end
    local pray = intents.pray
    if QD.T2.exec.prayer_due(S, F, pray, intents.pray_force)
        and not QD.T2.exec.prayer_yields(S, F, intents) then
        if QD.T2.exec.panel(S, ids.prayer_tab, false) then
            local r = api_drive.if_click(ids.prayers[pray].button, 1)
            x2_send(S, F, "prayer", pray, r)
            if r == "ok" then
                P.pressed[pray] = d
                P.pending, P.pending_until, P.state = pray, d + 1, "pending"
                S.presses = S.presses + 1
            end
        end
    end
    -- PRAYER OFF (intents.pray_off = {name...}: prayers the room wants
    -- UNLIT; Bloat's Piety while hiding). A press of the lit prayer toggles
    -- it off. It shares the prayer book with a press on in the same tick,
    -- and otherwise yields the panel: to a due protect press (above, it
    -- went first) and to a held op that wants the backpack this tick.
    for name, until_ in pairs(P.off_pending) do
        if not F.lit[name] or d > until_ + 1 then P.off_pending[name] = nil end
    end
    if intents.pray_off then
        local held_wants = intents.eat or intents.drink or intents.drop or QD.T2.exec.gear_due(S, F, intents)
        for _, name in ipairs(intents.pray_off) do
            assert(name ~= pray, "tob2 intents: " .. tostring(name) .. " both in pray and pray_off")
            if QD.T2.exec.prayer_off_due(S, F, name) then
                local go = false
                if QD.T2.V.TAB_SETTLES then
                    go = QD.T2.exec.panel(S, ids.prayer_tab, false)
                elseif X.panel_used then
                    go = X.panel_tab == ids.prayer_tab
                elseif not held_wants then
                    go = QD.T2.exec.panel(S, ids.prayer_tab, false)
                end
                if go then
                    local r = api_drive.if_click(ids.prayers[name].button, 1)
                    x2_send(S, F, "prayer", "off " .. name, r)
                    if r == "ok" then
                        P.pressed[name] = d
                        P.off_pending[name] = d + 1
                        S.presses = S.presses + 1
                    end
                end
            end
        end
    end

    -- ----------------------------------------------------- HELD (E4, E9, E11)
    local H = X.held
    if d >= H.food_until and d >= H.drink_until then H.state = "ready" end
    local gear, gear_key = QD.T2.exec.gear_due(S, F, intents)
    local wants_held = intents.eat or intents.drink or gear or intents.drop
    local interrupted = false
    local held_sent = nil
    H.gear_sent_now = false
    if wants_held then
        -- E9: the backpack op only once its tab ALREADY shows
        if QD.T2.exec.panel(S, ids.inv_tab, true) then
            if intents.eat then
                local r = QD.raid._tob_held(S, intents.eat, QD.TOBS.OP_EAT)
                x2_send(S, F, "held", "eat " .. intents.eat, r)
                if r == "ok" then
                    S.eats = S.eats + 1
                    H.food_until = d + V.EAT_GAP
                    H.state = "cooldown"
                    -- the eat runs in the input phase at clock d: it adds
                    -- FOOD_ATTACK_DELAY only to a weapon delay still running
                    local w0 = X.swing_next
                    if X.swing_next ~= nil and X.swing_next > d then X.swing_next = X.swing_next + V.FOOD_ATTACK_DELAY end
                    held_sent = { kind = "eat", heal = QD.T2.heal_of(S, F, intents.eat), w0 = w0, w = X.swing_next }
                else
                    S.last_eat = nil
                end
            end
            if intents.drink then
                local r = QD.raid._tob_held(S, intents.drink, QD.TOBS.OP_DRINK)
                x2_send(S, F, "held", "drink " .. intents.drink, r)
                if r == "ok" then
                    S.drinks = S.drinks + 1
                    H.drink_until = d + V.DRINK_GAP
                    H.state = "cooldown"
                    local heal = QD.T2.heal_of(S, F, intents.drink)
                    if held_sent then
                        held_sent.heal = held_sent.heal + heal
                    else
                        held_sent = { kind = "drink", heal = heal, w0 = X.swing_next, w = X.swing_next }
                    end
                else
                    S.last_drink = nil
                end
            end
            if gear then
                local free = nil
                for _, e in ipairs(gear) do
                    local go = true
                    if e.two_hand then
                        -- a two-handed wield over a shield needs a free slot
                        local sr, cell = api_drive.inv_slot(ids.worn, 5)
                        local shield = sr == "ok" and cell ~= nil and cell.obj_id ~= nil and cell.obj_id >= 0
                        if shield then
                            free = free or QD.T2.exec.free_slots(S)
                            if free < 1 then
                                go = false
                                QD.T2.monitor.note(S, "refused", d, "wield " .. e.obj .. ": two-handed, no free slot")
                            end
                        end
                    end
                    if go then
                        x2_send(S, F, "held", "wield " .. e.obj, QD.raid._tob_held(S, e.obj, QD.TOBS.OP_WIELD))
                    end
                end
                H.gear_key, H.gear_sent = gear_key, d
                H.gear_sent_now = true
                held_sent = held_sent or { kind = "wield", heal = 0, w0 = X.swing_next, w = X.swing_next }
            end
            if intents.drop then
                x2_send(S, F, "held", "drop " .. intents.drop, QD.raid._tob_held(S, intents.drop, QD.TOBS.OP_DROP))
                held_sent = held_sent or { kind = "drop", heal = 0, w0 = X.swing_next, w = X.swing_next }
            end
            -- E4/M6: a held op ends an attack under way
            interrupted = true
        else
            -- deferred a tick: the resource channel decides again next tick
            if intents.eat then S.last_eat = nil end
            if intents.drink then S.last_drink = nil end
        end
    end

    -- ------------------------------------------------------ WORN (E4, E9, E11)
    local unequip, un_key = QD.T2.exec.unequip_due(S, F, intents)
    if unequip and QD.T2.exec.panel(S, ids.equipment_tab, false) then
        for _, comp in ipairs(unequip) do
            x2_send(S, F, "worn", "unequip " .. comp, api_drive.if_click(comp, 1))
        end
        H.unequip_key, H.unequip_sent = un_key, d
        held_sent = held_sent or { kind = "unequip", heal = 0, w0 = X.swing_next, w = X.swing_next }
        interrupted = true   -- E4: an unequip ends the swing as a held op does
    end
    -- THE SPEC CHANNEL (use it or lose it; tob2_budget.lua SPECIAL ENERGY):
    -- the hammer back to the main weapon the tick after a dump, unless the
    -- room's own gear set does it; then a new dump when the schedule planned
    -- one (S.x2.dump_plan) and step 1 swings at a melee target the room
    -- marked spec_ok: the hammer on, the orb armed (below), the attack
    -- pressed again (E4) -- the special goes out on the swing.
    local dumping = false
    if X.dump ~= nil and d > X.dump.t then
        local wr, wcell = api_drive.inv_slot(ids.worn, 3)
        local worn_now = wr == "ok" and wcell and wcell.obj_id or nil
        if X.dump.main ~= nil and worn_now ~= X.dump.main and not gear then
            x2_send(S, F, "held", "wield back " .. X.dump.main, QD.raid._tob_held(S, X.dump.main, QD.TOBS.OP_WIELD))
            held_sent = held_sent or { kind = "wield", heal = 0, w0 = X.swing_next, w = X.swing_next, t = d }
            interrupted = true
        end
        X.dump = nil
    elseif X.dump_plan ~= nil and not gear and not intents.spec and not intents.cast and p1.act == "attack"
        and p1.swing and p1.tgt and result.tmeta[p1.tgt] and result.tmeta[p1.tgt].spec_ok
        and (result.tmeta[p1.tgt].range or 1) <= 1 then
        local wr, wcell = api_drive.inv_slot(ids.worn, 3)
        local main = wr == "ok" and wcell and wcell.obj_id or nil
        local r = QD.raid._tob_held(S, X.dump_plan.weapon, QD.TOBS.OP_WIELD)
        x2_send(S, F, "held", "spec wield " .. X.dump_plan.weapon, r)
        if r == "ok" then
            X.dump = { t = d, main = main }
            X.dumps = (X.dumps or 0) + 1
            dumping = true
            interrupted = true
            held_sent = held_sent or { kind = "wield", heal = 0, w0 = X.swing_next, w = X.swing_next, t = d }
        end
    end
    X.dump_plan = nil
    if held_sent then held_sent.t = d end

    -- ------------------------------------------------ INTERACTION: the intention
    local me_x, me_z = F.me.x, F.me.z
    -- E5 bookkeeping: unmoved ticks, a walk arrived
    if I.last_tile and I.last_tile.x == me_x and I.last_tile.z == me_z then
        I.unmoved = I.unmoved + 1
    else
        I.unmoved = 0
    end
    I.last_tile = { x = me_x, z = me_z }
    if I.state == "walking" and I.x == me_x and I.z == me_z then I.state = "idle" end
    if I.state == "casting" and d > I.sent then I.state = "idle" end   -- a cast is one swing
    -- E5: pushed off -- the walk under way expected me elsewhere this tick
    local pushed = false
    local E = S.expect
    if I.state == "walking" and E and E.tick == d and E.act == "walk" and (E.x ~= me_x or E.z ~= me_z) then
        pushed = true
    end
    -- E4: the held op ended the attack under way; the attack is pressed
    -- again in this same tick (below, the state no longer matches)
    if interrupted and (I.state == "attacking" or I.state == "casting" or I.state == "loc") then I.state = "idle" end

    local stays = p1.x == me_x and p1.z == me_z
    local want
    if intents.world_op or intents.loc_op then
        -- WORLD-OP (intents.world_op = {kind = "loc"|"obj"|"npc", id|symbol,
        -- op, x, z, element}; intents.loc_op = {loc, op, x, z} is the "loc"
        -- alias): an interaction like a walk: it ends an attack, the server
        -- walks me to the thing and runs the op (a loc's, a ground item's
        -- Take, an npc's); re-sent when it changes, after a held op (a
        -- backpack op clears the pending interaction), and after PATH_STALL
        -- ticks unmoved short of it. (Verzik P1's FETCH, Bloat's barrier.)
        want = QD.T2.exec.world_op_want(intents)
    elseif intents.cast then
        -- E2: a cast walks while the plan moves and casts once it stands and
        -- the cast clock allows; a cast is never an attack press
        local C = intents.cast
        local row = C.npc or (C.tgt and result.rows[C.tgt]) or (p1.tgt and result.rows[p1.tgt]) or result.rows[1]
        assert(row, "tob2 cast: no npc to cast on (intents.cast.npc, or a target)")
        assert(C.component, "tob2 cast: no spell component")
        -- the plan's own word on it: when step 1 attacks the cast's target,
        -- the cast goes only on the step's swing (the target's t_ok0 carries
        -- the gear and the cast clock: B.target needs); intents.cast.ready =
        -- false holds it; the cooldown gate stays (CAST_HOLD rooms)
        local plan_says = true
        if C.tgt ~= nil and p1.act == "attack" and p1.tgt == C.tgt then plan_says = p1.swing == true end
        if not stays then
            want = { mode = "walk", x = p1.x, z = p1.z }
        elseif d - I.cast_sent >= (C.cooldown or V.CAST_COOLDOWN) and C.ready ~= false and plan_says then
            want = { mode = "cast", npc = row, component = C.component, tgt = C.tgt }
        else
            want = { mode = "stay" }
        end
    elseif p1.act == "attack" and gear and not H.gear_sent_now then
        -- E4b: the gear for this attack is due but did not go out this tick
        -- (the panel was taken): an attack pressed now swings the weapon
        -- still worn (v2p2 seat 2 t181-183: a blowpipe target pressed in the
        -- barrage set, three predicted swings, none). Move with the plan,
        -- press once the wield is out.
        want = stays and { mode = "stay" } or { mode = "walk", x = p1.x, z = p1.z }
    elseif p1.act == "attack" then
        -- E1: the plan judged reach against the target's footprint at step 1
        -- (npcs move first, M4), so a moving target is judged at its next
        -- tile; the server's follow walk is the plan's ATTACK model and the
        -- monitor's `move` rule checks it.
        local row = result.rows[p1.tgt]
        assert(row, "tob2 emit: an attack step with no target row (tgt " .. tostring(p1.tgt) .. ")")
        want = { mode = "attack", npc = row, tgt = p1.tgt }
    elseif p1.act == "walk" then
        want = { mode = "walk", x = p1.x, z = p1.z }
    else
        want = { mode = "stay" }
    end

    -- ---------------------------------------------------------- SPECIAL (E8)
    -- the orb is a TOGGLE: pressed only while it is not armed, for an attack
    X.special.state = F.spec_armed and "armed" or "off"
    -- (a room's own special goes through the budget too: QD.T2.exec.spec_gate)
    if (dumping or (intents.spec and QD.T2.exec.spec_gate(S, F, intents))) and want.mode == "attack"
        and not F.spec_armed and d - X.special.pressed >= V.SPEC_GAP then
        x2_send(S, F, "special", "orb", api_drive.if_click(ids.spec_orb, 1))
        X.special.pressed = d
    end

    -- ------------------------------------------------------ INTERACTION: send
    local pressed = false
    -- am I in reach of the pressed target's footprint NOW (k = 0)? A press
    -- swings in the input phase only then; otherwise the target's step or
    -- my walk comes first and the swing is in tick d+1's phase. (The
    -- monitor's stamp. A "target stood still k0 -> k1" test was tried and
    -- was wrong: a barrage at a slug and a blowpipe at a crab that stepped
    -- inside range went out in the input phase, fixL2 mb8 seat 1 t143/t353.)
    local still = nil
    if want.tgt ~= nil and result.targets and result.targets[want.tgt] then
        local tg = result.targets[want.tgt]
        local p0 = tg.path[1]
        local n = tg.size or 1
        local range = tg.range or 1
        if range <= 1 then
            still = QD.raid._tob_beside(me_x, me_z, p0.x, p0.z, n)
        else
            local gap = QD.raid._tob_gap(me_x, me_z, p0.x, p0.z, n)
            still = gap >= 1 and gap <= range
        end
    end
    local function walk(x, z, why)
        -- E6: always the tick's END tile, one click a tick, so the server's
        -- route cannot pass anywhere else
        local r = api_drive.move_to(x, z)
        x2_send(S, F, "interact", "walk " .. x .. "," .. z .. " " .. why, r)
        I.state, I.x, I.z, I.npc, I.key, I.sent = "walking", x, z, nil, nil, d
        S.clicks = S.clicks + 1
    end
    if want.mode == "walk" then
        -- E5: re-sent when the tile changed, after a held op (M6: whether a
        -- held op stops a walk is not settled), after PATH_STALL unmoved
        -- ticks, or when pushed off the expected tile
        if I.state ~= "walking" or I.x ~= want.x or I.z ~= want.z then
            walk(want.x, want.z, "new")
        elseif interrupted then
            walk(want.x, want.z, "after-held")
        elseif I.unmoved >= V.PATH_STALL then
            walk(want.x, want.z, "stalled")
        elseif pushed then
            walk(want.x, want.z, "pushed")
        end
    elseif want.mode == "loc" then
        local near = want.x == nil or QD.raid._tob_cheb(me_x, me_z, want.x, want.z) <= want.near
        local why = nil
        if I.state ~= "loc" or I.key ~= want.key then
            why = "new"
        elseif I.unmoved >= V.PATH_STALL and not near then
            why = "stalled"
        end
        if why then
            local r = QD.T2.exec.world_op_send(want)
            x2_send(S, F, "interact", want.kind .. " " .. want.key .. " " .. why, r)
            S.clicks = S.clicks + 1
            I.state, I.key, I.x, I.z, I.npc, I.sent = "loc", want.key, want.x, want.z, nil, d
            if r ~= "ok" then I.state = "idle" end
        end
    elseif want.mode == "stay" then
        -- E3: hold a tile with a walk to my own tile, never an attack; an
        -- attack under way would walk me after its npc
        if I.state == "attacking" or I.state == "casting" then
            walk(me_x, me_z, "hold")
        elseif I.state == "walking" and (I.x ~= me_x or I.z ~= me_z) then
            walk(me_x, me_z, "stop")
        elseif I.state == "loc" then
            -- the room dropped the loc op: a walk under way toward it stops
            if I.unmoved == 0 then walk(me_x, me_z, "stop") else I.state = "idle" end
        end
    elseif want.mode == "attack" then
        local key = QD.T2.npc_key(want.npc)
        if I.state ~= "attacking" or I.key ~= key then
            local n = want.npc
            local r = api_drive.world_op("npc", n.npc_id, 2, n.element_id)
            if r == "no_row" then r = api_drive.world_op("npc", n.npc_id, 1, n.element_id) end
            x2_send(S, F, "interact", "attack " .. tostring(n.npc_id) .. "#" .. tostring(key), r)
            S.clicks = S.clicks + 1
            if r == "ok" then
                I.state, I.npc, I.key, I.tgt, I.sent, I.x, I.z = "attacking", n, key, want.tgt, d, nil, nil
                local meta = result.tmeta and result.tmeta[want.tgt]
                if meta then X.swing_speed = meta.speed end
                -- (a dump's swing is the hammer's: its speed sets the clock)
                if dumping and QD.T2.budget then X.swing_speed = QD.T2.budget.DUMP_SPEED end
                pressed = true
            else
                -- a refused press: idle, so the next tick presses again
                I.state = "idle"
            end
        end
    elseif want.mode == "cast" then
        if QD.T2.exec.panel(S, ids.magic_tab, false) then
            local n = want.npc
            local r = api_drive.cast_npc(n.npc_id, want.component, n.element_id)
            x2_send(S, F, "interact", "cast " .. tostring(want.component) .. " on " .. tostring(n.npc_id), r)
            S.clicks = S.clicks + 1
            if r == "ok" then
                S.casts = (S.casts or 0) + 1
                I.state, I.npc, I.key, I.spell, I.sent, I.cast_sent = "casting", n, QD.T2.npc_key(n), want.component, d, d
                -- the cast's own clock: its target's speed (the spell's), not
                -- the weapon's (v2p2 seat 1 t276: a barrage read at the
                -- blowpipe's 2 predicted two casts that never came)
                local meta = intents.cast.tgt and result.tmeta and result.tmeta[intents.cast.tgt]
                X.swing_speed = (meta and meta.speed) or V.CAST_COOLDOWN
                pressed = true
            end
        end
    end

    -- E9: no panel used this tick -> the backpack back on show, so the next
    -- held op is not deferred by a tab switch
    if not X.panel_used and S.tab ~= ids.inv_tab then
        api_drive.tab(ids.inv_tab)
        S.tab = ids.inv_tab
        X.panel_used = true
    end

    -- ------------------------------------------------- EXPECT (the monitor's)
    local nrefused = 0
    for _, s in ipairs(X.sent) do if s.r ~= "ok" then nrefused = nrefused + 1 end end
    S.expect = {
        tick = d + 1, from = { x = me_x, z = me_z }, x = p1.x, z = p1.z, act = p1.act,
        mode = want.mode, swing = p1.swing == true, walking = I.state == "walking" and not stays,
        hp = F.hp, land_exp = result.land_exp or 0, land_max = result.land_max or 0,
        heal = held_sent and held_sent.heal or 0, held = held_sent, sent = #X.sent, refused = nrefused,
        why = result.why_name,
        -- a new attack / cast press this tick, and the weapon clock it met:
        -- the monitor stamps the swing it brings (input phase or tick phase)
        pressed = pressed, w = X.swing_next, still = still,
        -- a loc op under way: the monitor expects movement toward it
        loc = (want.mode == "loc" and want.x ~= nil) and { x = want.x, z = want.z, near = want.near } or nil,
    }
end
