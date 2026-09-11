-- Same live-widget source on CS1/revconfig and CS2, including native remounts.
local plugin = { id = "widgetprobe", version = "3" }
local label, control, last_level
local function set_public_friends(api)
    local button = api.widgets.find("public_chat_button")
    if not button then return false, "unavailable" end
    local actions = button:actions()
    if not actions then return false, "native_blocked" end
    for _, action in ipairs(actions) do
        if action.label:find("[Ff]riends") then return api.widgets.invoke(action.ref) end
    end
    return false, "no_friends_action"
end
local function update(api)
    local strength = api.game.skill(2)
    if label and strength and last_level ~= strength.current_level then
        last_level = strength.current_level
        label:set_text("Strength: " .. last_level)
    end
end
function plugin.on_start(api)
    assert(api.widgets.watch("sidebar", function(sidebar, event)
        if event.kind == "unbound" then
            sidebar:reset()
            api.core.log("LUA_WIDGET_DEMO_UNBOUND")
            return
        end
        local before = assert(sidebar:position())
        assert(sidebar:set_position(before.x - 12, before.y))
        assert(sidebar:revalidate())
        local after = assert(sidebar:position())
        assert(after.x == before.x - 12 and after.y == before.y)
        api.core.log("LUA_WIDGET_DEMO", before.x, before.y, after.x, after.y)
    end))
    assert(api.widgets.watch("viewport", function(viewport, event)
        if event.kind == "unbound" then
            if label then label:remove() end
            label = nil
            return
        end
        label = assert(viewport:create_text("strength"))
        last_level = nil
        assert(label:set_position(12, 40))
        assert(label:set_text_color(0xffffff))
        update(api)
        assert(label:revalidate())
        -- An owned control: a text child armed with one operation. Pressing it
        -- invokes a checked native operation on a native widget (public chat
        -- to Friends) through the normal hit test and menu dispatch.
        control = assert(viewport:create_text("public"))
        assert(control:set_position(12, 56))
        assert(control:set_text("Public: Friends"))
        assert(control:set_text_color(0x00ffff))
        assert(control:set_on_op("Set public chat to friends", function(widget, event)
            assert(event.kind == "operation")
            local ok, reason = set_public_friends(api)
            api.core.log("LUA_WIDGET_DEMO_OP", tostring(ok), reason or "ok")
            if ok then widget:set_text("Public: set") end
        end))
        assert(control:revalidate())
        local box = assert(control:bounds())
        api.core.log("LUA_WIDGET_DEMO_OP_BOUNDS", box.x, box.y, box.width, box.height)
    end))
end
function plugin.on_logic_tick(api) update(api) end
function plugin.on_stop() label, control, last_level = nil, nil, nil end
return plugin
