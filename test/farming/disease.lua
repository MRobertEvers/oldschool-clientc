-- Crop disease and the allotment / hops gardeners (skill_farming/scripts/
-- farming_disease.rs2, farming_gardener.rs2). Disease is a roll, so the test
-- sets the flags word by hand where it needs one (8 diseased, 32 protected) and
-- grows a stage with ::farmtick; the protected flag keeps the plain stages from
-- rolling. What it pins is what the client would draw (the transmit varbit)
-- and what the player is told:
--   * a diseased potato draws potato_1_diseased (135), cured it is potato_1 (7);
--   * a diseased crop that is not cured dies (potato_1_dead, 199), and Clear
--     digs it up the recorded way (RSProx 3265: two digs, 5 ticks);
--   * a herb's dead art is the herbs' shared set (herb_1_dead, 170), not +192;
--   * Elstan's Pay (north-west) and Vasquen's Talk-to deal (wiki
--     Transcript:Kragen).
local function count(t, name)
    local _, n = t.inv.count(name)
    return n
end

return {
    id = "farming_disease",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel farming 99",
        "::give seed_dibber 1",
        "::give spade 1",
        "::give rake 1",
        "::give potato_seed 6",
        "::give guam_seed 1",
        "::give barley_seed 4",
        "::give plant_cure 1",
        "::give bucket_compost 5",
        "::setvar varb708_varbit_708 3",
        "::setvar varb780_varbit_780 3",
        "::setvar varb718_varbit_718 3",
    },
    run = function(t)
        t.ticks(2)
        t.cheat("::tele 0_47_51_46_42")
        t.ticks(3)
        local veg = t.player.by_symbol("loc", "farming_veg_patch_1")

        -- ---- Diseased, cured ----
        t.exec("plant", t.player.use_on, "potato_seed", veg)
        t.ticks(6)
        t.expect("plant.state", t.var.await_server("varb708_varbit_708", 6, 5))
        t.cheat("::setvar varp7050_farming_falador_allot_nw_flags 32")
        t.cheat("::farmtick")
        t.expect("grow.state", t.var.await_server("varb708_varbit_708", 7, 5))

        t.cheat("::setvar varp7050_farming_falador_allot_nw_flags 8")
        t.cheat("::farmrefresh")
        t.expect("sick.art", t.var.await_server("varb4771_farming_transmit_a", 135, 5))

        t.exec("sick.pay", t.player.talk_to, "elstan", 3)
        t.exec("sick.pay.page", t.chat.play, { "npc:That patch is diseased - you should cure it before asking me to be responsible for it!", "end" })

        t.exec("cure", t.player.use_on, "plant_cure", veg)
        t.exec("cure.mes", t.msg.expect, "You treat the diseased plant with the plant cure.")
        t.expect("cure.art", t.var.await_server("varb4771_farming_transmit_a", 7, 5))
        t.check("cure.vial", count(t, "vial_empty") == 1, "vials " .. tostring(count(t, "vial_empty")))

        -- ---- Diseased, left: dies, cleared ----
        t.cheat("::setvar varp7050_farming_falador_allot_nw_flags 8")
        t.cheat("::farmtick")
        t.expect("dead.art", t.var.await_server("varb4771_farming_transmit_a", 199, 5))
        do
            local _, v = t.var.varbit("varb708_varbit_708")
            t.check("dead.state", v == 7, "state " .. tostring(v))
        end

        t.exec("dead.pay", t.player.talk_to, "elstan", 3)
        t.exec("dead.pay.page", t.chat.play, { "npc:That patch is dead - it's too late for me to do anything about it now.", "end" })

        t.exec("clear", t.player.click_loc, "farming_veg_patch_1", 1)
        t.exec("clear.start", t.msg.expect, "You start digging the farming patch...")
        t.ticks(6)
        t.exec("clear.done", t.msg.expect, "You have successfully cleared this patch for new crops.")
        t.expect("clear.state", t.var.await_server("varb708_varbit_708", 3, 8))
        t.expect("clear.art", t.var.await_server("varb4771_farming_transmit_a", 3, 5))

        -- ---- Elstan's Pay (north-west) ----
        t.exec("replant", t.player.use_on, "potato_seed", veg)
        t.ticks(6)
        t.exec("pay", t.player.talk_to, "elstan", 3)
        t.exec("pay.menu", t.chat.play, { "choose:Yes.", "npc:That'll do nicely", "end" })
        t.exec("pay.mes", t.msg.expect, "You pay the gardener 2 buckets of compost to protect the patch.")
        t.check("pay.paid", count(t, "bucket_compost") == 3, "compost " .. tostring(count(t, "bucket_compost")))
        t.exec("pay.again", t.player.talk_to, "elstan", 3)
        t.exec("pay.again.page", t.chat.play, { "npc:I'm already looking after that patch for you.", "end" })

        -- ---- A herb's dead art (Falador herb patch) ----
        local herb = t.player.by_symbol("loc", "farming_herb_patch_1")
        t.exec("herb.plant", t.player.use_on, "guam_seed", herb)
        t.ticks(6)
        t.cheat("::setvar varp7053_farming_falador_herb_flags 32")
        t.cheat("::farmtick")
        t.expect("herb.grow", t.var.await_server("varb780_varbit_780", 5, 5))
        t.cheat("::setvar varp7053_farming_falador_herb_flags 8")
        t.cheat("::farmrefresh")
        t.expect("herb.sick.art", t.var.await_server("varb4774_farming_transmit_d", 128, 5))
        t.cheat("::farmtick")
        t.expect("herb.dead.art", t.var.await_server("varb4774_farming_transmit_d", 170, 5))

        -- ---- Vasquen, Lumbridge hops: Talk-to's deal ----
        t.cheat("::tele 0_50_51_26_44")
        t.ticks(3)
        local hops = t.player.by_symbol("loc", "farming_hops_patch_3")
        t.exec("hops.plant", t.player.use_on, "barley_seed", hops)
        t.ticks(6)
        t.exec("hops.talk", t.player.talk_to, "farming_gardener_hops_3", 1)
        t.exec("hops.deal", t.chat.play, {
            "choose:Would you look after my crops for me?",
            "player:Would you look after my crops for me?",
            "npc:If you like, but I want 3 buckets of compost for that.",
            "choose:Okay, it's a deal.",
            "player:Okay, it's a deal.",
            "npc:That'll do nicely",
            "end",
        })
        t.check("hops.paid", count(t, "bucket_compost") == 0, "compost " .. tostring(count(t, "bucket_compost")))
        t.finish(0)
    end,
}
