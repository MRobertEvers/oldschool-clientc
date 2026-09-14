-- Appended to the actual shipped source by plugin_lua_test.c. The plugin is a
-- Porcelain plugin now, so `api.porcelain` below is a stand-in for the real
-- layer with the layer's rules in it rather than a pass-through:
--
--   * menu_add refuses when the host-wide route table is full and RECORDS the
--     refusal, naming the label nobody will see;
--   * config_list_add MEASURES the join against the 192-byte config value
--     ceiling before it writes, refuses rather than let the store truncate,
--     and leaves the stored list exactly as it was;
--   * key_edge answers false on a lane with no keyboard frame, with one
--     finding, at the moment it is asked and not at some later fence;
--   * a finding whose element was declared through expect_absent reads as
--     expected, and one that was not does not.
--
-- Those four are the port. Each of the first three used to be a value this
-- plugin dropped -- a `false` from menu.add, a truncated `set`, a `0` from
-- key_held that means "not held" and "there is no keyboard" alike -- so each
-- is pinned here twice: that the refusal happened, and that it was reported.
--
-- What this file must NOT grow back: api.menu.add, api.input.key_held, a tag
-- encoding written out by hand, or a describe. The fake refuses all four.
return { id = 'entity-behavior', on_start = function(host)
    -- The two host constants this plugin is measured against. They are spelled
    -- here for the same reason the plugin spells TAG_OPS: neither reaches Lua.
    local CONFIG_VALUE_MAX = 192   -- TORIRS_PLUGIN_CONFIG_VALUE_MAX
    local TAG_OPS = 16             -- PORCELAIN_MENU_TAG_OPS
    -- The five modifiers porcelain_key_code knows, and nothing else: a name
    -- outside this set is ABSENT, which is what "off" is.
    local MODIFIERS = { shift = true, ctrl = true, escape = true, tab = true, space = true }

    local api                                       -- forward: the fake reaches it
    local npc = { slot = 7, base_npc_id = 42, npc_id = 100, name = 'Guard', element_id = 123 }
    local added, drawn = {}, {}
    local touch, held = false, {}
    local routes_left = 24                          -- TORIRS_PLUGIN_MENU_ROUTES_MAX

    -- ------------------------------------------------------------- findings
    -- Coalesced on (verb, element, result), like the real table, so a refusal
    -- read every frame is one row with a count and not a flood.
    local findings, expects = {}, {}
    local function finding(verb, element, result, detail)
        for _, seen in ipairs(findings) do
            if seen.verb == verb and seen.element == element and seen.result == result then
                seen.count = seen.count + 1
                return
            end
        end
        findings[#findings + 1] = { verb = verb, element = element, result = result,
            detail = detail, count = 1,
            expected = result == 'absent' and expects[element] ~= nil }
    end
    local function found(verb, result)
        for _, seen in ipairs(findings) do
            if seen.verb == verb and seen.result == result then return seen end
        end
    end
    local function unexpected()
        local total = 0
        for _, seen in ipairs(findings) do if not seen.expected then total = total + 1 end end
        return total
    end

    -- ------------------------------------------------------------ porcelain
    local edges, opened = {}, 0
    -- This plugin owns no control, so these two must stay at zero for the life
    -- of the run. They are counted anyway: "nothing to reconcile" and "the
    -- reconciler happened to write nothing" are not the same claim.
    local setters, revalidates, staged = 0, 0, false
    local porcelain = {
        open = function() opened = opened + 1; return true end,
        close = function() edges = {} end,
        expect_absent = function(element, why)
            assert(why and why ~= '', 'an expected absence has to state its reason')
            expects[element] = why
        end,
        key_edge = function(config_key, fn)
            -- input.key_held can never be true where there is no keyboard
            -- frame, so the answer is ABSENT with one finding -- HERE, in the
            -- call, so the plugin learns the feature is off before it offers
            -- a row nobody can reveal.
            if touch then
                finding('key_edge', 'role:' .. config_key, 'absent', 'touch lane has no key')
                return false
            end
            edges[#edges + 1] = { key = config_key, fn = fn, down = false }
            return true
        end,
        fence = function()
            for _, edge in ipairs(edges) do
                local name = api.config[edge.key]
                if not MODIFIERS[name] then
                    if not edge.absent then
                        edge.absent = true
                        finding('key_edge', 'role:' .. edge.key, 'absent', name or 'off')
                    end
                else
                    edge.absent = false
                    local down = held[name] == true
                    if down ~= edge.down then edge.down = down; edge.fn(down) end
                end
            end
        end,
        commit = function()
            -- One revalidate for every plugin together, and none at all when
            -- nothing was staged: an empty flush is not an erasure.
            if staged then revalidates = revalidates + 1; staged = false end
        end,
        describe = function() error('this plugin owns no control: it must not describe one') end,
        set = function() error('this plugin owns no control: it must not move one') end,
        menu_tag = function(subject, op)
            assert(subject >= 0, 'a menu tag names a subject')
            assert(op >= 0 and op < TAG_OPS, 'one of sixteen operations per subject')
            return subject * TAG_OPS + op
        end,
        menu_add = function(text, action)
            if routes_left <= 0 then
                finding('menu_add', 'role:menu', 'refused', text)
                return false
            end
            routes_left = routes_left - 1
            added[#added + 1] = { text = text, tag = action }
            return true
        end,
        config_list_add = function(key, item)
            local current = api.config[key] or ''
            for entry in string.gmatch(current, '[^,]+') do
                if entry == item then return true end
            end
            -- MEASURE BEFORE JOINING: the store's own setter truncates and its
            -- validator then accepts the fragment, so a list cut mid-id names
            -- a different species and reads back as one.
            local needed = #item + 1 + (current ~= '' and #current + 1 or 0)
            if needed > CONFIG_VALUE_MAX then
                finding('config_list_add', 'none', 'budget', item)
                return false
            end
            api.config.set(key, current ~= '' and (current .. ',' .. item) or item)
            return true
        end,
    }

    -- ------------------------------------------------------------------ api
    local config = { tags = '', color = 0xff00ff, fill = 48, shape = 'mesh',
                     reveal_key = 'shift' }
    config.set = function(key, value)
        -- lua_config_set refuses a value the store cannot hold rather than
        -- truncating it, and PluginHost_ConfigSet dispatches on_config_changed
        -- only when the value actually moved.
        assert(#tostring(value) < CONFIG_VALUE_MAX,
            'a value past the ceiling must never reach the store')
        if config[key] == value then return true end
        config[key] = value
        product.on_config_changed(api, key)
        return true
    end
    api = {
        config = config,
        porcelain = porcelain,
        core = { log = function() assert(false, 'the layer is present: nothing to report') end },
        world = {
            npc_by_slot = function() return npc end,
            npc_next = function(cursor) if cursor == -1 then return 0, npc end end,
        },
        -- The two raw verbs the port replaced. A regression to either is a
        -- refusal going quiet again, so neither is merely unused: both fail.
        input = { key_held = function()
            error('a Porcelain plugin does not poll the reveal key itself')
        end },
        menu = { add = function()
            error('a Porcelain plugin adds rows through the layer, which reports a refusal')
        end },
    }
    local graphics = {
        world_hull = function(element, color, fill, shape)
            drawn[#drawn + 1] = { element, color, fill, shape }
            return true, 'ok'
        end,
        context = function()
            error('a hull is named in scene terms: there is no rect here to derive')
        end,
    }
    local function frame() product.on_frame_start(api) end
    local menu = { hover_pass = false, rows = { { npc_slot = 7 }, { npc_slot = 7 } } }

    -- ------------------------------------------------------------ lifecycle
    product.on_start(api)
    assert(opened == 1, 'on_start opens the layer')
    assert(#edges == 1 and edges[1].key == 'reveal_key',
        'the reveal key is one edge, read from a config key')
    assert(expects['role:reveal_key'],
        'the lane that cannot answer the reveal key is declared before it is asked')
    assert(#findings == 0, 'a lane that can answer it raises nothing')

    -- --------------------------------------------------------- the two gates
    frame()
    product.on_menu_build(api, menu)
    assert(#added == 0, 'the rows are not offered while the reveal key is up')
    held.shift = true; frame()
    product.on_menu_build(api, { hover_pass = true, rows = { { npc_slot = 7 } } })
    assert(#added == 0, 'the hover pass is left on the first statement')

    -- ------------------------------------------------------- tag and retain
    product.on_menu_build(api, menu)
    assert(#added == 1 and added[1].text == 'Tag @yel@Guard', 'one action per hovered NPC')
    assert(added[1].tag == 42 * TAG_OPS + 1,
        'the row carries subject and intent in the one shared encoding')

    local retained = { owned = true, tag = added[1].tag }
    -- The same server slot now holds a different species while the menu stays open.
    npc = { slot = 7, base_npc_id = 99, npc_id = 99, name = 'Goblin', element_id = 124 }
    product.on_menu_select(api, retained)
    assert(config.tags == '42', 'retained Tag must not retarget a recycled NPC slot')
    product.on_menu_select(api, retained)
    assert(config.tags == '42', 'retained Tag preserves its intended operation')
    product.on_draw_world(api, graphics)
    assert(#drawn == 0, 'replacement species is not highlighted')
    npc = { slot = 8, base_npc_id = 42, npc_id = 101, name = 'Guard', element_id = 125 }
    product.on_draw_world(api, graphics)
    assert(#drawn == 1 and drawn[1][1] == 125 and drawn[1][4] == 'mesh',
        'tag follows the shell across model transforms and slot changes')

    -- Colour, fill and shape are read on every pass, so a panel change shows
    -- on the next frame and not on the next bind.
    config.color, config.fill, config.shape = 0x00ff00, 200, 'bounds'
    drawn = {}
    product.on_draw_world(api, graphics)
    assert(drawn[1][2] == 0x00ff00 and drawn[1][3] == 200 and drawn[1][4] == 'bounds',
        'colour, fill and shape are read fresh on every draw pass')
    config.color, config.fill, config.shape = 0xff00ff, 48, 'mesh'

    -- ---------------------------------------------------------------- untag
    added = {}; product.on_menu_build(api, menu)
    assert(added[1].text == 'Untag @yel@Guard')
    assert(added[1].tag == 42 * TAG_OPS + 0, 'an Untag row carries the untag intent')
    product.on_menu_select(api, { owned = true, tag = added[1].tag })
    assert(config.tags == '', 'Untag removes the saved species')
    product.on_menu_select(api, { owned = false, tag = retained.tag })
    assert(config.tags == '', 'native menu rows do not alter tags')

    -- ------------------------------------------------------ the stored list
    -- config_list_add appends; this list has always been sorted, and the ini
    -- and the live set are one string.
    for _, id in ipairs({ 900, 7, 42 }) do
        product.on_menu_select(api, { owned = true, tag = id * TAG_OPS + 1 })
    end
    assert(config.tags == '7,42,900', 'the stored list stays sorted whatever order tags arrive in')
    product.on_menu_select(api, { owned = true, tag = 7 * TAG_OPS + 1 })
    assert(config.tags == '7,42,900', 'tagging a species twice changes nothing')

    -- --------------------------------------------- the ceiling, out loud
    -- Enough species and the joined list passes the host's 192-byte value
    -- ceiling. The store used to snprintf it short and the validator accepted
    -- the fragment, so a list cut mid-id named a DIFFERENT species and read
    -- back as one. Now the addition is refused and the stored list untouched.
    local filler = {}
    for id = 1000, 1037 do filler[#filler + 1] = id end
    config.set('tags', table.concat(filler, ','))
    local stored = config.tags
    assert(#stored == 189 and #stored + #',12345' > CONFIG_VALUE_MAX,
        'the fixture list is one addition short of the ceiling')
    product.on_menu_select(api, { owned = true, tag = 12345 * TAG_OPS + 1 })
    assert(config.tags == stored, 'a refused addition leaves the stored list unchanged')
    local refusal = found('config_list_add', 'budget')
    assert(refusal and refusal.detail == '12345',
        'the refused addition is one finding naming the id it would not store')
    assert(unexpected() == 1, 'and it is not an absence anybody declared')

    -- ------------------------------------------ the route table, out loud
    -- TORIRS_PLUGIN_MENU_ROUTES_MAX is 24 shared by every plugin in one build.
    -- Over it `add` answers false; this plugin used to carry on adding rows
    -- that would never appear, and nobody learned that a row was missing.
    npc = { slot = 8, base_npc_id = 1000, npc_id = 8, name = 'Guard', element_id = 125 }
    routes_left = 0; added = {}
    product.on_menu_build(api, menu)
    assert(#added == 0, 'a refused row is not added')
    local refused = found('menu_add', 'refused')
    assert(refused and refused.detail == 'Untag @yel@Guard',
        'a refused menu row is one finding naming the label nobody will see')
    assert(unexpected() == 2, 'a refusal is never an expected absence')
    routes_left = 24

    -- ----------------------------------------------------------- the frame
    -- Nothing described and nothing to move: a settled frame is the fence's
    -- poll of one config key and nothing else.
    local before_setters, before_revalidates = setters, revalidates
    for _ = 1, 20 do frame() end
    assert(setters == before_setters, 'steady state costs zero engine setters')
    assert(revalidates == before_revalidates, 'steady state costs zero revalidates')
    assert(setters == 0 and revalidates == 0,
        'this plugin owns no control, so it never pays for one')

    -- ------------------------------------------------- the reveal key, off
    -- "off" is a real choice, it is declared, and a key that changed says
    -- nothing about whether the new one is held.
    config.set('reveal_key', 'off')
    added = {}; frame()
    product.on_menu_build(api, menu)
    assert(#added == 0, 'the reveal key turned off offers no rows')
    local off = found('key_edge', 'absent')
    assert(off and off.expected, 'a declared absence reads as expected')
    assert(unexpected() == 2, 'and adds nothing to what was not planned for')
    -- Putting the key back does NOT bring the rows back while it is still
    -- held. The layer's watch keeps its last edge across an ABSENT window, so
    -- a key that was down when the absence began and is down when it ends
    -- never transitions and the callback never fires. That is the verb's
    -- behaviour and it is pinned here rather than papered over: a release and
    -- a press is what re-arms it.
    config.set('reveal_key', 'shift')
    held.shift = true; frame(); added = {}
    product.on_menu_build(api, menu)
    assert(#added == 0, 'a key still held across an absence does not re-arm itself')
    held.shift = false; frame(); held.shift = true; frame(); added = {}
    product.on_menu_build(api, menu)
    assert(#added == 1, 'a press after the absence brings the rows back')

    -- -------------------------------------------- a lane with no keyboard
    -- The plugin has no on_stop: the host closes the handle after the stop
    -- callback and that drops the edge, which is done here in its place.
    assert(product.on_stop == nil, 'the host closes the handle; the plugin does not')
    porcelain.close()
    touch = true; findings = {}; held.shift = true
    product.on_start(api)
    assert(#edges == 0, 'a touch lane registers no edge at all')
    local absent = found('key_edge', 'absent')
    assert(absent and absent.element == 'role:reveal_key' and absent.expected,
        'a lane with no keyboard frame is one declared absence, not silence')
    added = {}; frame()
    product.on_menu_build(api, menu)
    assert(#added == 0, 'the Tag rows report themselves off, not merely never held')
    assert(#findings == 1 and unexpected() == 0,
        'and a frame there costs nothing and says nothing new')

    host.core.log('entity behavior passed')
end }
