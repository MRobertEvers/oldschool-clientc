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
-- delivered, which under a frame cap is the cap. "Frame" is api.frame_work_us
-- -- the time the frame's WORK took, measured by the shell and closed before
-- the pacing sleep -- averaged over the last FRAME_WINDOW frames, and
-- "Effective FPS" is a second divided by it: the rate the client could hold if
-- nothing were holding it back.
--
-- The distinction is the entire reason the second pair is worth drawing. The
-- frame time can NOT be had by subtracting two api.frame_ms stamps: that gap
-- is wall clock, sleep included, so it reads back the cap and effective FPS
-- would be a second copy of FPS. 4 ms of work in a 20 ms budget and 19 ms of
-- work in the same budget are both 50 FPS by the first number and 250 against
-- 53 by the second, and the gap between them is the headroom.
--
-- FPS and memory latch on the refresh interval so a late frame does not make
-- them flicker. The work numbers are read at draw time instead: a short window
-- is the whole reason to have one, and a stutter surfaced a second late is one
-- you have stopped looking for. Averaging FRAME_WINDOW frames is what keeps
-- them readable.
--

---@type torirs.Plugin
local plugin = {
    name = "performance-display",
    title = "FPS and Memory",
    version = "1.1.0",
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

function plugin.on_frame(api, ev)
    -- 0 means the host measured no frame -- a headless run reports nothing, and
    -- so does the first frame. Recording it would drag the mean toward a work
    -- time no frame took.
    local work_us = api.frame_work_us()
    if work_us > 0 then
        recent_push(work_us)
    end

    if not sample_started_ms then
        sample_started_ms = ev.now_ms
        sampled_memory = api.memory_bytes()
        return
    end

    sample_frames = sample_frames + 1
    local elapsed = ev.now_ms - sample_started_ms
    if elapsed < api.config.refresh_ms then return end

    sampled_fps = sample_frames * 1000 / elapsed
    sampled_memory = api.memory_bytes()
    sample_frames = 0
    sample_started_ms = ev.now_ms
end

function plugin.on_draw_world(api, draw)
    local work_us = recent_mean_us()
    local frame_ms = work_us / 1000
    local effective_fps = 0
    if work_us > 0 then effective_fps = 1000000 / work_us end

    local lines = {}
    if api.config.show_fps then
        lines[#lines + 1] = string.format("FPS: %.1f", sampled_fps)
    end
    if api.config.show_frame_time then
        lines[#lines + 1] = string.format("Frame: %.2f ms", frame_ms)
    end
    if api.config.show_effective_fps then
        lines[#lines + 1] = string.format("Effective FPS: %.1f", effective_fps)
    end
    if api.config.show_memory then
        lines[#lines + 1] = "Memory: " .. format_memory(sampled_memory)
    end
    if #lines == 0 then return end

    local x = api.config.x
    local y = api.config.y
    local width = 132

    for i = 1, #lines do
        -- draw.text is centred on x and uses y as its baseline.
        local text_x = x + width // 2
        local text_y = y + 14 + (i - 1) * 15
        draw.text(text_x + 1, text_y + 1, lines[i], 0x000000)
        draw.text(text_x, text_y, lines[i], api.config.text_color)
    end
end

function plugin.on_stop(api)
    sample_started_ms = nil
    sample_frames = 0
    sampled_fps = 0
    sampled_memory = 0
    recent = {}
    recent_written = 0
    recent_total = 0
end

return plugin
