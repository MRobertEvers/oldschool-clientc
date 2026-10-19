-- _icedemon_facing: the frozen Ice Demon faces the way he will walk out, in
-- every turn of his room.
--
-- RSProx 3285 (two rooms), 843, 3282, 3389, 820, every room turned: the add's
-- angle is the room's turn of his template facing (CCW east, THRU south, CW
-- west), and in all six it is the direction of his first step out of the storm
-- after the pop (0 south, 512 west, 1024 north, 1536 east); nothing turns him
-- while he is frozen (docs/rsprox-analysis/cox/icedemon.md, RI-S1). Ours used to
-- leave him at the engine's south whatever the turn. The walk out is seven
-- tiles straight (CW used to step diagonally and stop two tiles out).
--
-- The run name's number picks the layout (::coxtest 3 <n>), as cox_icedemon:
--   ./src/<objdir>/torirsserver --scriptrun test/raids/_icedemon_facing.lua --bots 1 \
--       --name face0 --ticks 1500 --session /tmp/iceface --fixture test/raids/fixtures/fresh_lumbridge.ini
local FROZEN, FIGHTING = 7584, 7585

local function coord_of(c)
    return (c >> 14) & 0x3fff, c & 0x3fff
end

local function sign(v)
    if v > 0 then return 1 elseif v < 0 then return -1 end
    return 0
end

return {
    id = "_icedemon_facing",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = { "::setlevel hitpoints 99", "::setlevel firemaking 99" },
    run = function(t)
        t.ticklog.start()
        local n = t.raid.cox_run_n()
        local er, ed = t.raid.cox_test_enter("icedemon", n)
        if er == "timeout" then
            for _ = 1, 40 do
                t.ticks(1)
                if t.npc.nearest("raids_icedemon_noncombat", 60) == "ok" then er = "ok" break end
            end
        end
        t.check("face.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
        if er ~= "ok" then t.finish(1) return end
        t.ticks(2)

        local _, spawns = t.ticklog.rows({ kind = "npc_spawn" })
        t.ticks(1)
        local slot, sx, sz
        for _, s in ipairs(spawns or {}) do
            if s.type == FROZEN then
                slot = s.slot
                sx, sz = coord_of(s.coord)
            end
        end
        t.check("face.spawned", slot ~= nil, "an npc_spawn row for 7584")
        if slot == nil then t.finish(1) return end
        local _, faces = t.ticklog.rows({ kind = "npc_face" })
        t.ticks(1)
        local face
        for _, f in ipairs(faces or {}) do
            if f.slot == slot and f.type == FROZEN then face = f end
        end
        t.check("face.faced", face ~= nil, "an npc_face row for the frozen demon")
        if face == nil then t.finish(1) return end
        -- his 2x2's middle is (sx + 1, sz + 1) in tile edges; the square's (x + .5, z + .5)
        local vx, vz = 2 * face.x + 1 - 2 * (sx + 1), 2 * face.z + 1 - 2 * (sz + 1)
        local fdx, fdz = 0, 0
        if math.abs(vx) > math.abs(vz) then fdx = sign(vx) else fdz = sign(vz) end

        -- the thaw off the server's log: the raider may stand beyond the
        -- client's npc view of him (seed 15: 31 tiles)
        t.cheat("::cox_icedemon_fuel", false)
        local up = false
        for _ = 1, 40 do
            t.ticks(10)
            local _, retypes = t.ticklog.rows({ kind = "npc_retype" })
            for _, r in ipairs(retypes or {}) do
                if r.slot == slot and r.to_type == FIGHTING then up = true end
            end
            if up then break end
        end
        t.check("face.thawed", up, "retyped to raids_icedemon_combat")
        if not up then t.finish(1) return end
        local _, tiles = t.ticklog.rows({ kind = "npc_tile" })
        t.ticks(1)
        -- RSProx: seven tiles straight out, the first step the way he faced
        local wdx, wdz, ex, ez
        local straight = true
        for _, r in ipairs(tiles or {}) do
            if r.slot == slot and r.type == FROZEN and (r.x ~= sx or r.z ~= sz) then
                if wdx == nil then wdx, wdz = sign(r.x - sx), sign(r.z - sz) end
                if r.x ~= sx and r.z ~= sz then straight = false end
                ex, ez = r.x, r.z
            end
        end
        t.check("face.walked", wdx ~= nil, "a step out of the storm")
        if wdx == nil then t.finish(1) return end
        local walk_ok = straight and math.abs(ex - sx) + math.abs(ez - sz) == 7
        t.check("face.walk_out", walk_ok, string.format("%d,%d -> %d,%d", sx, sz, ex, ez))
        local ok = fdx == wdx and fdz == wdz
        t.check("face.recorded", ok, string.format(
            "seed %s at %d,%d: frozen facing (%d,%d) toward %d,%d, first step (%d,%d)",
            tostring(ed and ed.seed), sx, sz, fdx, fdz, face.x, face.z, wdx, wdz))
        t.finish((ok and walk_ok) and 0 or 1)
    end,
}
