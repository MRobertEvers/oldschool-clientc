-- Native widget probe: identical source on CS1/revconfig and CS2.
local plugin = { id = "widgetprobe", version = "3" }
local moved = false
function plugin.on_logic_tick(api)
    if moved then return end
    local sidebar = api.widgets.find("sidebar")
    if not sidebar then return end
    local before = sidebar:position()
    if not before or before.width <= 0 then return end
    local ok = sidebar:set_position(before.x - 12, before.y)
    if not ok then return end
    assert(sidebar:revalidate())
    local after = sidebar:position()
    assert(after.x == before.x - 12 and after.y == before.y)
    api.core.log("LUA_WIDGET_DEMO", before.x, before.y, after.x, after.y)
    moved = true
end
return plugin
