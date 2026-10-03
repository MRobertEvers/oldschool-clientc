-- quest-driver / raid: enter a raid room as a player arrives in it (the room
-- built by the raid's own debugproc), and read the raid's state back.
-- Raid seam 1 (docs/RAID_ORCHESTRATOR.md section 4, instance resume per room;
-- build/seam_state/<pass>/triage.json key raid_room_entry_verbs).
--
--   t.raid.enter(raid, room, opts) -> ok refused timeout no_row
--   t.raid.state()                 -> ("ok", {raid, room, room_id, mode, started,
--                                     handle, boss_symbol, boss_slot, fight, line})
--                                     or not_found when no raid is active
--   t.raid.leave()                 -> ok refused timeout
--   t.raid.start_tile()            -> ("ok", {x, z, level}) refused unsupported
--
-- WHAT "ENTER" IS, AND WHAT IT IS NOT.  A room test is one relay leg per room
-- (RAID_ORCHESTRATOR.md section 6), so it has to begin standing where a player
-- who walked the raid to that room would stand: in the raid's real instance, on
-- the CORRIDOR side of the room's barrier, the room built and its fight
-- UNSTARTED.  enter() issues the raid's own landing debugproc (`::tobmode`,
-- `::toa`, `::coxseed` + `::coxgoto`) and nothing else: it never starts a room
-- (`::tobgo` / `::toago` are not used here and a test must not use them --
-- section 6 rule 1).  The fight is begun by the test, by the player's own click
-- on the barrier: ToB `tob_arena_barrier` op 1 then "Yes, begin the fight."
-- (tob_party.rs2 [oploc1,tob_arena_barrier]); ToA `toa_path_barrier` op 1 Pass
-- or `toa_wardens_barrier` op 1 (toa_barrier.rs2); CoX encounters wake on
-- approach, and Olm's chamber is entered by `raids_bossentrance` op 1 then
-- "Step through the mystical barrier." (cox.rs2).
--
-- THE STATE IS READ FROM THE SERVER'S OWN REGISTERS.  A raid's room, mode and
-- started flag are map-instance registers (`map_instance_var_get`), which no
-- client or varp reader can see, so each raid has one read-only debugproc that
-- prints them as a single `key=value` line (`::tobstate`, `::toastate`,
-- `::coxstate`) and state() parses it.  Which raid is active is read first from
-- the three session varps through t.var.server (they are transmit=no, so that
-- is the server content copy): varp5893_tob_active, varp6910_toa_active,
-- varp5912_cox_active.
--
-- THE SETTLE.  After the landing cheat the player has been teleported into a
-- freshly built instance, and the client is a tick or more behind the server
-- (pointer.lua goto_tile: the npc pool lands a tick after the tile, and the
-- loaded scene may still be the previous region's).  enter() waits for the
-- tile to leave the departure tile and hold for two ticks (Verzik's chamber is
-- entered by a queued walk a tick after the teleport -- tob_raid.rs2
-- ~tob_room_arrive), then the same two scene-settle awaits goto_tile uses (npc
-- pool non-empty, loc pool loaded around the player and the scene settled),
-- and only then reads the state and the boss.

-- Room tables.  `id` is the content's own room number (tob.constant
-- ^tob_room_*, toa.constant ^toa_room_*, cox.constant ^cox_room_*); `boss` is
-- the npc the room's build places (ToB: ~tob_spawn_boss at build time, so it is
-- PRESENT on landing) or the npc ~toa_spawn_boss places when the room STARTS
-- (ToA: so it is UNSPAWNED on landing); nil where the room has no single boss.
-- A list names every form the boss changes type through (npc_changetype:
-- Maiden's 70/50/30, Verzik's phases, Tekton's waiting/fighting), the first
-- form first; the pool is searched for any of them.
QD.raid._ROOMS = {
    tob = {
        maiden = { id = 1, boss = { "tob_maiden_100", "tob_maiden_70", "tob_maiden_50", "tob_maiden_30" } },
        bloat = { id = 2, boss = { "tob_bloat", "tob_bloat_hard", "tob_bloat_story" } },
        -- No boss until the waves are done: ~tob_spawn_boss returns early.
        nylocas = { id = 3 },
        sotetseg = { id = 4, boss = { "tob_sotetseg_combat", "tob_sotetseg_combat_hard", "tob_sotetseg_combat_story",
            "tob_sotetseg_noncombat", "tob_sotetseg_noncombat_hard", "tob_sotetseg_noncombat_story" } },
        xarpus = { id = 5, boss = { "tob_xarpus_static", "tob_xarpus_static_hard", "tob_xarpus_static_story",
            "tob_xarpus_feeding", "tob_xarpus_feeding_hard", "tob_xarpus_feeding_story",
            "tob_xarpus_combat", "tob_xarpus_combat_hard", "tob_xarpus_combat_story" } },
        verzik = { id = 6, boss = { "verzik_initial", "verzik_phase1", "verzik_phase1_to2_transition",
            "verzik_phase2", "verzik_phase3" } },
    },
    toa = {
        nexus = { id = 1 },
        crondis = { id = 2 },
        zebak = { id = 3, boss = "toa_zebak" },
        scabaras = { id = 4 },
        kephri = { id = 5, boss = "toa_kephri_boss_shielded" },
        het = { id = 6, boss = "toa_het_goal" },
        akkha = { id = 7, boss = "akkha_melee" },
        apmeken = { id = 8 },
        baba = { id = 9, boss = "toa_baba" },
        wardens = { id = 10, boss = "toa_wardens_p1_obelisk_npc" },
        wardens_p2 = { id = 11 },
        vault = { id = 12 },
    },
    cox = {
        -- Tekton wakes on approach (cox_tekton.rs2: `waiting` until a player is
        -- in range), and a landing at the room centre is in range.
        tekton = { id = 0, boss = { "raids_tekton_waiting", "raids_tekton_fighting_standard",
            "raids_tekton_fighting_enraged", "raids_tekton_walking_standard",
            "raids_tekton_walking_enraged", "raids_tekton_hammering" } },
        guardians = { id = 1 },
        vespula = { id = 2 },
        icedemon = { id = 3 },
        tightrope = { id = 4 },
        crabs = { id = 5 },
        thieving = { id = 6 },
        resource = { id = 7 },
        shamans = { id = 8 },
        mystics = { id = 9 },
        vasa = { id = 10 },
        vanguards = { id = 11 },
        muttadiles = { id = 12 },
        scavenger_small = { id = 13 },
        scavenger_large = { id = 14 },
        -- Not a grid room: the landing is floor 2's resource room, whose
        -- centre holds `raids_bossentrance` (cox_resource.rs2), and Olm does
        -- not exist until that barrier is confirmed (~cox_olm_enter).
        olm = { id = 7, floor = 1, boss = { "olm_head", "olm_head_spawning" } },
    },
}

-- ToB's three modes (tob.constant ^tob_mode_*), both ways.
QD.raid._TOB_MODES = { entry = 0, normal = 1, hard = 2 }
QD.raid._TOB_MODE_NAMES = { [0] = "entry", [1] = "normal", [2] = "hard" }

-- Per raid: the session varp that says it is active, its read-only state
-- debugproc, and its leave cheat.
QD.raid._RAIDS = {
    tob = { active = "varp5893_tob_active", state = "::tobstate", leave = "::tobout" },
    toa = { active = "varp6910_toa_active", state = "::toastate", leave = "::toaout" },
    cox = { active = "varp5912_cox_active", state = "::coxstate", leave = "::cox_leave" },
}
QD.raid._ORDER = { "tob", "toa", "cox" }

-- The default CoX layout seed: a room test must land in the same raid every
-- run (cox.rs2 `::coxseed`); a test that wants another layout passes opts.seed.
QD.raid._COX_DEFAULT_SEED = 1

-- Budgets, in server ticks.
QD.raid._ARRIVE_TICKS = 15
QD.raid._STATE_TICKS = 5
QD.raid._BOSS_TICKS = 5

-- (result, room_name) for a content room id of `raid`, or nil.
function QD.raid._room_name(raid, id)
    local rooms = QD.raid._ROOMS[raid]
    if not rooms then
        return nil
    end
    local best = nil
    for name, row in pairs(rooms) do
        -- olm shares id 7 with resource; the grid name wins for a state read.
        if row.id == id and name ~= "olm" then
            if best == nil or name < best then
                best = name
            end
        end
    end
    return best
end

-- The raid whose session varp reads 1 on the server, or nil, plus a reading
-- of all three for a detail.
function QD.raid._active()
    local found = nil
    local parts = {}
    for i = 1, #QD.raid._ORDER do
        local raid = QD.raid._ORDER[i]
        local result, value = QD.var.server(QD.raid._RAIDS[raid].active)
        parts[#parts + 1] = raid .. "=" .. (result == "ok" and tostring(value)
            or ("unread(" .. tostring(result) .. ")"))
        if result == "ok" and value == 1 and found == nil then
            found = raid
        end
    end
    return found, table.concat(parts, " ")
end

-- Dispatch a raid's read-only state debugproc and return (ok, fields, line):
-- fields is the line's key=value pairs, numbers as numbers and "x,z,l"
-- triples as {x, z, level}.  The serial is taken BEFORE the cheat, so the
-- line matched is the one this call asked for, never an older copy still in
-- the ring.
function QD.raid._read_state(raid)
    local spec = QD.raid._RAIDS[raid]
    local prefix = raid .. "state "
    local serial_result, since = api_drive.message_serial()
    if serial_result ~= "ok" then
        return serial_result, "raid.state: message_serial answered " .. tostring(serial_result)
    end
    local cheat_result, cheat_detail = QD.cheat(spec.state, false)
    if cheat_result ~= "ok" then
        return cheat_result, string.format("raid.state: %s answered %s%s", spec.state,
            tostring(cheat_result), cheat_detail and (" -- " .. tostring(cheat_detail)) or "")
    end
    local line = nil
    local awaited = await({
        level = function()
            local result, list = api_drive.messages()
            if result ~= "ok" then
                return false
            end
            for i = 1, #list do
                if list[i].serial > since and string.find(list[i].text, prefix, 1, true) == 1 then
                    line = list[i].text
                    return true
                end
            end
            return false
        end,
        note = "raid.state " .. spec.state .. " reply",
    }, QD.raid._STATE_TICKS)
    if awaited ~= "ok" or line == nil then
        return "timeout", string.format("raid.state: no '%s...' line within %d tick(s) of %s",
            prefix, QD.raid._STATE_TICKS, spec.state)
    end
    local fields = {}
    for key, value in string.gmatch(line, "(%w+)=([%-%d,]+)") do
        local a, b, c = string.match(value, "^(%-?%d+),(%-?%d+),(%-?%d+)$")
        if a then
            fields[key] = { x = tonumber(a), z = tonumber(b), level = tonumber(c) }
        else
            local rx, rz = string.match(value, "^(%-?%d+),(%-?%d+)$")
            if rx then
                fields[key] = { x = tonumber(rx), z = tonumber(rz) }
            else
                fields[key] = tonumber(value)
            end
        end
    end
    return "ok", fields, line
end

-- The room's boss in the client's npc pool: (status, row, symbol) where
-- status is "present" (symbol = the form found) or "unspawned" (symbol = the
-- first form), or "none" for a room with no boss.  `ticks` > 0 waits for it (a ToB boss is placed
-- by the build and lands in the pool a tick or two after the scene); 0 reads
-- once (a ToA boss that is not there yet is the expected answer, not a wait).
function QD.raid._boss(symbols, ticks)
    if symbols == nil then
        return "none", nil, nil
    end
    if type(symbols) ~= "table" then
        symbols = { symbols }
    end
    local function find()
        for i = 1, #symbols do
            local result, row = QD.npc.nearest(symbols[i], 0)
            if result == "ok" and type(row) == "table" then
                return symbols[i], row
            end
        end
        return nil, nil
    end
    if ticks and ticks > 0 then
        await({
            level = function()
                return (find()) ~= nil
            end,
            note = "raid boss " .. symbols[1],
        }, ticks)
    end
    local found, row = find()
    if found ~= nil then
        return "present", row, found
    end
    return "unspawned", nil, symbols[1]
end

-- "boss present (tob_maiden_100 slot 12 at 3172,4422)" / "boss unspawned
-- (toa_zebak)" / "boss none (room has no single boss)".
function QD.raid._boss_text(symbol, status, row)
    if status == "none" then
        return "boss none (room has no single boss)"
    end
    if status == "present" then
        return string.format("boss present (%s slot %s at %s,%s)", symbol,
            tostring(row.slot), tostring(row.x), tostring(row.z))
    end
    return string.format("boss unspawned (%s; pool nearest: %s)", symbol, QD.raid._pool_text(4))
end

-- The `n` nearest npcs in the client's pool as "symbol@x,z" -- what IS there
-- when the boss is not, so an "unspawned" reading names its neighbourhood (a
-- boss out of the client's view range reads the same as one never built).
function QD.raid._pool_text(n)
    local result, rows = api_drive.npcs(0)
    if result ~= "ok" or type(rows) ~= "table" then
        return "unread(" .. tostring(result) .. ")"
    end
    if #rows == 0 then
        return "empty"
    end
    local parts = {}
    for i = 1, math.min(n, #rows) do
        local name_result, name = api_drive.symbol_name("npc", rows[i].npc_id)
        parts[#parts + 1] = string.format("%s@%d,%d",
            name_result == "ok" and name or ("npc " .. tostring(rows[i].npc_id)), rows[i].x, rows[i].z)
    end
    return table.concat(parts, " ") .. string.format(" (%d in pool)", #rows)
end

-- The player's tile as "x,z,level", or "?".
function QD.raid._tile_text()
    local result, tile = QD.world.tile()
    if result ~= "ok" or type(tile) ~= "table" then
        return "?"
    end
    return string.format("%d,%d,%d", tile.x, tile.z, tile.level)
end

-- Wait for the teleport the landing cheat made: the tile leaves `departure`
-- and holds still for two server ticks, then goto_tile's two scene settles.
-- (ok, "x,z,level") or (timeout, detail).  `departure` may be nil when the
-- landing can be the tile already stood on (a second enter of the same room);
-- then only the hold is waited for.
function QD.raid._settle(departure, ticks)
    local last_key = nil
    local held_since = api_drive.tick()
    local arrived = await({
        level = function()
            local result, tile = api_drive.player_tile()
            if result ~= "ok" or type(tile) ~= "table" then
                return false
            end
            local key = string.format("%d,%d,%d", tile.x, tile.z, tile.level)
            if key ~= last_key then
                last_key = key
                held_since = api_drive.tick()
                return false
            end
            if departure ~= nil and key == departure then
                return false
            end
            return api_drive.tick() - held_since >= 2
        end,
        note = "raid arrival",
    }, ticks or QD.raid._ARRIVE_TICKS)
    if arrived ~= "ok" then
        return "timeout", string.format("never arrived: still at %s (left %s) after %d tick(s)",
            tostring(last_key), tostring(departure), ticks or QD.raid._ARRIVE_TICKS)
    end
    -- The same settle goto_tile does after a ::goto (pointer.lua, "THE SCENE
    -- IS ONE TICK BEHIND THE TILE" and SEAM-PRESS-PIXEL), verdicts advisory.
    await({
        level = function()
            local pool_result, rows = api_drive.npcs(0)
            return pool_result == "ok" and #rows > 0
        end,
        note = "raid scene settle",
    }, 3)
    await({
        level = function()
            local loc_result, rows = api_drive.locs(QD.player._goto_scene_radius)
            return loc_result == "ok" and #rows > 0 and api_drive.settled()
        end,
        note = "raid scene rebuild",
    }, QD.player._goto_scene_ticks)
    return "ok", QD.raid._tile_text()
end

-- Issue one landing cheat; (ok) or (result, detail naming it).
function QD.raid._cheat(text)
    local result, detail = QD.cheat(text)
    if result ~= "ok" then
        return result, string.format("%s answered %s%s", text, tostring(result),
            detail and (" -- " .. tostring(detail)) or "")
    end
    return "ok"
end

-- t.raid.enter(raid, room, opts)
--
--   raid  "tob" | "toa" | "cox"
--   room  ToB: maiden bloat nylocas sotetseg xarpus verzik
--         ToA: nexus crondis zebak scabaras kephri het akkha apmeken baba
--              wardens wardens_p2 vault
--         CoX: tekton guardians vespula icedemon tightrope crabs thieving
--              resource shamans mystics vasa vanguards muttadiles
--              scavenger_small scavenger_large olm
--   opts  ToB: mode = "entry" | "normal" (default) | "hard"
--         CoX: seed = 0..1023 (default QD.raid._COX_DEFAULT_SEED),
--              floor = 0 | 1 (default: floor 1 first, then floor 2)
--
-- Leaves any OTHER raid first (its own leave cheat).  ok detail:
--   "in tob maiden (normal) at x,z,level; boss present (tob_maiden_100 slot S at x,z);
--    handle H, started 0, fight x,z,level"
-- refused: an unknown raid/room/mode, the landing cheat refused, or the state
-- read back disagreeing with what was asked (wrong room/mode, already started).
function QD.raid.enter(raid, room, opts)
    opts = opts or {}
    local rooms = QD.raid._ROOMS[raid]
    if rooms == nil then
        return "refused", "raid.enter: unknown raid '" .. tostring(raid) .. "' (tob, toa, cox)"
    end
    local row = rooms[room]
    if row == nil then
        local names = {}
        for name in pairs(rooms) do
            names[#names + 1] = name
        end
        table.sort(names)
        return "refused", string.format("raid.enter: unknown %s room '%s' (%s)", raid,
            tostring(room), table.concat(names, " "))
    end
    local mode_name = nil
    local mode = nil
    if raid == "tob" then
        mode_name = opts.mode or "normal"
        mode = QD.raid._TOB_MODES[mode_name]
        if mode == nil then
            return "refused", "raid.enter: unknown ToB mode '" .. tostring(mode_name)
                .. "' (entry, normal, hard)"
        end
    elseif opts.mode ~= nil then
        return "refused", "raid.enter: opts.mode is ToB's; " .. raid .. " takes none"
    end

    -- Another raid open: leave it first, through its own cheat.
    local active = QD.raid._active()
    if active ~= nil and active ~= raid then
        local left_result, left_detail = QD.raid.leave()
        if left_result ~= "ok" then
            return left_result, "raid.enter: leaving " .. active .. " first failed -- "
                .. tostring(left_detail)
        end
        active = nil
    end

    local departure = QD.raid._tile_text()
    local issued = {}
    if raid == "tob" then
        local text = string.format("::tobmode %d %d", row.id, mode)
        local result, detail = QD.raid._cheat(text)
        if result ~= "ok" then
            return result, "raid.enter: " .. detail
        end
        issued[#issued + 1] = text
    elseif raid == "toa" then
        local text = string.format("::toa %d", row.id)
        local result, detail = QD.raid._cheat(text)
        if result ~= "ok" then
            return result, "raid.enter: " .. detail
        end
        issued[#issued + 1] = text
    else
        local seed = opts.seed or QD.raid._COX_DEFAULT_SEED
        -- A raid already open on another seed is a different layout: leave it.
        if active == "cox" then
            local state_result, fields = QD.raid._read_state("cox")
            if state_result ~= "ok" or fields.seed ~= seed then
                local left_result, left_detail = QD.raid.leave()
                if left_result ~= "ok" then
                    return left_result, "raid.enter: leaving the open CoX raid first failed -- "
                        .. tostring(left_detail)
                end
                active = nil
            end
        end
        if active ~= "cox" then
            local text = string.format("::coxseed %d", seed)
            local result, detail = QD.raid._cheat(text)
            if result ~= "ok" then
                return result, "raid.enter: " .. detail
            end
            issued[#issued + 1] = text
            local settle_result, settle_detail = QD.raid._settle(departure)
            if settle_result ~= "ok" then
                return settle_result, "raid.enter: after " .. text .. ": " .. settle_detail
            end
            departure = QD.raid._tile_text()
        end
        local floors = opts.floor ~= nil and { opts.floor } or (row.floor ~= nil and { row.floor } or { 0, 1 })
        local placed = false
        local misses = {}
        for i = 1, #floors do
            local text = string.format("::coxgoto %d %d", row.id, floors[i])
            local serial_result, since = api_drive.message_serial()
            local result, detail = QD.raid._cheat(text)
            if result ~= "ok" then
                return result, "raid.enter: " .. detail
            end
            issued[#issued + 1] = text
            -- The debugproc says which: "coxgoto room R floor F" or "That
            -- room is not on that floor in this raid."
            local said = nil
            if serial_result == "ok" then
                local list_result, list = api_drive.messages()
                if list_result == "ok" then
                    for j = 1, #list do
                        if list[j].serial > since then
                            said = list[j].text
                        end
                    end
                end
            end
            if said ~= nil and string.find(said, "coxgoto room", 1, true) == 1 then
                placed = true
                break
            end
            misses[#misses + 1] = text .. " -> " .. tostring(said)
        end
        if not placed then
            return "refused", string.format("raid.enter: cox seed %d has no %s room -- %s",
                seed, room, table.concat(misses, "; "))
        end
    end

    local settle_result, settle_detail = QD.raid._settle(departure)
    if settle_result ~= "ok" then
        return settle_result, "raid.enter: after " .. table.concat(issued, ", ") .. ": " .. settle_detail
    end

    local state_result, fields, line = QD.raid._read_state(raid)
    if state_result ~= "ok" then
        return state_result, "raid.enter: " .. tostring(fields)
    end
    if fields.active ~= 1 then
        return "refused", "raid.enter: " .. table.concat(issued, ", ") .. " left the raid inactive -- " .. line
    end
    local where = settle_detail
    local tail = ""
    if raid == "tob" or raid == "toa" then
        if fields.room ~= row.id then
            return "refused", string.format("raid.enter: asked for %s room %d, the raid is in room %s -- %s",
                raid, row.id, tostring(fields.room), line)
        end
        if fields.started ~= 0 then
            return "refused", "raid.enter: the room is already started on landing -- " .. line
        end
        if raid == "tob" and fields.mode ~= mode then
            return "refused", string.format("raid.enter: asked for mode %d, the instance carries %s -- %s",
                mode, tostring(fields.mode), line)
        end
        local fight = fields.fight
        tail = string.format("; handle %s, started 0, fight %s", tostring(fields.handle),
            type(fight) == "table" and string.format("%d,%d,%d", fight.x, fight.z, fight.level) or "?")
    else
        if room ~= "olm" and fields.room ~= row.id then
            return "refused", string.format("raid.enter: asked for cox room %d, standing in room %s -- %s",
                row.id, tostring(fields.room), line)
        end
        tail = string.format("; seed %s, floor %s, cell %s", tostring(fields.seed), tostring(fields.floor),
            type(fields.cell) == "table" and (fields.cell.x .. "," .. fields.cell.z) or "?")
    end

    -- ToB places its boss at build time; ToA and Olm only when the room
    -- starts, so they are read once and "unspawned" is the expected answer.
    local wait = (raid == "tob" or (raid == "cox" and room ~= "olm")) and QD.raid._BOSS_TICKS or 0
    local status, boss_row, boss_symbol = QD.raid._boss(row.boss, wait)
    local mode_text = raid == "tob" and mode_name
        or (raid == "toa" and ("level " .. tostring(fields.level)))
        or ("seed " .. tostring(fields.seed))
    return "ok", string.format("in %s %s (%s) at %s; %s%s", raid, room, mode_text, where,
        QD.raid._boss_text(boss_symbol, status, boss_row), tail)
end

-- t.raid.state() -> ("ok", {raid, room, room_id, mode, started, handle,
-- boss_symbol, boss_slot, fight, seed, line}) for the active raid, or
-- ("not_found", "<the three active varps>") when none is.  `mode` is ToB's
-- mode name, ToA's "level N", CoX's "seed N"; `started` is a boolean (CoX has
-- no started register and answers false); `boss_slot` is the client pool slot
-- of the room's boss, or nil when it is not in the pool.
function QD.raid.state()
    local raid, reading = QD.raid._active()
    if raid == nil then
        return "not_found", "raid.state: no raid active (" .. reading .. ")"
    end
    local result, fields, line = QD.raid._read_state(raid)
    if result ~= "ok" then
        return result, fields
    end
    local out = {
        raid = raid,
        room_id = fields.room,
        room = QD.raid._room_name(raid, fields.room),
        handle = fields.handle,
        started = fields.started == 1,
        fight = fields.fight,
        seed = fields.seed,
        line = line,
    }
    if raid == "tob" then
        out.mode = QD.raid._TOB_MODE_NAMES[fields.mode] or tostring(fields.mode)
        out.cleared = fields.cleared == 1
    elseif raid == "toa" then
        out.mode = "level " .. tostring(fields.level)
        out.entry = fields.entry
    else
        out.mode = "seed " .. tostring(fields.seed)
        out.floor = fields.floor
    end
    local room_row = out.room and QD.raid._ROOMS[raid][out.room] or nil
    if room_row and room_row.boss then
        local status, row, symbol = QD.raid._boss(room_row.boss, 0)
        out.boss_symbol = symbol
        if status == "present" then
            out.boss_slot = row.slot
        end
    end
    return "ok", out
end

-- t.raid.leave() -> ok "left <raid> (<leave cheat>): at x,z,level, <active
-- varp> = 0"; refused when no raid is active.  Waits for the server's active
-- varp to read 0 and for the walk-out teleport to settle.
function QD.raid.leave()
    local raid, reading = QD.raid._active()
    if raid == nil then
        return "refused", "raid.leave: no raid active (" .. reading .. ")"
    end
    local spec = QD.raid._RAIDS[raid]
    local departure = QD.raid._tile_text()
    local result, detail = QD.raid._cheat(spec.leave)
    if result ~= "ok" then
        return result, "raid.leave: " .. detail
    end
    local var_result, var_detail = QD.var.await_server(spec.active, 0, QD.raid._ARRIVE_TICKS)
    if var_result ~= "ok" then
        return var_result, "raid.leave: " .. spec.leave .. " ran but " .. tostring(var_detail)
    end
    local settle_result, settle_detail = QD.raid._settle(departure)
    if settle_result ~= "ok" then
        return settle_result, "raid.leave: " .. spec.leave .. ": " .. settle_detail
    end
    return "ok", string.format("left %s (%s): at %s, %s = 0", raid, spec.leave, settle_detail, spec.active)
end

-- t.raid.start_tile() -> ("ok", {x, z, level}, "x,z,level"): the room's first
-- tile inside its barrier, from the content (ToB ~tob_room_fight_tile, ToA
-- ~toa_room_fight_coord) -- where a player who crossed it stands, for a test
-- that walks up to the barrier itself.  refused when no ToB/ToA room is open;
-- unsupported for CoX, whose rooms have no barrier tile (its encounters wake
-- on approach, and Olm's way in is the raids_bossentrance loc).
function QD.raid.start_tile()
    local raid, reading = QD.raid._active()
    if raid == nil then
        return "refused", "raid.start_tile: no raid active (" .. reading .. ")"
    end
    if raid == "cox" then
        return "unsupported", "raid.start_tile: CoX rooms have no barrier tile -- encounters wake on"
            .. " approach; Olm's chamber is entered by clicking raids_bossentrance"
    end
    local result, fields, line = QD.raid._read_state(raid)
    if result ~= "ok" then
        return result, fields
    end
    local fight = fields.fight
    if type(fight) ~= "table" or (fight.x == 0 and fight.z == 0) then
        return "refused", "raid.start_tile: no room open (" .. tostring(line) .. ")"
    end
    return "ok", fight, string.format("%d,%d,%d", fight.x, fight.z, fight.level)
end
