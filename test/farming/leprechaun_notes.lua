-- Produce used on a tool leprechaun, as RSProx 4427 / 4422 / 4419 record it
-- (interface_farming/scripts/farming_tools.rs2, "Produce used on a tool
-- leprechaun"): Catherby notes the whole stack ([362]) or a single one ([376]);
-- Falador's leprechaun refuses a cabbage ([481]).
local function count(t, name)
    local _, n = t.inv.count(name)
    return n
end

return {
    id = "leprechaun_notes",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give cabbage 3",
        "::give guam_leaf 1",
        "::give cert_cabbage 1",
        "::give bronze_dagger 1",
    },
    run = function(t)
        t.ticks(2)
        -- Catherby's leprechaun (2815,3466)
        t.exec("goto-catherby", t.player.goto_tile, 2814, 3465, 0)
        local leprechaun = t.player.by_symbol("npc", "farming_tools_leprechaun")
        t.exec("cabbages", t.player.use_on, "cabbage", leprechaun)
        t.exec("cabbages-box", t.chat.play, {"objbox:The leprechaun exchanges your items for banknotes."})
        t.check("cabbages-noted", count(t, "cabbage") == 0 and count(t, "cert_cabbage") == 4, "cabbage " .. tostring(count(t, "cabbage")) .. ", noted " .. tostring(count(t, "cert_cabbage")))
        t.exec("guam", t.player.use_on, "guam_leaf", leprechaun)
        t.exec("guam-box", t.chat.play, {"objbox:The leprechaun exchanges your item for a banknote."})
        t.check("guam-noted", count(t, "cert_guam_leaf") == 1, "noted guam " .. tostring(count(t, "cert_guam_leaf")))
        t.exec("note", t.player.use_on, "cert_cabbage", leprechaun)
        t.exec("note-page", t.chat.play, {"npc:That IS a banknote!"})
        t.exec("dagger", t.player.use_on, "bronze_dagger", leprechaun)
        t.exec("dagger-page", t.chat.play, {"npc:Nay, I've got no banknotes"})
        t.check("dagger-kept", count(t, "bronze_dagger") == 1, "the dagger stays")

        -- Falador's farm (3053,3305): no cabbages
        t.cheat("::give cabbage 1")
        t.ticks(2)
        t.exec("goto-falador", t.player.goto_tile, 3054, 3304, 0)
        local falador = t.player.by_symbol("npc", "farming_tools_leprechaun")
        t.exec("falador-cabbage", t.player.use_on, "cabbage", falador)
        t.exec("falador-cabbage-page", t.chat.play, {"npc:A cabbage? Don't ye go showing me yer cabbage"})
        t.check("falador-cabbage-kept", count(t, "cabbage") == 1, "the cabbage stays unnoted")
        t.finish(0)
    end,
}
