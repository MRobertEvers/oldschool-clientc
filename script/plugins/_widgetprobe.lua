-- Same live-widget source on CS1/revconfig and CS2, including native remounts.
local plugin = { id = "widgetprobe", version = "3" }
local label
local function update(api)
    local strength = api.game.skill(2)
    if label and strength then label:set_text("Strength: " .. strength.current_level) end
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
        assert(label:set_position(12, 40))
        assert(label:set_text_color(0xffffff))
        update(api)
        assert(label:revalidate())
    end))
end
function plugin.on_server_tick(api) update(api) end
function plugin.on_stop() label = nil end
return plugin
