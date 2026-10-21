-- tfa_cheat: `::tfa` as the other raids' cheats -- gearup (RSProx 4308's
-- Gnomonkey), lobby, the chat menu into the Archive, out, `::tfa 5` straight
-- to Xarth, and the menu's second page to Dreadnoth.
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_cheat.lua --bots 1 \
--       --name tfa0 --ticks 9000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function in_lobby(t)
    local r, tile = t.world.tile()
    return r == "ok" and tile.level == 0 and tile.x >= 4096 and tile.x < 4160 and tile.z >= 5504 and tile.z < 5568
end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

return {
    id = "tfa_cheat",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = { "::clearinv" },
    run = function(t)
        t.ticks(2)
        t.cheat("::tfa gearup", false)
        t.ticks(2)
        local _, worn = t.inv.count_in("worn", "zaryte_xbow")
        t.check("gearup.worn", worn == 1, "zaryte_xbow worn " .. tostring(worn))
        local _, oblig = t.inv.count("obligator")
        local _, sara = t.inv.count("4dosepotionofsaradomin")
        t.check("gearup.pack", oblig == 1 and sara == 8, "obligator " .. tostring(oblig) .. ", brews " .. tostring(sara))
        local _, hp = t.skill.read("hitpoints")
        t.check("gearup.stats", type(hp) == "table" and hp.base_level == 99, "hitpoints " .. tostring(hp and hp.base_level))

        t.cheat("::tfa lobby", false)
        local ok = t.await({ level = function() return in_lobby(t) end, note = "the lobby" }, 10)
        t.check("lobby", ok == "ok", "not in the lobby")

        -- The menu, page one: the Archive.
        t.cheat("::tfa", false)
        t.ticks(2)
        t.check("menu.archive", t.chat.choose("The Archive (the Guardian)") == "ok", "no menu")
        ok = t.await({ level = function() return present(t, "tfa_guardian_start") end, note = "the Guardian" }, 20)
        t.check("archive", ok == "ok", "not in the Archive")
        t.ticks(4)
        t.cheat("::tfa lobby", false)
        ok = t.await({ level = function() return in_lobby(t) end, note = "the lobby again" }, 10)
        t.check("lobby.out", ok == "ok", "not back in the lobby")

        t.cheat("::tfa 5", false)
        ok = t.await({ level = function() return present(t, "tfa_xarth") end, note = "Xarth" }, 40)
        t.check("room.xarth", ok == "ok", "not at Xarth")
        t.ticks(4)
        t.cheat("::tfa lobby", false)
        ok = t.await({ level = function() return in_lobby(t) end, note = "the lobby, from Xarth" }, 10)
        t.check("lobby.from_room", ok == "ok", "not back in the lobby")
        -- The menu, page two: Dreadnoth.
        t.cheat("::tfa", false)
        t.ticks(2)
        t.check("menu.more", t.chat.choose("More...") == "ok", "no menu")
        t.ticks(1)
        t.check("menu.page2", t.chat.choose("Dreadnoth") == "ok", "no second page")
        ok = t.await({ level = function() return present(t, "tfa_jelly_boss") end, note = "Dreadnoth" }, 40)
        t.check("menu.dreadnoth", ok == "ok", "not at Dreadnoth")
        t.ticks(4)
        t.cheat("::tfa lobby", false)
        ok = t.await({ level = function() return in_lobby(t) end, note = "the lobby, from Dreadnoth" }, 10)
        t.check("lobby.from_menu_room", ok == "ok", "not back in the lobby")
        t.expect("tfa_cheat.run", "ok", "done")
    end,
}
