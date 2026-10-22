-- tfa_dreadnoth_party: Dreadnoth the Jellied with three raiders (--bots 3),
-- for the tick log to diff against RSProx's party fights (index 4301..4316;
-- docs/rsprox-analysis/guthixian_archive/dreadnoth_plan.md §2..§7): his
-- target switches, his crawl at his target and his swing at everyone beside
-- him, a blob seed on each raider's tile, each jelly after the raider nearest
-- it, the enrage's leaps onto his target and the turn every third landing.
--
--   seat 1 the solo test's way in, ::tfaroom straight to Dreadnoth, ::tfahost;
--   seats 2 and 3 ::tfajoin (the lobby has no join yet); all three over the
--   wall by the transporter
--   seat 1 shoots him from where it stands; seat 2 walks a ring round the
--   arena; seat 3 stands in a far corner and moves to another now and then
--   seat 1 takes him through the phases with ::tfaboss, as the solo test does
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_dreadnoth_party.lua --bots 3 \
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

-- Seat 2's ring, seat 3's corners: tiles of the room's square (his spawn's
-- south-west is local 29,29; his arena 24..38 each way).
local RING = { { 25, 25 }, { 37, 25 }, { 37, 37 }, { 25, 37 } }
local CORNERS = { { 37, 37 }, { 25, 37 }, { 37, 25 } }

local function base_of(t)
    local _, tile = t.world.tile()
    return tile.x - tile.x % 64, tile.z - tile.z % 64
end

local function fight(t, role, ticks, stop)
    local deadline = now(t) + ticks
    local bx, bz = base_of(t)
    local leg = 0
    while now(t) < deadline do
        t.cheat("::tfaheal", false)
        if stop and stop() then return true end
        local k = now(t)
        if role == 2 and k % 10 == 0 then
            leg = leg % #RING + 1
            t.player.walk_to(bx + RING[leg][1], bz + RING[leg][2], 4)
        elseif role == 3 and k % 40 == 0 then
            leg = leg % #CORNERS + 1
            t.player.walk_to(bx + CORNERS[leg][1], bz + CORNERS[leg][2], 4)
        elseif role == 1 then
            if present(t, "tfa_jelly_minion") then
                t.player.attack("tfa_jelly_minion", 2, 1, EAT)
            elseif present(t, "tfa_jelly_boss") then
                t.player.attack("tfa_jelly_boss", 2, 1, EAT)
            elseif not stop then
                return true
            end
        end
        t.ticks(1)
    end
    return false
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
        t.cheat("::tfaroom 4 2", false)
        ok = t.await({ level = function() return present(t, "tfa_jelly_boss") end, note = "dreadnoth" }, 30)
        t.check("dreadnoth.room", ok == "ok", "no Dreadnoth")
        t.cheat("::tfahost", false)
    end
    local br, bd = t.party.barrier("dn_built", 600)
    t.check("party.built", br == "ok", tostring(bd))
    if role ~= 1 then
        t.cheat("::tfajoin", false)
        local ok = t.await({ level = function() return present(t, "tfa_jelly_boss") end, note = "joined" }, 30)
        t.check("party.joined", ok == "ok", "no Dreadnoth")
    end
    br, bd = t.party.barrier("dn_in", 300)
    t.check("party.in", br == "ok", tostring(bd))
    t.ticks(4 + role)
    if role == 1 then t.ticklog.mark("transporter") end
    local r, d = t.player.click_loc("tfa_teleporter_jelly", 1)
    t.check("dreadnoth.transporter", r == "ok", tostring(r) .. " " .. tostring(d))
    t.ticks(4)
    t.prayer.set("protectfrommelee", true)
    if role == 1 then t.ticklog.mark("fight") end
    fight(t, role, 60)
    for _, stage in ipairs({ { "phase1", 1780, 60 }, { "phase2", 1360, 70 }, { "phase3", 940, 90 } }) do
        if role == 1 then
            t.ticklog.mark(stage[1])
            t.cheat("::tfaboss " .. stage[2], false)
        end
        fight(t, role, stage[3])
        if role == 1 then t.cheat("::tfadnjellies", false) end
    end
    if role == 1 then
        t.ticklog.mark("enrage")
        t.cheat("::tfaboss 520", false)
    end
    t.prayer.set("protectfrommissiles", true)
    fight(t, role, 80)
    if role == 1 then
        t.ticklog.mark("kill")
        t.cheat("::tfaboss 3", false)
    end
    -- the seven out of him swing for a while, then go (a bow is slow on them)
    local done = function() return t.msg.expect("Challenge complete: Dreadnoth the Jellied.") == "ok" end
    local dead = function() return not present(t, "tfa_jelly_boss") end
    t.check("dreadnoth.dead", fight(t, role, 300, dead), "still alive")
    fight(t, role, 30, done)
    if role == 1 then t.cheat("::tfadnjellies", false) end
    t.check("dreadnoth.line", fight(t, role, 60, done), "no line")
    if role == 1 then t.ticklog.mark("end") end
    t.party.barrier("dn_end", 120)
    t.finish(0)
end

return {
    id = "tfa_dreadnoth_party",
    fixture = "fresh_lumbridge.ini",
    party = 3,
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
        "::goto 4128 5556 0",
    },
    run = run,
}
