--
-- Performance Display
--
-- A small screen-space overlay for the two numbers that answer most client
-- performance questions: rendered frames per second and the client's memory
-- footprint. FPS is measured over a configurable window instead of derived from
-- one frame, so a single late frame does not make the label flicker wildly.
--

---@type torirs.Plugin
local plugin = {
    name = "performance-display",
    title = "FPS and Memory",
    version = "1.0.0",
    config = {
        { key = "show_fps", type = "bool", default = true, label = "Show FPS" },
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

local sample_started_ms = nil
local sample_frames = 0
local sampled_fps = 0
local sampled_memory = 0

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
    local lines = {}
    if api.config.show_fps then
        lines[#lines + 1] = string.format("FPS: %.1f", sampled_fps)
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
end

return plugin
