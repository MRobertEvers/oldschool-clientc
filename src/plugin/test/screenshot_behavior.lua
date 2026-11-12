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
    local host_api
    local config = { destination = '', delay_ticks = 2, on_level_up = true, on_death = false,
        on_valuable_drop = true, min_drop_value = 100000, hotkey = 0, camera = 'off' }

    -- ---------------------------------------------------------------- lane
    -- Elements the lane answers for, in PARENT-LOCAL coordinates, which is
    -- what every placement is computed in.
    local elements = {
        viewport = { bind = 'pending', box = { x = 4, y = 4, width = 512, height = 334 }, presented = true },
        -- `paints_own_art` is the lane answer the plate turns on, and it is
        -- not the same on every root: MEASURED true on modern164 and on the
        -- lane's own native 548, false on classic548/161, where the frame
        -- provider hides the eight filter plates and draws their hollows into
        -- its own chat-bar rock. The default here is the first kind, which is
        -- the one that needs the plate; `report_button.paints_own_art = false`
        -- below photographs the second.
        report_button = { bind = 'pending', box = { x = 430, y = 6, width = 80, height = 22 },
                          presented = true, paints_own_art = true },
    }
    local function element_moved(name, x, y, w, h)
        local e = elements[name]
        e.box = { x = x, y = y, width = w, height = h }
    end

    -- --------------------------------------------------------------- assets
    -- chat_button.png is the plate's SOURCE, 56x22 like the shipped sprite, and
    -- it carries pixels because the plugin three-slices it rather than ramping
    -- a palette across the box. Its cells are i*16 + 0xff000000 so a plate cut
    -- from it can be told from a constant, and it starts ready: the plate is
    -- the one picture the plugin reads rather than merely holds, so a test that
    -- landed it late would be testing the fake's own sequencing.
    local CHAT_BUTTON_W, CHAT_BUTTON_H = 56, 22
    local chat_button_pixels = {}
    for i = 1, CHAT_BUTTON_W * CHAT_BUTTON_H do
        chat_button_pixels[i] = 0xff000000 + (i * 16) % 0x1000000
    end
    local assets = { ['camera.png'] = { ready = false, ref = 1, w = 28, h = 26, requests = 0 },
                     ['camera_small.png'] = { ready = false, ref = 2, w = 20, h = 16, requests = 0 },
                     ['chat_button.png'] = { ready = true, ref = 3, w = CHAT_BUTTON_W,
                                             h = CHAT_BUTTON_H, requests = 0,
                                             pixels = chat_button_pixels } }
    local by_ref = { [1] = assets['camera.png'], [2] = assets['camera_small.png'],
                     [3] = assets['chat_button.png'] }
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
    -- Panel rows: the declaration is the ordered (key, kind) sequence, and a
    -- run that changes it is a page REBUILD -- the flash this plugin's page is
    -- written never to need. Counted, so the gallery can be held to that.
    local rows, row_order, row_staged, rebuilds = {}, {}, nil, 0
    function builder.row(row)
        assert(type(row.key) == 'string' and row.kind, 'a row needs a key and a kind')
        assert(not row_staged[row.key], 'row keys are unique: ' .. row.key)
        row_staged[row.key] = row
        row_staged[#row_staged + 1] = row.key .. ':' .. row.kind
    end

    local function reconcile()
        staged = {}
        row_staged = {}
        runs = runs + 1
        described(builder)
        local order = table.concat(row_staged, ',')
        if #row_order > 0 and order ~= row_order then rebuilds = rebuilds + 1 end
        row_order = order
        rows = {}
        for key, row in pairs(row_staged) do
            if type(key) == 'string' then rows[key] = row end
        end
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

    local panel_descriptor, panel_builds, settings_invalidations = nil, {}, 0

    -- ------------------------------------------------------- the captures
    -- The platform half, as tickets the test answers by hand: a ticket with
    -- no `answer` is in flight. Thumbnails count every ask, and `asked` is
    -- the set of pictures EVER asked for -- what laziness is measured by.
    local store = { kind = 'folder', root = '/home/p/pics' }
    local tickets, next_ticket = {}, 0
    local thumbs, asked = {}, {}
    local function ticket(verb, fields)
        next_ticket = next_ticket + 1
        fields.verb = verb
        tickets[next_ticket] = fields
        return next_ticket, 'ok'
    end
    local function last(verb)
        for t = next_ticket, 1, -1 do
            if tickets[t] and tickets[t].verb == verb then return tickets[t], t end
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
            return { bind = e.bind, presented = e.presented, paints_own_art = e.paints_own_art,
                     box = e.box, local_box = e.box }
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
        invalidate = function() dirty = true end,
        panel = function(descriptor)
            assert(not panel_descriptor, 'the pane is registered once')
            panel_descriptor = descriptor
        end,
        panel_build = function(view) panel_builds[#panel_builds + 1] = view end,
        panel_action = function(ev)
            local row = rows[ev.id]
            if not row then return false end
            if row.on_action then row.on_action(ev.id, ev) end
            return true
        end,
        panel_draw = function() return false end,
        close = function()
            for key, control in pairs(applied) do control.removed = true; applied[key] = nil end
            for name in pairs(held) do released[#released + 1] = name end
            held = {}
        end,
    }

    -- config.set is the host's, and it fires on_config_changed the way the
    -- runtime does.
    config.set = function(key, value)
        config[key] = value
        if product.on_config_changed then product.on_config_changed(host_api, key) end
        return true, 'ok'
    end
    local api = {
        config = config,
        porcelain = porcelain,
        core = { log = function() end, notify = function(text) notices[#notices + 1] = text end },
        client = { datestamp = function() return '2026-09-06_12-00-00' end },
        world = { local_player = function() return { name = 'Test Player' } end },
        assets = {
            image_size = function(ref)
                if ref >= 1000 then return 136, 89 end
                local asset = by_ref[ref]
                assert(asset and asset.ready, 'image_size on a handle that is not ready')
                return asset.w, asset.h
            end,
            -- The plate's source pixels. A plugin that reads these at describe
            -- time and indexes them from inside the layer's paint callback is
            -- the whole reason the read is separate from the paint.
            image_pixels = function(ref)
                local asset = by_ref[ref]
                assert(asset and asset.ready, 'image_pixels on a handle that is not ready')
                assert(asset.pixels, 'image_pixels on a handle with no pixels')
                return asset.pixels
            end,
            screenshot = function(directory, name)
                captures[#captures + 1] = directory .. '|' .. name
                return true, '/saved/' .. directory .. '/' .. name
            end,
            capture_root = function(destination)
                if destination == '/no/good' then return 'none', '' end
                return store.kind, destination ~= '' and destination or store.root
            end,
            capture_list = function(destination)
                return ticket('list', { destination = destination })
            end,
            capture_open = function(destination, path)
                return ticket('open', { destination = destination, path = path })
            end,
            pick_folder = function(start) return ticket('pick', { start = start }) end,
            capture_answer = function(t)
                local tk = tickets[t]
                if not tk then return 'invalid' end
                if not tk.answer then return 'pending' end
                tickets[t] = nil
                return 'ready', tk.answer[1], tk.answer[2]
            end,
            capture_image = function(destination, path, w, h)
                assert(w == 136 and h == 90, 'one thumbnail box, so one cache entry per picture')
                asked[path] = true
                local thumb = thumbs[path]
                if not thumb then
                    thumb = { state = 'pending', ref = 1000 + next_ticket }
                    thumbs[path] = thumb
                end
                if thumb.state == 'ready' then return thumb.ref, 'ready' end
                return nil, thumb.state
            end,
        },
        panel = { invalidate = function() settings_invalidations = settings_invalidations + 1 end },
        -- The raw widget verbs are gone from this plugin for good.
        widgets = { watch = function() error('a Porcelain plugin does not watch widgets') end },
    }

    host_api = api

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
    -- ...DERIVED FROM THE SPRITE, and that is the part worth pinning. The
    -- plate was a palette for one morning -- five tones sampled off a
    -- screenshot and ramped across the box -- and it read as a flat slab
    -- between two pictures. Every cell of it now comes out of chat_button.png,
    -- cache sprite 3051, the picture the lane itself draws under a filter.
    local from_sprite = {}
    for i = 1, #chat_button_pixels do from_sprite[chat_button_pixels[i]] = true end
    local cells = derived_slots['camera_plate.png'].cells
    for i = 1, #cells do
        assert(from_sprite[cells[i]], 'cell ' .. i .. ' of the plate is not a sprite pixel')
    end
    assert(plate.w == 80 and plate.h == 22,
        'the plate is the size of the BUTTON, so nothing REPLACE removed is left as a hole')
    assert(plate.x == 430 and plate.y == 6, 'and it sits exactly where the button was')
    assert(plate.armed == nil,
        'the plate carries no hit box: the camera over it owns the press')
    assert(#derived_paints == 1 and derived_paints[1] == 'camera_plate.png|chat_button.png 80x22',
        'the plate is painted once, keyed by the SOURCE and the box it was cut to')
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
    assert(#derived_paints == 2 and derived_paints[2] == 'camera_plate.png|chat_button.png 100x30',
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

    -- ------------------------------------------------ every image stays held
    -- The layer releases an image no describe run asked for. All three -- both
    -- icons and the plate's source sprite -- are asked for on every run, so a
    -- mode change never pays a second decode.
    for _ = 1, 8 do porcelain.note('explicit'); frame() end
    assert(#released == 0, 'no image is released while the camera is live')
    config.camera = 'bottom-right'; config_changed(); frame()
    assert(assets['camera.png'].requests == 1 and assets['camera_small.png'].requests == 1,
        'switching mode re-uses the held icon rather than re-requesting it')
    assert(only('camera').image == 'camera.png', 'and the corner camera is up at once')

    -- ------------------- the lane whose strip already drew the hollow
    -- THE DEFECT THE OWNER REPORTED: "there used to be JUST THE BUTTON SPRITE
    -- WITH NO BACKGROUND". A plate is right where REPLACE consumes the
    -- target's own picture and WRONG where it consumes none -- there the
    -- hollow under the camera belongs to the strip behind it, and a plate of
    -- the plugin's own is a slab laid over the lane's 2004 stone. One answer
    -- decides it, and it is the engine's.
    config.camera = 'report-button'; config_changed(); frame(); frame()
    local paints_before = #derived_paints
    elements.report_button.paints_own_art = false; porcelain.note('element'); frame()
    local bare = only('camera_report')
    assert(bare.relation == 'replace' and bare.anchor == 'report_button',
        'with no art to consume the camera itself takes the target\'s place')
    assert(bare.image == 'camera_small.png',
        'and it is the camera, not a plate with a camera on it')
    assert(#derived_paints == paints_before,
        'no plate is derived for a target that paints nothing: that is the defect')
    assert(bare.armed == 'Take screenshot',
        'the camera owns the press here, the way it did before there was a plate')
    elements.report_button.paints_own_art = true; porcelain.note('element'); frame()
    assert(exactly('camera_plate', 'camera_report'),
        'and the plate comes back on a lane whose button paints its own')

    -- ---------------------------------------------------------- the gallery
    -- The rail stone: registered once, at on_start, for the PAGE only -- the
    -- settings face is the generated form plus the rows built below.
    assert(panel_descriptor and panel_descriptor.icon_asset == 'panel_icon.png'
        and panel_descriptor.faces == 'page', 'one rail stone with the camera icon, page face')
    local first_list = last('list')
    assert(first_list and first_list.destination == '',
        'on_start asks for the listing of the plugin\'s own folder')
    assert(rows.where.text == 'Saved in /home/p/pics', 'the page says where the folder is')
    assert(rows.open.text == 'Open folder', 'a desktop opens its folder')

    -- Fourteen pictures, out of order, in three albums, one manual.
    local listing = {}
    for i = 1, 12 do
        listing[#listing + 1] = string.format(
            'Test-Player/Levels/Attack-%d_2026-09-%02d_12-00-00.png\t0\t0', i, i)
    end
    listing[#listing + 1] = 'Test-Player/Boss-Kills/Vorkath-100_2026-10-01_09-30-00.png\t0\t0'
    listing[#listing + 1] = 'Test-Player/screenshot_2026-08-01_08-00-00.png\t0\t0'
    -- Steady before the answer: a pending ticket costs a poll, not a rebuild.
    frame(); frame()
    first_list.answer = { 'ok', table.concat(listing, '\n') .. '\n' }
    local rebuilds_before = rebuilds
    frame(); frame()
    assert(rows.status.text == '14 pictures, page 1 of 2', 'counted and paged: ' .. rows.status.text)
    assert(#rows.album.options == 4, 'All plus one option per album')
    assert(rows.album.options[2].label == 'Boss Kills (1)', 'albums by name, with counts')
    assert(rows.album.options[4].label == 'Manual (1)', 'a capture outside an album is Manual')

    -- LAZY. Twelve on the page, so twelve asked for, and not one more.
    local function asked_count()
        local n = 0
        for _ in pairs(asked) do n = n + 1 end
        return n
    end
    assert(asked_count() == 12, 'only the page in view is read: ' .. asked_count())
    assert(asked['Test-Player/Boss-Kills/Vorkath-100_2026-10-01_09-30-00.png'],
        'newest first: October\'s kill heads the first page')
    assert(not asked['Test-Player/Levels/Attack-1_2026-09-01_12-00-00.png'],
        'and the oldest is not read until its page is turned to')
    assert(rows.grid6.height > 0 and rows.older.disabled ~= true and rows.newer.disabled == true,
        'a full first page, an Older and no Newer')

    -- A row paints a placeholder while its pictures are read, and is drawn
    -- again -- that row, by its paint key -- once they land.
    local drawn = {}
    local fake_draw = {
        context = function() return { bounds = { x = 0, y = 0, width = 300, height = 94 } } end,
        rect = function(_, _, _, _, _, alpha) drawn[#drawn + 1] = alpha == 0 and 'frame' or 'fill' end,
        line = function() drawn[#drawn + 1] = 'line' end,
        text = function() error('a well draws no text: not every presenter shows it') end,
        image = function(ref) drawn[#drawn + 1] = 'image:' .. ref end,
    }
    rows.grid1.paint('grid1', fake_draw)
    assert(drawn[3] == 'frame', 'a picture still being read is an empty frame its size')
    assert(rows.caption1.label == 'Vorkath 100 (01/10)' and rows.caption1.text == 'Attack 12 (12/09)',
        'the captions are the row under the pictures, left and right: '
        .. rows.caption1.label .. ' / ' .. rows.caption1.text)
    local key_before = rows.grid1.paint_key
    for _, thumb in pairs(thumbs) do thumb.state = 'ready' end
    frame(); frame()
    assert(rows.grid1.paint_key ~= key_before, 'a landed picture is a new paint key for its row')
    drawn = {}
    rows.grid1.paint('grid1', fake_draw)
    assert(drawn[3]:find('^image:'), 'and the row draws the picture')
    assert(rows.where and rebuilds == rebuilds_before,
        'landing a listing and its pictures is setters, never a page rebuild')

    -- A press on the right half of the first row is the second picture.
    rows.grid1.on_action('grid1', { action = 'activate', x = 200, y = 20 })
    local opened = last('open')
    assert(opened and opened.path == 'Test-Player/Levels/Attack-12_2026-09-12_12-00-00.png',
        'a press opens the picture under it')
    opened.answer = { 'ok', '' }
    frame()

    -- Turning the page reads the two that were left, and is not a rebuild.
    rows.older.on_action('older', { action = 'activate' })
    frame(); frame()
    assert(asked_count() == 14, 'the second page reads its two')
    assert(rows.status.text == '14 pictures, page 2 of 2', 'and says where it is')
    assert(rows.grid1.height > 0 and rows.grid2.height == 0, 'an empty grid row collapses')
    assert(rows.caption2.label == ' ', 'and its caption row says nothing, not its id')
    assert(rebuilds == rebuilds_before, 'a page turn is setters on the rows that exist')

    -- An album narrows the pictures and goes back to its first page.
    rows.album.on_action('album', { action = 'pick', text = 'Boss-Kills|Test-Player' })
    frame(); frame()
    assert(rows.status.text == '1 picture', 'one boss kill: ' .. rows.status.text)

    -- A capture is listed again a few ticks after it is taken.
    local lists_before = next_ticket
    product.on_key(api, { down = true, key = 44 })
    server_tick(); server_tick()
    assert(next_ticket == lists_before, 'not at once: the write has not landed yet')
    server_tick()
    assert(last('list') and next_ticket == lists_before + 1, 'and then once')
    last('list').answer = { 'ok', table.concat(listing, '\n') .. '\n' }
    frame()

    -- -------------------------------------------------- the settings face
    local built = {}
    local fake_panel = {}
    for _, verb in ipairs({ 'heading', 'paragraph', 'label', 'key_value', 'button' }) do
        fake_panel[verb] = function(...) built[#built + 1] = { verb, ... } end
    end
    -- A node is spelled as the builder verb of its kind, id first, so one
    -- lookup serves both: kind 1 is ToriRS_PanelNodeKind's PARAGRAPH.
    fake_panel.node = function(node)
        assert(node.kind == 1, 'only paragraphs travel as nodes here')
        built[#built + 1] = { 'paragraph', node.text, node.id }
    end
    local function built_has(verb, id)
        for _, call in ipairs(built) do
            if call[1] == verb and call[2] == id then return call end
        end
    end
    product.on_ui_build(api, fake_panel, 'settings')
    local shows_folder
    for _, call in ipairs(built) do
        if call[1] == 'paragraph' and call[2] == '/home/p/pics' then shows_folder = true end
    end
    assert(shows_folder, 'settings show the folder')
    assert(built_has('button', 'choose') and built_has('button', 'reveal'),
        'a desktop chooses its folder and opens it; nothing is typed')
    assert(built_has('button', 'default')[4] == false,
        'the default folder is already the one in use')

    -- Choose folder: the platform's chooser, then the folder it answered.
    product.on_ui_action(api, { id = 'choose', action = 'activate' })
    local pick = last('pick')
    assert(pick and pick.start == '/home/p/pics', 'the chooser opens where the pictures are')
    frame()
    pick.answer = { 'cancelled', '' }
    frame()
    assert(config.destination == '', 'a cancelled chooser changes nothing')
    product.on_ui_action(api, { id = 'choose', action = 'activate' })
    last('pick').answer = { 'ok', '/Users/p/Pictures/OSRS (main)\n' }
    local invalidations_before = settings_invalidations
    frame()
    assert(config.destination == '/Users/p/Pictures/OSRS (main)',
        'the chosen folder is the destination, spelled as the chooser spelled it')
    assert(last('list').destination == '/Users/p/Pictures/OSRS (main)',
        'and the gallery lists it at once')
    assert(settings_invalidations > invalidations_before,
        'the open settings face is rebuilt to show it')
    product.on_ui_action(api, { id = 'choose', action = 'activate' })
    last('pick').answer = { 'ok', '/no/good\n' }
    frame()
    assert(config.destination == '/Users/p/Pictures/OSRS (main)',
        'a folder the host would refuse is not stored')
    product.on_ui_action(api, { id = 'default', action = 'activate' })
    assert(config.destination == '', 'Use the default folder clears it')

    -- A browser: no chooser, a tree of what is stored, and a download.
    store.kind, store.root = 'browser', 'plugin_assets/screenshot'
    product.on_config_changed(api, 'destination')
    last('list').answer = { 'ok', table.concat(listing, '\n') .. '\n' }
    frame(); frame()
    assert(rows.open.text == 'Download all (.zip)', 'a browser downloads instead of opening')
    built = {}
    product.on_ui_build(api, fake_panel, 'settings')
    assert(not built_has('button', 'choose'), 'a browser has no folder to choose')
    assert(built_has('key_value', 'tree')[3] == 'screenshot/'
        and built_has('key_value', 'tree')[4] == '14 pictures',
        'the storage is shown from its root, with everything in it counted')
    local levels
    for _, call in ipairs(built) do
        if call[1] == 'key_value' and tostring(call[3]):find('Levels/') then levels = call end
    end
    assert(levels and levels[4] == '12 pictures', 'one line per folder in it, with its count')
    product.on_ui_action(api, { id = 'download', action = 'activate' })
    assert(last('open') and last('open').path == nil, 'Download all asks for every picture')
    product.on_ui_build(api, fake_panel, 'page')
    assert(panel_builds[#panel_builds] == 'page', 'the page face is the layer\'s to build')

    -- ------------------------------------------------------------ teardown
    product.on_stop(api)
    porcelain.close()   -- the host closes the handle after on_stop returns
    assert(#released == 3, 'close releases all three image handles: two icons and the plate art')
    none()
    host.core.log('screenshot behavior verified')
end }
