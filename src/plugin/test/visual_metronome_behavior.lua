-- Appended to the actual shipped source by plugin_lua_test.c.
--
-- What is pinned is the COUNTING, because the numbers and colours it shows are
-- what people time their play by: the reference's onGameTick, its reset
-- hotkey, and its config clamps. Then where each overlay lands.
--
-- The harness registers THIS table, not the product's, so the loader never
-- sees the product's config: its 32-item cap is checked here instead.
return { id = 'visual-metronome-behavior', on_start = function(host)
    local rects, texts, tiles, lines, logs = {}, {}, {}, {}, {}
    local WHITE, GRAY, DARK, BLACK = 0xffffff, 0x808080, 0x404040, 0x000000

    local config = {
        box = 'colour', box_size = 25, box_corner = 'above_chat_right', box_x = 4, box_y = 4,
        tick_count = 1, tick_color = WHITE, tock_color = GRAY, more_colors = '',
        show_player_tick = false, overhead_use_current_color = false,
        number_color = 0x00ffff, overhead_height = 20,
        highlight_current_tile = false, tile_fill_color = BLACK, change_fill_color = false,
        tile_fill_alpha = 50, tile_border_width = 2, tile_corners_only = false,
        tile_fadeout_ms = 0,
        mouse_following_tick = false, mouse_offset_x = 10, mouse_offset_y = -10,
        cycle2 = 'off', tick_count2 = 2, cycle2_color = 0x00ff00,
        cycle3 = 'off', tick_count3 = 2, cycle3_color = 0x0000ff,
        reset_hotkey = 0, show_hide_hotkey = 0, metronome_hidden = false,
    }
    config.set = function(key, value) config[key] = value; return true end

    -- Scene base (3190, 3190); the player stands on (3200, 3201).
    local me = { true_x = 3200, true_z = 3201, level = 0, element_id = 3,
        fine_x = 10 * 128 + 64, fine_z = 11 * 128 + 64 }
    local now = 1000
    -- The fixed frame's chat pane and its inset viewport, in a larger canvas.
    local P = { available = true, chat_presented = true }
    local BOXES = { chat = { x = 0, y = 338, width = 519, height = 165 },
        viewport = { x = 4, y = 4, width = 512, height = 334 } }
    local api = {
        config = config,
        core = { log = function(text) logs[#logs + 1] = text end,
            frame_ms = function() return now end },
        world = { local_player = function() return me end,
            scene_origin = function() return 3190, 3190 end },
        input = { pointer = function() return 300, 200 end },
        porcelain = {
            open = function() return P.available end,
            element = function(element)
                local presented = element ~= 'chat' or P.chat_presented
                return { bind = 'bound', presented = presented, box = BOXES[element] }
            end,
        },
        draw = {
            project = function(x, z, height) return x // 8, z // 8 - (height or 0) end,
            element_height = function(element) return element * 10 end,
        },
    }
    local graphics = {
        rect = function(...) rects[#rects + 1] = { ... } end,
        text = function(...) texts[#texts + 1] = { ... } end,
        line = function(...) lines[#lines + 1] = { ... } end,
        world_tile = function(...) tiles[#tiles + 1] = { ... }; return true end,
        context = function() return { bounds = { x = 0, y = 0, width = 765, height = 503 } } end,
    }

    local function frame()
        rects, texts, tiles, lines = {}, {}, {}, {}
        product.on_draw_world(api, graphics)
    end
    local function tick(n)
        for _ = 1, n or 1 do product.on_server_tick(api, { cycle = 0 }) end
    end
    local function shown()
        frame()
        return rects[1] and rects[1][5], texts[1] and texts[1][3]
    end

    assert(product.id == 'visual-metronome', 'its own id, and so its own settings section')
    assert(#product.config <= 32, 'a Lua plugin may declare 32 settings (PLUGIN_LUA_MAX_CONFIG)')
    local defaults = { box = 'colour', box_size = '25', tick_count = '1', more_colors = '',
        tick_color = '#FFFFFF', tock_color = '#808080', number_color = '#00FFFF',
        tile_fill_color = '#000000', tile_fill_alpha = '50', tile_border_width = '2',
        overhead_height = '20', mouse_offset_x = '10', mouse_offset_y = '-10',
        tick_count2 = '2', cycle2 = 'off', highlight_current_tile = '0' }
    for _, row in ipairs(product.config) do
        if defaults[row.key] then
            assert(defaults[row.key] == row.default, 'reference default for ' .. row.key)
            defaults[row.key] = nil
        end
    end
    assert(next(defaults) == nil, 'every pinned reference setting has its row')

    product.on_start(api)
    assert(#logs == 0, 'starting is silent')

    -- The box: the reference's ABOVE_CHATBOX_RIGHT by default, solid, square.
    frame()
    local r = rects[1]
    assert(#rects == 1 and r[1] == 519 - 25 - 4 and r[2] == 338 - 25 - 4
        and r[3] == 25 and r[4] == 25,
        'the box stands box_y above the chat, box_x in from its right edge')
    assert(r[5] == WHITE and r[6] == 255, 'opaque, and the tick colour before the first tick')
    assert(#texts == 0, 'no number unless asked for')
    local player = me
    me = nil
    frame()
    assert(#rects == 0, 'nothing before there is a player')
    me = player

    -- tick_count 1: the colour steps every tick and the number is its index.
    config.box = 'colour_and_number'
    local seen = {}
    for i = 1, 4 do
        tick()
        local colour, text = shown()
        seen[i] = colour
        assert(text == tostring((i - 1) % 2 + 1), 'with tick count 1 the number is the colour index')
    end
    assert(seen[1] == WHITE and seen[2] == GRAY and seen[3] == WHITE and seen[4] == GRAY,
        'tick, tock, tick, tock')

    -- More colours follow tick and tock; an entry that is not a colour is dropped.
    config.more_colors = ' #404040, nope ,000000 '
    product.on_config_changed(api, 'more_colors')
    product.on_start(api)
    tick(3)
    assert(shown() == DARK, 'the third colour is the first listed')
    tick()
    local colour4, text4 = shown()
    assert(colour4 == BLACK and text4 == '4', 'a # is optional, and the junk entry is not a colour')
    tick()
    assert(shown() == WHITE, 'and four colours wrap to the first')
    config.more_colors = ''
    product.on_config_changed(api, 'more_colors')

    -- tick_count 3: the colour holds for three ticks, the number counts them.
    product.on_start(api)
    config.tick_count = 3
    local trail = {}
    for i = 1, 7 do
        tick()
        local colour, text = shown()
        trail[i] = { colour, text }
    end
    for i = 1, 3 do
        assert(trail[i][1] == WHITE and trail[i][2] == tostring(i), 'ticks 1..3 are the first colour')
    end
    for i = 4, 6 do
        assert(trail[i][1] == GRAY and trail[i][2] == tostring(i - 3), 'ticks 4..6 the second')
    end
    assert(trail[7][1] == WHITE and trail[7][2] == '1', 'and the seventh starts over')

    -- A shortened cycle restarts a counter that is past it.
    product.on_start(api)
    config.tick_count = 5
    tick(4)
    config.tick_count = 3
    product.on_config_changed(api, 'tick_count')
    tick()
    local _, text = shown()
    assert(text == '1', 'a counter past a shortened cycle restarts it, not reads 5')

    -- Reset hotkey: back to the start of the cycle, on the press edge only.
    config.tick_count = 3
    config.reset_hotkey = 82
    product.on_start(api)
    tick(5)
    product.on_key(api, { key = 82, down = false })
    assert(select(2, shown()) == '2', 'a release does nothing')
    product.on_key(api, { key = 82, down = true })
    tick()
    local colour
    colour, text = shown()
    assert(colour == WHITE and text == '1', 'reset to 0: the next tick is tick 1 of the first colour')
    tick(3)
    product.on_key(api, { key = 82, down = true })
    tick()
    colour, text = shown()
    assert(colour == WHITE and text == '1', 'from the second colour too')

    -- Show/hide: persisted, and nothing draws while hidden.
    config.show_hide_hotkey = 72
    product.on_key(api, { key = 72, down = true })
    assert(config.metronome_hidden == true, 'the hide is stored in config')
    frame()
    assert(#rects == 0 and #texts == 0, 'hidden draws nothing')
    product.on_key(api, { key = 72, down = true })
    frame()
    assert(#rects == 1, 'and the second press brings it back')

    -- Corners: of the viewport, not the canvas.
    config.box_corner = 'bottom_right'
    frame()
    assert(rects[1][1] == 4 + 512 - 25 - 4 and rects[1][2] == 4 + 334 - 25 - 4, 'bottom_right')
    config.box_corner = 'top_left'
    frame()
    assert(rects[1][1] == 8 and rects[1][2] == 8, 'top_left is box_x, box_y into the viewport')
    config.box_corner = 'top_right'
    frame()
    assert(rects[1][1] == 4 + 512 - 25 - 4 and rects[1][2] == 8, 'top_right')

    -- Without a layer there is no viewport box: the canvas's corner, said once.
    P.available = false
    product.on_start(api)
    assert(#logs == 1 and logs[1]:find('porcelain', 1, true), 'a missing layer is said')
    frame()
    assert(rects[1][1] == 765 - 25 - 4 and rects[1][2] == 4, 'and the canvas stands in')
    P.available = true
    product.on_start(api)

    -- A chat the lane does not present: the viewport's corner stands in.
    config.box_corner = 'above_chat_right'
    P.chat_presented = false
    frame()
    assert(rects[1][1] == 4 + 512 - 25 - 4 and rects[1][2] == 4 + 334 - 25 - 4,
        'no chat, the viewport bottom-right')
    P.chat_presented = true

    -- Cycle 2: a free-running counter shown in a fill-less box left of the main one.
    product.on_start(api)
    config.tick_count = 1
    config.cycle2 = 'box'
    config.tick_count2 = 3
    tick(4)
    frame()
    assert(#rects == 1, 'the cycle box has no fill')
    assert(#texts == 2 and texts[2][3] == '1' and texts[2][4] == 0x00ff00,
        'cycle 2 of length 3 reads 1 on its fourth tick, in its own colour')
    assert(texts[2][1] == texts[1][1] - 29, 'one box and a gap to the left')
    config.cycle2 = 'off'
    config.box = 'colour'

    -- True tile: the border is the metronome colour, the fill the configured one.
    config.highlight_current_tile = true
    product.on_start(api)
    tick()
    frame()
    local t = tiles[1]
    assert(#tiles == 1 and t[1] == 3200 and t[2] == 3201 and t[3] == 0, 'the true tile')
    assert(t[4] == BLACK and t[5] == WHITE and t[6] == 50 and t[7] == 2,
        'fill colour, metronome border, fill opacity, border width')
    config.change_fill_color = true
    tick()
    frame()
    assert(tiles[1][4] == GRAY and tiles[1][5] == GRAY and tiles[1][6] == 50,
        'the fill can follow the metronome too')
    config.change_fill_color = false

    config.tile_corners_only = true
    frame()
    assert(tiles[1][7] == 0 and #lines == 8, 'corners only: a fill without border, and eight strokes')
    local l = lines[1]
    -- SW corner of (3200, 3201) is fine (1280, 1408) -> screen (160, 176);
    -- a quarter of the way east is 4 px at this projection.
    assert(l[1] == 160 and l[2] == 176 and l[3] == 164 and l[4] == 176, 'a quarter edge from the corner')
    l = lines[2]
    assert(l[1] == 160 and l[2] == 176 and l[3] == 160 and l[4] == 180,
        'and a quarter edge toward the other neighbour, north')
    config.tile_corners_only = false

    -- Fadeout: full for the tick after a move, then 1 - t^2 over the time less a tick.
    config.tile_fadeout_ms = 800
    product.on_start(api)
    now = 1000
    frame()                        -- tick 0: first sight counts as a move
    tick()
    now = 1300
    frame()                        -- tick 1: the stop is stamped...
    now = 1600
    frame()                        -- ...and re-stamped until the tick ends
    assert(tiles[1][6] == 50, 'full strength the tick after the move')
    tick()
    now = 1700
    frame()                        -- 100 of 200 ms: 1 - 0.25
    assert(tiles[1][6] == 37 and tiles[1][7] == 2, 'three quarters of the fill, the border rounds to 2')
    now = 1780
    frame()                        -- 180 of 200 ms: 1 - 0.81
    assert(tiles[1][6] == 9 and tiles[1][7] == 0, 'a fifth of the fill, and the border has thinned away')
    now = 1800
    frame()
    assert(#tiles == 0, 'gone when the fadeout time is up')
    me.true_x = 3201
    frame()
    assert(tiles[1][6] == 50, 'a step brings it back at full strength')
    me.true_x = 3200
    config.tile_fadeout_ms = 0
    config.highlight_current_tile = false

    -- Overhead: the number at model height plus the lift, cycles either side.
    config.show_player_tick = true
    config.cycle2, config.cycle3 = 'overhead', 'both'
    frame()
    assert(#texts == 4, 'the main number and two cycles overhead, and cycle 3 boxed')
    local x0 = me.fine_x // 8
    assert(texts[1][1] == x0 and texts[1][2] == me.fine_z // 8 - (30 + 20),
        'above the model: element height plus the overhead height')
    assert(texts[1][4] == 0x00ffff, 'in the number colour')
    assert(texts[2][1] == x0 - 20 and texts[3][1] == x0 + 20, 'cycles a gap to either side')
    config.overhead_use_current_color = true
    frame()
    assert(texts[1][4] ~= 0x00ffff, 'or in the metronome colour')
    me.element_id = -1
    frame()
    assert(#texts == 1, 'no model, no overhead: only the boxed cycle')
    me.element_id = 3
    config.show_player_tick = false
    config.cycle2, config.cycle3 = 'off', 'off'

    -- Pointer: canvas pass, offset from the pointer.
    texts = {}
    product.on_draw_canvas(api, graphics)
    assert(#texts == 0, 'off by default')
    config.mouse_following_tick = true
    product.on_draw_canvas(api, graphics)
    assert(#texts == 1 and texts[1][1] == 310 and texts[1][2] == 190, 'beside the pointer')

    host.core.log('visual metronome behavior passed')
end }
