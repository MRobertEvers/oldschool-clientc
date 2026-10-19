-- toa_supply_hud: the raid-wide HUD and the Helpful Spirit, solo. Into the Nexus: the party
-- slot, the health orb and the HUD overlay. `::toaspirit` brings the spirit (two paths done);
-- talking to it opens the three bundles, the boost bundle is chosen, and the Supplies bag's
-- ops are driven: Withdraw 1, a dose of smelling salts (the salts timer varbit), the partial
-- dose stored back, Open and Close, then Withdraw All, which empties and destroys the bag.
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(40)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
local function server(t, name)
    local _, v = t.var.server(name)
    return tonumber(v) or -1
end
local function count(t, name)
    local _, n = t.inv.count(name)
    return tonumber(n) or -1
end

return {
    id = "toa_supply_hud",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = { "::clearinv", "::setlevel hitpoints 99" },
    run = function(t)
        t.cheat("::toa 1", false)
        t.ticks(4)
        t.expect("hud.open", t.ui.await_open("toa_hud", 6))
        t.expect("hud.partyslot", server(t, "varb14354_toa_client_partyslot") == 1 and "ok" or "fail",
            "partyslot " .. server(t, "varb14354_toa_client_partyslot"))
        t.expect("hud.health", server(t, "varb14346_toa_client_p0") == 27 and "ok" or "fail",
            "p0 " .. server(t, "varb14346_toa_client_p0"))
        t.expect("hud.empty_slot", server(t, "varb14347_toa_client_p1") == 0 and "ok" or "fail",
            "p1 " .. server(t, "varb14347_toa_client_p1"))

        t.cheat("::toaspirit", false)
        t.ticks(2)
        t.expect("spirit.arrives", said(t, "A helpful spirit has arrived with some supplies.") and "ok" or "fail")
        t.expect("spirit.talk", t.player.talk_to("toa_midraidloot_trader", 1))
        -- the first Claim on an account: the spirit's message box, then the bundles
        local _, box = t.chat.text()
        t.expect("spirit.first_claim", tostring(box):find("The spirit looks at you without saying a word", 1, true)
            and "ok" or "fail", tostring(box))
        t.chat.continue_()
        t.expect("spirit.bundles", t.ui.await_open("toa_midraid_loot", 10))
        t.expect("spirit.met", server(t, "varb14462_itt_met_trader") == 1 and "ok" or "fail")
        t.expect("spirit.rolled", server(t, "varb14322_toa_midraidloot_rolled") == 1 and "ok" or "fail")
        local _, boost = t.inv.count_in("toa_midraidloot_bundle3", "toa_supply_stats_2")
        t.expect("spirit.boost_salts", tonumber(boost) == 3 and "ok" or "fail", "salts " .. tostring(boost))
        local _, w = t.ui.widget("toa_midraid_loot:select_button_3")
        t.ui.invoke(w, 1)
        t.expect("spirit.chosen", t.ui.await_close("toa_midraid_loot", 6))
        t.expect("spirit.claimed", server(t, "varb14321_toa_midraidloot_claimed") == 1 and "ok" or "fail")
        t.expect("bag.held", count(t, "toa_midraidloot_bag") == 1 and "ok" or "fail")

        t.player.inv_op("toa_midraidloot_bag", 2)
        t.ticks(2)
        t.expect("bag.withdraw1", count(t, "toa_supply_stats_2") == 1 and "ok" or "fail",
            "salts in pack " .. count(t, "toa_supply_stats_2"))
        t.player.inv_op("toa_supply_stats_2", 1)
        t.ticks(3)
        t.expect("salts.timer", server(t, "varb14344_toa_midraidloot_stats_timer") == 32 and "ok" or "fail",
            "timer " .. server(t, "varb14344_toa_midraidloot_stats_timer"))
        t.expect("salts.message", said(t, "You crush the salts. Your heart rate increases.") and "ok" or "fail")
        t.player.use_item_on_item("toa_supply_stats_1", "toa_midraidloot_bag")
        t.ticks(2)
        t.expect("bag.store", count(t, "toa_supply_stats_1") == 0 and "ok" or "fail",
            "partial salts in pack " .. count(t, "toa_supply_stats_1"))

        t.player.inv_op("toa_midraidloot_bag", 1)
        t.expect("bag.open", t.ui.await_open("toa_midraidloot_bag", 6))
        local _, c = t.ui.widget("toa_midraidloot_bag:com_2")
        t.ui.invoke(c, 1)
        t.expect("bag.close", t.ui.await_close("toa_midraidloot_bag", 6))

        t.player.inv_op("toa_midraidloot_bag", 3)
        t.ticks(2)
        local got = string.format("salts(2) %d salts(1) %d adrenaline %d bag %d",
            count(t, "toa_supply_stats_2"), count(t, "toa_supply_stats_1"),
            count(t, "toa_supply_energy_2"), count(t, "toa_midraidloot_bag"))
        t.expect("bag.withdraw_all", (count(t, "toa_supply_stats_2") == 2 and count(t, "toa_supply_stats_1") == 1
            and count(t, "toa_supply_energy_2") == 2 and count(t, "toa_midraidloot_bag") == 0) and "ok" or "fail", got)
        t.expect("bag.disintegrates", (said(t, "The empty bag disintegrates.") and said(t, "The bag disintegrates.")) and "ok" or "fail")
        -- a second Claim after the pick: the strange look
        t.player.talk_to("toa_midraidloot_trader", 1)
        local _, again = t.chat.text()
        t.expect("spirit.claimed_look", tostring(again):find("You've clearly claimed all you can for now.", 1, true)
            and "ok" or "fail", tostring(again))
        t.chat.continue_()
    end,
}
