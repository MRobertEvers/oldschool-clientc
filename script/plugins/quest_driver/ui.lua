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
    local result, detail = QD.cheat(cheat_text)
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
function QD.key(name)
    local result, detail = api_drive.key(name, true)
    if result ~= "ok" then
        return result, detail
    end
    return api_drive.key(name, false)
end

function QD.text(str)
    return api_drive.text(str)
end

-- t.shot polls api_drive.shot, which answers "timeout" (this pair's own
-- private "still queued" signal, never surfaced past this function) until
-- the renderer has written the file, "ok" with the path once it has, or
-- "refused" if the request itself was refused (no session dir, or the
-- renderer declined). QD.core_next_shot both numbers the capture and folds
-- it into the next ledger row.
--
-- THE UNCHANGED FRAME. api_drive.shot has a third return value, true when
-- the picture it just wrote was byte-identical to the last one this run
-- wrote and was therefore deleted again (torirs_plugin_drive_ui.c's banner).
-- The result is still "ok" -- nothing went wrong, and a test that asked for
-- a shot got a truthful answer about the screen -- but the detail is
-- "unchanged since <that shot's name>" instead of a path, so a caller that
-- prints it prints something true. QD.core_shot_unchanged is what keeps the
-- ledger honest about it: the name leaves this row's `shots` column (there
-- is no file to claim) and the row's detail gains `[frame unchanged]`.
--
-- `keep` is passed straight through: t.exec's `<name>-FAIL` capture sets it
-- so a FAIL row always keeps a picture (core.lua's record_with_shot).
function QD.shot(name, keep)
    local numbered = QD.core_next_shot(name)
    local last_result, last_detail, last_unchanged = api_drive.shot(numbered, keep)
    if last_result ~= "timeout" then
        if last_unchanged then
            QD.core_shot_unchanged(numbered)
        end
        return last_result, last_detail
    end
    local awaited, note = await({
        level = function()
            last_result, last_detail, last_unchanged = api_drive.shot(numbered, keep)
            return last_result ~= "timeout"
        end,
        note = "t.shot " .. numbered,
    }, 3)
    if awaited == "ok" then
        if last_unchanged then
            QD.core_shot_unchanged(numbered)
        end
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
    QD.ticks(4)
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

-- SEAM silent_press_npc_step (2026-09-21) -- WHERE THE COPIES OF AN NPC ARE
-- STANDING.
--
-- QD.npc.nearest answers ONE row and the pool it reads already carries that
-- row's tile (`x`, `z`, `level`, and the server `slot` behind them --
-- drive_ui_push_npc_row, torirs_plugin_drive_ui.c).  That is enough for a
-- quest file that only has to reach an npc.  It is not enough for one that
-- has to POSITION ITSELF RELATIVE TO ONE, which is what Sheep Herder is: the
-- prod pushes a sheep one tile directly away from the player
-- (diseased_sheep.rs2:121), so the tile to stand on is computed from the
-- sheep's tile and the pen's, and which of the three copies of
-- `plaguesheep_1` (m40_52.spawn:28-30) you are talking about changes with
-- every step either of you takes.  `nearest` cannot express that question and
-- a quest file has no other way to reach the pool.
--
-- So: every copy, nearest first, each row exactly as the pool holds it.
--
-- THREE RETURNS, not the usual two: `(result, summary, rows)`.  The summary
-- is a string because a row detail is a string -- a list of tables renders as
-- `1=<table> 2=<table>` through core.lua's detail_text and tells a reader
-- nothing -- and the rows are the third value so a quest file can do
-- arithmetic on them.  t.expect/t.exec take the first two and ignore the
-- third, which is the shape they already have with every other verb.
function QD.npc.tiles(sym, radius)
    local sym_result, npc_id = api_drive.symbol("npc", sym)
    if sym_result ~= "ok" then
        return "not_found", sym, nil
    end
    local result, rows = api_drive.npcs(radius or 0)
    if result ~= "ok" or type(rows) ~= "table" then
        return result, sym, nil
    end
    local found = {}
    local parts = {}
    for i = 1, #rows do
        local row = rows[i]
        if row.npc_id == npc_id or row.base_npc_id == npc_id then
            found[#found + 1] = row
            parts[#parts + 1] = "slot " .. tostring(row.slot)
                .. " (element " .. tostring(row.element_id) .. ") at "
                .. tostring(row.x) .. "," .. tostring(row.z)
                .. " L" .. tostring(row.level)
        end
    end
    if #found == 0 then
        return "no_row", string.format(
            "%s (id %d, searched %d npc(s) within %d)",
            sym, npc_id, #rows, radius or 0), nil
    end
    return "ok", string.format("%s: %d copy(s) -- %s", sym, #found,
        table.concat(parts, "; ")), found
end

-- shop ---------------------------------------------------------------------
--
-- SEAM shop_purchase_verb (2026-09-21) -- A SHOP IS A SCREEN LIKE ANY OTHER,
-- AND NOTHING IN THE DRIVER COULD REACH IT.
--
-- Shades of Mort'ton's temple leg cannot be played without one.
-- `flamtaer_temple.rs2`'s `[oploc1,_temple_wall]` raises `%temple_resources`
-- only while the backpack holds timber, five swamp paste and a limestone
-- brick (`~add_temple_resources`), timber (`timberbeam`) exists in exactly
-- one place in this content pack -- `razmire_builders_merchants.inv`, the
-- store Razmire OPENS AS THE QUEST'S OWN REWARD for killing five shades --
-- and trap 16 forbids `::give`ing a quest's own deliverable.  So the verb
-- table had no way to play the quest, and no way to press a shop at all: 104
-- verbs and not one of them opened, read or bought from one.
--
-- WHAT A PRESS ON A SHOP CELL ACTUALLY IS, measured rather than assumed
-- (build/quest_gate/shop_probe/ledger.tsv, the Lumbridge general store
-- through `::shop`):
--
--   * `shopmain:items` (interface 300 component 16) is a GRID, and each cell
--     is a dynamic child with its own component id -- `api_drive.component
--     ("shopmain:items", sub)` resolves it and `api_drive.if_click` on THAT
--     id sends IF_BUTTON<op> with (target = the grid, sub = the cell),
--     because `UIIfEventTable_ButtonTarget` maps a dynamic child back to its
--     parent and slot (src/ui/uitree_if_events.c:122).  The probe read subs
--     0..6 as seven distinct ids and op 3 on sub 1 moved the world:
--     `pot_empty 0 -> 5; coins 20000 -> 19995`.
--   * so this needs NO new C.  `ui.widget` + `ui.invoke` are the whole press
--     path, which is also the path a player's click takes
--     (app_minimenu_run_option's UI_MINIMENU_PICK_UI arm).
--   * the ladder is fixed and is NOT the quantity bar: `shop.rs2` binds
--     `[if_button2..5,shopmain:items]` to `~shop_buy_slot(1/5/10/50)`.  Op 1
--     means whatever the bar last said and op 6 is a Value check, so neither
--     is ever pressed here -- a buy verb that depended on a mode the client
--     also writes would buy a different number on its second call.
--   * the CELL'S SUB IS THE STOCK SLOT PLUS ONE.  `script_1074` builds the
--     grid on top of a cell 0 that is the selection highlight, and
--     `~shop_main_slot` subtracts the one back off `last_slot`
--     (shop.rs2's own header).  Off by one here buys the next item along.
--
-- WHY `open` IS TOLD THE SHOP'S INV SYMBOL.  The stock the player sees is a
-- server container transmitted into the grid (`inv_transmit(%shop,
-- shopmain:items)`), and the client keeps it in `InvManager` under its inv
-- id with NO record of which component it was pushed to (struct InvContainer,
-- src/inv/inv_manager.h:53 -- inv_id, slot_count, slots, and nothing else).
-- So "which container is this screen showing" is a question the client cannot
-- answer today, and the alternative to asking the caller is a number in a
-- quest file (trap 2).  The inv symbol is the same one the shop's own `.rs2`
-- hands `~openshop` (`razmirebuildingstore`, `generalshop1`), it is in
-- `all.inv.compack`, and with it the verb can name the slot, the stock and
-- the sub in its own detail instead of taking them on faith.
--
-- Recipe for a conformance row, since this seam's author does not write one:
-- the fixture stands in Lumbridge, so
-- `t.shop.open("generalshopkeeper1", 3, "generalshop1")` then
-- `t.shop.buy("pot_empty", 5)` then `t.shop.close()` is the whole verb set
-- against a shop that needs no quest state at all (`::give coins 5000`
-- first).  `::shop` opens the same screen with no npc, for a row that wants
-- to test `buy` without `open`.
--
-- (2026-09-22) `::shop` is also the whole recipe for `shop.attach`, which
-- landed below: `t.cheat("::shop")` then `t.shop.attach("generalshop1")`
-- then `t.shop.buy("pot_empty", 1)` is a shop nothing pressed open, bought
-- from -- and the same three rows with the `attach` line removed are the
-- `no_row` this seam was.

QD.shop = {}

QD.shop._interface = "shopmain"
QD.shop._grid = "shopmain:items"
-- Buy rungs, biggest first: { how many this press buys, its op number }.
QD.shop._rungs = { { 50, 5 }, { 10, 4 }, { 5, 3 }, { 1, 2 } }
-- The inv the OPEN shop is showing, remembered by shop.open for shop.buy.
QD.shop._inv_symbol = nil
QD.shop._inv_id = nil

function QD.shop._present()
    local sym_result, interface_id = api_drive.symbol("interface", QD.shop._interface)
    if sym_result ~= "ok" then
        return false
    end
    local present_result, present = api_drive.group_present(interface_id)
    return present_result == "ok" and present == true
end

-- How many of the open shop's slots hold something, as a sentence: the
-- reading that separates "the frame mounted" from "the stock arrived", which
-- are two server messages and not always the same tick.
function QD.shop._stocked(inv_id)
    local capacity_result, capacity = api_drive.inv_capacity(inv_id)
    if capacity_result ~= "ok" then
        return -1, 0
    end
    local stocked = 0
    for slot = 0, capacity - 1 do
        local slot_result, cell = api_drive.inv_slot(inv_id, slot)
        if slot_result == "ok" and cell.obj_id > 0 then
            stocked = stocked + 1
        end
    end
    return stocked, capacity
end

-- Every chat line newer than `since`, oldest first, as one string -- or nil.
--
-- A shop refuses in prose and never in a result word: `[label,buy_item]`
-- answers "You don't have enough coins.", "The shop has run out of stock."
-- and "You don't have enough inventory space." with a plain `mes` and then
-- simply returns, so a press that bought nothing and a press that was never
-- armed look identical from here unless the sentence travels with the row.
-- Not graded against a list, because the list would be shop.rs2's today and
-- content's tomorrow: whatever the server said is what the ledger carries.
-- (api_drive.messages answers newest-first -- torirs_plugin_drive_state.c.)
function QD.shop._lines_since(since)
    local result, rows = api_drive.messages()
    if result ~= "ok" or type(rows) ~= "table" then
        return nil
    end
    local said = {}
    for i = #rows, 1, -1 do
        if rows[i].serial > since and type(rows[i].text) == "string" then
            said[#said + 1] = rows[i].text
        end
    end
    if #said == 0 then
        return nil
    end
    return table.concat(said, " / ")
end

-- Where `item` sits in the OPEN shop: (ok, {slot, sub, stock}) or
-- (result, detail).  `sub` is the grid cell to press -- slot + 1.
function QD.shop._row(item)
    local obj_result, obj_id = api_drive.symbol("obj", item)
    if obj_result ~= "ok" then
        return "no_row", "shop: " .. tostring(item) .. " is not an obj symbol"
    end
    local capacity_result, capacity = api_drive.inv_capacity(QD.shop._inv_id)
    if capacity_result ~= "ok" then
        return capacity_result, "shop: the stock of " .. tostring(QD.shop._inv_symbol)
            .. " is not in this client's inv manager"
    end
    local offered = {}
    for slot = 0, capacity - 1 do
        local slot_result, cell = api_drive.inv_slot(QD.shop._inv_id, slot)
        if slot_result == "ok" and cell.obj_id == obj_id then
            return "ok", { slot = slot, sub = slot + 1, stock = cell.count }
        end
        if slot_result == "ok" and cell.obj_id > 0 and #offered < 6 then
            offered[#offered + 1] = tostring(cell.obj_id) .. "x" .. tostring(cell.count)
        end
    end
    return "not_found", string.format(
        "shop: %s is not stocked by %s (%d slot(s), first few obj ids: %s)",
        tostring(item), tostring(QD.shop._inv_symbol), capacity,
        table.concat(offered, " "))
end

-- Open a shop by pressing the npc's own shop op, and wait for the STOCK.
--
-- `op` is the npc's numbered op -- 3 is "Trade" on this pack's shopkeepers
-- ([opnpc3,generalshopkeeper1]) and Razmire's builders store is 4
-- ("Trade-Builders-Store", all.npc) -- and `shop_inv` is the inv symbol that
-- npc's script hands `~openshop`.  (ok, a sentence naming the stock) or
-- (result, detail).
function QD.shop.open(npc, op, shop_inv)
    op = op or 3
    if type(shop_inv) ~= "string" then
        return "no_row", "shop.open: name the shop's own inv symbol (the one its "
            .. ".rs2 hands ~openshop, e.g. razmirebuildingstore) -- the client "
            .. "cannot tell which container a grid is showing"
    end
    local inv_result, inv_id = api_drive.symbol("inv", shop_inv)
    if inv_result ~= "ok" then
        return "no_row", "shop.open: unknown inv symbol " .. tostring(shop_inv)
    end

    -- A shop already on screen is not this press's evidence: interface 300 is
    -- one page reused by every store, so "shopmain is mounted" after the
    -- click would be satisfied by the shop that was already up.  Same
    -- reasoning as ui.journal_open's opening close.
    if QD.shop._present() then
        local closed_result, closed_detail = QD.shop.close()
        if closed_result ~= "ok" then
            return closed_result, "shop.open: a shop was already open and would not "
                .. "close -- " .. tostring(closed_detail)
        end
    end

    -- The press is click_minimenu's, not talk_to's, and the difference is
    -- twenty ticks per shop.  talk_to settles on a dialogue page or a chat
    -- line, and `~openshop` produces NEITHER -- it opens a screen -- so every
    -- open spent talk_to's whole settle budget timing out before the wait
    -- below could even start (measured: `shop.open ... ticks=22`).  The
    -- screen is this verb's own evidence and the wait for it is right here.
    local target, sym_result, sym_name = QD.player.by_symbol("npc", npc)
    if not target then
        return sym_result, "shop.open: " .. tostring(sym_name)
    end
    local since = 0
    local serial_result, serial = api_drive.message_serial()
    if serial_result == "ok" then
        since = serial
    end
    local click_result, click = QD.drive.click_minimenu(target, op)
    if click_result ~= "ok" then
        return click_result, string.format(
            "shop.open: op %d on %s -- %s", op, tostring(npc), tostring(click))
    end

    local open_result = QD.ui.await_open(QD.shop._interface, 20)
    if open_result ~= "ok" then
        local said = QD.shop._lines_since(since)
        return "not_visible", string.format(
            "shop.open: op %d on %s pressed %q but no shopmain within 20 tick(s)%s",
            op, tostring(npc),
            type(click) == "table" and tostring(click.row_text) or tostring(click),
            said and (" -- the server said: " .. said) or "")
    end

    -- The frame and the stock are two messages.  Wait for the container, or
    -- every buy below reads an empty shop and calls the item unstocked.
    local stocked = 0
    local capacity = 0
    local landed = await({
        level = function()
            stocked, capacity = QD.shop._stocked(inv_id)
            return stocked > 0
        end,
        note = "shop.open stock " .. shop_inv,
    }, 10)
    if landed ~= "ok" then
        return "timeout", string.format(
            "shop.open: shopmain is up but %s carried no stock within 10 tick(s)",
            shop_inv)
    end

    QD.shop._inv_symbol = shop_inv
    QD.shop._inv_id = inv_id
    return "ok", string.format(
        "shop.open: op %d on %s opened shopmain -- %s holds %d stocked slot(s) of %d",
        op, tostring(npc), shop_inv, stocked, capacity)
end

-- How many cells of the OPEN grid are mounted, counting from sub 0 up, and
-- whether the count ran out of `limit` before the grid ran out of cells:
-- (cells, capped).
--
-- `shopmain:items` is rebuilt per open by `clientscript_shop_main_init` out
-- of the container the server transmitted (shop.rs2:108), so the cell count
-- is a reading OF THE SCREEN and not of the caller's claim -- the one number
-- that can contradict an `attach` argument.  The loop's own stopping
-- condition is the grid's end: api_drive.component answers not_visible for a
-- sub with no dynamic child (src/plugin/torirs_plugin_drive_ui.c:79-82,
-- UITree_FindChildBySubid), and `limit` only bounds the walk so a caller
-- that wants "is it exactly N" pays N calls and not a whole bank's worth.
--
-- MEASURED (build/quest_gate/seam6_shop_attach/ledger.tsv row 6): the
-- Lumbridge general store, `generalshop1` size=40, mounts 41 cells -- the
-- forty stock slots plus `script_1074`'s cell 0, the selection highlight
-- that `~shop_main_slot` subtracts back off (open's banner above).
function QD.shop._grid_cells(limit)
    local cells = 0
    while cells < limit do
        local cell_result = api_drive.component(QD.shop._grid, cells)
        if cell_result ~= "ok" then
            return cells, false
        end
        cells = cells + 1
    end
    return cells, true
end

-- SEAM shop_opened_by_dialogue_has_no_stock_binding (2026-09-22) -- A SHOP
-- THE PLAYER DID NOT PRESS OPEN IS STILL A SHOP.
--
-- `shop.open` is the only writer of `_inv_symbol`/`_inv_id`, and it gets
-- there by pressing the npc's numbered shop op itself.  `shop.buy` refuses
-- outright when that pair is nil.  So a shop reached ANY OTHER WAY -- a
-- dialogue row ("Can I see the building store please?"), a `::shop` cheat, a
-- loc's own op -- is unbuyable while sitting open and stocked on screen:
-- Shades of Mort'ton's second Razmire trip lost three rows to it
-- (build/quest_gate/mortton/ledger.tsv 54/55/56, all FAIL with "this shop
-- was not opened through shop.open", against
-- shots/99-razmire.recure-2-dialogue-FAIL.png, which shows Razmire Builders
-- Merchants open with its three stocked cells).
--
-- `attach` is the missing half: it binds the shop that is ALREADY up.  It is
-- `open` minus the press -- same inv-symbol argument, same stock wait, same
-- two fields, same detail shape -- and it deliberately does NOT inherit
-- open's close-what-is-already-open rule: that rule exists because a shop
-- left over from before is not evidence that open's press worked, and here
-- the shop left over from before is the entire subject.
--
-- WHY IT STILL ASKS THE CALLER FOR THE INV SYMBOL, and what that costs.  The
-- client has no component -> inv mapping any more than it has the inv ->
-- component one open's banner describes (struct InvContainer,
-- src/inv/inv_manager.h:53), so "which container is this screen showing" is
-- still a question only the caller can answer.  attach carries one hazard
-- open does not: every container this client has ever been sent stays
-- resident in its InvManager -- the backpack always, and a shop after
-- `[if_close,shopmain]`'s `inv_stoptransmit` (shop.rs2:157-159 stops the
-- updates, it does not drop the container) -- so "the named inv is resident
-- and stocked", which is all open's wait ever proves, is satisfied by a
-- container that is not on screen at all.  Measured, not feared:
-- `attach("inv")` with the Lumbridge general store up bound the PLAYER'S
-- BACKPACK as the shop, and every later `buy` would then have pressed grid
-- cells off backpack slot numbers.
--
-- So attach does one thing open does not have to: it reads the grid.  A
-- shop's grid is rebuilt per open from the container that was transmitted
-- into it, and it carries exactly `slot_count + 1` cells (the stock plus
-- script_1074's selection cell).  `_grid_cells` above is that reading, the
-- check below is the contradiction, and it REFUSES rather than warns --
-- a bound-to-the-wrong-container attach does not fail here, it fails as a
-- wrong item three buys later, which is the whole failure mode this driver
-- exists to make impossible.
--
-- (ok, a sentence naming the stock) or (result, detail).
function QD.shop.attach(shop_inv)
    if type(shop_inv) ~= "string" then
        return "no_row", "shop.attach: name the shop's own inv symbol (the one "
            .. "its .rs2 hands ~openshop, e.g. razmirebuildingstore) -- the "
            .. "client cannot tell which container a grid is showing"
    end
    local inv_result, inv_id = api_drive.symbol("inv", shop_inv)
    if inv_result ~= "ok" then
        return "no_row", "shop.attach: unknown inv symbol " .. tostring(shop_inv)
    end
    if not QD.shop._present() then
        return "not_visible", "shop.attach: no shopmain is on screen -- attach "
            .. "binds a shop something ELSE already opened (a dialogue row, "
            .. "::shop, a loc op); to open one, call shop.open(npc, op, inv)"
    end

    -- Same two-message wait as open: the frame and the stock are not the same
    -- tick, and a dialogue-opened shop is read the tick its page closes.
    local stocked = 0
    local capacity = 0
    local landed = await({
        level = function()
            stocked, capacity = QD.shop._stocked(inv_id)
            return stocked > 0
        end,
        note = "shop.attach stock " .. shop_inv,
    }, 10)
    if landed ~= "ok" then
        return "timeout", string.format(
            "shop.attach: shopmain is up but %s carried no stock within 10 "
                .. "tick(s) -- is %s the shop on screen?",
            shop_inv, shop_inv)
    end

    -- The grid on screen against the container the caller named.  Walked
    -- only as far as it takes to answer "is it exactly capacity + 1".
    local cells, capped = QD.shop._grid_cells(capacity + 2)
    if capped or cells ~= capacity + 1 then
        return "refused", string.format(
            "shop.attach: %s is resident with %d stocked slot(s) of %d, so the "
                .. "stock wait alone would have bound it -- but the grid on "
                .. "screen carries %s cell(s), not the %d a %d-slot container "
                .. "builds, so %s is not the shop being displayed",
            shop_inv, stocked, capacity,
            capped and ("more than " .. tostring(cells - 1)) or tostring(cells),
            capacity + 1, capacity, shop_inv)
    end

    QD.shop._inv_symbol = shop_inv
    QD.shop._inv_id = inv_id
    return "ok", string.format(
        "shop.attach: bound the open shopmain to %s -- %s holds %d stocked "
            .. "slot(s) of %d, and the grid on screen carries its %d cell(s)",
        shop_inv, shop_inv, stocked, capacity, cells)
end

-- One rung press: op `op` on the cell, then wait for the backpack to reach
-- `want`.  (ok) or (result, detail).
function QD.shop._press(item, sub, op, want)
    local cell_result, cell_id = api_drive.component(QD.shop._grid, sub)
    if cell_result ~= "ok" then
        return "not_visible", string.format(
            "shop.buy: cell %d of %s is not mounted", sub, QD.shop._grid)
    end
    local click_result, click_detail = api_drive.if_click(cell_id, op)
    if click_result ~= "ok" then
        return click_result, string.format(
            "shop.buy: op %d on cell %d was refused -- %s",
            op, sub, tostring(click_detail))
    end
    return await({
        event = "server_tick",
        match = function()
            local count_result, total = QD.inv.count(item)
            return count_result == "ok" and total >= want
        end,
        note = "shop.buy " .. tostring(item),
    }, 6)
end

-- Buy exactly `count` of `item` from the shop `shop.open` opened.
--
-- The ladder is composed greedily out of the four fixed rungs -- 25 swamp
-- paste is Buy-10, Buy-10, Buy-5 -- because a rung is the whole of what one
-- press means and there is no "buy N" the client can send.  `ok` is the
-- backpack holding `count` more than it did; anything short is `refused`,
-- carrying the server's own sentence when it printed one ("You don't have
-- enough coins.", "The shop has run out of stock.") and the presses that did
-- land when it did not.  (ok, "<item> A -> B for N coins [presses]").
function QD.shop.buy(item, count)
    count = count or 1
    if count < 1 then
        return "no_row", "shop.buy: " .. tostring(count) .. " is not a quantity"
    end
    if not QD.shop._present() then
        return "no_row", "shop.buy: no shop is on screen -- open one with shop.open"
    end
    if QD.shop._inv_id == nil then
        return "no_row", "shop.buy: this shop was not opened through shop.open, so "
            .. "its stock container is unknown -- call shop.open(npc, op, inv), "
            .. "or shop.attach(inv) if something else (a dialogue row, ::shop, "
            .. "a loc op) already put it on screen"
    end

    local row_result, row = QD.shop._row(item)
    if row_result ~= "ok" then
        return row_result, row
    end
    if row.stock < count then
        return "refused", string.format(
            "shop.buy: %s stocks %d %s, %d asked for",
            tostring(QD.shop._inv_symbol), row.stock, tostring(item), count)
    end

    local before_result, before = QD.inv.count(item)
    if before_result ~= "ok" then
        return before_result, "shop.buy: cannot read the backpack's " .. tostring(item)
    end
    local coins_before_result, coins_before = QD.inv.count("coins")
    local since = 0
    local serial_result, serial = api_drive.message_serial()
    if serial_result == "ok" then
        since = serial
    end

    local bought = 0
    local presses = {}
    local stalled = nil
    for i = 1, #QD.shop._rungs do
        local rung = QD.shop._rungs[i][1]
        local op = QD.shop._rungs[i][2]
        while count - bought >= rung and stalled == nil do
            local press_result, press_detail = QD.shop._press(
                item, row.sub, op, before + bought + rung)
            if press_result ~= "ok" then
                stalled = "Buy-" .. rung .. " answered " .. tostring(press_result)
                    .. (press_detail and (" -- " .. tostring(press_detail)) or "")
            else
                bought = bought + rung
                presses[#presses + 1] = "Buy-" .. rung
            end
        end
    end

    local after_result, after = QD.inv.count(item)
    local coins_after_result, coins_after = QD.inv.count("coins")
    local gained = (after_result == "ok" and before_result == "ok")
        and (after - before) or -1
    local paid = (coins_after_result == "ok" and coins_before_result == "ok")
        and (coins_before - coins_after) or -1
    local said = QD.shop._lines_since(since)
    local ladder = #presses > 0 and table.concat(presses, ", ") or "no press landed"
    local detail = string.format(
        "shop.buy: %s %s -> %s (%+d of %d asked), coins %s -> %s (%d paid) [%s]",
        tostring(item), tostring(before),
        after_result == "ok" and tostring(after) or tostring(after_result),
        gained, count,
        tostring(coins_before), tostring(coins_after), paid, ladder)
    if stalled ~= nil then
        detail = detail .. " -- " .. stalled
    end
    if said ~= nil then
        detail = detail .. " -- the server said: " .. said
    end
    if gained ~= count then
        return "refused", detail
    end
    return "ok", detail
end

-- Shut the shop.  There is no close button on interface 300 (its nineteen
-- components are the frame, the quantity bar, the grid and the scrollbar --
-- shopmain.compack), so this is the ESC path every other modal takes:
-- api_drive.close_modal, which app_cs2_flush drains into the server's own
-- ToriRSServer_WorldCloseModalEx and so runs `[if_close,shopmain]`.
function QD.shop.close()
    if not QD.shop._present() then
        QD.shop._inv_symbol = nil
        QD.shop._inv_id = nil
        return "ok", "shop.close: no shop was open"
    end
    local was = QD.shop._inv_symbol
    local close_result, close_detail = api_drive.close_modal()
    if close_result ~= "ok" then
        return close_result, "shop.close: close_modal -- " .. tostring(close_detail)
    end
    local gone = await({
        level = function()
            return not QD.shop._present()
        end,
        note = "shop.close",
    }, 10)
    if gone ~= "ok" then
        return "timeout", "shop.close: shopmain stayed on screen for 10 tick(s)"
    end
    QD.shop._inv_symbol = nil
    QD.shop._inv_id = nil
    return "ok", "shop.close: closed " .. tostring(was or "shopmain")
end
