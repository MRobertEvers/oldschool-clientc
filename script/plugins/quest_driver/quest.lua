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
--   varp       content symbol name of the quest's progress var: EITHER a
--              plain varp ("cookquest", configs/all.varp:61) or a VARBIT
--              ("quest_pry", configs/all.varbit:84653 basevar=pry_main,
--              with no all.varp row of its own at all). The field keeps its
--              name for every quest file already written; what it holds is
--              a var NAME, and which table that name lives in is this
--              file's problem, not the author's.
--
--              Until 2026-09-20 it was not: quest.stage/expect_stage and
--              expect_complete's quest.varp_complete row read
--              QD.var.varp(bound.varp) -- the varp table ONLY -- against a
--              QD.var.server(bound.varp) that resolves varbit-first, so a
--              varbit-tracked quest could not pass any of them: every stage
--              row answered `no_row quest_pry` (measured, test/quests/
--              pryingtimes.lua's banner and QUEUE.tsv row 123; that file
--              had to read t.var.varbit directly and end BLOCKED one row
--              short of expect_complete for this reason alone). The name is
--              now resolved ONCE, at bind time, through the same
--              QD._var_resolve state.lua's var.server/var.expect/var.await
--              use (varbit table first, then varp -- state.lua:37; that
--              order, not "varp then varbit", is what makes the client read
--              here and the server read in var.server the SAME var by
--              construction rather than by luck), the kind is remembered on
--              the binding, and every read below goes through the matching
--              pair: var.varbit + var.server->varbit_server, or var.varp +
--              var.server->var_server. Every ledger row names the kind it
--              read -- in the detail for a refusal or a failed read, and
--              through QD.note for expect_stage's `ok`, whose returned detail
--              stays the stage NUMBER _conformance.lua grades it on -- so a
--              row always says WHICH var answered.
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
-- ------------------------------------------------------- varbit transparency
--
-- WHICH TABLE DOES THIS NAME LIVE IN?  QD._var_resolve (state.lua:37) is the
-- one answer, and it is deliberately reused rather than re-implemented here:
-- var.server, var.expect, var.await and var.await_server all pick their
-- reader from it, so a quest that picks its CLIENT reader from anything else
-- can silently read a different var than the server half it is compared
-- against.  It tries the varbit table first and the varp table second.
--
-- Returns "varbit", "varp", or nil when NEITHER table has the name.  nil is
-- not an error here: quest.bind's own contract is that it never fails on a
-- typo'd name (see below), so the kind is re-resolved on each read until one
-- of the two tables answers.  A run whose symbols are not up yet at bind time
-- therefore still gets the right kind at the first stage read.
function QD.quest._kind_of(name)
    local kind = QD._var_resolve(name)
    return kind
end

-- The binding's kind, resolved at bind time and remembered -- re-resolved
-- only while it is still unknown.
function QD.quest._bound_kind(bound)
    if bound.varp_kind == nil then
        bound.varp_kind = QD.quest._kind_of(bound.varp)
    end
    return bound.varp_kind
end

-- (result, value, kind) -- the CLIENT's copy, read through the reader that
-- matches the name's own table.  `kind` is "varbit"/"varp"/"unknown" and is
-- what a detail string quotes.  An unresolvable name is handed to var.varp so
-- the failure word and the detail stay exactly what every other verb answers
-- for an unknown symbol (`no_row <name>`).
function QD.quest._read_client(name, kind)
    kind = kind or QD.quest._kind_of(name)
    if kind == "varbit" then
        local result, value = QD.var.varbit(name)
        return result, value, "varbit"
    end
    if kind == "varp" then
        local result, value = QD.var.varp(name)
        return result, value, "varp"
    end
    local result, value = QD.var.varp(name)
    return result, value, "unknown"
end

-- (result, value, kind) -- the SERVER's own value.  QD.var.server already
-- picks api_drive.varbit_server vs api_drive.var_server off the same
-- QD._var_resolve (state.lua:81-90), so there is nothing missing on the C
-- side and nothing to add on the Lua side: the pair is complete, and this
-- wrapper exists only so the kind travels beside the reading into the detail.
function QD.quest._read_server(name, kind)
    kind = kind or QD.quest._kind_of(name)
    local result, value = QD.var.server(name)
    return result, value, kind or "unknown"
end

-- ------------------------------------------------- the var with no client half
--
-- A THIRD channel, and it is not a third opinion: it is the only reader that
-- can answer at all for a var the client cannot address.
--
-- ToriRSServer_SendVarpSmall refuses to encode a varp id the connected
-- client's varp array cannot address (torirs_server_encode.c, "the official
-- client treats it as a fatal protocol error"), so a varp this tree allocates
-- ABOVE the cache's highest id is never transmitted, the client's array never
-- grows to cover it, and both halves of the pair above -- var[] and
-- var_serv[], which are the same array's two records -- answer `not_found`
-- for the whole run.  `transmit=yes` in the quest's own configs/*.varp does
-- not change that: the id is past what the wire can carry.  Measured
-- 2026-09-20 on `rovingelves_quest` (pack/varp.alloc id 6262 against an
-- all.varp.compack topping out near 5704), with the quest driven to a real
-- completion and ::setvar writing the value server-side:
--
--   var.varp -> not_found | var.server -> not_found | quest.stage -> not_found
--
-- so quest.varp_complete could never pass, however complete the quest was --
-- the row that FAILED was the driver's own reach, not the quest
-- (test/quests/rovingelves.lua's blocked row, QUEUE.tsv, and the same shape in
-- test/quests/pryingtimes.lua for the varbit half, which the bind banner above
-- already fixed).
--
-- api_drive.var_content reads the embedded server's OWN copy of the varp, out
-- of srv->active_player->varps[] (DriveState_VarpContent,
-- torirs_plugin_drive_state.c).  It cannot see a desync -- it is one number,
-- not two -- which is exactly why it is reached only when the pair has
-- answered `not_found` TWICE and there is no desync left to see: the client
-- holds no copy to disagree with.  Every row that lands on this channel says
-- so in its detail, so no ledger ever reads as if a client had agreed when
-- none could.
--
-- `unsupported` when the binary predates the reader (a run against an older
-- torirs_questtest): the row then FAILS with that word in it rather than
-- raising on a nil call.
function QD.quest._read_content(name)
    if type(api_drive.var_content) ~= "function" then
        return "unsupported", name .. ": this binary has no api_drive.var_content"
    end
    local result, id = api_drive.symbol("varp", name)
    if result ~= "ok" then
        return result, name
    end
    return api_drive.var_content(id)
end

-- ONE reading of the quest's progress var, from the strongest channel that
-- can answer, as a table every grader below reads the same way:
--
--   result   "ok" with a `value`, "refused" when the two client-side copies
--            disagree (the desync var.expect exists to catch), or the failing
--            read's own word.
--   source   "client+server" -- the pair agreed -- or "server content", the
--            fallback above.  A row prints it; nothing grades on it.
--   detail   what a ledger row says when this reading is the answer.
--
-- The fallback is tried for a VARP name only.  A varbit can never reach it:
-- VarPManager_GetVarbit answers 0/`ok` for a varbit whose base varp is past
-- the client's array (varp_manager.c's basevar bound), never `not_found`, so
-- there is no not_found pair to trigger on and a varbit on an untransmitted
-- carrier reads as a confident zero instead.  That is a DIFFERENT seam with a
-- different signature (test/quests/mourningsendpartii.lua's blocked row names
-- it), and guessing at it from here would mean grading a quest on the server's
-- word whenever the client merely disagreed -- which is the desync check
-- itself.
function QD.quest._reading(name, kind)
    kind = kind or QD.quest._kind_of(name)
    local reading = { name = name, kind = kind or "unknown", source = "client+server" }
    reading.named = name .. " (" .. reading.kind .. ")"
    reading.client_result, reading.client_value = QD.quest._read_client(name, kind)
    reading.server_result, reading.server_value = QD.quest._read_server(name, kind)

    if reading.client_result == "ok" and reading.server_result == "ok" then
        reading.detail = reading.named .. ": client=" .. tostring(reading.client_value)
            .. " server=" .. tostring(reading.server_value)
        if reading.client_value ~= reading.server_value then
            reading.result = "refused"
            return reading
        end
        reading.result = "ok"
        reading.value = reading.client_value
        return reading
    end

    if reading.kind ~= "varbit"
        and reading.client_result == "not_found"
        and reading.server_result == "not_found" then
        reading.content_result, reading.content_value = QD.quest._read_content(name)
        reading.source = "server content"
        if reading.content_result == "ok" then
            reading.result = "ok"
            reading.value = reading.content_value
            reading.detail = reading.named
                .. " has no client half (client=not_found server=not_found -- an id the"
                .. " client's varp array cannot address is never transmitted);"
                .. " read from the server's own varps instead: " .. tostring(reading.content_value)
            return reading
        end
        reading.result = reading.content_result
        reading.detail = reading.named
            .. ": client=not_found server=not_found and the server's own copy answered "
            .. tostring(reading.content_result) .. " " .. tostring(reading.content_value)
        return reading
    end

    reading.result = (reading.client_result ~= "ok") and reading.client_result
        or reading.server_result
    reading.detail = reading.named .. ": client read -> " .. tostring(reading.client_result)
        .. ", server read -> " .. tostring(reading.server_result)
    return reading
end

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
    local qp_result, qp_before = QD.quest._read_client("qp")
    QD.quest._bound = {
        varp = spec.varp,
        varp_kind = QD.quest._kind_of(spec.varp),
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

-- (result, value, kind, source).  The third and fourth returns are additive:
-- every caller in the tree reads the documented pair and is unaffected.
--
-- stage() is the CLIENT's reading and stays one: it is the raw read a quest
-- calls to look, and a quest that wants both sides graded calls expect_stage.
-- The one thing it will not do any more is answer `not_found` for a var the
-- client cannot hold -- it falls through to the server's own copy (see
-- _read_content) and says so in `source`, which is "client" or
-- "server content".
--
-- The failing detail is the var's own story now.  It used to read
-- `<name> (no varp and no varbit of that name)` for EVERY failing read, which
-- was measured wrong on rovingelves_quest: api_drive.symbol resolves that name
-- to kind=varp perfectly well and it is the VALUE that cannot be read, so the
-- one line a reader had to go on named the wrong cause.
function QD.quest.stage()
    local bound = QD.quest._bound
    if not bound then
        return "refused", "quest.stage: quest.bind was not called"
    end
    local kind = QD.quest._bound_kind(bound)
    local result, value = QD.quest._read_client(bound.varp, kind)
    if result == "ok" then
        return result, value, kind or "unknown", "client"
    end
    if kind == nil then
        return result, bound.varp .. " (no varp and no varbit of that name)"
    end
    if kind ~= "varbit" then
        local content_result, content_value = QD.quest._read_content(bound.varp)
        if content_result == "ok" then
            return "ok", content_value, kind, "server content"
        end
        return result, bound.varp .. " (" .. kind .. "): the client cannot address this var"
            .. " (client read -> " .. tostring(result) .. ") and the server's own copy answered "
            .. tostring(content_result)
    end
    return result, bound.varp .. " (" .. kind .. "): client read -> " .. tostring(result)
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

    -- One resolution, one reading, both sides.  The kind is named in EVERY
    -- answer below, pass or fail: "quest_pry (varbit) = 5" is a row that says
    -- what it read, where the old bare `quest_pry` could not distinguish "the
    -- varp table has no such name" from "the value is wrong".
    --
    -- What is graded is unchanged: a read that cannot answer returns its own
    -- word, a client that disagrees with the server is `refused` naming both
    -- sides, and a value that is not the one asked for is `refused` naming
    -- what was read.  What is new is the one case where there is no client
    -- copy to disagree with at all -- QD.quest._reading falls through to the
    -- server's own varps there, and its detail says which channel answered.
    local kind = QD.quest._bound_kind(bound)
    local reading = QD.quest._reading(bound.varp, kind)
    local named = reading.named

    if reading.result ~= "ok" then
        return reading.result, reading.detail
    end
    if reading.value ~= value then
        return "refused", reading.detail .. " expected=" .. tostring(value)
    end

    -- THE KIND GOES IN THE ROW, THE VALUE STAYS THE RETURN.  The ok detail
    -- is still the stage NUMBER, because that pair is what the conformance
    -- harness grades this verb on (test/quests/_conformance.lua's
    -- `answered(..., equals(QUEST_STARTED), ...)` -- a string there is a
    -- `hollow` row, and that file belongs to the conformance closer, not to
    -- this one).  What the ledger needs -- WHICH var answered -- is added as
    -- a note instead: core.lua folds it into the next row's detail, which is
    -- the row the caller is writing with this very pair, so
    -- `t.expect("...", t.quest.expect_stage("deliver"))` reads
    -- `5 -- quest_pry (varbit) = 5 (deliver)` and a reader can tell a varbit
    -- quest from a varp one without opening the config.
    QD.note(named .. " = " .. tostring(value)
        .. (type(name_or_value) == "string" and (" (" .. name_or_value .. ")") or "")
        .. (reading.source ~= "client+server" and (" [" .. reading.source .. "]") or ""))
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
        -- Both sides through the bound kind -- see the bind banner.  A
        -- varbit-tracked quest (quest_pry) read the varp table here and
        -- answered `no_row` on the client side forever, which is a FAIL that
        -- says nothing about the quest.
        --
        -- AND a varp the client cannot address at all reaches the server's own
        -- copy rather than failing a completed quest over a reading nothing in
        -- this process could ever take (QD.quest._reading, and
        -- QD.quest._read_content's banner for why `transmit=yes` does not
        -- help).  The row's detail names the channel either way, so a PASS
        -- taken that way is never mistaken for a client that agreed.
        local kind = QD.quest._bound_kind(bound)
        local reading = QD.quest._reading(bound.varp, kind)
        local pass = reading.result == "ok" and reading.value == complete_value
        all_pass = all_pass and pass
        QD.step("quest.varp_complete", pass and "PASS" or "FAIL",
            reading.detail .. " complete=" .. tostring(complete_value)
                .. " [" .. reading.source .. "]")
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
    -- The completion modal is photographed in EVERY run, before it is read
    -- and before quest.journal dismisses it (owner's rule, 2026-09-20): the
    -- scroll on screen is the one picture a green quest cannot be without.
    -- `keep` is true so the unchanged-frame dedupe never suppresses it even
    -- when the hand-in's own last shot already showed the scroll. The shot
    -- folds into the quest.scroll_title row below, which gate.py checks.
    QD.shot("quest.scroll", true)
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
    -- `qp` is a plain varp in this pack (configs/all.varp:206) and read
    -- through the same transparent pair anyway: the point of the pair is
    -- that no read in this file has to know which table a name lives in.
    local qp_result, qp_after, qp_kind = QD.quest._read_client("qp")
    local points_pass = false
    local points_detail
    if bound.qp_before == nil then
        points_detail = "no qp reading at bind time (" .. tostring(bound.qp_before_result) .. ")"
    elseif qp_result ~= "ok" then
        points_detail = "qp read -> " .. tostring(qp_result) .. " " .. tostring(qp_after)
    else
        local delta = qp_after - bound.qp_before
        points_pass = delta == bound.points
        points_detail = "qp (" .. tostring(qp_kind) .. ") " .. tostring(bound.qp_before)
            .. " -> " .. tostring(qp_after)
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
