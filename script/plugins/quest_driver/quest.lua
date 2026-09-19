-- quest-driver / quest: the one-call quest binding a generated test header
-- uses to name its own progress varp, its stage constants, its scroll title
-- and its point award, and to check all four at completion in one call.
-- Owner: 2a (docs/QUEST_SUITE_KIT.md phase 2). New for this pass -- there is
-- no ARCHITECT.md ownership row for "quest" yet.
--
-- Only quest_driver/core.lua may declare a chunk-scope `local` -- every
-- helper here hangs off QD instead, the same way state.lua/chat.lua/ui.lua
-- already do (see state.lua's own banner, R1, for why `api_drive`/`await`
-- and NOT `api.drive.*`/`QD.await` are the right names to read here: this
-- whole plugin is ONE Lua chunk, core.lua is concatenated first, and this
-- file -- last in DRIVE_SCRIPT_PARTS, right before the entry file -- can
-- also see every other part's QD.* verbs (QD.var.*, QD.scroll.*, QD.*),
-- because a quest RUN (which is when any of this actually executes) only
-- starts after the whole chunk has loaded once and finished building QD.
--
-- quest.bind{ varp=, constants=, row=, display=, points= }
--   varp       content symbol name of the quest's progress varp (a plain
--              varp, e.g. "cookquest" -- resolved through QD.var.varp/
--              QD.var.server, both varp-or-varbit-transparent already).
--   constants  { name = value, ... } -- the quest's own named stage values,
--              e.g. { not_started = 0, started = 1, complete = 2 }. This
--              table's VALUES are plain integers, not content symbols: there
--              is no "constant" kind in DriveSymbolKind
--              (src/plugin/torirs_plugin_drive.h) for a `^name` to resolve
--              through, so a generator (new_quest.py, phase 3) that wants to
--              hand these in by name has to read the quest's own
--              `*.constant` config file itself and inline the numbers here,
--              same as _conformance.lua's own VARP_VALUE literal already
--              does for a value a fixture pins. constants.complete IS
--              REQUIRED: quest.expect_complete's first row reads it.
--   row        the quest list row's content symbol, forwarded to
--              QD.ui.journal_open() (verbs-ui/2e) so it knows which journal
--              to open -- read from QD.quest._bound.row; 2e's file, not this
--              one, owns the click.
--   display    the quest's display name, matched against the reward scroll's
--              title (QD.scroll.title) and used to FIND the quest's row in
--              the quest list (QD.ui.journal_open takes that same display
--              name). It is NOT compared against the journal's own title --
--              see journal_title.
--   journal_title
--              optional. The title the quest's own `~<abbr>_journal` proc
--              hardcodes, WHEN it differs from the list's display name:
--              journal_open("Dragon Slayer I") comes back titled "Dragon
--              Slayer", because the row is the cache's `quest:displayname`
--              while dragon_journal.rs2 passes a literal of its own
--              (measured, 2026-09-19). Given, quest.journal requires the
--              title to equal it exactly; omitted, it requires only that the
--              journal carried a title at all and prints the one it found.
--   points     quest points this quest is worth. quest.expect_complete reads
--              %qp (content symbol "qp", a plain varp: OSRS-Content
--              .../quests/configs/questpoints.varp) at BIND time and again
--              at expect_complete time, and checks the delta equals this.
--
-- quest.bind never touches the world (no cheat, no read-and-fail): a test
-- that binds against a typo'd varp name only finds out at the first
-- quest.stage/expect_stage/expect_complete call, exactly like every other
-- verb's own "no_row" for an unknown symbol.
function QD.quest.bind(spec)
    if type(spec) ~= "table" then
        return "refused", "quest.bind: expected a table"
    end
    if type(spec.varp) ~= "string" or spec.varp == "" then
        return "refused", "quest.bind: varp is required"
    end
    local constants = spec.constants
    if type(constants) ~= "table" then
        constants = {}
    end
    local qp_result, qp_before = QD.var.varp("qp")
    QD.quest._bound = {
        varp = spec.varp,
        constants = constants,
        row = spec.row,
        display = spec.display,
        journal_title = spec.journal_title,
        points = spec.points,
        qp_before = (qp_result == "ok") and qp_before or nil,
        qp_before_result = qp_result,
    }
    return "ok", nil
end

-- name_or_value -> a numeric stage value, or nil when neither a number nor a
-- name `bound.constants` carries. A bare number is passed straight through
-- (a test that already has the value, e.g. from another quest's constant,
-- is not made to invent a name for it) -- the ARCHITECT.md naming rule this
-- would otherwise run into (docs/ARCHITECT.md S2, "no numeric id anywhere")
-- is about INTERFACE/COMPONENT ids and op strings, not a quest's own stage
-- number, which _conformance.lua's VARP_VALUE already treats the same way.
function QD.quest._stage_value(bound, name_or_value)
    if type(name_or_value) == "number" then
        return name_or_value
    end
    if type(name_or_value) == "string" then
        return bound.constants[name_or_value]
    end
    return nil
end

function QD.quest.stage()
    local bound = QD.quest._bound
    if not bound then
        return "refused", "quest.stage: quest.bind was not called"
    end
    return QD.var.varp(bound.varp)
end

-- refused on a client/server mismatch, naming which side disagreed -- the
-- same shape QD.var.expect already uses for a plain varp/varbit.
function QD.quest.expect_stage(name_or_value)
    local bound = QD.quest._bound
    if not bound then
        return "refused", "quest.expect_stage: quest.bind was not called"
    end
    local value = QD.quest._stage_value(bound, name_or_value)
    if value == nil then
        return "refused", "quest.expect_stage: unknown stage " .. tostring(name_or_value)
    end

    local client_result, client_value = QD.var.varp(bound.varp)
    if client_result ~= "ok" then
        return client_result, bound.varp
    end
    if client_value ~= value then
        return "refused", bound.varp .. ": client=" .. tostring(client_value) .. " expected=" .. tostring(value)
    end

    local server_result, server_value = QD.var.server(bound.varp)
    if server_result ~= "ok" then
        return server_result, bound.varp
    end
    if server_value ~= value then
        return "refused", bound.varp .. ": server=" .. tostring(server_value) .. " expected=" .. tostring(value)
    end

    return "ok", value
end

-- A short, safe-for-any-type description for a ledger detail column (which
-- api_drive.ledger reads as a string, luaL_optstring-style -- a raw table
-- would raise, not print). Not _conformance.lua's `describe`: that one is
-- verbs-conformance's own private helper, in a different file, and this file
-- has no access to it (only core.lua's OWN chunk-locals -- api_drive, await,
-- ok, fail, flush -- cross into the later parts; a `local` inside
-- test/quests/_conformance.lua's `run` closure is a different function
-- entirely and never in scope here).
function QD.quest._describe(value)
    if value == nil then
        return "nil"
    end
    local kind = type(value)
    if kind == "string" or kind == "number" or kind == "boolean" then
        return tostring(value)
    end
    if kind == "table" then
        local parts = {}
        for key, entry in pairs(value) do
            if type(entry) == "table" then
                entry = "<table>"
            end
            parts[#parts + 1] = tostring(key) .. "=" .. tostring(entry)
        end
        if #parts == 0 then
            return "{}"
        end
        return "{" .. table.concat(parts, " ") .. "}"
    end
    return "<" .. kind .. ">"
end

-- Four ledger rows, written directly (QD.step -- these are ALREADY graded
-- results, not verbs to wrap): quest.varp_complete, quest.scroll_title,
-- quest.points, quest.journal. Never calls ::complete -- every value it
-- reads is whatever the quest's own playthrough already put there.
function QD.quest.expect_complete()
    local bound = QD.quest._bound
    if not bound then
        QD.step("quest.varp_complete", "FAIL", "quest.expect_complete: quest.bind was not called")
        QD.step("quest.scroll_title", "FAIL", "quest.expect_complete: quest.bind was not called")
        QD.step("quest.points", "FAIL", "quest.expect_complete: quest.bind was not called")
        QD.step("quest.journal", "FAIL", "quest.expect_complete: quest.bind was not called")
        return "refused", "quest.bind was not called"
    end

    local all_pass = true

    -- ---------------------------------------------------- quest.varp_complete
    local complete_value = bound.constants.complete
    if complete_value == nil then
        all_pass = false
        QD.step("quest.varp_complete", "FAIL",
            "quest.bind: constants.complete was not provided")
    else
        local client_result, client_value = QD.var.varp(bound.varp)
        local server_result, server_value = QD.var.server(bound.varp)
        local pass = client_result == "ok" and server_result == "ok"
            and client_value == complete_value and server_value == complete_value
        all_pass = all_pass and pass
        QD.step("quest.varp_complete", pass and "PASS" or "FAIL",
            bound.varp .. ": client=" .. tostring(client_value) .. "(" .. tostring(client_result) .. ")"
                .. " server=" .. tostring(server_value) .. "(" .. tostring(server_result) .. ")"
                .. " complete=" .. tostring(complete_value))
    end

    -- ----------------------------------------------------- quest.scroll_title
    --
    -- CONTAINS the display name, never equals it. `questscroll:quest_title`
    -- is a whole sentence built by one proc for every quest in the tree --
    -- `if_settext(questscroll:quest_title, append(append("You have completed ",
    -- $name), "!"))`, OSRS-Content/.../quests/scripts/questscroll.rs2:73 --
    -- so on Cook's Assistant the component reads "You have completed Cook's
    -- Assistant!". An equality check here failed that (measured 2026-09-19,
    -- the conformance harness's own quest.scroll_title row) while the scroll
    -- on screen was exactly the right one. The quest's name inside the
    -- sentence is what only the right scroll can carry, and that is what is
    -- asserted; a plain find, never a pattern, because a display name carries
    -- apostrophes and brackets that a Lua pattern would read as syntax.
    local title_result, title_detail = QD.scroll.title()
    local title_name = type(title_detail) == "table" and title_detail.name or nil
    local title_pass = title_result == "ok"
        and type(title_name) == "string"
        and type(bound.display) == "string"
        and string.find(title_name, bound.display, 1, true) ~= nil
    all_pass = all_pass and title_pass
    QD.step("quest.scroll_title", title_pass and "PASS" or "FAIL",
        "expected a title containing " .. tostring(bound.display)
            .. " got=" .. tostring(title_name)
            .. " (" .. tostring(title_result) .. ") " .. QD.quest._describe(title_detail))

    -- ----------------------------------------------------------- quest.points
    local qp_result, qp_after = QD.var.varp("qp")
    local points_pass = false
    local points_detail
    if bound.qp_before == nil then
        points_detail = "no qp reading at bind time (" .. tostring(bound.qp_before_result) .. ")"
    elseif qp_result ~= "ok" then
        points_detail = "qp read -> " .. tostring(qp_result) .. " " .. tostring(qp_after)
    else
        local delta = qp_after - bound.qp_before
        points_pass = delta == bound.points
        points_detail = "qp " .. tostring(bound.qp_before) .. " -> " .. tostring(qp_after)
            .. " delta=" .. tostring(delta) .. " expected=" .. tostring(bound.points)
    end
    all_pass = all_pass and points_pass
    QD.step("quest.points", points_pass and "PASS" or "FAIL", points_detail)

    -- ---------------------------------------------------------- quest.journal
    --
    -- THE SCROLL IS CLOSED FIRST. A quest that has just been completed has
    -- its reward scroll on screen -- that is what the row above just read --
    -- and the scroll is a MODAL: the quest-list tab press ui.journal_open
    -- starts with is arguing with a script that owns the screen while it is
    -- up. So the scroll the title row needed is dismissed here, between the
    -- two rows that need opposite things, and the scroll.close answer is
    -- folded into the journal row's own detail rather than being silently
    -- dropped. A quest whose completion shows no scroll answers not_visible
    -- here and loses nothing.
    --
    -- ui.journal_open/close are verbs-ui/2e's, appended to ui.lua as its own
    -- section (docs/QUEST_SUITE_KIT.md phase 2). If that section is ever not
    -- there, this row says so with the fixed result word "unsupported"
    -- rather than being silently left out -- BLOCKED, not FAIL, because "the
    -- driver kit does not have this yet" is a different claim than "the
    -- quest is broken", and phase 1's ledger writer counts BLOCKED in its
    -- own SUMMARY bucket.
    --
    -- WHAT IS ASSERTED, and what the first draft got wrong. The spec asked
    -- for "first line contains QUEST COMPLETE". It never can: every one of
    -- the ~190 `~<abbr>_journal` procs appends `^journal_complete ..
    -- "QUEST COMPLETE!"` LAST, so on a completed Cook's Assistant the banner
    -- is line 8 of 8 and qj1 is "It was the Duke of Lumbridge's birthday..."
    -- (measured 2026-09-19). ui.journal_read answers a `complete` flag and
    -- the whole `lines` array for exactly this reason, and that flag is what
    -- this row reads. The title is checked against bound.journal_title when
    -- the bind supplied one, and otherwise only required to be non-empty --
    -- the journal's title is a literal inside the quest's own proc and is
    -- not always the list's display name.
    local scroll_close_result, scroll_close_detail
    if type(QD.scroll) == "table" and type(QD.scroll.close) == "function" then
        scroll_close_result, scroll_close_detail = QD.scroll.close()
    else
        scroll_close_result = "unsupported"
    end

    if type(QD.ui) ~= "table" or type(QD.ui.journal_open) ~= "function" then
        QD.step("quest.journal", "BLOCKED", "unsupported -- ui.journal_open not landed")
    else
        local open_result, open_detail = QD.ui.journal_open(bound.display)
        if open_result ~= "ok" then
            all_pass = false
            QD.step("quest.journal", "FAIL",
                "journal_open(" .. tostring(bound.display) .. ") -> " .. tostring(open_result)
                    .. " " .. QD.quest._describe(open_detail)
                    .. " [scroll.close=" .. tostring(scroll_close_result) .. "]")
        else
            local title = type(open_detail) == "table" and open_detail.title or nil
            local complete_ok = type(open_detail) == "table" and open_detail.complete == true
            local title_ok
            if bound.journal_title ~= nil then
                title_ok = title == bound.journal_title
            else
                title_ok = type(title) == "string" and title ~= ""
            end

            local close_result, close_detail
            if type(QD.ui.journal_close) == "function" then
                close_result, close_detail = QD.ui.journal_close()
            else
                close_result, close_detail = "unsupported", "ui.journal_close not landed"
            end

            local journal_pass = title_ok and complete_ok and close_result == "ok"
            all_pass = all_pass and journal_pass
            QD.step("quest.journal", journal_pass and "PASS" or "FAIL",
                "title=" .. tostring(title)
                    .. " (expected " .. tostring(bound.journal_title or "any non-empty title") .. ")"
                    .. " complete=" .. tostring(complete_ok)
                    .. " lines=" .. tostring(type(open_detail) == "table" and open_detail.line_count or "?")
                    .. " scroll.close=" .. tostring(scroll_close_result)
                    .. " journal.close=" .. tostring(close_result) .. " " .. QD.quest._describe(close_detail))
        end
    end

    if all_pass then
        return "ok", "quest.varp_complete/scroll_title/points/journal all PASS"
    end
    return "refused", "one or more of quest.varp_complete/scroll_title/points/journal did not pass -- see those rows"
end
