-- Scratch: NR-shaped Olm enter — hole → corridor (32,24), barrier → chamber (32,38).
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
    if r == "ok" then
        return row
    end
    return nil
end

return {
    id = "cox_olm_chamber_shot",
    fixture = "fresh_lumbridge.ini",
    frames = 1200,
    setup = {
        "::maxstats",
        "::godmode",
    },

    run = function(t)
        t.check("scope", true, "NR hole→corridor (32,24); barrier→chamber (32,38); west caves (20,*)")
        local er, ed = t.raid.enter("cox", "olm", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))

        local hr, hd = t.player.click_loc("raids_bossentrance", 1)
        t.check("hole.click", hr == "ok" or hr == "timeout", tostring(hr) .. " " .. tostring(hd))
        t.ticks(8)
        local tr, tile = t.world.tile()
        t.check("corridor.tile", tr == "ok" and type(tile) == "table" and tile.level == 2,
            "want plane 2 corridor, got " .. tile_text(t))
        -- Walk north toward the barrier; look north.
        t.player.walk_to(6432, 92, 20)
        t.ticks(2)
        t.drive.camera(0, 180, 500)
        t.ticks(3)
        t.shot("olm corridor before barrier at " .. tile_text(t))

        local cr, cd = t.player.click_loc("raids_olm_barrier", 1)
        t.check("barrier.click", cr == "ok" or cr == "timeout", tostring(cr) .. " " .. tostring(cd))
        local chr, chd = t.chat.play({ "options", "choose:Step through the mystical barrier." })
        t.check("barrier.confirm", chr == "ok" or chr == "timeout", tostring(chr) .. " " .. tostring(chd))
        t.ticks(12)

        tr, tile = t.world.tile()
        t.check("tile.read", tr == "ok", tostring(tile))
        t.check("chamber.plane", type(tile) == "table" and tile.level == 2,
            "want plane 2 chamber, got " .. tile_text(t))
        -- Look west at the cave bank.
        t.drive.camera(1536, 200, 700)
        t.ticks(3)
        t.shot("olm chamber after barrier at " .. tile_text(t))

        local wait = 0
        local head = npc_ok(t, HEAD)
        while head == nil and wait < 40 do
            wait = wait + 1
            t.ticks(1)
            head = npc_ok(t, HEAD) or npc_ok(t, HEAD_SPAWN)
        end
        if head == nil then
            t.cheat("::coxolm")
            t.ticks(12)
            head = npc_ok(t, HEAD) or npc_ok(t, HEAD_SPAWN)
        end
        t.check("boss.present", head ~= nil, "waited for olm_head at " .. tile_text(t))
        -- Closer to west wall, looking west at head cave.
        t.player.walk_to(6424, 102, 20)
        t.ticks(2)
        t.drive.camera(1536, 160, 500)
        t.ticks(4)
        t.shot("olm west caves after spawn at " .. tile_text(t))
        t.check("done", true, "chamber shots at " .. tile_text(t))
    end,
}
