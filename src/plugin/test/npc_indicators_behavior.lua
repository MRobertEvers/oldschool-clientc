-- Appended to the actual shipped source by plugin_lua_test.c.
--
-- What is pinned is WHICH tiles each render style marks, because that is the
-- whole difference between them: the true styles name absolute tiles off
-- true_x/true_z, the interpolated ones project fine corners, and the two
-- south-west styles mark the footprint's SW tile and nothing else.
return { id = 'npc-indicators-behavior', on_start = function(host)
    local tiles, hulls, lines, texts, projected, logs = {}, {}, {}, {}, {}, {}
    local P = { available = true }

    local config = { npcs = '', highlight_hull = false, highlight_tile = false,
        highlight_true_tile = false, highlight_sw_tile = false,
        highlight_sw_true_tile = false, draw_names = false, ignore_dead = true,
        color = 0x00ffff, fill_alpha = 20, border_width = 3 }

    -- A 3x3 boss mid-step: its server tile is (3200, 3201), and its model is
    -- half a tile east of that footprint's centre. Scene base is (3190, 3190),
    -- so the true footprint's centre is fine (11*128 + 192, 11*128 + 192).
    local boss = { name = 'Ezekiel', size = 3, true_x = 3200, true_z = 3201, level = 0,
        fine_x = 10 * 128 + 192 + 64, fine_z = 11 * 128 + 192,
        element_id = 7, health_ratio = 30, health_scale = 30 }
    local goblin = { name = '<col=ff0000>Goblin chief</col>', size = 1,
        true_x = 3195, true_z = 3195, level = 0, fine_x = 5 * 128 + 64, fine_z = 5 * 128 + 64,
        element_id = 8, health_ratio = 0, health_scale = 0 }
    local hob = { name = 'Hobgoblin', size = 1, true_x = 3196, true_z = 3196, level = 0,
        fine_x = 6 * 128 + 64, fine_z = 6 * 128 + 64, element_id = 9,
        health_ratio = 10, health_scale = 30 }
    local pool = { boss, goblin, hob }
    local walks = 0

    local api = {
        config = config,
        core = { log = function(text) logs[#logs + 1] = text end },
        world = { npc_next = function(cursor)
            if cursor == -1 then walks = walks + 1 end
            local next_cursor = cursor + 1
            if next_cursor >= #pool then return nil end
            return next_cursor, pool[next_cursor + 1]
        end },
        draw = {
            -- Screen = fine / 8, lifted by height: enough to read the
            -- corner back out of every line endpoint.
            project = function(x, z, height)
                projected[#projected + 1] = { x, z, height or 0 }
                return x // 8, z // 8 - (height or 0)
            end,
            element_height = function(element) return element * 10 end,
        },
        porcelain = {
            open = function() return P.available end,
            tile = function(...) tiles[#tiles + 1] = { ... }; return true end,
            hull = function(...) hulls[#hulls + 1] = { ... }; return true end,
        },
    }
    local graphics = {
        line = function(...) lines[#lines + 1] = { ... } end,
        text = function(...) texts[#texts + 1] = { ... } end,
        world_tile = function() assert(false, 'with a layer, tiles go through porcelain.tile') end,
        world_hull = function() assert(false, 'with a layer, hulls go through porcelain.hull') end,
    }

    local function frame()
        tiles, hulls, lines, texts, projected = {}, {}, {}, {}, {}
        product.on_draw_world(api, graphics)
    end
    local function list(value)
        config.npcs = value
        product.on_config_changed(api, 'npcs')
    end
    local function only(style)
        for _, key in ipairs({ 'highlight_hull', 'highlight_tile', 'highlight_true_tile',
                'highlight_sw_tile', 'highlight_sw_true_tile', 'draw_names' }) do
            config[key] = key == style
        end
    end

    assert(product.id == 'npc-indicators', 'its own id, and so its own settings section')
    local defaults = { npcs = '', highlight_hull = '1', highlight_tile = '0',
        highlight_true_tile = '0', highlight_sw_tile = '0', highlight_sw_true_tile = '0',
        draw_names = '0', ignore_dead = '1', color = '#00FFFF', fill_alpha = '20',
        border_width = '2' }
    for _, row in ipairs(product.config) do
        assert(defaults[row.key] == row.default, 'shipped default for ' .. row.key)
        defaults[row.key] = nil
    end
    assert(next(defaults) == nil, 'every reference render style has its row')

    product.on_start(api)
    assert(#logs == 0, 'a layer that opened is not worth a line')

    frame()
    assert(walks == 0, 'an empty list costs not one walk of the npc pool')

    list(' ezekiel , goblin* ')
    only('highlight_sw_true_tile')
    frame()
    assert(#tiles == 2, 'one SW true tile per listed npc: the boss and the goblin chief')
    local sw = tiles[1]
    assert(sw[1] == 3200 and sw[2] == 3201 and sw[3] == 0,
        "the boss's SW true tile is its server coordinate")
    assert(sw[4] == 0x00ffff and sw[5] == 0x00ffff and sw[6] == 20 and sw[7] == 3,
        'colour as fill and border, then the fill opacity, then the border width')
    assert(tiles[2][1] == 3195, 'a tagged name matches with its colour tags stripped')
    assert(#lines == 0 and #hulls == 0, 'and that style draws nothing else')

    only('highlight_true_tile')
    frame()
    assert(#tiles == 9 + 1, 'the true tile covers the whole 3x3 footprint, and the 1x1 too')
    local seen = {}
    for i = 1, 9 do seen[tiles[i][1] .. ':' .. tiles[i][2]] = true end
    for dz = 0, 2 do
        for dx = 0, 2 do
            assert(seen[(3200 + dx) .. ':' .. (3201 + dz)], 'footprint runs north-east of the SW tile')
        end
    end

    -- The interpolated SW tile follows the MODEL: half a tile east of the
    -- server footprint, so its SW corner is (10*128 + 64, 11*128) in fine units.
    only('highlight_sw_tile')
    pool = { boss }
    frame()
    assert(#lines == 4, 'a projected square is four edges')
    assert(projected[1][1] == 10 * 128 + 64 and projected[1][2] == 11 * 128,
        'its SW corner is the model centre less half the footprint')
    assert(projected[3][1] == 11 * 128 + 64 and projected[3][2] == 12 * 128,
        'and it is one tile across, not the footprint')
    assert(#tiles == 0, 'the interpolated style never names an absolute tile')

    only('highlight_tile')
    frame()
    assert(#lines == 4 and projected[3][1] == 13 * 128 + 64 and projected[3][2] == 14 * 128,
        'the interpolated tile spans the whole footprint')

    only('highlight_hull')
    frame()
    assert(#hulls == 1 and hulls[1][1] == 7 and hulls[1][2] == 0x00ffff and hulls[1][3] == 20,
        'the hull is the element, in the colour, at the fill opacity')

    only('draw_names')
    frame()
    assert(#texts == 1 and texts[1][3] == 'Ezekiel', 'the name is drawn above the boss')
    assert(projected[1][3] == 70 + 40, 'at the model height plus the reference lift')

    boss.element_id = -1
    only('highlight_hull')
    config.highlight_sw_true_tile = true
    frame()
    assert(#hulls == 0 and #tiles == 1, 'an unmodelled npc keeps its tiles and loses its hull')
    boss.element_id = 7

    pool = { boss, goblin, hob }
    boss.health_ratio = 0
    only('highlight_sw_true_tile')
    frame()
    assert(#tiles == 1 and tiles[1][1] == 3195, 'an emptied health bar is a dead npc and is skipped')
    config.ignore_dead = false
    frame()
    assert(#tiles == 2, 'unless dead npcs are wanted')
    config.ignore_dead = true
    boss.health_ratio = 30

    list('*goblin')
    frame()
    assert(#tiles == 1 and tiles[1][1] == 3196, 'a wildcard matches the whole name: Hobgoblin, not Goblin chief')

    list('ezek.el')
    frame()
    assert(#tiles == 0, 'a dot is a literal dot, not a pattern')

    P.available = false
    product.on_start(api)
    assert(#logs == 1 and logs[1]:find('porcelain', 1, true), 'a missing layer is said once')
    local raw = 0
    graphics.world_tile = function() raw = raw + 1; return true end
    list('Ezekiel')
    frame()
    assert(raw == 1, 'and the markers still draw through the raw verb')

    host.core.log('npc indicators behavior passed')
end }
