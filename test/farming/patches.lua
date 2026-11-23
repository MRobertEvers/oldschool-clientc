-- The patches past the four classic areas (tools/gen_farming_patch_slots.py ->
-- skill_farming/scripts/farming_patch_slots.rs2): each takes a crop, stores it
-- in its own varps, draws it through its wrapper's transmit varbit, grows, and
-- is harvested through the generic op1.
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
    id = "farming_patches",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel farming 99",
        "::give seed_dibber 1",
        "::give spade 1",
        "::give potato_seed 3",
        "::give guam_seed 1",
        "::give marigold_seed 1",
        "::give onion_seed 3",
        "::give tarromin_seed 1",
        "::setvar varp9753_farming_hosidius_allot_ne_state 3",
        "::setvar varp9768_farming_hosidius_herb_state 3",
        "::setvar varp9788_farming_guild_herb_state 3",
        "::setvar varp9793_farming_prif_allot_n_state 3",
        "::setvar varp9818_farming_civitas_flower_state 3",
        "::setvar varp9828_farming_harmony_herb_state 3",
    },
    run = function(t)
        t.ticks(2)

        -- ---- Hosidius: the NE allotment grown and harvested ----
        t.cheat("::tele 0_27_55_12_36")
        t.ticks(3)
        local veg = t.player.by_symbol("loc", "farming_veg_patch_10")
        t.exec("hos.plant", t.player.use_on, "potato_seed", veg)
        t.expect("hos.state", t.var.await_server("varp9753_farming_hosidius_allot_ne_state", 6, 8))
        t.expect("hos.art", t.var.await_server("varb4771_farming_transmit_a", 6, 5))
        t.cheat("::setvar varp9757_farming_hosidius_allot_ne_flags 32")
        t.ticks(2)
        grow(t, 4)
        t.expect("hos.grown", t.var.await_server("varp9753_farming_hosidius_allot_ne_state", 10, 5))
        t.expect("hos.grown.art", t.var.await_server("varb4771_farming_transmit_a", 10, 5))
        t.exec("hos.harvest", t.player.click_loc, "farming_veg_patch_10", 1)
        t.exec("hos.harvest.mes", t.msg.await, "You begin to harvest the allotment.", 5)
        t.exec("hos.empty", t.msg.await, "The allotment is now empty.", 300)
        t.check("hos.yield", count(t, "potato") >= 3, "potatoes " .. tostring(count(t, "potato")))

        -- Hosidius herb: planted, drawn on transmit_d, inspected.
        local herb = t.player.by_symbol("loc", "farming_herb_patch_6")
        t.exec("hosherb.plant", t.player.use_on, "guam_seed", herb)
        t.expect("hosherb.state", t.var.await_server("varp9768_farming_hosidius_herb_state", 4, 8))
        t.expect("hosherb.art", t.var.await_server("varb4774_farming_transmit_d", 4, 5))
        t.exec("hosherb.inspect", t.player.click_loc, "farming_herb_patch_6", 2)
        t.exec("hosherb.inspect.mes", t.msg.expect, "This is a herb patch. The soil has not been treated. The patch has Guam leaf growing in it and is at state 1/5.")

        -- ---- Farming Guild herb patch: transmit_e ----
        t.cheat("::tele 0_19_58_24_14")
        t.ticks(3)
        local gherb = t.player.by_symbol("loc", "farming_herb_patch_7")
        t.cheat("::give guam_seed 1")
        t.ticks(1)
        t.exec("guild.plant", t.player.use_on, "guam_seed", gherb)
        t.expect("guild.state", t.var.await_server("varp9788_farming_guild_herb_state", 4, 8))
        t.expect("guild.art", t.var.await_server("varb4775_farming_transmit_e", 4, 5))

        -- ---- Prifddinas north allotment: transmit_a ----
        t.cheat("::tele 0_51_95_31_22")
        t.ticks(3)
        local pveg = t.player.by_symbol("loc", "farming_veg_patch_14")
        t.exec("prif.plant", t.player.use_on, "onion_seed", pveg)
        t.expect("prif.state", t.var.await_server("varp9793_farming_prif_allot_n_state", 13, 8))
        t.expect("prif.art", t.var.await_server("varb4771_farming_transmit_a", 13, 5))

        -- ---- Civitas flower patch: transmit_c ----
        t.cheat("::tele 0_24_48_47_22")
        t.ticks(3)
        local cflower = t.player.by_symbol("loc", "farming_flower_patch_8")
        t.exec("civ.plant", t.player.use_on, "marigold_seed", cflower)
        t.expect("civ.state", t.var.await_server("varp9818_farming_civitas_flower_state", 8, 8))
        t.expect("civ.art", t.var.await_server("varb4773_farming_transmit_c", 8, 5))

        -- ---- Harmony herb patch: transmit_b, and it is disease free ----
        t.cheat("::tele 0_59_44_15_21")
        t.ticks(3)
        local hherb = t.player.by_symbol("loc", "farming_herb_patch_5")
        t.exec("harm.plant", t.player.use_on, "tarromin_seed", hherb)
        t.expect("harm.state", t.var.await_server("varp9828_farming_harmony_herb_state", 18, 8))
        t.expect("harm.art", t.var.await_server("varb4772_farming_transmit_b", 18, 5))
        grow(t, 4)
        t.expect("harm.grown", t.var.await_server("varp9828_farming_harmony_herb_state", 22, 5))
        t.finish(0)
    end,
}
