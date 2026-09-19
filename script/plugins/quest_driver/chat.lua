-- quest-driver / chat: holding a conversation.
-- Owner: verbs-chat (docs/ARCHITECT.md).
--
-- Every click here drives the RESUME seam (D1).  A chatmenu row is
-- cc_create'd text with no cache op and no button type, so a real click
-- builds RESUME_PAUSEBUTTON with action_index -1; a fabricated IF_BUTTON
-- reaches a row the client would never have built.
--
-- Two calls in a row must not both submit: api.drive.pause_pending is checked
-- BY COMPONENT ID before arming, because a player cannot double-submit and a
-- test that can is a test that proves nothing.  Identity for chat.continue_'s
-- click is resolved through api.drive.component -- the qualified
-- "<iface>:<child>" content symbol -- never a numeric id.
--
-- chatmenu's title and rows are cc_create'd at runtime and have no
-- content-pack symbol at all (chatbox_multi_init / chatbox_multi_addoption),
-- so neither api.drive.component nor a plain content-symbol lookup can name
-- one; only the dialog_options_title / dialog_options_row_N revconfig roles
-- can (plan 5.5 said "zero new C" via api.widgets, but nothing exposes
-- api.widgets to a part file other than core.lua -- only api.drive is
-- captured as a chunk-local upvalue in quest_driver/core.lua's core_bind, and
-- extending that is core-scheduler's file, not this pass's to touch -- see
-- the report filed for this pass). api.drive.options/option_row read and
-- identify those roles directly instead, entirely inside verbs-chat's own
-- files.

-- Private helpers hang off QD.chat as PLAIN FIELDS, never a top-level
-- `local`: every one of these eight files is concatenated into ONE chunk
-- before it compiles, so a `local` outside a function body is a register in
-- that single outer function, and only core.lua may spend one (docs/ARCHITECT.md
-- section 3, "Only core.lua declares chunk-scope locals").

-- The interface symbols every dialogue-shaped page can mount as, and the
-- fully-qualified continue-child symbol a real click resumes on. objectbox
-- has no continue child at all -- if_addresumebutton arms objectbox:universe
-- itself -- and chatmenu/levelup_display/questscroll are deliberately absent:
-- continuing those is not this verb's job (options goes through chat.choose,
-- levelup/scroll through verbs-read).
QD.chat._continue_symbol_by_iface = {
    chat_left = "chat_left:continue",
    chat_right = "chat_right:continue",
    messagebox = "messagebox:continue",
    messagebox_titled = "messagebox_titled:continue",
    messagebox_url = "messagebox_url:continue",
    objectbox = "objectbox:universe",
    objectbox_double = "objectbox_double:pausebutton",
}

-- chat.kind()'s bucketing (plan 5.4): several interfaces collapse into one
-- kind because the TEXT verbs (verbs-read) do not need to tell them apart at
-- this level -- only chat.continue_'s exact symbol lookup above does.
QD.chat._kind_by_iface = {
    chat_left = "npc",
    chat_right = "player",
    messagebox = "mesbox",
    messagebox_titled = "mesbox",
    messagebox_url = "mesbox",
    objectbox = "objbox",
    objectbox_double = "objbox",
    chatmenu = "options",
    levelup_display = "levelup",
    questscroll = "scroll",
}

-- meslayer_enter's switch_int(%varcint5): 7/19 answer resume_countdialog, 8
-- answers resume_namedialog, and every other nonzero value dispatches some
-- other prompt (friend/ignore add-delete, clan name, world search, chat
-- filter, autotyper, quantity-button callback, ...) that must not be typed
-- into blind. 0 and 1 are both "no prompt, ordinary chat line"
-- (meslayer_mode1.cs2: `%varcint5 <= 0 | %varcint5 = 1`).
function QD.chat._meslayer_bucket(mode)
    if mode == 7 or mode == 19 then
        return "count"
    end
    if mode == 8 then
        return "name"
    end
    if mode ~= nil and mode > 1 then
        return "other_input"
    end
    return "none"
end

-- Which page kind is live right now, synchronous, no await. See plan 5.4.
function QD.chat.kind()
    local group_res, group_id = api_drive.modal_group()
    if group_res == "ok" then
        local sym_res, sym = api_drive.symbol_name("interface", group_id)
        if sym_res == "ok" then
            return QD.chat._kind_by_iface[sym] or "none"
        end
        return "none"
    end
    local mode_res, mode = api_drive.meslayer_mode()
    if mode_res ~= "ok" then
        return "none"
    end
    return QD.chat._meslayer_bucket(mode)
end

-- chat.continue_ -- see docs/QUEST_DRIVER_PLAN.md 5.4 for the five steps this
-- follows. The await releases on whichever comes first: the client's own ack
-- of the click (resume_answered, scoped to the exact row -- D6), a fresh
-- mount under chat_modal_host (a new page arrived), or the host closing.
-- resume_answered fires the same frame the client drains the click, well
-- before the server's own reply remounts anything -- which is also why a
-- SECOND chat.continue_ called right after the first returns sees
-- pause_pending still active and is refused: only a new mount clears it.
function QD.chat.continue_()
    local group_res, group_id = api_drive.modal_group()
    if group_res ~= "ok" then
        return group_res, "chat.continue_: nothing mounted under chat_modal_host"
    end

    local sym_res, sym = api_drive.symbol_name("interface", group_id)
    -- Kept for the detail this verb answers with (trap 12/hollow rule):
    -- named from the SAME `sym` the continue-seam lookup below already
    -- resolved, not a second api_drive.symbol_name call.
    local from_kind = sym_res == "ok" and (QD.chat._kind_by_iface[sym] or "none") or "none"
    local continue_symbol = sym_res == "ok" and QD.chat._continue_symbol_by_iface[sym] or nil
    if not continue_symbol then
        return "unsupported", "chat.continue_: " .. tostring(sym) .. " has no continue seam"
    end

    local pending_res, pending_id = api_drive.pause_pending()
    if pending_res == "ok" and pending_id >= 0 then
        return "refused", "chat.continue_: a resume is already outstanding"
    end

    -- sub=-1: continue_symbol names the component itself (a leaf resume
    -- button, or objectbox:universe -- the node the role names, not a
    -- child of it). sub=0 asked DriveUi_Component for THAT component's own
    -- child at subid 0, which a leaf has none of, so this returned
    -- not_visible on every real dialogue page (QD-03). content_test.c's
    -- `resume` command (src/game/content_test.c:586) is the same call with
    -- the same -1.
    local com_res, com_id = api_drive.component(continue_symbol, -1)
    if com_res ~= "ok" then
        return com_res, "chat.continue_: " .. continue_symbol
    end

    local armed_res, armed = api_drive.click_armed(com_id)
    if armed_res ~= "ok" or not armed then
        return "not_visible", "chat.continue_: " .. continue_symbol .. " not armed"
    end

    local host_res, host_id = api_drive.component("chatbox:chatmodal", -1)

    local resume_res, resume_detail = api_drive.resume(com_id)
    if resume_res ~= "ok" then
        return resume_res, resume_detail
    end

    local await_result, await_detail = await({
        match = function(ev)
            if ev.kind == "resume_answered" then
                return ev.a == com_id
            end
            if host_res == "ok" and (ev.kind == "sub_mounted" or ev.kind == "sub_closed") then
                return ev.a == host_id
            end
            return false
        end,
        note = "chat.continue_",
    }, 6)
    if await_result ~= "ok" then
        return await_result, await_detail
    end

    local still_res = api_drive.modal_group()
    if still_res ~= "ok" then
        return "closed", "chat.continue_: modal closed"
    end
    return "ok", from_kind .. " -> " .. QD.chat.kind()
end

-- A page's own identity for a settle check: its text (npc/player/mesbox/
-- objbox, the same presented-text read chat.text() uses) or its rows
-- (options) -- so two mounts of the SAME kind in a row (two consecutive
-- "npc:" lines) are not mistaken for still being the first one. Never
-- QD.chat.text()/QD.chat.options() themselves: those await their OWN
-- readiness (chat.text up to 10 ticks; chat.options both rows resolved),
-- and a settle check wants a read of whatever is presented RIGHT NOW, not
-- one more wait stacked on top of the caller's own.
function QD.chat._page_identity(kind)
    if kind == "options" then
        local result, options = api_drive.options()
        return result == "ok" and table.concat(options.rows, "|") or nil
    end
    return QD.read._presented_text(QD.read._text_symbols)
end

-- Waits for the mounted page to actually differ from (before_kind,
-- before_identity) -- kind, or identity for a same-kind remount -- before
-- returning. EDGE + LEVEL (api.drive.await): already different resolves
-- with no yield. A page that never changes just runs out `ticks` -- this is
-- a best-effort settle whose own (result, detail) the caller ignores, not a
-- new failure mode drain/play must branch on.
function QD.chat._settle_change(before_kind, before_identity, ticks)
    return await({
        level = function()
            local kind = QD.chat.kind()
            if kind ~= before_kind then
                return true
            end
            return QD.chat._page_identity(kind) ~= before_identity
        end,
        note = "chat._settle_change",
    }, ticks or 6)
end

-- Pure Lua loop over chat.kind + the continue seam (plan 5.4). Stops BEFORE
-- clicking stop_at's kind. count/name/other_input/none are non-continue
-- kinds already, so the loop halts on them with no opt-in required. Any
-- result chat.continue_ cannot turn into another page (unsupported, refused,
-- not_visible) propagates straight out.
--
-- Each page's shot must be named for the page actually ON SCREEN when it is
-- taken, not for a stale read that still thinks something else is up. Two
-- things race that: chat.continue_'s own await (its banner above) can
-- resolve on resume_answered -- the CLIENT's ack of the click, fired the
-- same frame -- well before the server's reply actually remounts the next
-- page, so a `kind` read right after continue_() returns "ok" can still
-- describe the page that is CLOSING; and QD.shot itself pumps real frames
-- waiting for its own capture to land (D8/A1: a screenshot is not free),
-- during which that same pending remount can land and change what is on
-- screen out from under a name already decided before the call. Measured
-- 2026-09-19 against real published evidence: drained shots named for the
-- page a step had just left, or already showing the page after it, in both
-- directions.
--
-- So: settle first, name second. QD.chat._settle_change (above) waits,
-- content-first the same way chat.choose's own post-await classification
-- does (its QD-06 banner) and pointer.lua's _settle_after_click does for
-- clicks generally, for the mounted page to actually differ from the one
-- that was up before THIS iteration's continue_. Only once that has
-- resolved (or run out its own clock) does the NEXT iteration read `kind`
-- and take its shot, so the filename and the pixels agree. Keeps
-- QD.shot's own frame-pump semantics untouched (ui.lua) -- this settle runs
-- BEFORE the shot that needs it, not inside QD.shot itself.
function QD.chat.drain(opts)
    opts = opts or {}
    local max_pages = opts.max_pages or 40
    local stop_at = opts.stop_at
    local shots = opts.shots
    if shots == nil then
        shots = true
    end
    local pages = 0

    while true do
        local kind = QD.chat.kind()
        if kind == stop_at then
            return "ok", kind
        end
        if kind == "none" or kind == "count" or kind == "name" or kind == "other_input" then
            return "ok", kind
        end

        pages = pages + 1
        if pages > max_pages then
            return "timeout", kind
        end

        if shots then
            QD.shot(kind)
        end

        -- The settle below runs out its own clock and is never branched on,
        -- so a remount that took longer than it can still land DURING the
        -- QD.shot pump above (a screenshot is not free -- D8/A1). Re-read
        -- once here before clicking: continue_ against that NEW page is
        -- wrong twice over -- for stop_at it burns a page the caller asked
        -- to stop before touching, and for chatmenu/levelup_display/
        -- questscroll it always answers `unsupported` (chat.continue_ has no
        -- seam for those on purpose -- this file's own banner), which ends
        -- drain there instead of reporting the stop_at it had reached.
        -- Measured 2026-09-19: the cook's Talk-to page (~chatnpc_anim) lands
        -- its reply in exactly this window and drain read "chatmenu has no
        -- continue seam" on what was really a clean stop at "options".
        local settled_kind = QD.chat.kind()
        if settled_kind == stop_at then
            return "ok", settled_kind
        end
        if settled_kind == "none" or settled_kind == "count" or settled_kind == "name"
            or settled_kind == "other_input" then
            return "ok", settled_kind
        end

        -- The settle's own baseline is the page that is up RIGHT NOW --
        -- read after the shot's pump, not before it, so a remount that
        -- landed mid-pump does not make the settle below resolve on the
        -- change it has already seen.
        local identity = QD.chat._page_identity(settled_kind)

        local r, d = QD.chat.continue_()
        if r == "closed" then
            return "ok", "closed"
        end
        if r ~= "ok" then
            return r, d
        end

        -- continue_'s own "ok" can land before the server's reply actually
        -- remounts anything (this function's own banner above) -- settle on
        -- the mount really changing before the next iteration trusts `kind`
        -- again.
        QD.chat._settle_change(settled_kind, identity, 6)
    end
end

-- Idempotent (already-clear answers ok immediately): the level predicate
-- below is already true before any await registers when nothing is open, so
-- api.drive.await resolves it without ever yielding (EDGE + LEVEL). Also
-- cancels an outstanding count/name prompt -- ToriRSServer_WorldCloseModalEx
-- aborts those server-side, and the same level predicate (mode back to
-- none/other_input-cleared) observes it.
function QD.chat.close()
    local function closed_now()
        local group_res = api_drive.modal_group()
        if group_res == "ok" then
            return false
        end
        local mode_res, mode = api_drive.meslayer_mode()
        if mode_res == "ok" and QD.chat._meslayer_bucket(mode) ~= "none" then
            return false
        end
        return true
    end

    if closed_now() then
        return "ok", "chat.close: already closed"
    end

    -- Named for the hollow rule (trap 12): the caller wants to know what it
    -- closed, not just that something did.
    local before_kind = QD.chat.kind()

    local close_res, close_detail = api_drive.close_modal()
    if close_res ~= "ok" then
        return close_res, close_detail
    end

    local await_res, await_detail = await({ level = closed_now, note = "chat.close" }, 6)
    if await_res ~= "ok" then
        return await_res, await_detail
    end
    return "ok", "chat.close: closed " .. tostring(before_kind)
end

-- The osrs239 "dialog" for a count/name prompt is chat_input
-- (chatbox:input), disambiguated only by the meslayer mode VarC -- refuse to
-- type blind, or the digits/letters land in ordinary chat as a public
-- message (plan 5.4).
function QD.chat.count(n)
    local mode_res, mode = api_drive.meslayer_mode()
    if mode_res ~= "ok" then
        return mode_res, "chat.count: meslayer_mode"
    end
    if mode ~= 7 and mode ~= 19 then
        return "unsupported", "chat.count: not a quantity prompt (mode=" .. tostring(mode) .. ")"
    end

    local text_res, text_detail = QD.text(tostring(n))
    if text_res ~= "ok" then
        return text_res, text_detail
    end
    local key_res, key_detail = QD.key("enter")
    if key_res ~= "ok" then
        return key_res, key_detail
    end

    local await_res, await_detail = await({
        level = function()
            local m2, mode2 = api_drive.meslayer_mode()
            return m2 == "ok" and mode2 ~= 7 and mode2 ~= 19
        end,
        note = "chat.count",
    }, 6)
    if await_res ~= "ok" then
        return await_res, await_detail
    end
    return "ok", "chat.count: entered " .. tostring(n)
end

function QD.chat.name_entry(text)
    local mode_res, mode = api_drive.meslayer_mode()
    if mode_res ~= "ok" then
        return mode_res, "chat.name_entry: meslayer_mode"
    end
    if mode ~= 8 then
        return "unsupported", "chat.name_entry: not a name prompt (mode=" .. tostring(mode) .. ")"
    end

    local text_res, text_detail = QD.text(text)
    if text_res ~= "ok" then
        return text_res, text_detail
    end
    local key_res, key_detail = QD.key("enter")
    if key_res ~= "ok" then
        return key_res, key_detail
    end

    local await_res, await_detail = await({
        level = function()
            local m2, mode2 = api_drive.meslayer_mode()
            return m2 == "ok" and mode2 ~= 8
        end,
        note = "chat.name_entry",
    }, 6)
    if await_res ~= "ok" then
        return await_res, await_detail
    end
    return "ok", "chat.name_entry: entered '" .. tostring(text) .. "'"
end

-- api.drive.options reads dialog_options_title/_row_N directly (verbs-chat's
-- own C, see torirs_plugin_drive_chat.c): ready only once rows 1 and 2 both
-- resolve -- RUNCLIENTSCRIPT is held to the tick fence (app_cs2_flush.c:71,
-- drained at rs_gameproto_exec.c:2194), so the container can be mounted and
-- still empty for a frame, and row 1 alone can answer mid-rebuild.
function QD.chat.options()
    local result, options = api_drive.options()
    if result ~= "ok" then
        return result, "chat.options: rows not ready"
    end
    return "ok", options.rows
end

function QD.chat.options_title()
    local result, options = api_drive.options()
    if result ~= "ok" then
        return result, "chat.options_title: title not ready"
    end
    return "ok", options.title
end

-- selector is a 1-based row index or an exact row text (plan 5.5). Rows are
-- cc_create'd with no cache op and no button type: a real click builds
-- RESUME_PAUSEBUTTON with action_index -1, through api.drive.option_row's
-- identity seam.
function QD.chat.choose(selector)
    local options_res, options = api_drive.options()
    if options_res ~= "ok" then
        return options_res, "chat.choose: rows not ready"
    end
    local rows = options.rows
    local title = options.title

    -- Three selector forms (plan 5.5 + the chat.play pattern form): a
    -- 1-based row index, an exact row text, or a "/lua pattern/" string
    -- (leading and trailing "/", the body matched against each row's text
    -- with string.find, first row wins) -- for a row whose text varies by
    -- content (an item name, a quantity) where the author cannot spell it
    -- exactly.
    local row_index = nil
    if type(selector) == "number" then
        if rows[selector] ~= nil then
            row_index = selector
        end
    elseif type(selector) == "string" and #selector >= 2
        and selector:sub(1, 1) == "/" and selector:sub(-1) == "/" then
        local pattern = selector:sub(2, -2)
        for i, text in ipairs(rows) do
            if text:find(pattern) then
                row_index = i
                break
            end
        end
    else
        for i, text in ipairs(rows) do
            if text == selector then
                row_index = i
                break
            end
        end
    end
    if not row_index then
        return "no_row", "chat.choose: " .. tostring(selector)
    end

    local pending_res, pending_id = api_drive.pause_pending()
    if pending_res == "ok" and pending_id >= 0 then
        return "refused", "chat.choose: a resume is already outstanding"
    end

    local com_res, com_id = api_drive.option_row(row_index)
    if com_res ~= "ok" then
        return com_res, "chat.choose: row " .. tostring(row_index)
    end

    -- Resolved before resume(), same as chat.continue_ (QD-03: sub=-1 names
    -- the host component itself, not a child of it).
    local host_res, host_id = api_drive.component("chatbox:chatmodal", -1)

    local resume_res, resume_detail = api_drive.resume(com_id)
    if resume_res ~= "ok" then
        return resume_res, resume_detail
    end

    -- QD-06: resume_answered is the CLIENT's own ack of the click -- it
    -- fires the same frame the click drains, well before the server's own
    -- reply remounts anything (see chat.continue_'s banner above). Ending
    -- the wait on it meant classify() below ran while the old chatmenu was
    -- still mounted unchanged, so every successful choose read back its OWN
    -- pre-click snapshot and was misclassified "refused, stale reopen".
    -- Release on the server's reply instead -- a fresh mount or a close
    -- under chat_modal_host -- or on the deadline, and only then classify.
    local await_result, await_detail = await({
        match = function(ev)
            if host_res == "ok" and (ev.kind == "sub_mounted" or ev.kind == "sub_closed") then
                return ev.a == host_id
            end
            return false
        end,
        note = "chat.choose",
    }, 5)
    if await_result ~= "ok" and await_result ~= "timeout" then
        return await_result, await_detail
    end

    -- Classify: gone or changed = ok; identical title+rows = a stale reopen
    -- the server replayed (chat.rs2:240-263) = refused. This runs whether
    -- the release above was the server's reply or the deadline -- a
    -- deadline with the rows still exactly as sent IS the stale-reopen
    -- shape (CC_DELETEALL rebuilds the same group in place, so no fresh
    -- chat_modal_host mount fires to release the await early on that path).
    local now_kind = QD.chat.kind()
    if now_kind ~= "options" then
        return "ok", "chat.choose: page changed"
    end

    local now_options_res, now_options = api_drive.options()
    if now_options_res ~= "ok" then
        return "ok", "chat.choose: page changed"
    end
    local now_rows = now_options.rows
    local now_title = now_options.title

    local identical = now_title == title and #now_rows == #rows
    if identical then
        for i, text in ipairs(rows) do
            if now_rows[i] ~= text then
                identical = false
                break
            end
        end
    end
    if identical then
        return "refused", "stale reopen"
    end
    return "ok", "chat.choose: options changed"
end

-- ---------------------------------------------------------------- chat.play
--
-- A scripted walk over a whole conversation: an author states the pages the
-- dialogue is expected to show, in order, instead of hand-writing a
-- drain/choose pair per page and a shot per step (README's own broken
-- example -- test/quests/README.md, replaced by phase 3 -- was exactly that
-- hand-written shape, and forgetting the shot is the failure mode plan
-- section 5 calls out). Every entry is read against whatever the PREVIOUS
-- entry's own action already settled onto -- continue_/choose/count/
-- name_entry each already await their own fresh mount -- so chat.play itself
-- never calls `await`; it only reads chat.kind()/chat.text()/chat.options(),
-- all synchronous.
--
-- Entries (see docs/QUEST_SUITE_KIT.md's phase 2 table):
--   "npc:<substr>"     current page must be kind "npc", text contains
--                       substr; then continue_.
--   "player:<substr>"  same, kind "player".
--   "mesbox:<substr>"  same, kind "mesbox".
--   "options"          current page must be kind "options"; a bare check --
--                       it does not click anything, the NEXT entry (a
--                       "choose:") does. Two "options" in a row past the
--                       first is a stuck-page bug in the LIST, same as a
--                       missing "choose:" -- chat.play does not guess a row.
--   "choose:<sel>"     kind must be "options"; <sel> is passed straight to
--                       chat.choose (exact row text, 1-based index text is
--                       NOT accepted here -- only the string forms -- or a
--                       "/lua pattern/", chat.choose's own third form).
--   "*"                any ONE page of kind npc/player/mesbox/objbox (the
--                       kinds continue_ has a seam for); continue_'d with no
--                       text check. Landing on options/count/name/none is a
--                       mismatch here -- there is nothing generic to click.
--   "npc:*"/"player:*"/"mesbox:*"  same wildcard, but pinned to that one
--                       kind (unlike bare "*", which accepts any of the
--                       four) -- for a page whose text is not worth spelling
--                       but whose KIND the author still wants checked.
--   "count:<n>"        kind must be "count"; chat.count(n).
--   "name:<text>"      kind must be "name"; chat.name_entry(text).
--   "end"              kind must be "none" (the dialogue already closed);
--                       terminal, no action.
--
-- Fails on the first mismatch, returning ("mismatch", detail) with the page
-- kind and (where the kind carries one) its text in detail; a verb called
-- along the way (continue_/choose/count/name_entry) that itself answers
-- something other than "ok" propagates that exact (result, detail) instead
-- of being reworded into "mismatch" -- the caller sees whichever failure
-- actually happened.
--
-- Succeeds with ("ok", "<N> page(s): kind:fragment, ...") -- one
-- "kind:fragment" per entry matched, in order, the fragment being the first
-- ~30 characters of the page's own text where an entry read one (npc/
-- player/mesbox, tags stripped) or the entry itself otherwise (choose/
-- count/name/end/any), the whole joined string capped at ~200 characters.
-- Never ("ok", nil) -- chat.play used to be the hollow rule's own textbook
-- case (QUEST_AUTHORING.md trap 12): a verb whose successful answer IS
-- legitimately informative cannot go through t.exec unless it says so.
--
-- Screenshots: each entry gets exactly one shot, taken as soon as it starts
-- (so a mismatch still leaves a picture of the page that did not match),
-- named "<kind>-p<N>" where N is this call's own 1-based page ordinal and
-- <kind> is the entry's EXPECTED kind ("npc", "player", "mesbox", "options"
-- for both a bare "options" and a "choose:", "count", "name", "none" for
-- "end", "any" for "*") -- e.g. "npc-p1", "options-p2", "npc-p3". The
-- filename's actual ordering prefix ("NN-") is QD.core_next_shot's own
-- run-wide counter, reached the only way any part file reaches it: through
-- QD.shot. chat.play supplies a distinguishing suffix, never a second
-- counter of its own.
QD.chat._play_kind_by_prefix = {
    npc = "npc",
    player = "player",
    mesbox = "mesbox",
    count = "count",
    name = "name",
}

-- One chat.play entry -> {kind=, action=, arg=}, or nil, detail on a
-- malformed entry ("unsupported" at the call site -- a bad LIST is not a
-- mismatch against a live page, it is the test itself being unrunnable).
function QD.chat._play_parse(entry)
    if entry == "options" then
        return { kind = "options", action = "expect" }
    end
    if entry == "*" then
        return { kind = "any", action = "any" }
    end
    if entry == "end" then
        return { kind = "none", action = "end" }
    end

    local colon = entry:find(":", 1, true)
    if not colon then
        return nil, "chat.play: malformed entry '" .. tostring(entry) .. "'"
    end
    local prefix = entry:sub(1, colon - 1)
    local arg = entry:sub(colon + 1)

    if prefix == "choose" then
        return { kind = "options", action = "choose", arg = arg }
    end

    local kind = QD.chat._play_kind_by_prefix[prefix]
    if not kind then
        return nil, "chat.play: unknown entry '" .. tostring(entry) .. "'"
    end
    if kind == "count" then
        local n = tonumber(arg)
        if not n then
            return nil, "chat.play: count: needs a number, got '" .. tostring(arg) .. "'"
        end
        return { kind = "count", action = "count", arg = n }
    end
    if kind == "name" then
        return { kind = "name", action = "name", arg = arg }
    end
    return { kind = kind, action = "text", arg = arg }
end

-- What the live page currently is, for a mismatch's detail -- the kind, plus
-- its text (npc/player/mesbox) or its rows (options), read fresh so the
-- detail shows what actually mounted, not the entry that expected something
-- else.
function QD.chat._play_describe(kind)
    if kind == "npc" or kind == "player" or kind == "mesbox" then
        local text_res, text = QD.chat.text()
        return kind .. " text=" .. (text_res == "ok" and ("'" .. tostring(text) .. "'") or tostring(text))
    end
    if kind == "options" then
        local opt_res, rows = QD.chat.options()
        return "options rows=" .. (opt_res == "ok" and table.concat(rows, "|") or tostring(rows))
    end
    return kind
end

function QD.chat.play(list)
    local summary = {}

    for index, entry in ipairs(list) do
        local parsed, parse_detail = QD.chat._play_parse(entry)
        if not parsed then
            return "unsupported", parse_detail
        end

        -- Same race chat.drain's own banner documents (measured 2026-09-19:
        -- the cook's Talk-to page lands its reply during exactly this
        -- window): continue_/choose/count/name_entry can all return "ok" on
        -- the CLIENT's own resume_answered ack, well before the server's
        -- reply actually remounts the next page. QD.shot pumps real
        -- frames while it waits for its capture, and the previous entry's
        -- click can land during exactly that pump -- so `kind` is read
        -- AFTER the shot, never before it, or this entry grades the page
        -- that was still closing, not the one that is actually live.
        QD.shot(parsed.kind .. "-p" .. index)
        local actual_kind = QD.chat.kind()
        -- Overridden below for a "text" action that actually reads one;
        -- everything else summarises as the LIST entry it matched, capped
        -- the same 30 characters (this file's own banner above).
        local fragment = entry:sub(1, 30)

        if parsed.action == "expect" or parsed.action == "text" then
            if actual_kind ~= parsed.kind then
                return "mismatch", "chat.play: entry " .. index .. " ('" .. entry
                    .. "') expected kind=" .. parsed.kind .. ", got " .. QD.chat._play_describe(actual_kind)
            end
            if parsed.action == "text" then
                local text_res, text = QD.chat.text()
                if text_res ~= "ok" then
                    return "mismatch", "chat.play: entry " .. index .. " ('" .. entry
                        .. "') text unread (" .. tostring(text_res) .. "): " .. tostring(text)
                end
                local stripped = QD.read._strip_tags(text)
                -- "npc:*"/"player:*"/"mesbox:*": the same wildcard bare "*"
                -- is, pinned to one kind (this file's own banner above) --
                -- kind already checked above, so skip the substring check
                -- rather than searching the page for a literal "*".
                if parsed.arg ~= "*" and not stripped:find(parsed.arg, 1, true) then
                    return "mismatch", "chat.play: entry " .. index .. " ('" .. entry .. "') "
                        .. actual_kind .. " text does not contain '" .. parsed.arg
                        .. "' -- text='" .. tostring(text) .. "'"
                end
                fragment = stripped:sub(1, 30)
                local r, d = QD.chat.continue_()
                if r ~= "ok" then
                    return r, "chat.play: entry " .. index .. " continue_ -- " .. tostring(d)
                end
            end

        elseif parsed.action == "choose" then
            if actual_kind ~= "options" then
                return "mismatch", "chat.play: entry " .. index .. " ('" .. entry
                    .. "') expected options, got " .. QD.chat._play_describe(actual_kind)
            end
            local r, d = QD.chat.choose(parsed.arg)
            if r ~= "ok" then
                return r, "chat.play: entry " .. index .. " ('" .. entry .. "') choose -- " .. tostring(d)
            end

        elseif parsed.action == "count" then
            if actual_kind ~= "count" then
                return "mismatch", "chat.play: entry " .. index .. " ('" .. entry
                    .. "') expected count, got " .. QD.chat._play_describe(actual_kind)
            end
            local r, d = QD.chat.count(parsed.arg)
            if r ~= "ok" then
                return r, "chat.play: entry " .. index .. " ('" .. entry .. "') count -- " .. tostring(d)
            end

        elseif parsed.action == "name" then
            if actual_kind ~= "name" then
                return "mismatch", "chat.play: entry " .. index .. " ('" .. entry
                    .. "') expected name, got " .. QD.chat._play_describe(actual_kind)
            end
            local r, d = QD.chat.name_entry(parsed.arg)
            if r ~= "ok" then
                return r, "chat.play: entry " .. index .. " ('" .. entry .. "') name_entry -- " .. tostring(d)
            end

        elseif parsed.action == "end" then
            if actual_kind ~= "none" then
                return "mismatch", "chat.play: entry " .. index .. " ('end') expected no dialogue, got "
                    .. QD.chat._play_describe(actual_kind)
            end

        elseif parsed.action == "any" then
            if actual_kind == "options" or actual_kind == "count" or actual_kind == "name"
                or actual_kind == "none" then
                return "mismatch", "chat.play: entry " .. index
                    .. " ('*') cannot blindly continue past " .. QD.chat._play_describe(actual_kind)
            end
            local r, d = QD.chat.continue_()
            if r ~= "ok" then
                return r, "chat.play: entry " .. index .. " ('*') continue_ -- " .. tostring(d)
            end
        end

        summary[#summary + 1] = parsed.kind .. ":" .. fragment
    end

    local joined = table.concat(summary, ", ")
    if #joined > 200 then
        joined = joined:sub(1, 200)
    end
    return "ok", #summary .. " page(s): " .. joined
end
