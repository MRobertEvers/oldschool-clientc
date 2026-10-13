-- cox_olm_rise_compare: the same camera on Olm's wall before and after he rises.
--
-- Before the rise the wall holds the STATIC caves (olm_head_cave, olm_hand_*_cave:
-- the map's locs, rotation baked at build). After it, the risen locs (olm_head,
-- olm_hand_left/right) stand on the same tiles at the same angle and ANIMATE
-- (config idle seqs + LOC_ANIMs). The risen claws reuse their cave's rock-frame
-- model, so a rock that jumps between the pairs is the animated-loc path turning
-- a model differently from the static one.
--
--   python3 tools/raid_gate/run.py cox_olm_rise_compare --no-publish

local YAWS = { 0, 512, 1024, 1536 }
local CAMS = { { 220, 1100 } }

local function shoot(t, label)
    for _, y in ipairs(YAWS) do
        for _, c in ipairs(CAMS) do
            t.drive.camera(y, c[1], c[2])
            t.ticks(1)
            t.shot(string.format("%s y%d p%d z%d", label, y, c[1], c[2]))
        end
    end
end

return {
    id = "cox_olm_rise_compare",
    fixture = "fresh_lumbridge.ini",
    frames = 6000,
    setup = { "::maxstats", "::godmode" },
    run = function(t)
        t.cheat("::coxolmrush", false)
        t.ticks(4)
        local hr, hd = t.player.click_loc("raids_olm_barrier", 1)
        t.check("barrier.click", hr == "ok" or hr == "timeout", tostring(hr) .. " " .. tostring(hd))
        t.chat.play({ "options", "choose:Step through the mystical barrier." })
        local r0, start = t.world.tile()
        t.check("start", r0 == "ok", string.format("%d,%d,%d", start.x, start.z, start.level))
        -- the chamber's centre row; before the rise (+10)
        t.cheat(string.format("::tele %d_%d_%d_%d_%d", start.level, start.x // 64, (start.z + 6) // 64,
            start.x % 64, (start.z + 6) % 64), false)
        t.ticks(1)
        shoot(t, "before")
        -- well after the rise and its first actions; stand still so the head
        -- faces me
        t.ticks(30)
        shoot(t, "after")
        -- the head follows me: the south-only row, then the north-only row,
        -- a frame a tick of the west wall
        for _, dz in ipairs({ -6, 6 }) do
            local z = start.z + 6 + dz
            t.cheat(string.format("::tele %d_%d_%d_%d_%d", start.level, start.x // 64, z // 64, start.x % 64, z % 64), false)
            for i = 1, 10 do
                t.drive.camera(512, 220, 1100)
                t.ticks(1)
                t.shot(string.format("follow dz%d t%02d", dz, i))
            end
        end
        t.check("done", true, "")
    end,
}
