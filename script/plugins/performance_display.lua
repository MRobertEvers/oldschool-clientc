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
            min = 250,
            max = 5000,
            label = "Refresh interval (ms)"
        },
        { key = "x", type = "int", default = "10", min = 0, max = 4096, label = "X position" },
        { key = "y", type = "int", default = "25", min = 0, max = 4096, label = "Y position" },
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
local function layout_labels(api)
    local row = 0
    for _, metric in ipairs(metrics) do
        local label = labels[metric.key]
        if label then
            label:set_position(api.config.x, api.config.y + 3 + row * 15)
            label:set_size(132, 15)
            label:set_text_align(1, 0)
            label:set_text_color(api.config.text_color)
            label:revalidate()
        end
        if api.config[metric.visible] then row = row + 1 end
    end
    update_text(api)
end
function plugin.on_start(api)
    assert(api.widgets.watch("viewport", function(viewport, event)
        if event.kind == "unbound" then
            for _, label in pairs(labels) do label:remove() end
            labels = {}
            return
        end
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
    if elapsed < api.config.refresh_ms then update_text(api); return end

    sampled_fps = sample_frames * 1000 / elapsed
    sampled_memory = api.client.memory_bytes()
    sample_frames = 0
    sample_started_ms = ev.now_ms
    sample_drawn_at_start = ev.drawn_frames
    update_text(api)
end

function plugin.on_stop(api)
    labels = {}
    sample_started_ms = nil
    sample_frames = 0
    sampled_fps = 0
    sampled_memory = 0
    recent = {}
    recent_written = 0
    recent_total = 0
end

return plugin
