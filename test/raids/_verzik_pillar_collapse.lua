-- _verzik_pillar_collapse: a raider within 2 of a falling Verzik P1 pillar's
-- centre is thrown ONE tile away from it, then hit; one 3 out is untouched.
--
-- RSProx, 34 throws over 9 recordings with every raider shown
-- (docs/rsprox-analysis/tob/verzik.md, "Getting thrown"; T = the collapse,
-- 8377 + seq 8052): on T+2 the raider plays agilityarena_player_spikedback 1114
-- (NOT 1157, no 848, no spotanim 245, no stun) and moves one tile in an
-- exactmove; the hit lands on T+3. The heading is straight out along the longer
-- axis, and a corner raider goes to the side counterclockwise of the diagonal:
-- NE east, NW north, SW west, SE south. Nobody 3 out was thrown or hit.
-- (Before: 1157 + the 848/245 stun, a diagonal for a corner, range 3 - from
-- the owner ruling 2026-10-07 and four blert rows.)
--
-- FORCED: the fight is never begun.  ::tobwarp stands the raider beside a
-- pillar and ::tobvzpillar <i> runs the fight's own ~tob_verzik_collapse_pillar
-- on it. Cases (pillar centres: 0 26,19  1 26,25  3 38,19  4 38,25  5 38,31):
--   west: 2 west of pillar 0           -> one tile west
--   ne:   2 east, 2 north of pillar 1  -> one tile EAST (the corner rule)
--   east: 2 east of pillar 3           -> one tile east
--   se:   2 east, 2 south of pillar 4  -> one tile SOUTH (the corner rule)
--   far:  3 east of pillar 5           -> nothing: no anim, no move, no hit
-- Rows per case, T = the tick the pillar retyped 8379 -> 8377:
--   .collapsed  the retype
--   .anim       player_anim 1114 once, on T+2 (none for far); never 1157 or 848
--   .nostun     no player_spotanim 245
--   .shove      exactly one tile change, on T+2, onto the expected tile (none
--               for far)
--   .damage     one hit_player > 0 on T+3 (none for far)
--   .alive      hitpoints above 0, t.player.alive, no human_death 836
-- Shots: one a tick from the collapse, camera over the pillar.
local BASE_X, BASE_Z = 6400, 64   -- local (0,0) of the instance in this run (24,19 -> 6424,83)

local CASES = {
    { name = "west", pillar = 0, centre = { 26, 19 }, stand = { 24, 19 }, want = { 23, 19 } },
    { name = "ne",   pillar = 1, centre = { 26, 25 }, stand = { 28, 27 }, want = { 29, 27 } },
    { name = "east", pillar = 3, centre = { 38, 19 }, stand = { 40, 19 }, want = { 41, 19 } },
    { name = "se",   pillar = 4, centre = { 38, 25 }, stand = { 40, 23 }, want = { 40, 22 } },
    { name = "far",  pillar = 5, centre = { 38, 31 }, stand = { 41, 31 }, want = nil },
}

local function serial_of(detail)
    return tonumber(string.match(tostring(detail), "serial (%d+)"))
end

local function rows_since(t, kind, since)
    local ok, list = t.ticklog.rows({ kind = kind, since = since })
    if ok ~= "ok" or type(list) ~= "table" then return {} end
    return list
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
            local spiked, wrong, s245, died = {}, 0, 0, 0
            for _, a in ipairs(res.seqs) do
                if a.seq == 1114 then spiked[#spiked + 1] = "T+" .. (a.tick - T) end
                if a.seq == 1157 or a.seq == 848 then wrong = wrong + 1 end
                if a.seq == 836 then died = died + 1 end
            end
            for _, a in ipairs(res.spots) do if a.spot == 245 then s245 = s245 + 1 end end
            if c.want then
                t.check(p .. ".anim", #spiked == 1 and spiked[1] == "T+2" and wrong == 0,
                    "agilityarena_player_spikedback 1114 at " .. (#spiked > 0 and table.concat(spiked, ",") or "never")
                        .. ", 1157/848 x" .. wrong .. " (want 1114 once on T+2, no 1157/848)")
            else
                t.check(p .. ".anim", #spiked == 0 and wrong == 0,
                    "1114 at " .. (#spiked > 0 and table.concat(spiked, ",") or "never") .. ", 1157/848 x" .. wrong .. " (want none: 3 out)")
            end
            t.check(p .. ".nostun", s245 == 0, "spotanim 245 x" .. s245 .. " (want none)")
            -- every distinct tile after the collapse, and the tick it was first seen
            local changes, last = {}, { x = c.stand[1], z = c.stand[2] }
            for _, tl in ipairs(res.tiles) do
                if tl.x ~= last.x or tl.z ~= last.z then
                    changes[#changes + 1] = string.format("T+%d %d,%d", tl.tick - T, tl.x, tl.z)
                    last = tl
                end
            end
            local moves = #changes > 0 and table.concat(changes, "; ") or "none"
            if c.want then
                local onto = #changes == 1 and last.x == c.want[1] and last.z == c.want[2]
                local when = changes[1] and string.match(changes[1], "^T%+(%-?%d+)")
                t.check(p .. ".shove", onto and when == "2",
                    string.format("stood %d,%d (centre %d,%d); moves: %s; want one move on T+2 onto %d,%d",
                        c.stand[1], c.stand[2], c.centre[1], c.centre[2], moves, c.want[1], c.want[2]))
            else
                t.check(p .. ".shove", #changes == 0,
                    string.format("stood %d,%d (centre %d,%d); moves: %s; want none", c.stand[1], c.stand[2], c.centre[1], c.centre[2], moves))
            end
            local hit = {}
            local dmg = 0
            for _, h in ipairs(res.hits) do hit[#hit + 1] = h.dmg .. "@T+" .. (h.tick - T); dmg = dmg + h.dmg end
            if c.want then
                t.check(p .. ".damage", #res.hits == 1 and dmg > 0 and (res.hits[1].tick - T) == 3,
                    "hits " .. (#hit > 0 and table.concat(hit, ", ") or "none") .. " (want one, > 0, on T+3; NR roll 32-65)")
            else
                t.check(p .. ".damage", #res.hits == 0, "hits " .. (#hit > 0 and table.concat(hit, ", ") or "none") .. " (want none: 3 out)")
            end
            t.check(p .. ".alive", res.alive == "ok" and (res.hp or 0) > 0 and died == 0,
                "hitpoints " .. tostring(res.hp) .. ", alive " .. tostring(res.alive) .. ", human_death x" .. died)
        end
        return "ok"
    end,
}
