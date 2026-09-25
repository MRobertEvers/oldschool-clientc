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
-- way core.lua's own QD.* functions already read it (core.lua's `cheat`,
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

-- Returns the matching line's text (list[1] is the NEWEST line, so this is
-- the most recent match), or false. The text is what msg.expect answers as
-- its detail: the ledger then shows the line that proved the row.
function QD._msg_contains(list, substring)
    for i = 1, #list do
        if string.find(list[i].text, substring, 1, true) then
            return list[i].text
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

-- The varp with no client half.  ToriRSServer_SendVarpSmall never transmits
-- an id the client's varp array cannot address, so a varp this tree allocates
-- above the cache's highest id (pack/varp.alloc: twocats_lamp_pick 7152,
-- dwarfrock_puzzle_* 7153-7162, against TORIRSSERVER_VARP_CACHE_MAX 5725)
-- reads `not_found` from var_server for the whole run although the NAME
-- resolved and ::setvar wrote it -- the ledger said `not_found/nil`, never
-- `not_found/<name>` (seam12, 2026-09-24).  The embedded server's own copy
-- (api_drive.var_content, DriveState_VarpContent, bounded by
-- TORIRSSERVER_VARP_COUNT) is the only reader that can answer for such an id.
-- It is one number, not a client/server pair, so every answer that came from
-- it says so.  `unsupported` when the binary predates the reader.
QD._VAR_CONTENT_SOURCE = "server content copy; no client copy"

-- (result, value, source) for a resolved varp id: var_server first, the
-- server's own copy only when var_server answered `not_found`.
function QD._var_server_varp(id)
    local result, value = api_drive.var_server(id)
    if result ~= "not_found" then
        return result, value, "server"
    end
    if type(api_drive.var_content) ~= "function" then
        return "unsupported", "this binary has no api_drive.var_content", QD._VAR_CONTENT_SOURCE
    end
    local content_result, content_value = api_drive.var_content(id)
    return content_result, content_value, QD._VAR_CONTENT_SOURCE
end

-- The client's record of the server's value ONLY -- no content fallback.
-- quest.lua's _reading grades a client/server PAIR and reaches the server's
-- own copy itself when both halves say `not_found`; it must see that
-- not_found here, not a fallback answer beside a client that has none.
function QD._var_server_pair(name)
    local kind, id, fail_result, fail_name = QD._var_resolve(name)
    if not kind then
        return fail_result, fail_name
    end
    if kind == "varbit" then
        return api_drive.varbit_server(id)
    end
    return api_drive.var_server(id)
end

-- (result, value[, source]).  source is "server" (the client's var_serv[]
-- record) or QD._VAR_CONTENT_SOURCE; a varbit answers two values as before.
function QD.var.server(name)
    local kind, id, fail_result, fail_name = QD._var_resolve(name)
    if not kind then
        return fail_result, fail_name
    end
    if kind == "varbit" then
        return api_drive.varbit_server(id)
    end
    local result, value, source = QD._var_server_varp(id)
    if result == "unsupported" then
        return result, name .. ": " .. tostring(value)
    end
    return result, value, source
end

-- The shared body of var.await and var.await_server. An `ok` names what was
-- read and where (trap 12: an await that answers a bare ok lands a PASS row
-- with an empty detail through t.expect), and a timeout names the last value
-- the poll saw, so "never landed" and "landed as something else" differ.
function QD._var_await(name, value, ticks, side, verb)
    local kind, id, fail_result, fail_name = QD._var_resolve(name)
    if not kind then
        return fail_result, fail_name
    end
    local read
    local where = side
    if side == "server" and kind == "varp" then
        -- One probe decides the channel for the whole poll: an id the
        -- client cannot address never becomes addressable mid-run.
        local probe_result, probe_value, probe_source = QD._var_server_varp(id)
        if probe_result == "unsupported" then
            return "unsupported", name .. ": " .. tostring(probe_value)
        end
        if probe_source == QD._VAR_CONTENT_SOURCE then
            read = api_drive.var_content
            where = QD._VAR_CONTENT_SOURCE
        else
            read = api_drive.var_server
        end
    elseif side == "server" then
        read = api_drive.varbit_server
    else
        read = (kind == "varbit") and api_drive.varbit or api_drive.varp
    end
    local started = api_drive.tick()
    local last_result, last_value = "unread", nil
    local awaited, note = await({
        level = function()
            last_result, last_value = read(id)
            return last_result == "ok" and last_value == value
        end,
        note = verb .. " " .. name .. " == " .. tostring(value),
    }, ticks or 10)
    local label = (where == side) and (side .. " " .. kind) or (kind .. ", " .. where)
    if awaited == "ok" then
        return "ok", name .. " = " .. tostring(last_value) .. " (" .. label
            .. ") after " .. tostring(api_drive.tick() - started) .. " tick(s)"
    end
    local last = (last_result == "ok") and tostring(last_value) or last_result
    return awaited, tostring(note) .. " (last " .. where .. " read: " .. last .. ")"
end

-- var.await translates a varbit name to its base varp for event matching
-- (var events carry a varp id, plan 5.7); level is re-checked on every
-- server tick regardless, so a plain level predicate is sufficient here.
function QD.var.await(name, value, ticks)
    return QD._var_await(name, value, ticks, "client", "var.await")
end

-- Like var.await, but polls var.server/varbit_server instead of the CLIENT's
-- own read: a cheat's write (::setvar, ::cook's reset) lands on the server
-- first and reaches the client's var[]/varbit[] copy some ticks later --
-- cooksassistant.commit_settle (test/quests/cooks_assistant.lua) already
-- hand-rolls exactly this wait with a raw t.await block; this gives every
-- quest that same wait as one verb instead of a copy of that block each.
function QD.var.await_server(name, value, ticks)
    return QD._var_await(name, value, ticks, "server", "var.await_server")
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

    return "ok", name .. " = " .. tostring(value) .. " (client == server, " .. kind .. ")"
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
    return "ok", name .. ": absent (count 0)"
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
    -- The count before the wait and the count the poll settled on both go in
    -- the detail: "2 -> 3 (>= 3)" says the await watched it arrive, "3 -> 3"
    -- that it was already there.
    local before_result, before = api_drive.inv_count(container_id, obj_id)
    local before_text = (before_result == "ok") and tostring(before) or before_result
    local started = api_drive.tick()
    local last_result, last_total = "unread", nil
    local awaited, note = await({
        level = function()
            last_result, last_total = api_drive.inv_count(container_id, obj_id)
            return last_result == "ok" and last_total >= count
        end,
        note = "inv.await " .. name .. " >= " .. tostring(count),
    }, ticks or 10)
    if awaited == "ok" then
        return "ok", name .. " " .. before_text .. " -> " .. tostring(last_total)
            .. " (>= " .. tostring(count) .. ") after "
            .. tostring(api_drive.tick() - started) .. " tick(s)"
    end
    local last = (last_result == "ok") and tostring(last_total) or last_result
    return awaited, tostring(note) .. " (" .. before_text .. " -> " .. last .. ")"
end

-- One await across a whole requirement table, rather than one inv.await per
-- symbol: a test that needs three ingredients gets one ledger row naming
-- everything still short instead of three rows that only ever say "no" one
-- at a time. Every symbol is resolved ONCE, before the wait starts -- same
-- reason inv.await/inv.count resolve their symbol up front rather than
-- inside the polled predicate: a name that is not content at all is a test
-- bug, not a thing to keep silently re-discovering every tick.
function QD.inv.await_all(items, ticks)
    local inv_result, container_id = QD._inv_container()
    if inv_result ~= "ok" then
        return inv_result, "inv"
    end
    local wanted = {}
    for name, count in pairs(items) do
        local obj_result, obj_id = api_drive.symbol("obj", name)
        if obj_result ~= "ok" then
            return obj_result, name
        end
        wanted[#wanted + 1] = { name = name, obj_id = obj_id, count = count }
    end
    -- pairs() order is not stable; the detail is read by people, sort it.
    table.sort(wanted, function(a, b) return a.name < b.name end)

    -- Set by the level predicate on every poll; read back below once the
    -- await itself has settled, so a timeout's detail names what was still
    -- short at the LAST look rather than forcing a second, separate read
    -- (which could race the very thing that just timed out).
    local short_detail = ""
    local held_detail = ""
    local started = api_drive.tick()
    local awaited, note = await({
        level = function()
            local short = {}
            local held = {}
            for i = 1, #wanted do
                local entry = wanted[i]
                local result, total = api_drive.inv_count(container_id, entry.obj_id)
                if result ~= "ok" then
                    short[#short + 1] = entry.name .. ": " .. result
                elseif total < entry.count then
                    short[#short + 1] = entry.name .. ": " .. tostring(total) .. "<" .. tostring(entry.count)
                else
                    held[#held + 1] = entry.name .. "=" .. tostring(total)
                        .. " (>= " .. tostring(entry.count) .. ")"
                end
            end
            short_detail = table.concat(short, ", ")
            held_detail = table.concat(held, ", ")
            return #short == 0
        end,
        note = "inv.await_all",
    }, ticks or 10)

    if awaited == "ok" then
        return "ok", "all held: " .. held_detail .. " after "
            .. tostring(api_drive.tick() - started) .. " tick(s)"
    end
    if short_detail ~= "" then
        return awaited, "short: " .. short_detail
    end
    return awaited, note
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
    local line = QD._msg_contains(list, substring)
    if not line then
        return "refused", "no message contains: " .. substring
    end
    return "ok", "matched: " .. line
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
    local matched = nil
    local awaited, note = await({
        level = function()
            local result, list = api_drive.messages()
            if result ~= "ok" then
                return false
            end
            for i = 1, #list do
                if list[i].serial > since and string.find(list[i].text, substring, 1, true) then
                    matched = list[i].text
                    return true
                end
            end
            return false
        end,
        note = "msg.await " .. substring,
    }, ticks or 10)
    if awaited == "ok" then
        return "ok", "matched: " .. tostring(matched)
    end
    return awaited, note
end

-- skill -------------------------------------------------------------------

-- t.skill is a TABLE of three reads, not a function: t.skill.read(name),
-- t.skill.snapshot(), t.skill.expect_gain(name, xp, snapshot). The table is
-- built in core.lua's QD constructor, which is concatenated first, so this
-- file only ever adds to it.
--
-- `stated` is last_seen_level ~= 0: the pre-login table is a fresh account's,
-- not an empty one, so "is there a reading yet" cannot be asked of the level.
function QD.skill.read(name)
    local result, stat_index = api_drive.symbol("stat", name)
    if result ~= "ok" then
        return result, name
    end
    return api_drive.skill(stat_index)
end

-- The stat pack's own fixed protocol table (OSRS-Content/osrs239-content/
-- pack/stat.pack: "Skill ids. Fixed by the protocol", torirs_server.h:900-918)
-- -- there is no drive primitive that enumerates stats, only symbol(name)->id
-- and skill(id)->reading, so snapshot has to name every stat itself to visit
-- all of them once. Kept off QD (a `QD._` name, private, same convention as
-- _var_resolve above) rather than a chunk-scope local: only core.lua may
-- declare one of those.
QD._stat_names = {
    "attack", "defence", "strength", "hitpoints", "ranged", "prayer", "magic",
    "cooking", "woodcutting", "fletching", "fishing", "firemaking", "crafting",
    "smithing", "mining", "herblore", "agility", "thieving", "slayer",
    "farming", "runecraft", "hunter", "construction", "sailing", "summoning",
}

-- t.skill.snapshot(): every stat's reading, read once, keyed by name. A stat
-- whose own read did not answer `ok` (not_found on a lane missing a
-- feature-flagged skill, refused, ...) keeps that RESULT STRING as its
-- table entry instead of a reading table, so skill.expect_gain below (or any
-- other caller) can tell a real reading from an unavailable one with a plain
-- `type(snapshot[name]) == "table"` check, never a second round of result
-- comparisons.
function QD.skill.snapshot()
    local snapshot = {}
    for i = 1, #QD._stat_names do
        local name = QD._stat_names[i]
        local result, reading = QD.skill.read(name)
        snapshot[name] = (result == "ok") and reading or result
    end
    return "ok", snapshot
end

-- t.skill.expect_gain(name, xp, snapshot): `snapshot` is an earlier
-- t.skill.snapshot() table. Accepts the delta matching `xp` in either unit
-- the client's own experience field might be carrying -- whole xp, or the
-- server's xp*10 "tenths" -- the same two-unit uncertainty
-- cooksassistant.cooking_xp_up (test/quests/cooks_assistant.lua) already
-- resolves by hand, and names which one matched rather than leaving the
-- caller to guess from the raw delta.
function QD.skill.expect_gain(name, xp, snapshot)
    if type(snapshot) ~= "table" then
        return "no_row", "skill.expect_gain: snapshot is not a table"
    end
    local before = snapshot[name]
    if type(before) ~= "table" or type(before.experience) ~= "number" then
        return "no_row", "skill.expect_gain: snapshot has no reading for " .. tostring(name)
    end
    local result, after = QD.skill.read(name)
    if result ~= "ok" then
        return result, name
    end
    local delta = after.experience - before.experience
    if delta == xp then
        return "ok", name .. ": +" .. delta .. " xp (whole units)"
    end
    if delta == xp * 10 then
        return "ok", name .. ": +" .. delta .. " xp (tenths units)"
    end
    return "refused", name .. ": before=" .. before.experience .. " after=" .. after.experience
        .. " delta=" .. delta .. " expected=" .. xp .. " or " .. (xp * 10)
end

-- death -------------------------------------------------------------------
--
-- SEAM combat-hunt-kills-the-character (2026-09-20).  Mort'ton's shade hunt
-- ran for 1,006 server ticks and forty attack attempts and reported `holding
-- 4/5 shade_bones1 after 40 attempt(s) ... no target to engage, waited for a
-- respawn` (build/quest_gate/mortton/ledger.tsv row 19).  Its own screenshot,
-- shots/38-shadeHunt.png, is the Lumbridge castle courtyard: the chat log
-- reads "That's one Shade!" ... "That's four Shades!", then "Oh dear, you are
-- dead!" TWICE, "You wake up in Lumbridge.", "I can't reach that!".  The
-- character was killed at four kills and every attempt after that was clicked
-- from the respawn tile, ninety tiles away -- and the row blamed the shade
-- population, because NOTHING IN THIS DRIVER COULD SEE A DEATH: there was no
-- reading of whether the player was alive and no arm on any settle for the
-- sentence the server prints when he is not.
--
-- This file holds the READING only.  The rule built on it -- a death ENDS THE
-- RUN with a row named `player.died` -- is QD.player._death_fence, in
-- combat.lua, because ending a run is a policy and this file only answers
-- questions.  (The reading is in core-state's file and not verbs-pointer's
-- because it is a message-ring read like every other one here; the `player.`
-- namespace is where a quest file will look for it.  pointer.lua is owned by
-- two other seams in this same pass, so nothing here may live there yet.)
--
-- WHY THE CHAT LINE AND NOT THE HITPOINTS.  A client is told its own
-- hitpoints, so `skill.read("hitpoints").level == 0` looks like the reading to
-- take -- but it is true only for the ticks between the killing blow and
-- [proc,player_death_restore]'s refill (OSRS-Content/osrs239-content/server/
-- scripts/player/death.rs2), and a driver that polls once a tick while a click
-- verb is blocked inside a twenty-tick settle sees none of it.  What survives
-- is the sentence: death.rs2:280/284 and skill_combat/combat.rs2:398 print
-- "Oh dear, you are dead!", followed by "You wake up in Lumbridge." (or the
-- Gauntlet hub's line), and both stay in the client's 100-line ring.
-- src/game/rs_game_events.c:482 already reads that same line for the same
-- reason, which is where the exact spelling below comes from.
--
-- Read WHOLE-RING and LATCHED, not scoped to a serial window the way
-- QD.msg.await is: a death is not an event one verb owns, it is a fact about
-- every row after it, and the ring rolls, so the first reading that sees it is
-- the one that has to remember it.  There is no cross-run pollution to guard
-- against -- run.py gives every quest its own client process and its own
-- fixture.
QD.player.DEATH_LINE = "Oh dear, you are dead!"

-- The respawn sentence is not what is matched on -- it is what the detail
-- QUOTES, so a row says where the character woke up as well as that he fell.
QD.player.RESPAWN_LINES = {
    "You wake up in Lumbridge.",
    "You wake up in the Gauntlet hub.",
}

-- A chat line with its colour codes taken off and trimmed at both ends --
-- rs_game_events.c's own rule ("@" + three characters + "@", stripped before
-- any match; its own test pins "@red@Oh dear, you are dead!").  Content's
-- mes() writes these plain, but a line that reached the ring coloured would
-- otherwise read as a different sentence, and this is the one sentence the
-- driver must never miss.
function QD.player._plain_line(text)
    if type(text) ~= "string" then
        return ""
    end
    local stripped = string.gsub(text, "@%w%w%w@", "")
    return string.match(stripped, "^%s*(.-)%s*$") or stripped
end

-- The latch: nil until a death line has been seen, and from then on the
-- record, never re-read -- the ring that proved it may have rolled the line
-- off by the time anything asks again.
QD._death = nil

-- The record, or nil while this run has not died.  Cheap once latched.
function QD.player._death_record()
    if QD._death then
        return QD._death
    end
    local result, rows = api_drive.messages()
    if result ~= "ok" or type(rows) ~= "table" then
        -- A message read that did not answer says nothing about whether the
        -- player is alive, and claiming a death on it would end runs for a
        -- transient. Nothing is latched.
        return nil
    end
    local deaths = 0
    local newest = nil
    local wake = ""
    for i = 1, #rows do
        local line = QD.player._plain_line(rows[i].text)
        if line == QD.player.DEATH_LINE then
            deaths = deaths + 1
            if newest == nil or rows[i].serial > newest then
                newest = rows[i].serial
            end
        end
        for j = 1, #QD.player.RESPAWN_LINES do
            if line == QD.player.RESPAWN_LINES[j] then
                wake = line
            end
        end
    end
    if deaths == 0 then
        return nil
    end
    local tile_result, tile = api_drive.player_tile()
    local hp_result, hp = QD.skill.read("hitpoints")
    QD._death = {
        deaths = deaths,
        serial = newest,
        wake = wake,
        tick = api_drive.tick(),
        where = (tile_result == "ok" and type(tile) == "table")
            and (tostring(tile.x) .. "," .. tostring(tile.z) .. " L" .. tostring(tile.level))
            or tostring(tile_result),
        hitpoints = (hp_result == "ok" and type(hp) == "table")
            and (tostring(hp.level) .. "/" .. tostring(hp.base_level))
            or tostring(hp_result),
    }
    return QD._death
end

-- One sentence, so a reader meets the same facts wherever a death surfaces --
-- in a verb's `refused`, in the terminal row, in a note.
function QD.player._death_text(record)
    if not record then
        return "no death recorded"
    end
    return "the character DIED during this run: '" .. QD.player.DEATH_LINE
        .. "' is in the chat ring " .. tostring(record.deaths) .. " time(s)"
        .. ((record.wake ~= "") and (", followed by '" .. record.wake .. "'") or "")
        .. "; first read by the driver at tick " .. tostring(record.tick)
        .. ", standing at " .. tostring(record.where)
        .. ", hitpoints " .. tostring(record.hitpoints)
        .. " -- every click after a death is driven from the respawn point, not"
        .. " from wherever the quest left off"
end

-- t.player.alive() -> `ok` / `refused`.
--
-- The reading, never the rule: this writes no row and ends nothing, so a quest
-- file may assert it or branch on it.  `refused` for a death seen this run,
-- and also for a STATED hitpoints reading of 0 with no death line yet -- the
-- ticks of dying before the respawn -- because a character at zero is not
-- alive whatever the chatbox has caught up with.  `stated` is the guard the
-- pre-login table needs: an unstated stat table is a fresh account's zeros,
-- not a corpse.
--
-- It takes NO ARGUMENT, so it cannot go through t.exec, which grades a nil
-- first argument FAIL `bad verb/target` (core.lua's QD.exec).  Record it the
-- way every other argument-free read is recorded --
-- `t.expect("player.alive", t.player.alive())` -- which is also the right
-- shape: t.expect takes no screenshot, and photographing a state nothing
-- clicked is trap 4.
function QD.player.alive()
    local record = QD.player._death_record()
    if record then
        return "refused", QD.player._death_text(record)
    end
    local hp_result, hp = QD.skill.read("hitpoints")
    if hp_result ~= "ok" or type(hp) ~= "table" then
        return hp_result, "player.alive: the hitpoints reading answered " .. tostring(hp_result)
    end
    local tile_result, tile = api_drive.player_tile()
    local where = (tile_result == "ok" and type(tile) == "table")
        and (tostring(tile.x) .. "," .. tostring(tile.z) .. " L" .. tostring(tile.level))
        or tostring(tile_result)
    if hp.stated and hp.level <= 0 then
        return "refused", "player.alive: hitpoints 0/" .. tostring(hp.base_level)
            .. " at " .. where .. " -- dying, and no death line has been printed yet"
    end
    return "ok", "hitpoints " .. tostring(hp.level) .. "/" .. tostring(hp.base_level)
        .. " at " .. where .. ", no death line in the chat ring"
end
