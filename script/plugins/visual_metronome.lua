--
-- Visual Metronome
--
-- A port of the RuneLite plugin-hub Visual Metronome (vincent0955/
-- Visual-metronome, f2226279d). Every server tick it steps a colour cycle and
-- shows it -- a solid box in the viewport, the colour of the player's true
-- tile, a number over their head or beside the pointer -- so tick-timed play
-- (tick manipulation, prayer flicking, 3t skilling) can be kept by eye.
--
-- The counting is the reference's onGameTick, line for line, because the
-- numbers it shows are what people learn to play by:
--
--   * tick_count == 1: the colour moves on every tick, and the number shown is
--     the COLOUR's index, 1 .. the number of colours.
--   * tick_count > 1: the colour moves once per tick_count ticks, and the
--     number shown is the tick inside that group, 1 .. tick_count.
--   * cycles 2 and 3 are free-running counters of their own length with no
--     colour, shown as numbers only.
--
-- The tick is on_server_tick: raised at SERVER_TICK_END, or at PLAYER_INFO on
-- a lane that has no end-of-tick packet -- either way once per server tick,
-- after that tick's packets have landed, which is what RuneLite's GameTick is.
--
-- How each overlay lands on this api:
--
--   * The box is draw.rect in the world pass. The reference's box is a
--     draggable, resizable RuneLite overlay at ABOVE_CHATBOX_RIGHT; with no
--     drag here, `box_corner` and the offsets say where it goes, and the
--     default is that same spot: on the chat pane's top edge, flush right.
--     The other four are the viewport's corners. The world pass draws in
--     canvas coordinates, and both boxes are asked of porcelain.element in
--     canvas space. The chat edge is the default rather than the viewport's
--     corner because on the native 548 frame the viewport is the WHOLE frame
--     -- the scene draws behind the fixed interfaces -- and its bottom-right
--     is under the side panel. An element the lane does not present falls
--     back to the viewport, then to the canvas, and with no layer at all the
--     canvas is said once. Cycle 2 and 3 boxes, which carry no fill in the
--     reference, sit to the left of the main one.
--   * The true tile is world_tile on true_x/true_z, the border in the current
--     colour. Our colours carry no alpha, so the reference's translucent fill
--     colour is a colour plus an opacity. "Corners only" projects the tile's
--     corners and draws the reference's quarter-edge strokes with draw.line,
--     which has a fixed stroke: the border width applies to the full border
--     only.
--   * Fadeout scales the fill's opacity by the reference's curve. A line or
--     border has no alpha on this api (draw.line's is accepted and ignored),
--     so the border fades by thinning instead and is gone once it rounds to
--     zero; corner strokes stay until the fill has faded.
--   * The overhead number is draw.text at the model's height plus the
--     configured lift. The pointer number is drawn in the canvas pass, as the
--     reference draws it ALWAYS_ON_TOP, so it follows the pointer over the
--     interfaces too.
--   * Hotkeys are raw key codes, 0 for none -- the screenshot plugin's shape;
--     nothing here can capture a key binding.
--
-- The reference has 52 settings and a Lua plugin may declare 32
-- (PLUGIN_LUA_MAX_CONFIG), so some are folded together:
--
--   * "Visual Metronome" and "Show Metronome Tick Number" are one `box` choice;
--     the reference's switch already hid the number with the box.
--   * "Number of Colors" and the 3rd..10th pickers are `more_colors`, a list
--     after tick and tock; the cycle is as long as the colours given.
--   * Each extra cycle's enable, overhead and overlay switches are one choice.
--   * The metronome fill's opacity is the tile fill's opacity: the reference
--     defaults both to 50, and only one is ever in use.
--   * "Fadeout" and its time are one time, 0 meaning off.
--
-- Not ported: party sync (there is no party service), the font type and size
-- (the overlay draws in one face), the overhead X offset and cycle gap (fixed
-- at the reference's 0 and 20), the increase/decrease tick count hotkeys and
-- "Reset to Tick" (the reset key always restarts the cycle), and alt +
-- right-click to reset the box size.
--

---@type torirs.Plugin
local plugin = {
    id      = "visual-metronome",
    title   = "Visual Metronome",
    version = "1.0.0",
    config  = {
        {
            key = "box",
            type = "enum",
            choices = "off|colour|colour_and_number",
            default = "colour",
            label = "Metronome box"
        },
        { key = "box_size", type = "int", default = "25", min = 16, max = 256, label = "Box size" },
        {
            key = "box_corner",
            type = "enum",
            choices = "above_chat_right|top_left|top_right|bottom_left|bottom_right",
            default = "above_chat_right",
            label = "Box position"
        },
        { key = "box_x", type = "int", default = "4", min = 0, max = 4096, label = "Box X offset" },
        { key = "box_y", type = "int", default = "4", min = 0, max = 4096, label = "Box Y offset" },
        { key = "tick_count", type = "int", default = "1", min = 1, max = 1000, label = "Tick count" },

        { key = "tick_color", type = "color", default = "#FFFFFF", label = "Tick colour" },
        { key = "tock_color", type = "color", default = "#808080", label = "Tock colour" },
        -- The reference's 3rd..10th colour pickers and its "Number of Colors",
        -- as one list: the cycle is tick, tock, then these in order. Empty is
        -- the reference's default of two colours.
        { key = "more_colors", type = "string", default = "", label = "More colours (#RRGGBB, comma separated)" },

        { key = "show_player_tick", type = "bool", default = "0", label = "Show tick number above player" },
        {
            key = "overhead_use_current_color",
            type = "bool",
            default = "0",
            label = "Use metronome colour for numbers"
        },
        { key = "number_color", type = "color", default = "#00FFFF", label = "Tick number colour" },
        {
            key = "overhead_height",
            type = "int",
            default = "20",
            min = -500,
            max = 500,
            label = "Overhead tick height"
        },

        { key = "highlight_current_tile", type = "bool", default = "0", label = "Enable true tile overlay" },
        { key = "tile_fill_color", type = "color", default = "#000000", label = "True tile fill colour" },
        {
            key = "change_fill_color",
            type = "bool",
            default = "0",
            label = "Fill true tile with metronome colour"
        },
        {
            key = "tile_fill_alpha",
            type = "int",
            default = "50",
            min = 0,
            max = 255,
            label = "True tile fill opacity"
        },
        {
            key = "tile_border_width",
            type = "int",
            default = "2",
            min = 0,
            max = 8,
            label = "True tile border width"
        },
        { key = "tile_corners_only", type = "bool", default = "0", label = "Corners only" },
        {
            key = "tile_fadeout_ms",
            type = "int",
            default = "0",
            min = 0,
            max = 10000,
            label = "True tile fadeout (ms, 0 = off)"
        },

        { key = "mouse_following_tick", type = "bool", default = "0", label = "Tick counter follows mouse" },
        { key = "mouse_offset_x", type = "int", default = "10", min = -100, max = 100, label = "Mouse X offset" },
        { key = "mouse_offset_y", type = "int", default = "-10", min = -100, max = 100, label = "Mouse Y offset" },

        {
            key = "cycle2",
            type = "enum",
            choices = "off|overhead|box|both",
            default = "off",
            label = "Cycle 2"
        },
        { key = "tick_count2", type = "int", default = "2", min = 2, max = 1000, label = "Cycle 2 length" },
        { key = "cycle2_color", type = "color", default = "#00FFFF", label = "Cycle 2 colour" },
        {
            key = "cycle3",
            type = "enum",
            choices = "off|overhead|box|both",
            default = "off",
            label = "Cycle 3"
        },
        { key = "tick_count3", type = "int", default = "2", min = 2, max = 1000, label = "Cycle 3 length" },
        { key = "cycle3_color", type = "color", default = "#00FFFF", label = "Cycle 3 colour" },

        { key = "reset_hotkey", type = "int", default = "0", min = 0, max = 512, label = "Tick cycle reset key (0 = off)" },
        { key = "show_hide_hotkey", type = "int", default = "0", min = 0, max = 512, label = "Show/hide key (0 = off)" },
        -- No label: the show/hide key's state, persisted, not a panel setting.
        { key = "metronome_hidden", type = "bool", default = "0" },
    },
}

-- Colours 3 onward, parsed from `more_colors` when it changes. The reference
-- caps the cycle at ten.
local MAX_COLORS = 10
local more_colors = {}

-- Corner strokes run a quarter of the way along each edge (the reference's
-- CORNER_SIZE_DIVISOR, from Corner Tile Indicators).
local CORNER_DIVISOR = 4
local TICK_MS = 600
-- The overhead cycle numbers stand this far either side of the main one
-- (the reference's overheadCyclesGapDistance default).
local OVERHEAD_GAP = 20
-- Cycle 2 and 3 boxes stand this far apart from the main box and each other.
local BOX_GAP = 4

-- The reference's plugin state, under its names.
local color_index = 0
local tick_counter = 0
local tick_counter2 = 0
local tick_counter3 = 0
local current_color = 0xFFFFFF

-- Whether porcelain.open answered: it is how the chat's and viewport's boxes
-- are known.
local layer = false

-- Fadeout bookkeeping: the tile the player stood on last frame, the tick they
-- last moved on, and the wall clock when they stopped.
local ticks = 0
local last_tile = nil
local last_moved_tick = 0
local stopped_ms = 0

local function load_colors(api)
    more_colors = {}
    for raw in string.gmatch(api.config.more_colors, "[^,]+") do
        local hex = string.match(raw, "^%s*#?(%x%x%x%x%x%x)%s*$")
        if hex and #more_colors < MAX_COLORS - 2 then
            more_colors[#more_colors + 1] = tonumber(hex, 16)
        end
    end
end

local function color_cycle()
    return 2 + #more_colors
end

local function set_color_by_index(api, index)
    if index == 1 then
        current_color = api.config.tick_color
    elseif index == 2 then
        current_color = api.config.tock_color
    elseif more_colors[index - 2] then
        current_color = more_colors[index - 2]
    end
end

-- "overhead", "box" or "both" each include one of the two places.
local function cycle_in(mode, place)
    return mode == place or mode == "both"
end

-- The number the box, the overhead and the pointer show for the main cycle.
local function tick_text(api)
    if api.config.tick_count == 1 then return tostring(color_index) end
    return tostring(tick_counter)
end

local function number_color(api)
    return api.config.overhead_use_current_color and current_color or api.config.number_color
end

-- Reference onConfigChanged: a counter past a shortened cycle restarts it.
local function clamp_counters(api)
    local cfg = api.config
    if color_index > color_cycle() then color_index = 0 end
    if tick_counter > cfg.tick_count then tick_counter = 0 end
    if tick_counter2 > cfg.tick_count2 then tick_counter2 = 0 end
    if tick_counter3 > cfg.tick_count3 then tick_counter3 = 0 end
end

-- Reference keyPressed, tick reset branch, with "Reset to Tick" at its
-- default of 0: the next tick is tick 1 of the first colour.
local function reset_cycles()
    color_index = 0
    tick_counter, tick_counter2, tick_counter3 = 0, 0, 0
end

local function reset_state(api)
    reset_cycles()
    current_color = api.config.tick_color
    ticks = 0
    last_tile = nil
    last_moved_tick = 0
    stopped_ms = 0
end

function plugin.on_start(api)
    load_colors(api)
    reset_state(api)
    layer = api.porcelain.open()
    if not layer then
        api.core.log("visual-metronome: no porcelain layer -- the box is placed on the canvas")
        return
    end
    -- The watches are made here, where the layer may subscribe: a first
    -- question asked in the draw pass is refused (a draw subscribes to
    -- nothing), and that refusal lands on the findings channel. They bind
    -- when the frame comes up; the draw pass only reads them.
    api.porcelain.element("chat")
    api.porcelain.element("viewport")
end

function plugin.on_config_changed(api, key)
    if key == "more_colors" then load_colors(api) end
    clamp_counters(api)
end

function plugin.on_server_tick(api)
    local cfg = api.config
    ticks = ticks + 1

    if tick_counter % cfg.tick_count == 0 then
        tick_counter = 0
        if color_index == color_cycle() then color_index = 0 end
        color_index = color_index + 1
        set_color_by_index(api, color_index)
    end
    tick_counter = tick_counter + 1

    if cfg.cycle2 ~= "off" then
        if tick_counter2 % cfg.tick_count2 == 0 then tick_counter2 = 0 end
        tick_counter2 = tick_counter2 + 1
    end
    if cfg.cycle3 ~= "off" then
        if tick_counter3 % cfg.tick_count3 == 0 then tick_counter3 = 0 end
        tick_counter3 = tick_counter3 + 1
    end
end

-- Raw, as the screenshot plugin's hotkey is: an arbitrary key code read on the
-- press edge. Nothing is consumed.
function plugin.on_key(api, ev)
    if not ev.down then return end
    local cfg = api.config
    if cfg.show_hide_hotkey ~= 0 and ev.key == cfg.show_hide_hotkey then
        api.config.set("metronome_hidden", not cfg.metronome_hidden)
    end
    if cfg.reset_hotkey ~= 0 and ev.key == cfg.reset_hotkey then
        reset_cycles()
    end
end

-- A size x size box whose number, if any, is centred in it. Text is drawn
-- centred on x with y as its baseline, so the baseline sits a little under the
-- middle.
local function box(draw, x, y, size, fill, text, text_color)
    if fill then draw.rect(x, y, size, size, fill, 255) end
    if text then draw.text(x + size // 2, y + size // 2 + 5, text, text_color) end
end

-- An element's canvas box while the lane presents it; nil otherwise, and
-- always nil without a layer.
local function presented_box(api, element)
    if not layer then return nil end
    local state = api.porcelain.element(element)
    if state.bind ~= "bound" or not state.presented then return nil end
    local box = state.box
    if (box.width or 0) <= 0 or (box.height or 0) <= 0 then return nil end
    return { x = box.x or 0, y = box.y or 0, width = box.width, height = box.height }
end

local function draw_boxes(api, draw)
    local cfg = api.config
    local size = cfg.box_size
    local corner = cfg.box_corner
    local area = nil
    if corner == "above_chat_right" then
        area = presented_box(api, "chat")
        if area then
            -- Flush right on the chat's top edge, standing on it.
            area = { x = area.x, y = area.y - size - 2 * cfg.box_y, width = area.width,
                height = size + 2 * cfg.box_y }
        end
        corner = "bottom_right"
    end
    area = area or presented_box(api, "viewport")
    if not area then
        local context = draw.context()
        if not context then return end
        area = context.bounds
    end

    local x = area.x + cfg.box_x
    if corner == "top_right" or corner == "bottom_right" then
        x = area.x + area.width - size - cfg.box_x
    end
    local y = area.y + cfg.box_y
    if corner == "bottom_left" or corner == "bottom_right" then
        y = area.y + area.height - size - cfg.box_y
    end

    if cfg.box ~= "off" then
        box(draw, x, y, size, current_color,
            cfg.box == "colour_and_number" and tick_text(api) or nil, cfg.number_color)
    end
    -- Cycle boxes are text only, in the cycle's colour, stepping leftward.
    local step = size + BOX_GAP
    if cycle_in(cfg.cycle2, "box") then
        x = x - step
        box(draw, x, y, size, nil, tostring(tick_counter2), cfg.cycle2_color)
    end
    if cycle_in(cfg.cycle3, "box") then
        x = x - step
        box(draw, x, y, size, nil, tostring(tick_counter3), cfg.cycle3_color)
    end
end

-- The reference's renderPolygonCorners over the true tile: from each corner,
-- a stroke a quarter of the way toward each neighbour.
local function tile_corners(api, draw, me, colour)
    local bx, bz = api.world.scene_origin()
    if not bx then return end
    local x0 = (me.true_x - bx) * 128
    local z0 = (me.true_z - bz) * 128
    local corners = {}
    for i, c in ipairs({ { 0, 0 }, { 128, 0 }, { 128, 128 }, { 0, 128 } }) do
        local sx, sy = api.draw.project(x0 + c[1], z0 + c[2])
        if not sx then return end
        corners[i] = { sx, sy }
    end
    for i = 1, 4 do
        local p = corners[i]
        local n = corners[i % 4 + 1]
        local v = corners[(i + 2) % 4 + 1]
        draw.line(p[1], p[2], p[1] + (n[1] - p[1]) // CORNER_DIVISOR,
            p[2] + (n[2] - p[2]) // CORNER_DIVISOR, colour)
        draw.line(p[1], p[2], p[1] + (v[1] - p[1]) // CORNER_DIVISOR,
            p[2] + (v[2] - p[2]) // CORNER_DIVISOR, colour)
    end
end

local function draw_tile(api, draw, me, now_ms)
    local cfg = api.config

    -- Moving is judged per server tick, as the reference's getTickCount is.
    -- Every frame of the tick after the last move re-stamps the stop, so the
    -- stamp left standing is that tick's last frame: where the fade begins.
    local tile = me.true_x .. ":" .. me.true_z .. ":" .. me.level
    if tile ~= last_tile then
        last_moved_tick = ticks
    elseif last_moved_tick + 1 == ticks then
        stopped_ms = now_ms
    end
    last_tile = tile

    local fill = cfg.change_fill_color and current_color or cfg.tile_fill_color
    local fill_alpha = cfg.tile_fill_alpha
    local border = cfg.tile_border_width

    -- Full strength while moving and for one tick after; then the reference's
    -- 1 - t^2 over the fadeout time less that tick.
    if cfg.tile_fadeout_ms > 0 and ticks - last_moved_tick > 1 then
        local fade_ms = cfg.tile_fadeout_ms - TICK_MS
        local since = now_ms - stopped_ms
        if fade_ms <= 0 or since >= fade_ms then return end
        local opacity = 1 - (since / fade_ms) ^ 2
        fill_alpha = math.floor(fill_alpha * opacity)
        border = math.floor(border * opacity + 0.5)
    end

    if cfg.tile_corners_only then
        draw.world_tile(me.true_x, me.true_z, me.level, fill, fill, fill_alpha, 0)
        tile_corners(api, draw, me, current_color)
    else
        draw.world_tile(me.true_x, me.true_z, me.level, fill, current_color, fill_alpha, border)
    end
end

local function draw_overhead(api, draw, me)
    local cfg = api.config
    if me.element_id < 0 then return end
    local height = api.draw.element_height(me.element_id) + cfg.overhead_height
    local sx, sy = api.draw.project(me.fine_x, me.fine_z, height)
    if not sx then return end

    draw.text(sx, sy, tick_text(api), number_color(api))
    if cycle_in(cfg.cycle2, "overhead") then
        draw.text(sx - OVERHEAD_GAP, sy, tostring(tick_counter2), cfg.cycle2_color)
    end
    if cycle_in(cfg.cycle3, "overhead") then
        draw.text(sx + OVERHEAD_GAP, sy, tostring(tick_counter3), cfg.cycle3_color)
    end
end

-- Nothing before there is a player, as a RuneLite overlay draws nothing on the
-- login screen: there are no ticks to keep.
function plugin.on_draw_world(api, draw)
    local cfg = api.config
    if cfg.metronome_hidden then return end
    local me = api.world.local_player()
    if not me then return end
    if cfg.highlight_current_tile then draw_tile(api, draw, me, api.core.frame_ms()) end
    if cfg.show_player_tick then draw_overhead(api, draw, me) end
    draw_boxes(api, draw)
end

function plugin.on_draw_canvas(api, draw)
    local cfg = api.config
    if cfg.metronome_hidden or not cfg.mouse_following_tick then return end
    local px, py = api.input.pointer()
    if not px then return end
    draw.text(px + cfg.mouse_offset_x, py + cfg.mouse_offset_y, tick_text(api), number_color(api))
end

function plugin.on_stop(api)
    reset_state(api)
end

return plugin
