-- The Scripts tab: pick a prepared driver script and watch it play.
--
-- A sidebar page for a WATCHED client (profiles/osrs239-scripts.ini): a search
-- box, one row per prepared script, Play and Stop, and a status block that
-- follows the running script. Play hands the script to the quest driver through
-- api.drive.start, which exists only in a client started with
-- TORIRS_DRIVE_ON_DEMAND=1 (src/plugin/torirs_plugin_drive.c). In any other
-- client the page says so and Play stays off.
--
-- The list comes from the asset `index.tsv`, which is a link to
-- build/quest_gate/_scripts/index.tsv -- the file
-- `python3 tools/raid_gate/prepare_scripts.py` writes. A plugin cannot read an
-- arbitrary path (the sandbox has no io, and an asset name is a bare filename in
-- the plugin's own directory), so the link in script/plugins/assets/script-runner/
-- is how the index reaches the page. Refresh re-reads it.
--
-- Panel engine rules this page keeps:
--   * every row has a stable id; a status change is a set_text / set_value on an
--     existing row, never a rebuild;
--   * on_ui_build runs again only when the row SET changes: the index was read,
--     or the search text changed the filter;
--   * no row's text exceeds TEXT_LIMIT (the host drops a row over 192);
--   * nothing walks the UI tree; the driver status is read every
--     STATUS_INTERVAL_FRAMES frames and only rows whose text moved are touched.
--
-- A watched run is not a test run: it uses the wall clock, the logged-in
-- account and whatever the world holds from the last run, and its ledger grades
-- nothing (docs/minigames/raid_loop/DRIVER_NOTES.md, "Watching a test").

---@type torirs.Plugin
local plugin = { id = "script-runner", title = "Scripts", version = "1" }

local INDEX_ASSET = "index.tsv"
local TEXT_LIMIT = 180
local STATUS_INTERVAL_FRAMES = 10
local SEARCH_SETTLE_MS = 600
local ROW_ID_PREFIX = "script:"
local WIDGET_ID_LIMIT = 31

-- Everything the page shows, kept so a status poll changes only what moved.
local scripts = {}          -- array of {id, title, path, available, reason, row_id}
local scripts_by_row = {}   -- row_id -> script
local index_state = "pending"  -- pending | ready | missing | empty
local index_detail = ""
local search_text = ""      -- the filter the list shows
local typed_text = ""       -- what the search box holds; becomes search_text once typing settles
local typed_at_ms = nil     -- now_ms of the last keystroke still waiting to settle
local now_ms = 0
local selected = nil        -- a script table, or nil
local note = ""             -- the last start/stop answer, for the watcher
local frames = 0
local shown = {}            -- row id -> text last written by set_text
local shown_enabled = {}    -- button id -> last enabled flag written
local shown_driver_logged = nil

local function clip(text)
    text = tostring(text or "")
    if #text > TEXT_LIMIT then
        return text:sub(1, TEXT_LIMIT - 3) .. "..."
    end
    return text
end

-- A path shown to the watcher from `build/` on, so it fits one row.
local function short_path(path)
    path = tostring(path or "")
    local from_build = path:match(".*/(build/quest_gate/.*)$") or path:match(".*/(build/.*)$")
    return clip(from_build or path)
end

local function split_tabs(line)
    local fields = {}
    local start = 1
    while true do
        local tab = line:find("\t", start, true)
        if not tab then
            fields[#fields + 1] = line:sub(start)
            return fields
        end
        fields[#fields + 1] = line:sub(start, tab - 1)
        start = tab + 1
    end
end

local function row_id_for(script_id, ordinal)
    local id = ROW_ID_PREFIX .. script_id
    if #id > WIDGET_ID_LIMIT then
        id = ROW_ID_PREFIX .. "#" .. tostring(ordinal)
    end
    return id
end

-- index.tsv: a header row naming the columns, then one row per script.
-- Columns are found by name, so a column prepare_scripts.py adds later does
-- not shift these.
local function parse_index(bytes)
    local parsed = {}
    local columns = nil
    for line in bytes:gmatch("[^\r\n]+") do
        local fields = split_tabs(line)
        if not columns then
            columns = {}
            for position, name in ipairs(fields) do
                columns[name] = position
            end
        else
            local function field(name)
                local position = columns[name]
                return position and fields[position] or ""
            end
            local script_id = field("id")
            if script_id ~= "" then
                parsed[#parsed + 1] = {
                    id = script_id,
                    title = field("title"),
                    path = field("path"),
                    available = field("available") == "1" and field("path") ~= "",
                    reason = field("reason"),
                }
            end
        end
    end
    table.sort(parsed, function(a, b)
        if a.available ~= b.available then
            return a.available
        end
        return a.id < b.id
    end)
    for ordinal, script in ipairs(parsed) do
        script.row_id = row_id_for(script.id, ordinal)
    end
    return parsed, columns ~= nil
end

local function load_index(api)
    local bytes = api.assets.bytes(INDEX_ASSET)
    api.assets.release(INDEX_ASSET)
    if not bytes then
        index_state = "missing"
        index_detail = "No index. Run: python3 tools/raid_gate/prepare_scripts.py, then Refresh."
        scripts = {}
    else
        local parsed, had_header = parse_index(bytes)
        scripts = parsed
        if not had_header or #parsed == 0 then
            index_state = "empty"
            index_detail = "The index lists no scripts."
        else
            index_state = "ready"
            local playable = 0
            for _, script in ipairs(parsed) do
                if script.available then playable = playable + 1 end
            end
            index_detail = string.format("%d scripts, %d playable here.", #parsed, playable)
        end
    end
    scripts_by_row = {}
    for _, script in ipairs(scripts) do
        scripts_by_row[script.row_id] = script
    end
    -- The selection survives a refresh when its script is still listed.
    if selected then
        local kept = nil
        for _, script in ipairs(scripts) do
            if script.id == selected.id then kept = script end
        end
        selected = kept
    end
    api.core.log("script-runner: index " .. index_state .. ": " .. index_detail)
end

local function request_index(api)
    index_state = "pending"
    index_detail = "Reading the index..."
    local state = api.assets.request(INDEX_ASSET)
    if state == "ready" then
        load_index(api)
    elseif state ~= "pending" then
        index_state = "missing"
        index_detail = "No index (" .. tostring(state) .. "). Run: python3 tools/raid_gate/prepare_scripts.py, then Refresh."
        api.core.log("script-runner: index " .. tostring(state))
    end
end

local function matches(script, query)
    if query == "" then
        return true
    end
    local wanted = query:lower()
    return (script.id:lower():find(wanted, 1, true) ~= nil) or
           (script.title:lower():find(wanted, 1, true) ~= nil)
end

local function match_count(query)
    local count = 0
    for _, script in ipairs(scripts) do
        if matches(script, query) then count = count + 1 end
    end
    return count
end

-- What the status block and the buttons say right now.
local function status_view(api)
    local view = {
        driver = "", step = "-", counts = "-", summary = "-", session = "-",
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
    local script_name = (status.script or ""):match("([^/]*)%.lua$") or ""
    if status.state == "running" then
        view.driver = "running " .. script_name .. (status.stopping and " (stopping)" or "")
            .. (status.starting and " (starting)" or "")
    elseif status.state == "finished" then
        view.driver = "finished " .. script_name
    else
        view.driver = "idle"
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
        view.session = short_path(status.session)
    end
    view.play = selected ~= nil and selected.available and status.state ~= "running"
    view.stop = status.state == "running" and not status.stopping
    return view
end

local function selected_text()
    if not selected then
        return "none (pick a row)"
    end
    return clip(selected.id)
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

local function show_enabled(api, id, enabled)
    if shown_enabled[id] ~= enabled then
        shown_enabled[id] = enabled
        api.panel.set_value(id, enabled)
    end
end

local function refresh_status(api)
    local view = status_view(api)
    -- One log line per driver state change (idle, running, finished), so a
    -- headless run's client.log traces what the page showed.
    if view.driver ~= shown_driver_logged then
        shown_driver_logged = view.driver
        api.core.log("script-runner: driver " .. view.driver .. " | " .. view.step .. " | " ..
            view.counts .. " | " .. view.summary)
    end
    show_text(api, "driver", view.driver)
    show_text(api, "step", view.step)
    show_text(api, "counts", view.counts)
    show_text(api, "summary", view.summary)
    show_text(api, "session", view.session)
    show_text(api, "selected", selected_text())
    show_text(api, "note", note == "" and " " or note)
    show_enabled(api, "play", view.play)
    show_enabled(api, "stop", view.stop)
end

-- The row's name is the script id; its summary is the title, and says
-- "selected" on the row Play would start. The pane is narrow, so the id and the
-- title get a line each rather than one line that clips.
local function row_label(script)
    return clip(script.id)
end

local function row_summary(script)
    local title = script.title ~= "" and script.title or "(no title)"
    if selected and selected.id == script.id then
        return clip("selected: " .. title)
    end
    return clip(title)
end

function plugin.on_start(api)
    api.panel.request({ icon_asset = "panel_icon.png", preferred_width = 320 })
    request_index(api)
end

function plugin.on_asset(api, ev)
    if ev.name ~= INDEX_ASSET then
        return
    end
    if ev.ok then
        load_index(api)
    else
        api.assets.release(INDEX_ASSET)
        scripts = {}
        scripts_by_row = {}
        index_state = "missing"
        index_detail = "No index. Run: python3 tools/raid_gate/prepare_scripts.py, then Refresh."
        api.core.log("script-runner: index missing")
    end
    api.panel.invalidate()
end

function plugin.on_ui_build(api, panel, view)
    if view == "settings" then
        panel.paragraph("Scripts has no settings. Its list is build/quest_gate/_scripts/index.tsv.")
        return
    end
    -- Every row below is written with its CURRENT text, so the page a rebuild
    -- produces is already what the status poll would have patched it to.
    local status = status_view(api)
    shown = {}
    shown_enabled = {}
    -- The controls sit ABOVE the list: with a dozen scripts listed, a Play
    -- below them was off the bottom of the page even in fullscreen (measured
    -- at 807x503), and Stop is the button a watcher reaches for in a hurry.
    panel.node({ kind = 5, id = "search", label = "Search", text = typed_text })
    if search_text == "" then
        panel.label("index", clip(index_detail))
    else
        panel.label("index", clip(string.format("%d of %d match '%s'", match_count(search_text),
            #scripts, search_text)))
    end
    panel.key_value("selected", "Selected", selected_text())
    panel.button("play", "Play", status.play)
    panel.button("stop", "Stop", status.stop)
    panel.key_value("driver", "Driver", clip(status.driver))
    panel.key_value("step", "Step", clip(status.step))
    panel.key_value("counts", "Rows", clip(status.counts))
    panel.label("note", note == "" and " " or clip(note))
    panel.node({ kind = 9, id = "rule_list" })
    local listed = 0
    for _, script in ipairs(scripts) do
        if matches(script, search_text) then
            listed = listed + 1
            if script.available then
                panel.action_row(script.row_id, row_label(script), row_summary(script))
            else
                -- Not an action row: a plain label cannot be activated, and its
                -- white text stands apart from the orange playable rows.
                panel.label(script.row_id, clip(script.id .. ": unavailable -- " .. script.reason))
            end
        end
    end
    if listed == 0 and index_state == "ready" then
        panel.label("no_match", clip("No script matches '" .. search_text .. "'."))
    end
    panel.button("refresh", "Refresh", true)
    panel.node({ kind = 9, id = "rule_end" })
    -- The end of a run: what the ledger said, and where it is.
    panel.key_value("summary", "Summary", clip(status.summary))
    panel.key_value("session", "Session", clip(status.session))
    shown.driver = clip(status.driver)
    shown.step = clip(status.step)
    shown.counts = clip(status.counts)
    shown.summary = clip(status.summary)
    shown.session = clip(status.session)
    shown.selected = selected_text()
    shown.note = note == "" and " " or clip(note)
    shown_enabled.play = status.play
    shown_enabled.stop = status.stop
    api.core.log(string.format("script-runner: page built: search='%s' rows=%d of %d", search_text,
        listed, #scripts))
end

local function play(api)
    if not selected then
        note = "Pick a script first."
        return
    end
    if api.drive == nil or api.drive.start == nil then
        note = "This client has no driver: start it with ./launch run osrs239-scripts."
        return
    end
    -- The session directory sits beside _scripts/: build/quest_gate/watch_<id>.
    local gate_directory = selected.path:match("^(.*)/[^/]+/[^/]+$") or "."
    local session = gate_directory .. "/watch_" .. selected.id
    local result, detail = api.drive.start(selected.path, session)
    if result == "ok" then
        note = clip("Started " .. selected.id .. "; ledger in " .. short_path(session))
    else
        note = clip(tostring(detail or result))
    end
    api.core.log("script-runner: play " .. selected.id .. " -> " .. tostring(result) .. " " ..
        tostring(detail))
end

local function stop(api)
    if api.drive == nil or api.drive.stop == nil then
        note = "This client has no driver."
        return
    end
    local result, detail = api.drive.stop()
    note = clip(result == "ok" and "Stopping at the next step." or tostring(detail or result))
    api.core.log("script-runner: stop -> " .. tostring(result) .. " " .. tostring(detail))
end

function plugin.on_ui_action(api, ev)
    if ev.id == "search" then
        -- The list is NOT rebuilt per keystroke: a rebuild re-creates the
        -- search box, which loses the keyboard, so the next letter would land
        -- in the chat line. The count updates in place while typing; the list
        -- follows once the box has been still for SEARCH_SETTLE_MS.
        if ev.action == "text" then
            local text = ev.text or ""
            if text ~= typed_text then
                typed_text = text
                typed_at_ms = now_ms
                show_text(api, "index", string.format("%d of %d match '%s'", match_count(typed_text),
                    #scripts, typed_text))
            end
        end
        return
    end
    if ev.id == "refresh" then
        request_index(api)
        api.panel.invalidate()
        return
    end
    if ev.id == "play" then
        play(api)
        refresh_status(api)
        return
    end
    if ev.id == "stop" then
        stop(api)
        refresh_status(api)
        return
    end
    local script = scripts_by_row[ev.id]
    if script and ev.action == "activate" and script.available then
        local previous = selected
        selected = script
        if previous and previous.row_id ~= script.row_id then
            show_text(api, previous.row_id, row_summary(previous))
        end
        show_text(api, script.row_id, row_summary(script))
        note = ""
        api.core.log("script-runner: selected " .. script.id)
        refresh_status(api)
    end
end

function plugin.on_frame_start(api, ev)
    frames = frames + 1
    now_ms = ev and ev.now_ms or now_ms
    if typed_at_ms and now_ms - typed_at_ms >= SEARCH_SETTLE_MS then
        typed_at_ms = nil
        if typed_text ~= search_text then
            search_text = typed_text
            api.core.log(string.format("script-runner: search '%s' -> %d rows", search_text,
                match_count(search_text)))
            api.panel.invalidate()
        end
    end
    if frames % STATUS_INTERVAL_FRAMES ~= 0 then
        return
    end
    refresh_status(api)
end

return plugin
