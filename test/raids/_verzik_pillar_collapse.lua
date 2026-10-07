-- _verzik_pillar_collapse: a raider beside a falling Verzik P1 pillar takes the
-- collapse's damage and stun, and is NOT knocked flat.  anim audit 2026-10-07.
--
-- The owner saw a raider play "a full death animation" in P1 and come back.  It
-- was the pillar collapse: [proc,tob_verzik_collapse_pillar] threw everyone in
-- range two tiles back with human_troll_flyback 1157 (flat on the back, up
-- again).  No source puts a knockback on a pillar: the collapse is damage (wiki
-- Perfect_Verzik:19; guide bCkpMm0ZDHE 7:06 "collapse and cause huge damage"),
-- and 1157 is sourced only on the P2 bounce (tobmistaketracker
-- VerzikP2MistakeDetector.java:38).  OSRS-Content 8b927584d8 keeps the damage
-- and the stun (human_stunned 848 + spotanim 245 stunned_thieving, blert
-- VerzikDataTracker.java:56) and drops the knockback.
--
-- FORCED: the fight is never begun.  ::tobwarp stands the raider on the tile
-- west of pillar 0 (its 3x3 is local 25..27,18..20, the collapse sweeps 2 tiles
-- from its centre 26,19), and ::tobvzpillar 0 runs the fight's own collapse
-- proc on that pillar.  Checked from the tick log, from the collapse onward:
--   pillar.collapsed   the pillar retyped 8379 -> 8377
--   pillar.damage      a hit_player on the raider, damage > 0
--   pillar.stun        player_anim 848 and player_spotanim 245
--   pillar.no_flyback  no player_anim 1157
--   pillar.in_place    every player_tile row is the tile it stood on
--   pillar.alive       hitpoints above 0, t.player.alive, no human_death 836
-- Shots: one a tick from the collapse, camera over the pillar.
local LX, LZ = 24, 19

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
    max_frames = 60000,
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
        t.cheat(string.format("::tobwarp %d %d", LX, LZ), true)
        t.ticks(3)
        local _, before = t.ticklog.mark("pillar before")
        local s0 = serial_of(before)
        t.ticks(1)
        local stood = nil
        for _, row in ipairs(rows_since(t, "player_tile", 0)) do stood = row end
        t.check("pillar.stood", stood ~= nil,
            stood and string.format("raider pid %s at %s,%s level %s before the collapse (warp local %d,%d)",
                tostring(stood.pid), tostring(stood.x), tostring(stood.z), tostring(stood.level), LX, LZ)
            or "no player_tile row")
        local _, mark = t.ticklog.mark("pillar collapse")
        local s1 = serial_of(mark)
        t.cheat("::tobvzpillar 0", true)
        for i = 0, 5 do
            t.drive.camera(0, 383, 1100)
            t.shot(string.format("pillar.collapse.t%d", i))
            t.ticks(1)
        end
        t.ticks(4)

        local retyped = 0
        for _, row in ipairs(rows_since(t, "npc_retype", s1)) do
            if row.from_type == 8379 and row.to_type == 8377 then retyped = retyped + 1 end
        end
        t.check("pillar.collapsed", retyped >= 1, retyped .. " pillar(s) retyped 8379 -> 8377 after ::tobvzpillar 0")

        local pid = stood and stood.pid
        local dmg, hits = 0, 0
        for _, row in ipairs(rows_since(t, "hit_player", s1)) do
            if pid == nil or row.pid == pid then hits = hits + 1; dmg = dmg + (row.damage or 0) end
        end
        t.check("pillar.damage", dmg > 0, string.format("%d hit(s), %d damage on the raider from the collapse (max 70)", hits, dmg))

        local seqs, spots = {}, {}
        for _, row in ipairs(rows_since(t, "player_anim", s1)) do
            if pid == nil or row.pid == pid then seqs[row.seq] = (seqs[row.seq] or 0) + 1 end
        end
        for _, row in ipairs(rows_since(t, "player_spotanim", s1)) do
            if pid == nil or row.pid == pid then spots[row.spotanim] = (spots[row.spotanim] or 0) + 1 end
        end
        local function list(m)
            local out = {}
            for k, v in pairs(m) do out[#out + 1] = tostring(k) .. "x" .. tostring(v) end
            table.sort(out)
            return #out > 0 and table.concat(out, " ") or "none"
        end
        t.check("pillar.stun", (seqs[848] or 0) > 0 and (spots[245] or 0) > 0,
            "player_anim " .. list(seqs) .. "; player_spotanim " .. list(spots) .. " (want 848 and 245)")
        t.check("pillar.no_flyback", (seqs[1157] or 0) == 0,
            "player_anim 1157 human_troll_flyback x" .. tostring(seqs[1157] or 0) .. " (want none)")

        local moved = {}
        for _, row in ipairs(rows_since(t, "player_tile", s1)) do
            if stood and (pid == nil or row.pid == pid) and (row.x ~= stood.x or row.z ~= stood.z or row.level ~= stood.level) then
                moved[#moved + 1] = string.format("t%s %s,%s", tostring(row.tick), tostring(row.x), tostring(row.z))
            end
        end
        t.check("pillar.in_place", stood ~= nil and #moved == 0,
            #moved == 0 and "the raider never left its tile (no knockback)" or ("moved: " .. table.concat(moved, "; ")))

        local _, hp = t.skill.read("hitpoints")
        local alive_r, alive_d = t.player.alive()
        local died = (seqs[836] or 0) > 0
        t.check("pillar.alive", alive_r == "ok" and hp ~= nil and (hp.level or 0) > 0 and not died,
            string.format("hitpoints %s, alive %s (%s), human_death x%d",
                tostring(hp and hp.level), tostring(alive_r), tostring(alive_d), seqs[836] or 0))
        return "ok"
    end,
}
