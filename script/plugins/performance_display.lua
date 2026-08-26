--
-- Performance Display
--
-- A small screen-space overlay for the numbers that answer most client
-- performance questions: rendered frames per second, how long a frame is
-- actually taking, and the client's memory footprint.
--
-- There are two frame rates here and they mean different things. "FPS" is
-- counted over the whole refresh window, so it is what the client really
-- delivered, pacing and stalls included; it is sampled on that interval, so a
-- single late frame does not make it flicker wildly. "Effective FPS" is the
-- rate implied by the mean frame time of the last handful of frames -- what
-- the client is sustaining right now. That pair is read at draw time rather
-- than latched on the refresh interval: a short window is the whole reason to
-- have it, and a stutter that only shows up a second later is one you have
-- already stopped looking for. Averaging the window is what keeps it readable.
--

---@type torirs.Plugin
local plugin = {
    name = "performance-display",
    title = "FPS and Memory",
    version = "1.1.0",
    config = {
        { key = "show_fps", type = "bool", default = true, label = "Show FPS" },
        { key = "show_frame_time", type = "bool", default = true, label = "Show frame time" },
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

-- How many recent frames the mean frame time is taken over.
local FRAME_WINDOW = 10

local sample_started_ms = nil
local sample_frames = 0
local sampled_fps = 0
local sampled_memory = 0

-- Ring of the last FRAME_WINDOW inter-frame deltas, with a running sum so the
-- mean costs no loop. `recent_written` is the total ever written, which gives
-- both the write slot and -- until the ring fills -- how many entries are real.
local recent = {}
local recent_written = 0
local recent_total = 0
local last_frame_ms = nil

local function recent_push(delta_ms)
    local slot = recent_written % FRAME_WINDOW + 1
    recent_total = recent_total - (recent[slot] or 0)
    recent[slot] = delta_ms
    recent_total = recent_total + delta_ms
    recent_written = recent_written + 1
end

local function recent_mean_ms()
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
    if last_frame_ms then
        recent_push(ev.now_ms - last_frame_ms)
    end
    last_frame_ms = ev.now_ms

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
    local frame_ms = recent_mean_ms()
    local effective_fps = 0
    if frame_ms > 0 then effective_fps = 1000 / frame_ms end

    local lines = {}
    if api.config.show_fps then
        lines[#lines + 1] = string.format("FPS: %.1f", sampled_fps)
    end
    if api.config.show_frame_time then
        lines[#lines + 1] = string.format("Frame: %.1f ms", frame_ms)
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
    last_frame_ms = nil
end

return plugin
