-- quest-driver / tob2_budget: THE RESOURCE LAYER (ToB v2), the relay's
-- supplies as one budget (TOB2_DESIGN.md "The RESOURCE layer").
--
-- The local planner sees 12 ticks; the relay dies in Verzik P3 of a supply
-- problem spread over the whole raid (35 of the team's 40 super combat doses
-- gone before Verzik, Sotetseg drinking 49-64 brew doses). So:
--
--   STOCK    per seat, the doses carried, read from the pack each tick:
--            brew, food, restore (super restores and prayer potions),
--            combat (super combat), ranging.
--   NEED     per room, the doses a seat spends there (QD.T2.budget.NEED,
--            measured, below), Verzik split into P1+P2 and P3.
--   RESERVE  what the rooms after the current one in the relay's ORDER
--            need, net of the supply chests on the way (CHEST, measured):
--            spendable = the lowest the stock runs on the walk through the
--            later rooms. QD.T2.resources (tob2_run.lua) drinks or eats a
--            kind only while spendable > 0; the rescue on predicted death
--            ignores the reserve (a death ends the relay sooner than any
--            shortage), and so do a prayer restore under PRAYER_FLOOR points
--            and a brew or a food under HP_FLOOR hitpoints.
--   MONITOR  per room, the doses spent against NEED, in the solver summary.
--
-- The budget's state lives across room solves in this Lua state (one per
-- seat): QD.T2.budget.state = { order, spent[room][kind], last stock }.
-- Without an ORDER (a room test that is not the relay) the reserve is 0.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.budget = QD.T2.budget or {}

QD.T2.budget.KINDS = { "brew", "food", "restore", "combat", "ranging" }

-- A restore under this many prayer points ignores the reserve.
QD.T2.budget.PRAYER_FLOOR = 10

-- THE NEED TABLE: doses a seat spends per room, by seat (pid order, the
-- relay's role), by kind. SOURCE: runs/relay_v2_128 (the 128-seed all-v2
-- relay, 2026-10-12), the 89 PASSING relays; consume rows of the tick log
-- (one row = one dose or one fish), split by each seat's
-- "<room>.barrier.ready" PASS line, Verzik P3 from its "verzik.p2" PASS
-- line; the MEDIAN per seat over the passing relays (scratchpad
-- tob2/fixB/need.py). A room's segment runs from its barrier.ready to the
-- next one's, so the relay's top-up on arriving at a room is charged to the
-- room before it. NOTE: this is what the relays DID spend, not what P3
-- should get -- P3's combat 1 is the drained outcome the layer exists to
-- fix; a room or the relay may raise an entry with QD.T2.budget.set_need.
-- Team means (all 5 seats) beside each room, for reference.
QD.T2.budget.NEED = {
    -- team mean: brew 3.4, food 3.9, restore 11.3, combat 5.2, ranging 2.0
    maiden = {
        { brew = 0, food = 0, restore = 2, combat = 0, ranging = 1 },
        { brew = 0, food = 0, restore = 2, combat = 0, ranging = 1 },
        { brew = 1, food = 2, restore = 2, combat = 2, ranging = 0 },
        { brew = 1, food = 2, restore = 2, combat = 2, ranging = 0 },
        { brew = 0, food = 0, restore = 2, combat = 1, ranging = 0 } },
    -- team mean: brew 0.0, food 0.0, restore 0.0, combat 2.6, ranging 0.0
    bloat = {
        { brew = 0, food = 0, restore = 0, combat = 1, ranging = 0 },
        { brew = 0, food = 0, restore = 0, combat = 1, ranging = 0 },
        { brew = 0, food = 0, restore = 0, combat = 0, ranging = 0 },
        { brew = 0, food = 0, restore = 0, combat = 0, ranging = 0 },
        { brew = 0, food = 0, restore = 0, combat = 0, ranging = 0 } },
    -- team mean: brew 10.6, food 4.3, restore 24.6, combat 21.6, ranging 4.0
    nylocas = {
        { brew = 2, food = 1, restore = 4, combat = 5, ranging = 2 },
        { brew = 2, food = 1, restore = 5, combat = 5, ranging = 2 },
        { brew = 2, food = 0, restore = 5, combat = 4, ranging = 0 },
        { brew = 2, food = 0, restore = 5, combat = 4, ranging = 0 },
        { brew = 1, food = 1, restore = 5, combat = 5, ranging = 0 } },
    -- team mean: brew 48.8, food 0.1, restore 31.3, combat 0.7, ranging 0.0
    -- Sotetseg's super combats are POLICY (owner 2026-10-12: "Sotetseg should
    -- also prioritize super combats ... budget more super combats for sote"):
    -- measured spend was 0 (the doses went to the Nylocas), so the fight ran
    -- unboosted; two a seat (start boosted, re-boost after its brews). The walk
    -- holds them back from the Nylocas.
    sotetseg = {
        { brew = 10, food = 0, restore = 6, combat = 2, ranging = 0 },
        { brew = 10, food = 0, restore = 6, combat = 2, ranging = 0 },
        { brew = 10, food = 0, restore = 6, combat = 2, ranging = 0 },
        { brew = 10, food = 0, restore = 7, combat = 2, ranging = 0 },
        { brew = 10, food = 0, restore = 6, combat = 2, ranging = 0 } },
    -- team mean: brew 5.8, food 3.0, restore 1.8, combat 0.0, ranging 0.0
    xarpus = {
        { brew = 0, food = 0, restore = 0, combat = 0, ranging = 0 },
        { brew = 0, food = 0, restore = 0, combat = 0, ranging = 0 },
        { brew = 1, food = 0, restore = 0, combat = 0, ranging = 0 },
        { brew = 0, food = 0, restore = 0, combat = 0, ranging = 0 },
        { brew = 4, food = 1, restore = 0, combat = 0, ranging = 0 } },
    -- team mean: brew 0.4, food 2.8, restore 16.4, combat 5.0, ranging 0.0
    verzik_p12 = {
        { brew = 0, food = 0, restore = 3, combat = 1, ranging = 0 },
        { brew = 0, food = 0, restore = 3, combat = 1, ranging = 0 },
        { brew = 0, food = 0, restore = 3, combat = 1, ranging = 0 },
        { brew = 0, food = 0, restore = 3, combat = 1, ranging = 0 },
        { brew = 0, food = 1, restore = 3, combat = 1, ranging = 0 } },
    -- team mean: brew 51.8, food 15.6, restore 43.7, combat 4.9, ranging 0.0
    -- P3's super combats are POLICY, not the measured spend (2026-10-12): the
    -- relays spent 35 of the team's 40 doses before Verzik (Nylocas 21.6) and
    -- fought P3 on 1 a seat, brew-drained (Attack <= base 18-28% of P3). Four
    -- a seat: a restore + re-dose per 2-3 brews over ~360 ticks; the walk holds
    -- them back from the rooms before.
    verzik_p3 = {
        { brew = 12, food = 2, restore = 10, combat = 4, ranging = 0 },
        { brew = 11, food = 2, restore = 9, combat = 4, ranging = 0 },
        { brew = 10, food = 3, restore = 9, combat = 4, ranging = 0 },
        { brew = 11, food = 4, restore = 9, combat = 4, ranging = 0 },
        { brew = 8, food = 4, restore = 8, combat = 4, ranging = 0 } },
}

-- THE CHESTS: doses a seat buys at the supply chest after a room (the relay
-- buys at Bloat's and Sotetseg's), by seat, by kind (a brew or a restore is
-- 4 doses, a shark 1 food). SOURCE: the same 89 passing relays of
-- runs/relay_v2_128, the "<room>.chest_bought" PASS lines, the median per
-- seat (tob2/fixB/chest.py). Without them the reserve read the whole raid's
-- need against the pack alone, and Maiden could not brew at all (fixB f1:
-- three seats dead in Maiden).
QD.T2.budget.CHEST = {
    bloat = {
        { brew = 8, food = 0, restore = 0 }, { brew = 8, food = 0, restore = 0 },
        { brew = 0, food = 0, restore = 0 }, { brew = 4, food = 0, restore = 0 },
        { brew = 0, food = 0, restore = 0 } },
    sotetseg = {
        { brew = 0, food = 2, restore = 8 }, { brew = 0, food = 2, restore = 8 },
        { brew = 8, food = 3, restore = 8 }, { brew = 0, food = 5, restore = 8 },
        { brew = 4, food = 6, restore = 8 } },
}

-- NORMAL'S TABLES, a trio (the Hard ones above are five seats of Hard Mode's
-- spend: their P3 reserve of four super combat doses a seat held the Normal
-- kit's last doses out of P2, which then ran 600-1000 ticks unboosted and sent
-- the team into P3 on 0-4 restore doses - relay_v2a, 50 of 128 dead in P3).
-- SOURCE: the Normal v1 relay, 128 seeds (2026-10-17), its 112 passing
-- relays, measured exactly as the Hard tables (build script need.py): the
-- median a seat per room, and the chests' buys. P3's combat is the measured
-- one dose a seat.
QD.T2.budget.NEED_NORMAL = {
    -- team mean: brew 7.5, food 3.2, restore 10.1, combat 5.3, ranging 1.0
    maiden = {
        { brew = 0, food = 0, restore = 3, combat = 0, ranging = 1 },
        { brew = 7, food = 2, restore = 4, combat = 4, ranging = 0 },
        { brew = 0, food = 1, restore = 3, combat = 1, ranging = 0 } },
    -- team mean: brew 0.0, food 0.0, restore 0.0, combat 1.9, ranging 0.0
    bloat = {
        { brew = 0, food = 0, restore = 0, combat = 1, ranging = 0 },
        { brew = 0, food = 0, restore = 0, combat = 1, ranging = 0 },
        { brew = 0, food = 0, restore = 0, combat = 0, ranging = 0 } },
    -- team mean: brew 1.0, food 1.0, restore 15.3, combat 9.9, ranging 1.1
    nylocas = {
        { brew = 0, food = 1, restore = 5, combat = 4, ranging = 1 },
        { brew = 0, food = 0, restore = 5, combat = 1, ranging = 0 },
        { brew = 0, food = 0, restore = 5, combat = 5, ranging = 0 } },
    -- team mean: brew 19.1, food 1.4, restore 19.5, combat 0.8, ranging 0.0
    sotetseg = {
        { brew = 6, food = 0, restore = 6, combat = 1, ranging = 0 },
        { brew = 7, food = 0, restore = 7, combat = 0, ranging = 0 },
        { brew = 6, food = 0, restore = 6, combat = 0, ranging = 0 } },
    -- team mean: brew 0.0, food 0.0, restore 3.1, combat 0.0, ranging 0.0
    xarpus = {
        { brew = 0, food = 0, restore = 1, combat = 0, ranging = 0 },
        { brew = 0, food = 0, restore = 1, combat = 0, ranging = 0 },
        { brew = 0, food = 0, restore = 1, combat = 0, ranging = 0 } },
    -- team mean: brew 2.3, food 2.9, restore 15.8, combat 3.3, ranging 0.0
    -- P2's super combats are POLICY: its reds are a race - two a summon, ~280
    -- hp of heal every 44 ticks against a whip team's ~7 a tick - and an
    -- unboosted team lost it (n_relay rbt: her bar fell 1.9 a tick through
    -- the reds, 900 ticks). v1 kept its last two doses for P2's reds and P3.
    verzik_p12 = {
        { brew = 0, food = 1, restore = 5, combat = 2, ranging = 0 },
        { brew = 0, food = 1, restore = 5, combat = 2, ranging = 0 },
        { brew = 0, food = 0, restore = 5, combat = 2, ranging = 0 } },
    -- team mean: brew 38.8, food 3.5, restore 18.2, combat 2.7, ranging 0.0
    -- P3's restores are POLICY, not the measured median (2026-10-17): the v2
    -- relay's P3 losses entered on 3-4 restore doses a seat, were drained by
    -- mid-P3 and fought on at -99 (relay_v2g raf, +625 ticks); the chests
    -- buy toward ten, the brews they replace were 30-35 a seat at P3's door.
    verzik_p3 = {
        { brew = 14, food = 0, restore = 10, combat = 2, ranging = 0 },
        { brew = 11, food = 3, restore = 10, combat = 2, ranging = 0 },
        { brew = 14, food = 0, restore = 10, combat = 2, ranging = 0 } },
}
QD.T2.budget.CHEST_NORMAL = {
    bloat = { { brew = 8, food = 0, restore = 0 }, { brew = 12, food = 0, restore = 0 }, { brew = 0, food = 0, restore = 0 } },
    sotetseg = { { brew = 12, food = 1, restore = 8 }, { brew = 8, food = 4, restore = 4 }, { brew = 20, food = 0, restore = 4 } },
}

-- The mode's tables (QD.raid.tob_mode: the party's, set at the raid's entry).
function QD.T2.budget.need()
    return (QD.raid.tob_mode == "hard") and QD.T2.budget.NEED or QD.T2.budget.NEED_NORMAL
end

function QD.T2.budget.chest()
    return (QD.raid.tob_mode == "hard") and QD.T2.budget.CHEST or QD.T2.budget.CHEST_NORMAL
end

-- Under this many hitpoints a brew or a food ignores the reserve (the
-- rescue does too, but only on a death the plan can see coming).
QD.T2.budget.HP_FLOOR = 40

-- WHICH KINDS THE RESOURCE CHANNEL'S GATE HOLDS BACK. Probed 2026-10-12
-- (runs/fixB2 f1, f8, the full relay): gating brews and food in every room
-- killed seats in Sotetseg's maze both times (death balls of 41-65 against
-- a seat held off its brews; f1 had passed in relay_v2_128). So the gate
-- holds the BOOSTS -- super combat, ranging, and a super restore drunk for a
-- drained stat -- and the brew, food and prayer-restore budget is the
-- room's to act on through QD.T2.budget.spendable (its floors: Sotetseg's
-- "soon" brew floor is where its 49-64 doses go). true = gated.
QD.T2.budget.GATE = { combat = true, ranging = true, stat_restore = true, brew = false, food = false, restore = false }

-- The relay's room names -> the budget's rooms (Verzik in two parts).
QD.T2.budget.ROOM_OF = { verzik_p1 = "verzik_p12", verzik_p2 = "verzik_p12", verzik_p3 = "verzik_p3" }
QD.T2.budget.EXPAND = { verzik = { "verzik_p12", "verzik_p3" } }

QD.T2.budget.state = QD.T2.budget.state or { order = nil, spent = {}, last = nil, room = nil }

-- The relay's ORDER (tob_relay.lua: room names, "verzik" for all three
-- phases); the rooms after the current one are the reserve's.
function QD.T2.budget.set_order(order)
    assert(order)
    local list = {}
    for _, name in ipairs(order) do
        local ex = QD.T2.budget.EXPAND[name]
        if ex then
            for _, r in ipairs(ex) do list[#list + 1] = r end
        else
            list[#list + 1] = name
        end
    end
    QD.T2.budget.state.order = list
end

-- Raise (or set) a room's need for one seat and kind (a policy override).
function QD.T2.budget.set_need(room, seat, kind, doses)
    local need = QD.T2.budget.need()
    assert(need[room], "tob2 budget: no room " .. tostring(room))
    assert(need[room][seat], "tob2 budget: no seat " .. tostring(seat))
    need[room][seat][kind] = doses
end

-- The budget's room for a solve name ("verzik_p2" -> "verzik_p12").
function QD.T2.budget.room_of(name)
    return QD.T2.budget.ROOM_OF[name] or name
end

-- The obj ids of each kind, with the doses one carries; resolved once.
function QD.T2.budget.ids()
    local B = QD.T2.budget
    if B._ids then return B._ids end
    local out = { brew = {}, food = {}, restore = {}, combat = {}, ranging = {} }
    local function doses(kind, stem, prefixes)
        for _, pre in ipairs(prefixes) do
            for n = 1, 4 do
                local r, id = api_drive.symbol("obj", pre .. n .. stem)
                if r == "ok" then out[kind][#out[kind] + 1] = { id = id, n = n } end
            end
        end
        assert(#out[kind] >= 4, "tob2 budget: no obj " .. stem)
    end
    doses("brew", "dosepotionofsaradomin", { "br_", "" })
    doses("restore", "dose2restore", { "br_", "" })
    for n = 1, 4 do
        local r, id = api_drive.symbol("obj", n .. "doseprayerrestore")
        if r == "ok" then out.restore[#out.restore + 1] = { id = id, n = n } end
    end
    doses("combat", "dose2combat", { "br_", "" })
    doses("ranging", "doserangerspotion", { "br_", "" })
    for _, name in ipairs({ "anglerfish", "mantaray", "seaturtle", "shark" }) do
        local r, id = api_drive.symbol("obj", name)
        assert(r == "ok", "tob2 budget: no obj " .. name)
        out.food[#out.food + 1] = { id = id, n = 1 }
    end
    local ir, inv = api_drive.symbol("inv", "inv")
    assert(ir == "ok", "tob2 budget: no inv named inv")
    out.inv = inv
    B._ids = out
    return out
end

-- STOCK: { brew, food, restore, combat, ranging } doses in my pack now.
function QD.T2.budget.stock(S)
    local ids = QD.T2.budget.ids()
    local out = {}
    for _, kind in ipairs(QD.T2.budget.KINDS) do
        local total = 0
        for _, e in ipairs(ids[kind]) do
            local _, c = api_drive.inv_count(ids.inv, e.id)
            total = total + (c or 0) * e.n
        end
        out[kind] = total
    end
    return out
end

-- My seat in the NEED table (the relay's role, 1..5).
function QD.T2.budget.seat(S)
    local seat = S.role or 1
    if seat < 1 then seat = 1 end
    if seat > 5 then seat = 5 end
    return seat
end

-- SPENDABLE: doses of `kind` I may use in this room. Walk the rooms after
-- the current one, adding each chest's purchases when its room is passed
-- and taking each room's NEED; the lowest the running stock gets is what
-- this room may spend (a chest refills later rooms, never this one's
-- shortfall). No relay ORDER, or a room not in it: all of the stock.
function QD.T2.budget.spendable(S, kind, stock)
    stock = stock or (S.t2 and S.t2.budget_stock) or QD.T2.budget.stock(S)
    local st = QD.T2.budget.state
    local have = stock[kind] or 0
    local order = st.order
    if order == nil then return have end
    local room = QD.T2.budget.room_of(S.t2 and S.t2.room or st.room or "")
    local seat = QD.T2.budget.seat(S)
    local after = false
    local run, low = have, have
    for _, r in ipairs(order) do
        if after then
            local need = QD.T2.budget.need()[r]
            if need then run = run - (need[seat][kind] or 0) end
            if run < low then low = run end
        end
        if r == room then after = true end
        if after then
            -- the chest after room r refills the rooms after it
            local chest = QD.T2.budget.chest()[r]
            if chest then run = run + (chest[seat][kind] or 0) end
        end
    end
    if not after then return have end
    return math.max(0, low)
end

-- RESERVE: what the rooms after the current one need of `kind`, net of the
-- chests on the way (stock - spendable).
function QD.T2.budget.reserve(S, kind, stock)
    stock = stock or (S.t2 and S.t2.budget_stock) or QD.T2.budget.stock(S)
    return (stock[kind] or 0) - QD.T2.budget.spendable(S, kind, stock)
end

-- Once a tick (QD.T2.step): read the stock, count what went down since the
-- last tick as this room's spend (a pickup or a chest purchase raises it and
-- is not counted).
function QD.T2.budget.tick(S, F)
    local st = QD.T2.budget.state
    local room = QD.T2.budget.room_of(S.t2.room)
    local stock = QD.T2.budget.stock(S)
    st.spent[room] = st.spent[room] or {}
    local sp = st.spent[room]
    if st.last ~= nil and st.room == room then
        for _, kind in ipairs(QD.T2.budget.KINDS) do
            local dn = st.last[kind] - stock[kind]
            if dn > 0 then sp[kind] = (sp[kind] or 0) + dn end
        end
    end
    st.last, st.room = stock, room
    S.t2.budget_stock = stock
    QD.T2.budget.spec_tick(S, F)
    return stock
end

-- The resource channel's filter: S.ids with the lists of every kind that is
-- not spendable emptied, for one QD.raid._tob_supplies call.
function QD.T2.budget.filtered_ids(S, F)
    local stock = S.t2.budget_stock or QD.T2.budget.stock(S)
    local ids = {}
    for k, v in pairs(S.ids) do ids[k] = v end
    local held = {}
    for _, kind in ipairs(QD.T2.budget.KINDS) do
        held[kind] = QD.T2.budget.spendable(S, kind, stock) <= 0
    end
    -- (a prayer restore under PRAYER_FLOOR, a brew or a food under HP_FLOOR,
    -- ignore the reserve)
    if held.restore and (F.prayer or 0) < QD.T2.budget.PRAYER_FLOOR then held.restore = false end
    if (F.hp or 0) < QD.T2.budget.HP_FLOOR then held.brew, held.food = false, false end
    local G = QD.T2.budget.GATE
    if not G.brew then held.brew = false end
    if not G.food then held.food = false end
    if not G.combat then held.combat = false end
    if not G.ranging then held.ranging = false end
    -- (restores: the prayer list and the drained-stat list gate apart)
    local held_stat = QD.T2.budget.spendable(S, "restore", stock) <= 0 and G.stat_restore
    if not G.restore then held.restore = false end
    if held.brew then ids.brews = {} end
    if held.food then ids.food = {} end
    if held.restore then ids.restores = {} end
    if held.restore or held_stat then ids.stat_restores = {} end
    if held.combat then ids.combats = {} end
    if held.ranging then ids.rangings = {} end
    held.stat_restore = held_stat
    return ids, held
end

-- "budget nylocas brew 2/2 food 0/1 restore 4/4 combat 3/5 ranging 0/2"
-- (spent / need, per room so far, my seat).
function QD.T2.budget.summary(S)
    local st = QD.T2.budget.state
    local seat = QD.T2.budget.seat(S)
    local out = {}
    local rooms = st.order or { QD.T2.budget.room_of(S.t2 and S.t2.room or "") }
    for _, r in ipairs(rooms) do
        local sp = st.spent[r]
        if sp then
            local N = QD.T2.budget.need()
            local need = N[r] and N[r][seat] or {}
            local parts = {}
            for _, kind in ipairs(QD.T2.budget.KINDS) do
                parts[#parts + 1] = kind .. " " .. (sp[kind] or 0) .. "/" .. (need[kind] or 0)
            end
            out[#out + 1] = r .. " " .. table.concat(parts, " ")
        end
    end
    local stock = st.last
    local stext = ""
    if stock then
        local parts = {}
        for _, kind in ipairs(QD.T2.budget.KINDS) do parts[#parts + 1] = kind .. " " .. stock[kind] end
        stext = "; stock " .. table.concat(parts, " ")
    end
    return "budget " .. table.concat(out, " | ") .. stext .. "; " .. QD.T2.budget.spec_summary(S)
end

-- ============================================================ SPECIAL ENERGY
--
-- Owner rule (2026-10-12): "budget at least 1 special attack saved for the
-- start of P3, but you also shouldn't be sitting at 100% spec before P3."
--
-- THE ENERGY MODEL, measured (runs/relay_bud8, raider rows' last field):
--   * regeneration +100 every 50 ticks = 2 a tick (899 of 899 rises on
--     ticks with no special; the kit carries no Lightbearer);
--   * EVERY ROOM CLEAR SETS IT TO 1000 for the whole party (~tob_restore
--     via ~tob_restore_party, tob_raid.rs2; the wiki: "hitpoints, prayer,
--     run energy and special attack energy of all team members are restored
--     to full"); the jumps in the log sit on the Maiden and Sotetseg clears,
--     where seats had spent below 1000. The last restore before P3 is
--     Xarpus' clear: from there P3 is a median 553 ticks away (P1 130, P2
--     310; the 97 passing relays of relay_v2_128 + relay_bud8, fixS/dur.py).
-- So before Verzik a special is never owed to P3 (a clear refills it), and
-- in P1/P2 a special is affordable when what is left plus the regeneration
-- until P3 still covers the P3 weapon's cost.
QD.T2.budget.SPEC_REGEN = 2            -- energy a tick
QD.T2.budget.SPEC_FULL = 1000
-- median phase lengths (ticks) on the way to P3
QD.T2.budget.SPEC_TICKS = { verzik_p1 = 130, verzik_p2 = 310 }
-- the special weapons' costs (energy, 0-1000; the obj's sa_energy)
-- (the Dawnbringer is `verzik_special_weapon` in this cache: under the name
-- "dawnbringer" no symbol resolved, the gate charged its special the default
-- 500 and refused it at 350-499 energy - P1 waited ~100 ticks a seat for a
-- third special, 250 ticks to v1's 182 in Normal)
QD.T2.budget.SPEC_COST = { bone_claws = 350, dragon_claws = 500, dragon_dagger = 250, crystal_halberd = 300,
                           dragon_warhammer = 500, verzik_special_weapon = 350, toxic_blowpipe = 500 }
-- the pre-P3 dump weapon (the kit's one-handed hammer), its cost and speed
QD.T2.budget.DUMP_WEAPON = "dragon_warhammer"
QD.T2.budget.DUMP_SPEED = 6
-- "use it or lose it": dump when energy is within this many ticks of
-- regeneration of the cap
QD.T2.budget.DUMP_HORIZON = 20

-- The relay's P3 special weapon (tob_relay.lua SPEC_WEAPON): its cost is the
-- energy owed to the start of P3. nil = no reserve.
function QD.T2.budget.set_spec(weapon)
    local st = QD.T2.budget.state
    if weapon == nil or weapon == false then
        st.spec_reserve = 0
        return
    end
    local cost = QD.T2.budget.SPEC_COST[weapon]
    assert(cost, "tob2 budget: no special cost for " .. tostring(weapon))
    st.spec_reserve = cost
end

-- My special energy now (the measure's F.energy, kept by the tick).
function QD.T2.budget.energy(S)
    local e = S.t2 and S.t2.energy
    if e ~= nil then return e end
    local r, v = api_drive.varp(S.ids.energy)
    assert(r == "ok", "tob2 budget: no special energy varp")
    return v
end

-- Ticks from now to the start of P3 when no room clear lies between, else
-- nil (a clear refills the energy first).
function QD.T2.budget.ticks_to_p3(S)
    local name = S.t2 and S.t2.room or ""
    local el = 0
    if S.start ~= nil and S.t2 and S.t2.now ~= nil then el = math.max(0, S.t2.now - S.start) end
    local T = QD.T2.budget.SPEC_TICKS
    if name == "verzik_p1" then return math.max(0, T.verzik_p1 - el) + T.verzik_p2 end
    if name == "verzik_p2" then return math.max(0, T.verzik_p2 - el) end
    if name == "verzik_p3" then return 0 end
    return nil
end

-- May I spend `cost` energy now? Always while a room clear lies ahead (no
-- reserve with no relay ORDER either); in P1/P2 only when min(1000, energy -
-- cost + regen x ticks to P3) still covers the P3 weapon's cost; in P3 when
-- I have it.
function QD.T2.budget.spec_ok(S, cost)
    assert(cost)
    local e = QD.T2.budget.energy(S)
    if e < cost then return false end
    local st = QD.T2.budget.state
    local reserve = st.spec_reserve or 0
    if st.order == nil or reserve <= 0 then return true end
    local ticks = QD.T2.budget.ticks_to_p3(S)
    if ticks == nil or ticks == 0 then return true end
    local at_p3 = math.min(QD.T2.budget.SPEC_FULL, e - cost + QD.T2.budget.SPEC_REGEN * ticks)
    return at_p3 >= reserve
end

-- Should the spec channel dump a special now ("use it or lose it")? Energy
-- within DUMP_HORIZON ticks of regeneration of the cap, and affordable.
function QD.T2.budget.dump_due(S, cost)
    local e = QD.T2.budget.energy(S)
    if e < QD.T2.budget.SPEC_FULL - QD.T2.budget.SPEC_REGEN * QD.T2.budget.DUMP_HORIZON then return false end
    return QD.T2.budget.spec_ok(S, cost)
end

-- The special energy monitor, once a tick: per room, specials fired (a fall
-- of the energy), ticks, ticks at 1000; the energy entering P3.
function QD.T2.budget.spec_tick(S, F)
    local st = QD.T2.budget.state
    local room = QD.T2.budget.room_of(S.t2.room)
    local e = F.energy or 0
    S.t2.energy, S.t2.now = e, F.tick
    st.spec = st.spec or {}
    local m = st.spec[room]
    if m == nil then
        m = { fired = 0, ticks = 0, full = 0, first = e }
        st.spec[room] = m
    end
    m.ticks = m.ticks + 1
    if e >= QD.T2.budget.SPEC_FULL then m.full = m.full + 1 end
    if st.spec_last ~= nil and st.spec_room == room and e < st.spec_last then m.fired = m.fired + 1 end
    st.spec_last, st.spec_room = e, room
end

-- "spec maiden fired 2 full 31% | ...; p3 in 650 (owed 350)"
function QD.T2.budget.spec_summary(S)
    local st = QD.T2.budget.state
    if st.spec == nil then return "spec -" end
    local out = {}
    local rooms = st.order or { QD.T2.budget.room_of(S.t2 and S.t2.room or "") }
    for _, r in ipairs(rooms) do
        local m = st.spec[r]
        if m then
            out[#out + 1] = string.format("%s fired %d full %d%%", r, m.fired, (100 * m.full) // math.max(1, m.ticks))
        end
    end
    local p3 = st.spec.verzik_p3
    return "spec " .. table.concat(out, " | ") .. (p3 and ("; p3 in " .. p3.first) or "")
        .. " (owed " .. tostring(st.spec_reserve or 0) .. ")"
end

-- What a seat needs of `kind` in the rooms after `room` ("bloat" or
-- "sotetseg": the supply chests), by the relay's ORDER (else every room
-- after it): the relay's restock buys to this (tob_relay.lua restock).
function QD.T2.budget.later_need(seat, room, kind)
    local B = QD.T2.budget
    local order = B.state.order or { "maiden", "bloat", "nylocas", "sotetseg", "xarpus", "verzik_p12", "verzik_p3" }
    if seat < 1 then seat = 1 end
    if seat > 5 then seat = 5 end
    local after, total = false, 0
    for _, r in ipairs(order) do
        local N = B.need()
        if after and N[r] then total = total + (N[r][seat][kind] or 0) end
        if r == room then after = true end
    end
    return total
end

-- ================================================================ THE BOARD
--
-- THE SUPPLY BOARD (owner 2026-10-12: "You can use the chat to coordinate
-- which seats get what resources"; "the scriptrun lane must use real chat.
-- It must use the same channels as the live client"). Every seat says ONE
-- public chat line -- "sup <exchange> <seat> <healing> <brews> <fish>
-- <slots> <need>", digits and spaces, well under the 80 characters a line
-- carries -- through api_drive.say (the client's own MESSAGE_PUBLIC), and
-- every seat reads every seat's line from the player-info chat block
-- (api_drive.messages, type 2, under the speaker's name; QD.chat.public).
-- Said after tick T, read by every seat in view at T+1, the speaker too.
-- The allocation below is then computed from the same numbers on every seat.
QD.T2.board = QD.T2.board or {}
QD.T2.board.FIELDS = 5

-- The newest chat serial now: read lines after it.
function QD.T2.board.mark()
    local r, _, newest = QD.chat.public(0)
    assert(r == "ok", "tob2 board: the chat answered " .. tostring(r))
    return newest
end

-- Say my line for exchange `seq`. -> the verb's answer
function QD.T2.board.say(seq, seat, vals)
    assert(math.type(seq) == "integer")
    assert(math.type(seat) == "integer")
    assert(#vals == QD.T2.board.FIELDS, "tob2 board: a line carries " .. QD.T2.board.FIELDS .. " values")
    local parts = { "sup", tostring(seq), tostring(seat) }
    for _, v in ipairs(vals) do
        assert(math.type(v) == "integer" and v >= 0, "tob2 board: value " .. tostring(v))
        parts[#parts + 1] = tostring(v)
    end
    local line = table.concat(parts, " ")
    assert(#line <= 80, "tob2 board: line over 80 characters")
    return QD.chat.say(line)
end

-- Read the lines said since `since` for exchange `seq` into `got` (seat ->
-- values), by the speaker's NAME (`names[seat]`, the relay's party list);
-- the seat number in the line must agree with the speaker. -> newest serial
function QD.T2.board.collect(seq, names, since, got)
    local r, rows, newest = QD.chat.public(since)
    assert(r == "ok", "tob2 board: the chat answered " .. tostring(r))
    for _, m in ipairs(rows) do
        local text = string.lower(m.text or "")
        local q, seat, rest = text:match("^sup (%d+) (%d+) (.*)$")
        if q and tonumber(q) == seq then
            seat = tonumber(seat)
            local who = nil
            for n, name in ipairs(names) do
                if QD.party._same(m.sender, name) then who = n end
            end
            assert(who ~= nil, "tob2 board: a supply line from " .. tostring(m.sender) .. ", not of the party")
            assert(who == seat, "tob2 board: " .. tostring(m.sender) .. " said seat " .. seat .. ", is seat " .. who)
            local vals = {}
            for v in rest:gmatch("%d+") do vals[#vals + 1] = tonumber(v) end
            assert(#vals == QD.T2.board.FIELDS, "tob2 board: a line of " .. #vals .. " values: " .. text)
            got[seat] = vals
        end
    end
    return newest
end

-- THE ALLOCATION (pure; every seat computes it from the same board).
-- rows[seat] = { heal = hp of healing carried (16 a brew dose, 20 a fish),
-- full = full brews it may give, fish = fish it may give, slots = free
-- slots it may fill, need = hp of healing its later rooms need }.
-- Target per seat = the team's healing split in proportion to need (equal
-- when no seat needs any); then, repeatedly, the largest deficit takes from
-- the largest surplus one whole item -- a full brew (64) when the giver's
-- surplus and the taker's deficit are each at least half of it, else a fish
-- (20) on the same rule -- ties to the lower seat. -> transfers
-- { {from, to, kind = "brew"|"fish"}, ... }, targets {seat -> hp}.
QD.T2.budget.ITEM_HP = { brew = 64, fish = 20 }

function QD.T2.budget.allocate(rows)
    local n = #rows
    assert(n >= 1)
    local total, needs = 0, 0
    for s = 1, n do
        local r = rows[s]
        assert(r.heal and r.full and r.fish and r.slots and r.need, "tob2 allocate: seat " .. s .. " row incomplete")
        total = total + r.heal
        needs = needs + r.need
    end
    local target, have, full, fish, slots = {}, {}, {}, {}, {}
    for s = 1, n do
        local r = rows[s]
        target[s] = (needs > 0) and (total * r.need / needs) or (total / n)
        have[s], full[s], fish[s], slots[s] = r.heal, r.full, r.fish, r.slots
    end
    local moves = {}
    for _ = 1, 200 do
        -- the largest deficit that has a slot
        local to, dmax = nil, 0
        for s = 1, n do
            local def = target[s] - have[s]
            if slots[s] > 0 and def > dmax then to, dmax = s, def end
        end
        if to == nil then break end
        local done = false
        for _, kind in ipairs({ "brew", "fish" }) do
            if not done then
                local hp = QD.T2.budget.ITEM_HP[kind]
                if dmax >= hp / 2 then
                    local from, smax = nil, 0
                    for s = 1, n do
                        local stock = (kind == "brew") and full[s] or fish[s]
                        local sur = have[s] - target[s]
                        if s ~= to and stock > 0 and sur >= hp / 2 and sur > smax then from, smax = s, sur end
                    end
                    if from then
                        moves[#moves + 1] = { from = from, to = to, kind = kind }
                        have[from], have[to] = have[from] - hp, have[to] + hp
                        if kind == "brew" then full[from] = full[from] - 1 else fish[from] = fish[from] - 1 end
                        slots[to] = slots[to] - 1
                        slots[from] = slots[from] + 1
                        done = true
                    end
                end
            end
        end
        if not done then break end
    end
    return moves, target
end
