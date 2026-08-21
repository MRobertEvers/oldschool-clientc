-- Development probe: exercises every api surface and prints what it sees.
-- Not shipped in plugins.ini; loaded on demand via TORIRS_PLUGIN_MANIFEST.
local plugin = {
    name = "probe",
    config = {
        { key = "tint", type = "color", label = "Tint", default = "#20C0FF" },
    },
}

local draws = 0

function plugin.on_start(api)
    api.log("PROBE start, tint=", api.config.tint)
end

function plugin.on_world_loaded(api, w)
    api.log("PROBE world_loaded base=", w.base_tile_x, w.base_tile_z)
end

function plugin.on_server_tick(api, ev)
    if ev.cycle % 40 == 0 then
        api.log("PROBE server_tick cycle=", ev.cycle)
    end
end

function plugin.on_draw_world(api, draw)
    draws = draws + 1
    if draws == 400 then
        local me = api.local_player()
        if me then
            api.log("PROBE player true=", me.true_x, me.true_z, "lvl", me.level,
                    "dest", me.dest_x, me.dest_z, "name", me.name)
        end
        local n = 0
        for _, npc in api.npcs() do
            n = n + 1
            if n <= 10 then
                api.log("PROBE npc", n, "base=", npc.base_npc_id, "id=", npc.npc_id,
                        "slot=", npc.server_slot, "elem=", npc.element_id,
                        "size=", npc.size, "name=", npc.name)
            end
        end
        api.log("PROBE npcs visible:", n)
    end
end

return plugin
