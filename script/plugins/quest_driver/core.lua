-- quest-driver / core: the shared namespace, the await wrapper, the step
-- recorder and the ledger.  Owner: core-scheduler (docs/ARCHITECT.md).
--
-- THIS FILE IS FIRST in the concatenation and it is the only one that may
-- declare the shared locals.  Every other part adds to QD and declares
-- nothing at chunk scope, because a chunk-scope local is a register in the
-- main function and Lua gives that function 200 of them for all eight files.
--
-- No part but quest_driver.lua may have a top-level `return`: a top-level
-- return ends the chunk, and the seven files after it would never run.

---@type table
local QD = {
    chat = {},      -- verbs-chat + verbs-read
    scroll = {},    -- verbs-read
    levelup = {},   -- verbs-read
    player = {},    -- verbs-pointer
    var = {},       -- core-state
    inv = {},       -- core-state
    msg = {},       -- core-state
    skill = {},     -- core-state: read/snapshot/expect_gain
    ui = {},        -- verbs-ui
    npc = {},       -- verbs-ui
    world = {},     -- verbs-pointer (composed on verbs-ui's readers)
    drive = {},     -- verbs-pointer: the raw pointer verbs
    quest = {},     -- quest.lua (docs/QUEST_SUITE_KIT.md phase 2, owner 2a):
                     -- bind/stage/expect_stage/expect_complete
    session = {},   -- session.lua (seam 18 D): logout/login/relog through
                     -- the client's own logout button and title screen
    prayer = {},    -- prayer.lua (raid seam 1): set/read a prayer by click
    raid = {},      -- raid.lua (raid seam 1): enter a raid room, read the raid
    wave = {},      -- waves.lua (waves seam pass 2): enter a wave minigame
                     -- at a wave, read its state (docs/WAVES_ORCHESTRATOR.md
                     -- section 5)
    ticklog = {},   -- ticklog.lua (raid seam 1): the server's per-tick event log
    party = {},     -- raid.lua (raid seam17): a party run's role, barrier and
                     -- the ToB lobby verbs (form, apply, accept, ready, follow_in)
}

-- The one global this chunk exports. QD itself stays `local` -- a register,
-- not a table lookup, for the seven files that reference it hundreds of
-- times -- but torirs_plugin_drive.c's scheduler has no `require` and no
-- registry ref into a local, so it needs exactly one way in: the coroutine
-- that runs a quest test is resumed with `run(t)`, and `t` IS this table
-- (drive_scheduler_start reads QD_ROOT with lua_getglobal before the first
-- resume). Safe as an ordinary global: each Lua plugin owns a private
-- lua_State (torirs_plugin_lua.c: one lua_newstate per script), so no other
-- plugin's globals ever see this one.
QD_ROOT = QD

-- Every verb answers (result, detail). These two are the only places a
-- result string is compared, so a typo is one failure and not fifty.
local function ok(result) return result == "ok" end
local function fail(result, detail)
    return result ~= "ok", detail or result
end

-- Bound once, from quest_driver.lua's on_start(api): the coroutine that runs
-- a quest test is resumed from C with no `api` argument of its own -- it
-- only ever receives `t` (QD_ROOT above) -- so the one function value every
-- part needs, api.drive, is captured here, where an `api` table is actually
-- in scope, and every closure below (and in every other part: they all run
-- inside the SAME chunk) shares this one upvalue.
local api_drive
-- api.core, for the one reading api.drive does not carry: which SCREEN the
-- client is on (api.core.screen(), enum AppScreen -- 10 title, 20
-- connecting, 30 game). session.lua's logout/login settle on it; every
-- other verb assumes the gameframe and never needs it.
local api_core
function QD.core_bind(api)
    api_drive = api.drive
    api_core = api.core
end

-- ------------------------------------------------------------ terminal finish
--
-- t.finish(code) and t.blocked(reason) END THE RUN. `finished` is how every
-- other function in this chunk knows that, and `park` is what makes it true
-- of the Lua script and not only of the ledger file.
--
-- The problem it solves, measured in the 2026-09-19 Haiku pilot: t.finish
-- wrote SUMMARY and returned, so a quest file that called it (or t.blocked,
-- which calls it) and then carried on -- a missing `return`, a `t.blocked`
-- in the middle of a script that keeps walking, a generated tail below a
-- stub -- kept driving the client and kept appending rows BELOW its own
-- SUMMARY line. Two pilot authors reported "blocked" for a file that ran on
-- to expect_complete and left 8-21 FAIL rows behind it. "Write SUMMARY" was
-- never the same thing as "stop", and the only convergence point C has is
-- main.c's end-of-frame check of PluginDrive_Finished -- which does stop the
-- process, but not until this frame's Lua has finished running.
--
-- So the script stops itself, by parking: an await whose level predicate is
-- never true, re-armed forever, on a deadline no run reaches (1,000,000
-- server ticks). It yields, the frame ends, main.c sees the finish flag and
-- returns the exit code. Nothing after the parking call ever executes, which
-- is what "ends the run" has to mean in a sandbox with no `error` a test may
-- raise, no `os.exit`, and no `coroutine` to close.
--
-- `park` calls api_drive.await DIRECTLY rather than the `await` wrapper
-- below, which would park again and recurse.
local finished = false

local function park(reason)
    while true do
        api_drive.await({ level = function() return false end, note = reason }, 1000000)
    end
end

-- The await primitive. `descriptor` is { event=, match=, level=, note= } (at
-- least one of match/level) and the deadline is in SERVER TICKS. EDGE +
-- LEVEL and the deadline=0 "resolve now, never yield" case both live in
-- api.drive.await itself (torirs_plugin_drive.c) -- this wrapper only exists
-- so every other part calls a chunk-local `await`, not `api_drive.await`.
--
-- It is also the one seam EVERY verb in all eight files passes through when
-- it waits for the world, so the finish check sits here: the first thing a
-- post-finish script tries to wait for is the last thing it does.
-- ------------------------------------------------ progress on stderr (seam31)
--
-- A RUN THAT STOPS MID-WAIT MUST SAY WHERE (seam31 run_never_ends_silently).
-- Legends' leg 7 stopped three runs in a row inside the Nezikchened kill wait
-- with no SUMMARY, exit 0 and nothing in client.log: the virtual clock
-- (TORIRS_MAX_FRAMES) ran out, main.c returned 0, and the last thing on disk
-- was the row BEFORE the wait.  A ledger row is written when a verb returns,
-- so a verb that never returns leaves no row; these lines are what run.py's
-- unfinished-run row (tools/quest_gate/run.py: finish_unfinished_ledger) reads
-- back to name the row that was open, the tick and the last progress:
--
--   QUEST row-begin <name> tick=<T>         t.exec, before the verb runs
--   QUEST progress <text> tick=<T>          a long wait, every
--                                            QD.PROGRESS_EVERY_TICKS ticks
--
-- One line per row and one per 25 ticks of a long wait: cheap next to the
-- shot-aim lines client.log already carries.
QD.PROGRESS_EVERY_TICKS = 25
-- An await whose deadline is shorter than this cannot stall long enough to
-- matter and says nothing.
QD.PROGRESS_MIN_DEADLINE = 30

function QD.core_progress(text)
    api_drive.report(string.format("progress %s tick=%d", tostring(text), api_drive.tick()))
end

function QD.core_row_begin(name)
    api_drive.report(string.format("row-begin %s tick=%d", tostring(name), api_drive.tick()))
end

-- A long await's level predicate, wrapped to report every
-- QD.PROGRESS_EVERY_TICKS server ticks while it is still false.  The C
-- scheduler evaluates the level every frame it holds the await, so this is
-- the one place a single long yield can speak from without a C change.  A
-- match-only descriptor has no level to wrap: its start is reported once.
local function await_with_progress(descriptor, deadline)
    local note = tostring(descriptor.note or "await")
    local start = api_drive.tick()
    QD.core_progress(string.format("await '%s' begins, deadline %d tick(s)", note, deadline))
    local level = descriptor.level
    if type(level) ~= "function" then
        return api_drive.await(descriptor, deadline)
    end
    local next_report = start + QD.PROGRESS_EVERY_TICKS
    local wrapped = {}
    for key, value in pairs(descriptor) do
        wrapped[key] = value
    end
    wrapped.level = function()
        local now = api_drive.tick()
        if now >= next_report then
            next_report = now + QD.PROGRESS_EVERY_TICKS
            QD.core_progress(string.format("await '%s' still waiting, %d of %d tick(s)",
                note, now - start, deadline))
        end
        return level()
    end
    return api_drive.await(wrapped, deadline)
end

local function await(descriptor, deadline)
    if finished then
        park("await after finish")
    end
    if type(descriptor) == "table" and type(deadline) == "number"
        and deadline >= QD.PROGRESS_MIN_DEADLINE then
        return await_with_progress(descriptor, deadline)
    end
    return api_drive.await(descriptor, deadline)
end

QD.ok = ok
QD.fail = fail

-- t.await (and QD.await, the same function: `t` IS QD) answers a DETAIL on
-- `ok` (seam27): the descriptor's own note and how many server ticks the
-- wait took.  The C primitive resumes a met await with ("ok", nil), so
-- `t.check("x", t.await{...})` and `t.expect("x", t.await{...})` wrote a PASS
-- row with an empty detail -- which gate.py now fails.  A timeout keeps C's
-- own detail (the note).  Every other verb's own `return await(...)` goes
-- through the chunk-local `await` above and is unchanged; the verbs that
-- `return QD.await(...)` (player.click_obj's backpack-rose wait,
-- player.idle) answer `<note>: met after N tick(s)` too.
QD.await = function(descriptor, deadline)
    local start = api_drive.tick()
    local result, detail = await(descriptor, deadline)
    if result == "ok" and detail == nil then
        local note = type(descriptor) == "table" and descriptor.note or nil
        detail = string.format("%s: met after %d tick(s)", tostring(note or "await"),
            api_drive.tick() - start)
    end
    return result, detail
end

-- ---------------------------------------------------------------- the ledger
--
-- One row per t.step/t.expect: index, step, verdict, ticks (elapsed world
-- cycles since the previous row), shots (the numbered captures taken during
-- this step), detail. The file itself -- the header, the trailing SUMMARY
-- row, one row at a time as t.finish is what actually converges -- is
-- api.drive.ledger's job (torirs_plugin_drive.c: drive_ledger_write); this
-- is the bookkeeping that turns a bare (name, result, detail) into that row.

local last_tick = 0
local shot_counter = 0
local pending_shots = {}
local pending_notes = {}
-- Set by QD.core_shot_unchanged below; consumed by the next row's flush.
local pending_unchanged = false

-- verbs-ui's (future) t.shot calls this to learn the "NNN-name" its capture
-- is written under (<session>/shots/NNN-name.png, App_RequestScreenshot) and
-- to fold the shot into whichever step's row is written next. The number
-- MUST be monotonic for the life of the run -- shots/NNN-name.png is one
-- directory for the whole quest (design doc: "leaves behind a numbered
-- screenshot for every interaction"; gate.py's only remaining defence
-- against a step that silently took no real shot is a duplicate-MD5 check
-- across every file in that directory). `pending_shots` itself is reset by
-- `flush` on every step/expect row (below) so it must NOT be what backs the
-- number: sizing off it restarts the count at 1 on every row, so a
-- multi-step quest collides its filenames (two different steps' shots both
-- land on "01-<name>.png", one overwriting the other whenever a name
-- repeats, e.g. a "before"/"after" pair taken every step) and defeats the
-- ordering the numbering exists to give gate.py and a human reading the
-- directory.
function QD.core_next_shot(name)
    -- "No further shot is taken" -- the second half of the terminal-finish
    -- rule (the `park` banner above). This is the choke point for every
    -- capture in the suite (t.shot, and t.exec/t.check through it), so a
    -- post-finish shot never reaches the screenshot request at all: the
    -- script parks here instead, before a PNG is numbered that no ledger row
    -- will ever claim.
    if finished then
        park("shot after finish: " .. tostring(name))
    end
    shot_counter = shot_counter + 1
    -- Three digits: legends takes 780 shots in one run, and a two-digit
    -- prefix put "100-" between "10-" and "11-" in every plain filename
    -- sort (the contact sheet, TIMEOUT.png's pick of the last shot, a
    -- human's ls). Readers still sort by the NUMBER, never the string.
    local numbered = string.format("%03d-%s", shot_counter, name)
    pending_shots[#pending_shots + 1] = numbered
    return numbered
end

-- The capture `numbered` asked for was NOT WRITTEN: its frame was
-- byte-identical to the last shot this run actually wrote, so the driver
-- deleted it again (torirs_plugin_drive_ui.c's own banner, "The unchanged
-- frame"). ui.lua's QD.shot calls this on that answer.
--
-- Two things have to happen here and neither can happen in C. The name comes
-- back OUT of `pending_shots`, because a row that names a shot no longer on
-- disk is exactly what gate.py fails a quest for ("claims shot X, which is
-- not on disk") -- the row's `shots` column must be empty rather than
-- hopeful. And the number is HANDED BACK: the file numbering is what a human
-- reads the shots/ directory by, and a suppressed capture that kept its
-- number leaves a hole in it that looks like a lost picture. Handing it back
-- is safe precisely because nothing was written under it -- the next capture
-- takes the same number, and the only file that ever bore it is gone.
--
-- `pending_unchanged` is what the row itself says about it: one
-- `[frame unchanged]` in the detail, however many of this row's captures
-- were suppressed, because the row's point is that the screen did not move,
-- not how many times it was photographed not moving.
function QD.core_shot_unchanged(numbered)
    for index = #pending_shots, 1, -1 do
        if pending_shots[index] == numbered then
            table.remove(pending_shots, index)
            break
        end
    end
    shot_counter = shot_counter - 1
    pending_unchanged = true
end

-- Free-text context folded into the NEXT step/expect row's detail, so a
-- mutation test can see *why* a verb answered the way it did without
-- fabricating a whole extra step just to say so.
function QD.note(text)
    pending_notes[#pending_notes + 1] = text
end

-- A detail column that is not a string is DESCRIBED here, once, rather than
-- at every call site. api.drive.ledger reads that column with luaL_optstring,
-- which RAISES on a table -- "bad extra argument #-1 to 'ledger' (string
-- expected, got table)", measured 2026-09-19 -- and this sandbox has no
-- pcall, so such a raise does not fail one row, it ends the whole run at that
-- row with every later verb unreached. Several verbs document a TABLE as
-- their detail (ui.journal_open's {title, lines, complete}, world.tile's
-- {x,z,level}, chat.options' rows), and t.exec exists precisely to wrap a verb
-- and write its answer to the ledger, so handing one of those to t.expect or
-- t.exec must print, not detonate. Key order inside a rendered table follows
-- `pairs`, so it is a summary for a human reading the row, never something to
-- match on.
local function detail_text(detail)
    local kind = type(detail)
    if detail == nil or kind == "string" then
        return detail
    end
    if kind == "number" or kind == "boolean" then
        return tostring(detail)
    end
    if kind ~= "table" then
        return "<" .. kind .. ">"
    end
    local parts = {}
    for key, value in pairs(detail) do
        local shown = type(value)
        if shown == "string" or shown == "number" or shown == "boolean" then
            shown = tostring(value)
        else
            shown = "<" .. shown .. ">"
        end
        parts[#parts + 1] = tostring(key) .. "=" .. shown
    end
    if #parts == 0 then
        return "{}"
    end
    return "{" .. table.concat(parts, " ") .. "}"
end

-- A ledger row's OWN TWO COLUMNS -- `step` and `verdict` -- are checked here,
-- for the same reason detail_text exists directly above: api.drive.ledger
-- reads BOTH of them with luaL_checkstring (torirs_plugin_drive.c's
-- lua_drive_ledger), which RAISES on anything else, and this sandbox has no
-- pcall, so such a raise does not fail one row -- it ends the WHOLE RUN at
-- that row with every later verb unreached.
--
-- The shape that does it is the natural one, which is why documenting it was
-- never going to be enough: t.check's second argument IS a condition, so an
-- author writes `t.step(name, result == "ok", detail)` by analogy and gets
-- `quest-driver:253: bad extra argument #-1 to 'ledger' (string expected,
-- got boolean)`. Between a Rock wrote exactly that at its wall of flame and
-- threw away the 93 PASS rows already on disk -- the rows that had just
-- proved the Arzinian realm teleport and the gold-ore mining -- for one
-- mistyped argument in row 94 (build/quest_gate/betweenarock/ledger.tsv,
-- 2026-09-21).
--
-- So the bad argument is NAMED and the run carries on. That is this tree's
-- rule for a contract violation (CLAUDE.md) as far as it can be carried
-- across a boundary that has no `assert` and where the only louder answer
-- available -- the raise -- destroys the evidence it would be reporting on:
-- a boolean verdict is read as the PASS/FAIL it plainly meant, any other
-- non-verdict is FAIL, a non-string step name is tostring()'d, and either
-- way the row carries `[bad ledger argument]` in its detail saying which
-- argument was wrong, what the file actually wrote, and how it was read.
-- That detail reaches the ledger row AND drive_ledger_write's stderr mirror
-- (`QUEST <quest> <verdict> <step> ... why=...`), so the diagnosis is in
-- front of the author on a green run too, not only on a red one.
--
-- Naming it is NOT the same as allowing it. "Do not write this" is
-- lint_quest.py's `t.step(..., <boolean>)` rule, which the reviewer runs
-- (without --allow-check) before any file is believed; this is the floor
-- under a file that got past the linter, and a floor is not a licence.
local function describe_value(value)
    local kind = type(value)
    if kind == "string" then
        if #value > 80 then
            value = string.sub(value, 1, 80) .. "..."
        end
        return "the string \"" .. value .. "\""
    end
    if kind == "nil" then
        return "nil"
    end
    if kind == "number" or kind == "boolean" then
        return "the " .. kind .. " " .. tostring(value)
    end
    if kind == "table" then
        return "the table " .. detail_text(value)
    end
    return "a " .. kind
end

-- Both normalisers report through `pending_notes` -- QD.note's own buffer,
-- which the very next flush folds into that row's detail -- so the complaint
-- lands ON the offending row whichever path reaches the ledger, including
-- record_with_shot's, which has to resolve the step name before flush ever
-- sees it. Re-running either on an already-good value is a no-op, so the
-- two calls on that path cannot report twice.
local function normalise_step_name(name)
    if type(name) == "string" then
        return name
    end
    local coerced = tostring(name)
    pending_notes[#pending_notes + 1] = "[bad ledger argument] step name was "
        .. describe_value(name) .. ", not a string -- recorded as \"" .. coerced .. "\""
    return coerced
end

local function normalise_verdict(verdict)
    if verdict == "PASS" or verdict == "FAIL" or verdict == "BLOCKED" then
        return verdict
    end
    if type(verdict) == "boolean" then
        -- Unambiguous: the row is graded the way the file meant it, and the
        -- note is how the author learns the call was still wrong.
        local read_as = verdict and "PASS" or "FAIL"
        pending_notes[#pending_notes + 1] = "[bad ledger argument] verdict was "
            .. describe_value(verdict) .. ", read as " .. read_as
            .. " -- t.step's verdict is \"PASS\"/\"FAIL\"/\"BLOCKED\" (the "
            .. "`cond and \"PASS\" or \"FAIL\"` idiom); t.check and t.expect "
            .. "are the ones that take the condition itself"
        return read_as
    end
    -- Not a verdict and not a condition: a verb's own result word
    -- (t.expect's argument), a nil from a two-argument call, a detail that
    -- slid into the wrong slot. Nothing here says the step passed, so it
    -- did not -- and unlike the boolean above there is no reading of it
    -- that the file plainly meant, which is the whole difference between
    -- the two branches.
    local note = "[bad ledger argument] verdict was " .. describe_value(verdict)
        .. ", which is not \"PASS\"/\"FAIL\"/\"BLOCKED\" -- graded FAIL because "
        .. "nothing in the call says otherwise"
    if type(verdict) == "string" then
        note = note .. " (a verb's own result word goes to t.expect, which grades it)"
    end
    pending_notes[#pending_notes + 1] = note
    return "FAIL"
end

-- Every row this run has written, and how many of them were not PASS. The
-- legs harness (run.py's wrapper, docs/quest_authoring/relay.md) reads the
-- pair before and after a leg: a checkpoint is written only after a leg whose
-- own rows were ALL PASS.
local rows_written = 0
local rows_not_pass = 0

function QD.core_row_tally()
    return rows_written, rows_not_pass
end

-- The runner camera split (raid camera seam runner_view_split): while a
-- Play's own view is attached (QD.core_run_test), every action the WATCHER
-- took on the game with Interact on is a `watcher.<what>` row, written ahead
-- of the script's next row with the tick it reached the game. A test run
-- attaches nothing, so this is one boolean test per row there.
local view_attached = false
local watcher_serial = 0

local function core_watcher_rows()
    local result, actions = api_drive.view_watcher(watcher_serial)
    if result ~= "ok" or type(actions) ~= "table" then
        return
    end
    for _, action in ipairs(actions) do
        watcher_serial = action.serial
        api_drive.ledger({ step = "watcher." .. tostring(action.what), verdict = "PASS", ticks = 0, shots = "",
            detail = string.format("tick=%d at %d,%d detail=%d: the watcher acted on the game (Interact on)",
                action.tick, action.x, action.y, action.detail) })
    end
end

local function flush(name, verdict, detail)
    if view_attached then
        core_watcher_rows()
    end
    name = normalise_step_name(name)
    verdict = normalise_verdict(verdict)
    rows_written = rows_written + 1
    if verdict ~= "PASS" then
        rows_not_pass = rows_not_pass + 1
    end
    local now = api_drive.tick()
    local ticks = now - last_tick
    last_tick = now
    detail = detail_text(detail)
    if #pending_notes > 0 then
        local joined = table.concat(pending_notes, "; ")
        detail = (detail and detail ~= "") and (detail .. " -- " .. joined) or joined
        pending_notes = {}
    end
    if pending_unchanged then
        local marker = "[frame unchanged]"
        detail = (detail and detail ~= "") and (detail .. " " .. marker) or marker
        pending_unchanged = false
    end
    local shots = table.concat(pending_shots, ",")
    pending_shots = {}
    api_drive.ledger({ step = name, verdict = verdict, ticks = ticks, shots = shots, detail = detail or "" })
    -- The row above was REFUSED if the run has already finished: SUMMARY is
    -- on disk and drive_ledger_write answers a row that arrives after it with
    -- one stderr line, "quest-driver: row after finish ignored: <name>",
    -- rather than appending below the summary (torirs_plugin_drive.c). The
    -- call is still made rather than skipped here, because that refusal --
    -- named, on stderr, once -- is the evidence a reader needs that a quest
    -- file kept going after it said it was done. Then the script stops: this
    -- is the first row it tried to write past its own finish, and it is the
    -- last thing it does.
    if finished then
        park("row after finish: " .. tostring(name))
    end
    return verdict == "PASS"
end

-- The test's own controls, and they live on the ROOT table beside the verb
-- namespaces: `t.cheat`, `t.ticks`, `t.step`, `t.exec`, ... There used to be
-- a second namespace named `t` inside `t` holding exactly these, so every
-- quest file spelled the prefix twice; that bought nothing and is gone. t.key,
-- t.text and t.shot are verbs-ui's (ui.lua); everything below is the
-- scheduler's.

-- Manual record: the caller already knows the verdict (e.g. a hand-rolled
-- assertion, or bridging a non-drive check into the same ledger).
function QD.step(name, verdict, detail)
    return flush(name, verdict, detail), detail
end

-- Assertion form: wraps a verb's own (result, detail) pair and records PASS
-- when result == "ok", FAIL otherwise. Returns the verb's own pair back
-- unchanged, so `local r, d = t.expect("open bank", ui.open(...))` still
-- reads like the verb call it wraps.
function QD.expect(name, result, detail)
    flush(name, ok(result) and "PASS" or "FAIL", detail)
    return result, detail
end

-- t.exec / t.check share one rule (docs/QUEST_SUITE_KIT.md phase 2 table): a
-- repeated `name` in the same run gets -2, -3, ... appended, so two calls
-- that reuse a name (a "before"/"after" pair, a retried step) still write
-- two distinguishable ledger rows and two distinguishable shot names instead
-- of two rows that read identically. QD.core_next_shot's own NN- counter
-- already keeps the FILES unique; this keeps the NAME -- the ledger's `step`
-- column and gate.py's "one PNG per t.exec row" bookkeeping -- unique too.
local step_name_uses = {}
local function unique_step_name(name)
    local count = (step_name_uses[name] or 0) + 1
    step_name_uses[name] = count
    if count == 1 then
        return name
    end
    return name .. "-" .. tostring(count)
end

-- Common tail for t.exec/t.check: shoot `name` (and, on a non-PASS verdict,
-- also `name-FAIL`) BEFORE flush -- QD.core_next_shot only queues a shot for
-- whichever row's flush() runs next (its own banner, above), so the order
-- here is load-bearing: shoot first, flush second, or the shot lands on the
-- QUEST's next row instead of this one.
local function record_with_shot(name, verdict, detail)
    -- The name is resolved HERE, before unique_step_name, and not left to
    -- flush: a nil name would otherwise reach `step_name_uses[nil] = count`,
    -- which raises "table index is nil" and ends the run one line short of
    -- the ledger call this whole seam is about.
    local unique_name = unique_step_name(normalise_step_name(name))
    QD.shot(unique_name)
    if verdict ~= "PASS" then
        -- `true` is QD.shot's `keep`: the -FAIL capture is NEVER suppressed
        -- as an unchanged frame. A verb that failed without moving the screen
        -- is the commonest failure there is (a timed-out await, a click that
        -- landed on nothing), and it is the one whose picture a human most
        -- needs -- "the frame was identical to the last one" is not an
        -- answer to "what did the screen look like when this failed".
        QD.shot(unique_name .. "-FAIL", true)
    end
    return flush(unique_name, verdict, detail), detail
end

-- Assertion form, but not a verb: PASS iff `condition_or_result` is `true`
-- or the string "ok". Unlike t.expect, t.check takes its own screenshot(s)
-- (see record_with_shot above) rather than leaving that to the caller.
function QD.check(name, condition_or_result, detail)
    local pass = condition_or_result == true or condition_or_result == "ok"
    record_with_shot(name, pass and "PASS" or "FAIL", detail)
    return condition_or_result, detail
end

-- t.exec(name, verb, ...) -> verb's own (result, detail), forwarded unchanged.
--
-- NAMED `exec`, NOT `do`: the spec first called this verb after the Lua
-- reserved word `do` (3rd/lua/llex.c's keyword table), which the grammar's
-- `Name` rule excludes -- so the definition, a bare read of it, and every
-- call site a quest file would ever write were all syntax errors, and the
-- verb had to be defined and called through a string index instead.
-- Verified against this tree's own vendored Lua at the time: both the call
-- and a bare read failed to parse with "<name> expected near 'do'". A verb
-- whose every call site has to be spelled with a string index is a verb the
-- generator, the linter, the gate and verb_list.py each need a private
-- regex for, so the name moved instead of the syntax: `exec` is an ordinary
-- Lua Name, it reads the same at every call site, and nothing in this tree
-- spells the old one any more.
--
-- Bad verb (not a function) or a nil first argument (the "target" every
-- verb this wraps takes as its own first parameter) is a FAIL naming the
-- shape, not a call: an untargeted verb is a broken TEST, not a `not_found`
-- the world answered.
QD.exec = function(name, verb, ...)
    if type(verb) ~= "function" then
        return record_with_shot(name, "FAIL", "bad verb/target")
    end
    local target = ...
    if target == nil then
        return record_with_shot(name, "FAIL", "bad verb/target")
    end
    -- Before the verb runs: a verb that never returns (the run ended inside
    -- it) is named by this line and nothing else (seam31).
    QD.core_row_begin(name)
    local result, detail = verb(...)
    -- Hollow rule (docs/QUEST_SUITE_KIT.md phase 2, README's hollow rule):
    -- an `ok` verb answer with no detail behind it is graded FAIL `hollow`,
    -- not PASS -- t.exec cannot read a verb's own banner to know whether ITS
    -- particular nil is documented or not (there is no pcall here to probe
    -- with, and no per-verb table the way _conformance.lua's own hand-written
    -- `answered()` predicates have), so the rule t.exec enforces is the blanket
    -- one the spec states in its own words: ok + nil detail = hollow. A verb
    -- whose successful answer is legitimately empty (t.ticks, t.settle,
    -- drive.camera, ...) is not a "verb" this wrapper should be pointed at --
    -- call it directly and record it with t.check/t.step instead.
    if result == "ok" and detail == nil then
        record_with_shot(name, "FAIL", "hollow -- ok with no detail")
        return result, detail
    end
    record_with_shot(name, ok(result) and "PASS" or "FAIL", detail)
    return result, detail
end

-- Writes the fixed BLOCKED verdict (docs/QUEST_SUITE_KIT.md phase 1) as row
-- "blocked", shoots it, and calls t.finish(0) -- which ENDS THE RUN: the
-- BLOCKED row and its shot are written first, in that order, and the finish
-- is terminal, so nothing the quest file writes after `t.blocked(...)` runs
-- at all (QD.finish and the `park` banner at the top of this file).
--
-- That is a change from the behaviour this banner used to record and the one
-- the 2026-09-19 pilot ran into: a t.step placed after t.blocked used to
-- execute and append its own row to ledger.tsv BELOW the SUMMARY line
-- already on disk, which is how two authors came to report "blocked" for a
-- file that had in fact run on to expect_complete with 8-21 FAIL rows.
--
-- `return` immediately after `t.blocked(...)`/`t.finish(...)` anyway: the
-- code below it is now unreachable rather than harmful, and unreachable code
-- that looks live is its own trap for the next reader. lint_quest.py (phase
-- 3) has that as a checkable rule.
function QD.blocked(reason)
    QD.shot("blocked")
    flush("blocked", "BLOCKED", reason)
    return QD.finish(0)
end

-- After dispatch, wait (<=5 ticks) for any NEW chat line: every cheat ladder
-- branch and every debugproc prints one (phase 1 banner,
-- src/torirsserver/torirs_server_world.c), so this gives the cheat's own
-- effect a chance to become visible -- in the chatbox, and by extension in
-- the world state the chatbox line is reporting on -- before the caller's
-- very next read runs against a client that has not seen the reply yet.
-- QD.msg.await's substring match is plain (string.find(..., 1, true)), and
-- an EMPTY substring matches at position 1 in any text (verified: 3rd/lua,
-- string.find("x", "", 1, true) == 1), so "" is "any line at all", not a
-- literal empty-message search. The RESULT returned to the caller is
-- api_drive.cheat's own (result, detail), unchanged either way: a timed-out
-- await here (a cheat that prints nothing, or ::nosuchcheat's no_row) is not
-- folded into it, and t.cheat still answers exactly what the ladder/
-- debugproc verdict was.
--
-- `wait_for_reply = false` IS THE ONE WAY OUT, and it exists because this
-- wait has a cost the spec did not name: msg.await is scoped to lines that
-- arrive AFTER it registers, so a cheat whose reply t.cheat already waited
-- for can never be waited for again -- "dispatch the cheat, then
-- t.msg.await('Dropped')" times out, honestly, with nothing new to wait for.
-- Measured 2026-09-19 by the verb conformance harness, whose msg.await row
-- had exactly that shape and went red the first time this landed. So a
-- caller that is going to do the waiting ITSELF says so, and gets the old
-- fire-and-forget dispatch; everybody else -- every quest test, every setup
-- list -- gets the wait by default and never thinks about it.
--
-- `::bankgive` IS SETUP ONLY (seam bank_withdraw_and_deposit_verbs,
-- matthew-mbp-m4-b56-seam2).  It stocks the bank the t.bank verbs withdraw
-- from (general/scripts/misc/cheat_bank.rs2), which is the bank-side twin of
-- a setup `::give` -- and, after the quest is bound, a mid-run `::give` with a
-- detour through the bank (trap 16).  Every quest file's run() binds first
-- (t.quest.bind, or the legs harness's top-level `bind` before leg 1), and
-- run.py's wrapper runs `setup` before run(), so "the quest is bound" is the
-- one reading that separates the two without a hand-kept phase flag.  The
-- refusal is the driver's, made before anything is sent: nothing reaches the
-- server, and the caller's row says why.
function QD.cheat(text, wait_for_reply)
    if type(text) == "string" and string.match(text, "^%s*:*bankgive") ~= nil
        and QD.quest._bound ~= nil then
        return "refused", "::bankgive is a SETUP cheat: it stocks the bank before the quest "
            .. "starts; after t.quest.bind it would be a mid-run ::give (trap 16) -- put it in "
            .. "`setup` and withdraw with t.bank.withdraw"
    end
    -- Every seat sends the cheat as the client's CLIENT_CHEAT packet, run at
    -- the server's next tick (torirs_plugin_drive.c DriveCore_Cheat), so
    -- "nothing understood it" is the server's reply line, not a return code.
    local _, since = api_drive.message_serial()
    local result, detail = api_drive.cheat(text)
    if wait_for_reply ~= false then
        QD.msg.await("", 5)
        local mr, list = api_drive.messages()
        if mr == "ok" then
            for i = 1, #list do
                if list[i].serial > (since or 0) and string.find(list[i].text, "Unknown command: ", 1, true) == 1 then
                    return "no_row", list[i].text
                end
            end
        end
    end
    return result, detail
end

-- Advance the virtual clock by exactly n server ticks and no more: a level
-- predicate on drive.tick() reaching a target computed ONCE, at call time,
-- so two overlapping t.ticks calls can never race each other's target.
function QD.ticks(n)
    local target = api_drive.tick() + n
    return await({ level = function() return api_drive.tick() >= target end,
                    note = "t.ticks" }, n + 2)
end

function QD.settle()
    return await({ level = function() return api_drive.settled() end, note = "t.settle" }, 30)
end

-- t.until_tick(tick) -> "ok" | "late", detail: wait for the server tick
-- `tick` (api_drive.tick, the tick this client has perceived). "late" when it
-- has already passed: a party that starts a room on a fixed absolute tick
-- starts it on the same tick on every lane, so every absolute clock the room
-- stamps (an instance's clock, a varp holding a tick) agrees between a live
-- party and scriptrun -- and a seat that got there late must say so rather
-- than start on another tick.
function QD.until_tick(tick)
    local now = api_drive.tick()
    if now > tick then
        return "late", string.format("t.until_tick: tick %d is past (now %d)", tick, now)
    end
    return await({ level = function() return api_drive.tick() >= tick end,
                   note = "t.until_tick " .. tostring(tick) }, tick - now + 2)
end

-- t.tick() -> (ok, tick): the SERVER's tick (raid seam 1,
-- docs/RAID_ORCHESTRATOR.md section 4), as the client was TOLD it: the tick of
-- the last boundary whose output reached this client (api_drive.server_tick,
-- ToriRSServer_EmbedClientTick -- a member's TICK frame, the leader's and a
-- solo run's own boundary). The client never reads its embedded server's
-- counter; every seat now answers from the one source.
--
-- Not api_drive.tick, which every deadline above is written against: that is
-- the client's world cycle / 30, a clock that runs at the client's frame pace
-- from wherever the client started. A tick-ledger row ("Maiden swung on 9,
-- 19, 29"; "the step resolved on T+1") is about the server's phase order, and
-- only srv->tick states it -- it is the clock every t.ticklog row carries.
-- `unsupported` on a socket-server run or a binary without the seam.
--
-- A PARTY MEMBER (raid seam22, party_death_and_member_readers) holds no world;
-- server_tick answers it from its TICK frames now, and the fallback below (for
-- an older binary, where it answered unsupported) reads instead the tick its last
-- TICK frame carried (api_drive.session().lockstep_tick, torirs_plugin_drive.c
-- lua_drive_session; nil outside a party), which
-- the leader stamped from srv->tick right after that boundary's world tick.
-- Between two boundaries it is the number the leader's t.tick() reads in the
-- same interval, so a member can write tick-stamped rows and wait "until tick
-- T" on the same clock as the tick log. The leader and a solo run never take
-- this branch (server_tick answers ok): their t.tick is unchanged.
function QD.tick()
    if api_drive.server_tick == nil then
        return "unsupported", "t.tick: this binary has no api_drive.server_tick (rebuild)"
    end
    local result, tick = api_drive.server_tick()
    if result ~= "ok" then
        local session = api_drive.session()
        if type(session) == "table" and math.type(session.lockstep_tick) == "integer" then
            return "ok", session.lockstep_tick
        end
        return result, "t.tick: no embedded server in this run"
    end
    return "ok", tick
end

-- t.drive.start(path, session_dir) / t.drive.stop() / t.drive.status()
-- (raid seam23, script_start_on_demand): a WATCHED client's driver.
--
-- A client started with TORIRS_DRIVE_ON_DEMAND=1 (profiles/
-- osrs239-scripts.ini, the Scripts tab) runs no script at world-ready: start
-- names an already-wrapped script file (run.py's write_wrapper_script output;
-- the tab itself uses t.drive.play below since seam24) and the session
-- directory its ledger and
-- shots go to, and the coroutine begins on the next frame the world is ready;
-- stop ends it at its next yield with a `run.unfinished` FAIL row and a
-- SUMMARY (exit=none), the two lines run.py writes for a run that never
-- finished; status reads {state = idle|running|finished, script, session,
-- step, verdict, rows, pass, fail, blocked, summary, exit, on_demand, runs,
-- starting, stopping}. A finished or stopped script returns the driver to
-- idle and the quest-driver plugin is reloaded, so nothing a part remembered
-- reaches the next run. Thin on purpose: the state is the C driver's
-- (torirs_plugin_drive.c, "on demand").
--
-- On a TEST run (this harness, run.py) start and stop answer `refused` --
-- that run's script is TORIRS_QUEST_SCRIPT and ends at t.finish -- and status
-- answers `ok` with state "running" and the run's own script. None of the
-- three is ever `unsupported`.
function QD.drive.start(path, session_dir)
    return api_drive.start(path, session_dir)
end

function QD.drive.stop()
    return api_drive.stop()
end

function QD.drive.status()
    return api_drive.status()
end

-- t.drive.tests([refresh]) / t.drive.play(test) (raid seam24,
-- scripts_tab_every_script): the Scripts tab's two questions. tests reads the
-- scripts manifest (tests/tests.ini, [test:<id>] sections) through the IO
-- layer the way the plugin host reads plugins/plugins.ini, answering
-- "timeout" while the read is in flight and the text once it lands; refresh
-- asks again. play({id, source, fixture, suite, title, legs}) plays the test
-- file as it sits in the tree on a fresh account (QD.core_run_test below) and
-- answers the account. On a TEST run both answer `refused`.
function QD.drive.tests(refresh)
    return api_drive.tests(refresh)
end

function QD.drive.play(test)
    return api_drive.play(test)
end

-- t.view.attach(role) / t.view.detach() / t.view.status() /
-- t.view.interact([on]) / t.view.watcher([after]) (raid camera seam
-- runner_view_split): the script's own world view (api.drive.view_*; the
-- meta file documents each). QD.core_run_test attaches for a Play, so no test
-- file calls these; they exist for the conformance rows and for a probe.
QD.view = {}

function QD.view.attach(role)
    local result, status = api_drive.view_attach(role)
    if result == "ok" and type(status) == "table" and status.attached then
        view_attached = true
        watcher_serial = status.watcher_serial or 0
    end
    return result, status
end

function QD.view.detach()
    if view_attached then
        core_watcher_rows()
        view_attached = false
    end
    return api_drive.view_detach()
end

function QD.view.status()
    return api_drive.view_status()
end

function QD.view.interact(on)
    return api_drive.view_interact(on)
end

function QD.view.watcher(after)
    return api_drive.view_watcher(after)
end

-- t.finish(code): write the ledger's SUMMARY row and END THE RUN.
--
-- The SUMMARY is written synchronously inside api_drive.finish
-- (PluginDrive_Finish -> drive_ledger_write_summary), and main.c stops the
-- frame loop at the end of this frame and exits with `code`. `finished` is
-- what makes the Lua half of that true as well: from here, the next ledger
-- row, the next shot and the next await all park forever instead (see the
-- `park` banner at the top of this file), so a quest file that forgets its
-- `return` cannot drive the client for another twenty rows below its own
-- summary line. A file may still `return` right after t.finish -- that is
-- clearer to read -- but it is no longer what makes the run stop.
--
-- Returns api_drive.finish's own (result, detail) for the one caller that
-- reads it, QD.blocked below.
function QD.finish(code)
    -- The run wrapper's own view goes at the finish (the C side detaches a
    -- stop): the watcher's last actions are rows first.
    if view_attached then
        core_watcher_rows()
        api_drive.view_detach()
        view_attached = false
    end
    local result, detail = api_drive.finish(code)
    finished = true
    return result, detail
end

-- ------------------------------------------------------------------ legs
--
-- QD.core_legs_drive(quest, opts): the relay harness (docs/quest_authoring/
-- relay.md "Checkpoints"). A quest file may declare
--     legs = { { name = "<leg>", run = function(t) ... end }, ... }
-- in place of `run`, plus a top-level `bind = {...}` (the t.quest.bind
-- table). run.py's wrapper hands the file's table here; so does
-- _conformance.lua's seam row, which is why this lives in the driver and not
-- in the wrapper's generated text.
--
--   * `quest.bind` is bound before the first leg that runs, so a leg resumed
--     from a checkpoint is bound exactly as it is in the full run;
--   * before leg k: row `leg.<k>.<name>` (PASS) carrying the player's tile
--     (client), the bound quest variable read from the SERVER, and the
--     backpack -- the ledger shows where each leg began, and a --from-leg
--     run's row can be compared with the full run's;
--   * after leg k, when every row the leg wrote was PASS (the file's last leg
--     included, when it returns without t.finish -- seam31):
--     `::checkpoint k` (the server's save serialiser in checkpoint mode). The
--     server refuses at a point that is not quiet -- a dialogue or interface
--     open, a parked script, combat -- and its reply names which. The outcome
--     is carried into the next leg row's detail, or a `leg.<k>.end` row when
--     no leg follows in this run; run.py reads it back from there;
--   * a leg that returns without t.finish hands on to the next; after the
--     last leg that runs, t.finish(0) -- unless opts.finish == false (the
--     conformance row, which has more rows to write).
--
-- opts: from (first leg, default 1), only (run `from` alone), tile ({x,z,level}
-- a resumed player must be seen standing on before the first read), finish.
-- Returns a report {rows = {<leg row detail>...}, checkpoints = {[k] = text}}
-- for a caller that did not finish.
function QD._legs_quest_state(bind)
    local parts = {}
    local tile_result, tile = QD.world.tile()
    if tile_result == "ok" and type(tile) == "table" then
        parts[#parts + 1] = "tile=" .. tile.x .. "," .. tile.z .. "," .. tile.level
    else
        parts[#parts + 1] = "tile=unread(" .. tostring(tile_result) .. ")"
    end
    if type(bind) == "table" and bind.varp then
        local stage_result, stage = QD.var.server(bind.varp)
        parts[#parts + 1] = "stage=" .. tostring(bind.varp) .. "="
            .. (stage_result == "ok" and tostring(stage)
                or ("unread(" .. tostring(stage_result) .. ")"))
    else
        parts[#parts + 1] = "stage=unbound"
    end
    local held = {}
    for slot = 0, 27 do
        local slot_result, cell = QD.inv.slot(slot)
        if slot_result == "ok" and cell.name ~= "" and cell.count ~= 0 then
            held[#held + 1] = cell.name .. "x" .. cell.count
        end
    end
    parts[#parts + 1] = "inv=" .. (#held > 0 and table.concat(held, ",") or "empty")
    return table.concat(parts, " ")
end

-- The server's own reply to the LATEST `::checkpoint k` ("checkpoint k written
-- at ..." or "checkpoint k refused: <why>"): the matching line with the
-- highest serial -- the ring's order is not the arrival order to rely on.
function QD._legs_checkpoint_reply(k)
    local lines_result, rows = QD.msg.last(16)
    local best, best_serial = nil, nil
    if lines_result == "ok" and type(rows) == "table" then
        for i = 1, #rows do
            local text = tostring(rows[i].text)
            local serial = tonumber(rows[i].serial) or 0
            if string.find(text, "checkpoint " .. k .. " ", 1, true)
                and (best_serial == nil or serial > best_serial) then
                best, best_serial = text, serial
            end
        end
    end
    return best or ("(no reply line from ::checkpoint " .. k .. ")")
end

-- How long the harness waits for a transient refusal (a parked script, a
-- combat claim) to clear before it gives up on a leg's checkpoint.
QD.LEGS_QUIET_TICKS = 10

function QD.core_legs_drive(quest, opts)
    opts = opts or {}
    local report = { rows = {}, checkpoints = {} }
    local legs = type(quest) == "table" and quest.legs or nil
    local shape_error = nil
    if type(legs) ~= "table" or #legs == 0 then
        shape_error = "legs is not a non-empty list of { name =, run = function(t) end }"
    elseif quest.run ~= nil then
        shape_error = "the file declares both `legs` and `run`: a relay file has legs only"
    else
        for index, leg in ipairs(legs) do
            if type(leg) ~= "table" or type(leg.name) ~= "string" or type(leg.run) ~= "function" then
                shape_error = "leg " .. index .. " is not { name = \"...\", run = function(t) ... end }"
                break
            end
        end
    end
    local from = opts.from or 1
    if not shape_error and (from < 1 or from > #legs) then
        shape_error = "leg " .. tostring(from) .. " does not exist: the file has " .. #legs .. " leg(s)"
    end
    if shape_error then
        flush("legs.shape", "FAIL", shape_error)
        if opts.finish ~= false then
            QD.finish(1)
        end
        return report
    end
    local last = opts.only and from or #legs
    local bind = quest.bind
    if bind ~= nil then
        local bind_result, bind_detail = QD.quest.bind(bind)
        if bind_result ~= "ok" then
            flush("legs.bind", "FAIL", "the file's bind field: t.quest.bind answered "
                .. tostring(bind_result) .. " (" .. tostring(bind_detail) .. ")")
            if opts.finish ~= false then
                QD.finish(1)
            end
            return report
        end
    end
    local carried = nil
    if from > 1 then
        -- The checkpoint's player logs in from its save: wait for the client
        -- to stand where the save says before the first read.
        if type(opts.tile) == "table" then
            QD.await({ level = function()
                local tile_result, tile = QD.world.tile()
                return tile_result == "ok" and tile.x == opts.tile[1]
                    and tile.z == opts.tile[2] and tile.level == opts.tile[3]
            end, note = "checkpoint: the saved tile" }, 20)
        end
        QD.ticks(2)
        QD.settle()
        carried = "resumed from checkpoint " .. (from - 1) .. " (from_leg=" .. from
            .. (opts.only and ", only_leg" or "") .. ")"
    end
    for k = from, last do
        local leg = legs[k]
        local detail = QD._legs_quest_state(bind)
        if carried then
            detail = detail .. " -- " .. carried
        end
        carried = nil
        report.rows[#report.rows + 1] = "leg." .. k .. "." .. leg.name .. ": " .. detail
        flush("leg." .. k .. "." .. leg.name, "PASS", detail)
        local bad_before = rows_not_pass
        leg.run(QD)
        -- t.finish (the quest's last leg) or t.blocked ended the run inside
        -- the leg: t.finish sets `finished` and returns, so the leg returns
        -- too -- nothing follows it, not even a checkpoint (seam31 found
        -- `_legs` writing checkpoint 3 after its t.finish(0)).
        if finished then
            break
        end
        local bad_in_leg = rows_not_pass - bad_before
        -- Every leg that returns unfinished gets its checkpoint, the file's
        -- LAST leg too (seam31): the leg a relay author has just written is
        -- the last one in the file, and the next author resumes from its
        -- checkpoint after appending leg k+1 (run.py hashes legs 1..k only).
        -- A camera a cutscene still drives is not a quiet point either (seam32
        -- cutscene_verb_and_camera_read): the save carries no camera, so a
        -- leg resumed from it would start free where the full run is still
        -- mid-shot. Wait out a sequence that is about to reset; one that
        -- never does is named and gets no checkpoint.
        local camera_driven = QD.cutscene and QD.cutscene._camera_driven
            and QD.cutscene._camera_driven(QD.LEGS_QUIET_TICKS) or nil
        if bad_in_leg == 0 and camera_driven then
            carried = "checkpoint " .. k .. " NOT written: " .. camera_driven
        elseif bad_in_leg == 0 then
            -- A leg's last click can leave a script parked for a tick or
            -- two (a p_delay after an item lands) or a single-way claim
            -- running down: those clear by themselves, so the request is
            -- repeated once a tick for up to QD.LEGS_QUIET_TICKS. A
            -- dialogue or an open interface never clears on its own and
            -- is still refused at the end, naming it.
            local cheat_result = QD.cheat("::checkpoint " .. k)
            local waited = 0
            while cheat_result == "refused" and waited < QD.LEGS_QUIET_TICKS do
                local reply = QD._legs_checkpoint_reply(k)
                if string.find(reply, "dialogue is open", 1, true)
                    or string.find(reply, "interface is open", 1, true) then
                    break
                end
                QD.ticks(1)
                waited = waited + 1
                cheat_result = QD.cheat("::checkpoint " .. k)
            end
            carried = "checkpoint " .. k .. (cheat_result == "ok" and " written: " or " NOT written: ")
                .. (cheat_result == "no_row"
                    and "this binary has no ::checkpoint (no_row) -- rebuild it"
                    or QD._legs_checkpoint_reply(k))
                .. (waited > 0 and (" (after " .. waited .. " quiet-wait tick(s))") or "")
        else
            carried = "checkpoint " .. k .. " NOT written: leg " .. k .. " wrote "
                .. bad_in_leg .. " non-PASS row(s)"
        end
        report.checkpoints[k] = carried
        if k == last then
            local end_detail = QD._legs_quest_state(bind) .. " -- " .. carried
            report.rows[#report.rows + 1] = "leg." .. k .. ".end: " .. end_detail
            flush("leg." .. k .. ".end", "PASS", end_detail)
        end
    end
    if opts.finish ~= false and not finished then
        QD.finish(0)
    end
    return report
end

-- QD.core_run_test(loader, options): THE SCRIPTS TAB'S RUNNER (raid seam24,
-- scripts_tab_every_script). Not a verb: the coroutine a Play starts
-- (torirs_plugin_drive.c api.drive.play -> PluginLua_TestThreadCreate) calls
-- it with the RAW test file compiled as `loader` and the Play's options
-- {id, suite, title, account, password, source, legs}.
--
-- run.py runs a test through a wrapper it writes (tools/quest_gate/run.py
-- write_wrapper_script): the test's source embedded verbatim, the legs shim,
-- then a QUEST.run that waits for the login grant and runs the setup list.
-- A watched Play has no such file -- nothing is pre-generated and the source
-- is read again on every Play -- so this does the same here, in this order:
--
--   1. the test's table, built and checked exactly as the wrapper does (a
--      legs file becomes the one run that calls t.core_legs_drive with no
--      checkpoint: run.py's FULL run, every leg in one sitting, no relog);
--   2. the shot latch is settled (below);
--   3. the account: log out of whoever is in the world (t.session.logout's
--      click path) and log in as options.account, whose save the C side
--      wrote from the test's fixture -- a fresh account per Play, as every
--      test assumes; a failure is ONE FAIL row, watch.account, and the end;
--   4. core_run_test_wrapped below: write_wrapper_script's QUEST.run, copied
--      from the Lua run.py generates (every comment dropped; run.py's
--      docstring and the generated file carry them), so the login-grant wait
--      and the setup list are the ones a test run gets.
--
-- Nothing here runs on a test run: the bootstrap that calls it exists only in
-- an on-demand client. KEEP core_run_test_wrapped IN STEP with
-- write_wrapper_script: a change there is a change here.
local function core_run_test_wrapped(quest_setup, quest_run)
    return function(t)
        t.await({ level = function()
            for slot = 0, 27 do
                local slot_result, cell = t.inv.slot(slot)
                if slot_result == "ok" and cell.name ~= "" and cell.count ~= 0 then
                    return true
                end
            end
            return false
        end, note = "setup: the login grant" }, 10)
        t.settle()
        local function setup_give(text)
            local name, count = string.match(text, "^%s*:*give%s+([%w_]+)%s*(%d*)")
            if not name then
                return nil
            end
            local wanted = tonumber(count)
            if not wanted or wanted < 1 then
                wanted = 1
            end
            return name, wanted
        end
        local function setup_backpack_mark()
            local mark = 0
            for slot = 0, 27 do
                local slot_result, cell = t.inv.slot(slot)
                if slot_result == "ok" and cell.name ~= "" and cell.count ~= 0 then
                    mark = mark + cell.count + 1
                end
            end
            return mark
        end
        local function setup_backpack_empty()
            for slot = 0, 27 do
                local slot_result, cell = t.inv.slot(slot)
                if slot_result == "ok" and cell.name ~= "" and cell.count ~= 0 then
                    return false
                end
            end
            return true
        end
        local function setup_setlevel(text)
            if not string.match(text, "^%s*:*setlevel") then
                return nil
            end
            local stat, level = string.match(text, "^%s*:*setlevel%s+(%a[%w_]*)%s+(%d+)%s*$")
            if not stat then
                return "", nil
            end
            return stat, tonumber(level)
        end
        local function setup_wield(text)
            return string.match(text, "^%s*:*wield%s+([%w_]+)%s*$")
        end
        local function setup_last_lines(n)
            local lines_result, rows = t.msg.last(n)
            if lines_result ~= "ok" or type(rows) ~= "table" then
                return "(no chat lines)"
            end
            local texts = {}
            for i = 1, #rows do
                texts[#texts + 1] = "'" .. tostring(rows[i].text) .. "'"
            end
            return table.concat(texts, " / ")
        end
        local function setup_failed(cheat, why)
            t.step("setup." .. cheat, "FAIL",
                why .. " -- the world this quest assumes was never stated")
            t.finish(1)
        end
        if type(quest_setup) == "table" then
            for _, cheat in ipairs(quest_setup) do
                local give_name, give_count = setup_give(cheat)
                local before = nil
                local before_mark = nil
                if give_name then
                    local count_result, count_total = t.inv.count(give_name)
                    if count_result == "ok" then
                        before = count_total
                    else
                        before_mark = setup_backpack_mark()
                    end
                end
                local wield_name = setup_wield(cheat)
                local worn_before = nil
                if wield_name then
                    local worn_result, worn_count = t.ui._worn_count(wield_name)
                    if worn_result ~= "ok" then
                        setup_failed(cheat, "cannot read the worn container for "
                            .. wield_name .. " (" .. tostring(worn_result) .. " "
                            .. tostring(worn_count) .. "), so a wield could never be"
                            .. " proved")
                        return
                    end
                    worn_before = worn_count
                elseif string.match(cheat, "^%s*:*wield") then
                    setup_failed(cheat, "not `::wield <item_name>`: a line the"
                        .. " read-back cannot name is a wield nobody can prove")
                    return
                end
                local level_stat, level_wanted = setup_setlevel(cheat)
                if level_stat == "" then
                    setup_failed(cheat, "not `::setlevel <stat name> <level>`: the"
                        .. " engine answers ok to it and sets nothing, and a numeric"
                        .. " stat id cannot be read back to prove it landed")
                    return
                end
                local setup_result, setup_detail = t.cheat(cheat)
                if setup_result ~= "ok" then
                    setup_failed(cheat, "setup cheat answered "
                        .. tostring(setup_result)
                        .. " (" .. tostring(setup_detail) .. "); last lines: "
                        .. setup_last_lines(2))
                    return
                end
                if before ~= nil then
                    local landed = t.inv.await(give_name, before + give_count, 10)
                    if landed ~= "ok" then
                        local after_result, after_total = t.inv.count(give_name)
                        if after_result ~= "ok" or after_total <= before then
                            setup_failed(cheat, "the cheat answered ok and no "
                                .. give_name
                                .. " reached the backpack within 10 ticks (held "
                                .. tostring(before) .. " before, "
                                .. tostring(after_total) .. " after)")
                            return
                        end
                    end
                elseif before_mark ~= nil then
                    local moved = t.await({ level = function()
                        return setup_backpack_mark() ~= before_mark
                    end, note = "setup: ::give reaching the backpack" }, 10)
                    if moved ~= "ok" then
                        setup_failed(cheat, "the cheat answered ok and the backpack"
                            .. " did not change within 10 ticks (" .. give_name
                            .. " is not an obj this client can count, so every"
                            .. " slot was watched instead)")
                        return
                    end
                elseif worn_before ~= nil then
                    local worn_last = worn_before
                    local worn_landed = t.await({ level = function()
                        local read_result, reading = t.ui._worn_count(wield_name)
                        if read_result == "ok" then
                            worn_last = reading
                        end
                        return read_result == "ok" and reading > worn_before
                    end, note = "setup: ::wield reaching the worn container" }, 10)
                    if worn_landed ~= "ok" then
                        setup_failed(cheat, "the cheat answered ok and " .. wield_name
                            .. " is not worn 10 ticks later (worn " .. tostring(worn_before)
                            .. " before, " .. tostring(worn_last) .. " after); last lines: "
                            .. setup_last_lines(3))
                        return
                    end
                elseif level_stat ~= nil then
                    local level_last = "unread"
                    local level_landed = t.await({ level = function()
                        local read_result, reading = t.skill.read(level_stat)
                        if read_result ~= "ok" then
                            level_last = tostring(read_result) .. " " .. tostring(reading)
                            return false
                        end
                        level_last = "stated=" .. tostring(reading.stated)
                            .. " base_level=" .. tostring(reading.base_level)
                        return reading.stated and reading.base_level == level_wanted
                    end, note = "setup: ::setlevel reaching the client" }, 10)
                    if level_landed ~= "ok" then
                        setup_failed(cheat, "the cheat answered ok and " .. level_stat
                            .. " never read base_level " .. tostring(level_wanted)
                            .. " within 10 ticks (last reading: " .. level_last .. ")")
                        return
                    end
                elseif string.match(cheat, "^%s*:*clearinv") then
                    local cleared = t.await({ level = setup_backpack_empty,
                        note = "setup: ::clearinv reaching the client" }, 10)
                    if cleared ~= "ok" then
                        setup_failed(cheat, "the cheat answered ok and the"
                            .. " backpack still holds items 10 ticks later")
                        return
                    end
                end
            end
            t.ticks(1)
            t.settle()
        end
        return quest_run(t)
    end
end

local CORE_RUN_TEST_SHOT = "watch-start"

-- The shot latch (torirs_plugin_drive_ui.c, one capture outstanding at a
-- time) is C state that outlives the run that requested it: a run that ended
-- with a capture still in flight -- a t.shot whose await timed out, a script
-- error during one -- left it for this run's FIRST t.shot to collect, which
-- then names the last run's picture (seam23, tob_bloat's 001 row naming
-- maiden's 006). One capture here, before anything else, settles it: if a
-- stale request is pending this poll collects it (its file lands in the OLD
-- session's shots/), otherwise it photographs the world as Play found it.
-- Either way the latch is empty when the test's own first t.shot asks.
local function core_run_test_settle_shot()
    local result, detail = api_drive.shot(CORE_RUN_TEST_SHOT, false)
    if result == "timeout" then
        await({ level = function()
            result, detail = api_drive.shot(CORE_RUN_TEST_SHOT, false)
            return result ~= "timeout"
        end, note = "watch: the shot latch" }, 10)
    end
    api_drive.report("watch: shot latch settled: " .. tostring(result) .. " " .. tostring(detail))
end

function QD.core_run_test(loader, options)
    assert(type(loader) == "function", "core_run_test: loader is not the compiled test")
    assert(type(options) == "table", "core_run_test: no options table")
    local QUEST = loader()
    if type(QUEST) == "table" and QUEST.legs ~= nil then
        local legs_quest = QUEST
        QUEST = { setup = legs_quest.setup, run = function(t)
            return t.core_legs_drive(legs_quest, { from = nil, only = false, tile = nil })
        end }
    end
    if type(QUEST) ~= "table" or type(QUEST.run) ~= "function" then
        error("quest file did not return { run = function(t) ... end }"
            .. " or { legs = { { name =, run = function(t) ... end }, ... } }")
    end
    local quest_setup = QUEST.setup
    if quest_setup ~= nil and type(quest_setup) ~= "table" then
        error("quest file's setup is a " .. type(quest_setup)
            .. ", not a table of cheat lines")
    end
    -- THE RUNNER'S OWN VIEW (owner, 2026-10-05: "the scripts will have to
    -- say attachCamera(\"AutomationRunner\")"): from here every camera move,
    -- pick, press and photograph of this script goes through a view of its
    -- own, and the watcher's camera is left alone. A client that presents
    -- nothing attaches nothing and says so.
    local view_result, view = api_drive.view_attach("AutomationRunner")
    view_attached = view_result == "ok" and type(view) == "table" and view.attached == true
    watcher_serial = view_attached and view.watcher_serial or 0
    api_drive.report("watch: view " .. (view_attached and "attached (AutomationRunner; the watcher keeps their camera)"
        or ("not attached: " .. tostring(type(view) == "table" and view.reason or view))))
    core_run_test_settle_shot()
    -- The starting state the tab chose (raid seam25): reset (::resetcharacter
    -- on the live character, optionally a fixture in place), fresh (seam24's
    -- new account: logout, forget varps, login) or as_is.  session.lua
    -- QD.session._start owns all three; nil start is "fresh".
    local start_result, start_detail = QD.session._start(options)
    if start_result ~= "ok" then
        QD.step("watch.start", "FAIL", tostring(start_detail))
        QD.finish(1)
        return
    end
    QD.step("watch.start", "PASS", tostring(start_detail))
    -- A LAUNCHING LEADER (raid seam37): api.drive.play with party = {size =
    -- N, launch = true}. Its members are started now, after its own log-in
    -- (their world is this client's) and before the setup list, through the
    -- embedded IO server's launch service (raid.lua QD.launch._party_up); from
    -- the party host on, every boundary waits for their READY. A member's own
    -- Play has party.launch false and skips this.
    if type(options.party) == "table" and options.party.launch then
        local party_result, party_detail = QD.launch._party_up(options)
        QD.step("launch.party", party_result == "ok" and "PASS" or "FAIL", tostring(party_detail))
        if party_result ~= "ok" then
            QD.finish(1)
            return
        end
    end
    -- The camera as the first Play found it (torirs_plugin_drive.c
    -- g_demand_camera_*): the last test's camera verbs and a logout leave the
    -- pose where they put it, and a fresh process would start from the
    -- client's own.
    local camera = options.camera
    local camera_result = "none"
    if type(camera) == "table" then
        camera_result = api_drive.camera(camera.yaw, camera.pitch, camera.zoom)
        if camera_result ~= "ok" then
            QD.step("watch.account", "FAIL", "putting the camera back to yaw=" .. tostring(camera.yaw)
                .. " pitch=" .. tostring(camera.pitch) .. " zoom=" .. tostring(camera.zoom)
                .. " answered " .. tostring(camera_result))
            QD.finish(1)
            return
        end
    end
    api_drive.report(string.format("watch: camera %s; %s (%s) on %s: %s; %s", tostring(camera_result),
        tostring(options.id),
        tostring(options.suite), tostring(options.account), tostring(options.start or "fresh"), tostring(start_detail)))
    return core_run_test_wrapped(quest_setup, QUEST.run)(QD)
end
