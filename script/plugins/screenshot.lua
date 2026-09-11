-- Screenshot capture: game-event and hotkey captures, plus a camera control
-- that is an owned widget with one native operation. The corner modes place
-- the control inside the live viewport; the report-button mode hides the
-- native Report button's presentation and puts the camera over its slot. The
-- native button keeps its identity and comes back when the mode changes or the
-- plugin stops.
---@type torirs.Plugin
local plugin = {
    id = "screenshot",
    title = "Screenshots",
    version = "3.0.0",
    config = {
        { key = "destination", type = "string", default = "",
          label = "Save folder (empty = client plugin folder)" },
        { key = "delay_ticks", type = "int", default = "2", min = 0, max = 20,
          label = "Ticks to wait before capturing" },
        { key = "on_level_up", type = "bool", default = true, label = "Level up" },
        { key = "on_quest_complete", type = "bool", default = true, label = "Quest complete" },
        { key = "on_boss_kill", type = "bool", default = true, label = "Boss kill" },
        { key = "on_pet", type = "bool", default = true, label = "Pet drop" },
        { key = "on_collection_log", type = "bool", default = true, label = "Collection log" },
        { key = "on_combat_achievement", type = "bool", default = true, label = "Combat achievement" },
        { key = "on_treasure_trail", type = "bool", default = true, label = "Clue casket" },
        { key = "on_valuable_drop", type = "bool", default = true, label = "Valuable drop" },
        { key = "on_untradeable_drop", type = "bool", default = false, label = "Untradeable drop" },
        { key = "on_death", type = "bool", default = false, label = "Death" },
        { key = "on_duel_end", type = "bool", default = false, label = "Duel end" },
        { key = "min_drop_value", type = "int", default = "100000", min = 0,
          max = 2000000000, label = "Valuable drop threshold" },
        { key = "hotkey", type = "int", default = "0", min = 0, max = 512,
          label = "Manual screenshot key (0 = off)" },
        { key = "camera", type = "enum",
          choices = "off|top-left|top-right|bottom-left|bottom-right|report-button",
          default = "off", label = "Camera button" },
    },
}

local MARGIN = 6
local OPERATION = "Take screenshot"
local IDLE_OPACITY = 170
local icon, icon_small
local viewport, report            -- current native widgets, nil when unbound
local corner_control, report_control
local pending = {}

local KINDS = {
    level_up = { "on_level_up", "Levels" },
    quest_complete = { "on_quest_complete", "Quests" },
    valuable_drop = { "on_valuable_drop", "Valuable-Drops" },
    untradeable_drop = { "on_untradeable_drop", "Untradeable-Drops" },
    boss_kill = { "on_boss_kill", "Boss-Kills" },
    pet = { "on_pet", "Pets" },
    collection_log = { "on_collection_log", "Collection-Log" },
    combat_achievement = { "on_combat_achievement", "Combat-Achievements" },
    death = { "on_death", "Deaths" },
    treasure_trail = { "on_treasure_trail", "Clue-Scroll-Rewards" },
    duel_end = { "on_duel_end", "Duels" },
}

local function slug(text)
    if not text or text == "" then return "" end
    local out = text:gsub("[^A-Za-z0-9]+", "-"):gsub("^%-+", ""):gsub("%-+$", "")
    return #out > 40 and out:sub(1, 40) or out
end

local function folder(api, category)
    local out = api.config.destination
    local player = api.world.local_player()
    if player and player.name ~= "" then
        local who = slug(player.name)
        if who ~= "" then out = (out ~= "" and out .. "/" or "") .. who end
    end
    if category then out = (out ~= "" and out .. "/" or "") .. category end
    return out
end

local function filename(api, ev)
    local name = slug(ev.subject)
    if name == "" then name = ev.kind end
    if ev.value and ev.value >= 0 then name = name .. "-" .. ev.value end
    return name .. "_" .. (api.client.datestamp() or "unknown") .. ".png"
end

local function capture(api, name, directory)
    local ok, path = api.assets.screenshot(directory, name)
    if not ok then
        api.core.log("screenshot failed:", path)
        return
    end
    api.core.log("captured", path)
    api.core.notify("Screenshot saved: " .. path)
end

local function capture_now(api)
    capture(api, "screenshot_" .. (api.client.datestamp() or "unknown") .. ".png",
        folder(api, nil))
end

local function wanted(api, ev)
    local kind = KINDS[ev.kind]
    if not kind or not api.config[kind[1]] then return nil end
    if ev.kind == "valuable_drop" and ev.value >= 0 and
        ev.value < api.config.min_drop_value then return nil end
    return kind[2]
end

-- One camera control: an owned image child of `parent` at (x, y), sized to the
-- image, armed with the capture operation. Returns nil until the image bytes
-- have decoded; on_asset re-places it then.
local function place_camera(api, parent, key, image, x, y)
    if not image then return nil end
    local width, height = api.assets.image_size(image)
    if not width then return nil end
    local control = parent:create_image(key)
    if not control then return nil end
    assert(control:set_image(image, width, height))
    assert(control:set_position(x, y))
    assert(control:set_opacity(IDLE_OPACITY))
    assert(control:set_on_op(OPERATION, function(widget)
        widget:set_opacity(255)
        capture_now(api)
    end))
    assert(control:revalidate())
    local box = control:bounds()
    if box then api.core.log("SCREENSHOT_CAMERA", key, box.x, box.y, box.width, box.height) end
    return control
end

local function corner_position(api, where, width, height)
    local box = viewport and viewport:position()
    if not box or not width then return nil end
    local x = where:find("right") and box.width - width - MARGIN or MARGIN
    local y = where:find("bottom") and box.height - height - MARGIN or MARGIN
    return x, y
end

local function update_controls(api)
    local where = api.config.camera
    -- Corner control lives in the viewport.
    if corner_control then corner_control:remove(); corner_control = nil end
    if viewport and icon and where ~= "off" and where ~= "report-button" then
        local width, height = api.assets.image_size(icon)
        local x, y = corner_position(api, where, width, height)
        if x then corner_control = place_camera(api, viewport, "camera", icon, x, y) end
    end
    -- Report-button mode hides the native button's presentation only; its
    -- native identity, operation and later server updates stay intact and are
    -- revealed again by set_hidden(false), reset or plugin teardown.
    if report_control then report_control:remove(); report_control = nil end
    if report then
        local replace = where == "report-button"
        assert(report:set_hidden(replace))
        if replace and icon_small then
            local parent, box = report:parent(), report:position()
            local width, height = api.assets.image_size(icon_small)
            if parent and box and width then
                report_control = place_camera(api, parent, "camera_report", icon_small,
                    box.x + (box.width - width) // 2, box.y + (box.height - height) // 2)
            end
        end
    end
end

function plugin.on_start(api)
    pending = {}
    icon = api.assets.image("camera.png")
    icon_small = api.assets.image("camera_small.png")
    assert(api.widgets.watch("viewport", function(widget, event)
        viewport = event.kind == "bound" and widget or nil
        if event.kind == "unbound" then corner_control = nil end
        update_controls(api)
    end))
    assert(api.widgets.watch("report_button", function(widget, event)
        report = event.kind == "bound" and widget or nil
        if event.kind == "unbound" then report_control = nil end
        update_controls(api)
    end))
end

function plugin.on_asset(api, ev)
    if ev.name == "camera.png" or ev.name == "camera_small.png" then
        icon = icon or api.assets.image("camera.png")
        icon_small = icon_small or api.assets.image("camera_small.png")
        update_controls(api)
    end
end

function plugin.on_config_changed(api, key)
    if key == "camera" then update_controls(api) end
end

function plugin.on_game_event(api, ev)
    local category = wanted(api, ev)
    if not category then return end
    local shot = {
        ticks_left = api.config.delay_ticks,
        name = filename(api, ev),
        directory = folder(api, category),
    }
    if shot.ticks_left <= 0 then
        capture(api, shot.name, shot.directory)
    else
        pending[#pending + 1] = shot
    end
end

function plugin.on_server_tick(api)
    local keep = {}
    for _, shot in ipairs(pending) do
        shot.ticks_left = shot.ticks_left - 1
        if shot.ticks_left <= 0 then capture(api, shot.name, shot.directory)
        else keep[#keep + 1] = shot end
    end
    pending = keep
end

function plugin.on_key(api, ev)
    local hotkey = api.config.hotkey
    if hotkey ~= 0 and ev.down and ev.key == hotkey then capture_now(api) end
end

function plugin.on_stop(api)
    pending = {}
    -- Owner teardown removes the owned controls and the report button's
    -- presentation hide; only the image handles are ours to release.
    if icon then api.assets.image_release(icon) end
    if icon_small then api.assets.image_release(icon_small) end
    icon, icon_small, viewport, report, corner_control, report_control = nil, nil, nil, nil, nil, nil
end

return plugin
