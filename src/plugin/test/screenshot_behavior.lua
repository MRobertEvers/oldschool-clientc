-- Appended to the actual shipped source by plugin_lua_test.c. The plugin is a
-- Porcelain plugin now, so the fake below is a small reconciler with the
-- layer's rules in it rather than a bag of fake widgets: a key not
-- re-described is removed, an item whose element is ABSENT is skipped, an
-- unchanged item costs no setter, and geometry follows the target's box at
-- every fence. That last rule is the point of the port -- the corner camera
-- and the report camera both used to go stale when their element moved
-- without a rebind -- so it is pinned twice, once for each placement.
--
-- What this file must NOT grow back: any use of the raw widget verbs
-- (create_image, set_anchor, set_on_op) or a set_hidden on the report button.
-- The fake refuses both.
return { id = 'screenshot-behavior', on_start = function(host)
    local captures, notices, calls = {}, {}, {}
    local config = { destination = '', delay_ticks = 2, on_level_up = true, on_death = false,
        on_valuable_drop = true, min_drop_value = 100000, hotkey = 0, camera = 'off' }

    -- ---------------------------------------------------------------- lane
    -- Elements the lane answers for, in PARENT-LOCAL coordinates, which is
    -- what every placement is computed in.
    local elements = {
        viewport = { bind = 'pending', box = { x = 4, y = 4, width = 512, height = 334 }, presented = true },
        report_button = { bind = 'pending', box = { x = 430, y = 6, width = 80, height = 22 }, presented = true },
    }
    local function element_moved(name, x, y, w, h)
        local e = elements[name]
        e.box = { x = x, y = y, width = w, height = h }
    end

    -- --------------------------------------------------------------- assets
    local assets = { ['camera.png'] = { ready = false, ref = 1, w = 28, h = 26, requests = 0 },
                     ['camera_small.png'] = { ready = false, ref = 2, w = 20, h = 16, requests = 0 } }
    local by_ref = { [1] = assets['camera.png'], [2] = assets['camera_small.png'] }
    -- Derived pictures are NOT assets: the layer composes them from the
    -- plugin's own pixels, so nothing here is requested, held or released.
    -- Keyed by name; the slot remembers the inputs it was painted for.
    local derived_slots, derived_paints, next_derived_ref = {}, {}, 100
    local held, released = {}, {}
    -- The layer releases an image no describe run has asked for in four runs.
    local IMAGE_IDLE_RUNS = 4
    local runs = 0

    -- ------------------------------------------------------------ porcelain
    local described, applied = nil, {}     -- key -> live control
    local dirty, ever, open_count = true, false, 0
    local revalidates, setters, anchors = 0, 0, 0

    local function signature(item)
        -- Everything the real hash covers except the resolved box.
        -- An item that never mentions opacity is OPAQUE, and says so with
        -- zero: PORCELAIN_OPACITY_DEFAULT is 0 and invisible has its own
        -- name. The plate is such an item -- it states no opacity at all.
        return table.concat({ item.image or '-', item.place.kind, item.place.on,
            tostring(item.place.corner), item.place.dx or 0, item.place.dy or 0,
            item.w, item.h, item.opacity or 0, item.op_label or '-',
            tostring(item.hit), tostring(item.on_op) }, '|')
    end

    -- INSIDE and WITHIN read the corner and the offsets identically and differ
    -- only in whose coordinates the answer is in: INSIDE makes a SIBLING under
    -- the target's parent, so the target's own parent-local origin is added;
    -- WITHIN makes a CHILD of the target, whose origin is therefore zero.
    local function place_box(item, target)
        local w, h = item.w, item.h
        local t, p = target.box, item.place
        local dx, dy = p.dx or 0, p.dy or 0
        if p.kind == 'replace' then
            return t.x + (t.width - w) // 2 + dx, t.y + (t.height - h) // 2 + dy, w, h
        end
        assert(p.kind == 'inside' or p.kind == 'within',
            'only replace, inside and within are described')
        local ox = p.kind == 'within' and 0 or t.x
        local oy = p.kind == 'within' and 0 or t.y
        if p.corner == 'centre' then
            return ox + (t.width - w) // 2 + dx, oy + (t.height - h) // 2 + dy, w, h
        end
        local x = (p.corner == 'top_right' or p.corner == 'bottom_right')
            and ox + t.width - w - dx or ox + dx
        local y = (p.corner == 'bottom_left' or p.corner == 'bottom_right')
            and oy + t.height - h - dy or oy + dy
        return x, y, w, h
    end

    local builder = {}
    local staged
    function builder.control(item)
        assert(item.key and item.place and item.place.on, 'an item needs a key and a placement')
        staged[#staged + 1] = item
    end

    local function reconcile()
        staged = {}
        runs = runs + 1
        described(builder)
        for name, asset in pairs(assets) do
            if held[name] and asset.ready and runs >= held[name] + IMAGE_IDLE_RUNS then
                held[name] = nil
                released[#released + 1] = name
            end
        end
        local wanted = {}
        for _, item in ipairs(staged) do wanted[item.key] = item end
        for key, control in pairs(applied) do
            if not wanted[key] then
                calls[#calls + 1] = 'remove ' .. key
                applied[key] = nil
                control.removed = true
            end
        end
        for _, item in ipairs(staged) do
            local target = elements[item.place.on]
            if target.bind ~= 'bound' then
                -- PENDING defers, ABSENT skips; either way a control whose
                -- target no longer binds is removed, never left floating.
                if applied[item.key] then
                    calls[#calls + 1] = 'remove ' .. item.key
                    applied[item.key].removed = true
                    applied[item.key] = nil
                end
            else
                local control = applied[item.key]
                if not control then
                    control = { key = item.key }
                    applied[item.key] = control
                    calls[#calls + 1] = 'create ' .. item.key
                end
                local sig = signature(item)
                if control.signature ~= sig then
                    control.signature = sig
                    control.image, control.opacity = item.image, item.opacity or 0
                    control.label, control.fn = item.op_label, item.on_op
                    -- armed = hit AND enabled AND a handler: a control with no
                    -- hit box is drawn, inert and has no menu row.
                    control.armed = (item.hit and item.enabled ~= false and item.on_op ~= nil)
                        and item.op_label or nil
                    control.relation = item.place.kind
                    -- WITHIN parents to the element and takes NO anchor. Every
                    -- other placement is a SIBLING under the element's parent,
                    -- which is what an anchor costs and what makes the frame
                    -- have depth.
                    if item.place.kind == 'within' then
                        control.parent, control.anchor = item.place.on, nil
                    else
                        control.parent = 'parent-of-' .. item.place.on
                        control.anchor = item.place.on
                        anchors = anchors + 1
                    end
                    setters = setters + 4
                end
                local x, y, w, h = place_box(item, target)
                if control.x ~= x or control.y ~= y or control.w ~= w or control.h ~= h then
                    control.x, control.y, control.w, control.h = x, y, w, h
                    setters = setters + 1
                end
            end
        end
    end

    local server_tick_fns = {}
    local porcelain
    porcelain = {
        open = function() open_count = open_count + 1; return true end,
        describe = function(fn) described = fn; dirty = true end,
        note = function() dirty = true end,
        every_server_tick = function(fn) server_tick_fns[#server_tick_fns + 1] = fn end,
        tick = function(cadence)
            assert(cadence == 'server_tick', 'only the server tick is forwarded')
            for _, fn in ipairs(server_tick_fns) do fn() end
        end,
        fence = function()
            -- Pending images the layer polls for itself: no on_asset needed.
            for name, asset in pairs(assets) do
                if held[name] and not asset.ready and asset.landed then
                    asset.ready = true; dirty = true
                end
            end
            if dirty or not ever then
                ever, dirty = true, false
                reconcile()
            else
                -- No input moved: geometry still follows every target.
                for _, item in ipairs(staged or {}) do
                    local control, target = applied[item.key], elements[item.place.on]
                    if control and target.bind == 'bound' then
                        local x, y, w, h = place_box(item, target)
                        if control.x ~= x or control.y ~= y then
                            control.x, control.y, control.w, control.h = x, y, w, h
                            setters = setters + 1
                        end
                    end
                end
            end
        end,
        commit = function() revalidates = revalidates + 1 end,
        element = function(name)
            local e = elements[name]
            assert(e, 'unknown element ' .. name)
            return { bind = e.bind, presented = e.presented, box = e.box, local_box = e.box }
        end,
        image = function(name)
            local asset = assets[name]
            assert(asset, 'unknown image ' .. name)
            if not held[name] then asset.requests = asset.requests + 1 end
            held[name] = runs
            if not asset.ready then return nil, 'pending' end
            return asset.ref, 'ready'
        end,
        -- Porcelain_Derived's own two rules, and no more than those: a key
        -- is painted AT MOST ONCE per (key, inputs, size) -- the layer hashes
        -- the inputs -- and what comes back is the compose's state, which is
        -- what the plugin branches on. The paint callback is the PLUGIN's
        -- function; calling it here is what proves the plugin can fill a
        -- whole w*h plate, which is the difference between a plate and a
        -- plate with holes in it.
        derived = function(key, inputs, w, h, paint)
            assert(type(key) == 'string', 'a derived picture is named')
            assert(type(inputs) == 'string', 'a derived picture states its inputs')
            assert(type(paint) == 'function', 'a derived picture paints itself')
            assert(w > 0, 'a derived picture has a width')
            assert(h > 0, 'a derived picture has a height')
            local slot = derived_slots[key]
            if slot and slot.inputs == inputs and slot.w == w and slot.h == h then
                return slot.ref, slot.state
            end
            local cells = paint(w, h)
            assert(type(cells) == 'table', 'the paint callback returns a cell table')
            assert(#cells == w * h, 'the paint callback returns exactly w*h cells')
            for i = 1, w * h do
                assert(math.type(cells[i]) == 'integer',
                    'cell ' .. i .. ' of ' .. key .. ' is not an ARGB integer')
            end
            derived_paints[#derived_paints + 1] = key .. '|' .. inputs
            next_derived_ref = next_derived_ref + 1
            slot = { inputs = inputs, w = w, h = h, ref = next_derived_ref,
                     state = 'ready', cells = cells }
            derived_slots[key] = slot
            return slot.ref, slot.state
        end,
        set = function(key, motion)
            local control = applied[key]
            assert(control, 'set on a key that is not in the applied description: ' .. key)
            if motion.opacity and motion.opacity ~= control.opacity then
                control.opacity = motion.opacity
                setters = setters + 1
            end
            return true, 'ok'
        end,
        close = function()
            for key, control in pairs(applied) do control.removed = true; applied[key] = nil end
            for name in pairs(held) do released[#released + 1] = name end
            held = {}
        end,
    }

    local api = {
        config = config,
        porcelain = porcelain,
        core = { log = function() end, notify = function(text) notices[#notices + 1] = text end },
        client = { datestamp = function() return '2026-09-06_12-00-00' end },
        world = { local_player = function() return { name = 'Test Player' } end },
        assets = {
            image_size = function(ref)
                local asset = by_ref[ref]
                assert(asset and asset.ready, 'image_size on a handle that is not ready')
                return asset.w, asset.h
            end,
            screenshot = function(directory, name)
                captures[#captures + 1] = directory .. '|' .. name
                return true, '/saved/' .. directory .. '/' .. name
            end,
        },
        -- The raw widget verbs are gone from this plugin for good.
        widgets = { watch = function() error('a Porcelain plugin does not watch widgets') end },
    }

    -- The three pump helpers. `api.porcelain.open()` installs all three in the
    -- runtime, which is where they are proved (test_porcelain_pump in
    -- plugin_lua_test.c dispatches through the real callback table and reads
    -- the real counters). What they do HERE is model the host, so that what
    -- this file asserts is the plugin's behaviour and not its boilerplate.
    local frames = 0
    local function frame()
        frames = frames + 1
        porcelain.fence()
        product.on_frame_start(api)
        porcelain.commit()
    end
    local function config_changed()
        porcelain.note('config')
        if product.on_config_changed then product.on_config_changed(api, 'camera') end
    end
    local function server_tick()
        porcelain.tick('server_tick')
        if product.on_server_tick then product.on_server_tick(api) end
    end
    local function only(key)
        local found
        for k, v in pairs(applied) do
            assert(k == key, 'unexpected control ' .. k)
            found = v
        end
        assert(found, 'expected the control ' .. key)
        return found
    end
    local function none()
        assert(next(applied) == nil, 'expected no control')
    end
    -- The report slot is TWO controls now, and which two, in what order, is
    -- the fix: a plate the size of the button and a camera over it.
    local function exactly(...)
        local want = table.pack(...)
        local live = 0
        for _ in pairs(applied) do live = live + 1 end
        assert(live == want.n, 'expected ' .. want.n .. ' controls, found ' .. live)
        local out = {}
        for i = 1, want.n do
            out[i] = applied[want[i]]
            assert(out[i], 'expected the control ' .. want[i])
        end
        return table.unpack(out, 1, want.n)
    end
    local function created_in_order(first, second)
        local seen = {}
        for _, call in ipairs(calls) do
            if call == 'create ' .. first or call == 'create ' .. second then
                seen[#seen + 1] = call
            end
        end
        assert(seen[#seen - 1] == 'create ' .. first,
            first .. ' is described before ' .. second)
        assert(seen[#seen] == 'create ' .. second,
            second .. ' is described after ' .. first)
    end

    -- ------------------------------------------------------------ lifecycle
    product.on_start(api)
    assert(open_count == 1 and described, 'on_start opens porcelain and installs one describe')
    frame()
    none()
    -- The lane mounts: elements bind, then the images land.
    elements.viewport.bind = 'bound'; elements.report_button.bind = 'bound'
    porcelain.note('element'); frame()
    assert(next(applied) == nil, 'camera off describes nothing')

    -- ------------------------------------------------------- corner camera
    config.camera = 'bottom-right'; config_changed(); frame()
    none()   -- the image has not decoded yet, so nothing is described
    assets['camera.png'].landed = true; frame(); frame()
    local camera = only('camera')
    assert(camera.image == 'camera.png' and camera.w == 28 and camera.h == 26,
        'corner control shows the camera image at its size')
    assert(camera.x == 512 - 28 - 6 and camera.y == 334 - 26 - 6,
        'bottom-right corner keeps the margin, in the viewport\'s OWN coordinates')
    assert(camera.armed == 'Take screenshot' and camera.opacity == 170 and camera.relation == 'within',
        'control is armed with the capture operation: a hit box, enabled, and a handler')
    -- WITHIN, and the whole reason for it: a corner ornament that cost no
    -- anchor before the layer existed still costs none through it. What it is
    -- NOT is a frame-time win -- measured at 0.32 ms under a 0.73 ms noise
    -- floor, see PORCELAIN_WITHIN in torirs_plugin_api.h.
    assert(camera.parent == 'viewport', 'the corner camera is a CHILD of the viewport')
    assert(camera.anchor == nil, 'and takes NO anchor')
    assert(anchors == 0, 'so the corner camera costs the frame no anchor at all')

    -- THE DEFECT THE PORT FIXES. The viewport is widened with no rebind and no
    -- config change: before the port the camera stayed at the old offset.
    element_moved('viewport', 4, 4, 900, 600)
    frame()
    assert(camera.x == 900 - 28 - 6 and camera.y == 600 - 26 - 6,
        'the corner camera is re-placed on a resize without a rebind')
    element_moved('viewport', 4, 4, 512, 334); frame()

    -- The press: lit, captures, and comes back to idle on its own.
    camera.fn(camera.key)
    assert(#captures == 1 and captures[1]:find('^Test%-Player|screenshot_') and camera.opacity == 255,
        'pressing the control captures into the player folder and lights it')
    assert(notices[1]:find('Screenshot saved'), 'capture notifies')
    for _ = 1, 12 do frame() end
    assert(camera.opacity == 170, 'the press decays back to the idle opacity')

    -- Steady state: nothing described changed and nothing moved.
    local before_setters, before_revalidates = setters, revalidates
    for _ = 1, 5 do frame() end
    assert(setters == before_setters, 'a settled description makes no setter call')
    assert(revalidates == before_revalidates + 5, 'exactly one commit per frame')

    -- ------------------------------------------------------ report-button
    config.camera = 'report-button'; config_changed()
    assets['camera_small.png'].landed = true
    frame(); frame()
    local plate, small = exactly('camera_plate', 'camera_report')
    created_in_order('camera_plate', 'camera_report')
    -- SSHOT-164-REPORT-PLATE-GONE. REPLACE consumes the target's WHOLE record
    -- set, and on modern164 the red plate is IN that set, so replacing the
    -- button took the plate with it and left a 79x23 hole with a 20x16 camera
    -- floating in the middle. Nothing in a plugin can tell that root from
    -- classic548's, where the plate belongs to the parent strip -- so the
    -- replacement stops asking and brings its own.
    assert(plate.relation == 'replace' and plate.anchor == 'report_button',
        'the plate stands in place of the report button through a REPLACE placement')
    assert(plate.image == 'camera_plate.png', 'and it is the plugin\'s own derived picture')
    assert(plate.w == 80 and plate.h == 22,
        'the plate is the size of the BUTTON, so nothing REPLACE removed is left as a hole')
    assert(plate.x == 430 and plate.y == 6, 'and it sits exactly where the button was')
    assert(plate.armed == nil,
        'the plate carries no hit box: the camera over it owns the press')
    assert(#derived_paints == 1 and derived_paints[1] == 'camera_plate.png|80x22',
        'the plate is painted exactly once, keyed by the box it was painted for')
    -- REPLACE is the placement that must be a sibling: a REPLACE from a child
    -- of the target is ANCHOR_INVALID. So the plate DOES pay an anchor, and
    -- that is the difference WITHIN exists to avoid paying twice.
    assert(plate.parent == 'parent-of-report_button',
        'a REPLACE is a sibling over its target')
    -- The camera is INSIDE, not WITHIN: a sibling under the same parent, so
    -- it lands AFTER the plate in the one unit's draw order instead of
    -- parenting itself to a native button whose subtree REPLACE just took.
    assert(small.relation == 'inside' and small.anchor == 'report_button',
        'the camera is anchored INSIDE the button, over the plate')
    assert(small.parent == 'parent-of-report_button', 'and is a sibling too')
    assert(anchors == 2, 'the plate and the camera pay one anchor each, and no more')
    assert(small.image == 'camera_small.png' and small.x == 430 + (80 - 20) // 2
        and small.y == 6 + (22 - 16) // 2,
        'the small camera is centred on the report slot in its own parent-local box')
    assert(elements.report_button.hidden == nil, 'report-button mode never hides the native button')
    assert(small.armed == 'Take screenshot', 'the report-slot control is armed too')
    small.fn(small.key)
    assert(#captures == 2 and small.opacity == 255, 'the report-slot control captures too')

    -- THE SECOND DEFECT. The bar moves under a provided frame with no rebind.
    element_moved('report_button', 300, 40, 80, 22)
    frame()
    assert(small.x == 300 + 30 and small.y == 40 + 3,
        'the report camera follows its element when the frame moves it')
    assert(plate.x == 300 and plate.y == 40, 'and the plate goes with it, or it is a hole again')
    assert(#derived_paints == 1, 'a move is not a new size, so the plate is not repainted')
    -- A new SIZE is a new picture, and the key says so: the plate covers
    -- whatever box the lane reports, which is the whole point of deriving it.
    element_moved('report_button', 300, 40, 100, 30); porcelain.note('element'); frame()
    assert(#derived_paints == 2 and derived_paints[2] == 'camera_plate.png|100x30',
        'a resized button repaints the plate at the new box')
    assert(plate.w == 100 and plate.h == 30, 'and the plate covers the new box exactly')
    element_moved('report_button', 430, 6, 80, 22); porcelain.note('element'); frame()

    -- --------------------------------------------- the target goes and comes
    elements.report_button.bind = 'pending'; porcelain.note('element'); frame()
    assert(small.removed, 'a control whose target no longer binds is removed, not left floating')
    assert(plate.removed, 'and the plate under it goes too, rather than being left over a gap')
    none()
    elements.report_button.bind = 'bound'; porcelain.note('element'); frame()
    local plate2, small2 = exactly('camera_plate', 'camera_report')
    assert(small2.relation == 'inside' and small2.image == 'camera_small.png',
        'a rebound report button gets its camera back')
    assert(plate2.relation == 'replace' and plate2.image == 'camera_plate.png',
        'and its plate, which is what the REPLACE took away')

    -- ------------------------------ a lane with no report button at all
    elements.report_button.bind = 'absent'; porcelain.note('element'); frame(); frame()
    local fallback = only('camera')
    assert(fallback.relation == 'within' and fallback.parent == 'viewport'
        and fallback.x == 512 - 28 - 6,
        'where the lane has no report button the camera falls back to the viewport corner')
    elements.report_button.bind = 'bound'; porcelain.note('element'); frame(); frame()
    assert(exactly('camera_plate', 'camera_report'),
        'and it goes back to the report slot, plate and all, when one exists')

    -- ---------------------------------------------------------- turning off
    config.camera = 'off'; config_changed(); frame()
    assert(elements.report_button.hidden == nil, 'the native button was never hidden')
    none()

    -- --------------------------------------------------- events and hotkey
    config.camera = 'report-button'; config_changed(); frame(); frame()
    assert(exactly('camera_plate', 'camera_report'), 'report-button mode is re-established')
    product.on_game_event(api, { kind = 'death', subject = 'x', value = -1 })
    assert(#captures == 2, 'disabled event kinds do not capture')
    product.on_game_event(api, { kind = 'valuable_drop', subject = 'Abyssal whip', value = 50000 })
    assert(#captures == 2, 'drops under the threshold do not capture')
    product.on_game_event(api, { kind = 'level_up', subject = 'Attack', value = 40 })
    assert(#captures == 2, 'delayed captures wait for the server tick')
    server_tick(); server_tick()
    assert(#captures == 3 and captures[3] == 'Test-Player/Levels|Attack-40_2026-09-06_12-00-00.png',
        'level up captures after delay_ticks into its category folder')
    product.on_key(api, { down = true, key = 44 })
    assert(#captures == 3, 'hotkey 0 means off: no key captures')
    config.hotkey = 44
    product.on_key(api, { down = true, key = 45 })
    assert(#captures == 3, 'a key that is not the hotkey does not capture')
    product.on_key(api, { down = false, key = 44 })
    assert(#captures == 3, 'the hotkey RELEASE does not capture')
    product.on_key(api, { down = true, key = 44 })
    assert(#captures == 4, 'the hotkey captures immediately')

    -- ------------------------------------------------- both icons stay held
    -- The layer releases an image no describe run asked for. Both are asked
    -- for on every run, so a mode change never pays a second decode.
    for _ = 1, 8 do porcelain.note('explicit'); frame() end
    assert(#released == 0, 'neither icon is released while the camera is live')
    config.camera = 'bottom-right'; config_changed(); frame()
    assert(assets['camera.png'].requests == 1 and assets['camera_small.png'].requests == 1,
        'switching mode re-uses the held icon rather than re-requesting it')
    assert(only('camera').image == 'camera.png', 'and the corner camera is up at once')

    -- ------------------------------------------------------------ teardown
    product.on_stop(api)
    porcelain.close()   -- the host closes the handle after on_stop returns
    assert(#released == 2, 'close releases both image handles')
    none()
    host.core.log('screenshot behavior verified')
end }
