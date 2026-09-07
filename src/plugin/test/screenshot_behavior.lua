-- Appended to the actual shipped source by plugin_lua_test.c. Exercises the
-- camera control's placement modes, the report-button presentation hide and
-- its release, event/tick captures and teardown against fake widgets; native
-- ownership and clicks are covered by the C suites and native captures.
return { id = 'screenshot-behavior', on_start = function(host)
    local watches, captures, notices = {}, {}, {}
    local config = { destination = '', delay_ticks = 2, on_level_up = true, on_death = false,
        on_valuable_drop = true, min_drop_value = 100000, hotkey = 0, camera = 'off' }
    local images = { ['camera.png'] = 1, ['camera_small.png'] = 2 }
    local sizes = { [1] = { 28, 26 }, [2] = { 20, 16 } }
    local released = {}
    local api = {
        config = config,
        core = { log = function() end, notify = function(text) notices[#notices + 1] = text end },
        client = { datestamp = function() return '2026-09-06_12-00-00' end },
        world = { local_player = function() return { name = 'Test Player' } end },
        assets = {
            image = function(name) return images[name], 'ready' end,
            image_size = function(image) return sizes[image][1], sizes[image][2] end,
            image_release = function(image) released[#released + 1] = image end,
            screenshot = function(directory, name)
                captures[#captures + 1] = directory .. '|' .. name
                return true, '/saved/' .. directory .. '/' .. name
            end,
        },
        widgets = { watch = function(role, callback) watches[role] = callback; return true end },
    }
    local function widget(name, x, y, w, h, parent)
        local self = { name = name, children = {}, hidden = false, box = { x = x, y = y, width = w, height = h } }
        function self:position() return self.box end
        function self:bounds() return self.box end
        function self:parent() return parent end
        function self:set_hidden(value) self.hidden = value; return true end
        function self:create_image(key)
            assert(not self.children[key], 'duplicate owned control ' .. key)
            local control = { key = key, ops = {} }
            function control:set_image(image, w2, h2) self.image, self.w, self.h = image, w2, h2; return true end
            function control:set_position(x2, y2) self.x, self.y = x2, y2; return true end
            function control:set_opacity(value) self.opacity = value; return true end
            function control:set_on_op(label, fn) self.label, self.fn = label, fn; return true end
            function control:revalidate() return true end
            function control:bounds() return { x = self.x, y = self.y, width = self.w, height = self.h } end
            function control:remove() self.removed = true; self.children_parent.children[key] = nil end
            control.children_parent = self
            self.children[key] = control
            return control
        end
        return self
    end
    local function one(t) local k, v = next(t); assert(k and next(t, k) == nil, 'exactly one control'); return v end

    product.on_start(api)
    assert(watches.viewport and watches.report_button, 'binds viewport and report button')
    local viewport = widget('viewport', 4, 4, 512, 334)
    local bar = widget('chat_buttons', 0, 470, 519, 33)
    local report = widget('report', 430, 6, 80, 22, bar)
    watches.viewport(viewport, { kind = 'bound' })
    watches.report_button(report, { kind = 'bound' })
    assert(next(viewport.children) == nil and next(bar.children) == nil and not report.hidden, 'camera off places nothing')

    config.camera = 'bottom-right'; product.on_config_changed(api, 'camera')
    local camera = one(viewport.children)
    assert(camera.image == 1 and camera.w == 28 and camera.h == 26, 'corner control shows the camera image at its size')
    assert(camera.x == 512 - 28 - 6 and camera.y == 334 - 26 - 6, 'bottom-right corner keeps the margin')
    assert(camera.label == 'Take screenshot' and camera.opacity == 170, 'control is armed with the capture operation')
    camera.fn(camera)
    assert(#captures == 1 and captures[1]:find('^Test%-Player|screenshot_') and camera.opacity == 255,
        'pressing the control captures into the player folder')
    assert(notices[1]:find('Screenshot saved'), 'capture notifies')

    config.camera = 'report-button'; product.on_config_changed(api, 'camera')
    assert(next(viewport.children) == nil, 'leaving a corner mode removes the corner control')
    assert(report.hidden, 'report-button mode hides only the native button presentation')
    local small = one(bar.children)
    assert(small.image == 2 and small.x == 430 + (80 - 20) // 2 and small.y == 6 + (22 - 16) // 2,
        'the small camera is centred over the report slot in the native parent')
    small.fn(small)
    assert(#captures == 2, 'the report-slot control captures too')

    config.camera = 'off'; product.on_config_changed(api, 'camera')
    assert(not report.hidden and next(bar.children) == nil, 'camera off releases the native hide and the control')

    config.camera = 'report-button'; product.on_config_changed(api, 'camera')
    assert(one(bar.children).image == 2, 'report-button mode is re-established')
    -- A native remount: the old bar and its owned children are gone with it.
    watches.report_button(report, { kind = 'unbound' })
    local bar2 = widget('chat_buttons', 0, 470, 519, 33)
    local report2 = widget('report', 430, 6, 80, 22, bar2)
    watches.report_button(report2, { kind = 'bound' })
    assert(report2.hidden and one(bar2.children).image == 2, 'a rebound report button is hidden and covered again')

    product.on_game_event(api, { kind = 'death', subject = 'x', value = -1 })
    assert(#captures == 2, 'disabled event kinds do not capture')
    product.on_game_event(api, { kind = 'valuable_drop', subject = 'Abyssal whip', value = 50000 })
    assert(#captures == 2, 'drops under the threshold do not capture')
    product.on_game_event(api, { kind = 'level_up', subject = 'Attack', value = 40 })
    assert(#captures == 2, 'delayed captures wait for the server tick')
    product.on_server_tick(api); product.on_server_tick(api)
    assert(#captures == 3 and captures[3] == 'Test-Player/Levels|Attack-40_2026-09-06_12-00-00.png',
        'level up captures after delay_ticks into its category folder')
    config.hotkey = 44
    product.on_key(api, { down = true, key = 44 })
    assert(#captures == 4, 'the hotkey captures immediately')

    product.on_stop(api)
    assert(#released == 2, 'stop releases both image handles')
    host.core.log('screenshot behavior verified')
end }
