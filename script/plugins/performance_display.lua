--
-- Performance Display
--
-- A small screen-space overlay for the numbers that answer most client
-- performance questions: rendered frames per second, how long a frame is
-- actually taking, and the client's memory footprint.
--
-- There are two frame rates here and they mean different things.
--
-- "FPS" is frames counted over the refresh window: what the client actually
-- delivered, which under a frame cap is the cap. "Frame" is api.core.frame_work_us
-- -- the time the frame's WORK took, measured by the shell and closed before
-- the pacing sleep -- averaged over the last FRAME_WINDOW frames, and
-- "Effective FPS" is a second divided by it: the rate the client could hold if
-- nothing were holding it back.
--
-- The distinction is the entire reason the second pair is worth drawing. The
-- frame time can NOT be had by subtracting two api.core.frame_ms stamps: that gap
-- is wall clock, sleep included, so it reads back the cap and effective FPS
-- would be a second copy of FPS. 4 ms of work in a 20 ms budget and 19 ms of
-- work in the same budget are both 50 FPS by the first number and 250 against
-- 53 by the second, and the gap between them is the headroom.
--
-- FPS and memory latch on the refresh interval so a late frame does not make
-- them flicker. The work numbers update at frame start: a short window
-- is the whole reason to have one, and a stutter surfaced a second late is one
-- you have stopped looking for. Averaging FRAME_WINDOW frames is what keeps
-- them readable.
--

-- The refresh window's declared bounds, named because the schema is not the
-- only thing that has to honour them: a config value reaches the plugin
-- unchecked (api.config.set stores any string the INI grammar accepts, and
-- plugin_prefs.ini is a text file a user is invited to edit), so the same two
-- numbers are the row's min/max AND what the sampler clamps to. A refresh of 0
-- divides a frame count by a zero-length window.
local REFRESH_MIN_MS = 250
local REFRESH_MAX_MS = 5000

-- The block the rows are drawn in. The column is a fixed width so the four
-- lines share a left edge; the rows are the font's line height.
local COLUMN_WIDTH = 132
local ROW_HEIGHT = 15
-- The first row's inset below `y`. Part of the block's height, so the clamp
-- below has to subtract it.
local ROW_TOP = 3

-- `x`/`y` are declared against the largest surface a client can have, not
-- against the one it happens to be on -- a 4K canvas is a legal place to put a
-- readout and a 512-wide viewport is not a reason to forbid the number. What
-- keeps the readout on screen is the clamp in layout_labels, which knows the
-- surface it is laying out on; the schema only has to stop absurdity.
local POSITION_MAX = 4096

---@type torirs.Plugin
local plugin = {
    id = "performance-display",
    title = "FPS and Memory",
    version = "3.0.0",
    config = {
        { key = "show_fps", type = "bool", default = true, label = "Show FPS" },
        { key = "show_frame_time", type = "bool", default = true, label = "Show frame time (work)" },
        { key = "show_effective_fps", type = "bool", default = true, label = "Show effective FPS" },
        { key = "show_memory", type = "bool", default = true, label = "Show memory" },
        {
            key = "refresh_ms",
            type = "int",
            default = "1000",
            min = REFRESH_MIN_MS,
            max = REFRESH_MAX_MS,
            label = "Refresh interval (ms)"
        },
        { key = "x", type = "int", default = "10", min = 0, max = POSITION_MAX, label = "X position" },
        { key = "y", type = "int", default = "25", min = 0, max = POSITION_MAX, label = "Y position" },
        { key = "text_color", type = "color", default = "#FFFFFF", label = "Text colour" },
    },
}

-- How many recent frames the mean work time is taken over. The client's own
-- developer overlay averages its readout over 10, and matching it means the
-- two agree when both are up.
local FRAME_WINDOW = 10

local sample_started_ms = nil
local sample_frames = 0
local sample_drawn_at_start = 0
local sampled_fps = 0
local sampled_memory = 0

-- Ring of the last FRAME_WINDOW work times in microseconds, with a running sum
-- so the mean costs no loop. `recent_written` is the total ever written, which
-- gives both the write slot and -- until the ring fills -- how many entries
-- are real.
local recent = {}
local recent_written = 0
local recent_total = 0

local function recent_push(work_us)
    local slot = recent_written % FRAME_WINDOW + 1
    recent_total = recent_total - (recent[slot] or 0)
    recent[slot] = work_us
    recent_total = recent_total + work_us
    recent_written = recent_written + 1
end

-- Mean of the window, in microseconds. 0 when nothing has been recorded.
local function recent_mean_us()
    local count = recent_written
    if count > FRAME_WINDOW then count = FRAME_WINDOW end
    if count == 0 then return 0 end
    return recent_total / count
end

local function clamp(value, low, high)
    if value < low then return low end
    if value > high then return high end
    return value
end

-- The refresh window actually used. See REFRESH_MIN_MS.
local function refresh_ms(api)
    return clamp(api.config.refresh_ms, REFRESH_MIN_MS, REFRESH_MAX_MS)
end

local function format_memory(bytes)
    if bytes <= 0 then return "unavailable" end
    if bytes >= 1024 * 1024 * 1024 then
        return string.format("%.2f GiB", bytes / (1024 * 1024 * 1024))
    end
    if bytes >= 1024 * 1024 then
        return string.format("%.1f MiB", bytes / (1024 * 1024))
    end
    return string.format("%.0f KiB", bytes / 1024)
end

-- Owned native text widgets share the viewport's layout and visibility.
-- Native remounts rebind them; no draw builder or per-frame geometry repair.
local labels = {}
-- The bound viewport, kept so a layout can ask how big the surface is and
-- where it sits on the canvas.
local surface = nil
local metrics = {
    { key = "fps", visible = "show_fps" },
    { key = "frame", visible = "show_frame_time" },
    { key = "effective", visible = "show_effective_fps" },
    { key = "memory", visible = "show_memory" },
}
local function update_text(api)
    local work_us = recent_mean_us()
    local text = {
        fps = string.format("FPS: %.1f", sampled_fps),
        frame = string.format("Frame: %.2f ms", work_us / 1000),
        effective = string.format("Effective FPS: %.1f", work_us > 0 and 1000000 / work_us or 0),
        memory = "Memory: " .. format_memory(sampled_memory),
    }
    for _, metric in ipairs(metrics) do
        local label = labels[metric.key]
        if label then label:set_text(api.config[metric.visible] and text[metric.key] or "") end
    end
end
-- Two boxes sharing a pixel.
local function overlaps(ax, ay, aw, ah, bx, by, bw, bh)
    return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah
end

--
-- The bottom edge, in surface rows, of a chat region covering the block --
-- 0 when nothing covers it.
--
-- The plugin cannot be told which toplevel it is on and must not guess from a
-- revision number. What it can ask is the question that actually matters: on
-- the fixed 548 root and on the 2004 frame the surface is a box inset in the
-- chrome and the chat sits below it, so the top-left corner the schema
-- defaults to is bare scenery. On the mobile 601 root the surface IS the whole
-- canvas and the frame paints its chat across the top of it -- 11,0,519,145 --
-- so the same default buries all four rows under the chat lines. Both lanes
-- answer "does anything cover the rows", and only the one where something does
-- moves them.
--
local CHAT_ROLES = { "chat", "frame_chat" }

local function chat_floor(api, view, x, y, height)
    local floor = 0
    for _, role in ipairs(CHAT_ROLES) do
        local widget = api.widgets.find(role)
        local box = widget and widget:visible() and widget:bounds()
        if box and overlaps(view.x + x, view.y + y, COLUMN_WIDTH, height,
                box.x, box.y, box.width, box.height) then
            local edge = box.y + box.height - view.y
            if edge > floor then floor = edge end
        end
    end
    return floor
end

local function layout_labels(api)
    local rows = 0
    for _, metric in ipairs(metrics) do
        if api.config[metric.visible] then rows = rows + 1 end
    end

    local height = ROW_TOP + rows * ROW_HEIGHT
    local x, y = api.config.x, api.config.y
    local view = surface and surface:bounds()

    -- Kept inside the surface it is a child of. UIELEM_BUILTIN_WORLD does not
    -- clip its children, so an x of 600 on a 512-wide viewport used to put the
    -- whole readout off the right-hand edge while the engine's own dump still
    -- reported four unhidden nodes with text in them -- a readout that is gone
    -- and a trace that says it is fine. Clamped, an out-of-surface number
    -- parks the block against the edge, where it is visible and obviously not
    -- where it was asked to go.
    if view then
        x = clamp(x, 0, math.max(0, view.width - COLUMN_WIDTH))
        y = clamp(y, 0, math.max(0, view.height - height))
        local floor = chat_floor(api, view, x, y, height)
        if floor > y then
            y = math.min(floor, math.max(0, view.height - height))
        end
    end

    local row = 0
    for _, metric in ipairs(metrics) do
        local label = labels[metric.key]
        local shown = api.config[metric.visible] and true or false
        if label then
            -- A metric that is off is HIDDEN and not laid out. Positioning it
            -- first and only then asking whether it is shown left a live
            -- 132x15 node sitting exactly on top of the next visible row: two
            -- owned nodes, one rectangle, and nothing in the tree saying one
            -- of them is off.
            label:set_hidden(not shown)
            if shown then
                label:set_position(x, y + ROW_TOP + row * ROW_HEIGHT)
                label:set_size(COLUMN_WIDTH, ROW_HEIGHT)
                -- LEFT, not centred. `x` is labelled "X position" in the
                -- settings page and has to be the position of the text: a
                -- centred line inside a fixed column starts wherever its digit
                -- count leaves it, so the readout slid sideways every time the
                -- frame time gained a digit and the four rows never shared a
                -- left edge.
                label:set_text_align(0, 0)
                label:set_text_color(api.config.text_color)
                label:revalidate()
            end
        end
        if shown then row = row + 1 end
    end
    update_text(api)
end
function plugin.on_start(api)
    assert(api.widgets.watch("viewport", function(viewport, event)
        if event.kind == "unbound" then
            for _, label in pairs(labels) do label:remove() end
            labels = {}
            surface = nil
            return
        end
        surface = viewport
        for _, metric in ipairs(metrics) do
            labels[metric.key] = assert(viewport:create_text("performance_" .. metric.key))
        end
        layout_labels(api)
    end))
end
function plugin.on_config_changed(api) layout_labels(api) end

function plugin.on_frame_start(api, ev)
    -- 0 means the host measured no frame -- a headless run reports nothing, and
    -- so does the first frame. Recording it would drag the mean toward a work
    -- time no frame took.
    local work_us = api.core.frame_work_us()
    if work_us > 0 then
        recent_push(work_us)
    end

    if not sample_started_ms then
        sample_started_ms = ev.now_ms
        sample_drawn_at_start = ev.drawn_frames
        sampled_memory = api.client.memory_bytes()
        update_text(api)
        return
    end

    -- Frames DRAWN, not on_frame_start calls: the loop runs at the pacer's rate
    -- whether or not it draws, so counting calls reads 50 while the screen
    -- is capped at 15.
    sample_frames = ev.drawn_frames - sample_drawn_at_start
    local elapsed = ev.now_ms - sample_started_ms
    -- The clamped window, so the division below is never by zero: a refresh of
    -- 0 -- which the settings panel refuses and a hand-edited prefs file does
    -- not -- would make every frame a window boundary and read the rate as
    -- inf.
    if elapsed < refresh_ms(api) then update_text(api); return end

    sampled_fps = sample_frames * 1000 / elapsed
    sampled_memory = api.client.memory_bytes()
    sample_frames = 0
    sample_started_ms = ev.now_ms
    sample_drawn_at_start = ev.drawn_frames
    update_text(api)
end

function plugin.on_stop(api)
    labels = {}
    surface = nil
    sample_started_ms = nil
    sample_frames = 0
    sampled_fps = 0
    sampled_memory = 0
    recent = {}
    recent_written = 0
    recent_total = 0
end

return plugin
