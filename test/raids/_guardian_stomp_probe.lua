-- _guardian_stomp_probe: the Guardians' swings at a raider who stays beside a
-- statue (no flinching), read off the server's tick log against RSProx
-- (docs/rsprox-analysis/cox/guardians.md, "Tells").
--
-- RSProx 820, 3282, 3389 (111 rocks): a stomp's rock `motherlode_rockfall`
-- (645) drops from a random one of the eight tiles round its target tile, start
-- 15, end 30; the statue's block is human_spear_block (430).
--
--   ./src/<objdir>/torirsserver --scriptrun test/raids/_guardian_stomp_probe.lua --bots 1 \
--       --name stomp0 --ticks 1500 --session /tmp/stomp --fixture test/raids/fixtures/fresh_lumbridge.ini
local ROCK, BLOCK = 645, 430

local function coord_of(c)
    return (c >> 14) & 0x3fff, c & 0x3fff
end

return {
    id = "_guardian_stomp_probe",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = {
        "::setlevel hitpoints 99", "::setlevel attack 99", "::setlevel strength 99", "::setlevel mining 99",
        "::clearinv", "::give dragon_pickaxe 1", "::wield dragon_pickaxe", "::god 1",
    },
    run = function(t)
        t.ticklog.start()
        local n = t.raid.cox_run_n()
        local er, ed = t.raid.cox_test_enter("guardians", n)
        t.check("stomp.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
        if er ~= "ok" then t.finish(1) return end
        t.ticks(2)
        -- stay on it: every attack re-issued at once, no step away
        for _ = 1, 40 do
            if t.npc.nearest("raids_stoneguardians_left", 40) ~= "ok" then break end
            t.player.attack("raids_stoneguardians_left", 2, 5)
        end
        local _, projs = t.ticklog.rows({ kind = "projectile" })
        t.ticks(1)
        local _, anims = t.ticklog.rows({ kind = "npc_anim" })
        t.ticks(1)
        local offs, bad, rocks = {}, {}, 0
        for _, p in ipairs(projs or {}) do
            if p.spotanim == ROCK then
                rocks = rocks + 1
                local sx, sz = coord_of(p.src)
                local dx, dz = coord_of(p.dst)
                local k = string.format("%d,%d", sx - dx, sz - dz)
                offs[k] = (offs[k] or 0) + 1
                if math.max(math.abs(sx - dx), math.abs(sz - dz)) ~= 1 then bad[#bad + 1] = k end
                if p.end_cycle - p.start_cycle ~= 15 or p.start_cycle ~= 15 then
                    bad[#bad + 1] = string.format("cycles %d..%d", p.start_cycle, p.end_cycle)
                end
            end
        end
        local blocks, swings = 0, 0
        for _, a in ipairs(anims or {}) do
            if a.type == 7569 or a.type == 7570 then
                if a.seq == BLOCK then blocks = blocks + 1 end
                if a.seq == 1203 or a.seq == 4278 then swings = swings + 1 end
            end
        end
        local parts = {}
        for k, v in pairs(offs) do parts[#parts + 1] = k .. " x" .. v end
        table.sort(parts)
        t.check("stomp.swung", swings > 0, string.format("%d swings, %d blocks", swings, blocks))
        t.check("stomp.rocks", rocks > 0 and #bad == 0,
            string.format("%d rocks, source - target: %s %s", rocks, table.concat(parts, " "), table.concat(bad, " ")))
        t.finish((rocks > 0 and #bad == 0) and 0 or 1)
    end,
}
