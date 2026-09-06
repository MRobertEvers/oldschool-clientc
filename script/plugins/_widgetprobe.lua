-- Same event-driven source on CS1/revconfig and CS2, including native remounts.
local plugin = { id = "widgetprobe", version = "3" }
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
end
return plugin
