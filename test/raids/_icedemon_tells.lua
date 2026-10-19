-- _icedemon_tells: the Ice Demon's attacks send what Jagex's server sends.
-- RSProx 3285/843/3282/3389/820, 46 attacks in instance tiles
-- (docs/rsprox-analysis/cox/icedemon.md, tools/cox/rsprox_icedemon.py):
--   both: from his south-west tile, start 51, ending on the landing tick;
--   Ice Burst: ice_burst_travel (366) and ice_cast (171) at the cast; at the
--     landing nine ice_blitz_impact (363), delays 0,3,3,6,6,9,9,12,12, height
--     10, and ice_burst_impact (170);
--   boulder: raids_icedemon_iceball_travel (1324) and castlewars_catapult_fire
--     (1440) at the cast; at the landing raids_icedemon_iceball_hit (1325),
--     height 10, and 100_cauldron_explodes (1021).
--   one seq a tick (RI-A2): demon_casting (69) on his attack tick, but his
--     demon_block (65) when the raider's swing lands a defend on him that tick
--     (29 of 29 recorded; the two are of equal priority, and the later wins).
-- Solo, 99 hitpoints, no prayers (so he picks either style), standing near him
-- after ::cox_icedemon_fuel has thawed him. The solver's room test prays
-- Protect from Missiles and never sees a burst.
local BURST, BOULDER = 366, 1324

local function coord_of(c)
    return (c >> 14) & 0x3fff, c & 0x3fff
end

return {
    id = "_icedemon_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel firemaking 99",

    },
    run = function(t)
        t.ticklog.start()
        local er, ed = t.raid.cox_test_enter("icedemon", 1)
        t.check("tells.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
        if er ~= "ok" then t.finish(1) return end
        t.ticks(2)
        t.cheat("::cox_icedemon_fuel", false)
        local up = false
        for _ = 1, 400 do
            if t.npc.nearest("raids_icedemon_combat", 40) == "ok" then up = true break end
            t.ticks(1)
        end
        t.check("tells.thawed", up, "raids_icedemon_combat up")
        if not up then t.finish(1) return end
        local _, demon = t.npc.nearest("raids_icedemon_combat", 40)
        local near = false
        for _, d in ipairs({ { -4, 0 }, { 0, -4 }, { 5, 0 }, { 0, 5 }, { -3, -3 }, { 4, 4 } }) do
            if t.player.walk_to(demon.x + d[1], demon.z + d[2], 40) == "ok" then near = true break end
        end
        t.check("tells.near", near, "walked within 5 of him")
        -- he answers an attack; he does not start one with a raider who walks
        -- up after his transform
        local ar, ad = t.player.attack("raids_icedemon_combat")
        t.check("tells.attack", ar == "ok", tostring(ad))
        t.ticks(30)
        -- the server's log: every projectile from his tile, and what lands
        local pr, projs = t.ticklog.rows({ kind = "projectile" })
        t.ticks(1)
        local sr, spots = t.ticklog.rows({ kind = "map_spotanim" })
        t.ticks(1)
        local nr, sounds = t.ticklog.rows({ kind = "sound" })
        t.ticks(1)
        local ar2, nanims = t.ticklog.rows({ kind = "npc_anim" })
        t.ticks(1)
        local pr2, panims = t.ticklog.rows({ kind = "player_anim" })
        -- the last seq the demon took each tick is the one sent
        local last_seq, swung = {}, {}
        for _, a in ipairs(ar2 == "ok" and nanims or {}) do
            if a.type == 7585 then last_seq[a.tick] = a.seq end
        end
        for _, a in ipairs(pr2 == "ok" and panims or {}) do
            if a.seq == 422 or a.seq == 423 then swung[a.tick] = true end
        end
        local clashes = 0
        local spots_at, sounds_at = {}, {}
        for _, s in ipairs(sr == "ok" and spots or {}) do
            spots_at[s.tick] = spots_at[s.tick] or {}
            table.insert(spots_at[s.tick], s)
        end
        for _, s in ipairs(nr == "ok" and sounds or {}) do
            sounds_at[s.tick] = sounds_at[s.tick] or {}
            sounds_at[s.tick][s.sound] = true
        end
        local seen = { [BURST] = 0, [BOULDER] = 0 }
        local bad = {}
        for _, p in ipairs(pr == "ok" and projs or {}) do
            if p.spotanim == BURST or p.spotanim == BOULDER then
                seen[p.spotanim] = seen[p.spotanim] + 1
                if swung[p.tick] then
                    clashes = clashes + 1
                    if last_seq[p.tick] ~= 65 then
                        bad[#bad + 1] = string.format("t%d: punched as he cast, sent seq %s, not demon_block", p.tick, tostring(last_seq[p.tick]))
                    end
                elseif last_seq[p.tick] ~= 69 then
                    bad[#bad + 1] = string.format("t%d: his cast sent seq %s, not demon_casting", p.tick, tostring(last_seq[p.tick]))
                end
                local L = p.tick + p.end_cycle // 30
                local tx, tz = coord_of(p.dst)
                if p.start_cycle ~= 51 or p.end_cycle % 30 ~= 0 then
                    bad[#bad + 1] = string.format("t%d %d start %d end %d", p.tick, p.spotanim, p.start_cycle, p.end_cycle)
                end
                local here = spots_at[L] or {}
                if p.spotanim == BURST then
                    local delays = {}
                    for _, s in ipairs(here) do
                        local sx, sz = coord_of(s.coord)
                        if s.spotanim == 363 and math.abs(sx - tx) <= 1 and math.abs(sz - tz) <= 1 and s.height == 10 then
                            delays[#delays + 1] = s.delay
                        end
                    end
                    table.sort(delays)
                    if table.concat(delays, ",") ~= "0,3,3,6,6,9,9,12,12" then
                        bad[#bad + 1] = string.format("t%d burst impacts %s", p.tick, table.concat(delays, ","))
                    end
                    if not (sounds_at[p.tick] or {})[171] or not (sounds_at[L] or {})[170] then
                        bad[#bad + 1] = string.format("t%d burst sounds", p.tick)
                    end
                else
                    local hit = false
                    for _, s in ipairs(here) do
                        local sx, sz = coord_of(s.coord)
                        if s.spotanim == 1325 and sx == tx and sz == tz and s.height == 10 and s.delay == 0 then hit = true end
                    end
                    if not hit then bad[#bad + 1] = string.format("t%d boulder impact", p.tick) end
                    if not (sounds_at[p.tick] or {})[1440] or not (sounds_at[L] or {})[1021] then
                        bad[#bad + 1] = string.format("t%d boulder sounds", p.tick)
                    end
                end
            end
        end
        t.check("tells.both_styles", seen[BURST] > 0 and seen[BOULDER] > 0,
            string.format("bursts %d, boulders %d (punched as he cast: %d)", seen[BURST], seen[BOULDER], clashes))
        t.check("tells.recorded", #bad == 0 and seen[BURST] + seen[BOULDER] > 0,
            #bad == 0 and "every attack as recorded" or table.concat(bad, "; "))
        t.finish((#bad == 0 and seen[BURST] > 0 and seen[BOULDER] > 0) and 0 or 1)
    end,
}
