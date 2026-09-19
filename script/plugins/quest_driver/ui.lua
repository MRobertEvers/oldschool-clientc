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

-- worker 2c block: npc.await_present / npc.await_gone (QUEST_SUITE_KIT.md
-- phase 2 table) -- thin polls over npc.nearest above, generalising the
-- hand-rolled `t.await({ level = function() return t.npc.nearest(...) ==
-- "ok" end, ... })` blocks hans.lua already writes twice (hans.leaves,
-- hans.returns) into one verb each.

function QD.npc.await_present(sym, radius, ticks)
    return await({
        level = function()
            local result = QD.npc.nearest(sym, radius)
            return result == "ok"
        end,
        note = "npc.await_present " .. tostring(sym),
    }, ticks or 10)
end

function QD.npc.await_gone(sym, radius, ticks)
    return await({
        level = function()
            local result = QD.npc.nearest(sym, radius)
            return result ~= "ok"
        end,
        note = "npc.await_gone " .. tostring(sym),
    }, ticks or 10)
end
-- end worker 2c block

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

-- the quest journal ---------------------------------------------------------
--
-- Owner: verbs-ui / phase-2 worker 2e. APPENDED section -- everything above
-- this banner belongs to other hands (npc helpers are being inserted after
-- QD.npc.nearest by 2c), so nothing here edits a line above it.
--
-- How the journal actually opens, read out of the content rather than
-- guessed (OSRS-Content/.../interface_questjournal/scripts/quest_journal.rs2,
-- and tools/quest_gate/quest_inventory.md "Quest journal"):
--
--   * `~quest_journal_login` arms `if_setevents(questlist:list, 1, $count,
--     ^if_event_op2)` AT LOGIN -- the op-2 mask does not wait for the quest
--     tab to be looked at;
--   * clientscript 2633 (`questlist_draw_2633`) builds one dynamic text row
--     per quest under `questlist:list` with `cc_create($list, 4, $n, 0)` and
--     `cc_settext(<quest name>)`, where `$n` runs 1..db_listall(quest) and IS
--     the quest's `quest:id` column -- the same key `~quest_journal_open_by_id`
--     dispatches on.  So the row for a quest is found by its DISPLAY NAME,
--     which is the only name a test author has, and the sub id never has to
--     be written down anywhere;
--   * `[if_button2,questlist:list]` -> `~quest_journal_open_by_id(last_slot)`
--     -> the quest's own `~<abbr>_journal` proc -> `~quest_journal($title,
--     $text)`, which paints `questjournal:title` plus `qj1..qjN` through
--     `split_init`/`split_get` and mounts interface 119 with
--     `if_opensub(toplevel_osrs_stretch:mainmodal, questjournal, 0)`.
--
-- This drives that real click, not a debugproc: the press goes through
-- api_drive.if_click on the ROW's own runtime component id, which
-- UIIfEventTable_ButtonTarget resolves back to (questlist:list, sub) so the
-- IF_BUTTON2 the server sees carries `last_slot` = the quest id.  A
-- debugproc opener would prove the journal proc and nothing about the list,
-- the event mask, or the dispatch -- and there is no `[debugproc,journal]`
-- in this content pack anyway (checked: the only openers are questlist op 2
-- and skill_guide_v2's quest-journal button, both through the same proc).
--
-- No revconfig role and no new C reader are needed for any of this.
-- `questjournal:title`, `questjournal:qj<n>` and `questlist:list` are
-- ordinary qualified content symbols, and TORIRSSERVER_PACK_COMPONENT is
-- built by walking every interface's own `.compack`
-- (torirs_server_content.c:400-443), so they already resolve through
-- api_drive.component exactly as `questscroll:quest_title` does for
-- scroll.title.  The `[role:dialog_quest_scroll_*]` rows next door are read
-- by nothing in src/ -- they are declarative -- so mirroring them here would
-- have added config that no code path consults.  @see the 2e report.

QD.ui._journal_interface = "questjournal"
QD.ui._journal_list = "questlist:list"
-- ^questjournal_max_lines, interface_questjournal/configs/questjournal.constant.
QD.ui._journal_max_lines = 210
-- db_listall(quest) is ~190 in this cache and the sub ids are 1..count, so a
-- scan that stops on a run of misses reads the whole list without ever
-- writing a count down here. The run has to be longer than one miss: a row
-- whose text has not landed yet is a miss that the next sub is not.
QD.ui._journal_scan_ceiling = 400
QD.ui._journal_scan_miss_run = 12

-- Every journal string is markup: ^journal_todo is "<col=000080>",
-- ^journal_done is "<str>", ^journal_complete is "<col=ff0000>" and the
-- title is prefixed with ^journal_highlight. The client has no tag parser
-- (read.lua's banner, same reason), so the tags come off here. An
-- unterminated '<' is left alone, matching drive_ui_strip_tags in
-- torirs_plugin_drive_ui.c.
function QD.ui._journal_plain(text)
    if type(text) ~= "string" then
        return ""
    end
    local out = ""
    local at = 1
    while at <= #text do
        local open_at = string.find(text, "<", at, true)
        if not open_at then
            out = out .. string.sub(text, at)
            break
        end
        local close_at = string.find(text, ">", open_at, true)
        if not close_at then
            out = out .. string.sub(text, at)
            break
        end
        out = out .. string.sub(text, at, open_at - 1)
        at = close_at + 1
    end
    -- split_get hands back hard-break-separated lines; a trailing '|' of an
    -- empty continuation is noise in a compared string.
    out = string.gsub(out, "|", " ")
    out = string.gsub(out, "^%s+", "")
    out = string.gsub(out, "%s+$", "")
    return out
end

-- symbol (+ optional sub) -> its live text with the markup stripped, or nil
-- when the component is not mounted. Same shape as read.lua's private pair,
-- spelled here rather than borrowed so this section depends on nothing
-- another owner's file keeps private.
function QD.ui._journal_text(symbol, sub)
    local component_result, component_id = api_drive.component(symbol, sub or -1)
    if component_result ~= "ok" then
        return nil
    end
    local text_result, text = api_drive.widget_text(component_id)
    if text_result ~= "ok" then
        return nil
    end
    return QD.ui._journal_plain(text), component_id
end

-- Put the quest LIST on screen, which the quest tab alone does not do.
--
-- Measured, not assumed: driving `ui.tab("quests")` and reading
-- `questlist:list` answered zero rows every time, and the shot showed the
-- account-summary panel. Interface 629 `side_journal` is what tab 2 mounts
-- (player/configs/gameframe.enum: `toplevel_osrs_stretch:side2,side_journal`),
-- and 629 carries a `tab_container` the CACHE never fills -- each of its five
-- strip icons has a separate interface for a body, mounted by the SERVER
-- (interface_journal/scripts/journal.rs2's own banner says so in as many
-- words). `~journal_show` defaults to `account_summary_sidepanel`, so a fresh
-- login sits on the summary tab and `questlist` (399) is not in the tree at
-- all.
--
-- `side_journal:quest_list` is the strip icon for it: armed op 1 at login
-- (`if_setevents(side_journal:quest_list, 0, 0, ^if_event_op1)`) with
-- `[if_button,side_journal:quest_list] ~journal_show(^journal_tab_quests)`
-- behind it, which `if_opensub`s questlist into `side_journal:tab_container`.
-- Pressing it is therefore the whole of "show me the quest list", and it is
-- the same press a player makes.
--
-- The press is skipped when the rows are already built, so re-opening a
-- second quest's journal in one test does not tear the list down and rebuild
-- it (cc_deleteall) under its own feet.
-- One op-1 press on the quest-list strip icon: (ok) or (result, detail).
function QD.ui._journal_press_quest_list()
    local icon_result, icon_id = api_drive.component("side_journal:quest_list", -1)
    if icon_result ~= "ok" then
        return "not_visible", "ui.journal_open: side_journal:quest_list is not mounted"
    end
    local click_result, click_detail = api_drive.if_click(icon_id, 1)
    if click_result ~= "ok" then
        return click_result, "ui.journal_open: op 1 on side_journal:quest_list was "
            .. "refused -- " .. tostring(click_detail)
    end
    return "ok", nil
end

function QD.ui._journal_open_list()
    local tab_result, tab_detail = QD.ui.tab("quests")
    if tab_result ~= "ok" then
        return tab_result, "ui.journal_open: could not select the quest tab -- "
            .. tostring(tab_detail)
    end

    local mounted_result = await({
        level = function()
            local result = api_drive.component("side_journal:quest_list", -1)
            return result == "ok"
        end,
        note = "ui.journal_open side_journal",
    }, 10)
    if mounted_result ~= "ok" then
        return "not_visible",
            "ui.journal_open: the quest tab did not mount side_journal within 10 ticks"
    end

    -- Skipped when the rows are already built, so re-opening a second quest's
    -- journal in one test does not tear the list down and rebuild it
    -- (cc_deleteall) under its own feet.
    if api_drive.component(QD.ui._journal_list, 1) == "ok" then
        return "ok", "questlist is already on screen"
    end

    -- THE TAB SWITCH HAS TO LAND BEFORE THE PRESS, and a press that arrives
    -- too early is not refused -- it is simply not acted on, and nothing
    -- anywhere says so. Measured 2026-09-19, three runs: `ui.tab("quests")`
    -- followed immediately by op 1 on side_journal:quest_list built NO rows
    -- in 60 ticks, while the very same call made a second time built them in
    -- 0, and a first press taken 8 ticks after the tab switch built them in
    -- 8. The component resolving (the await above) only says the client has
    -- the widget, not that the tab change has been acted on.
    --
    -- So: settle, press, wait -- and if the rows still are not there, press
    -- ONCE more rather than spend the whole budget on a press that was never
    -- going to land. Two presses are safe: `~journal_show` re-opens the same
    -- sub, and the second press is what the old code was accidentally
    -- relying on.
    QD.t.ticks(4)
    local pressed, press_detail = QD.ui._journal_press_quest_list()
    if pressed ~= "ok" then
        return pressed, press_detail
    end

    local function rows_await(ticks)
        return await({
            level = function()
                local result = api_drive.component(QD.ui._journal_list, 1)
                return result == "ok"
            end,
            note = "ui.journal_open rows",
        }, ticks)
    end

    if rows_await(20) == "ok" then
        return "ok", "questlist is on screen"
    end

    pressed, press_detail = QD.ui._journal_press_quest_list()
    if pressed ~= "ok" then
        return pressed, press_detail
    end
    if rows_await(20) ~= "ok" then
        return "not_visible",
            "ui.journal_open: questlist:list built no rows within 40 ticks and two "
            .. "presses of the quest-list tab"
    end
    return "ok", "questlist is on screen (the second press is what built it)"
end

-- The quest-list row whose text names `display_name`: (ok, component_id,
-- sub) or (result, detail). An exact match wins; the containment fallback is
-- for the two decorations clientscript 2633 adds to a row's own text --
-- "MQ: <name>" for a miniquest and "<name> (Beta)" -- neither of which the
-- quest's `quest:displayname` carries.
function QD.ui._journal_find_row(display_name)
    local rows = {}
    local misses = 0
    local sub = 1
    while sub <= QD.ui._journal_scan_ceiling do
        local text, component_id = QD.ui._journal_text(QD.ui._journal_list, sub)
        if text == nil then
            misses = misses + 1
            if #rows > 0 and misses >= QD.ui._journal_scan_miss_run then
                break
            end
        else
            misses = 0
            rows[#rows + 1] = { text = text, component_id = component_id, sub = sub }
        end
        sub = sub + 1
    end

    if #rows == 0 then
        return "not_visible", string.format(
            "ui.journal_open: questlist:list has no rows -- the quest-list tab "
            .. "is not built (looking for %q)", tostring(display_name))
    end

    -- Exact over the WHOLE list before any containment, in two passes rather
    -- than one: "Dragon Slayer" is a substring of "Dragon Slayer II", and the
    -- sequel sorts first in this list, so a single pass that accepted a
    -- containment hit as it walked would open the wrong quest's journal and
    -- pass every assertion that only looks at the title it was given.
    for i = 1, #rows do
        if rows[i].text == display_name then
            return "ok", rows[i].component_id, rows[i].sub
        end
    end
    -- Only then the two decorations clientscript 2633 adds to a row's own
    -- text -- "MQ: <name>" for a miniquest and "<name> (Beta)" -- neither of
    -- which the quest's `quest:displayname` carries. Ambiguity here is
    -- refused, not guessed at.
    local hit = nil
    local hits = 0
    for i = 1, #rows do
        if string.find(rows[i].text, display_name, 1, true) then
            hits = hits + 1
            hit = rows[i]
        end
    end
    if hits == 1 then
        return "ok", hit.component_id, hit.sub
    end
    if hits > 1 then
        return "refused", string.format(
            "ui.journal_open: %q names %d questlist rows -- spell the row's "
            .. "own text", tostring(display_name), hits)
    end

    local sample = {}
    for i = 1, #rows do
        if #sample < 3 then
            sample[#sample + 1] = rows[i].text
        end
    end
    return "not_found", string.format(
        "ui.journal_open: no questlist row named %q (scanned %d rows, e.g. %s)",
        tostring(display_name), #rows, table.concat(sample, " / "))
end

-- Read the journal that is mounted RIGHT NOW. No click, no wait: this is the
-- reader `journal_open()` with no argument is defined to be, and the one
-- `journal_open(name)` finishes with once its own click has landed.
--
-- `first_line` is the FIRST body line, `questjournal:qj1` -- which for every
-- quest in this pack is the opening sentence of the progress text, NOT the
-- completion banner: `^journal_complete .. "QUEST COMPLETE!"` is appended
-- LAST by every one of the ~190 `~<abbr>_journal` procs (checked across the
-- tree). `lines` and `complete` are here so a caller can assert the banner
-- without this verb having to lie about which line it is on. @see the 2e
-- report's open issue for quest.expect_complete.
function QD.ui.journal_read()
    local interface_result, interface_id = api_drive.symbol("interface", QD.ui._journal_interface)
    if interface_result ~= "ok" then
        return "no_row", "ui.journal_read: unknown interface " .. QD.ui._journal_interface
    end
    local present_result, present = api_drive.group_present(interface_id)
    if present_result ~= "ok" or not present then
        return "not_visible", "ui.journal_read: interface questjournal is not mounted"
    end

    local title = QD.ui._journal_text("questjournal:title")
    if title == nil then
        return "not_visible", "ui.journal_read: questjournal:title is not mounted"
    end

    -- All 210 qj components exist on the mounted interface whether or not
    -- this quest's text reached them (`~quest_journal` only writes 1..$n and
    -- blanks $n+1..%qj_lines), so the body ends at the LAST NON-EMPTY row,
    -- not at the first empty one: a journal's own "||" paragraph break is a
    -- genuinely blank line in the middle of the text, and stopping on it
    -- would silently cut every journal short at its first paragraph.
    local lines = {}
    local complete = false
    local last_written = 0
    local line = 1
    while line <= QD.ui._journal_max_lines do
        local text = QD.ui._journal_text("questjournal:qj" .. line)
        if text == nil then
            break
        end
        lines[line] = text
        if text ~= "" then
            last_written = line
            if string.find(text, "QUEST COMPLETE", 1, true) then
                complete = true
            end
        end
        line = line + 1
    end
    while #lines > last_written do
        lines[#lines] = nil
    end

    -- A mounted journal whose title never received its IF_SETTEXT is the one
    -- state that reads like a working one and is not: say so rather than
    -- handing back an `ok` with two empty strings (the hollow rule).
    if title == "" and #lines == 0 then
        return "not_visible",
            "ui.journal_read: questjournal is mounted but carries no text yet"
    end

    return "ok", {
        title = title,
        first_line = lines[1] or "",
        lines = lines,
        line_count = #lines,
        complete = complete,
    }
end

-- (ok, {title, first_line, lines, line_count, complete}) or (result, detail).
--
-- With `display_name` (what quest.bind's `display=` carries): select the
-- quest tab, find that quest's row in the list by its own rendered name, and
-- press op 2 on it -- the real "Read journal:" click. With no argument: read
-- whatever journal is already open.
function QD.ui.journal_open(display_name)
    if display_name == nil then
        return QD.ui.journal_read()
    end

    -- Close a journal that is already open BEFORE pressing anything.
    --
    -- Measured: with Cook's Assistant's journal still up, journal_open("Dragon
    -- Slayer I") answered `ok` and handed back Cook's Assistant -- the paint
    -- wait below was already satisfied by the PREVIOUS quest's page, so a
    -- click that did not repaint (or had not repainted yet) read as a
    -- success. Interface 119 is one shared page repainted per quest, so
    -- "a journal is mounted" can never be this verb's evidence that ITS
    -- click landed. Starting from a closed page makes the wait mean what it
    -- says, and a test that opens two journals in a row no longer has to
    -- remember to close the first.
    local open_already, open_already_id = api_drive.symbol("interface", QD.ui._journal_interface)
    if open_already == "ok" then
        local present_result, present = api_drive.group_present(open_already_id)
        if present_result == "ok" and present then
            local closed_result, closed_detail = QD.ui.journal_close()
            if closed_result ~= "ok" then
                return closed_result,
                    "ui.journal_open: a journal was already open and would not close -- "
                    .. tostring(closed_detail)
            end
        end
    end

    local list_result, list_detail = QD.ui._journal_open_list()
    if list_result ~= "ok" then
        return list_result, list_detail
    end

    local find_result, row_component_id, row_sub = QD.ui._journal_find_row(display_name)
    if find_result ~= "ok" then
        return find_result, row_component_id
    end

    -- op 2 is "Read journal:" (clientscript 2633's own cc_setop(2, ...)); the
    -- server's mask for it was armed at login by ~quest_journal_login.
    local click_result, click_detail = api_drive.if_click(row_component_id, 2)
    if click_result ~= "ok" then
        return click_result, string.format(
            "ui.journal_open: op 2 on the %q row (sub %d) was refused -- %s",
            display_name, row_sub, tostring(click_detail))
    end

    -- The mount and the fifty IF_SETTEXTs are one server tick's work but they
    -- reach the client on separate messages, so the wait is on the TEXT, not
    -- on the group: a journal read the frame its interface appeared answers
    -- an empty title, which is the hollow `ok` this whole file exists to not
    -- produce.
    local painted_result = await({
        level = function()
            local result = QD.ui.journal_read()
            return result == "ok"
        end,
        note = "ui.journal_open " .. display_name,
    }, 20)
    if painted_result ~= "ok" then
        local read_result, read_detail = QD.ui.journal_read()
        return "timeout", string.format(
            "ui.journal_open: %q row %d clicked, but no painted journal within "
            .. "20 ticks (last read: %s / %s)",
            display_name, row_sub, tostring(read_result),
            type(read_detail) == "string" and read_detail or "table")
    end
    return QD.ui.journal_read()
end

-- (ok) once interface 119 is gone. `questjournal:close` is a static op-1
-- button re-armed on every mount by `~quest_journal_arm`
-- (if_setevents(questjournal:close, 0, 0, ^if_event_op1)) and its handler is
-- `[if_button1,questjournal:close] if_closesub(toplevel_osrs_stretch:mainmodal)`,
-- so this is the same press a player makes -- not an if_close cheat, which
-- would close every slot (and has, elsewhere in this client).
function QD.ui.journal_close()
    local close_result, close_id = api_drive.component("questjournal:close", -1)
    if close_result ~= "ok" then
        return "not_visible", "ui.journal_close: questjournal:close is not mounted"
    end
    local click_result, click_detail = api_drive.if_click(close_id, 1)
    if click_result ~= "ok" then
        return click_result, "ui.journal_close: op 1 on questjournal:close was "
            .. "refused -- " .. tostring(click_detail)
    end
    local result, detail = QD.ui.await_close(QD.ui._journal_interface, 10)
    if result ~= "ok" then
        return result, "ui.journal_close: interface questjournal stayed mounted -- "
            .. tostring(detail)
    end
    return "ok", "questjournal closed"
end
