-- TEMPORARY: the loot beam's shipped models, stood on the local player.
local plugin = {
    name = "lb-shape",
    version = "1.0.0",
    config = {
        { key = "colour", type = "color", default = "#FF9600", label = "Colour" },
        { key = "style",  type = "enum", default = "modern", choices = "modern|light",
          label = "Style" },
        { key = "spin",   type = "int", default = "90", min = 0, max = 720, label = "Spin" },
    },
}

local STYLES = {
    modern = { asset = "beam_modern.model", body = 26432, core = 26584 },
    light  = { asset = "beam_light.model",  body = 6371,  core = nil },
}

local beam, model, placed = nil, nil, nil
local at_x, at_z, at_level = 0, 0, 0

function plugin.on_start(api) beam, model, placed = nil, nil, nil end
function plugin.on_stop(api)
    if beam then api.object_destroy(beam) end
    beam = nil
end
function plugin.on_world_loaded(api) placed = nil end

function plugin.on_server_tick(api)
    local me = api.local_player()
    if not me then return end
    local shape = STYLES[api.config.style]
    if not model then model = api.model_load(shape.asset) end
    if not model then return end
    if not beam then
        beam = api.object_create()
        if not beam then return end
        local hsl = api.hsl(api.config.colour)
        local h, s, l = api.hsl_unpack(hsl)
        local step = s > 2 and 1 or 0
        api.object_model(beam, model, "asset")
        api.object_recolor(beam, shape.body, api.hsl_pack(h, s - step, l))
        if shape.core then
            api.object_recolor(beam, shape.core, api.hsl_pack(h, s, math.min(l + 24, 127)))
        end
        api.object_light(beam, 75, 1875)
        api.object_active(beam, true)
    end
    local key = me.level .. ":" .. me.true_x .. ":" .. me.true_z
    if key ~= placed then
        placed = key
        at_x, at_z, at_level = me.true_x, me.true_z, me.level
        api.object_position(beam, at_x, at_z, at_level)
        api.log("lb-shape " .. api.config.style .. " at " .. key
            .. " ready=" .. tostring(api.object_ready(beam)))
    end
end

function plugin.on_frame(api, ev)
    if not beam or not placed or api.config.spin == 0 then return end
    api.object_position(beam, at_x, at_z, at_level, 0,
        (ev.now_ms * api.config.spin * 2048) // 360000 % 2048)
end

return plugin
