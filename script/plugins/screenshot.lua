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
        -- A key CODE, and the range is the codes that exist. The client walks
        -- k < TORIRSK_COUNT when it broadcasts keys, so KEY_MAX below is the
        -- last code it can ever deliver; a wider max was a settings row that
        -- accepted 112 or an SDL scancode, saved it, and then never fired.
        -- The panel clamps an int row into [min, max], so an out-of-range
        -- number now comes back as a key the user can see instead of silence.
        { key = "hotkey", type = "int", default = "0", min = 0, max = 52,
          label = "Manual screenshot key (0 off, 1-26 A-Z, 27-36 0-9, 45 space)" },
        { key = "camera", type = "enum",
          choices = "off|top-left|top-right|bottom-left|bottom-right|report-button",
          default = "off", label = "Camera button" },
    },
}

local MARGIN = 6
local OPERATION = "Take screenshot"
local IDLE_OPACITY = 170
local PRESS_OPACITY = 255
-- Logic ticks the pressed look is held for (~20ms each). The idle translucency
-- is the affordance this port chose in place of a hover highlight, so the
-- press has to be a FLASH: setting 255 with nothing to put it back made
-- "pressed" the permanent look of the button after the first shot.
local FLASH_TICKS = 10
-- Longest filename the host accepts, one below TORIRS_PLUGIN_ASSET_NAME_MAX.
-- A refused name is only a log line, so a shot with an over-long subject would
-- simply not happen.
local NAME_MAX = 63
local icon, icon_small
local viewport, report            -- current native widgets, nil when unbound
local corner_control, report_control
local corner_x, corner_y
local flash, flash_ticks = nil, 0
-- The box the live control was placed against. A window resize moves it
-- without changing any node's identity, so no widget watch fires and nothing
-- else would ever notice the offsets had gone stale.
local anchor_x, anchor_y, anchor_width, anchor_height
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

local function slug(text, limit)
    if not text or text == "" then return "" end
    local out = text:gsub("[^A-Za-z0-9]+", "-"):gsub("^%-+", ""):gsub("%-+$", "")
    if limit and #out > limit then
        out = limit > 0 and out:sub(1, limit):gsub("%-+$", "") or ""
    end
    return out
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
    -- The stamp and the value are fixed-width; the subject is the only part
    -- that can run away, so it is trimmed to what they leave rather than to a
    -- constant that a long collection-log entry walks straight past.
    local tail = "_" .. (api.client.datestamp() or "unknown") .. ".png"
    if ev.value and ev.value >= 0 then tail = "-" .. ev.value .. tail end
    local room = NAME_MAX - #tail
    local name = slug(ev.subject, room)
    if name == "" then name = ev.kind end
    return name .. tail
end

-- What to tell the user. The absolute path is a single unwrapped GAME line
-- that the chatbox clips at its right edge -- and it clips exactly the tail,
-- which is the filename, the only part worth reading. The folder the user
-- configured is theirs already, so the message names what the PLUGIN chose
-- beneath it. The full path stays in the plugin log.
local function short_path(api, name, directory)
    local root = api.config.destination
    if root ~= "" and directory:sub(1, #root) == root then
        directory = directory:sub(#root + 1):gsub("^/+", "")
    end
    return (directory ~= "" and directory .. "/" or "") .. name
end

local function capture(api, name, directory)
    -- `result` is the absolute path on success and the host's refusal reason
    -- on failure.
    local ok, result = api.assets.screenshot(directory, name)
    if not ok then
        api.core.log("screenshot failed:", result, name)
        -- Said out loud: a refusal was a log line nobody reads, so a capture
        -- that did not happen looked exactly like one that did.
        api.core.notify("Screenshot failed (" .. result .. ")")
        return
    end
    api.core.log("captured", result)
    api.core.notify("Screenshot saved: " .. short_path(api, name, directory))
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
        widget:set_opacity(PRESS_OPACITY)
        flash, flash_ticks = widget, FLASH_TICKS
        capture_now(api)
    end))
    assert(control:revalidate())
    local box = control:bounds()
    if box then api.core.log("SCREENSHOT_CAMERA", key, box.x, box.y, box.width, box.height) end
    return control
end

-- Native chrome may float INSIDE a resizable viewport. Keep the camera
-- in free viewport space instead of painting under that chrome or intercepting
-- its buttons. Only visible role bounds are obstacles, never merely bound
-- sidebar content whose tab is closed.
local CAMERA_COVERS = { "chat", "frame_chat", "sidebar", "frame_sidebar",
    "minimap", "compass", "orbs", "map_housing" }
for i = 0, 13 do CAMERA_COVERS[#CAMERA_COVERS + 1] = "sidetab_" .. i end

local function intersects(x, y, width, height, box)
    return x < box.x + box.width and box.x < x + width and
        y < box.y + box.height and box.y < y + height
end

local function corner_position(api, where, width, height)
    local box = viewport and viewport:bounds()
    if not box or not width then return nil end
    local x = where:find("right") and box.width - width - MARGIN or MARGIN
    local y = where:find("bottom") and box.height - height - MARGIN or MARGIN
    local xs, ys, covers = { x }, { y }, {}
    local seen_x, seen_y = { [x] = true }, { [y] = true }
    local function add_candidate(values, seen, value, limit)
        if value >= MARGIN and value <= limit and not seen[value] then
            values[#values + 1], seen[value] = value, true
        end
    end
    for _, role in ipairs(CAMERA_COVERS) do
        local widget = api.widgets.find(role)
        local cover = widget and widget:visible() and widget:bounds()
        if cover and cover.width > 0 and cover.height > 0 and
                intersects(box.x, box.y, box.width, box.height, cover) then
            cover = { x = cover.x - box.x, y = cover.y - box.y,
                width = cover.width, height = cover.height }
            covers[#covers + 1] = cover
            add_candidate(xs, seen_x, cover.x - width - MARGIN, box.width - width - MARGIN)
            add_candidate(xs, seen_x, cover.x + cover.width + MARGIN, box.width - width - MARGIN)
            add_candidate(ys, seen_y, cover.y - height - MARGIN, box.height - height - MARGIN)
            add_candidate(ys, seen_y, cover.y + cover.height + MARGIN, box.height - height - MARGIN)
        end
    end
    local best_x, best_y, best_distance
    for _, cx in ipairs(xs) do
        for _, cy in ipairs(ys) do
            if cx >= MARGIN and cy >= MARGIN and
                    cx + width + MARGIN <= box.width and cy + height + MARGIN <= box.height then
                local clear = true
                for _, cover in ipairs(covers) do
                    if intersects(cx, cy, width, height, cover) then clear = false; break end
                end
                local distance = (cx - x)^2 + (cy - y)^2
                if clear and (not best_distance or distance < best_distance) then
                    best_x, best_y, best_distance = cx, cy, distance
                end
            end
        end
    end
    return best_x, best_y
end

-- The box whichever live control was positioned against: the viewport for a
-- corner, the native button's slot for the report mode.
local function anchor_box()
    if corner_control and viewport then return viewport:position() end
    if report_control and report then return report:position() end
    return nil
end

local function remember_anchor(box)
    if box then
        anchor_x, anchor_y, anchor_width, anchor_height = box.x, box.y, box.width, box.height
    else
        anchor_x, anchor_y, anchor_width, anchor_height = nil, nil, nil, nil
    end
end

local function update_controls(api)
    local where = api.config.camera
    -- Every control below is about to be replaced, so no press is outstanding.
    flash, flash_ticks = nil, 0
    -- Corner control lives in the viewport.
    if corner_control then corner_control:remove(); corner_control = nil end
    corner_x, corner_y = nil, nil
    if viewport and icon and where ~= "off" and where ~= "report-button" then
        local width, height = api.assets.image_size(icon)
        local x, y = corner_position(api, where, width, height)
        corner_x, corner_y = x, y
        if x then
            corner_control = place_camera(api, viewport, "camera", icon, x, y)
        else
            api.core.log("corner camera has no free space outside visible native chrome")
        end
    end
    -- Report-button mode hides the native button's presentation only; its
    -- native identity, operation and later server updates stay intact and are
    -- revealed again by set_hidden(false), reset or plugin teardown.
    if report_control then report_control:remove(); report_control = nil end
    if report then
        local replace = where == "report-button"
        -- A live root remount -- the user changing client layout -- retires
        -- the native node before the watch has handed over the new one, so
        -- this call answers `stale_reference`. That is a runtime state, not a
        -- broken contract: asserting on it faulted the script and disabled the
        -- WHOLE plugin (camera, hotkey and all eleven automatic captures) the
        -- first time anyone changed layout, in every camera mode including the
        -- shipped default. Drop the dead ref instead -- the bound event that
        -- follows the remount runs this again with the live one.
        local ok, reason = report:set_hidden(replace)
        if not ok then
            api.core.log("report button unavailable:", reason)
            report = nil
        elseif replace and icon_small then
            local parent, box = report:parent(), report:position()
            local width, height = api.assets.image_size(icon_small)
            if parent and box and width then
                report_control = place_camera(api, parent, "camera_report", icon_small,
                    box.x + (box.width - width) // 2, box.y + (box.height - height) // 2)
            end
        end
    end
    remember_anchor(anchor_box())
end

function plugin.on_start(api)
    pending = {}
    flash, flash_ticks = nil, 0
    remember_anchor(nil)
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

-- The client tick, not the server's: a window resize is not a game event and
-- must not wait 600ms (or a disconnected session forever) to be noticed, and
-- the press flash is measured in these.
function plugin.on_logic_tick(api)
    if flash_ticks > 0 then
        flash_ticks = flash_ticks - 1
        if flash_ticks == 0 then flash:set_opacity(IDLE_OPACITY); flash = nil end
    end
    local where = api.config.camera
    if viewport and icon and where ~= "off" and where ~= "report-button" then
        local width, height = api.assets.image_size(icon)
        local x, y = corner_position(api, where, width, height)
        -- A tab can open or close without resizing the viewport. Recompute
        -- the desired free position, but retain the control while it agrees.
        if x ~= corner_x or y ~= corner_y then update_controls(api) end
        return
    end
    local box = anchor_box()
    if not box then return end
    if box.x == anchor_x and box.y == anchor_y and
        box.width == anchor_width and box.height == anchor_height then return end
    -- The parent kept its identity and changed its size, so the offsets the
    -- control was given describe a corner that has moved. Re-place it.
    update_controls(api)
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
    flash, flash_ticks = nil, 0
    corner_x, corner_y = nil, nil
    remember_anchor(nil)
end

return plugin
