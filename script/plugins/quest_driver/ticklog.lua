-- quest-driver / ticklog: the embedded server's per-tick event log (npc
-- animations, projectiles, hits, spawns, deaths, loc changes, tiles), read by a
-- test to build its tick ledger. Raid seam 1 (docs/RAID_ORCHESTRATOR.md
-- sections 4 and 6). The namespace QD.ticklog is declared in core.lua.
--
-- The log is the SERVER's (src/torirsserver/torirs_server_ticklog.c): every
-- row is stamped with srv->tick on the tick the server did the thing, which
-- is the clock Blert's NPC_ATTACK/NPC_SPAWN/NPC_DEATH rows are on
-- (tools/verify_tob_timings.py), so a cadence measured here compares with a
-- recorder's distribution directly. It is OFF until t.ticklog.start(): an
-- ordinary quest run never pays for it. The rows also land in
-- build/quest_gate/<run>/ticklog.tsv as they are recorded.
--
--   t.ticklog.start()            -> ok, "ticklog on at tick T (serial S) -> path"
--   t.ticklog.mark(label)        -> ok, "mark '<label>' at tick T (serial S)"
--   t.ticklog.rows(opts)         -> ok, { row, ... }   (refused when off)
--   t.ticklog.gaps(slot, kind, opts) -> ok, "10, 10, 10 (...)", {10, 10, 10}, {ticks}
--   t.ticklog.slot(npc)          -> ok, world slot  (npc = a t.npc row or its slot)
--
-- SLOTS ARE THE SERVER'S. A t.npc.nearest row's `slot` is the CLIENT's name
-- for the npc (NPC_INFO's per-client slot map), not the world slot the log is
-- keyed by -- a Lumbridge goblin read as client slot 58 was world slot 632.
-- Pass the row itself (`opts.npc = row`, or `gaps(row, ...)`) and the driver
-- translates it, or call t.ticklog.slot(row) once BEFORE the fight: once the
-- npc has despawned from the client the translation answers not_found.
--
-- A row is { serial, tick, kind, <named fields> } -- the names per kind are
-- QD.ticklog.FIELDS below, and every packed coordinate field `coord`/`src`/
-- `dst` is also given unpacked as `x, z, level` / `src_x, src_z, src_level` /
-- `dst_x, dst_z, dst_level`. opts filters: since (a serial: rows after it),
-- kind (a name or a list of names), slot (an npc slot; for hit_player the
-- dealing npc's slot), npc (a t.npc row or client slot, translated to `slot`),
-- pid, type (an npc type id), seq (npc_anim), spotanim, where (a
-- function(row) -> boolean).

QD.ticklog.FIELDS = {
    start = { "start_tick" },
    mark = {},
    npc_anim = { "slot", "type", "seq", "delay" },
    npc_spotanim = { "slot", "type", "spotanim", "height", "delay" },
    projectile = { "src", "dst", "target", "spotanim", "start_cycle", "end_cycle" },
    map_spotanim = { "coord", "spotanim", "height", "delay" },
    hit_player = { "pid", "npc_slot", "damage", "hitsplat", "dealer_pid", "npc_type" },
    hit_npc = { "slot", "type", "damage", "hitsplat" },
    npc_spawn = { "slot", "type", "coord" },
    npc_death = { "slot", "type", "coord" },
    npc_free = { "slot", "type", "coord" },
    npc_retype = { "slot", "from_type", "to_type", "duration" },
    loc_set = { "coord", "loc", "shape", "angle", "loc_kind" },
    obj_add = { "coord", "obj", "count", "receiver_pid" },
    player_tile = { "pid", "x", "z", "level" },
    npc_tile = { "slot", "x", "z", "level", "type" },
    -- An `npc_facesquare` (SS_OP_NPC_FACESQUARE, the only writer of an npc's
    -- face-coord): the TILE it turned the npc to, not a packed coord.
    npc_face = { "slot", "type", "x", "z" },
}

QD.ticklog._RAW = { "a", "b", "c", "d", "e", "f" }

-- ToriRSServer_CoordPack: level << 28 | x << 14 | z.
function QD.ticklog._unpack(coord)
    return (coord >> 14) & 0x3fff, coord & 0x3fff, (coord >> 28) & 0x3
end

function QD.ticklog._name(raw)
    local row = { serial = raw.serial, tick = raw.tick, kind = raw.kind, label = raw.label }
    local names = QD.ticklog.FIELDS[raw.kind] or {}
    for i, name in ipairs(names) do
        row[name] = raw[QD.ticklog._RAW[i]]
    end
    if row.coord ~= nil then
        row.x, row.z, row.level = QD.ticklog._unpack(row.coord)
    end
    if row.src ~= nil then
        row.src_x, row.src_z, row.src_level = QD.ticklog._unpack(row.src)
    end
    if row.dst ~= nil then
        row.dst_x, row.dst_z, row.dst_level = QD.ticklog._unpack(row.dst)
    end
    return row
end

function QD.ticklog._unsupported(verb)
    return "unsupported", "t.ticklog." .. verb
        .. ": this binary has no api_drive.ticklog (rebuild with the raid seam)"
end

function QD.ticklog.start()
    if api_drive.ticklog_start == nil then
        return QD.ticklog._unsupported("start")
    end
    local result, state = api_drive.ticklog_start()
    if result ~= "ok" then
        return result, "t.ticklog.start: " .. tostring(result)
            .. " (no embedded server in this run)"
    end
    QD.ticklog._started = state.start_tick
    return "ok", string.format("ticklog on at tick %d (now %d, serial %d) -> %s",
        state.start_tick, state.tick, state.serial, tostring(state.path or "memory only"))
end

function QD.ticklog.mark(label)
    if api_drive.ticklog_mark == nil then
        return QD.ticklog._unsupported("mark")
    end
    local result, mark = api_drive.ticklog_mark(tostring(label))
    if result ~= "ok" then
        return result, "t.ticklog.mark: the log is off; call t.ticklog.start() first"
    end
    return "ok", string.format("mark '%s' at tick %d (serial %d)", tostring(label),
        mark.tick, mark.serial)
end

function QD.ticklog._kind_set(kind)
    if kind == nil then
        return nil
    end
    local set = {}
    if type(kind) == "table" then
        for _, name in ipairs(kind) do
            set[name] = true
        end
    else
        set[kind] = true
    end
    for name in pairs(set) do
        if QD.ticklog.FIELDS[name] == nil then
            return nil, name
        end
    end
    return set
end

function QD.ticklog._keep(row, opts, kinds)
    if kinds ~= nil and not kinds[row.kind] then
        return false
    end
    if opts.slot ~= nil then
        local slot = row.slot
        if row.kind == "hit_player" then
            slot = row.npc_slot
        end
        if slot ~= opts.slot then
            return false
        end
    end
    if opts.pid ~= nil and row.pid ~= opts.pid then
        return false
    end
    if opts.type ~= nil and row.type ~= opts.type and row.npc_type ~= opts.type then
        return false
    end
    if opts.seq ~= nil and row.seq ~= opts.seq then
        return false
    end
    if opts.spotanim ~= nil and row.spotanim ~= opts.spotanim then
        return false
    end
    if opts.where ~= nil and not opts.where(row) then
        return false
    end
    return true
end

-- t.ticklog.slot(npc) -> "ok", world slot | not_found | refused | unsupported
function QD.ticklog.slot(npc)
    if api_drive.server_npc_slot == nil then
        return QD.ticklog._unsupported("slot")
    end
    local client = npc
    if type(npc) == "table" then
        client = npc.slot
    end
    if math.type(client) ~= "integer" then
        return "refused", "t.ticklog.slot: wants a t.npc row or its client slot, got "
            .. tostring(npc)
    end
    local result, world = api_drive.server_npc_slot(client)
    if result ~= "ok" then
        return result, "t.ticklog.slot: client slot " .. client
            .. " has no world slot (the client no longer names that npc)"
    end
    return "ok", world
end

-- t.ticklog.rows(opts) -> "ok", { row, ... } | "refused", why
function QD.ticklog.rows(opts)
    if api_drive.ticklog == nil then
        return QD.ticklog._unsupported("rows")
    end
    opts = opts or {}
    if opts.npc ~= nil then
        local slot_result, slot = QD.ticklog.slot(opts.npc)
        if slot_result ~= "ok" then
            return slot_result, slot
        end
        local copy = {}
        for key, value in pairs(opts) do
            copy[key] = value
        end
        copy.npc = nil
        copy.slot = slot
        opts = copy
    end
    local kinds, bad = QD.ticklog._kind_set(opts.kind)
    if bad ~= nil then
        return "refused", "t.ticklog.rows: no row kind '" .. tostring(bad) .. "'"
    end
    -- One kind and the slot are filtered in C (api_drive.ticklog's own
    -- arguments): a town logs thousands of npc_tile rows, and building a Lua
    -- table for each only to drop it here exhausts the per-resume budget.
    local c_kind = nil
    if type(opts.kind) == "string" then
        c_kind = opts.kind
    elseif type(opts.kind) == "table" and #opts.kind == 1 then
        c_kind = opts.kind[1]
    end
    local out = {}
    local after = opts.since or 0
    while true do
        local result, page = api_drive.ticklog(after, 8192, c_kind, opts.slot)
        if result ~= "ok" then
            return result, "t.ticklog.rows: the log is off; call t.ticklog.start() first"
        end
        for _, raw in ipairs(page) do
            local row = QD.ticklog._name(raw)
            if QD.ticklog._keep(row, opts, kinds) then
                out[#out + 1] = row
            end
        end
        if #page < 8192 or page.next_serial == after then
            break
        end
        after = page.next_serial
    end
    return "ok", out
end

-- t.ticklog.gaps(slot, kind, opts) -> "ok", text, gaps, ticks
-- The cadence every spec row needs: the tick gaps between consecutive rows of
-- `kind` for npc `slot` (nil = every slot), after opts' filters (seq= picks
-- the attack animation out of an npc's block and death animations). `text`
-- reads "10, 10, 10 (3 gap(s) over 4 row(s), ticks 9..39)".
function QD.ticklog.gaps(slot, kind, opts)
    local filter = {}
    for key, value in pairs(opts or {}) do
        filter[key] = value
    end
    if type(slot) == "table" then
        filter.npc = slot
    else
        filter.slot = slot
    end
    filter.kind = kind
    local result, rows = QD.ticklog.rows(filter)
    if result ~= "ok" then
        return result, rows, {}, {}
    end
    local gaps, ticks, parts = {}, {}, {}
    for i, row in ipairs(rows) do
        ticks[i] = row.tick
        if i > 1 then
            gaps[#gaps + 1] = row.tick - rows[i - 1].tick
            parts[#parts + 1] = tostring(gaps[#gaps])
        end
    end
    local text = string.format("%s (%d gap(s) over %d row(s)%s)",
        #parts > 0 and table.concat(parts, ", ") or "no gaps", #gaps, #rows,
        #rows > 0 and string.format(", ticks %d..%d", ticks[1], ticks[#ticks]) or "")
    return "ok", text, gaps, ticks
end
