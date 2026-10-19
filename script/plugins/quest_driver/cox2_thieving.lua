-- quest-driver / cox2_thieving: the thieving room solver (COX_FINISH_PLAN.md round 2).
-- Owned by the crabs/thieving unit; a QD.T2 room registered as "cox_thieving".
--
-- Plan: docs/minigames/cox/rooms/thieving_solver.md; spec thieving_spec.md.
-- The strategy is Synq's (0:18:13) and the wiki's: open the nearest chest,
-- hold the op until it opens (one attempt a tick, Mod Ash), and feed the
-- trough early so the scavenger eats while I thieve; never reopen a chest that
-- turned out to be a hazard (poison or the bats: both show the open loc 29743,
-- the de0 plugin's PCHEST). Done when the scavenger is the sleeping form
-- (7603): it has left the corridor.
--
-- Perception: api_drive.locs (chests, the trough), api_drive.npcs (the
-- scavenger's form), api_drive.inv_count (my grubs). Nothing server-side.
-- ==========================================================================

QD.COXTH = QD.COXTH or {}

QD.COXTH.V = {
    DEPOSIT_AT = 10,       -- grubs held before a trip to the trough (Synq ~8, wiki 10-15)
    NEED_SOLO = 30,        -- grubs a solo raid's scavenger eats (wiki, Synq)
    SLEEP_WAIT = 70,       -- ticks after the last deposit before more grubs are fetched
    RADIUS = 40,
}

function QD.COXTH.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_thieving", opts)
    local room = QD.raid._tob_symbols("cox_thieving", {
        { "chest_closed", "loc", "raids_thievingchest_closed" }, { "chest_hazard", "loc", "raids_thievingchest_open" },
        { "chest_grubs", "loc", "raids_thievingchest_eggs_hatched" }, { "chest_done", "loc", "raids_thievingchest_eggs_whole" },
        { "trough_empty", "loc", "raids_thievingchest_foodtrough_empty" },
        { "trough_full", "loc", "raids_thievingchest_foodtrough_full" },
        { "scav", "npc", "raids_thievingchest_beast_active" }, { "scav_asleep", "npc", "raids_thievingchest_beast_sleeping" },
        { "grubs", "obj", "raids_thievingchest_grubs" }, { "bat", "obj", "raids_bat6_cooked" },
        { "stamina", "obj", "4dosestamina" },
        { "stinkhorn", "obj", "raids_stinkhorn_mushroom" }, { "juice", "obj", "raids_endarkened_juice" },
        { "cicely", "obj", "raids_cicely" },
    })
    for k, v in pairs(room) do ids[k] = v end
    QD.COX.raid_restores(ids)
    ids.spares = QD.COX.spare_ids()
    return ids
end

-- A full pack's least-needed doses, in the order put down to make room
-- (Synq: "Drop a potion if you need to"): a stamina, an empty vial, a
-- revitalisation, a xeric's aid (lowest dose first). The kit's one free slot
-- is the scavengers' juice by the time a puzzle room comes. -> { obj id }
function QD.COX.spare_ids()
    local out = {}
    for _, name in ipairs({ "1dosestamina", "2dosestamina", "3dosestamina", "4dosestamina", "raids_vial_empty" }) do
        local r, id = api_drive.symbol("obj", name)
        if r == "ok" then out[#out + 1] = id end
    end
    -- then a restore, then a heal (the supply families: the raid's own, then
    -- the outside super restores and Saradomin brews)
    for _, fam in ipairs({ "restore", "heal" }) do
        for _, id in ipairs(QD.COX.supply_ids(fam)) do out[#out + 1] = id end
    end
    return out
end

-- The first of S.ids.spares in the pack -> obj id, or nil. lend: the dose
-- to take back after the room ({ id }; nil for an empty vial, left down).
function QD.COX.spare(S)
    for _, obj in ipairs(S.ids.spares) do
        local r, n = api_drive.inv_count(S.ids.inv, obj)
        if r == "ok" and n > 0 then
            local vr, vial = api_drive.symbol("obj", "raids_vial_empty")
            return obj, (not (vr == "ok" and vial == obj)) and { id = obj } or nil
        end
    end
    return nil, nil
end

-- A dose put down to make room, taken back off this room's floor (Take on
-- its tile, re-pressed every 8 ticks); given up when it is gone (a drop lies
-- 300 ticks), the pack is full again, or 40 ticks pass. -> true while taking
function QD.COX.retake_lent(S, F, intents, room, lent)
    if lent == nil or lent.done then return false end
    lent.t0 = lent.t0 or F.tick
    if F.tick - lent.t0 > 40 or QD.T2.exec.free_slots(S) == 0 then
        lent.done = true
        api_drive.ticklog_mark(string.format("cox retake %d: given up (%s)", lent.id,
            (F.tick - lent.t0 > 40) and "40 ticks" or "pack full"))
        return false
    end
    local r, rows = api_drive.objs(24)
    local row = nil
    if r == "ok" then
        for _, o in ipairs(rows) do
            if o.obj_id == lent.id and o.x >= room.ox and o.x < room.ox + 32 and o.z >= room.oz and o.z < room.oz + 32 then
                row = o
            end
        end
    end
    if row == nil then
        lent.done = true
        api_drive.ticklog_mark(string.format("cox retake %d: %s", lent.id, lent.pressed and "taken" or "not on the floor"))
        return false
    end
    lent.pressed = true
    if (F.tick - lent.t0) % 8 == 7 then return true end
    intents.world_op = { kind = "obj", id = row.obj_id, op = 3, x = row.x, z = row.z }
    return true
end

-- The room's reward, on the floor under the scavenger as it gets up (RSProx
-- 3389, docs/rsprox-analysis/cox/thieving.md RH-S3; it lies 200 ticks): each
-- of the three secondaries taken while the pack has a stack of it or a free
-- slot; given up after 30 ticks. -> true while taking
function QD.COXTH.take_reward(S, F, intents)
    local th, ids = S.th, S.ids
    if th.reward_done then return false end
    th.reward_t0 = th.reward_t0 or F.tick
    if F.tick - th.reward_t0 > 30 then
        th.reward_done = true
        return false
    end
    local r, rows = api_drive.objs(24)
    local room = th.room
    if r == "ok" and room ~= nil then
        for _, o in ipairs(rows) do
            if (o.obj_id == ids.stinkhorn or o.obj_id == ids.juice or o.obj_id == ids.cicely)
                and o.x >= room.ox and o.x < room.ox + 32 and o.z >= room.oz and o.z < room.oz + 32 then
                local cr, held = api_drive.inv_count(ids.inv, o.obj_id)
                if (cr == "ok" and held > 0) or QD.T2.exec.free_slots(S) > 0 then
                    if (F.tick - th.reward_t0) % 8 ~= 7 then
                        intents.world_op = { kind = "obj", id = o.obj_id, op = 3, x = o.x, z = o.z }
                    end
                    return true
                end
            end
        end
    end
    th.reward_done = true
    return false
end

-- This tick's room: chests (closed, by tile), the trough, the scavenger.
function QD.COXTH.see(S, F)
    local ids, th = S.ids, S.th
    if th.room == nil then
        local here = QD.COX.here()
        if here ~= nil and here.name == "thieving" then th.room = here end
    end
    local lr, rows = api_drive.locs(QD.COXTH.V.RADIUS)
    local closed, trough = {}, nil
    -- (this room's 32 tiles only: the room next door can hold a trough too)
    local room = th.room
    if lr == "ok" then
        for _, row in ipairs(rows) do
            local id = row.resolved_loc_id or row.loc_id
            if room ~= nil and (row.x < room.ox or row.x >= room.ox + 32 or row.z < room.oz or row.z >= room.oz + 32) then
                id = -1
            end
            local key = row.x .. "," .. row.z
            if id == ids.chest_closed then
                closed[#closed + 1] = row
            elseif id == ids.chest_hazard then
                -- the chest I was opening turned out a hazard: one opening of it
                if not th.hazard[key] and th.target and th.target.x == row.x and th.target.z == row.z then
                    th.hazard_hits[key] = (th.hazard_hits[key] or 0) + 1
                end
                th.hazard[key] = true
            elseif id == ids.trough_empty or id == ids.trough_full then
                trough = row
            end
        end
    end
    th.closed, th.trough = closed, trough
    th.asleep, th.scav = false, nil
    for _, row in ipairs(F.npcs) do
        if row.npc_id == ids.scav_asleep then th.asleep = true end
        if row.npc_id == ids.scav or row.npc_id == ids.scav_asleep then th.scav = row end
    end
    local ir, held = api_drive.inv_count(ids.inv, ids.grubs)
    held = (ir == "ok" and held) or 0
    -- grubs that left my pack went into the trough (nothing else takes them)
    if th.held ~= nil and held < th.held then
        th.deposited = th.deposited + (th.held - held)
        th.last_deposit = F.tick
    end
    if th.held ~= nil and held > th.held then th.opened = th.opened + 1 end
    th.held = held
end

-- The nearest closed chest I have not seen turn into a hazard, by the
-- client's own route length (ties: Chebyshev).
function QD.COXTH.pick(S, F)
    local th = S.th
    if th.target ~= nil then
        local key = th.target.x .. "," .. th.target.z
        for _, row in ipairs(th.closed) do
            if row.x == th.target.x and row.z == th.target.z and not th.hazard[key] then return th.target end
        end
        -- it opened (or proved a hazard): a tick with no op, so the next one
        -- is a new press even on the same chest relocked (the executive sends
        -- an op again only when the intention changes)
        th.target = nil
        return "gap"
    end
    local best, bd = nil, nil
    for _, row in ipairs(th.closed) do
        if not th.hazard[row.x .. "," .. row.z] then
            local d = QD.raid._tob_cheb(F.me.x, F.me.z, row.x, row.z)
            if bd == nil or d < bd then best, bd = row, d end
        end
    end
    -- (by tile, never by element: a chest's scene element is a new one each
    -- time it relocks, and a stale element answers not_found)
    th.target = best and { x = best.x, z = best.z, id = best.loc_id } or nil
    return th.target
end

function QD.COXTH.strategy(S, F, B)
    local th, V = S.th, QD.COXTH.V
    local intents = { mode = "thieve" }
    if th.room == nil then
        local room, inside = QD.COX.lookup_room(F, "thieving", th)
        if room == nil then
            intents.mode = "no-room"
            return intents
        end
        if not inside then
            -- in through the corridor: toward its centre
            intents.mode = "walk-in"
            B.pull({ name = "in", x = room.ox + 16, z = room.oz + 16, size = 3, weight = 10, t0 = F.tick + 1, t1 = F.tick + B.h })
            return intents
        end
        th.room = room
    end
    QD.COXTH.see(S, F)
    if th.asleep then
        -- the dose put down to make room comes back: a slot for it from the
        -- grubs left over (worthless once it sleeps), else a bat
        if th.lent ~= nil and not th.lent.done and QD.T2.exec.free_slots(S) == 0 then
            for _, obj in ipairs({ S.ids.grubs, S.ids.bat }) do
                local r, n = api_drive.inv_count(S.ids.inv, obj)
                if r == "ok" and n > 0 then
                    intents.mode = "room-for-dose"
                    intents.drop = obj
                    return intents
                end
            end
        end
        if QD.COX.retake_lent(S, F, intents, th.room, th.lent) then
            intents.mode = "retake"
            return intents
        end
        intents.mode = QD.COXTH.take_reward(S, F, intents) and "reward" or "done"
        return intents
    end
    local need = V.NEED_SOLO + th.extra - th.deposited
    -- deposited everything it needs and still awake a while later: it got
    -- hungry again, fetch a few more (wiki: "its HP can regenerate")
    if need <= 0 and th.last_deposit and F.tick - th.last_deposit > V.SLEEP_WAIT then
        th.extra = th.extra + 3
        th.last_deposit = F.tick
        need = V.NEED_SOLO + th.extra - th.deposited
    end
    local feed = th.held > 0 and (th.held >= V.DEPOSIT_AT or th.held >= need)
    if need <= 0 and th.held == 0 then
        intents.mode = "wait"
        return intents
    end
    if feed and th.trough then
        intents.mode = "feed"
        intents.world_op = { kind = "loc", id = th.trough.loc_id, op = 1, x = th.trough.x, z = th.trough.z }
        return intents
    end
    -- A FULL PACK with no grubs cannot take any (Synq: "Make sure you have at
    -- least one inventory slot available ... Drop a potion if you need to"):
    -- a bat goes first (the bat chest fills the slot), then a dose
    -- (QD.COX.spare_ids), taken back when the scavenger sleeps
    if th.held == 0 and QD.T2.exec.free_slots(S) == 0 then
        local r, n = api_drive.inv_count(S.ids.inv, S.ids.bat)
        if r == "ok" and n > 0 then
            intents.mode = "make-room"
            intents.drop = S.ids.bat
            return intents
        end
        local spare, lend = QD.COX.spare(S)
        if spare ~= nil then
            intents.mode = "make-room"
            intents.drop = spare
            th.lent = th.lent or lend
            return intents
        end
    end
    local chest = QD.COXTH.pick(S, F)
    if chest == "gap" then
        intents.mode = "gap"
        return intents
    end
    if chest == nil then
        if th.held > 0 and th.trough then
            intents.mode = "feed"
            intents.world_op = { kind = "loc", id = th.trough.loc_id, op = 1, x = th.trough.x, z = th.trough.z }
        else
            intents.mode = "wait"
        end
        return intents
    end
    intents.world_op = { kind = "loc", id = chest.id, op = 1, x = chest.x, z = chest.z }
    return intents
end

QD.T2.register("cox_thieving", {
    ids = function(opts) return QD.COXTH.ids(opts) end,

    init = function(S, opts)
        S.who = "cox_thieving_solve"
        S.th = { hazard = {}, hazard_hits = {}, closed = {}, trough = nil, held = nil, deposited = 0, opened = 0, extra = 0,
                 asleep = false, target = nil, last_deposit = nil }
    end,

    enter = function(S) end,

    predict = function(S, F, B) end,

    strategy = function(S, F, B) return QD.COXTH.strategy(S, F, B) end,

    done = function(S, F)
        if S.th.no_room ~= nil then return "no_room" end
        if S.th.asleep and (S.th.lent == nil or S.th.lent.done) and S.th.reward_done then return "ok" end
        return nil
    end,

    summary = function(S)
        local th = S.th
        local hz = {}
        for k in pairs(th.hazard) do hz[#hz + 1] = k end
        table.sort(hz)
        if th.no_room ~= nil then return "thieving: " .. th.no_room end
        return string.format("thieving: deposited %d (extra %d), chests with grubs %d, hazards seen {%s}, asleep %s",
            th.deposited, th.extra, th.opened, table.concat(hz, " "), tostring(th.asleep))
    end,
})

-- The number in my player's name (a scriptrun bot is "<name>_p<seat>"): a room
-- test's layout index, so a sweep th0..th47 sees every variant and turn.
function QD.raid.cox_run_n()
    local pr, players = api_drive.players()
    if pr ~= "ok" then return 0 end
    for _, p in ipairs(players) do
        if p.me then
            return tonumber(string.match(tostring(p.name), "(%d+)_p%d+$") or "0") or 0
        end
    end
    return 0
end

-- Through `room`'s exit Passage, and proven through: one Enter on it (the
-- server walks me there; the Passage puts me on its far side,
-- cox.rs2 [oploc1,raids_doorway]), then up to `ticks` ticks for my tile to
-- jump across it. A Passage that cannot be reached (the way blocked: the
-- scavenger, the crabs' big crystal) never jumps. -> "ok" | "blocked", detail
function QD.raid.cox_take_exit(room, ticks)
    local p = QD.COX.exit_passage(room)
    if p == nil then return "not_found", "no passage" end
    local _, before = api_drive.player_tile()
    local r = QD.raid.tob_loc_op("raids_doorway", 1, { x = p.x, z = p.z }, 40)
    if r ~= "ok" then return r, "passage op" end
    local last = before
    for _ = 1, ticks or 30 do
        QD.ticks(1)
        local _, me = api_drive.player_tile()
        -- a teleport: more than one run step (2 tiles) in a tick
        if last and me and math.max(math.abs(me.x - last.x), math.abs(me.z - last.z)) > 2 then
            return "ok", string.format("%d,%d -> %d,%d", last.x, last.z, me.x, me.z)
        end
        last = me
    end
    return "blocked", string.format("passage %d,%d not taken; at %d,%d", p.x, p.z, last.x, last.z)
end

-- Does the client's route from my tile end beside the sx-by-sz footprint at
-- (x, z) (within one tile of it)? (api_drive.route's arrive)
function QD.raid.cox_reaches(x, z, sx, sz)
    local rr, route = api_drive.route(x, z)
    if rr ~= "ok" or type(route) ~= "table" or route.arrive == nil then return false end
    local ax = route.arrive.x or route.arrive[1]
    local az = route.arrive.z or route.arrive[2]
    local dx = math.max(x - ax, 0, ax - (x + sx - 1))
    local dz = math.max(z - az, 0, az - (z + sz - 1))
    return math.max(dx, dz) <= 1
end

-- Walk from where ::coxtest left me (past the room before's Passage, which
-- may still be in that room's cell) into `name`'s cell, toward its centre
-- (cx, cz). -> "ok", the room | "stuck", detail
function QD.raid.cox_walk_into(name, cx, cz, ticks)
    local here = QD.COX.here()
    if here ~= nil and here.name == name then return "ok", here end
    local wr, wd = QD.raid.cox_walk_near(cx, cz, ticks or 60)
    here = QD.COX.here()
    if here ~= nil and here.name == name then return "ok", here end
    return "stuck", tostring(wr) .. " " .. tostring(wd) .. " in " .. tostring(here and here.name)
end

-- The client's loc rows within `radius` (for tests and probes).
function QD.raid.cox_locs(radius)
    return api_drive.locs(radius or 40)
end

-- The last `n` chat lines the client holds, newest last (probes).
function QD.raid.cox_messages(n)
    local r, rows = api_drive.messages(n or 10)
    local out = {}
    if r == "ok" then
        for i = 1, #rows do out[#out + 1] = tostring(rows[i].text) end
    end
    return out
end

-- QD.raid.cox_test_enter with a longer wait for ::coxtest's answer: the
-- search for the n-th layout spreads over ticks (p_delay), and from n ~ 36 on
-- it answers after more than cox_test_enter's 6 ticks.
function QD.raid.cox_test_enter_long(room, n, ticks)
    local id = QD.COX.ROOM[room]
    assert(id, "cox_test_enter_long: no room named " .. tostring(room))
    local _, since = api_drive.message_serial()
    QD.cheat(string.format("::coxtest %d %d", id, n or 0), false)
    for _ = 1, ticks or 60 do
        QD.ticks(1)
        local r, rows = api_drive.messages(20)
        if r == "ok" then
            for i = 1, #rows do
                if (rows[i].serial or 0) > (since or 0) then
                    local s, x, z = string.match(rows[i].text or "", "coxtest seed (%d+) centre (%d+),(%d+)")
                    if s then
                        return "ok", { seed = tonumber(s), cx = tonumber(x), cz = tonumber(z) }
                    end
                    if string.find(rows[i].text or "", "coxtest:", 1, true) then
                        return "refused", rows[i].text
                    end
                end
            end
        end
    end
    return "timeout", "no ::coxtest answer"
end


-- THE ROOM named `name`, for a solver called from wherever the room before
-- left me (the whole raid's contract): the relay leaves me on the room
-- before's way-out edge, up to 32 tiles from this room's middle, and the
-- middle can lie outside the zones the client holds (raid1 prep relay: the
-- thieving room at floor 2's start, looked up at its middle, never found).
-- Asked as the ice demon solver asks: the cell I stand in; the room ahead
-- of it (QD.COX.room_ahead: its tile nearest me); each of the 4 cells beside
-- mine at ITS tile nearest me. -> room, inside (I stand in it), or nil, why
function QD.COX.find_room(name)
    local r, me = api_drive.player_tile()
    if r ~= "ok" or me == nil then return nil, false, "no player tile (" .. tostring(r) .. ")" end
    local here = QD.COX.room_at(me.x, me.z, me.level)
    if here ~= nil and here.name == name then return here, true end
    if here ~= nil then
        local ahead = QD.COX.room_ahead(here)
        if ahead ~= nil and ahead.name == name then return ahead, false end
    end
    local best, bd = nil, nil
    local cx, cz = me.x - me.x % 32, me.z - me.z % 32
    for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        local nx, nz = cx + 32 * d[1], cz + 32 * d[2]
        local px = math.max(nx, math.min(nx + 31, me.x))
        local pz = math.max(nz, math.min(nz + 31, me.z))
        local room = QD.COX.room_at(px, pz, me.level)
        if room ~= nil and room.name == name then
            local dist = math.max(math.abs(me.x - px), math.abs(me.z - pz))
            if bd == nil or dist < bd then best, bd = room, dist end
        end
    end
    if best ~= nil then return best, false end
    return nil, false, string.format("no %s room here, ahead or beside me (at %d,%d level %d, in %s)",
        name, me.x, me.z, me.level, here and here.name or "no room")
end

-- The room lookup a solver's strategy makes each tick until found; a room
-- not found for QD.COX.NO_ROOM_TICKS is a failure: st.no_room = why, which
-- the room's done() returns as "no_room" (the relay reports it; never a
-- wait to the run's end). -> room, inside, or nil
QD.COX.NO_ROOM_TICKS = 8
function QD.COX.lookup_room(F, name, st)
    local room, inside, why = QD.COX.find_room(name)
    if room ~= nil then
        st.no_room_t0 = nil
        return room, inside
    end
    st.no_room_t0 = st.no_room_t0 or F.tick
    if F.tick - st.no_room_t0 >= QD.COX.NO_ROOM_TICKS then st.no_room = why end
    return nil
end

-- A room's heals and restores from the supply families (QD.COX.SUPPLY):
-- what the whole raid's kit carries and makes.
function QD.COX.raid_restores(ids)
    -- (the supply families, the raid's own first, then what was brought in:
    -- heal = Xeric's aid then Saradomin brew; the restores, revitalisation
    -- and super restore, then the prayer doses; a drained stat takes a
    -- restore only)
    ids.brews = QD.COX.supply_ids("heal")
    ids.restores = QD.COX.supply_ids("restore")
    for _, id in ipairs(QD.COX.supply_ids("prayer")) do ids.restores[#ids.restores + 1] = id end
    ids.stat_restores = QD.COX.supply_ids("restore")
end
