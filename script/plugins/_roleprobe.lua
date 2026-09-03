-- Development probe for canonical named UI. No cache component ids and no
-- compatibility role names are used here.
---@type torirs.Plugin
local plugin = {
    id = "roleprobe",
    version = "2",
    config = {
        { key = "tint", type = "color", label = "Tint", default = "#20C0FF" },
        { key = "press_report", type = "bool", label = "Press report on start", default = false },
    },
}

local WATCHED = {
    "frame.viewport",
    "frame.minimap",
    "frame.sidebar",
    "frame.chat.button.report",
}
local nodes = {}
local frames = 0
local canvas_draws = 0

function plugin.on_start(api)
    for _, name in ipairs(WATCHED) do nodes[name] = api.ui.ref(name) end
    api.core.log("ROLEPROBE started with", #WATCHED, "canonical names")
end

local function report(api)
    for _, name in ipairs(WATCHED) do
        local ref = nodes[name]
        local info = ref and api.ui.info(ref)
        if info then
            local box = info.bounds
            api.core.log(name, box.x, box.y, box.width, box.height,
                "visible=", info.visible, "enabled=", info.enabled)
        else
            api.core.log(name, "not present")
        end
    end
    if api.config.press_report then
        local ref = nodes["frame.chat.button.report"]
        local info = ref and api.ui.info(ref)
        local action = info and info.actions[1]
        api.core.log("report invoke=", action and api.ui.invoke(ref, action) or false)
    end
end

function plugin.on_frame_start(api)
    frames = frames + 1
    if frames == 20 or frames == 80 then report(api) end
end

function plugin.on_draw_canvas(api, draw)
    canvas_draws = canvas_draws + 1
    for _, name in ipairs(WATCHED) do
        local ref = nodes[name]
        local info = ref and api.ui.info(ref)
        if info and info.visible then
            local box = info.bounds
            draw.rect(box.x, box.y, box.width, box.height, api.config.tint, 0)
        end
    end
end

return plugin
