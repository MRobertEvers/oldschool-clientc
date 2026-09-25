-- Appended to the actual shipped source by plugin_lua_test.c.
--
-- The ledger calls loot-beam the only overlay plugin that is not an overlay,
-- and it had no behaviour test at all -- which is how four of its rows came to
-- be recorded defects. This is that test, and it is written in the pair every
-- one of those rows is written in.
--
-- WHAT it does: which tiles get a beam, in which colour, from which model,
-- recoloured with which arguments, at which yaw, and which of them come down
-- when the item under them goes. And WHAT IT COSTS: which verbs are asked,
-- how many times, and -- the whole point of a retained layer -- how few of
-- them are asked at all when nothing has changed.
--
-- The fabricated api records the ORDER and the ARGUMENTS of every call, not
-- just the count. Without the arguments "it recoloured the beam" is true of a
-- beam that is the wrong colour; without the order, "one assets.model call
-- across many dresses" is indistinguishable from a memo that happens to be
-- warm.
--
-- Every porcelain verb this plugin has no business calling is a TRAP rather
-- than a stub: describe, fence, commit, set and draw_context each abort with
-- the reason they do not belong here, so the steady-state claim is asserted by
-- sixty frames surviving rather than by a counter nobody reads.
return { id = 'loot-beam-behavior', on_start = function(host)

    ----------------------------------------------------------------------
    -- the fabricated world
    ----------------------------------------------------------------------

    local logs, findings, declared, calls, order = {}, {}, {}, {}, {}
    local P = { available = true, opens = 0, model_asks = {}, table_asks = 0,
                tiers_reads = 0, tier_calls = 0, every = {} }
    local assets = { model_asks = {} }
    local scene = { next_handle = 100, budget = 1000, live = {}, destroyed = {} }

    -- What the host has on its floor. Every field the plugin reads.
    local stacks = {}
    -- Who is standing on it, and on which storey. nil is a real state -- the
    -- lane before login, and between a logout and the next world -- and it is
    -- no floor to filter against.
    local me = { level = 0, true_x = 3210, true_z = 3424 }
    -- What the shipped asset folder holds. A name absent from here is a
    -- MISSING asset, which is the terminal state the model row is about.
    -- `Death rune` is the ordinary item the per-unit truncation is proved on;
    -- it used to be the coin, which is the one item that rule must not be
    -- proved on. There is deliberately NO coin row here: section 6b's first
    -- assertion is the shipped defect exactly as a user meets it, with nothing
    -- but the cache's own cost of 1 behind it.
    --
    -- The rows are keyed by NAME. `# Rune scimitar = 1` is a commented-out row
    -- and `1127 = 39000` is an obj id, and NEITHER may be read: the first is
    -- the shape the old whole-file gmatch matched anyway, and the second is
    -- the shape that shipped -- an id from one cache, applied to every cache.
    local files = { ['beam_modern.model'] = true, ['beam_light.model'] = true,
                    ['prices.txt'] = 'Rune scimitar=39000\n# a comment\n\n'
                        .. 'DEATH RUNE = 1\nnonsense\n'
                        .. '# Rune scimitar = 1\n1127 = 77\n' }

    local function record(name, ...)
        order[#order + 1] = name
        calls[#calls + 1] = { name, ... }
    end
    local function since(mark, name)
        local n = 0
        for i = mark + 1, #calls do
            if calls[i][1] == name then n = n + 1 end
        end
        return n
    end
    local function args(mark, name, which)
        local n = 0
        for i = mark + 1, #calls do
            if calls[i][1] == name then
                n = n + 1
                if n == which then return calls[i] end
            end
        end
    end
    local function names(mark)
        local out = {}
        for i = mark + 1, #calls do out[#out + 1] = calls[i][1] end
        return table.concat(out, ',')
    end

    ----------------------------------------------------------------------
    -- the config, off the shipped schema
    ----------------------------------------------------------------------

    local config = {}
    local function reset_config()
        config = {}
        for _, item in ipairs(product.config) do
            local value = item.default
            if item.type == 'int' then value = tonumber(value)
            elseif item.type == 'color' then value = tonumber(value:sub(2), 16)
            elseif item.type == 'bool' then value = value == '1' end
            config[item.key] = value
        end
    end
    reset_config()

    ----------------------------------------------------------------------
    -- the layer
    --
    -- tier is the C helper's two rules spelled again: strictly greater at
    -- every threshold, and a threshold at or below zero disables its tier.
    -- The C body is pinned by porcelain_test.c; what is pinned HERE is that
    -- the plugin asks for the answer instead of computing one, which is
    -- exactly what the two shipped plugins disagreeing about `>` cost.
    ----------------------------------------------------------------------

    local porcelain = {
        open = function() P.opens = P.opens + 1; return P.available end,
        expect_unsupported = function(feature, why)
            declared[#declared + 1] = { feature = feature, why = why }
        end,
        finding = function(verb, element, result, detail)
            findings[#findings + 1] =
                { verb = verb, element = element, result = result, detail = detail }
        end,
        tiers_from_config = function()
            P.tiers_reads = P.tiers_reads + 1
            record('tiers_from_config')
            if P.tiers_absent then return nil end
            return { low = config.low_value, medium = config.medium_value,
                     high = config.high_value, insane = config.insane_value }
        end,
        tier = function(t, value)
            P.tier_calls = P.tier_calls + 1
            if t.insane > 0 and value > t.insane then return 4 end
            if t.high > 0 and value > t.high then return 3 end
            if t.medium > 0 and value > t.medium then return 2 end
            if t.low > 0 and value > t.low then return 1 end
            return 0
        end,
        model = function(name)
            P.model_asks[name] = (P.model_asks[name] or 0) + 1
            record('porcelain.model', name)
            -- The verb's own contract: (nil, state), never a handle, and a
            -- terminal state is remembered by the LAYER -- so this fake keeps
            -- answering it and the plugin is the thing under test for asking
            -- only once.
            if files[name] then return nil, 'ready' end
            return nil, 'missing'
        end,
        table = function(asset, parse)
            P.table_asks = P.table_asks + 1
            record('porcelain.table', asset)
            local bytes = files[asset]
            if bytes == nil then return false end
            if bytes == 'pending' then return false end
            if P.table_parsed then return true end
            P.table_parsed = parse(bytes) ~= false
            return P.table_parsed
        end,
        every = function(cadence, fn)
            P.every[#P.every + 1] = { cadence = cadence, fn = fn }
        end,
        tick = function(cadence)
            record('porcelain.tick', cadence)
            for _, timer in ipairs(P.every) do
                if timer.cadence == cadence then timer.fn(0) end
            end
        end,
        -- Traps. A port that reached for any of these would be reconciling a
        -- description it does not have, obeying a rectangle a world object is
        -- not placed in, or announcing into the chat box.
        describe = function() assert(false, 'this plugin describes nothing') end,
        invalidate = function() assert(false, 'this plugin describes nothing') end,
        fence = function() assert(false, 'nothing here is retained: nothing to reconcile') end,
        commit = function() assert(false, 'nothing here is retained: nothing to reconcile') end,
        set = function() assert(false, 'this plugin owns no control to move') end,
        draw_context = function()
            assert(false, 'a beam is a world object, not a rectangle in a pass')
        end,
        element = function() assert(false, 'this plugin names no element') end,
        image = function() assert(false, 'this plugin holds no image') end,
        notify = function() assert(false, 'the count line is a diagnostic, not an announcement') end,
        hull = function() assert(false, 'a beam is not a hull') end,
        expect_absent = function() assert(false, 'no element here can be absent') end,
    }

    ----------------------------------------------------------------------
    -- the api
    ----------------------------------------------------------------------

    local api = {
        config = config,
        core = { log = function(text) logs[#logs + 1] = text end },
        world = {
            -- The floor the beams are for. loot-beam did not read it at all
            -- and lit every plane the scene had loaded; ground_items.lua has
            -- always filtered on `obj.level == me.level`.
            local_player = function()
                record('local_player')
                return me
            end,
            item_next = function(cursor)
            -- Recorded, because "a burst of ten spawns rebuilds once" is a
            -- claim about the WALK. Counting objects made cannot see it: a
            -- floor that qualifies for nothing makes none either way.
                record('item_next', cursor)
                local index = cursor + 2
                if index > #stacks then return nil end
                return index - 1, stacks[index]
            end },
        -- A fixed answer per colour, so every recolour argument below is a
        -- number this file can work out by hand.
        draw = { hsl_from_rgb = function(rgb)
            record('hsl_from_rgb', rgb)
            if rgb == 0x111111 then return (3 << 10) | (2 << 7) | 10 end
            return (10 << 10) | (5 << 7) | 60
        end },
        assets = { model = function(name)
            assets.model_asks[name] = (assets.model_asks[name] or 0) + 1
            record('assets.model', name)
            if not files[name] then return nil, 'missing' end
            if files[name] == 'nohandle' then return nil, 'pending' end
            return 7000 + #name, 'ready'
        end },
        scene = {
            instance_create = function()
                record('instance_create')
                local count = 0
                for _ in pairs(scene.live) do count = count + 1 end
                if count >= scene.budget then return nil, 'budget' end
                scene.next_handle = scene.next_handle + 1
                scene.live[scene.next_handle] = true
                return scene.next_handle, 'ok'
            end,
            instance_destroy = function(handle)
                record('instance_destroy', handle)
                scene.destroyed[#scene.destroyed + 1] = handle
                scene.live[handle] = nil
            end,
            instance_model = function(handle, model)
                record('instance_model', handle, model); return true, 'ok'
            end,
            instance_recolor = function(handle, face, hsl)
                record('instance_recolor', handle, face, hsl)
                if scene.refuse_recolor then return false, 'invalid' end
                return true, 'ok'
            end,
            instance_clear_recolors = function(handle) record('clear_recolors', handle) end,
            instance_light = function(handle, a, b)
                record('instance_light', handle, a, b); return true, 'ok'
            end,
            instance_position = function(handle, x, z, level, pitch, yaw)
                record('instance_position', handle, x, z, level, pitch, yaw)
                return true, 'ok'
            end,
            instance_active = function(handle, on) record('instance_active', handle, on) end,
        },
        porcelain = porcelain,
    }

    local function tick()
        product.on_logic_tick(api, {})
    end
    local function beams_up()
        local n = 0
        for _ in pairs(scene.live) do n = n + 1 end
        return n
    end
    -- The cache's own name for each id the fixture drops. A ground-item
    -- snapshot carries one on every lane, and the price table is keyed on it,
    -- so a fixture without names would price everything from the cache and
    -- never exercise an override at all.
    local OBJ_NAME = { [1127] = 'Rune scimitar', [560] = 'Death rune',
                       [995] = 'Coins', [526] = 'Bones' }
    local function stack(id, x, z, cost, count, level, name)
        return { obj_id = id, tile_x = x, tile_z = z, level = level or 0,
                 cost = cost, count = count or 1,
                 name = name or OBJ_NAME[id] or ('Obj ' .. id) }
    end
    local function start()
        me = { level = 0, true_x = 3210, true_z = 3424 }
        P.every, P.table_parsed, P.table_asks, P.tier_calls = {}, false, 0, 0
        P.model_asks, assets.model_asks = {}, {}
        scene.live, scene.destroyed, scene.next_handle = {}, {}, 100
        logs, findings, declared = {}, {}, {}
        product.on_start(api)
    end

    ----------------------------------------------------------------------
    -- 1. the schema IS the settings tab, and a changed default is a changed
    --    picture on every lane at once
    ----------------------------------------------------------------------

    assert(product.id == 'loot-beam' and product.title == 'Loot Beams')
    local defaults = { tier = 'high', style = 'modern', value_mode = 'alch',
        low_value = '20000', medium_value = '100000', high_value = '1000000',
        insane_value = '10000000', low_color = '#66B2FF', medium_color = '#99FF99',
        high_color = '#FF9600', insane_color = '#FF66B2', spin = '90' }
    assert(#product.config == 12, 'twelve rows: three choices, four values, four colours, a spin')
    for _, row in ipairs(product.config) do
        assert(defaults[row.key] == row.default, 'shipped default for ' .. row.key)
        defaults[row.key] = nil
    end
    assert(next(defaults) == nil, 'every declared row is one of the twelve')
    -- The four value rows are the four porcelain.tiers_from_config reads, by
    -- the names it reads them under. A rename here silently disarms every
    -- tier: the verb would answer zeros and the zero gate would turn all four
    -- off, which is a plugin that draws nothing and says nothing.
    local wanted = { low_value = true, medium_value = true, high_value = true,
                     insane_value = true }
    for _, row in ipairs(product.config) do wanted[row.key] = nil end
    assert(next(wanted) == nil, 'the four keys the layer reads are the four declared')

    ----------------------------------------------------------------------
    -- 2. start: the layer is opened once, the gap is declared, the
    --    thresholds are read once, the file is asked for once, and the
    --    cadence is registered -- and the cadence is the LOGIC tick
    ----------------------------------------------------------------------

    start()
    assert(P.opens == 1, 'on_start opens the porcelain layer exactly once')
    assert(#declared == 1 and declared[1].feature == 'shared_price_table',
        'the one ledger row the layer cannot satisfy is declared, not left unsaid')
    assert(declared[1].why:find('per plugin handle', 1, true),
        'and the declaration says WHY, in terms a reader can disagree with')
    assert(P.tiers_reads == 1, 'the four thresholds are read once at start')
    assert(#P.every == 1 and P.every[1].cadence == 'logic_tick',
        'one cadence, and it is the client logic tick -- no lane is named anywhere')
    assert(P.table_asks == 1 and P.table_parsed, 'the price file is asked for once')
    assert(#findings == 0, 'a start that went well raises nothing')

    ----------------------------------------------------------------------
    -- 3. the price table: rows parsed, junk skipped, and the override is the
    --    number the tier is measured against
    ----------------------------------------------------------------------

    -- `Rune scimitar=39000` and `DEATH RUNE = 1` are rows; the comment, the
    -- blank, the unreadable line and the commented-out row are not, and none
    -- of them costs the file. `1127 = 77` IS read as a row -- it is a name
    -- like any other, and no cache calls an item "1127", so it matches
    -- nothing. That is the point: an obj id in this file is now inert rather
    -- than being a price silently applied to whatever item inherited the
    -- number on the cache that actually booted.
    assert(logs[1] == 'prices.txt: 3 price overrides',
        'three rows, three skipped: ' .. logs[1])

    ----------------------------------------------------------------------
    -- 4. cadence: a burst of ten spawns is ONE rebuild, and a tick with
    --    nothing dirty walks nothing at all
    ----------------------------------------------------------------------

    stacks = { stack(1127, 3210, 3424, 39000) }   -- 39000 raw, 23400 alch
    local mark = #calls
    for _ = 1, 10 do product.on_item_spawn(api, {}) end
    tick()
    -- One walk of the floor: the stack, then the end of the list. A zone
    -- update carrying a dozen OBJ_ADDs must not walk it a dozen times.
    assert(since(mark, 'item_next') == 2, 'ten spawns in one tick is one walk')
    assert(since(mark, 'tiers_from_config') == 0 and P.tiers_reads == 1,
        'the thresholds are read per config CHANGE, not once per rebuild')
    mark = #calls
    tick(); tick(); tick()
    assert(names(mark) == 'porcelain.tick,porcelain.tick,porcelain.tick',
        'a tick with nothing dirty forwards and returns: ' .. names(mark))

    ----------------------------------------------------------------------
    -- 5. the tier: the floor, the strict comparison, and the zero gate
    ----------------------------------------------------------------------

    -- Default floor is `high` and the stack alches to 23400, which clears
    -- `low` and nothing else -- so the default config lights nothing.
    assert(beams_up() == 0, 'beam from high does not light a low-value drop')

    config.tier = 'low'
    product.on_config_changed(api, 'tier')
    mark = #calls
    tick()
    assert(beams_up() == 1, 'lowering the floor lights it')
    -- The colour is the tier the value EARNED, not the floor it cleared: 23400
    -- is a LOW-tier value however low the floor is set.
    assert(args(mark, 'hsl_from_rgb', 1)[2] == config.low_color,
        'the colour is the tier the value earned')

    -- Strictly greater. 23400 alch against a low threshold of exactly 23400 is
    -- NOT a low-value item: this is the operator the two shipped plugins
    -- disagreed about, and >= would keep the beam standing here.
    config.low_value = 23400
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 0, 'an item worth exactly the threshold is not over it')
    config.low_value = 23399
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 1, 'one coin over it is')

    -- A threshold at or below zero turns its tier OFF. Before the port a zero
    -- meant "everything qualifies", which is what the one rs289lc capture of
    -- this plugin froze.
    config.low_value = 0
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 0, 'a zero threshold disables its tier rather than matching all')
    config.low_value = 20000
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 1, 'and a real threshold arms it again')

    -- The plugin does not own a comparison at all: every decision above went
    -- through the layer's one helper.
    assert(P.tier_calls > 0, 'the tier test is the layer\'s one helper, not a local >=')

    ----------------------------------------------------------------------
    -- 6. value mode, and the price override applied per UNIT before the
    --    stack multiply -- which is where the reference truncates too
    ----------------------------------------------------------------------

    -- The death rune has a cache cost of 100 and a prices.txt override of 1
    -- (spelled `DEATH RUNE`, so this is also where the match is proved
    -- case-insensitive), and there are ten of it on the tile. Priced from the cache the stack alches
    -- to (100*3//5)*10 = 600; priced from the override it alches to
    -- (1*3//5)*10 = 0. A threshold of 500 tells the two apart.
    stacks = { stack(560, 3211, 3425, 100, 10) }
    config.tier, config.low_value = 'low', 500
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 0, 'the override is the price, not the cache cost')
    -- And the truncation is per UNIT, before the stack multiply, which is
    -- where the reference truncates: 1*3//5 = 0, ten of them is 0. Truncate
    -- after the multiply instead and it is (1*10*3)//5 = 6, which lights a
    -- beam over ten coins.
    config.low_value = 5
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 0, 'the alch price is truncated per UNIT, before the stack multiply')
    -- ...for every item but ONE. See section 6b: a coin's unit alch price
    -- truncates to zero, which is the item this rule cannot be applied to.
    config.value_mode = 'value'
    product.on_config_changed(api, 'value_mode')
    tick()
    -- exchange = 1 * 10 = 10, over 5.
    assert(beams_up() == 1, 'value mode takes the raw price')
    config.value_mode = 'alch'
    product.on_config_changed(api, 'value_mode')

    -- The alch arithmetic, on the pair ground-items' own test uses: 65000
    -- cache cost alches to 39000, and the truncation is per unit.
    stacks = { stack(4151, 3212, 3426, 65000, 1) }
    config.low_value, config.medium_value = 38999, 39000
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 1, '65000 alches to 39000: over the low threshold')
    config.low_value = 39000
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 0, 'and not over a threshold of exactly 39000')

    ----------------------------------------------------------------------
    -- 6b. COINS, the drop this plugin could not see
    --
    -- A coin's cache cost is 1. floor(1 * 0.6) is 0. The alch price is
    -- truncated per unit, and `alch` is the shipped default value_mode -- so
    -- five thousand coins were worth ZERO and no threshold above zero could
    -- ever be cleared by a pile of gold. The archetypal loot beam could not
    -- fire.
    --
    -- The reference corrects exactly this, in GroundItemsPlugin.buildGroundItem
    -- under the comment "Update item price in case it is coins":
    -- setHaPrice(1) and setGePrice(1), so a coin is one gp under both prices
    -- and a pile is worth its count.
    ----------------------------------------------------------------------

    start()
    stacks = { stack(995, 3213, 3427, 1, 5000) }
    config.tier, config.value_mode = 'low', 'alch'
    config.low_value, config.medium_value = 4999, 100000
    product.on_config_changed(api, 'low_value')
    product.on_item_spawn(api, {})
    tick()
    assert(beams_up() == 1, 'five thousand coins raises a beam')
    -- Worth EXACTLY its count, and not merely "more than nothing": a rule that
    -- rounded the unit price UP instead of correcting it would put the pile at
    -- 5000 too, but one that dropped the truncation altogether would not.
    config.low_value = 5000
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 0, 'a coin is one gp: the pile is worth 5000, not 0 and not 300000')
    config.low_value = 4999
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 1)
    -- Both prices, so the other two modes agree with it rather than routing
    -- around it.
    for _, mode in ipairs({ 'value', 'highest' }) do
        config.value_mode, config.low_value = mode, 5000
        product.on_config_changed(api, 'value_mode')
        tick()
        assert(beams_up() == 0, mode .. ' mode prices a coin at one gp too')
        config.low_value = 4999
        product.on_config_changed(api, 'low_value')
        tick()
        assert(beams_up() == 1, mode .. ' mode still lights the pile')
    end
    config.value_mode = 'alch'
    product.on_config_changed(api, 'value_mode')
    -- The rule is one gp PER COIN and not a floor under the pile: the value
    -- tracks the count all the way down to one.
    config.low_value = 1
    product.on_config_changed(api, 'low_value')
    stacks = { stack(995, 3213, 3427, 1, 1) }
    product.on_item_changed(api, {})
    tick()
    assert(beams_up() == 0, 'one coin is worth one, which is not OVER a threshold of one')
    stacks = { stack(995, 3213, 3427, 1, 2) }
    product.on_item_changed(api, {})
    tick()
    assert(beams_up() == 1, 'and two coins are worth two, which is')

    -- The correction is written AFTER the price lookup it overrides, so a
    -- prices.txt row for coins does not get a say either -- it is a correction
    -- of whatever the lookup answered, not a fallback for a lookup that
    -- answered nothing.
    files['prices.txt'] = 'Coins = 100\n'
    start()
    assert(logs[1] == 'prices.txt: 1 price overrides', logs[1])
    config.tier, config.value_mode = 'low', 'alch'
    config.low_value, config.medium_value = 5000, 100000
    product.on_config_changed(api, 'low_value')
    stacks = { stack(995, 3213, 3427, 1, 5000) }
    product.on_item_spawn(api, {})
    tick()
    assert(beams_up() == 0,
        'a prices.txt row for coins is overridden too: the pile is 5000, not 300000')
    config.low_value = 4999
    product.on_config_changed(api, 'low_value')
    tick()
    assert(beams_up() == 1, 'and still 5000')
    files['prices.txt'] = 'Rune scimitar=39000\n# a comment\n\n'
        .. 'DEATH RUNE = 1\nnonsense\n# Rune scimitar = 1\n1127 = 77\n'

    ----------------------------------------------------------------------
    -- 6b2. an OBJ ID in the price table prices nothing
    --
    -- The shipped table was `obj_id = price` and its header said the ids were
    -- for one specific cache. Read on any other cache a row matched nothing --
    -- or matched whatever item had inherited the number, which is a
    -- twenty-five-million-gp beam over a bronze dagger. Since an override is
    -- the only thing that can lift a stack to these thresholds (a cache cost
    -- is a shop number and no shop number reaches a million), that made a beam
    -- structurally impossible on every lane but the one the file was written
    -- against. The row is now inert: it is a name, and nothing is called 1127.
    ----------------------------------------------------------------------

    start()
    config.tier, config.value_mode = 'low', 'value'
    config.low_value = 100
    product.on_config_changed(api, 'low_value')
    -- `1127 = 77` is in the file and this stack IS obj 1127, with a cache cost
    -- of 1 -- but it is called "Bronze dagger", so the row cannot reach it.
    -- Priced by the id row it would be 77 and lit; priced from the cache it is
    -- 1 and dark.
    stacks = { stack(1127, 3210, 3424, 1, 1, 0, 'Bronze dagger') }
    product.on_item_spawn(api, {}); tick()
    assert(beams_up() == 0,
        'an obj id in the price table is a name nothing has, not a price')
    -- The same stack under the name the row for it IS keyed on: 39000, lit.
    stacks = { stack(1127, 3210, 3424, 1, 1, 0, 'Rune scimitar') }
    product.on_item_changed(api, {}); tick()
    assert(beams_up() == 1, 'and the NAME row prices it')

    ----------------------------------------------------------------------
    -- 6b3. A DAT1 FLOOR AT THE SHIPPED DEFAULTS -- the case the whole
    --      name-keyed table exists for, and the one no capture had
    --
    -- Every assertion above about the price table arranges its own
    -- thresholds: `tier = low`, `low_value = 100`. That proves the LOOKUP and
    -- it cannot prove the CLAIM, which is that on a 2004-era cache the
    -- shipped file lights the shipped tier -- and that claim is the entire
    -- reason the table was re-keyed from obj ids to names. It went
    -- unphotographed for exactly as long: the live lane's drive drops a rune
    -- scimitar, a dragon med helm, 20,000 coins and five sharks, which alch to
    -- 15,360 / 60,000 / 20,000 / 180 against `high_value` = 1,000,000. Those
    -- four are a CORRECTLY dark floor, so the one live picture of this plugin
    -- was a picture of nothing and could not be told from a broken one.
    --
    -- The two rows below are the two the live capture now stands on
    -- (lb-priced-live.png, ~torirsbeamdrop), and they are here in their
    -- SHIPPED spelling with the SHIPPED defaults around them. Both are chosen
    -- so the cache alone cannot light them:
    --
    --   Red partyhat      LostCity's holiday.obj states no cost, so
    --                     ObjType.cost keeps its default of 1 and the cache
    --                     alches the hat to floor(1 * 0.6) = 0. Nothing but
    --                     the row can raise this beam, and the row makes it
    --                     900,000,000 -- the INSANE tier, the pink beam the
    --                     CS2 lanes were photographed with.
    --
    --   Dragon chainbody  cost 250,000, so the cache alches it to 150,000: a
    --                     MEDIUM item, under the `high` floor and therefore
    --                     dark. The row lifts it to 1,080,000, one rung over
    --                     `high_value`, and it beams HIGH. This is the row
    --                     shape an id-keyed table got subtly wrong rather
    --                     than blankly -- a tier moved, not a beam invented.
    --
    -- reset_config() is deliberately NOT called: it rebinds the upvalue and
    -- `api.config` still points at the table the plugin reads, so the six
    -- rows this section needs are restored by name.
    ----------------------------------------------------------------------

    config.tier, config.value_mode = 'high', 'alch'
    config.low_value, config.medium_value = 20000, 100000
    config.high_value, config.insane_value = 1000000, 10000000
    files['prices.txt'] = '# the two rows the live capture stands on\n'
        .. 'Red partyhat     = 1500000000\nDragon chainbody = 1800000\n'
    start()
    assert(logs[1] == 'prices.txt: 2 price overrides', logs[1])

    -- Two tiles, because one beam per tile is coloured by the best thing on
    -- it: piled together the hat would have hidden the chainbody.
    stacks = { stack(1038, 3210, 3424, 1, 1, 0, 'Red partyhat'),
               stack(3140, 3211, 3424, 250000, 1, 0, 'Dragon chainbody') }
    product.on_item_spawn(api, {})
    mark = #calls
    tick()
    assert(beams_up() == 2, 'the shipped table lights the shipped tier on a dat1 floor')
    assert(logs[#logs] == '2 beam(s) over 2 ground stack(s)', logs[#logs])
    -- The want set is walked SORTED, so "0:3210:3424" is dressed before
    -- "0:3211:3424" and these two colours are in a fixed order.
    assert(args(mark, 'hsl_from_rgb', 1)[2] == config.insane_color,
        'a hat the cache values at zero is an INSANE-tier beam under the row')
    assert(args(mark, 'hsl_from_rgb', 2)[2] == config.high_color,
        'and a chainbody the cache alches to a medium 150,000 is a HIGH one')
    assert(args(mark, 'instance_position', 1)[3] == 3210)
    assert(args(mark, 'instance_position', 2)[3] == 3211,
        'each beam stands on its own tile')

    -- The other half of the claim, and the half that says the rows are doing
    -- the work: the SAME floor, the SAME shipped thresholds, a table with no
    -- rows in it. 0 and 150,000 against a floor of 1,000,000 is dark, which
    -- is what every dat1 capture of this plugin has been.
    files['prices.txt'] = '# a table with no rows in it\n'
    start()
    assert(logs[1] == 'prices.txt: 0 price overrides', logs[1])
    product.on_item_spawn(api, {})
    tick()
    assert(beams_up() == 0,
        'without the rows the same dat1 floor is dark -- a cache cost is a shop number')
    assert(logs[#logs] == '0 beam(s) over 2 ground stack(s)',
        'and the count line says so out loud, which is the reading it exists to give')

    files['prices.txt'] = 'Rune scimitar=39000\n# a comment\n\n'
        .. 'DEATH RUNE = 1\nnonsense\n# Rune scimitar = 1\n1127 = 77\n'

    ----------------------------------------------------------------------
    -- 6c. the PLANE
    --
    -- The client tracks every ground stack in the LOADED SCENE, not only the
    -- ones on the storey the player is standing on. loot-beam read none of
    -- that: a drop in the room above got an object, a position, a yaw every
    -- frame and a line in the count -- and no pixels, because the host drops a
    -- plugin object whose level is not the camera's before the painter sees
    -- it. So the cost was the count line lying and the 64-object budget being
    -- spent on beams that could not be drawn. ground_items.lua has always
    -- filtered on `obj.level == me.level`; this is the same test.
    ----------------------------------------------------------------------

    start()
    config.tier, config.low_value, config.medium_value = 'low', 1000, 100000
    product.on_config_changed(api, 'low_value')
    -- Two upstairs, one on the player's own floor, all three over the
    -- threshold.
    stacks = { stack(1127, 3210, 3424, 200000, 1, 1),
               stack(1127, 3211, 3425, 200000, 1, 1),
               stack(1127, 3300, 3500, 200000, 1, 0) }
    me.level = 0
    product.on_item_spawn(api, {})
    mark = #calls
    tick()
    assert(beams_up() == 1, 'only the stack on the player\'s own plane is lit')
    -- And it is that stack: the two beams that used to stand here were over
    -- tiles the player is looking at the ceiling of.
    assert(args(mark, 'instance_position', 1)[3] == 3300 and
           args(mark, 'instance_position', 1)[4] == 3500 and
           args(mark, 'instance_position', 1)[5] == 0,
        'the beam is on the player\'s storey, over the stack that is on it')
    -- The count line is about the floor the player can see, so the TALLY is
    -- filtered by the same test: "0 beam(s) over 3 ground stack(s)" would read
    -- as a threshold set too high when two of the three are upstairs.
    assert(logs[#logs] == '1 beam(s) over 1 ground stack(s)', logs[#logs])

    -- Up the stairs. The same three stacks, a different floor: now it is the
    -- two that are lit and the third that is not.
    me.level = 1
    product.on_item_changed(api, {})
    tick()
    assert(beams_up() == 2, 'the player moves a storey and the lit set moves with them')
    assert(logs[#logs] == '2 beam(s) over 2 ground stack(s)', logs[#logs])

    -- No local player -- the lane before login, and between a logout and the
    -- next world -- is no floor to filter against, and nothing is lit.
    me = nil
    product.on_item_changed(api, {})
    tick()
    assert(beams_up() == 0, 'no player is no floor, and no beams')
    assert(logs[#logs] == '0 beam(s) over 0 ground stack(s)', logs[#logs])
    me = { level = 1, true_x = 3210, true_z = 3424 }
    product.on_item_changed(api, {})
    tick()
    assert(beams_up() == 2, 'and they come back when the player does')

    ----------------------------------------------------------------------
    -- 7. one beam per TILE, coloured by the best stack on it
    ----------------------------------------------------------------------

    start()
    config.tier, config.low_value, config.medium_value = 'low', 1000, 20000
    config.high_value, config.insane_value = 2000000, 10000000
    product.on_config_changed(api, 'low_value')
    -- A rune scimitar and a bone under it: one tile, one beam, and the colour
    -- of the scimitar.
    stacks = { stack(526, 3210, 3424, 4000), stack(1127, 3210, 3424, 200000) }
    product.on_item_spawn(api, {})
    mark = #calls
    tick()
    local dressed = mark
    assert(since(mark, 'instance_create') == 1, 'two stacks on one tile is one beam')
    assert(beams_up() == 1)
    -- The scimitar's cache cost is overridden to 39000 by prices.txt and
    -- alches to 23400, which clears medium; the bone's 4000 alches to 2400,
    -- which clears low. The tile carries medium, because it is coloured by
    -- the best thing on it.
    assert(since(mark, 'hsl_from_rgb') == 1)
    assert(args(mark, 'hsl_from_rgb', 1)[2] == config.medium_color,
        'the tile takes the colour of the best thing on it')

    ----------------------------------------------------------------------
    -- 8. the dress: the order, the face keys and every argument, for both
    --    styles -- and the recolours cleared before a re-dress
    ----------------------------------------------------------------------

    -- hsl_from_rgb answers h=10 s=5 l=60 for everything but 0x111111.
    -- MODERN: body 26432 loses a notch of saturation, core 26584 gains 24
    -- luminance, and the model is lit at 75/1875.
    mark = dressed
    assert(args(mark, 'instance_model', 1)[3] == 7000 + #'beam_modern.model',
        'the MODERN style is dressed from its own shipped file')
    assert(args(mark, 'instance_recolor', 1)[3] == 26432 and
           args(mark, 'instance_recolor', 1)[4] == (10 << 10) | (4 << 7) | 60,
        'the body band loses one notch of saturation')
    assert(args(mark, 'instance_recolor', 2)[3] == 26584 and
           args(mark, 'instance_recolor', 2)[4] == (10 << 10) | (5 << 7) | 84,
        'the core keeps the saturation and gains 24 luminance')
    assert(args(mark, 'instance_light', 1)[3] == 75 and
           args(mark, 'instance_light', 1)[4] == 1875,
        'and the model is lit well above the default, or the bands read as plastic')
    assert(names(mark):find(
        'clear_recolors,instance_model,instance_recolor,instance_recolor,instance_light', 1, true),
        'recolours are cleared BEFORE the dress, not after: ' .. names(mark))

    -- LIGHT has one band and no core, and switching style re-dresses every
    -- standing beam -- which is the whole reason on_config_changed dirties.
    config.style = 'light'
    product.on_config_changed(api, 'style')
    mark = #calls
    tick()
    assert(since(mark, 'instance_recolor') == 1, 'the LIGHT style has one band and no core')
    assert(args(mark, 'instance_recolor', 1)[3] == 6371, 'and it is band 6371')
    assert(args(mark, 'instance_model', 1)[3] == 7000 + #'beam_light.model',
        'from the other shipped file')
    assert(since(mark, 'instance_create') == 0, 'a re-dress is not a new object')

    -- A dull colour keeps its saturation: taking a notch off sat <= 2 pushes
    -- the band to grey.
    config.medium_color = 0x111111
    config.style = 'modern'
    product.on_config_changed(api, 'medium_color')
    mark = #calls
    tick()
    assert(args(mark, 'instance_recolor', 1)[4] == (3 << 10) | (2 << 7) | 10,
        'an already-dull colour is not desaturated further')
    assert(args(mark, 'instance_recolor', 2)[4] == (3 << 10) | (2 << 7) | 34,
        'and its core still gains the 24')

    ----------------------------------------------------------------------
    -- 9. the model is asked for ONCE per style, however many dresses
    ----------------------------------------------------------------------

    assert(assets.model_asks['beam_modern.model'] == 1 and
           assets.model_asks['beam_light.model'] == 1,
        'one assets.model call per style across every dress')
    assert(P.model_asks['beam_modern.model'] == 1,
        'and one layer ask per style: the memo is the handle, not the question')

    ----------------------------------------------------------------------
    -- 10. a MISSING model is asked once, reported once, and never asked again
    ----------------------------------------------------------------------

    files['beam_modern.model'] = nil
    start()
    config.tier, config.low_value = 'low', 1000
    product.on_config_changed(api, 'low_value')
    stacks = { stack(1127, 3210, 3424, 200000), stack(1127, 3211, 3425, 200000),
               stack(1127, 3212, 3426, 200000) }
    product.on_item_spawn(api, {})
    tick()
    product.on_item_spawn(api, {}); tick()
    product.on_item_spawn(api, {}); tick()
    assert(P.model_asks['beam_modern.model'] == 1,
        'a missing model is asked for exactly once, not once per dress for ever')
    assert(assets.model_asks['beam_modern.model'] == nil,
        'and the handle is never asked for at all once the state is terminal')
    assert(beams_up() == 3, 'the objects still exist -- it is the model that is missing')
    files['beam_modern.model'] = true

    ----------------------------------------------------------------------
    -- 11. the object budget: a STABLE clipped set, and one finding
    ----------------------------------------------------------------------

    scene.budget = 64
    start()
    config.tier, config.low_value = 'low', 1000
    product.on_config_changed(api, 'low_value')
    -- Sixty-five qualifying tiles, handed over in DESCENDING tile order --
    -- which is the whole test. The want set is walked in the order it was
    -- built unless something sorts it, so a plugin that does not sort clips
    -- the lowest tile here and the highest one when the floor comes back the
    -- other way round. A plugin that sorts clips the same tile both times.
    local function floor_of(direction)
        stacks = {}
        for i = 0, 64 do
            local z = direction > 0 and (3400 + i) or (3464 - i)
            stacks[#stacks + 1] = stack(1127, 3210, z, 200000)
        end
    end
    local function positioned(from)
        local out = {}
        for i = from + 1, #calls do
            if calls[i][1] == 'instance_position' then
                out[calls[i][3] .. ':' .. calls[i][4]] = true
            end
        end
        return out
    end
    local function budget_findings()
        local n = 0
        for _, f in ipairs(findings) do
            if f.result == 'budget' then n = n + 1 end
        end
        return n
    end
    floor_of(-1)
    product.on_item_spawn(api, {})
    mark = #calls
    tick()
    assert(beams_up() == 64, '65 qualifying tiles, 64 objects')
    local first = positioned(mark)
    assert(budget_findings() == 1, 'the refusal is ONE finding')
    assert(findings[1].verb == 'scene' and findings[1].detail:find('instance_create', 1, true),
        'and it names the verb that was refused')
    -- The tile that loses is the LAST one in TILE order, whatever order the
    -- host handed the floor over in.
    assert(not first['3210:3464'], 'the clipped tile is the last in tile order')
    for i = 0, 63 do
        assert(first['3210:' .. (3400 + i)], 'the kept set is the 64 lowest tiles')
    end
    -- Same floor, handed over the other way round. Without a total order the
    -- clipped set is a different arbitrary 64 and the floor flickers.
    floor_of(1)
    product.on_item_spawn(api, {})
    mark = #calls
    tick()
    local second = positioned(mark)
    for key in pairs(first) do assert(second[key], 'clipped set moved: ' .. key) end
    for key in pairs(second) do assert(first[key], 'clipped set moved: ' .. key) end
    assert(budget_findings() == 1, 'and the second refusal is not a second line')
    scene.budget = 1000

    ----------------------------------------------------------------------
    -- 12. the lifetime: a beam is keyed on the item under it and comes down
    --     with it
    ----------------------------------------------------------------------

    start()
    config.tier, config.low_value = 'low', 1000
    product.on_config_changed(api, 'low_value')
    stacks = { stack(1127, 3210, 3424, 200000), stack(1127, 3300, 3500, 200000) }
    product.on_item_spawn(api, {})
    tick()
    assert(beams_up() == 2)
    -- The stack on the first tile is taken. The beam over it must go, and the
    -- other one must not: keyed on the tile, not on the set.
    table.remove(stacks, 1)
    product.on_item_despawn(api, {})
    mark = #calls
    tick()
    assert(beams_up() == 1, 'the beam comes down with the item under it')
    assert(since(mark, 'instance_destroy') == 1, 'exactly one object destroyed')
    assert(since(mark, 'instance_create') == 0, 'and the survivor is not re-made')
    -- Nothing under it at all.
    stacks = {}
    product.on_item_despawn(api, {})
    tick()
    assert(beams_up() == 0, 'an empty floor is no beams')

    ----------------------------------------------------------------------
    -- 13. the count line, only when the count moves
    ----------------------------------------------------------------------

    start()
    config.tier, config.low_value = 'low', 1000
    product.on_config_changed(api, 'low_value')
    stacks = { stack(1127, 3210, 3424, 200000) }
    product.on_item_spawn(api, {}); tick()
    local lines = #logs
    assert(logs[lines] == '1 beam(s) over 1 ground stack(s)', logs[lines])
    -- Same picture, twice more. "No beams appear" has two causes and one line
    -- separates them; a line per tick would bury both.
    product.on_item_changed(api, {}); tick()
    product.on_item_changed(api, {}); tick()
    assert(#logs == lines, 'the count line is only printed when the count moves')
    stacks = {}
    product.on_item_despawn(api, {}); tick()
    assert(logs[#logs] == '0 beam(s) over 0 ground stack(s)', logs[#logs])

    -- 13b. ...and when the FLOOR moves under an unchanged beam count.
    --
    -- This is the reading the line exists to give and the one it could not
    -- reach. The line's whole job is to separate "nothing on the floor clears
    -- the threshold" from "beams exist and are not being drawn", which makes
    -- 0-over-N its most important output -- and it used to be gated on the
    -- BEAM count alone, so with live stuck at 0 a floor covered in loot
    -- printed exactly what an empty floor printed: nothing. That is how this
    -- plugin came to be reported as drawing nothing on a lane where it was
    -- correctly drawing nothing, and the report cost a full investigation of
    -- the scene path before anyone measured the floor.
    start()
    config.tier, config.low_value = 'low', 1000000
    product.on_config_changed(api, 'low_value')
    stacks = { stack(526, 3210, 3424, 4000), stack(526, 3211, 3425, 4000),
               stack(526, 3212, 3426, 4000) }
    product.on_item_spawn(api, {}); tick()
    assert(beams_up() == 0, 'nothing on this floor clears a million')
    assert(logs[#logs] == '0 beam(s) over 3 ground stack(s)',
        'a floor with loot on it says so, even when no beam is raised: ' ..
        tostring(logs[#logs]))
    lines = #logs
    -- Still only when something moves: the same floor twice more is silent.
    product.on_item_changed(api, {}); tick()
    product.on_item_changed(api, {}); tick()
    assert(#logs == lines, 'an unchanged floor with no beams is still one line, not one a tick')
    -- And the tally alone moving is a new reading.
    stacks[4] = stack(526, 3213, 3427, 4000)
    product.on_item_spawn(api, {}); tick()
    assert(logs[#logs] == '0 beam(s) over 4 ground stack(s)',
        'the stack count moving is a change worth a line: ' .. tostring(logs[#logs]))

    ----------------------------------------------------------------------
    -- 14. a world load clears first and rebuilds from scratch
    ----------------------------------------------------------------------

    start()
    config.tier, config.low_value = 'low', 1000
    product.on_config_changed(api, 'low_value')
    stacks = { stack(1127, 3210, 3424, 200000), stack(1127, 3300, 3500, 200000) }
    product.on_item_spawn(api, {}); tick()
    assert(beams_up() == 2)
    mark = #calls
    product.on_world_loaded(api, {})
    assert(since(mark, 'instance_destroy') == 2, 'every beam is destroyed on a world load')
    assert(beams_up() == 0, 'nothing is trusted across a scene rebuild')
    tick()
    assert(beams_up() == 2, 'and the set is rebuilt, not restored')
    assert(P.model_asks['beam_modern.model'] == 1,
        'the models survive a world load: they are geometry')

    ----------------------------------------------------------------------
    -- 15. the spin: the arithmetic, the per-tile phase, its stability across
    --     a rebuild, and the early return when it is off
    --
    -- A beam TURNS. This section exists because the spin was once deleted
    -- outright over the report that an orbiting camera cancels it -- which it
    -- does, and which the plugin header answers -- and the deletion reached
    -- every player who was not orbiting. What is pinned here is the motion
    -- itself, so the next removal fails a test instead of shipping.
    ----------------------------------------------------------------------

    start()
    config.tier, config.low_value = 'low', 1000
    product.on_config_changed(api, 'low_value')
    stacks = { stack(1127, 3210, 3424, 200000), stack(1127, 3211, 3424, 200000) }
    product.on_item_spawn(api, {}); tick()
    local phase_a = (3210 * 137 + 3424 * 311) % 2048
    local phase_b = (3211 * 137 + 3424 * 311) % 2048
    assert(phase_a ~= phase_b,
        'neighbouring tiles must not turn in lockstep, or they read as one rigid object')
    mark = #calls
    product.on_frame_start(api, { now_ms = 1000 })
    assert(since(mark, 'instance_position') == 2, 'one yaw per beam per frame')
    local turn = (1000 * 90 * 2048) // 360000
    local yaws = {}
    yaws[args(mark, 'instance_position', 1)[7]] = true
    yaws[args(mark, 'instance_position', 2)[7]] = true
    assert(yaws[(turn + phase_a) % 2048] and yaws[(turn + phase_b) % 2048],
        '2048 units to a turn, spin degrees to a second, plus the tile phase')
    -- The yaw MOVES between frames. Every assertion above holds for a beam
    -- pinned at one angle forever, so without this one the whole section
    -- passes on a plugin that has stopped turning.
    mark = #calls
    product.on_frame_start(api, { now_ms = 2000 })
    assert(args(mark, 'instance_position', 1)[7] ~= (turn + phase_a) % 2048 and
           args(mark, 'instance_position', 1)[7] ~= (turn + phase_b) % 2048,
        'a later frame is a different yaw: the beam is turning, not standing')
    -- A rebuild must not move the phase: frame_ms alone would, and every beam
    -- would jump on the tick that rebuilt it.
    product.on_item_changed(api, {}); tick()
    mark = #calls
    product.on_frame_start(api, { now_ms = 1000 })
    yaws = {}
    yaws[args(mark, 'instance_position', 1)[7]] = true
    yaws[args(mark, 'instance_position', 2)[7]] = true
    assert(yaws[(turn + phase_a) % 2048] and yaws[(turn + phase_b) % 2048],
        'the phase is the TILE, so it is the same across a rebuild')

    config.spin = 0
    mark = #calls
    product.on_frame_start(api, { now_ms = 2000 })
    assert(names(mark) == '', 'spin off returns before the walk and costs one config read')
    config.spin = 90

    ----------------------------------------------------------------------
    -- 16. teardown
    ----------------------------------------------------------------------

    mark = #calls
    product.on_stop(api)
    assert(since(mark, 'instance_destroy') == 2, 'stop takes every object out of the world')
    assert(beams_up() == 0)

    ----------------------------------------------------------------------
    -- 17. the steady state
    --
    -- Sixty frames and sixty ticks with nothing changing. Every layer verb
    -- except open, the declaration, the two reads at start, model, table,
    -- every, tick, tier and finding is a trap above, so this run asserts the
    -- whole of "an unchanged description costs nothing" by surviving -- no
    -- describe, no fence, no commit, no setter, no draw_context -- and the
    -- counts below say what it does cost: one forwarded logic tick, and one
    -- yaw per beam per frame, which is the animation itself.
    ----------------------------------------------------------------------

    start()
    config.tier, config.low_value = 'low', 1000
    product.on_config_changed(api, 'low_value')
    stacks = { stack(1127, 3210, 3424, 200000) }
    product.on_item_spawn(api, {}); tick()
    assert(beams_up() == 1)
    local reads, tiers_reads = P.tier_calls, P.tiers_reads
    mark = #calls
    for i = 1, 60 do
        tick()
        product.on_frame_start(api, { now_ms = i * 20 })
    end
    assert(since(mark, 'porcelain.tick') == 60, 'one forwarded tick per logic tick')
    assert(since(mark, 'instance_create') == 0, 'and not one object made')
    assert(since(mark, 'instance_recolor') == 0, 'nor one band recoloured')
    assert(since(mark, 'porcelain.table') == 0 and since(mark, 'porcelain.model') == 0,
        'nor one asset re-asked')
    assert(P.tier_calls == reads, 'nor one ground stack re-priced: the tick returns on a boolean')
    assert(P.tiers_reads == tiers_reads, 'the thresholds are read per CHANGE, not per rebuild')
    assert(since(mark, 'instance_position') == 60,
        'what it does cost is the spin, which is the animation: one write per beam per frame')
    assert(#findings == 0, 'and a settled plugin has nothing to report')

    ----------------------------------------------------------------------
    -- 18. a host with no layer
    --
    -- Every rule this plugin now runs on lives in the layer. Raising beams
    -- without it would mean re-implementing the cadence, the tier operator,
    -- the terminal asset states and the price table here -- which is the four
    -- recorded defects, kept alive for a configuration that does not ship.
    -- So it says so, once, and raises nothing. Every porcelain verb below is
    -- still a trap, which is what proves it asked for none of them.
    ----------------------------------------------------------------------

    P.available = false
    start()
    assert(#logs == 1 and logs[1]:find('porcelain', 1, true),
        'a host with no layer is said out loud, once: ' .. tostring(logs[1]))
    assert(logs[1]:find('no beams', 1, true), 'and the line says what that costs')
    stacks = { stack(1127, 3210, 3424, 200000) }
    mark = #calls
    product.on_item_spawn(api, {})
    tick()
    product.on_frame_start(api, { now_ms = 1000 })
    product.on_asset(api, { name = 'prices.txt', ok = true })
    product.on_config_changed(api, 'tier')
    product.on_world_loaded(api, {})
    assert(names(mark) == '', 'and nothing is asked of a layer that is not there: ' .. names(mark))
    assert(#logs == 1, 'nor a second line about it')
    P.available = true

    ----------------------------------------------------------------------
    -- 19. a missing price file is stated, and the plugin prices from the
    --     cache
    ----------------------------------------------------------------------

    files['prices.txt'] = nil
    start()
    assert(#logs == 0, 'the ask itself is not worth a line')
    product.on_asset(api, { name = 'beam_modern.model', ok = true })
    assert(P.table_asks == 1, 'an asset that is not the price file is not the price file')
    product.on_asset(api, { name = 'prices.txt', ok = false })
    assert(logs[#logs] == 'no prices.txt; pricing from the cache\'s own OC_COST',
        'the absent file is stated, in the sentence the ledger names: ' .. tostring(logs[#logs]))
    -- And the cache cost is what prices the floor: 1127 was overridden to
    -- 39000 by the file that is not here, so it is back to its own 200000.
    config.tier, config.low_value = 'low', 1000
    product.on_config_changed(api, 'low_value')
    stacks = { stack(1127, 3210, 3424, 200000) }
    product.on_item_spawn(api, {}); tick()
    assert(beams_up() == 1, 'and the floor is still lit, from the cache\'s own number')
    files['prices.txt'] = '1127=39000\n'

    ----------------------------------------------------------------------
    -- 20. a refused scene verb is a finding, once
    ----------------------------------------------------------------------

    start()
    scene.refuse_recolor = true
    config.tier, config.low_value = 'low', 1000
    product.on_config_changed(api, 'low_value')
    stacks = { stack(1127, 3210, 3424, 200000), stack(1127, 3211, 3425, 200000) }
    product.on_item_spawn(api, {}); tick()
    product.on_item_changed(api, {}); tick()
    local refusals = 0
    for _, f in ipairs(findings) do
        if f.verb == 'scene' and f.detail:find('instance_recolor', 1, true) then
            refusals = refusals + 1
        end
    end
    assert(refusals == 1, 'a refused scene verb is one finding, not one per beam per rebuild')
    scene.refuse_recolor = false

    host.core.log('loot beam behavior passed')
end }
