-- quest-driver / ui: panels, tabs, npc and loc queries, keys and shots.
-- Owner: verbs-ui (docs/ARCHITECT.md).
--
-- Mount liveness is api.drive.group_present (UITree_GroupPresent): never
-- interface_parents, which is CS2-only, and never modal_host_uid, which is
-- set on open and never cleared on close.  ui.is_modal re-verifies for that
-- exact reason -- a bare non-zero test is wrong at boot and permanently wrong
-- after the session's first dialogue.
--
-- Only quest_driver/core.lua may declare a chunk-scope `local`; every helper
-- here hangs off QD instead. `api_drive` and `await` are core.lua's
-- chunk-scope locals, visible here because core.lua is concatenated first
-- (torirs_plugin_drive.c:69-77) and this whole plugin is one Lua chunk --
-- see quest_driver/state.lua's own banner (R1) for why that is spelled out:
-- `api.drive.*` and `QD.await` are both wrong here, `api_drive.*` and
-- `await` are right.
--
-- ui.invoke and ui.tab compose api_drive.if_click / api_drive.tab, both wired
-- through the test-only exports App_PluginDriveIfClick / App_PluginDriveTabSelect
-- (torirs_plugin_drive_ui.c's file banner) -- this stale note used to say they
-- were unreachable; a live run (verb conformance) shows both PASS.

-- ui.open deviates from plan 5.8's single-argument ui.open(interface): no
-- name -> debugproc table exists anywhere in this tree (every interface's
-- opening debugproc is its own hand-authored [debugproc,<name>] in
-- OSRS-Content, each with its own argument shape -- objbox takes an obj id,
-- farmkit takes none, bankpin takes a string), so there is nothing generic
-- to drive from a bare interface name. `interface` is the [iface:] symbol
-- ui.await_open needs; `cheat_text` is the exact debugproc line the test
-- author already knows for the content under test. Flagged in the BUILDER
-- report for the Final agent / design doc to settle.
function QD.ui.open(interface, cheat_text)
    local result, detail = QD.t.cheat(cheat_text)
    if result == "no_row" or result == "refused" then
        return result, detail
    end
    return QD.ui.await_open(interface)
end

function QD.ui.await_open(interface, ticks)
    local sym_result, interface_id = api_drive.symbol("interface", interface)
    if sym_result ~= "ok" then
        return "no_row", "ui.await_open: unknown interface " .. tostring(interface)
    end
    return await({
        level = function()
            local result, present = api_drive.group_present(interface_id)
            return result == "ok" and present
        end,
        note = "ui.await_open " .. interface,
    }, ticks or 20)
end

function QD.ui.await_close(interface, ticks)
    local sym_result, interface_id = api_drive.symbol("interface", interface)
    if sym_result ~= "ok" then
        return "no_row", "ui.await_close: unknown interface " .. tostring(interface)
    end
    return await({
        level = function()
            local result, present = api_drive.group_present(interface_id)
            return result == "ok" and not present
        end,
        note = "ui.await_close " .. interface,
    }, ticks or 20)
end

-- Wrapped as a plain component id, not a ToriRS_WidgetRef: constructing that
-- retained-control wrapper is verbs-read/chat.lua's business (get_text/state
-- compose on the id this returns), and inventing a second shape here would
-- give a test two different kinds of "widget" depending which file it came
-- through.
function QD.ui.widget(sym, sub)
    local result, component_id = api_drive.component(sym, sub or -1)
    return result, component_id
end

function QD.ui.invoke(widget, op)
    return api_drive.if_click(widget, op)
end

-- "the numbering the ini already documents" (plan 5.8): no name -> tab
-- number table (porcelain_frames.c's own PORCELAIN_TAB_NAME is a different
-- plugin layer's table, not this one). api_drive.tab_by_name resolves a name
-- through app->revconfig_refs' "tab" kind, the same table RevConfigRefs_Get
-- answers everywhere else in the engine: the profile's [tabs] map when a lane
-- states one, else the [role:panel_<name>] match=slot(sidebar, <n>) rows a
-- 2004 profile already carries (refs_add_tab_from_panel_role,
-- revconfig_refs.c) -- one question, one spelling, on every lane. A numeric
-- name is still passed straight through: it means "this tab number", not
-- "a name that happens to parse as one".
function QD.ui.tab(name)
    local tab_number = tonumber(name)
    if tab_number then
        return api_drive.tab(tab_number)
    end
    local sym_result, number = api_drive.tab_by_name(name)
    if sym_result ~= "ok" then
        return "no_row", string.format(
            "ui.tab: %s is in neither the [tabs] map nor a panel_%s role", name, name)
    end
    return api_drive.tab(number)
end

function QD.ui.is_modal()
    local result, live = api_drive.modal_live()
    if result ~= "ok" then
        return result, nil
    end
    return "ok", live
end

-- npc ---------------------------------------------------------------------

function QD.npc.by_name(name)
    local result, rows = api_drive.npcs(0)
    if result ~= "ok" then
        return result, nil
    end
    for i = 1, #rows do
        if rows[i].name == name then
            return "ok", rows[i]
        end
    end
    -- The detail names the SEARCH, not just the subject: "not_found" with a
    -- bare name cannot tell a test author whether the npc is absent or
    -- whether the world query returned nothing at all, and those are
    -- different bugs in different files.
    return "not_found", string.format("%s (searched %d npc(s))", name, #rows)
end

function QD.npc.by_symbol(sym)
    local sym_result, npc_id = api_drive.symbol("npc", sym)
    if sym_result ~= "ok" then
        return "not_found", sym
    end
    local result, rows = api_drive.npcs(0)
    if result ~= "ok" then
        return result, nil
    end
    for i = 1, #rows do
        if rows[i].npc_id == npc_id or rows[i].base_npc_id == npc_id then
            return "ok", rows[i]
        end
    end
    return "no_row", string.format("%s (id %d, searched %d npc(s))", sym, npc_id, #rows)
end

function QD.npc.nearest(sym, radius)
    local sym_result, npc_id = api_drive.symbol("npc", sym)
    if sym_result ~= "ok" then
        return "not_found", sym
    end
    local result, rows = api_drive.npcs(radius or 0)
    if result ~= "ok" then
        return result, nil
    end
    -- api_drive.npcs returns nearest first, so the first identity match is
    -- the nearest one.
    for i = 1, #rows do
        if rows[i].npc_id == npc_id or rows[i].base_npc_id == npc_id then
            return "ok", rows[i]
        end
    end
    return "no_row", string.format(
        "%s (id %d, searched %d npc(s) within %d)", sym, npc_id, #rows, radius or 0)
end

-- t.key / t.text / t.shot --------------------------------------------------

-- One raw press-then-release call each, mirroring content_test.c's own
-- "key " cheat (down + release, no held state a quest test would ever want).
function QD.t.key(name)
    local result, detail = api_drive.key(name, true)
    if result ~= "ok" then
        return result, detail
    end
    return api_drive.key(name, false)
end

function QD.t.text(str)
    return api_drive.text(str)
end

-- t.shot polls api_drive.shot, which answers "timeout" (this pair's own
-- private "still queued" signal, never surfaced past this function) until
-- the renderer has written the file, "ok" with the path once it has, or
-- "refused" if the request itself was refused (no session dir, or the
-- renderer declined). QD.core_next_shot both numbers the capture and folds
-- it into the next ledger row.
function QD.t.shot(name)
    local numbered = QD.core_next_shot(name)
    local last_result, last_detail = api_drive.shot(numbered)
    if last_result ~= "timeout" then
        return last_result, last_detail
    end
    local awaited, note = await({
        level = function()
            last_result, last_detail = api_drive.shot(numbered)
            return last_result ~= "timeout"
        end,
        note = "t.shot " .. numbered,
    }, 3)
    if awaited == "ok" then
        return last_result, last_detail
    end
    return awaited, note
end
