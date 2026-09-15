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
--   * the edge itself is moved by note_key and NEVER by a fence poll, because
--     a poll cannot see a press that opens and closes inside one frame -- and
--     nothing in the host forwards a key into the layer, so the plugin has to
--     forward its own on_key. This fake used to poll a `held` table at the
--     fence, which is precisely why it could not see that this plugin never
--     forwarded anything: from its port until now the reveal key could not go
--     down and Tag/Untag were unreachable on every lane;
--   * a finding whose element was declared through expect_absent reads as
--     expected, and one that was not does not;
--   * hull answers false when the frame's draw allotment is gone or another
--     plugin holds the entity's appearance, and RECORDS which -- the raw
--     draw.world_hull this plugin used to call answers both and was spelled
--     with the answer dropped.
--
-- And it no longer forbids api.core.log. It used to assert(false) there --
-- "the layer is present: nothing to report" -- which is a test pinning silent
-- success: this plugin printed nothing at all when it worked, so a tag list
-- from the wrong cache drew nothing, logged nothing, and photographed exactly
-- like a working one. The pass's reading is a line now, and the four ways it
-- can read are pinned below, each having to print DIFFERENTLY from the others.
--
-- Each of the first three used to be a value this plugin dropped -- a `false`
-- from menu.add, a truncated `set`, a `0` from key_held that means "not held"
-- and "there is no keyboard" alike -- so each is pinned here twice: that the
-- refusal happened, and that it was reported. The fourth is pinned the same
-- way: that the forward happens, and that the edge moved because of it.
--
-- What this file must NOT grow back: api.menu.add, api.input.key_held,
-- draw.world_hull with its answer dropped, a tag encoding written out by
-- hand, or a describe. The fake refuses all five.
return { id = 'entity-behavior', on_start = function(host)
    -- The two host constants this plugin is measured against. They are spelled
    -- here for the same reason the plugin spells TAG_OPS: neither reaches Lua.
    local CONFIG_VALUE_MAX = 192   -- TORIRS_PLUGIN_CONFIG_VALUE_MAX
    local TAG_OPS = 16             -- PORCELAIN_MENU_TAG_OPS
    -- The five modifiers porcelain_key_code knows and the codes it maps them
    -- to; a name outside this set is ABSENT, which is what "off" is. They are
    -- CODES and not a set because the edge is driven by note_key, which is
    -- handed the same integer the host puts in a ToriRS_KeyEvent.
    local KEY_CODE = { shift = 42, ctrl = 43, escape = 37, tab = 36, space = 57 }

    local api                                       -- forward: the fake reaches it
    local logs = {}                                 -- every api.core.log, in order
    local function said() return logs[#logs] end
    local npc = { slot = 7, base_npc_id = 42, npc_id = 100, name = 'Guard', element_id = 123 }
    local added, drawn = {}, {}
    local touch = false
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
    -- The two refusals the raw verb answers and this plugin used to drop. With
    -- them the pass can match an npc and still draw nothing, which is the one
    -- silent state a tag list alone can never explain.
    local hull_refuses = false
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
            edges[#edges + 1] = { key = config_key, fn = fn, down = false, code = -1 }
            return true
        end,
        -- The plugin forwarding its own on_key. This is the ONLY thing that
        -- moves an edge; there is no poll anywhere in the layer, and there is
        -- no caller of this in the host, so a plugin that does not forward has
        -- a key that can never go down.
        note_key = function(key, down)
            for _, edge in ipairs(edges) do
                if edge.code == key and down ~= edge.down then
                    edge.down = down
                    edge.fn(down)
                end
            end
        end,
        fence = function()
            -- The BINDING is re-read here, so a rebind takes effect without a
            -- reload; the edge is not. A binding that changes while its old key
            -- is down releases it, because the old key cannot stay held
            -- through a rebind.
            for _, edge in ipairs(edges) do
                local name = api.config[edge.key]
                local code = KEY_CODE[name] or -1
                if code ~= edge.code and edge.down then
                    edge.down = false
                    edge.fn(false)
                end
                edge.code = code
                if code < 0 and not edge.absent then
                    edge.absent = true
                    finding('key_edge', 'role:' .. edge.key, 'absent', name or 'off')
                end
                if code >= 0 then edge.absent = false end
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
        -- draw.world_hull with BUDGET and ARBITRATION_LOST made loud. The
        -- bool is the whole point: it is what reached the screen, and the
        -- plugin's tally has to count THAT and not what it asked for.
        hull = function(element, colour, fill, shape)
            if hull_refuses then
                finding('world_hull', 'none', 'budget', 'the frame draw allotment')
                return false
            end
            drawn[#drawn + 1] = { element, colour, fill, shape }
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
        core = { log = function(text) logs[#logs + 1] = text end },
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
        world_hull = function()
            error('a Porcelain plugin draws its hull through the layer, which reports a refusal')
        end,
        context = function()
            error('a hull is named in scene terms: there is no rect here to derive')
        end,
    }
    -- The host's pump, modelled. This plugin declares no on_frame_start at all
    -- now: `api.porcelain.open()` installs the fence and the commit, and the
    -- fence is the only thing this plugin ever needed per frame -- the reveal
    -- key's edge is polled there. What used to be a hand-written pair guarded
    -- by "only when the edge armed" is the runtime's, and a handle with no
    -- description pays nothing for it (test_pump_costs_an_idle_plugin_nothing
    -- in plugin_lua_test.c reads that off the real layer's counters).
    local function frame()
        porcelain.fence()
        if product.on_frame_start then product.on_frame_start(api) end
        porcelain.commit()
    end
    -- A key event the way the host delivers one. It reaches the layer only if
    -- the plugin forwards it, which is the thing this file exists to pin: the
    -- pump above fences, but nothing in the host forwards a key.
    local function press(key, down) product.on_key(api, { key = key, down = down }) end
    local menu = { hover_pass = false, rows = { { npc_slot = 7 }, { npc_slot = 7 } } }

    -- ------------------------------------------------------------ lifecycle
    product.on_start(api)
    assert(opened == 1, 'on_start opens the layer')
    assert(#edges == 1 and edges[1].key == 'reveal_key',
        'the reveal key is one edge, read from a config key')
    assert(expects['role:reveal_key'],
        'the lane that cannot answer the reveal key is declared before it is asked')
    assert(#findings == 0, 'a lane that can answer it raises nothing')

    -- ------------------------------------------------------- reading one
    -- Nothing tagged: a setting said no. The README's rule is that "drew
    -- nothing because a setting said no" and "drew nothing because it is
    -- broken" must not be the same picture; here they are not the same line
    -- either, and the all-zero reading is printed on the FIRST pass rather
    -- than suppressed as uninteresting.
    assert(#logs == 0, 'a layer that opened reports nothing at start')
    product.on_draw_world(api, graphics)
    assert(#drawn == 0, 'nothing is tagged, so nothing is outlined')
    assert(said() == '0 hull(s) over 0 tagged npc(s) of 1 in scene; 0 species tagged',
        'an empty tag list is a reading and is reported as one')

    -- --------------------------------------------------------- the two gates
    frame()
    product.on_menu_build(api, menu)
    assert(#added == 0, 'the rows are not offered while the reveal key is up')
    -- The press reaches the layer only because the plugin forwards its own
    -- on_key. Nothing in the host does it, and there is no poll: without the
    -- forward this edge can never go down, and Tag/Untag are unreachable on
    -- every lane rather than only on the touch one.
    press(KEY_CODE.ctrl, true)
    assert(not edges[1].down, 'a key that is not the bound one moves nothing')
    press(KEY_CODE.shift, true)
    assert(edges[1].down,
        'the reveal key edge is moved by the plugin forwarding its own on_key')
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
    -- READING TWO, and the one this whole line exists for: a species is
    -- tagged, the scene has npcs, and NONE of them is it. That is a cs2 tag
    -- list carried to a LostCity lane, and before this it photographed and
    -- logged exactly like a working plugin.
    local nothing_matched = said()
    assert(nothing_matched == '0 hull(s) over 0 tagged npc(s) of 1 in scene; 1 species tagged',
        'a tag that matches nothing in this scene says so, in numbers')
    npc = { slot = 8, base_npc_id = 42, npc_id = 101, name = 'Guard', element_id = 125 }
    product.on_draw_world(api, graphics)
    assert(#drawn == 1 and drawn[1][1] == 125 and drawn[1][4] == 'mesh',
        'tag follows the shell across model transforms and slot changes')
    -- READING FOUR: alive, and saying so. A plugin that works is no longer
    -- indistinguishable from one that does not.
    local alive = said()
    assert(alive == '1 hull(s) over 1 tagged npc(s) of 1 in scene; 1 species tagged',
        'a drawn outline is reported as a drawn outline')
    assert(alive ~= nothing_matched,
        'the two states must not print the same line -- a constant would pass every other check here')
    local spoken = #logs

    -- Colour, fill and shape are read on every pass, so a panel change shows
    -- on the next frame and not on the next bind.
    config.color, config.fill, config.shape = 0x00ff00, 200, 'bounds'
    drawn = {}
    product.on_draw_world(api, graphics)
    assert(drawn[1][2] == 0x00ff00 and drawn[1][3] == 200 and drawn[1][4] == 'bounds',
        'colour, fill and shape are read fresh on every draw pass')
    assert(#logs == spoken,
        'a reading that did not move is not reprinted -- a line per frame buries the transition')
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
    frame(); added = {}
    product.on_menu_build(api, menu)
    assert(#added == 0, 'a key still held across an absence does not re-arm itself')
    press(KEY_CODE.shift, false); press(KEY_CODE.shift, true); added = {}
    product.on_menu_build(api, menu)
    assert(#added == 1, 'a press after the absence brings the rows back')

    -- ------------------------------------------ the outline, refused
    -- Last of the four readings, and placed after every unexpected() count
    -- above because it raises a third finding nobody declared. The fixture is
    -- put back to one tagged species in front of one npc first, so the three
    -- lines below are comparable with the two taken earlier.
    config.set('tags', '42')
    npc = { slot = 8, base_npc_id = 42, npc_id = 101, name = 'Guard', element_id = 125 }
    drawn = {}
    product.on_draw_world(api, graphics)
    assert(#drawn == 1, 'the fixture is one tagged npc again')
    assert(said() == alive, 'and it reads as the drawn outline it is')

    -- READING THREE: the hulls were asked for and refused. Only the layer can
    -- tell this from reading four -- the raw verb answers the same refusal and
    -- this plugin used to drop it -- so the count reported has to be what was
    -- DRAWN and not what was matched.
    hull_refuses = true; drawn = {}
    product.on_draw_world(api, graphics)
    assert(#drawn == 0, 'a refused hull draws nothing')
    assert(said() == '0 hull(s) over 1 tagged npc(s) of 1 in scene; 1 species tagged',
        'a matched npc whose outline was refused is not reported as an outline')
    assert(said() ~= alive and said() ~= nothing_matched,
        'refused, unmatched and drawn are three different lines')
    local refused_hull = found('world_hull', 'budget')
    assert(refused_hull, 'and the refusal itself is a finding, not a silence')
    hull_refuses = false
    product.on_draw_world(api, graphics)
    assert(said() == alive, 'and it goes back to reporting the outline when it draws one')

    -- -------------------------------------------- a lane with no keyboard
    -- The plugin has no on_stop: the host closes the handle after the stop
    -- callback and that drops the edge, which is done here in its place.
    assert(product.on_stop == nil, 'the host closes the handle; the plugin does not')
    porcelain.close()
    touch = true; findings = {}
    product.on_start(api)
    assert(#edges == 0, 'a touch lane registers no edge at all')
    -- And the forward stands down with the edge: a plugin whose key answered
    -- ABSENT has nothing to forward to, and calling the layer without a
    -- registered edge would be one call per key per frame for nothing.
    press(KEY_CODE.shift, true)
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
