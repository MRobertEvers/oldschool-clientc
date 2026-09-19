-- The verb conformance harness: call EVERY verb the quest driver exposes on
-- `t`, exactly once, against a live world, and leave one ledger row per verb.
--
-- Why this file exists.  The driver was written, reviewed twice and passed
-- every gate in the tree while its verb layer did nothing, because the gate
-- was "it compiles" and no reviewer ever ran a verb.  This harness is the
-- replacement gate: `make -C src test-quest-conformance` is red until every
-- row here says PASS, so "fixed" means the ledger moved, not that the build
-- is green.  It is EXPECTED to be mostly failing the day it lands -- that
-- failing table is the baseline the repair phases are measured against.
--
-- Shape.  Every verb is one entry in PLAN, in dependency order: the world is
-- put into a known state first (an npc to point at, an item in the backpack,
-- an item on the ground, a dialogue to read), then the readers, then the
-- actions, then the dialogue pages, then the pure helpers.  An entry NEVER
-- aborts the run:
--   * a verb that is not a function answers "missing" and is not called;
--   * a verb whose subject could not be built (the npc did not resolve, so
--     there is no target to project) answers "no_subject" and is not called,
--     naming the dependency that failed;
--   * every other answer is the verb's own (result, detail), verbatim.
-- There is no pcall in this sandbox (torirs_plugin_lua.c:3925 removes it on
-- purpose), so "never aborts" is bought with type checks and valid argument
-- shapes, not with a catch.  A verb that raises anyway ends the run at that
-- row; the rows already written are on disk (drive_ledger_write appends one
-- row at a time) and tools/quest_gate/conformance.py reports every verb after
-- it as "abort" rather than silently scoring them.
--
-- Rules this file keeps (docs/ARCHITECT.md S2, test/quests/README.md):
--   * no numeric interface or component id, and no client op string: every
--     target is a content symbol, every option is an op NUMBER;
--   * no lane name anywhere;
--   * cheats used to STATE THE WORLD are setup, not rows -- exactly as a
--     normal quest test's `setup = { ... }` is.  The t.cheat row is its own
--     dedicated call, and every setup cheat is marked `-- setup` below.
--
-- Two rows cannot grade themselves from inside Lua, and are re-graded by
-- tools/quest_gate/conformance.py against evidence outside the coroutine:
--   * "note" -- t.note folds its text into the NEXT row's detail, which is
--     this row.  The checker fails the row unless CONFORMANCE_NOTE_PROBE is
--     in the detail column.
--   * "t.finish" -- finish ENDS the run, so the row is written first and the
--     call is made after the loop.  The checker fails the row unless the
--     ledger's SUMMARY row says exit=0 and the process exited 0.
--
-- ---------------------------------------------------------------------------
-- 78 verbs, one row each.  tools/quest_gate/verb_list.py --check reads the
-- `step("<name>", ...)` lines below and the QD.* definitions in
-- script/plugins/quest_driver/*.lua and refuses to agree when they differ, so
-- a verb added to the driver with no row here fails a make gate rather than
-- being quietly never called.  The count is asserted in the harness too, so
-- editing this file alone cannot drift either.
-- @verb-count 78
-- ---------------------------------------------------------------------------

local VERB_COUNT = 78
local NOTE_PROBE = "CONFORMANCE_NOTE_PROBE"

-- Content symbols, never ids.  Each is the subject some verb needs, and each
-- exists in this content pack (OSRS-Content/osrs239-content/configs/*.compack).
-- Every cheat below is a [debugproc], never one of torirs_server_world.c's own
-- ::give / ::spawn / ::setlevel branches: DriveCore_Cheat
-- (src/plugin/torirs_plugin_drive.c:357-364) dispatches ONLY through
-- ToriRSServer_RunDebugprocForTest, so an engine-ladder cheat answers no_row
-- and does nothing. Using one here would score a t.cheat defect against
-- whichever verb went without its subject.
local NPC_SYMBOL = "man"            -- Lumbridge's own; no debugproc spawns npcs
local NPC_DISPLAY_NAME = "Man"      -- npc.by_name matches the DISPLAY name
local COOK_SYMBOL = "cook"          -- the head the cook's dialogue shows
local LOC_SYMBOL = "tree"           -- Lumbridge has these in every direction
local OBJ_SYMBOL = "airrune"        -- ::runes puts 25 in the backpack
local ABSENT_OBJ_SYMBOL = "knife"   -- nothing here puts one in the backpack
local VARP_SYMBOL = "tutorial"      -- the fixture pins it (perm scope)
local VARP_VALUE = 1000             -- "tutorial finished" (docs/WORKTREE_SETUP.md)
local VARBIT_SYMBOL = "troll_freed_eadgar"
local STAT_SYMBOL = "cooking"
local INVENTORY_INTERFACE = "inventory"
local OBJECTBOX_INTERFACE = "objectbox"
local INVENTORY_ITEMS_COMPONENT = "inventory:items"
local OBJBOX_TEXT_FRAGMENT = "You get some"   -- interface_chat/scripts/chat.rs2:408
local DROP_MESSAGE_FRAGMENT = "Dropped"       -- ::dropobj's reply (cheat_obj.rs2:20)

-- A detail column is a string or the ledger's luaL_optstring raises.  Tables
-- (a verb that answers with a row, a tile, an options list) are summarised
-- rather than dropped: what a verb ANSWERED WITH is half of what this harness
-- is for.
local function describe(value, depth)
    depth = depth or 0
    local kind = type(value)
    if value == nil then
        return "nil"
    elseif kind == "string" then
        if #value > 120 then
            return string.sub(value, 1, 120) .. "..."
        end
        return value
    elseif kind == "number" or kind == "boolean" then
        return tostring(value)
    elseif kind == "function" then
        return "<function>"
    elseif kind == "table" then
        if depth > 1 then
            return "<table>"
        end
        local parts = {}
        local count = 0
        for key, entry in pairs(value) do
            count = count + 1
            if count > 8 then
                parts[#parts + 1] = "..."
                break
            end
            parts[#parts + 1] = tostring(key) .. "=" .. describe(entry, depth + 1)
        end
        if count == 0 then
            return "{}"
        end
        return "{" .. table.concat(parts, " ") .. "}"
    end
    return "<" .. kind .. ">"
end

return {
    id = "_conformance",
    fixture = "fresh_lumbridge.ini",
    -- Stated here for the runner's benefit; this harness re-issues them
    -- itself through t.cheat so it can also run standalone.
    setup = {},

    run = function(t)
        -- Rewritten by tools/quest_gate/conformance.py between attempts. There
        -- is no pcall here, so a verb that RAISES (rather than answering a
        -- result) ends the run where it stands. The runner attributes that
        -- error to the first verb with no row, records it as "error" with the
        -- message, adds it here, and re-runs -- so every verb still gets
        -- exactly one row, and an unrunnable verb costs its own row and no
        -- one else's. Empty on a clean first attempt.
        local SKIP = {} --@skip

        local PLAN = {}
        local verb_count = 0

        -- One entry per verb.  `fn` returns (result, detail) -- or nil when it
        -- has already written its own row (t.step is the only one).
        local function step(name, fn)
            verb_count = verb_count + 1
            PLAN[#PLAN + 1] = { name = name, call = fn }
        end

        -- A world-state entry: a cheat ladder that gives the verbs after it a
        -- subject.  It writes no row, is never skipped, and is exactly what a
        -- normal quest test's `setup = { ... }` list is.
        local function stage(fn)
            PLAN[#PLAN + 1] = { stage = fn }
        end

        -- Resolve a verb by its dotted path without ever indexing a nil.
        local function verb(namespace, name)
            if name == nil then
                local value = t[namespace]
                if type(value) == "function" then
                    return value
                end
                return nil
            end
            local group = t[namespace]
            if type(group) ~= "table" then
                return nil
            end
            local value = group[name]
            if type(value) == "function" then
                return value
            end
            return nil
        end

        local function missing(namespace, name)
            if name == nil then
                return "missing", "t." .. namespace .. " is not a function"
            end
            return "missing", "t." .. namespace .. "." .. name .. " is not a function"
        end

        -- A cheat that STATES THE WORLD.  Not a row: setup, exactly as a
        -- normal quest test's `setup = { ... }` list is.
        local function setup_cheat(text)
            local cheat = verb("t", "cheat")
            if cheat then
                cheat(text)
            end
        end

        local function settle(ticks)
            local advance = verb("t", "ticks")
            if advance then
                advance(ticks)
            end
        end

        -- Subjects the action verbs need, filled in by their own rows below so
        -- a later verb can say WHICH dependency was missing instead of
        -- crashing on a nil.
        local npc_target = nil
        local player_tile = nil
        local inventory_widget = nil

        -- ------------------------------------------------------- the world

        -- Lumbridge, an npc to point at, bread in the backpack, bread on the
        -- ground, a skill with a reading.  All setup, no rows.
        setup_cheat("::tele lumbridge")
        settle(4)
        setup_cheat("::runes 25")
        setup_cheat("::dropobj " .. OBJ_SYMBOL .. " 1")
        setup_cheat("::xp " .. STAT_SYMBOL .. " 500")
        settle(6)

        -- --------------------------------------------- phase 0: the clock

        step("t.cheat", function()
            local fn = verb("t", "cheat")
            if not fn then return missing("t", "cheat") end
            local result, detail = fn("::xp " .. STAT_SYMBOL .. " 100")
            return result, "::xp -> " .. describe(detail)
        end)

        step("t.ticks", function()
            local fn = verb("t", "ticks")
            if not fn then return missing("t", "ticks") end
            local result, detail = fn(2)
            return result, "advance 2 server ticks -> " .. describe(detail)
        end)

        step("t.settle", function()
            local fn = verb("t", "settle")
            if not fn then return missing("t", "settle") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("t.shot", function()
            local fn = verb("t", "shot")
            if not fn then return missing("t", "shot") end
            local result, detail = fn("conformance-world")
            return result, describe(detail)
        end)

        -- ------------------------------------- phase 1: naming the world

        step("world.tile", function()
            local fn = verb("world", "tile")
            if not fn then return missing("world", "tile") end
            local result, detail = fn()
            if result == "ok" and type(detail) == "table" then
                player_tile = detail
            end
            return result, describe(detail)
        end)

        step("world.level", function()
            local fn = verb("world", "level")
            if not fn then return missing("world", "level") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("npc.by_name", function()
            local fn = verb("npc", "by_name")
            if not fn then return missing("npc", "by_name") end
            local result, detail = fn(NPC_DISPLAY_NAME)
            return result, NPC_DISPLAY_NAME .. " -> " .. describe(detail)
        end)

        step("npc.by_symbol", function()
            local fn = verb("npc", "by_symbol")
            if not fn then return missing("npc", "by_symbol") end
            local result, detail = fn(NPC_SYMBOL)
            return result, NPC_SYMBOL .. " -> " .. describe(detail)
        end)

        step("npc.nearest", function()
            local fn = verb("npc", "nearest")
            if not fn then return missing("npc", "nearest") end
            local result, detail = fn(NPC_SYMBOL, 40)
            return result, NPC_SYMBOL .. " r=40 -> " .. describe(detail)
        end)

        step("world.loc_near", function()
            local fn = verb("world", "loc_near")
            if not fn then return missing("world", "loc_near") end
            local result, detail = fn(LOC_SYMBOL, 60)
            return result, LOC_SYMBOL .. " r=60 -> " .. describe(detail)
        end)

        step("world.obj_near", function()
            local fn = verb("world", "obj_near")
            if not fn then return missing("world", "obj_near") end
            local result, detail = fn(OBJ_SYMBOL, 20)
            return result, OBJ_SYMBOL .. " r=20 -> " .. describe(detail)
        end)

        step("player.by_symbol", function()
            local fn = verb("player", "by_symbol")
            if not fn then return missing("player", "by_symbol") end
            -- (target, "ok") on success; (nil, result, name) on failure.
            local target, result, name = fn("npc", NPC_SYMBOL)
            if type(target) == "table" then
                npc_target = target
                return "ok", "npc " .. NPC_SYMBOL .. " -> " .. describe(target)
            end
            return result or "no_row", "npc " .. NPC_SYMBOL .. " -> " .. describe(name)
        end)

        -- ------------------------------------------ phase 2: state reads

        step("var.varp", function()
            local fn = verb("var", "varp")
            if not fn then return missing("var", "varp") end
            local result, detail = fn(VARP_SYMBOL)
            return result, VARP_SYMBOL .. " -> " .. describe(detail)
        end)

        step("var.varbit", function()
            local fn = verb("var", "varbit")
            if not fn then return missing("var", "varbit") end
            local result, detail = fn(VARBIT_SYMBOL)
            return result, VARBIT_SYMBOL .. " -> " .. describe(detail)
        end)

        step("var.server", function()
            local fn = verb("var", "server")
            if not fn then return missing("var", "server") end
            local result, detail = fn(VARP_SYMBOL)
            return result, VARP_SYMBOL .. " -> " .. describe(detail)
        end)

        step("var.expect", function()
            local fn = verb("var", "expect")
            if not fn then return missing("var", "expect") end
            local result, detail = fn(VARP_SYMBOL, VARP_VALUE)
            return result, VARP_SYMBOL .. " == " .. VARP_VALUE .. " -> " .. describe(detail)
        end)

        step("var.await", function()
            local fn = verb("var", "await")
            if not fn then return missing("var", "await") end
            local result, detail = fn(VARP_SYMBOL, VARP_VALUE, 3)
            return result, VARP_SYMBOL .. " == " .. VARP_VALUE .. " -> " .. describe(detail)
        end)

        step("skill", function()
            local fn = verb("skill")
            if not fn then return missing("skill") end
            local result, detail = fn(STAT_SYMBOL)
            return result, STAT_SYMBOL .. " -> " .. describe(detail)
        end)

        step("inv.count", function()
            local fn = verb("inv", "count")
            if not fn then return missing("inv", "count") end
            local result, detail = fn(OBJ_SYMBOL)
            return result, OBJ_SYMBOL .. " -> " .. describe(detail)
        end)

        step("inv.has", function()
            local fn = verb("inv", "has")
            if not fn then return missing("inv", "has") end
            local result, detail = fn(OBJ_SYMBOL)
            return result, OBJ_SYMBOL .. " -> " .. describe(detail)
        end)

        step("inv.slot", function()
            local fn = verb("inv", "slot")
            if not fn then return missing("inv", "slot") end
            local result, detail = fn(1)
            return result, "slot 1 -> " .. describe(detail)
        end)

        step("inv.expect_has", function()
            local fn = verb("inv", "expect_has")
            if not fn then return missing("inv", "expect_has") end
            local result, detail = fn(OBJ_SYMBOL, 1)
            return result, OBJ_SYMBOL .. " >= 1 -> " .. describe(detail)
        end)

        step("inv.expect_absent", function()
            local fn = verb("inv", "expect_absent")
            if not fn then return missing("inv", "expect_absent") end
            local result, detail = fn(ABSENT_OBJ_SYMBOL)
            return result, ABSENT_OBJ_SYMBOL .. " -> " .. describe(detail)
        end)

        step("inv.await", function()
            local fn = verb("inv", "await")
            if not fn then return missing("inv", "await") end
            local result, detail = fn(OBJ_SYMBOL, 1, 3)
            return result, OBJ_SYMBOL .. " >= 1 -> " .. describe(detail)
        end)

        step("msg.last", function()
            local fn = verb("msg", "last")
            if not fn then return missing("msg", "last") end
            local result, detail = fn(5)
            return result, "last 5 -> " .. describe(detail)
        end)

        step("msg.expect", function()
            local fn = verb("msg", "expect")
            if not fn then return missing("msg", "expect") end
            local result, detail = fn(DROP_MESSAGE_FRAGMENT)
            return result, "contains '" .. DROP_MESSAGE_FRAGMENT .. "' -> " .. describe(detail)
        end)

        -- setup: msg.await is scoped to lines that arrive AFTER it registers,
        -- so the line it waits for is sent here -- dispatched into the server
        -- now, reaching the client a tick later, inside the await.
        stage(function()
            setup_cheat("::dropobj " .. OBJ_SYMBOL .. " 1")
        end)

        step("msg.await", function()
            local fn = verb("msg", "await")
            if not fn then return missing("msg", "await") end
            local result, detail = fn(DROP_MESSAGE_FRAGMENT, 6)
            return result, "new line containing '" .. DROP_MESSAGE_FRAGMENT .. "' -> " .. describe(detail)
        end)

        -- -------------------------------- phase 3: pointing and acting

        step("drive.camera", function()
            local fn = verb("drive", "camera")
            if not fn then return missing("drive", "camera") end
            local result, detail = fn(0, 300, 400)
            return result, "yaw=0 pitch=300 zoom=400 -> " .. describe(detail)
        end)

        step("drive.screen_position", function()
            local fn = verb("drive", "screen_position")
            if not fn then return missing("drive", "screen_position") end
            if not npc_target then
                return "no_subject", "player.by_symbol(npc, " .. NPC_SYMBOL .. ") built no target"
            end
            local result, detail = fn(npc_target)
            return result, describe(detail)
        end)

        step("drive.click_minimenu", function()
            local fn = verb("drive", "click_minimenu")
            if not fn then return missing("drive", "click_minimenu") end
            if not npc_target then
                return "no_subject", "player.by_symbol(npc, " .. NPC_SYMBOL .. ") built no target"
            end
            local result, detail = fn(npc_target, 1)
            return result, "op slot 1 -> " .. describe(detail)
        end)

        step("drive.op", function()
            local fn = verb("drive", "op")
            if not fn then return missing("drive", "op") end
            if not npc_target then
                return "no_subject", "player.by_symbol(npc, " .. NPC_SYMBOL .. ") built no target"
            end
            local result, detail = fn(npc_target, 1)
            return result, "LOGGED bypass, op 1 -> " .. describe(detail)
        end)

        step("player.idle", function()
            local fn = verb("player", "idle")
            if not fn then return missing("player", "idle") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("player.walk_to", function()
            local fn = verb("player", "walk_to")
            if not fn then return missing("player", "walk_to") end
            if not player_tile or type(player_tile.x) ~= "number" then
                return "no_subject", "world.tile() answered no tile to walk one step from"
            end
            local result, detail = fn(player_tile.x + 1, player_tile.z)
            return result, "one tile east -> " .. describe(detail)
        end)

        step("player.walk_near", function()
            local fn = verb("player", "walk_near")
            if not fn then return missing("player", "walk_near") end
            if not npc_target then
                return "no_subject", "player.by_symbol(npc, " .. NPC_SYMBOL .. ") built no target"
            end
            local result, detail = fn(npc_target)
            return result, describe(detail)
        end)

        step("player.talk_to", function()
            local fn = verb("player", "talk_to")
            if not fn then return missing("player", "talk_to") end
            local result, detail = fn(NPC_SYMBOL)
            return result, NPC_SYMBOL .. " -> " .. describe(detail)
        end)

        step("player.click_loc", function()
            local fn = verb("player", "click_loc")
            if not fn then return missing("player", "click_loc") end
            local result, detail = fn(LOC_SYMBOL)
            return result, LOC_SYMBOL .. " -> " .. describe(detail)
        end)

        step("player.click_obj", function()
            local fn = verb("player", "click_obj")
            if not fn then return missing("player", "click_obj") end
            local result, detail = fn(OBJ_SYMBOL)
            return result, OBJ_SYMBOL .. " -> " .. describe(detail)
        end)

        step("player.use_on", function()
            local fn = verb("player", "use_on")
            if not fn then return missing("player", "use_on") end
            local result, detail = fn(OBJ_SYMBOL, npc_target)
            return result, describe(detail)
        end)

        step("player.inv_op", function()
            local fn = verb("player", "inv_op")
            if not fn then return missing("player", "inv_op") end
            local result, detail = fn(OBJ_SYMBOL, 1)
            return result, describe(detail)
        end)

        step("player.equip", function()
            local fn = verb("player", "equip")
            if not fn then return missing("player", "equip") end
            local result, detail = fn(OBJ_SYMBOL)
            return result, describe(detail)
        end)

        step("player.drop", function()
            local fn = verb("player", "drop")
            if not fn then return missing("player", "drop") end
            local result, detail = fn(OBJ_SYMBOL)
            return result, describe(detail)
        end)

        -- ------------------------------------------------- phase 4: ui

        step("ui.widget", function()
            local fn = verb("ui", "widget")
            if not fn then return missing("ui", "widget") end
            local result, detail = fn(INVENTORY_ITEMS_COMPONENT)
            if result == "ok" then
                inventory_widget = detail
            end
            return result, INVENTORY_ITEMS_COMPONENT .. " -> " .. describe(detail)
        end)

        step("ui.invoke", function()
            local fn = verb("ui", "invoke")
            if not fn then return missing("ui", "invoke") end
            if inventory_widget == nil then
                return "no_subject", "ui.widget(" .. INVENTORY_ITEMS_COMPONENT .. ") resolved nothing to invoke"
            end
            local result, detail = fn(inventory_widget, 1)
            return result, "op 1 -> " .. describe(detail)
        end)

        step("ui.tab", function()
            local fn = verb("ui", "tab")
            if not fn then return missing("ui", "tab") end
            -- The name a quest author would write.  ui.tab has no name ->
            -- number table (ui.lua:84-96); passing a bare number instead would
            -- hide that behind the test's own knowledge.
            local result, detail = fn(INVENTORY_INTERFACE)
            return result, INVENTORY_INTERFACE .. " -> " .. describe(detail)
        end)

        step("ui.await_open", function()
            local fn = verb("ui", "await_open")
            if not fn then return missing("ui", "await_open") end
            local result, detail = fn(INVENTORY_INTERFACE, 4)
            return result, INVENTORY_INTERFACE .. " -> " .. describe(detail)
        end)

        step("t.key", function()
            local fn = verb("t", "key")
            if not fn then return missing("t", "key") end
            local result, detail = fn("enter")
            return result, "enter press+release -> " .. describe(detail)
        end)

        step("t.text", function()
            local fn = verb("t", "text")
            if not fn then return missing("t", "text") end
            local result, detail = fn("conformance")
            return result, describe(detail)
        end)

        -- ------------------------------- phase 5: the objectbox dialogue

        step("ui.open", function()
            local fn = verb("ui", "open")
            if not fn then return missing("ui", "open") end
            local result, detail = fn(OBJECTBOX_INTERFACE, "::objbox " .. OBJ_SYMBOL .. " 250")
            return result, OBJECTBOX_INTERFACE .. " via ::objbox -> " .. describe(detail)
        end)

        step("ui.is_modal", function()
            local fn = verb("ui", "is_modal")
            if not fn then return missing("ui", "is_modal") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("chat.kind", function()
            local fn = verb("chat", "kind")
            if not fn then return missing("chat", "kind") end
            local kind = fn()
            if kind == "objbox" then
                return "ok", "objbox"
            end
            return "refused", "expected objbox with ::objbox up, got " .. describe(kind)
        end)

        step("chat.item", function()
            local fn = verb("chat", "item")
            if not fn then return missing("chat", "item") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("chat.expect_item", function()
            local fn = verb("chat", "expect_item")
            if not fn then return missing("chat", "expect_item") end
            local result, detail = fn(OBJ_SYMBOL)
            return result, OBJ_SYMBOL .. " -> " .. describe(detail)
        end)

        step("chat.text", function()
            local fn = verb("chat", "text")
            if not fn then return missing("chat", "text") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("chat.expect_text", function()
            local fn = verb("chat", "expect_text")
            if not fn then return missing("chat", "expect_text") end
            local result, detail = fn(OBJBOX_TEXT_FRAGMENT)
            return result, "'" .. OBJBOX_TEXT_FRAGMENT .. "' -> " .. describe(detail)
        end)

        step("chat.continue_", function()
            local fn = verb("chat", "continue_")
            if not fn then return missing("chat", "continue_") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("ui.await_close", function()
            local fn = verb("ui", "await_close")
            if not fn then return missing("ui", "await_close") end
            local result, detail = fn(OBJECTBOX_INTERFACE, 6)
            return result, OBJECTBOX_INTERFACE .. " after chat.continue_ -> " .. describe(detail)
        end)

        -- ------------------------------ phase 6: a dialogue with choices

        -- setup: the cook's quest-start dialogue -- an npc page (a head and a
        -- name) that runs on into a chatmenu.
        stage(function()
            setup_cheat("::cookbmp_choice")
            settle(3)
        end)

        step("chat.head", function()
            local fn = verb("chat", "head")
            if not fn then return missing("chat", "head") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("chat.name", function()
            local fn = verb("chat", "name")
            if not fn then return missing("chat", "name") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("chat.expect_head", function()
            local fn = verb("chat", "expect_head")
            if not fn then return missing("chat", "expect_head") end
            local result, detail = fn(COOK_SYMBOL)
            return result, COOK_SYMBOL .. " -> " .. describe(detail)
        end)

        step("chat.drain", function()
            local fn = verb("chat", "drain")
            if not fn then return missing("chat", "drain") end
            local result, detail = fn({ stop_at = "options", max_pages = 12 })
            return result, "stop_at=options -> " .. describe(detail)
        end)

        step("chat.options", function()
            local fn = verb("chat", "options")
            if not fn then return missing("chat", "options") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("chat.options_title", function()
            local fn = verb("chat", "options_title")
            if not fn then return missing("chat", "options_title") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("chat.choose", function()
            local fn = verb("chat", "choose")
            if not fn then return missing("chat", "choose") end
            local result, detail = fn(1)
            return result, "row 1 -> " .. describe(detail)
        end)

        step("chat.count", function()
            local fn = verb("chat", "count")
            if not fn then return missing("chat", "count") end
            local result, detail = fn(1)
            return result, describe(detail)
        end)

        step("chat.name_entry", function()
            local fn = verb("chat", "name_entry")
            if not fn then return missing("chat", "name_entry") end
            local result, detail = fn("conformance")
            return result, describe(detail)
        end)

        step("chat.close", function()
            local fn = verb("chat", "close")
            if not fn then return missing("chat", "close") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        -- ------------------------ phase 7: the scroll and the levelup box

        step("scroll.title", function()
            local fn = verb("scroll", "title")
            if not fn then return missing("scroll", "title") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("scroll.rewards", function()
            local fn = verb("scroll", "rewards")
            if not fn then return missing("scroll", "rewards") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("scroll.close", function()
            local fn = verb("scroll", "close")
            if not fn then return missing("scroll", "close") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("levelup.skill", function()
            local fn = verb("levelup", "skill")
            if not fn then return missing("levelup", "skill") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("levelup.continue_", function()
            local fn = verb("levelup", "continue_")
            if not fn then return missing("levelup", "continue_") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        -- ------------------------- phase 8: the scheduler's own controls

        step("await", function()
            local fn = verb("await")
            if not fn then return missing("await") end
            local result, detail = fn({
                level = function() return true end,
                note = "conformance.await",
            }, 2)
            return result, "a level predicate true at registration -> " .. describe(detail)
        end)

        step("ok", function()
            local fn = verb("ok")
            if not fn then return missing("ok") end
            local yes = fn("ok")
            local no = fn("timeout")
            if yes == true and no == false then
                return "ok", 'ok("ok")=true, ok("timeout")=false'
            end
            return "refused", 'ok("ok")=' .. describe(yes) .. ' ok("timeout")=' .. describe(no)
        end)

        step("fail", function()
            local fn = verb("fail")
            if not fn then return missing("fail") end
            local failed, detail = fn("timeout", "why")
            local not_failed = fn("ok")
            if failed == true and detail == "why" and not_failed == false then
                return "ok", 'fail("timeout","why")=(true,"why"), fail("ok")=false'
            end
            return "refused", "fail(timeout,why)=(" .. describe(failed) .. "," .. describe(detail)
                .. ") fail(ok)=" .. describe(not_failed)
        end)

        step("note", function()
            local fn = verb("note")
            if not fn then return missing("note") end
            fn(NOTE_PROBE)
            -- t.note folds into the NEXT row's detail, which is this one.
            -- tools/quest_gate/conformance.py fails this row unless the probe
            -- is in the detail column.
            return "ok", "a note was posted and must appear in this detail"
        end)

        step("t.step", function()
            local fn = verb("t", "step")
            if not fn then return missing("t", "step") end
            fn("t.step", "PASS", "this row was written by t.step itself, not by t.expect")
            return nil
        end)

        step("t.expect", function()
            local fn = verb("t", "expect")
            if not fn then return missing("t", "expect") end
            -- Every other row in this ledger was written by t.expect; a row
            -- named t.expect, in a ledger with a header, IS the evidence.
            return "ok", "wrote every other row in this ledger"
        end)

        step("t.finish", function()
            local fn = verb("t", "finish")
            if not fn then return missing("t", "finish") end
            -- finish ENDS the run, so it is called after the loop.  The
            -- ledger's SUMMARY row (exit=) and the process exit code are what
            -- tools/quest_gate/conformance.py re-grades this row against.
            return "ok", "called after this row; SUMMARY exit= and the process exit code are the evidence"
        end)

        -- ------------------------------------------------------ the run

        if verb_count ~= VERB_COUNT then
            local recorder = verb("t", "step")
            if recorder then
                recorder("conformance-plan", "FAIL",
                    "the plan holds " .. verb_count .. " verbs, the driver exposes "
                    .. VERB_COUNT .. " -- run tools/quest_gate/verb_list.py to see which")
            end
            local stop = verb("t", "finish")
            if stop then
                stop(1)
            end
            return
        end

        local record = verb("t", "expect")
        for i = 1, #PLAN do
            local entry = PLAN[i]
            if entry.stage then
                entry.stage()
            elseif not SKIP[entry.name] then
                local result, detail = entry.call()
                if result ~= nil and record then
                    -- The ledger's verdict column is only PASS/FAIL; the verb's
                    -- own result word (timeout, not_found, refused, covered,
                    -- no_row, not_visible, closed, unsupported) is the half that
                    -- says WHY, so it leads the detail.
                    record(entry.name, result, "[" .. tostring(result) .. "] " .. (detail or ""))
                end
            end
        end

        local stop = verb("t", "finish")
        if stop then
            stop(0)
        end
    end,
}
