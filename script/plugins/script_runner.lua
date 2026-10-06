-- The Scripts tab: every automated script in the tree, and Play to watch one.
--
-- A sidebar page for a WATCHED client (profiles/osrs239-scripts.ini): a suite
-- filter, a search box, Play and Stop, a status block that follows the
-- running script, and a window of the scripts that match. Play hands the
-- chosen test to the quest driver through api.drive.play, which exists only
-- in a client started with TORIRS_DRIVE_ON_DEMAND=1
-- (src/plugin/torirs_plugin_drive.c). In any other client the page says so
-- and Play stays off.
--
-- THE LIST IS ASKED FOR, the way the plugin host asks for its plugins:
-- api.drive.tests() reads the scripts manifest tests/tests.ini through the IO
-- layer as one SCRIPT item (natively the filesystem under script/, on the
-- browser lane the served script directory). The launcher writes that file on
-- every `./launch run osrs239-scripts` from every tests directory
-- (tools/raid_gate/prepare_scripts.py); Refresh asks again. Play asks for the
-- chosen test's source and fixture the same way, again on every Play, so an
-- edited test runs on the next Play without a restart.
--
-- Panel engine rules this page keeps:
--   * the ROW SET NEVER CHANGES: the controls, the status block, LIST_WINDOW
--     list slots and a "more" row are declared once; the filter, the search
--     and the driver status change only their text (set_text / set_label /
--     set_value). A rebuild re-creates the search box and the keyboard leaves
--     it (seam23 measured 'maid' typing 'aid' into the chat), so with no
--     rebuild the list can follow every keystroke and the box keeps focus;
--   * no row's text exceeds TEXT_LIMIT (the host drops a row over 192);
--   * nothing walks the UI tree; the filter runs over the manifest (about 130
--     entries) only when the filter or the search moved, and the driver status
--     is read every STATUS_INTERVAL_FRAMES frames; only rows whose text moved
--     are touched.
--
-- A watched run is not a test run: it uses the wall clock and its ledger
-- grades nothing (docs/minigames/raid_loop/DRIVER_NOTES.md, "Watching a test:
-- the Scripts tab").
--
-- TWO VIEWS (camera seam2): while a script plays here it has its own camera
-- (the AutomationRunner view) and yours is left alone. The page carries the
-- INTERACT switch (api.drive.view_interact: off when a script starts -- your
-- mouse and keys only orbit, zoom and inspect; on -- they play the game, and
-- every action is a watcher.* ledger row), and two readings: "Under your
-- pointer" (your own pick: api.input.hover_entity / hover_tile, which answer
-- for the presented view) and "Runner pointer" (api.drive.view_status's
-- runner block). The on-frame badge, the runner's ghost cursor and the
-- outline of what it pressed are drawn by the client itself
-- (src/app/app_overlay.c, "THE WATCHER'S AIDS"), so they cost nothing when no
-- script is attached and cannot be covered by any plugin's drawing.
--
-- PARTY ROWS (raid seam37, scripts_tab_party_play). A test that declares
-- `party = N` is played by THIS client as raider 1 while the client's own
-- embedded IO server starts raiders 2..N (src/platform/launch_sessions.h):
-- the same binary and manifest, without the embedded options, each joining
-- this client's world over the party link. Play passes api.drive.play a
-- `party = {size = N, launch = true, windowed = {...}}`; the driver logs in,
-- opens the launch session, spawns the members (headless unless their
-- Windowed tick is on), hosts the party and plays role 1
-- (quest_driver/raid.lua QD.launch._party_up). A party row is playable when
-- the launch service answers here (one probe at start: `launch/status` of no
-- session answers "no such session" where the service runs, "unsupported"
-- with the reason on web, Android, iOS and Windows) and the client is
-- frame-locked (TORIRS_MAX_FRAMES: a party's frames each pay one logic
-- cycle, and the party host refuses otherwise). The PARTY block reads
-- `launch/status` of the session the driver opened
-- (api.drive.party().launch_session/_token) every PARTY_INTERVAL_FRAMES
-- frames: per seat its process (pid, up or how it ended) and the status it
-- last posted (state, step, rows, hitpoints, alive). Stop all sends `stop` to
-- every member and stops raider 1; the members stay logged in. Respawn closes
-- the party and plays the row again with new members (a member cannot rejoin
-- a lock step that is already running). The session is CLOSED (quit, the
-- grace, then the kill) on Stop, on the next Play, when this client logs out,
-- when the plugin stops, and by the service itself when this process exits.

---@type torirs.Plugin
local plugin = { id = "script-runner", title = "Scripts", version = "2" }

local TEXT_LIMIT = 180
local STATUS_INTERVAL_FRAMES = 10
local LIST_WINDOW = 12
local SUITES = {
    { value = "all", label = "All" },
    { value = "quest", label = "Quests" },
    { value = "raid", label = "Raids" },
}
local SUITE_ORDER = { quest = 1, raid = 2 }

-- Raid seam25: where a Play starts from (the owner, 2026-10-05: "The script
-- runner should also specify a starting character state, such as 'reset
-- character' - clear inventory, clear worn items, clear effects").  Reset is
-- the default; a manifest row may name its own (`start=reset|fresh|as_is`),
-- which the select shows and the owner can still change before Play.
local STARTS = {
    { value = "reset", label = "Reset character" },
    { value = "fresh", label = "Fresh character" },
    { value = "as_is", label = "As it is" },
}
local START_LABEL = { reset = "Reset character", fresh = "Fresh character", as_is = "As it is" }
local start_choice = "reset"

-- The manifest, as parsed: array of {id, suite, title, source, fixture, legs,
-- party, max_frames, available, reason}, sorted by suite then id.
local scripts = {}
local manifest_state = "none"   -- none | pending | ready | missing
local manifest_detail = "Asking for the scripts manifest..."
local suite_filter = "all"
local search_text = ""
local matched = {}              -- the scripts the filter and search let through
local slot_script = {}          -- slot index -> script shown there (nil when blank)
local selected = nil
local note = ""
local frames = 0
local shown = {}                -- row id -> text last written by set_text
local shown_label = {}          -- row id -> label last written by set_label
local shown_enabled = {}        -- button id -> last enabled flag written
local shown_driver_logged = nil
local shown_watch_logged = nil
local shown_party_logged = nil
local page_built = false
-- "Under your pointer": the last hover asked about and its words, so the
-- entity pools are walked only when what is under the pointer changes.
local hover_key = nil
local hover_words = "-"
local HOVER_KINDS = { [1] = "loc", [2] = "npc", [3] = "player", [4] = "obj" }
local HOVER_WALK_LIMIT = 20000

-- The PARTY block (see the banner). PARTY_SEATS is the launch service's
-- LAUNCH_SESSIONS_PARTY_MAX: a row is declared for every seat so the row set
-- never changes.
local PARTY_SEATS = 4
local PARTY_INTERVAL_FRAMES = 30
-- Status reads in a row with no local player before a logout closes the
-- party (a Fresh start's own relog happens before the party is up).
local PARTY_LOGOUT_READS = 3
-- The launch service probe: none | pending | yes | no, and why not.
local launch_service = { state = "none", reason = "asking this client's launch service" }
local windowed = { false, false, false, false }   -- seat -> its Windowed tick (seat 1 unused)
local party_tickets = {}        -- launch tickets queued here: {ticket =, purpose =}
local party_status = nil        -- the last launch/status read: {session = row, seats = {[n] = row}}
local party_status_note = "no party"
local party_status_pending = false
local party_logout_reads = 0
local party_last_play = nil     -- the party row the last Play started (Respawn plays it again)
-- The last session seen open, read on after its close until no member is
-- up, so the block ends on each member's exit instead of its last "up".
local party_known = nil         -- {session =, token =, settled = false}
-- This process hosted a party already. The embedded transport keeps a
-- runtime-hosted party's link until the client exits
-- (torirs_server_embed.c ToriRSServer_EmbedPartyHostRequest refuses a second
-- host: measured, seam37 runB, launch.party FAIL "this client already hosts a
-- party"), so a second party Play in one client cannot host: the rows say so
-- instead of failing a row.
local party_hosted = false
local PARTY_REHOST_REASON = "this client already hosted a party; the embedded transport keeps that link "
    .. "until the client exits: restart the client to play another party"
local list_stale = false        -- the party gate moved: the list's "unavailable" words follow

local function clip(text)
    text = tostring(text or "")
    if #text > TEXT_LIMIT then
        return text:sub(1, TEXT_LIMIT - 3) .. "..."
    end
    return text
end

local function slot_id(index)
    return "slot" .. index
end

-- tests.ini: `[test:<id>]` sections of `key=value` lines; `;` starts a comment.
local function parse_manifest(text)
    local parsed = {}
    local current = nil
    for line in text:gmatch("[^\r\n]+") do
        local stripped = line:match("^%s*(.-)%s*$")
        if stripped ~= "" and stripped:sub(1, 1) ~= ";" then
            local section = stripped:match("^%[test:(.+)%]$")
            if section then
                current = { id = section, suite = "", title = "", source = "", fixture = "",
                    legs = 0, party = 1, max_frames = 0, available = false, reason = "", start = "" }
                parsed[#parsed + 1] = current
            elseif stripped:sub(1, 1) == "[" then
                current = nil
            elseif current then
                local key, value = stripped:match("^([%w_]+)%s*=%s*(.*)$")
                if key == "legs" or key == "party" or key == "max_frames" then
                    current[key] = math.tointeger(tonumber(value)) or 0
                elseif key == "available" then
                    current.available = value == "1"
                elseif key then
                    current[key] = value
                end
            end
        end
    end
    for _, script in ipairs(parsed) do
        if script.source == "" or script.fixture == "" then
            script.available = false
            if script.reason == "" then
                script.reason = "the manifest names no source or fixture"
            end
        end
    end
    table.sort(parsed, function(a, b)
        local order_a = SUITE_ORDER[a.suite] or 99
        local order_b = SUITE_ORDER[b.suite] or 99
        if order_a ~= order_b then
            return order_a < order_b
        end
        return a.id < b.id
    end)
    return parsed
end

local function manifest_counts()
    local by_suite = {}
    for _, script in ipairs(scripts) do
        local entry = by_suite[script.suite]
        if not entry then
            entry = { listed = 0, available = 0 }
            by_suite[script.suite] = entry
        end
        entry.listed = entry.listed + 1
        if script.available then
            entry.available = entry.available + 1
        end
    end
    local parts = {}
    for _, suite in ipairs({ "quest", "raid" }) do
        local entry = by_suite[suite]
        if entry then
            parts[#parts + 1] = string.format("%s %d (%d playable)", suite, entry.listed, entry.available)
        end
    end
    return table.concat(parts, ", ")
end

local function matches(script)
    if suite_filter ~= "all" and script.suite ~= suite_filter then
        return false
    end
    if search_text == "" then
        return true
    end
    local wanted = search_text:lower()
    return (script.id:lower():find(wanted, 1, true) ~= nil) or
           (script.title:lower():find(wanted, 1, true) ~= nil)
end

local function refilter()
    matched = {}
    for _, script in ipairs(scripts) do
        if matches(script) then
            matched[#matched + 1] = script
        end
    end
end

-- Whether this client can play a party at all (see the banner): the launch
-- service answered the probe, and the client is frame-locked. Returns ok and,
-- when not, why.
local function party_gate(api)
    if api.drive == nil or api.drive.launch_status == nil or api.drive.party == nil then
        return false, "this client's driver has no launch channel"
    end
    if launch_service.state ~= "yes" then
        return false, launch_service.reason
    end
    local result, info = api.drive.party()
    if result ~= "ok" or type(info) ~= "table" then
        return false, "drive.party answered " .. tostring(result)
    end
    if not info.frame_locked then
        return false, "a party plays in lock step: start this client frame-locked "
            .. "(TORIRS_MAX_FRAMES=2000000000 TORIRS_EMBED_CLOCK_MS=20)"
    end
    if party_hosted then
        return false, PARTY_REHOST_REASON
    end
    return true, ""
end

-- Whether Play can start `script` here, and why not.
local function script_playable(api, script)
    if not script.available then
        return false, script.reason
    end
    if script.party > 1 then
        local ok, reason = party_gate(api)
        if not ok then
            return false, string.format("party of %d: %s", script.party, reason)
        end
    end
    return true, ""
end

-- What one list slot says: the id (and "unavailable"), then the title or the
-- reason, and "selected" on the script Play would start.
local function slot_texts(api, script)
    if not script then
        return " ", " "
    end
    local label = script.id
    local summary = script.title ~= "" and script.title or script.id
    local playable, reason = script_playable(api, script)
    if not playable then
        label = script.id .. "  (unavailable)"
        summary = reason
    elseif script.legs > 0 then
        summary = summary .. string.format("  [%d legs]", script.legs)
    end
    if playable and script.party > 1 then
        summary = summary .. string.format("  [party of %d]", script.party)
    end
    if selected and selected.id == script.id then
        summary = "selected: " .. summary
    end
    return clip(label), clip(summary)
end

local function matched_text(api)
    if manifest_state ~= "ready" then
        return clip(manifest_detail)
    end
    local suite_word = suite_filter == "all" and "scripts" or (suite_filter == "quest" and "quests" or "raids")
    local in_suite = 0
    for _, script in ipairs(scripts) do
        if suite_filter == "all" or script.suite == suite_filter then
            in_suite = in_suite + 1
        end
    end
    local playable = 0
    for _, script in ipairs(matched) do
        if script_playable(api, script) then
            playable = playable + 1
        end
    end
    if search_text == "" then
        return clip(string.format("%d %s, %d playable.", #matched, suite_word, playable))
    end
    return clip(string.format("%d of %d %s match '%s' (%d playable)", #matched, in_suite, suite_word,
        search_text, playable))
end

local function more_text()
    if #matched > LIST_WINDOW then
        return string.format("%d more: refine the search", #matched - LIST_WINDOW)
    end
    if manifest_state == "ready" and #matched == 0 then
        return clip("No script matches '" .. search_text .. "'.")
    end
    return " "
end

-- What the status block and the buttons say right now.
-- The snapshot of the entity under the watcher's pointer: walk the one pool
-- its kind names until the element id matches (bounded; only on a change).
local function hover_snapshot(api, kind, element_id)
    local walker = ({ [1] = api.world.scenery_next, [2] = api.world.npc_next,
        [3] = api.world.player_next, [4] = api.world.item_next })[kind]
    if walker == nil then
        return nil
    end
    local cursor = -1
    for _ = 1, HOVER_WALK_LIMIT do
        local next_cursor, snap = walker(cursor)
        if not next_cursor then
            return nil
        end
        cursor = next_cursor
        if snap and snap.element_id == element_id then
            return snap
        end
    end
    return nil
end

-- "Under your pointer": kind, name, config id and tile of what the WATCHER's
-- own pick holds (the presented view; with no script attached, the only one).
local function under_you_text(api)
    if api.input == nil or api.input.hover_entity == nil then
        return "-"
    end
    local hover = api.input.hover_entity()
    local tile_x, tile_z, level = api.input.hover_tile()
    local key
    if hover then
        key = string.format("e%d:%d:%d,%d", hover.kind, hover.element_id, hover.tile_x, hover.tile_z)
    elseif tile_x then
        key = string.format("t%d,%d,%d", tile_x, tile_z, level or 0)
    else
        key = "none"
    end
    if key == hover_key then
        return hover_words
    end
    hover_key = key
    if hover then
        local kind = HOVER_KINDS[hover.kind] or ("kind " .. tostring(hover.kind))
        local snap = hover_snapshot(api, hover.kind, hover.element_id)
        local name = snap and snap.name or "?"
        local id = snap and (snap.npc_id or snap.loc_id or snap.obj_id or snap.server_pid)
        hover_words = string.format("%s %s%s at %d,%d level %d", kind, name,
            id and (" (id " .. tostring(id) .. ")") or "", hover.tile_x, hover.tile_z, hover.level)
    elseif tile_x then
        hover_words = string.format("tile %d,%d level %d, nothing on it", tile_x, tile_z, level or 0)
    else
        hover_words = "nothing (not over the world)"
    end
    return hover_words
end

-- The runner/watcher split's readings: the switch, the badge's words and the
-- runner's pointer. `views` is nil when the driver has no view verbs.
local function watch_view(api)
    local watch = {
        attached = false, interact = false,
        control = "no script attached: your mouse and keys play the game",
        runner = "-",
    }
    if api.drive == nil or api.drive.view_status == nil then
        watch.control = "unavailable: this client has no driver"
        return watch
    end
    local result, status = api.drive.view_status()
    if result ~= "ok" or type(status) ~= "table" then
        watch.control = "unavailable: drive.view_status answered " .. tostring(result)
        return watch
    end
    watch.attached = status.attached and true or false
    watch.interact = status.interact and true or false
    if watch.attached then
        watch.control = watch.interact and "You can interact: your clicks and keys play the game (ledger rows)"
            or "Runner has control: your mouse orbits, zooms and inspects only"
    elseif status.lane_refusal then
        watch.control = "one view: " .. tostring(status.lane_refusal)
    end
    local runner = status.runner
    if type(runner) == "table" then
        watch.runner = string.format("%d,%d  picked %d%s  camera yaw %d pitch %d zoom %d",
            runner.pointer_x or -1, runner.pointer_y or -1, runner.picked or 0,
            runner.menu_open and "  menu open" or "", runner.yaw or 0, runner.pitch or 0, runner.zoom or 0)
        if not watch.attached then
            watch.runner = "(one view) " .. watch.runner
        end
    end
    return watch
end

local function status_view(api)
    local view = {
        driver = "", test = "-", leg = "-", step = "-", counts = "-", summary = "-", session = "-",
        play = false, stop = false,
    }
    if api.drive == nil or api.drive.status == nil then
        view.driver = "unavailable: start this client with ./launch run osrs239-scripts"
        return view
    end
    local result, status = api.drive.status()
    if result ~= "ok" or type(status) ~= "table" then
        view.driver = "unavailable: drive.status answered " .. tostring(result)
        return view
    end
    if not status.on_demand then
        view.driver = "unavailable: a test run owns the driver (TORIRS_DRIVE_ON_DEMAND is unset)"
        return view
    end
    local name = status.id ~= "" and status.id or ((status.script or ""):match("([^/]*)%.lua$") or "")
    if status.state == "running" then
        view.driver = "running " .. name .. (status.stopping and " (stopping)" or "")
            .. (status.starting and " (starting)" or "")
    elseif status.state == "finished" then
        view.driver = "finished " .. name
    elseif status.refusal and status.refusal ~= "" then
        view.driver = "refused: " .. status.refusal
    else
        view.driver = "idle"
    end
    if status.play and status.id ~= "" then
        view.test = string.format("%s %s on %s, start %s", status.suite ~= "" and status.suite or "test",
            status.id, status.account ~= "" and status.account or "?", status.start or "fresh")
        if status.legs and status.legs > 0 then
            view.leg = status.leg > 0 and string.format("leg %d of %d", status.leg, status.legs)
                or string.format("before leg 1 of %d", status.legs)
        end
    end
    if status.rows and status.rows > 0 then
        view.step = string.format("%d. %s %s", status.rows, status.step or "", status.verdict or "")
        view.counts = string.format("PASS %d  FAIL %d  BLOCKED %d", status.pass or 0, status.fail or 0,
            status.blocked or 0)
    end
    if status.summary and status.summary ~= "" then
        -- The ledger's SUMMARY row is tab-separated; a label draws a tab as nothing.
        view.summary = status.summary:gsub("\t", " ")
    end
    if status.session and status.session ~= "" then
        view.session = status.session
    end
    view.play = selected ~= nil and script_playable(api, selected) and status.state ~= "running"
    view.stop = status.state == "running" and not status.stopping
    view.running = status.state == "running"
    view.account = status.account or ""
    view.status = status
    return view
end

-- ===================================================================== party
-- The PARTY block (see the banner). Every launch call is one ticket on the
-- driver's launch channel; each is read back here (the driver holds 16).

-- The session this client's driver opened, or nil: {session =, token =}.
local function own_session(api)
    if api.drive == nil or api.drive.party == nil then
        return nil
    end
    local result, info = api.drive.party()
    if result ~= "ok" or type(info) ~= "table" then
        return nil
    end
    if (info.launch_session or "") == "" or (info.launch_token or "") == "" then
        return nil
    end
    return { session = info.launch_session, token = info.launch_token }
end

local function leader_lines(own)
    return "session=" .. own.session .. "\ntoken=" .. own.token .. "\n"
end

-- Queue one launch call (`verb` is the api.drive.launch_* name) and keep its
-- ticket. Returns ok and, when refused at once, why.
local function party_queue(api, purpose, verb, body)
    local queued, ticket = api.drive[verb](body)
    if queued ~= "ok" then
        api.core.log("script-runner: party " .. purpose .. " -> " .. tostring(queued) .. " " .. tostring(ticket))
        return false, tostring(ticket)
    end
    party_tickets[#party_tickets + 1] = { ticket = ticket, purpose = purpose }
    return true, ""
end

-- One `launch/status` line: its space-separated pairs; `status=` runs to the
-- end of the line and is split the same way.
local function party_line(line)
    local row = {}
    local head, posted = line:match("^(.-) status=(.*)$")
    if head == nil then
        head = line
    end
    for key, value in head:gmatch("([%w_]+)=(%S+)") do
        row[key] = value
    end
    row.status = {}
    for key, value in (posted or ""):gmatch("([%w_]+)=(%S+)") do
        row.status[key] = value
    end
    return row
end

-- The first line of an answer, without its `error: ` / `unsupported: ` word.
local function answer_reason(text)
    local line = tostring(text or ""):match("^[^\n]*") or ""
    return line:gsub("^error:%s*", ""):gsub("^unsupported:%s*", "")
end

local function party_answer(api, purpose, result, text)
    if purpose == "probe" then
        -- `error: no such session` is the service answering; `unsupported`
        -- names the platform's reason.
        if result == "refused" then
            launch_service.state = "yes"
            launch_service.reason = ""
        else
            launch_service.state = "no"
            launch_service.reason = "no launch service here: " .. answer_reason(text)
        end
        api.core.log("script-runner: launch service " .. launch_service.state .. " (" .. tostring(result)
            .. ": " .. answer_reason(text) .. ")")
        list_stale = true
        return
    end
    if purpose == "status" then
        party_status_pending = false
        if result ~= "ok" then
            party_status_note = "launch/status: " .. answer_reason(text)
            if party_known ~= nil then
                party_known.settled = true
            end
            return
        end
        local read = { seats = {} }
        for line in tostring(text):gmatch("[^\n]+") do
            if line:match("^session=") then
                read.session = party_line(line)
            elseif line:match("^seat=") then
                local row = party_line(line)
                local seat = math.tointeger(tonumber(row.seat))
                if seat then
                    read.seats[seat] = row
                end
            end
        end
        party_status = read
        local session = read.session or {}
        if party_known ~= nil and session.session == party_known.session and session.state ~= "open" then
            local up = false
            for _, row in pairs(read.seats) do
                if row.alive == "1" then
                    up = true
                end
            end
            party_known.settled = not up
        end
        party_status_note = string.format("session %s %s, port %s", tostring(session.session or "?"),
            tostring(session.state or "?"), tostring(session.port or "?"))
        return
    end
    -- close, stop_all
    party_status_note = purpose .. ": " .. (result == "ok" and (tostring(text):match("state=(%S+)") or "ok")
        or answer_reason(text))
    api.core.log("script-runner: party " .. purpose .. " -> " .. tostring(result) .. " "
        .. answer_reason(text))
end

local function party_drain(api)
    if #party_tickets == 0 then
        return
    end
    local kept = {}
    for _, entry in ipairs(party_tickets) do
        local result, text = api.drive.launch_answer(entry.ticket)
        if result == "timeout" then
            kept[#kept + 1] = entry
        else
            party_answer(api, entry.purpose, result, text)
        end
    end
    party_tickets = kept
end

-- Close the party the driver brought up (quit to every member, the grace,
-- then the kill). `why` goes to the log.
local function party_close(api, why)
    local own = own_session(api)
    if own == nil then
        return false
    end
    api.core.log("script-runner: party close (" .. why .. ") session " .. own.session)
    party_queue(api, "close", "launch_close", leader_lines(own))
    party_logout_reads = 0
    return true
end

-- Every PARTY_INTERVAL_FRAMES frames: the probe once, the session's status,
-- and the logout watch.
local function party_poll(api)
    if api.drive == nil or api.drive.launch_status == nil or api.drive.launch_answer == nil then
        return
    end
    if launch_service.state == "none" then
        launch_service.state = "pending"
        local ok, why = party_queue(api, "probe", "launch_status", "session=probe\ntoken=probe\n")
        if not ok then
            launch_service.state = "no"
            launch_service.reason = "no launch service here: " .. why
        end
    end
    local own = own_session(api)
    if own ~= nil and (party_known == nil or party_known.session ~= own.session) then
        party_known = { session = own.session, token = own.token, settled = false }
        if not party_hosted then
            party_hosted = true
            list_stale = true
        end
    end
    if own == nil then
        party_logout_reads = 0
        if party_known ~= nil and not party_known.settled and not party_status_pending then
            party_status_pending = party_queue(api, "status", "launch_status", leader_lines(party_known))
        end
        return
    end
    if not party_status_pending then
        party_status_pending = party_queue(api, "status", "launch_status", leader_lines(own))
    end
    if api.world ~= nil and api.world.local_player ~= nil and api.world.local_player() == nil then
        party_logout_reads = party_logout_reads + 1
        if party_logout_reads >= PARTY_LOGOUT_READS then
            party_close(api, "this client logged out")
        end
    else
        party_logout_reads = 0
    end
end

-- How big the party on show is: the session's size, else the last Play's.
local function party_size()
    local session = party_status and party_status.session
    local size = session and math.tointeger(tonumber(session.size))
    if size then
        return size
    end
    return party_last_play and party_last_play.party or 0
end

-- One seat's line: who, its process, and the status it last posted.
local function party_seat_text(api, seat, view)
    local size = party_size()
    if size == 0 then
        return seat == 1 and "no party (pick a party row and Play)" or "-"
    end
    if seat > size then
        return string.format("(a party of %d)", size)
    end
    if seat == 1 then
        local hitpoints = "?"
        if api.game ~= nil and api.game.skill ~= nil then
            local skill = api.game.skill(3)
            if skill then
                hitpoints = tostring(skill.current_level)
            end
        end
        local status = view.status or {}
        return string.format("%s (this client): %s %s %s (%d rows), hp %s", view.account ~= "" and view.account or "?",
            tostring(status.state or "?"), tostring(status.step or ""), tostring(status.verdict or ""),
            status.rows or 0, hitpoints)
    end
    local row = party_status and party_status.seats[seat]
    if row == nil then
        return "not spawned yet"
    end
    local process
    if row.alive == "1" then
        process = "pid " .. tostring(row.pid) .. " up"
    elseif row.pid == "0" then
        process = "not spawned"
    elseif row.signal ~= nil and row.signal ~= "-" then
        process = "pid " .. tostring(row.pid) .. " ended by signal " .. row.signal
    else
        process = "pid " .. tostring(row.pid) .. " exited " .. tostring(row.exit)
    end
    local posted = row.status
    if posted.state == nil then
        return string.format("%s, %s, no status posted yet", tostring(row.account), process)
    end
    return string.format("%s, %s: %s %s %s (%s rows), hp %s, %s", tostring(row.account), process,
        posted.state, tostring(posted.step or ""), tostring(posted.verdict or ""), tostring(posted.rows or "0"),
        tostring(posted.hitpoints or "?"), posted.alive == "1" and "alive" or "dead")
end

-- The PARTY block's texts and its two buttons.
local function party_view(api, view)
    local own = own_session(api)
    local block = { seats = {} }
    for seat = 1, PARTY_SEATS do
        block.seats[seat] = party_seat_text(api, seat, view)
    end
    if own then
        block.session = party_status_note
    elseif party_last_play and party_status == nil then
        block.session = party_status_note   -- the Play is still bringing it up
    elseif party_last_play then
        block.session = "closed (" .. party_status_note .. ")"
    else
        block.session = "no party"
    end
    local member_up = false
    if party_status then
        for _, row in pairs(party_status.seats) do
            if row.alive == "1" then
                member_up = true
            end
        end
    end
    block.stop_all = own ~= nil and (view.running or member_up)
    -- Enabled after a party Play whenever raider 1 is not running; pressed
    -- in a client that already hosted, it says why it cannot (play()).
    block.respawn = party_last_play ~= nil and not view.running
    block.open = own ~= nil
    return block
end

local function selected_text()
    if not selected then
        return "none (pick a row)"
    end
    return clip(selected.suite .. " " .. selected.id)
end

-- Write one row's text only when it moved: a set_text that changes nothing
-- still costs the host a patch.
local function show_text(api, id, text)
    text = clip(text)
    if shown[id] ~= text then
        shown[id] = text
        api.panel.set_text(id, text)
    end
end

local function show_label(api, id, label)
    if shown_label[id] ~= label then
        shown_label[id] = label
        api.panel.set_label(id, label)
    end
end

local function show_enabled(api, id, enabled)
    if shown_enabled[id] ~= enabled then
        shown_enabled[id] = enabled
        api.panel.set_value(id, enabled)
    end
end

-- The list slots, the count and the "more" row: set_label / set_text only.
local function refresh_list(api)
    if not page_built then
        return
    end
    for index = 1, LIST_WINDOW do
        local script = matched[index]
        slot_script[index] = script
        local label, summary = slot_texts(api, script)
        show_label(api, slot_id(index), label)
        show_text(api, slot_id(index), summary)
    end
    show_text(api, "matched", matched_text(api))
    show_text(api, "more", more_text())
end

-- The Interact switch and the two pointer readings.
local function refresh_watch(api)
    local watch = watch_view(api)
    local under_you = under_you_text(api)
    -- Logged when the switch or the watcher's own pick moves (never for the
    -- runner's pointer alone, which moves on every press).
    local line = watch.control .. " | under you: " .. under_you
    if line ~= shown_watch_logged then
        shown_watch_logged = line
        api.core.log("script-runner: watch " .. line .. " | runner " .. watch.runner)
    end
    if not page_built then
        return
    end
    show_enabled(api, "interact", watch.interact)
    show_text(api, "control", watch.control)
    show_text(api, "under_you", under_you)
    show_text(api, "runner_ptr", watch.runner)
end

local function refresh_status(api)
    local view = status_view(api)
    -- One log line per driver state change, so a headless run's client.log
    -- traces what the page showed.
    if view.driver ~= shown_driver_logged then
        shown_driver_logged = view.driver
        api.core.log("script-runner: driver " .. view.driver .. " | " .. view.test .. " | " .. view.leg
            .. " | " .. view.step .. " | " .. view.counts .. " | " .. view.summary)
    end
    local party = party_view(api, view)
    view.stop = view.stop or party.open
    local party_line_text = party.session .. " | " .. table.concat(party.seats, " | ")
    if party_line_text ~= shown_party_logged then
        shown_party_logged = party_line_text
        api.core.log("script-runner: party " .. party_line_text)
    end
    if not page_built then
        return
    end
    show_text(api, "party_session", party.session)
    for seat = 1, PARTY_SEATS do
        show_text(api, "seat" .. seat, party.seats[seat])
    end
    show_enabled(api, "stop_all", party.stop_all)
    show_enabled(api, "respawn", party.respawn)
    show_text(api, "driver", view.driver)
    show_text(api, "test", view.test)
    show_text(api, "leg", view.leg)
    show_text(api, "step", view.step)
    show_text(api, "counts", view.counts)
    show_text(api, "summary", view.summary)
    show_text(api, "session", view.session)
    show_text(api, "selected", selected_text())
    show_text(api, "note", note == "" and " " or note)
    show_enabled(api, "play", view.play)
    show_enabled(api, "stop", view.stop)
    refresh_watch(api)
end

-- Ask for the manifest (refresh: ask again) and take it when it has landed.
local function poll_manifest(api, refresh)
    if api.drive == nil or api.drive.tests == nil then
        manifest_state = "missing"
        manifest_detail = "No driver in this client: start it with ./launch run osrs239-scripts."
        return
    end
    local result, text = api.drive.tests(refresh)
    if result == "timeout" then
        manifest_state = "pending"
        manifest_detail = "Asking for the scripts manifest..."
        return
    end
    if result ~= "ok" then
        manifest_state = "missing"
        manifest_detail = tostring(text)
        scripts = {}
        api.core.log("script-runner: manifest " .. tostring(result) .. ": " .. tostring(text))
    else
        scripts = parse_manifest(text)
        manifest_state = "ready"
        manifest_detail = manifest_counts()
        api.core.log(string.format("script-runner: manifest: %d tests: %s", #scripts, manifest_detail))
        -- The selection survives a refresh when its script is still listed.
        if selected then
            local kept = nil
            for _, script in ipairs(scripts) do
                if script.id == selected.id and script.suite == selected.suite then kept = script end
            end
            selected = kept
        end
    end
    refilter()
    refresh_list(api)
end

function plugin.on_start(api)
    api.panel.request({ icon_asset = "panel_icon.png", preferred_width = 320 })
    poll_manifest(api, false)
end

function plugin.on_ui_build(api, panel, view)
    if view == "settings" then
        panel.paragraph("Scripts has no settings. Its list is the scripts manifest tests/tests.ini.")
        return
    end
    -- Every row below is written with its CURRENT text, so the page a rebuild
    -- produces is already what the patches would have made it. The row SET is
    -- the same every time (see the banner): nothing here depends on the
    -- filter, the search or the manifest except text.
    local status = status_view(api)
    local party = party_view(api, status)
    status.stop = status.stop or party.open
    shown = {}
    shown_label = {}
    shown_enabled = {}
    panel.select("suite", "Suite", suite_filter, SUITES)
    panel.node({ kind = 5, id = "search", label = "Search", text = search_text })
    panel.label("matched", matched_text(api))
    panel.key_value("selected", "Selected", selected_text())
    panel.select("start", "Start from", start_choice, STARTS)
    panel.button("play", "Play", status.play)
    panel.button("stop", "Stop", status.stop)
    -- Two views (camera seam2): the switch and the readings. Always declared,
    -- so the row set never changes; with no script attached they say so.
    local watch = watch_view(api)
    local under_you = under_you_text(api)
    panel.toggle("interact", "Interact (play while the script runs)", watch.interact)
    panel.key_value("control", "Control", clip(watch.control))
    panel.key_value("under_you", "Under your pointer", clip(under_you))
    panel.key_value("runner_ptr", "Runner pointer", clip(watch.runner))
    panel.key_value("driver", "Driver", clip(status.driver))
    panel.key_value("test", "Test", clip(status.test))
    panel.key_value("leg", "Leg", clip(status.leg))
    panel.key_value("step", "Step", clip(status.step))
    panel.key_value("counts", "Rows", clip(status.counts))
    panel.label("note", note == "" and " " or clip(note))
    -- The PARTY block: declared for every seat whatever is selected (the row
    -- set never changes); a solo Play's rows say "no party".
    panel.node({ kind = 9, id = "rule_party" })
    panel.key_value("party_session", "Party", clip(party.session))
    for seat = 1, PARTY_SEATS do
        panel.key_value("seat" .. seat, "Seat " .. seat, clip(party.seats[seat]))
        shown["seat" .. seat] = clip(party.seats[seat])
    end
    for seat = 2, PARTY_SEATS do
        panel.toggle("windowed" .. seat, "Seat " .. seat .. " in a window (else headless)", windowed[seat])
        shown_enabled["windowed" .. seat] = windowed[seat]
    end
    panel.button("stop_all", "Stop all", party.stop_all)
    panel.button("respawn", "Respawn", party.respawn)
    shown.party_session = clip(party.session)
    shown_enabled.stop_all = party.stop_all
    shown_enabled.respawn = party.respawn
    panel.node({ kind = 9, id = "rule_list" })
    for index = 1, LIST_WINDOW do
        local label, summary = slot_texts(api, matched[index])
        slot_script[index] = matched[index]
        panel.action_row(slot_id(index), label, summary)
        shown_label[slot_id(index)] = label
        shown[slot_id(index)] = summary
    end
    panel.label("more", more_text())
    panel.button("refresh", "Refresh", true)
    panel.node({ kind = 9, id = "rule_end" })
    -- The end of a run: what the ledger said, and where it is.
    panel.key_value("summary", "Summary", clip(status.summary))
    panel.key_value("session", "Session", clip(status.session))
    shown.matched = matched_text(api)
    shown.more = more_text()
    shown.driver = clip(status.driver)
    shown.test = clip(status.test)
    shown.leg = clip(status.leg)
    shown.step = clip(status.step)
    shown.counts = clip(status.counts)
    shown.summary = clip(status.summary)
    shown.session = clip(status.session)
    shown.selected = selected_text()
    shown.note = note == "" and " " or clip(note)
    shown_enabled.play = status.play
    shown_enabled.stop = status.stop
    shown_enabled.interact = watch.interact
    shown.control = clip(watch.control)
    shown.under_you = clip(under_you)
    shown.runner_ptr = clip(watch.runner)
    page_built = true
    api.core.log(string.format("script-runner: page built: suite=%s search='%s' matched=%d of %d",
        suite_filter, search_text, #matched, #scripts))
end

-- Play `script` (the selected row, or Respawn's last party row). A party row
-- closes the party still up from the last Play first, then asks the driver to
-- bring a new one up (see the banner).
local function play(api, script)
    if not script then
        note = "Pick a script first."
        return
    end
    if api.drive == nil or api.drive.play == nil then
        note = "This client has no driver: start it with ./launch run osrs239-scripts."
        return
    end
    local playable, reason = script_playable(api, script)
    if not playable then
        note = clip(script.id .. " cannot be played here: " .. reason)
        return
    end
    party_close(api, "a new Play")
    local party = nil
    local start = start_choice
    if script.party > 1 then
        party = { size = script.party, launch = true,
            windowed = { false, windowed[2], windowed[3], windowed[4] } }
        -- Raider 1 is the Play's own fresh account: the members look for it
        -- by that name (QD_PARTY names[1]), and every member is a fresh
        -- account too, as under run.py. A reset or as-is start would play on
        -- the character this client is logged in as, which no member knows
        -- (measured, seam37 smoke2: "the leader playbloa3 is not in its pool").
        start = "fresh"
    end
    local result, detail = api.drive.play({
        id = script.id, source = script.source, fixture = script.fixture,
        suite = script.suite, title = script.title, legs = script.legs,
        start = start,
        -- a row that names start=reset asks for its fixture applied in place
        -- (the cheat takes the fixture's basename)
        reset_fixture = (start == "reset" and script.start == "reset")
            and (script.fixture:match("([^/]+)%.ini$") or script.fixture) or nil,
        party = party,
    })
    if result == "ok" then
        local who = ""
        if party then
            local shown_seats = {}
            for seat = 2, script.party do
                shown_seats[#shown_seats + 1] = seat .. (windowed[seat] and " windowed" or " headless")
            end
            who = string.format(" as raider 1 of %d (seats %s)", script.party, table.concat(shown_seats, ", "))
            party_last_play = script
            party_known = nil
            party_status = nil
            party_status_pending = false
            party_status_note = "bringing the party up: log in, open, spawn, host"
        end
        note = clip("Playing " .. script.id .. who .. " from " .. START_LABEL[start] .. ", "
            .. tostring(detail) .. (start == "fresh" and " (logging out and in first)." or "."))
    else
        note = clip(tostring(detail or result))
    end
    api.core.log("script-runner: play " .. script.id .. (party and (" party " .. script.party) or "")
        .. " -> " .. tostring(result) .. " " .. tostring(detail))
end

-- Stop: raider 1's script ends at its next step, and a party is closed (its
-- members quit). With only a party left up (the test finished), Stop closes it.
local function stop(api)
    if api.drive == nil or api.drive.stop == nil then
        note = "This client has no driver."
        return
    end
    local result, detail = "ok", "no script running"
    local _, status = api.drive.status()
    if type(status) == "table" and status.state == "running" then
        result, detail = api.drive.stop()
    end
    local closed = party_close(api, "Stop")
    if result == "ok" then
        note = (type(status) == "table" and status.state == "running") and "Stopping at the next step." or ""
        if closed then
            note = note .. (note == "" and "" or " ") .. "The party is closing: every member quits."
        end
    else
        note = clip(tostring(detail or result))
    end
    note = clip(note)
    api.core.log("script-runner: stop -> " .. tostring(result) .. " " .. tostring(detail)
        .. (closed and " (party closing)" or ""))
end

-- Stop all: every member's script stops where it is and so does raider 1's;
-- the members stay logged in (Stop, a new Play or logging out closes them).
local function stop_all(api)
    local own = own_session(api)
    if own == nil then
        note = "No party is up."
        return
    end
    party_queue(api, "stop_all", "launch_command", leader_lines(own) .. "seat=all\ncommand=stop\n")
    local _, status = api.drive.status()
    if type(status) == "table" and status.state == "running" then
        api.drive.stop()
    end
    note = "Stop all: every raider stops at its next step; the members stay logged in."
    api.core.log("script-runner: stop all (session " .. own.session .. ")")
end

-- Respawn: the party closed and the same row played again with new members.
local function respawn(api)
    if party_last_play == nil then
        note = "No party was played yet."
        return
    end
    if party_hosted then
        note = clip("Respawn: " .. PARTY_REHOST_REASON .. ".")
        api.core.log("script-runner: respawn refused: " .. PARTY_REHOST_REASON)
        return
    end
    local _, status = api.drive.status()
    if type(status) == "table" and status.state == "running" then
        note = "Respawn: stop the party first (Stop, or Stop all)."
        return
    end
    api.core.log("script-runner: respawn " .. party_last_play.id)
    play(api, party_last_play)
end

-- The Interact switch: only while a script's view is attached (the driver
-- refuses otherwise, and the switch falls back to what the client says).
local function set_interact(api, on)
    if api.drive == nil or api.drive.view_interact == nil then
        note = "This client has no driver."
        return
    end
    local result, detail = api.drive.view_interact(on)
    if result == "ok" then
        note = on and "Interact is ON: your clicks and keys play the game; each is a watcher.* ledger row."
            or "Interact is off: the runner has control; your mouse orbits, zooms and inspects."
    else
        note = clip(tostring(detail or result))
    end
    -- The toggle shows what the client holds, not what was asked for.
    shown_enabled.interact = nil
    api.core.log("script-runner: interact " .. tostring(on) .. " -> " .. tostring(result))
end

local function select_script(api, script)
    local playable, reason = script_playable(api, script)
    if not playable then
        note = clip(script.id .. " cannot be played here: " .. reason)
        api.core.log("script-runner: unavailable " .. script.id .. ": " .. reason)
        refresh_status(api)
        return
    end
    selected = script
    note = ""
    -- The row's own required start, shown in the select; the owner can still
    -- change it before Play (the select re-reads start_choice on rebuild).
    if script.start == "reset" or script.start == "fresh" or script.start == "as_is" then
        start_choice = script.start
    end
    if script.party > 1 then
        start_choice = "fresh"   -- a party's raider 1 is its fresh account (play())
    end
    api.core.log("script-runner: selected " .. script.suite .. " " .. script.id)
    refresh_list(api)
    refresh_status(api)
end

function plugin.on_ui_action(api, ev)
    if ev.id == "search" then
        if ev.action == "text" then
            local text = ev.text or ""
            if text ~= search_text then
                search_text = text
                refilter()
                refresh_list(api)
                api.core.log(string.format("script-runner: search '%s' -> %d rows", search_text, #matched))
            end
        end
        return
    end
    if ev.id == "start" then
        start_choice = ev.text or "reset"
        api.core.log("script-runner: start from " .. start_choice)
        return
    end
    if ev.id == "suite" then
        local value = ev.text or "all"
        if value ~= suite_filter then
            suite_filter = value
            refilter()
            refresh_list(api)
            api.core.log(string.format("script-runner: suite %s -> %d rows", suite_filter, #matched))
        end
        return
    end
    if ev.id == "refresh" then
        poll_manifest(api, true)
        refresh_list(api)
        return
    end
    if ev.id == "play" then
        play(api, selected)
        refresh_status(api)
        return
    end
    if ev.id == "stop_all" then
        stop_all(api)
        refresh_status(api)
        return
    end
    if ev.id == "respawn" then
        respawn(api)
        refresh_status(api)
        return
    end
    local windowed_seat = math.tointeger(tonumber((ev.id or ""):match("^windowed(%d+)$")))
    if windowed_seat and windowed_seat >= 2 and windowed_seat <= PARTY_SEATS then
        windowed[windowed_seat] = ev.on and true or false
        shown_enabled[ev.id] = windowed[windowed_seat]
        api.core.log("script-runner: seat " .. windowed_seat .. (windowed[windowed_seat] and " windowed" or " headless"))
        return
    end
    if ev.id == "stop" then
        stop(api)
        refresh_status(api)
        return
    end
    if ev.id == "interact" then
        set_interact(api, ev.on and true or false)
        refresh_status(api)
        return
    end
    local index = math.tointeger(tonumber((ev.id or ""):match("^slot(%d+)$")))
    if index and ev.action == "activate" then
        local script = slot_script[index]
        if script then
            select_script(api, script)
        end
    end
end

function plugin.on_frame_start(api, _ev)
    frames = frames + 1
    if frames % STATUS_INTERVAL_FRAMES ~= 0 then
        return
    end
    if manifest_state == "none" or manifest_state == "pending" then
        poll_manifest(api, false)
    end
    if api.drive ~= nil and api.drive.launch_answer ~= nil then
        party_drain(api)
        if frames % PARTY_INTERVAL_FRAMES == 0 then
            party_poll(api)
        end
    end
    if list_stale then
        list_stale = false
        refilter()
        refresh_list(api)
    end
    refresh_status(api)
end

-- The plugin's end (a reload, the client's exit): close the party it brought
-- up (the service also kills every member when this process exits).
function plugin.on_stop(api)
    if api.drive ~= nil and api.drive.launch_close ~= nil then
        party_close(api, "the Scripts plugin stopped")
    end
end

return plugin
