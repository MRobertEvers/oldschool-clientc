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
        return table.concat({ item.image or '-', item.place.kind, item.place.on,
            tostring(item.place.corner), item.place.dx or 0, item.place.dy or 0,
            item.w, item.h, item.opacity, item.op_label or '-',
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
                    control.image, control.opacity = item.image, item.opacity
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
    -- WITHIN, and the whole reason for it. One live anchor makes
    -- UITree_FrameHasDepth true for the entire frame; a corner ornament that
    -- cost no anchor before the layer existed must still cost none through it.
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
    local small = only('camera_report')
    assert(small.relation == 'replace' and small.anchor == 'report_button',
        'the small camera stands in place of the report button through a REPLACE placement')
    -- REPLACE is the placement that must be a sibling: a REPLACE from a child
    -- of the target is ANCHOR_INVALID. So this one DOES pay an anchor, and
    -- that is the difference WITHIN exists to avoid paying twice.
    assert(small.parent == 'parent-of-report_button' and anchors == 1,
        'a REPLACE is a sibling over its target, and pays exactly one anchor')
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
    element_moved('report_button', 430, 6, 80, 22); frame()

    -- --------------------------------------------- the target goes and comes
    elements.report_button.bind = 'pending'; porcelain.note('element'); frame()
    assert(small.removed, 'a control whose target no longer binds is removed, not left floating')
    none()
    elements.report_button.bind = 'bound'; porcelain.note('element'); frame()
    local small2 = only('camera_report')
    assert(small2.relation == 'replace' and small2.image == 'camera_small.png',
        'a rebound report button is replaced again')

    -- ------------------------------ a lane with no report button at all
    elements.report_button.bind = 'absent'; porcelain.note('element'); frame(); frame()
    local fallback = only('camera')
    assert(fallback.relation == 'within' and fallback.parent == 'viewport'
        and fallback.x == 512 - 28 - 6,
        'where the lane has no report button the camera falls back to the viewport corner')
    elements.report_button.bind = 'bound'; porcelain.note('element'); frame(); frame()
    assert(only('camera_report'), 'and it goes back to the report slot when one exists')

    -- ---------------------------------------------------------- turning off
    config.camera = 'off'; config_changed(); frame()
    assert(elements.report_button.hidden == nil, 'the native button was never hidden')
    none()

    -- --------------------------------------------------- events and hotkey
    config.camera = 'report-button'; config_changed(); frame(); frame()
    assert(only('camera_report'), 'report-button mode is re-established')
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
