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
-- The picture is Porcelain's. This plugin owns no widget, no watch, no
-- geometry repair and no pump: it states the four rows it wants, and re-states
-- them only when a string or a setting moved. Everything that used to be
-- bookkeeping here -- create on BOUND, remove on UNBOUND, re-run the layout
-- after a config change, write four positions and four sizes that were already
-- right, fence and commit -- is the layer's, and the reconciler compares
-- before it writes. That is what keeps the layout calls at four for a bind.
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

-- The four rows, in the order they stack. A metric whose switch is off keeps
-- its row -- same key, empty string -- and yields its slot, so turning FPS off
-- moves Frame up to the top rather than leaving a hole.
local METRICS = {
    { key = "fps", visible = "show_fps" },
    { key = "frame", visible = "show_frame_time" },
    { key = "effective", visible = "show_effective_fps" },
    { key = "memory", visible = "show_memory" },
}

-- The box is stated, not measured: nothing in the api answers how wide a
-- string is in the widget's face, so a row that overruns is clipped at 132 and
-- says nothing. Porcelain makes w,h mandatory on a text item for exactly that
-- reason.
local ROW_WIDTH, ROW_HEIGHT, FIRST_ROW_TOP = 132, 15, 3

-- What each row says right now, keyed the way the controls are. This is the
-- whole of the description that moves between frames.
local readout = {}
-- The describe callback is handed the builder and nothing else, so the api
-- table -- which is one table for the life of the script -- is held here.
local host = nil
local live = false

-- Refresh `readout` from the current samples and settings. Answers whether any
-- row's string actually moved: a describe that would say what the applied
-- description already says is a describe not worth running.
local function compose(api)
    local work_us = recent_mean_us()
    local wanted = {
        fps = string.format("FPS: %.1f", sampled_fps),
        frame = string.format("Frame: %.2f ms", work_us / 1000),
        effective = string.format("Effective FPS: %.1f", work_us > 0 and 1000000 / work_us or 0),
        memory = "Memory: " .. format_memory(sampled_memory),
    }
    local moved = false
    for _, metric in ipairs(METRICS) do
        local text = api.config[metric.visible] and wanted[metric.key] or ""
        if readout[metric.key] ~= text then
            readout[metric.key] = text
            moved = true
        end
    end
    return moved
end

-- Alignment 1 is CENTRE. Every row is a 132 px box with its text centred in
-- it, which is what this readout has always looked like; left would be a
-- silent change of appearance.
local function describe(build)
    local config = host.config
    local row = 0
    for _, metric in ipairs(METRICS) do
        build.text({
            key = "performance_" .. metric.key,
            text = readout[metric.key],
            place = {
                kind = "inside",
                on = "viewport",
                corner = "top_left",
                dx = config.x,
                dy = config.y + FIRST_ROW_TOP + row * ROW_HEIGHT,
            },
            w = ROW_WIDTH,
            h = ROW_HEIGHT,
            rgb = config.text_color,
            align = 1,
        })
        if config[metric.visible] then row = row + 1 end
    end
end

function plugin.on_start(api)
    host = api
    for _, metric in ipairs(METRICS) do readout[metric.key] = "" end
    compose(api)
    live = api.porcelain.open()
    if not live then
        -- Out loud: a client with no Porcelain has nowhere to put this, and a
        -- blank corner reads exactly like a working readout of zero.
        api.core.log("performance-display: no porcelain layer -- no readout")
        return
    end
    api.porcelain.describe(describe)
end

function plugin.on_config_changed(api)
    host = api
    if not live then return end
    -- The switches gate the strings as well as the rows, so the description has
    -- to be recomposed; the config stamp itself is the pump's, noted before
    -- this runs, and the next fence reads the recomposed `readout`.
    compose(api)
end

function plugin.on_frame_start(api, ev)
    host = api

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
    else
        -- Frames DRAWN, not on_frame_start calls: the loop runs at the pacer's
        -- rate whether or not it draws, so counting calls reads 50 while the
        -- screen is capped at 15.
        sample_frames = ev.drawn_frames - sample_drawn_at_start
        local elapsed = ev.now_ms - sample_started_ms
        if elapsed >= api.config.refresh_ms then
            sampled_fps = sample_frames * 1000 / elapsed
            sampled_memory = api.client.memory_bytes()
            sample_frames = 0
            sample_started_ms = ev.now_ms
            sample_drawn_at_start = ev.drawn_frames
        end
    end

    if not live then return end
    -- Re-describe only for a string that moved. A frame that says nothing new
    -- costs one hash compare per row inside the fence and no engine call at
    -- all, which is the whole reason the description is retained.
    -- open() installed the pump, so there is no fence and no commit here; this
    -- handler runs between them and the NEXT fence consumes the invalidate.
    if compose(api) then api.porcelain.invalidate() end
end

function plugin.on_stop(api)
    -- Close drops the four controls with the handle; the ring, the window and
    -- the latched values are cleared here so a re-enable starts from
    -- "Frame: 0.00 ms" rather than from a stale mean.
    if live then api.porcelain.close() end
    live = false
    host = nil
    sample_started_ms = nil
    sample_frames = 0
    sampled_fps = 0
    sampled_memory = 0
    recent = {}
    recent_written = 0
    recent_total = 0
end

return plugin
