-- Appended to the actual shipped source by plugin_lua_test.c.
--
-- This overlay owns no widget, so there is no reconciled picture to pin; what
-- is pinned instead is the pair every ledger row for it is written in. WHAT it
-- draws: three markers, in one order, with all six arguments of each call, and
-- the four states in which one of them must not be there. And WHAT IT COSTS:
-- one declaration at start and, from then on, not one call into the layer --
-- no describe, no fence, no commit, no setter, and specifically no
-- draw_context, which a world-addressed marker has no use for and which would
-- suppress three good markers if its answer were obeyed.
--
-- The fabricated api records the ORDER of every call it is asked for, not just
-- the count. Without that, "hover off costs no pick" is invisible: the picture
-- is identical either way and only the sequence says which happened. The order
-- is over the api's FUNCTIONS only -- config is a plain table here, because
-- the sandbox removes setmetatable and a Lua test cannot see a field read.
return { id = 'tile-behavior', on_start = function(host)
    local calls, order, logs, declared = {}, {}, {}, {}
    local P = { available = true, opens = 0, capabilities = {} }

    local me = { true_x = 3200, true_z = 3201, level = 0, dest_x = 3204, dest_z = 3205 }
    local resident = true

    -- Every marker's fill is a DIFFERENT colour from its outline here, and none
    -- of the six is the shipped default. The shipped defaults are pinned off
    -- the schema below; what these values pin is the argument ORDER -- fill at
    -- 4, outline at 5, alpha at 6 -- which three markers sharing one colour,
    -- as they do out of the box, could never fail to satisfy.
    local config = { show_hover = true, show_dest = true,
        true_color = 0x00ffff, true_fill_color = 0x0066aa, true_fill_alpha = 40,
        dest_color = 0xffff00, dest_fill_color = 0xcc8800, dest_fill_alpha = 12,
        hover_color = 0xffffff, hover_fill_color = 0x778899, hover_fill_alpha = 3 }

    local porcelain = {
        open = function() P.opens = P.opens + 1; return P.available end,
        expect_unsupported = function(feature, why)
            declared[#declared + 1] = { feature = feature, why = why }
        end,
        -- Everything below is a trap, not a stub. A port that reached for any
        -- of them would be reconciling, announcing, lane-testing, or obeying a
        -- rectangle that is not an input to a single call it makes.
        draw_context = function()
            assert(false, 'a world tile is not placed out of the pass rectangle')
        end,
        describe = function() assert(false, 'this overlay describes nothing') end,
        invalidate = function() assert(false, 'this overlay describes nothing') end,
        fence = function() assert(false, 'nothing here is retained: nothing to reconcile') end,
        commit = function() assert(false, 'nothing here is retained: nothing to reconcile') end,
        set = function() assert(false, 'this overlay owns no control to move') end,
        has = function(name) P.capabilities[#P.capabilities + 1] = name; return true end,
        require = function(name) P.capabilities[#P.capabilities + 1] = name; return true end,
        notify = function() assert(false, 'this overlay announces nothing') end,
        element = function() assert(false, 'this overlay names no element') end,
        image = function() assert(false, 'this overlay holds no image') end,
        expect_absent = function() assert(false, 'no element here can be absent') end,
    }

    local api = {
        config = config,
        core = { log = function(text) logs[#logs + 1] = text end },
        world = { local_player = function()
            order[#order + 1] = 'local_player'
            if not resident then return nil end
            return me
        end },
        input = { hover_tile = function()
            order[#order + 1] = 'hover_tile'
            return 3200, 3201, 2
        end },
        widgets = {
            watch = function() assert(false, 'a ported plugin owns no watch') end,
            revalidate = function() assert(false, 'nothing here can move a widget') end,
        },
        porcelain = porcelain,
    }
    local graphics = { world_tile = function(...)
        calls[#calls + 1] = { ... }
        -- The pair the api hands back. It is a constant today -- the host's
        -- api_draw_tile returns void and swallows the budget refusal -- which
        -- is the gap on_start declares and the reason nothing reads this.
        return true, 'ok'
    end }

    local function frame()
        calls, order = {}, {}
        product.on_draw_world(api, graphics)
    end
    local function asked(name)
        for _, seen in ipairs(order) do
            if seen == name then return true end
        end
        return false
    end

    -- The panel is the schema, and the schema is eleven rows: three per marker
    -- plus the two switches. The shipped defaults are the shipped look, so a
    -- changed default is a changed picture on every lane at once.
    assert(product.id == 'tile-indicator-lua' and product.title == 'Tile Indicator (Lua)',
        'the Lua twin keeps its own id, and so its own settings section')
    local defaults = { true_color = '#00FFFF', true_fill_color = '#00FFFF',
        true_fill_alpha = '40', dest_color = '#FFFF00', dest_fill_color = '#FFFF00',
        dest_fill_alpha = '0', show_dest = '1', hover_color = '#FFFFFF',
        hover_fill_color = '#FFFFFF', hover_fill_alpha = '0', show_hover = '1' }
    assert(#product.config == 11, 'eleven config rows: three per marker and two toggles')
    for _, row in ipairs(product.config) do
        assert(defaults[row.key] == row.default, 'shipped default for ' .. row.key)
        defaults[row.key] = nil
    end
    assert(next(defaults) == nil, 'every declared row is one of the eleven')

    -- Start declares the one refusal this plugin cannot see, by name, so it is
    -- an expected finding in every capture instead of a comment.
    product.on_start(api)
    assert(P.opens == 1, 'on_start opens the porcelain layer exactly once')
    assert(#logs == 0, 'a layer that opened is not worth a line')
    assert(#declared == 1 and declared[1].feature == 'draw_refusal_readout',
        'the unreadable draw result is declared, not left unsaid')
    assert(declared[1].why:find('api_draw_tile', 1, true),
        'and the declaration names where the refusal is swallowed')

    frame()
    assert(#calls == 3, 'hover, true and destination markers must all draw')
    assert(calls[1][1] == 3200 and calls[1][2] == 3201 and calls[1][3] == 2,
        'hover uses picked level and draws first')
    -- Said twice, from two sides, and in this order on purpose: the committed
    -- negative control for this plugin swaps the two halves of on_draw_world
    -- and names the line above as the one that must catch it.
    assert(order[1] == 'hover_tile', 'the hover pick is the first thing the frame asks for')
    assert(calls[1][4] == 0x778899 and calls[1][5] == 0xffffff and calls[1][6] == 3,
        'hover passes fill, then outline, then alpha')
    assert(calls[2][1] == 3200 and calls[2][2] == 3201 and calls[2][3] == 0,
        'true marker wins hover overlap')
    assert(calls[2][4] == 0x0066aa and calls[2][5] == 0x00ffff and calls[2][6] == 40,
        'the true tile carries its own fill under its own outline')
    assert(calls[3][1] == 3204 and calls[3][2] == 3205 and calls[3][3] == 0,
        'destination uses routed flag')
    assert(calls[3][4] == 0xcc8800 and calls[3][5] == 0xffff00 and calls[3][6] == 12,
        'and the destination its own')

    me.dest_x, me.dest_z = me.true_x, me.true_z
    frame()
    assert(#calls == 2, 'arrival removes destination marker')
    me.dest_x, me.dest_z = 3204, 3205

    resident = false
    frame()
    assert(#calls == 1 and calls[1][3] == 2, 'hover works without a resident player')
    resident = true

    config.show_hover = false
    frame()
    assert(#calls == 2, 'hover setting takes effect immediately')
    assert(not asked('hover_tile'), 'and the switch is read BEFORE the pick, so off costs none')
    config.show_hover = true

    -- The destination switch returns before the comparison; all a test outside
    -- the engine can see of that is the marker going away on the next frame
    -- with no restart.
    config.show_dest = false
    frame()
    assert(#calls == 2, 'the destination toggle takes effect immediately')
    config.show_dest = true

    -- Aboard a vessel the api answers deck tiles as STAGING-ABSOLUTE addresses
    -- and world_tile draws them through the hull's live transform. The plugin's
    -- whole contribution to that is to not touch the numbers -- no clamp, no
    -- region fold, no level of its own.
    me.true_x, me.true_z, me.level = 6080, 6152, 1
    me.dest_x, me.dest_z = 6083, 6152
    frame()
    assert(calls[2][1] == 6080 and calls[2][2] == 6152 and calls[2][3] == 1,
        'a staging-absolute deck tile is passed through untouched')
    assert(calls[3][1] == 6083 and calls[3][2] == 6152 and calls[3][3] == 1,
        'and so is the deck destination')
    me.true_x, me.true_z, me.level = 3200, 3201, 0
    me.dest_x, me.dest_z = 3204, 3205

    -- Steady state. Sixty frames of the same picture. Every layer verb except
    -- open and the declaration is a trap above, so this run asserts the whole
    -- of "the steady state costs nothing" by surviving: no describe, no fence,
    -- no commit, no setter, no draw_context, no revalidate, and no capability
    -- asked -- least of all "map_flag", which could never answer false because
    -- a destination is always answered by the snapshot.
    for _ = 1, 60 do frame() end
    assert(#calls == 3, 'the sixtieth frame draws what the first one did')
    assert(#declared == 1, 'and the declaration is made once, not once a frame')
    assert(#P.capabilities == 0,
        'a destination is always answered by the snapshot: map_flag is not a capability')

    -- A host built without the layer, reached the way a re-enable reaches it:
    -- on_start again. The markers outrank the declaration, so they still draw
    -- -- and the plugin says so once rather than going quiet.
    P.available = false
    product.on_start(api)
    assert(#logs == 1, 'a host with no porcelain layer is said out loud, once')
    assert(logs[1]:find('porcelain', 1, true), 'and the line names what is missing')
    assert(#declared == 1, 'there is nowhere to declare the gap, so it is not declared')
    frame()
    assert(#calls == 3, 'the markers draw anyway')

    host.core.log('tile behavior passed')
end }
