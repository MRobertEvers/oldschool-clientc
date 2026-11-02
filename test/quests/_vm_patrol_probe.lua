-- Probe: the Varrock Museum's patrolling workmen (vm_patrol_workmen.rs2) and the Dig Site
-- buttons' Polish, recorded on our tick log for the RSProx diff in the_dig_site.md section 15.
-- Stands between the Dig Site loader and the plank drop for one whole wheelbarrow loop, then
-- in the museum yard for the next dump.
return {
    setup = {
        "::clearinv",
        "::give digsitebuttons 1",
        "::tele 0_52_53_27_36",
    },
    run = function(t)
        t.ticklog.start()
        t.ticks(2)
        t.exec("buttons.polish", t.player.inv_op, "digsitebuttons", 2)
        t.ticks(2)
        local _, plain = t.inv.count("digsitebuttons")
        local _, shiny = t.inv.count("anma_p_buttons")
        t.check("buttons.polished", plain == 0 and shiny == 1, "buttons " .. tostring(plain) .. " polished " .. tostring(shiny))
        t.ticklog.mark("leg.digsite")
        t.ticks(320)
        t.ticklog.mark("leg.museum")
        t.cheat("::tele 0_51_53_3_42")
        t.ticks(320)
    end,
}
