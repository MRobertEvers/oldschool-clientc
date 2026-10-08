-- Olm shelf + NR 5x7: corridor, chamber open view, west-wall rise.
local HEAD = "olm_head"
local HEAD_SPAWN = "olm_head_spawning"

local function tile_text(t)
    local r, tile = t.world.tile()
    if r ~= "ok" or type(tile) ~= "table" then
        return tostring(r) .. "/" .. tostring(tile)
    end
    return string.format("%d,%d,%d", tile.x, tile.z, tile.level)
end

local function npc_ok(t, name)
    local r, row = t.npc.state(name)
    if r == "ok" then return row end
    return nil
end

return {
    id = "cox_olm_chamber_shot",
    fixture = "fresh_lumbridge.ini",
    frames = 1200,
    setup = { "::maxstats", "::godmode" },
    run = function(t)
        t.check("scope", true, "5x7 shelf; west rise + east idle caves")
        local er, ed = t.raid.enter("cox", "olm", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))

        local hr, hd = t.player.click_loc("raids_bossentrance", 1)
        t.check("hole.click", hr == "ok" or hr == "timeout", tostring(hr) .. " " .. tostring(hd))
        t.ticks(8)
        local tr, tile = t.world.tile()
        t.check("corridor.tile", tr == "ok" and type(tile) == "table" and tile.level == 2,
            "want plane 2 corridor, got " .. tile_text(t))
        t.player.walk_to(tile.x, tile.z + 4, 24)
        t.ticks(2)
        t.drive.camera(0, 180, 500)
        t.ticks(3)
        t.shot("olm corridor before barrier at " .. tile_text(t))

        local cr, cd = t.player.click_loc("raids_olm_barrier", 1)
        t.check("barrier.click", cr == "ok" or cr == "timeout", tostring(cr) .. " " .. tostring(cd))
        t.chat.play({ "options", "choose:Step through the mystical barrier." })
        t.ticks(12)

        tr, tile = t.world.tile()
        t.check("chamber.plane", type(tile) == "table" and tile.level == 2,
            "want plane 2 chamber, got " .. tile_text(t))
        -- Open arena looking north (not into the west-wall rock).
        t.drive.camera(0, 200, 700)
        t.ticks(3)
        t.shot("olm chamber looking north at " .. tile_text(t))

        local head = npc_ok(t, HEAD) or npc_ok(t, HEAD_SPAWN)
        t.check("boss.present", head ~= nil, "head/spawn at " .. tile_text(t))
        -- From centre look west at the rise — stay at chamber x, do not walk
        -- into raids_olmic_head2 carved heads at window (12,24)/(19,24).
        t.drive.camera(1536, 180, 650)
        t.ticks(4)
        t.shot("olm west wall rise at " .. tile_text(t))
        -- East idle bank: NR keeps empty Large rock/hole caves (rot 1).
        t.drive.camera(512, 180, 650)
        t.ticks(4)
        t.shot("olm east wall idle caves at " .. tile_text(t))
        -- North: west claws on the left, east empty rocks on the right.
        t.drive.camera(0, 200, 700)
        t.ticks(3)
        t.shot("olm chamber north after spawn at " .. tile_text(t))
        t.check("done", true, "shots at " .. tile_text(t))
    end,
}
