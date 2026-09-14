-- Screenshot capture: game-event and hotkey captures, plus a camera control
-- described to the Porcelain layer. The report-button mode stands the camera
-- IN PLACE of the native Report button through a REPLACE placement, so it
-- follows the button's native visibility both ways and the native identity,
-- operation and later server updates stay intact. The corner modes place the
-- control at an inset inside the live viewport.
--
-- What this file no longer does, because the layer does it: it does not watch
-- widgets, does not remember which control is alive, does not re-place a
-- control when the frame moves its element or the window resizes (the corner
-- camera going stale on a resize was a defect; geometry now follows the target
-- at every fence), does not remove a control whose target died, does not hold
-- or release image handles, and does not drive the layer: open() installs the
-- pump, so the fence, the commit, the config note and the server-tick forward
-- are the layer's and this file's handlers run inside them.
---@type torirs.Plugin
local plugin = {
    id = "screenshot",
    title = "Screenshots",
    version = "4.0.0",
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
        -- REPLACE consumes the target's paint, input and hover, so the native
        -- "Report abuse" row is unreachable while the camera stands there.
        -- That is a consequence of the mode and is stated, not discovered.
        { key = "camera", type = "enum",
          choices = "off|top-left|top-right|bottom-left|bottom-right|report-button",
          default = "off", label = "Camera button (report-button hides the Report abuse option)" },
    },
}

local MARGIN = 6
local OPERATION = "Take screenshot"
local IDLE_OPACITY, PRESSED_OPACITY = 170, 255
-- Frames the press stays lit. Without it the control that has ever been
-- pressed is opaque for ever, which is what it did before the port.
local PRESS_FRAMES = 12

local CORNERS = {
    ["top-left"] = "top_left", ["top-right"] = "top_right",
    ["bottom-left"] = "bottom_left", ["bottom-right"] = "bottom_right",
}

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

-- The api table is one object for the script's whole life, so the describe and
-- the op handler reach it without a fresh closure per describe run.
local API
local pending = {}
local lit_key, lit_frames = nil, 0

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

-- The press: motion, not structure, so it takes the direct path and never
-- re-runs the describe. on_frame_start puts it back.
local function on_camera_op(key)
    lit_key, lit_frames = key, PRESS_FRAMES
    API.porcelain.set(key, { opacity = PRESSED_OPACITY })
    capture_now(API)
end

-- One camera item. Porcelain owns the image handle; its size is the only
-- thing the layer has no verb for, so it is read through the same handle.
local function camera(key, image, place)
    local ref = API.porcelain.image(image)
    if not ref then return nil end
    local width, height = API.assets.image_size(ref)
    if not width then return nil end
    return { key = key, image = image, w = width, h = height, opacity = IDLE_OPACITY,
             place = place, op_label = OPERATION, hit = true, on_op = on_camera_op }
end

-- WITHIN, not INSIDE: a CHILD of the viewport in the viewport's own
-- coordinates, reading corner and offsets identically but taking NO anchor.
-- INSIDE's sibling-plus-anchor is what REPLACE needs and an ornament does not,
-- and one live anchor makes UITree_FrameHasDepth true for the whole frame.
local function corner_item(where)
    return camera("camera", "camera.png",
        { kind = "within", on = "viewport", corner = CORNERS[where], dx = MARGIN, dy = MARGIN })
end

-- Both icons, held from the start and touched on every describe run, whichever
-- mode is live: the layer releases an image no run asked for, and a camera
-- whose mode changes must not pay a decode to come back.
local function hold_icons()
    API.porcelain.image("camera.png")
    API.porcelain.image("camera_small.png")
end

local function describe(d)
    local where = API.config.camera
    hold_icons()
    if where == "off" then return end
    local item
    if where == "report-button" then
        -- PENDING is not absent: the element resolves at a later fence and the
        -- layer defers the item until it does. ABSENT is a lane fact -- no
        -- chat-filter art at all -- already reported on the findings channel;
        -- the camera goes to a corner rather than nowhere.
        if API.porcelain.element("report_button").bind ~= "absent" then
            item = camera("camera_report", "camera_small.png",
                { kind = "replace", on = "report_button" })
        else
            item = corner_item("bottom-right")
        end
    else
        item = corner_item(where)
    end
    if item then d.control(item) end
end

local function tick_pending()
    local keep = {}
    for _, shot in ipairs(pending) do
        shot.ticks_left = shot.ticks_left - 1
        if shot.ticks_left <= 0 then capture(API, shot.name, shot.directory)
        else keep[#keep + 1] = shot end
    end
    pending = keep
end

local function wanted(api, ev)
    local kind = KINDS[ev.kind]
    if not kind or not api.config[kind[1]] then return nil end
    if ev.kind == "valuable_drop" and ev.value >= 0 and
        ev.value < api.config.min_drop_value then return nil end
    return kind[2]
end

function plugin.on_start(api)
    API = api
    pending = {}
    lit_key, lit_frames = nil, 0
    assert(api.porcelain.open(), "screenshot needs the porcelain layer")
    hold_icons()
    api.porcelain.describe(describe)
    api.porcelain.every_server_tick(tick_pending)
end

-- The pump calls this between its fence and its commit, so the set() below is
-- inside the frame the layer opened -- where a direct-motion write belongs.
function plugin.on_frame_start(api)
    if lit_frames > 0 then
        lit_frames = lit_frames - 1
        if lit_frames == 0 then
            api.porcelain.set(lit_key, { opacity = IDLE_OPACITY })
            lit_key = nil
        end
    end
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

-- Raw, not Porcelain_KeyEdge: that verb names five modifier keys by string and
-- polls key_held at the fence, and this hotkey is an arbitrary key code read
-- on the press edge.
function plugin.on_key(api, ev)
    local hotkey = api.config.hotkey
    if hotkey ~= 0 and ev.down and ev.key == hotkey then capture_now(api) end
end

function plugin.on_stop()
    -- The layer's close removes the control and releases the image; the host
    -- calls it after this handler returns.
    pending = {}
    lit_key, lit_frames = nil, 0
end

return plugin
