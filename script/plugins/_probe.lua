-- Development probe: exercises every api surface and prints what it sees.
-- Not shipped in plugins.ini; loaded on demand via TORIRS_PLUGIN_MANIFEST.
---@type torirs.Plugin
local plugin = {
    id = "probe",
    config = {
        { key = "tint", type = "color", label = "Tint", default = "#20C0FF" },
    },
}

local draws = 0

local function npcs(api)
    local cursor = -1
    return function()
        local next_cursor, npc = api.world.npc_next(cursor)
        if not next_cursor then return nil end
        cursor = next_cursor
        return npc
    end
end

function plugin.on_start(api)
    api.core.log("PROBE start, tint=", api.config.tint)
end

function plugin.on_world_loaded(api, w)
    api.core.log("PROBE world_loaded base=", w.base_tile_x, w.base_tile_z)
end

function plugin.on_server_tick(api, ev)
    if ev.cycle % 40 == 0 then
        api.core.log("PROBE server_tick cycle=", ev.cycle)
    end
end

function plugin.on_draw_world(api, draw)
    draws = draws + 1
    if draws == 400 then
        local me = api.world.local_player()
        if me then
            api.core.log("PROBE player true=", me.true_x, me.true_z, "lvl", me.level,
                    "dest", me.dest_x, me.dest_z, "name", me.name)
        end
        local n = 0
        for npc in npcs(api) do
            n = n + 1
            if n <= 10 then
                api.core.log("PROBE npc", n, "base=", npc.base_npc_id, "id=", npc.npc_id,
                        "slot=", npc.server_slot, "elem=", npc.element_id,
                        "size=", npc.size, "name=", npc.name)
            end
        end
        api.core.log("PROBE npcs visible:", n)
    end
end

return plugin
