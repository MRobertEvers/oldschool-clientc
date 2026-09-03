-- Screenshot capture, including a canonical named-UI replacement for the
-- report button. The contribution is static; configuration only activates it
-- and updates its retained appearance.
---@type torirs.Plugin
local plugin = {
    id = "screenshot",
    title = "Screenshots",
    version = "2.0.0",
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
    ui_contributions = {
        {
            node = "frame.chat.button.report",
            mode = "replace_or_provide",
            facets = { "appearance", "actions" },
            value = {
                flags = 3, -- VISIBLE | ENABLED
                action = "capture",
                actions = { "capture" },
            },
        },
    },
}

local REPORT = "frame.chat.button.report"
local ACTION_CAPTURE = 1
local MARGIN = 6
local icon
local icon_small
local icon_small_width
local icon_small_height
local report_ref
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

local function update_report(api)
    if not report_ref then return end
    local enabled = api.config.camera == "report-button"
    api.ui.set_enabled(report_ref, enabled)
    if not enabled then return end

    if icon_small then
        icon_small_width, icon_small_height = api.assets.image_size(icon_small)
    end

    api.ui.update(report_ref, { "appearance", "actions" }, {
        flags = 3,
        action = "capture",
        actions = { "capture" },
    })
end

function plugin.on_ui_node_draw(api, node, draw)
    if node ~= report_ref or api.config.camera ~= "report-button" or
        not icon_small or not icon_small_width then return end
    local info = api.ui.info(node)
    if not info then return end
    local box = info.bounds
    local mouse_x, mouse_y = api.input.pointer()
    local hovered = mouse_x and mouse_x >= box.x and mouse_x < box.x + box.width and
        mouse_y >= box.y and mouse_y < box.y + box.height
    local plate = hovered and 0x873838 or 0x5C1D1C
    draw.rect(box.x, box.y, box.width, box.height, 0x2A1412, 255)
    if box.width > 2 and box.height > 2 then
        draw.rect(box.x + 1, box.y + 1, box.width - 2, box.height - 2, plate, 255)
    end
    draw.image(icon_small,
        box.x + (box.width - icon_small_width) // 2,
        box.y + (box.height - icon_small_height) // 2,
        255)
end

function plugin.on_start(api)
    report_ref = api.ui.ref(REPORT)
    icon = api.assets.image("camera.png")
    icon_small = api.assets.image("camera_small.png")
    update_report(api)
end

function plugin.on_asset(api, ev)
    if ev.name == "camera.png" or ev.name == "camera_small.png" then
        update_report(api)
    end
end

function plugin.on_config_changed(api, key)
    if key == "camera" then update_report(api) end
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

function plugin.on_draw_canvas(api, draw)
    local where = api.config.camera
    if where == "off" or where == "report-button" or not api.world.local_player() then return end
    if not icon then return end
    local width, height = api.assets.image_size(icon)
    if not width then return end
    local box = api.placement.place("overlay_safe", where, width, height, MARGIN)
    if not box then return end
    draw.action_region_id(box, "capture", ACTION_CAPTURE)
    local x, y = api.input.pointer()
    local hovered = x and x >= box.x and x < box.x + box.width and
        y >= box.y and y < box.y + box.height
    draw.image(icon, box.x, box.y, hovered and 255 or 85)
end

function plugin.on_canvas_action(api, ev)
    if ev.id ~= ACTION_CAPTURE then return end
    capture_now(api)
    return "consume"
end

function plugin.on_ui_node_action(api, node, action)
    if node ~= report_ref or action ~= "capture" then return end
    capture_now(api)
    return "consume"
end

function plugin.on_stop(api)
    pending = {}
    if report_ref then api.ui.set_enabled(report_ref, false) end
    if icon then api.assets.image_release(icon) end
    if icon_small then api.assets.image_release(icon_small) end
    icon, icon_small, icon_small_width, icon_small_height, report_ref = nil, nil, nil, nil, nil
end

return plugin
