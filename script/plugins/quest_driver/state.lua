-- quest-driver / state: var, inv, skill and message reads.
-- Owner: core-state (docs/ARCHITECT.md).
--
-- No revconfig role belongs in this file.  Every name here is a server
-- content symbol resolved through api.drive.symbol, and every value is either
-- an engine struct read or the client's record of the server's own value.
--
-- var.expect requires client == server == value: a client showing the right
-- number while the server disagrees is the desync this verb exists to catch,
-- so it is `refused` with a detail naming which side disagreed -- never `ok`.
--
-- api.drive.symbol(kind, name) kind strings used here, lowercase of the
-- DriveSymbolKind the id comes from: "varp", "varbit", "obj", "stat", "inv".
-- Every other group's symbol() call should use the same lowercasing so one
-- core-scheduler dispatch covers all six files.
--
-- Only quest_driver/core.lua may declare a chunk-scope `local` (it is the
-- only part with the budget for one): every helper here hangs off QD
-- instead, under a `QD._` name so it cannot collide with a real verb.
--
-- There is no `api` global in this chunk (the sandbox hands `api` only to
-- quest_driver.lua's on_start/on_frame_start; a quest coroutine is resumed
-- with `t` alone). Every call below reads `api_drive`, the chunk-scope local
-- core.lua declares and binds in QD.core_bind(api) -- visible here because
-- core.lua is concatenated first (torirs_plugin_drive.c:69-77) and this is
-- one Lua chunk, so a top-level local's scope runs to the end of it, the same
-- way core.lua's own QD.t.* functions already read it (core.lua's `cheat`,
-- `settle`, `finish`). Do not write `api.drive.*` here again -- fixed 2026-09-19,
-- R1: every one of the 23 call sites in this file indexed a nil global and
-- raised on first use.

-- Try the name as a varbit first, then as a varp: exactly one of the two
-- content-symbol tables will have it, and callers never say which.
-- Returns kind, id  -- or nil, nil, fail_result, fail_name when neither
-- table has it (fail_result/fail_name come from the varp lookup, the last
-- one tried, so the detail names the symbol rather than guessing).
function QD._var_resolve(name)
    local result, id = api_drive.symbol("varbit", name)
    if result == "ok" then
        return "varbit", id
    end
    local vp_result, vp_id = api_drive.symbol("varp", name)
    if vp_result == "ok" then
        return "varp", vp_id
    end
    return nil, nil, vp_result, name
end

-- The backpack container id, resolved fresh each call.
function QD._inv_container()
    return api_drive.symbol("inv", "inv")
end

function QD._msg_contains(list, substring)
    for i = 1, #list do
        if string.find(list[i].text, substring, 1, true) then
            return true
        end
    end
    return false
end

-- var -------------------------------------------------------------------

function QD.var.varp(name)
    local result, id = api_drive.symbol("varp", name)
    if result ~= "ok" then
        return result, name
    end
    return api_drive.varp(id)
end

function QD.var.varbit(name)
    local result, id = api_drive.symbol("varbit", name)
    if result ~= "ok" then
        return result, name
    end
    return api_drive.varbit(id)
end

function QD.var.server(name)
    local kind, id, fail_result, fail_name = QD._var_resolve(name)
    if not kind then
        return fail_result, fail_name
    end
    if kind == "varbit" then
        return api_drive.varbit_server(id)
    end
    return api_drive.var_server(id)
end

-- var.await translates a varbit name to its base varp for event matching
-- (var events carry a varp id, plan 5.7); level is re-checked on every
-- server tick regardless, so a plain level predicate is sufficient here.
function QD.var.await(name, value, ticks)
    local kind, id, fail_result, fail_name = QD._var_resolve(name)
    if not kind then
        return fail_result, fail_name
    end
    local read = (kind == "varbit") and api_drive.varbit or api_drive.varp
    return await({
        level = function()
            local result, current = read(id)
            return result == "ok" and current == value
        end,
        note = "var.await " .. name .. " == " .. tostring(value),
    }, ticks or 10)
end

-- Requires client == server == value.  A client that already shows the right
-- number while the server disagrees is exactly the desync this exists to
-- catch, so that is `refused`, with a detail naming which side disagreed.
function QD.var.expect(name, value)
    local kind, id, fail_result, fail_name = QD._var_resolve(name)
    if not kind then
        return fail_result, fail_name
    end

    local client_read = (kind == "varbit") and api_drive.varbit or api_drive.varp
    local server_read = (kind == "varbit") and api_drive.varbit_server or api_drive.var_server

    local client_result, client_value = client_read(id)
    if client_result ~= "ok" then
        return client_result, name
    end
    if client_value ~= value then
        return "refused", name .. ": client=" .. tostring(client_value) .. " expected=" .. tostring(value)
    end

    local server_result, server_value = server_read(id)
    if server_result ~= "ok" then
        return server_result, name
    end
    if server_value ~= value then
        return "refused", name .. ": server=" .. tostring(server_value) .. " expected=" .. tostring(value)
    end

    return "ok", nil
end

-- inv ---------------------------------------------------------------------

function QD.inv.count(name)
    local obj_result, obj_id = api_drive.symbol("obj", name)
    if obj_result ~= "ok" then
        return obj_result, name
    end
    local inv_result, container_id = QD._inv_container()
    if inv_result ~= "ok" then
        return inv_result, "inv"
    end
    return api_drive.inv_count(container_id, obj_id)
end

function QD.inv.has(name)
    local result, total = QD.inv.count(name)
    if result ~= "ok" then
        return result, total
    end
    return "ok", total > 0
end

function QD.inv.slot(index)
    local inv_result, container_id = QD._inv_container()
    if inv_result ~= "ok" then
        return inv_result, "inv"
    end
    local result, slot = api_drive.inv_slot(container_id, index)
    if result ~= "ok" then
        return result, slot
    end
    if slot.obj_id <= 0 then
        return "ok", { name = "", count = 0 }
    end
    local name_result, name = api_drive.symbol_name("obj", slot.obj_id)
    if name_result ~= "ok" then
        return name_result, name
    end
    return "ok", { name = name, count = slot.count }
end

function QD.inv.expect_has(name, count)
    local result, total = QD.inv.count(name)
    if result ~= "ok" then
        return result, total
    end
    if total <= 0 then
        return "refused", name .. ": absent"
    end
    if count and total < count then
        return "refused", name .. ": present but short (" .. tostring(total) .. "<" .. tostring(count) .. ")"
    end
    return "ok", total
end

function QD.inv.expect_absent(name)
    local result, total = QD.inv.count(name)
    if result ~= "ok" then
        return result, total
    end
    if total > 0 then
        return "refused", name .. ": present (" .. tostring(total) .. ")"
    end
    return "ok", nil
end

function QD.inv.await(name, count, ticks)
    local obj_result, obj_id = api_drive.symbol("obj", name)
    if obj_result ~= "ok" then
        return obj_result, name
    end
    local inv_result, container_id = QD._inv_container()
    if inv_result ~= "ok" then
        return inv_result, "inv"
    end
    return await({
        level = function()
            local result, total = api_drive.inv_count(container_id, obj_id)
            return result == "ok" and total >= count
        end,
        note = "inv.await " .. name .. " >= " .. tostring(count),
    }, ticks or 10)
end

-- msg -----------------------------------------------------------------

function QD.msg.last(n)
    return api_drive.messages(n)
end

function QD.msg.expect(substring)
    local result, list = api_drive.messages()
    if result ~= "ok" then
        return result, list
    end
    if not QD._msg_contains(list, substring) then
        return "refused", "no message contains: " .. substring
    end
    return "ok", nil
end

-- Scoped to messages inserted AFTER registration (plan 5.7's recorded
-- deviation): a 100-line ring easily holds a stale match from an earlier
-- quest step, so the serial fetched here, before the descriptor is handed
-- to await, is the floor every candidate line must clear.
function QD.msg.await(substring, ticks)
    local serial_result, since = api_drive.message_serial()
    if serial_result ~= "ok" then
        return serial_result, since
    end
    return await({
        level = function()
            local result, list = api_drive.messages()
            if result ~= "ok" then
                return false
            end
            for i = 1, #list do
                if list[i].serial > since and string.find(list[i].text, substring, 1, true) then
                    return true
                end
            end
            return false
        end,
        note = "msg.await " .. substring,
    }, ticks or 10)
end

-- skill -------------------------------------------------------------------

-- `stated` is last_seen_level ~= 0: the pre-login table is a fresh account's,
-- not an empty one, so "is there a reading yet" cannot be asked of the level.
QD.skill = function(name)
    local result, stat_index = api_drive.symbol("stat", name)
    if result ~= "ok" then
        return result, name
    end
    return api_drive.skill(stat_index)
end
