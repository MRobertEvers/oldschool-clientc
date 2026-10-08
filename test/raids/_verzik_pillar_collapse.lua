-- _verzik_pillar_collapse: a raider beside a falling Verzik P1 pillar is thrown
-- ONE tile directly away from it with the flyback, then hit.  anim audit
-- 2026-10-07.
--
-- Owner ruling 2026-10-07: "The collapsing pillar DOES knock 1 tile away from
-- the pillar and has the animation."  blert agrees tick for tick (coordinator's
-- harvest, T = the pillar 8379's NPC_DEATH, centre = SW + 1,1): 280f7cef T108
-- 3 east -> 1 east at T+2; 45e4e8b6 T108 2 west -> 1 west at T+2; 45e4e8b6
-- T117 2 east -> 1 east at T+2; 6f735fbc T66 2 south -> 1 south at T+2; damage
-- on T+3 in three of four.  The content before this (a8e1d1e517) had no shove
-- at all; before that (8b927584d8's parent) it threw two tiles, always
-- south-west.
--
-- FORCED: the fight is never begun.  ::tobwarp stands the raider beside a
-- pillar and ::tobvzpillar <i> runs the fight's own ~tob_verzik_collapse_pillar
-- on it.  Two cases, each checked from the tick log:
--   west: pillar 0 (3x3 at local 25..27,18..20, centre 26,19), raider at 24,19
--         (2 west)  -> must end on 23,19
--   diag: pillar 1 (centre 26,25), raider at 28,27 (2 east, 2 north) -> 29,28
--   east: pillar 3 (3x3 at 37..39,18..20, centre 38,19), raider at 40,19
--         (2 east) -> 41,19.  West and east together catch a heading taken
--         from ~coord_direction2, which answers south-west for due west.
-- Rows per case, T = the tick the pillar retyped 8379 -> 8377:
--   .collapsed  the retype
--   .flyback    player_anim 1157 on T+2
--   .stun       player_anim 848 and player_spotanim 245
--   .shove      exactly one tile change, on T+2, onto the expected tile, which
--               is one tile from where it stood and farther from the centre
--   .damage     a hit_player > 0 on T+3
--   .alive      hitpoints above 0, t.player.alive, no human_death 836
-- Shots: one a tick from the collapse, camera over the pillar.
local BASE_X, BASE_Z = 6400, 64   -- local (0,0) of the instance in this run (24,19 -> 6424,83)

local CASES = {
    { name = "west", pillar = 0, centre = { 26, 19 }, stand = { 24, 19 }, want = { 23, 19 } },
    { name = "diag", pillar = 1, centre = { 26, 25 }, stand = { 28, 27 }, want = { 29, 28 } },
    { name = "east", pillar = 3, centre = { 38, 19 }, stand = { 40, 19 }, want = { 41, 19 } },
}

local function serial_of(detail)
    return tonumber(string.match(tostring(detail), "serial (%d+)"))
end

local function rows_since(t, kind, since)
    local ok, list = t.ticklog.rows({ kind = kind, since = since })
    if ok ~= "ok" or type(list) ~= "table" then return {} end
    return list
end

local function cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

return {
    id = "_verzik_pillar_collapse",
    fixture = "fresh_lumbridge.ini",
    max_frames = 110000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel magic 99",
    },
    run = function(t)
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("pillar.ticklog", tl_ok == "ok", tostring(tl_detail))
        local r, d = t.raid.enter("tob", "verzik", { mode = "normal" })
        t.check("pillar.enter", r == "ok", tostring(d))
        local results = {}
        for _, c in ipairs(CASES) do
            -- full hitpoints for each case: two 32-65 rolls could otherwise kill
            t.cheat("::setlevel hitpoints 99", true)
            t.cheat(string.format("::tobwarp %d %d", c.stand[1], c.stand[2]), true)
            t.ticks(3)
            local _, mark = t.ticklog.mark("pillar " .. c.name)
            local s1 = serial_of(mark)
            t.cheat(string.format("::tobvzpillar %d", c.pillar), true)
            for i = 0, 5 do
                t.drive.camera(0, 383, 1100)
                t.shot(string.format("pillar.%s.t%d", c.name, i))
                t.ticks(1)
            end
            t.ticks(4)
            local res = { c = c }
            for _, row in ipairs(rows_since(t, "npc_retype", s1)) do
                if row.from_type == 8379 and row.to_type == 8377 and res.T == nil then res.T = row.tick end
            end
            res.seqs, res.spots, res.hits, res.tiles = {}, {}, {}, {}
            for _, row in ipairs(rows_since(t, "player_anim", s1)) do
                res.seqs[#res.seqs + 1] = { seq = row.seq, tick = row.tick }
            end
            for _, row in ipairs(rows_since(t, "player_spotanim", s1)) do
                res.spots[#res.spots + 1] = { spot = row.spotanim, tick = row.tick }
            end
            for _, row in ipairs(rows_since(t, "hit_player", s1)) do
                res.hits[#res.hits + 1] = { dmg = row.damage or 0, tick = row.tick }
            end
            for _, row in ipairs(rows_since(t, "player_tile", s1)) do
                res.tiles[#res.tiles + 1] = { x = row.x - BASE_X, z = row.z - BASE_Z, tick = row.tick }
            end
            local _, hp = t.skill.read("hitpoints")
            res.hp = hp and hp.level
            res.alive = t.player.alive()
            results[#results + 1] = res
            t.ticks(8)
        end

        for _, res in ipairs(results) do
            local c, T = res.c, res.T
            local p = "pillar." .. c.name
            t.check(p .. ".collapsed", T ~= nil, "pillar " .. c.pillar .. " retyped 8379 -> 8377 at T=" .. tostring(T))
            T = T or -100
            local fly, s848, s245, died = {}, 0, 0, 0
            for _, a in ipairs(res.seqs) do
                if a.seq == 1157 then fly[#fly + 1] = "T+" .. (a.tick - T) end
                if a.seq == 848 then s848 = s848 + 1 end
                if a.seq == 836 then died = died + 1 end
            end
            for _, a in ipairs(res.spots) do if a.spot == 245 then s245 = s245 + 1 end end
            t.check(p .. ".flyback", #fly == 1 and fly[1] == "T+2",
                "human_troll_flyback 1157 at " .. (#fly > 0 and table.concat(fly, ",") or "never") .. " (want once, T+2)")
            t.check(p .. ".stun", s848 > 0 and s245 > 0, "848 x" .. s848 .. ", spotanim 245 x" .. s245)
            -- every distinct tile after the collapse, and the tick it was first seen
            local changes, last = {}, { x = c.stand[1], z = c.stand[2] }
            for _, tl in ipairs(res.tiles) do
                if tl.x ~= last.x or tl.z ~= last.z then
                    changes[#changes + 1] = string.format("T+%d %d,%d", tl.tick - T, tl.x, tl.z)
                    last = tl
                end
            end
            local onto = #changes == 1 and last.x == c.want[1] and last.z == c.want[2]
            local one = cheb(last.x, last.z, c.stand[1], c.stand[2]) == 1
            local farther = cheb(last.x, last.z, c.centre[1], c.centre[2]) > cheb(c.stand[1], c.stand[2], c.centre[1], c.centre[2])
            local when = changes[1] and string.match(changes[1], "^T%+(%-?%d+)")
            t.check(p .. ".shove", onto and one and farther and when == "2",
                string.format("stood %d,%d (centre %d,%d); moves: %s; want one move on T+2 onto %d,%d",
                    c.stand[1], c.stand[2], c.centre[1], c.centre[2],
                    #changes > 0 and table.concat(changes, "; ") or "none", c.want[1], c.want[2]))
            local hit = {}
            local dmg = 0
            for _, h in ipairs(res.hits) do hit[#hit + 1] = h.dmg .. "@T+" .. (h.tick - T); dmg = dmg + h.dmg end
            t.check(p .. ".damage", #res.hits == 1 and dmg > 0 and (res.hits[1].tick - T) == 3,
                "hits " .. (#hit > 0 and table.concat(hit, ", ") or "none") .. " (want one, > 0, on T+3; NR roll 32-65)")
            t.check(p .. ".alive", res.alive == "ok" and (res.hp or 0) > 0 and died == 0,
                "hitpoints " .. tostring(res.hp) .. ", alive " .. tostring(res.alive) .. ", human_death x" .. died)
        end
        return "ok"
    end,
}
