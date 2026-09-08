-- Appended to the actual shipped source by plugin_lua_test.c. Exercises the
-- camera control's placement modes, the report-button presentation hide and
-- its release, the resize follow-up, the press flash, event/tick captures and
-- teardown against fake widgets; native ownership and clicks are covered by
-- the C suites and native captures.
return { id = 'screenshot-behavior', on_start = function(host)
    local watches, captures, notices, hud = {}, {}, {}, {}
    local config = { destination = '', delay_ticks = 2, on_level_up = true, on_death = false,
        on_valuable_drop = true, on_collection_log = true, min_drop_value = 100000,
        hotkey = 0, camera = 'off' }
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
        widgets = { watch = function(role, callback) watches[role] = callback; return true end,
            find = function(role) return hud[role] end },
    }
    local function widget(name, x, y, w, h, parent)
        local self = { name = name, children = {}, hidden = false, box = { x = x, y = y, width = w, height = h } }
        function self:position() return self.box end
        function self:bounds() return self.box end
        function self:visible() return not self.hidden end
        function self:parent() return parent end
        -- A retired native node answers stale_reference, exactly as the host
        -- does between a root remount and the watch that redelivers the role.
        function self:set_hidden(value)
            if self.stale then return false, 'stale_reference' end
            self.hidden = value; return true
        end
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
    local function schema(key)
        for _, item in ipairs(product.config) do
            if item.key == key then return item end
        end
    end

    -- The hotkey row offers key CODES, and the client only ever broadcasts
    -- codes below TORIRSK_COUNT (53). A wider maximum was a row that accepted
    -- an ASCII value or an SDL scancode, saved it, and then never fired.
    assert(schema('hotkey').max == 52, 'the hotkey row stops at the last code the client delivers')
    assert(schema('hotkey').min == 0, 'zero is still off')

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
        'pressing the control captures into the player folder and goes solid')
    -- The press is a FLASH. Nothing else restores the idle translucency, so
    -- without the tick below 'pressed' is the permanent look of the button.
    assert(one(viewport.children).opacity == 255, 'still solid before the flash expires')
    for _ = 1, 10 do product.on_logic_tick(api) end
    assert(camera.opacity == 170, 'the press flash returns to the idle translucency')
    assert(one(viewport.children) == camera, 'an unchanged viewport does not rebuild the control every tick')

    -- The notice is the file and the folder the PLUGIN chose, not the absolute
    -- path: one GAME line does not wrap and the chatbox clips exactly the tail.
    assert(notices[1] == 'Screenshot saved: Test-Player/screenshot_2026-09-06_12-00-00.png',
        'the notice names the capture, not an unwrappable absolute path')
    assert(not notices[1]:find('/saved/'), 'the absolute path stays in the log')
    config.destination = '/home/me/shots'
    camera.fn(camera)
    assert(notices[#notices] == 'Screenshot saved: Test-Player/screenshot_2026-09-06_12-00-00.png',
        'the folder the user configured is not repeated back at them')
    config.destination = ''
    for _ = 1, 10 do product.on_logic_tick(api) end

    -- A window resize changes the viewport's box without changing its node, so
    -- no watch fires; the corner offsets it was given now name a corner that
    -- has moved. The client tick is what notices.
    viewport.box.width, viewport.box.height = 1158, 800
    product.on_logic_tick(api)
    local moved = one(viewport.children)
    assert(moved.x == 1158 - 28 - 6 and moved.y == 800 - 26 - 6,
        'a window resize moves the corner camera to the new corner')
    product.on_logic_tick(api)
    assert(one(viewport.children) == moved, 'the resize settles in one rebuild')

    -- The camera shares the viewport with a visible native tab strip. Its
    -- click target must occupy free space, not become a replacement tab op.
    for i = 0, 13 do hud['sidetab_' .. i] = widget('tab', 700 + i*33, 768, 33, 36) end
    product.on_logic_tick(api)
    local clear = one(viewport.children)
    assert(clear.x == 1124 and clear.y == 732,
        'corner camera moves above native tab chrome instead of painting beneath it')
    local before_click = #captures
    clear.fn(clear)
    assert(#captures == before_click + 1, 'the relocated visible camera still captures')
    -- Keep later absolute capture counts unchanged: this probe owns its extra
    -- evidence and removes it after checking the operation.
    captures[#captures], notices[#notices] = nil, nil
    hud.frame_sidebar = widget('sidebar', 965, 500, 190, 261)
    product.on_logic_tick(api)
    clear = one(viewport.children)
    assert(clear.x + 28 <= 961 and clear.y + 26 <= 764,
        'opening a sidebar moves the camera clear without a viewport resize')
    for _, covered in pairs(hud) do covered.hidden = true end
    product.on_logic_tick(api)
    assert(one(viewport.children).x == 1124 and one(viewport.children).y == 768,
        'bound but hidden native roles do not displace the corner camera')
    hud = {}


    config.camera = 'report-button'; product.on_config_changed(api, 'camera')
    assert(next(viewport.children) == nil, 'leaving a corner mode removes the corner control')
    assert(report.hidden, 'report-button mode hides only the native button presentation')
    local small = one(bar.children)
    assert(small.image == 2 and small.x == 430 + (80 - 20) // 2 and small.y == 6 + (22 - 16) // 2,
        'the small camera is centred over the report slot in the native parent')
    small.fn(small)
    assert(#captures == 3, 'the report-slot control captures too')

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

    -- A LIVE remount: the node is retired before the watch hands over the new
    -- one, so the next update reaches a stale reference. That is a runtime
    -- state; faulting on it disabled the whole plugin -- camera, hotkey and
    -- every automatic capture -- the first time anyone changed client layout.
    report2.stale = true
    -- The adapter's protected on_start call fails this test on any error;
    -- pcall itself is deliberately unavailable in the plugin sandbox.
    product.on_config_changed(api, 'camera')
    assert(next(bar2.children) == nil, 'the stale reference takes its cover control with it')
    local bar3 = widget('chat_buttons', 0, 470, 519, 33)
    local report3 = widget('report', 430, 6, 80, 22, bar3)
    watches.report_button(report3, { kind = 'bound' })
    assert(report3.hidden and one(bar3.children).image == 2, 'the remount that follows re-establishes the cover')

    product.on_game_event(api, { kind = 'death', subject = 'x', value = -1 })
    assert(#captures == 3, 'disabled event kinds do not capture')
    product.on_game_event(api, { kind = 'valuable_drop', subject = 'Abyssal whip', value = 50000 })
    assert(#captures == 3, 'drops under the threshold do not capture')
    product.on_game_event(api, { kind = 'level_up', subject = 'Attack', value = 40 })
    assert(#captures == 3, 'delayed captures wait for the server tick')
    product.on_server_tick(api); product.on_server_tick(api)
    assert(#captures == 4 and captures[4] == 'Test-Player/Levels|Attack-40_2026-09-06_12-00-00.png',
        'level up captures after delay_ticks into its category folder')

    -- A long subject must still make a filename the host accepts: the guard is
    -- `len < 64` and a refusal is only a log line, so an over-long name was a
    -- capture that silently did not happen.
    -- The subject below is cut exactly on a word separator, so the trim has to
    -- take the dangling '-' with it rather than run it into the value.
    product.on_game_event(api, { kind = 'collection_log', value = 1234567890,
        subject = 'Trailblazer reloaded relics hunter (t3) armour set' })
    product.on_server_tick(api); product.on_server_tick(api)
    local shot = captures[#captures]:match('|(.+)$')
    assert(#shot < 64, 'a long subject still fits the host name limit, got ' .. #shot)
    assert(shot:find('^Trailblazer%-reloaded%-relics'), 'the trimmed name still leads with the subject')
    assert(not shot:find('%-[%-_]'), 'trimming leaves no dangling separator')
    assert(shot:find('%-1234567890_2026%-09%-06_12%-00%-00%.png$'), 'the value and the stamp survive the trim')

    config.hotkey = 44
    product.on_key(api, { down = true, key = 44 })
    assert(#captures == 6, 'the hotkey captures immediately')

    -- A refused capture is visible. The host answers a refusal with a reason
    -- and no file, which used to reach only the plugin log, so a shot that did
    -- not happen looked exactly like one that did.
    local screenshot = api.assets.screenshot
    api.assets.screenshot = function() return false, 'invalid_argument' end
    product.on_key(api, { down = true, key = 44 })
    assert(#captures == 6, 'a refusal writes nothing')
    assert(notices[#notices] == 'Screenshot failed (invalid_argument)',
        'a refused capture says so instead of passing for a saved shot')
    api.assets.screenshot = screenshot

    product.on_stop(api)
    assert(#released == 2, 'stop releases both image handles')
    host.core.log('screenshot behavior verified')
end }
