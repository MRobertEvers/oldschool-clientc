-- Harvesting the crops planted through the generic op1 (skill_farming/scripts/
-- farming_items.rs2 [oploc1,_farming_crop]), the crops added on 2026-11-19, and
-- Inspect (farming_inspect.rs2). Each crop is protected (flags word 32) so a
-- disease roll cannot stop its growth, and grown a stage per ::farmtick.
--   * snape grass grows 128..134 and then jumps to 138 (potato's diseased art
--     sits at 135..137), and its grown art is harvested through op1;
--   * a gout tuber is refused until Eadgar's Ruse is done, and goutweed is
--     picked through op1 (no name binding);
--   * jute (hops) and a cadavaberry bush: check-health, pick-from, clear;
--   * Inspect's sentence, recorded wording (RSProx 548, 1105, 3265, 4427);
--   * the single patches (farming_special_patches.rs2): a cactus checked,
--     picked, regrown a spine and cleared; six bittercaps; belladonna that
--     hurts without gloves and yields 6+ nightshade with them.
local function count(t, name)
    local _, n = t.inv.count(name)
    return n
end

local function grow(t, times)
    for _ = 1, times do
        t.cheat("::farmtick")
        t.ticks(1)
    end
end

return {
    id = "farming_harvest",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel farming 99",
        "::give seed_dibber 1",
        "::give spade 1",
        "::give snape_grass_seed 3",
        "::setvar varb708_varbit_708 3",
        "::setvar varb780_varbit_780 3",
        "::setvar varb718_varbit_718 3",
        "::setvar varb732_varbit_732 3",
        "::setvar varb744_varbit_744 3",
        "::setvar varb746_varbit_746 3",
        "::setvar varb748_varbit_748 3",
    },
    run = function(t)
        t.ticks(2)
        t.cheat("::tele 0_47_51_46_42")
        t.ticks(3)
        local veg = t.player.by_symbol("loc", "farming_veg_patch_1")

        -- ---- Snape grass ----
        t.exec("veg.inspect.empty", t.player.click_loc, "farming_veg_patch_1", 2)
        t.exec("veg.inspect.empty.mes", t.msg.expect, "This is an allotment. The soil has not been treated. The patch is empty and weeded.")
        t.exec("snape.plant", t.player.use_on, "snape_grass_seed", veg)
        t.expect("snape.state", t.var.await_server("varb708_varbit_708", 128, 8))
        t.cheat("::setvar varp7050_farming_falador_allot_nw_flags 32")
        t.ticks(3)
        t.exec("snape.inspect", t.player.click_loc, "farming_veg_patch_1", 2)
        t.exec("snape.inspect.mes", t.msg.expect, "This is an allotment. The soil has not been treated. The patch has snape grass growing in it and is at state 1/8.")
        grow(t, 6)
        t.expect("snape.stage6", t.var.await_server("varb708_varbit_708", 134, 5))
        grow(t, 1)
        t.expect("snape.grown", t.var.await_server("varb708_varbit_708", 138, 5))
        t.expect("snape.grown.art", t.var.await_server("varb4771_farming_transmit_a", 138, 5))
        t.exec("snape.inspect.grown", t.player.click_loc, "farming_veg_patch_1", 2)
        t.exec("snape.inspect.grown.mes", t.msg.expect, "This is an allotment. The soil has not been treated. The patch (snape grass) is fully grown.")
        t.exec("snape.harvest", t.player.click_loc, "farming_veg_patch_1", 1)
        t.exec("snape.harvest.mes", t.msg.expect, "You begin to harvest the allotment.")
        t.exec("snape.empty", t.msg.await, "The allotment is now empty.", 300)
        t.check("snape.yield", count(t, "snape_grass") >= 3, "snape grass " .. tostring(count(t, "snape_grass")))
        t.expect("snape.cleared", t.var.await_server("varb708_varbit_708", 3, 5))

        t.cheat("::clearinv")
        t.cheat("::give seed_dibber 1")
        t.cheat("::give spade 1")
        t.cheat("::give village_rare_tuber 1")
        t.ticks(1)

        -- ---- Gout tuber (Falador herb patch) ----
        local herb = t.player.by_symbol("loc", "farming_herb_patch_1")
        t.exec("gout.refused", t.player.use_on, "village_rare_tuber", herb)
        t.ticks(4)
        do
            local _, v = t.var.varbit("varb780_varbit_780")
            t.check("gout.refused.state", v == 3, "state " .. tostring(v))
        end
        t.check("gout.refused.kept", count(t, "village_rare_tuber") == 1, "tubers " .. tostring(count(t, "village_rare_tuber")))
        t.cheat("::setvar varp335_eadgar_quest 110")
        t.ticks(1)
        t.exec("gout.plant", t.player.use_on, "village_rare_tuber", herb)
        t.exec("gout.plant.mes", t.msg.expect, "You plant a gout tuber in the herb patch.")
        t.expect("gout.state", t.var.await_server("varb780_varbit_780", 192, 5))
        t.cheat("::setvar varp7053_farming_falador_herb_flags 32")
        t.ticks(3)
        t.exec("gout.inspect", t.player.click_loc, "farming_herb_patch_1", 2)
        t.exec("gout.inspect.mes", t.msg.expect, "This is a herb patch. The soil has not been treated. The patch has Goutweed growing in it and is at state 1/5.")
        grow(t, 4)
        t.expect("gout.grown", t.var.await_server("varb780_varbit_780", 196, 5))
        t.exec("gout.harvest", t.player.click_loc, "farming_herb_patch_1", 1)
        t.exec("gout.harvest.mes", t.msg.expect, "You begin to harvest the herb patch.")
        t.exec("gout.empty", t.msg.await, "The herb patch is now empty.", 300)
        t.check("gout.yield", count(t, "eadgar_goutweed_herb") >= 3, "goutweed " .. tostring(count(t, "eadgar_goutweed_herb")))

        t.cheat("::clearinv")
        t.cheat("::give seed_dibber 1")
        t.cheat("::give spade 1")
        t.cheat("::give jute_seed 3")
        t.ticks(1)

        -- ---- Jute (Lumbridge hops patch) ----
        t.cheat("::tele 0_50_51_26_44")
        t.ticks(3)
        local hops = t.player.by_symbol("loc", "farming_hops_patch_3")
        t.exec("jute.plant", t.player.use_on, "jute_seed", hops)
        t.expect("jute.state", t.var.await_server("varb718_varbit_718", 84, 8))
        t.cheat("::setvar varp7056_farming_lumbridge_hops_flags 32")
        t.ticks(3)
        grow(t, 5)
        t.expect("jute.grown", t.var.await_server("varb718_varbit_718", 89, 5))
        t.exec("jute.harvest", t.player.click_loc, "farming_hops_patch_3", 1)
        t.exec("jute.harvest.mes", t.msg.expect, "You begin to harvest the hops patch.")
        t.exec("jute.empty", t.msg.await, "The hops patch is now empty.", 300)
        t.check("jute.yield", count(t, "jute_fibre") >= 3, "jute " .. tostring(count(t, "jute_fibre")))

        t.cheat("::clearinv")
        t.cheat("::give seed_dibber 1")
        t.cheat("::give spade 1")
        t.cheat("::give cadavaberry_bush_seed 1")
        t.ticks(1)

        -- ---- Cadavaberry bush (Champions' Guild) ----
        t.cheat("::tele 0_49_52_45_27")
        t.ticks(3)
        local bush = t.player.by_symbol("loc", "farming_bush_patch_1")
        t.exec("bush.plant", t.player.use_on, "cadavaberry_bush_seed", bush)
        t.expect("bush.state", t.var.await_server("varb732_varbit_732", 15, 8))
        t.cheat("::setvar varp7047_farming_champions_bush_flags 32")
        t.ticks(3)
        grow(t, 6)
        t.expect("bush.claim", t.var.await_server("varb732_varbit_732", 251, 5))
        t.exec("bush.check", t.player.click_loc, "farming_bush_patch_1", 1)
        t.exec("bush.check.mes", t.msg.expect, "You examine the bush for signs of disease and find that it's in perfect health.")
        t.expect("bush.full", t.var.await_server("varb732_varbit_732", 25, 5))
        t.exec("bush.pick", t.player.click_loc, "farming_bush_patch_1", 1)
        t.exec("bush.pick.mes", t.msg.expect, "You pick some cadava berries.")
        t.expect("bush.picked", t.var.await_server("varb732_varbit_732", 21, 40))
        t.check("bush.yield", count(t, "cadavaberries") == 4, "berries " .. tostring(count(t, "cadavaberries")))
        t.exec("bush.clear", t.player.click_loc, "farming_bush_patch_1", 1)
        t.exec("bush.clear.confirm", t.chat.play, { "choose:Yes, I want to clear it for new crops.", "end" })
        t.ticks(6)
        t.exec("bush.clear.mes", t.msg.expect, "You have successfully cleared this patch for new crops.")
        t.expect("bush.cleared", t.var.await_server("varb732_varbit_732", 3, 5))

        local function fresh(seed)
            t.cheat("::clearinv")
            t.cheat("::give seed_dibber 1")
            t.cheat("::give spade 1")
            t.cheat("::give " .. seed .. " 1")
            t.ticks(1)
        end

        -- ---- Cactus (Al Kharid) ----
        fresh("cactus_seed")
        t.cheat("::tele 0_51_50_49_3")
        t.ticks(3)
        local cactus = t.player.by_symbol("loc", "farming_cactus_patch")
        t.exec("cactus.plant", t.player.use_on, "cactus_seed", cactus)
        t.exec("cactus.plant.mes", t.msg.expect, "You plant a cactus seed in the cactus patch.")
        t.expect("cactus.state", t.var.await_server("varb744_varbit_744", 8, 8))
        t.cheat("::setvar varp9744_farming_alkharid_cactus_flags 32")
        t.ticks(3)
        grow(t, 7)
        t.expect("cactus.claim", t.var.await_server("varb744_varbit_744", 31, 5))
        t.exec("cactus.check", t.player.click_loc, "farming_cactus_patch", 1)
        t.expect("cactus.full", t.var.await_server("varb744_varbit_744", 18, 5))
        t.exec("cactus.pick", t.player.click_loc, "farming_cactus_patch", 1)
        t.expect("cactus.picked", t.var.await_server("varb744_varbit_744", 15, 30))
        t.check("cactus.yield", count(t, "cactus_spine") == 3, "spines " .. tostring(count(t, "cactus_spine")))
        grow(t, 1)
        t.expect("cactus.regrew", t.var.await_server("varb744_varbit_744", 16, 5))
        t.exec("cactus.inspect", t.player.click_loc, "farming_cactus_patch", 2)
        t.exec("cactus.inspect.mes", t.msg.expect, "This is a cactus patch. The soil has not been treated. The patch (Cactus) is fully grown.")
        t.exec("cactus.clear", t.player.click_loc, "farming_cactus_patch", 5)
        t.exec("cactus.clear.confirm", t.chat.play, { "choose:Yes, I want to clear it for new crops.", "end" })
        t.expect("cactus.cleared", t.var.await_server("varb744_varbit_744", 3, 10))

        -- ---- Bittercap mushrooms (Canifis) ----
        fresh("mushroom_seed")
        t.cheat("::tele 0_53_54_58_14")
        t.ticks(3)
        local mush = t.player.by_symbol("loc", "farming_mushroom_patch")
        t.exec("mush.plant", t.player.use_on, "mushroom_seed", mush)
        t.expect("mush.state", t.var.await_server("varb746_varbit_746", 4, 8))
        t.cheat("::setvar varp9748_farming_canifis_mushroom_flags 32")
        t.ticks(3)
        grow(t, 6)
        t.expect("mush.grown", t.var.await_server("varb746_varbit_746", 10, 5))
        t.exec("mush.pick", t.player.click_loc, "farming_mushroom_patch", 1)
        t.exec("mush.empty", t.msg.await, "The mushroom patch is now empty.", 60)
        t.check("mush.yield", count(t, "bittercap_mushroom") == 6, "mushrooms " .. tostring(count(t, "bittercap_mushroom")))
        t.expect("mush.cleared", t.var.await_server("varb746_varbit_746", 3, 5))

        -- ---- Belladonna (Draynor Manor) ----
        fresh("belladonna_seed")
        t.cheat("::give leather_gloves 1")
        t.cheat("::tele 0_48_52_13_25")
        t.ticks(3)
        local bella = t.player.by_symbol("loc", "farming_belladonna_patch")
        t.exec("bella.plant", t.player.use_on, "belladonna_seed", bella)
        t.expect("bella.state", t.var.await_server("varb748_varbit_748", 4, 8))
        t.cheat("::setvar varp9752_farming_draynor_belladonna_flags 32")
        t.ticks(3)
        grow(t, 4)
        t.expect("bella.grown", t.var.await_server("varb748_varbit_748", 8, 5))
        local _, hp0 = t.skill.read("hitpoints")
        -- A bare-handed pick changes nothing the click's settle watches for (no
        -- chat, no loc, no inventory): the hit is the answer, checked below.
        t.player.click_loc("farming_belladonna_patch", 1)
        t.ticks(3)
        do
            local _, hp1 = t.skill.read("hitpoints")
            t.check("bella.bare.hurt", hp1.level == hp0.level - 2, "hp " .. tostring(hp0.level) .. " -> " .. tostring(hp1.level))
            local _, v = t.var.varbit("varb748_varbit_748")
            t.check("bella.bare.kept", v == 8, "state " .. tostring(v))
        end
        t.exec("bella.gloves", t.player.equip, "leather_gloves")
        t.exec("bella.pick", t.player.click_loc, "farming_belladonna_patch", 1)
        t.expect("bella.cleared", t.var.await_server("varb748_varbit_748", 3, 8))
        t.check("bella.yield", count(t, "stackable_nightshade") >= 6, "nightshade " .. tostring(count(t, "stackable_nightshade")))
        t.finish(0)
    end,
}
