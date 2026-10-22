-- tfa_ezekiel_under: Ezekiel with two raiders (--bots 2), the second standing
-- under his 3x3, for the hits a raider there takes (ezekiel_plan.md §4, §5;
-- docs/rsprox-analysis/guthixian_archive/tools/ez_residual_hits.py):
--
--   seat 1 the solo test's way in (::tfaroom straight to Ezekiel), ::tfahost,
--   through the transporter first (the tank), and shoots him from range
--   seat 2 ::tfajoin, through the transporter after, and walks onto his
--   middle tile every tick
--   checks, on the tick log, every raider under him at the end of the tick
--   before: his melee hits them (4300..4320: 7/8), the star's second beat
--   (S+5) hits them (3/3), its first (S+3) does not (never recorded)
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_ezekiel_under.lua --bots 2 \
--       --name tfa0 --ticks 9000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function in_lobby(t)
    local r, tile = t.world.tile()
    return r == "ok" and tile.x >= 4096 and tile.x < 4160 and tile.z >= 5504 and tile.z < 5568
end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

local EAT = { eat = { item = "shark", below = 50 } }

-- Seat 1 shoots; seat 2 steps onto his middle tile. Both topped up each tick.
local function fight(t, role, ticks)
    local deadline = now(t) + ticks
    while now(t) < deadline do
        t.cheat("::tfaheal", false)
        if not present(t, "tfa_ezekiel") then return end
        if role == 1 then
            t.player.attack("tfa_ezekiel", 2, 1, EAT)
        else
            local _, _, bosses = t.npc.tiles("tfa_ezekiel", 0)
            local boss = bosses and bosses[1]
            local r, here = t.world.tile()
            if boss and r == "ok" and (here.x ~= boss.x + 1 or here.z ~= boss.z + 1) then
                t.player.walk_to(boss.x + 1, boss.z + 1, 1)
            end
        end
        t.ticks(1)
    end
end

-- The tick log's rows by tick: each raider's tile and Ezekiel's south-west
-- tile at the end of a tick, his seqs, the hits.
local function under_hits(t, from, upto)
    local _, tiles = t.ticklog.rows({ kind = "player_tile" })
    local _, ntiles = t.ticklog.rows({ kind = "npc_tile" })
    local _, spawns = t.ticklog.rows({ kind = "npc_spawn" })
    local _, anims = t.ticklog.rows({ kind = "npc_anim" })
    local _, hits = t.ticklog.rows({ kind = "hit_player" })
    local slot
    for _, s in ipairs(spawns or {}) do
        if s.type == 16652 then slot = s.slot end
    end
    local pt, boss = {}, {}
    for _, r in ipairs(tiles or {}) do
        pt[r.pid] = pt[r.pid] or {}
        pt[r.pid][r.tick] = { x = r.x, z = r.z }
    end
    for _, r in ipairs(ntiles or {}) do
        if r.slot == slot then boss[r.tick] = { x = r.x, z = r.z } end
    end
    local function at(m, tick)
        local best, bt
        for k, v in pairs(m) do
            if k <= tick and (not bt or k > bt) then best, bt = v, k end
        end
        return best
    end
    -- A sword's burn (hitsplat 67) is its own mechanic, on its own clock.
    local hit = {}
    for _, h in ipairs(hits or {}) do
        if h.hitsplat ~= 67 then hit[h.pid .. ":" .. h.tick] = true end
    end
    -- seq -> the hit ticks it judges: melee its own, the special S+3 and S+5.
    local seen = { melee = { 0, 0 }, beat1 = { 0, 0 }, beat2 = { 0, 0 } }
    for _, a in ipairs(anims or {}) do
        if a.slot == slot and a.tick >= from then
            local judged = {}
            if a.seq == 14685 then judged = { { a.tick, "melee" } } end
            if a.seq == 14687 then judged = { { a.tick + 3, "beat1" }, { a.tick + 5, "beat2" } } end
            for _, j in ipairs(judged) do
                local tick, what = j[1], j[2]
                -- A beat due after the log's last tick is not judged yet.
                local b = tick < upto and at(boss, tick - 1)
                for pid, m in pairs(pt) do
                    local tl = at(m, tick - 1)
                    if b and tl and tl.x >= b.x and tl.x <= b.x + 2 and tl.z >= b.z and tl.z <= b.z + 2 then
                        seen[what][1] = seen[what][1] + 1
                        if hit[pid .. ":" .. tick] then seen[what][2] = seen[what][2] + 1 end
                    end
                end
            end
        end
    end
    return seen
end

local function run(t)
    local role = t.party.role()
    t.player._death_fence = function() return false end
    if role == 1 then
        t.check("party.ticklog", t.ticklog.start() == "ok", "")
    end
    t.ticks(2)
    if role == 1 then
        t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        t.chat.choose("Form or join a party.")
        t.check("partylist.open", t.ui.await_open("tfa_partylist", 10))
        t.ticks(1)
        t.ui.press_pause("tfa_partylist:contents", 1)
        t.check("partydetails.open", t.ui.await_open("tfa_partydetails", 10))
        t.ticks(1)
        t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        local ok = t.await({ level = function() return not in_lobby(t) end, note = "into the room" }, 20)
        t.check("room.enter", ok == "ok", "still in the lobby")
        t.ticks(10)
        t.cheat("::tfaroom 2 1", false)
        ok = t.await({ level = function() return present(t, "tfa_ezekiel") end, note = "ezekiel" }, 30)
        t.check("ezekiel.room", ok == "ok", "no Ezekiel")
        t.cheat("::tfahost", false)
    end
    local br, bd = t.party.barrier("ez_built", 600)
    t.check("party.built", br == "ok", tostring(bd))
    if role ~= 1 then
        t.cheat("::tfajoin", false)
        local ok = t.await({ level = function() return present(t, "tfa_ezekiel") end, note = "joined" }, 30)
        t.check("party.joined", ok == "ok", "no Ezekiel")
    end
    br, bd = t.party.barrier("ez_in", 300)
    t.check("party.in", br == "ok", tostring(bd))
    t.ticks(2)
    -- Seat 1 over the wall first: the tank (8/8).
    if role ~= 1 then t.ticks(4) end
    local r, d = t.player.click_loc("tfa_boss_teleporter", 1)
    t.check("ezekiel.transporter", r == "ok", tostring(r) .. " " .. tostring(d))
    t.ticks(4)
    local from = now(t)
    if role == 1 then t.ticklog.mark("under") end
    fight(t, role, 150)
    t.party.barrier("ez_fought", 120)
    if role == 1 then
        t.ticks(2)
        local seen = under_hits(t, from, now(t))
        local function show(k) return string.format("%s %d/%d", k, seen[k][2], seen[k][1]) end
        t.note("under him: " .. show("melee") .. ", " .. show("beat1") .. ", " .. show("beat2"))
        t.check("ezekiel.under.melee", seen.melee[1] > 0 and seen.melee[2] == seen.melee[1],
            show("melee") .. " hit (want every one)")
        t.check("ezekiel.under.star2", seen.beat2[1] > 0 and seen.beat2[2] == seen.beat2[1],
            show("beat2") .. " hit (want every one)")
        t.check("ezekiel.under.star1", seen.beat1[2] == 0, show("beat1") .. " hit (want none)")
        t.ticklog.mark("end")
    end
    t.party.barrier("ez_end", 120)
    t.finish(0)
end

return {
    id = "tfa_ezekiel_under",
    fixture = "fresh_lumbridge.ini",
    party = 2,
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setvar varb16426_tfa_miniquest 2",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel prayer 99",
        "::give bow_of_faerdhinen 1",
        "::wield bow_of_faerdhinen",
        "::give black_dragonhide_body 1",
        "::wield black_dragonhide_body",
        "::give shark 20",
        "::maxrange",
        "::goto 4128 5556 0",
    },
    run = run,
}
