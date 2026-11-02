-- Probe: open Renu's travel map at Fortis after Twilight's Promise and photograph it.
return {
    setup = {
        "::clearinv",
        "::complete quest_childrenofthesun",
        "::complete quest_twilightspromise",
        "::tele 0_26_49_33_4",
    },
    run = function(t)
        t.ticks(4)
        t.exec("goto-renu", t.player.goto_tile, 1697, 3140, 0)
        local tr = t.player.talk_to("quetzal_fortis", 1)
        local st = t.ui.await_open("quetzal_menu", 8)
        t.check("menu.open", st == "ok", "talk " .. tostring(tr) .. " await " .. tostring(st))
        t.ticks(3)
        t.shot("quetzal-menu")
        local wr, wid = t.ui.widget("quetzal_menu:icons", 1)
        t.check("icons.widget", wr == "ok" and wid ~= nil, tostring(wr))
        t.ui.invoke(wid, 1)
        t.ticks(8)
        local _, at = t.world.tile()
        t.check("flight.teomat", at ~= nil and at.x == 1437 and at.z == 3171, "landed at " .. tostring(at and (at.x .. "," .. at.z)))
        local st2 = t.ui.await_open("quetzal_menu", 1)
        t.check("menu.closed", st2 ~= "ok", tostring(st2))
    end,
}
