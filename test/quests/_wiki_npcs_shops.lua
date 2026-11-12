-- The Asgarnia / Misthalin / Al Kharid npc and shop pass (2026-11-12): one
-- row each for the pieces that changed behaviour rather than words.
--   * Betty's Trade op: was a stacked empty [opnpc3,betty] header, i.e. no
--     shop at all (areas/port_sarim/scripts/betty.rs2).
--   * The Gem trader's Trade op: was "nothing to trade with yet"; the shop
--     itself is new (shop/al_kharid/scripts/gem_trader.rs2).
--   * Phials un-noting a banknote used on him (general/scripts/
--     banknote_exchange.rs2): 3 noted bones -> 3 bones for 15 coins.
--   * Ali the Leaflet Dropper's Take-flyer op: hands over one flyer
--     (areas/alkharid/scripts/alkharid_wiki_npcs.rs2).
return {
    id = "_wiki_npcs_shops",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::give coins 1000",
        "::give cert_bones 3",
    },
    run = function(t)
        local function count(sym)
            local _, n = t.inv.count(sym)
            return n or 0
        end

        -- Betty, Port Sarim.
        t.cheat("::tele 0_47_50_5_59")
        t.ticks(3)
        t.exec("betty.trade", t.shop.open, "betty", 3, "magicshop")
        local betty_cr, betty_cd = t.shop.close()
        t.check("betty.close", betty_cr, betty_cd)

        -- The Gem trader, Al Kharid.
        t.cheat("::tele 0_51_50_23_12")
        t.ticks(3)
        t.exec("gem_trader.trade", t.shop.open, "gem_trader", 3, "gemshop2")
        local gem_trader_cr, gem_trader_cd = t.shop.close()
        t.check("gem_trader.close", gem_trader_cr, gem_trader_cd)

        -- Phials, Rimmington: all three notes at 5 coins each.
        t.cheat("::tele 0_46_50_7_12")
        t.ticks(3)
        local coins_before = count("coins")
        local phials = t.player.by_symbol("npc", "uncerter_rimmington")
        t.exec("phials.use_note", t.player.use_on, "cert_bones", phials)
        t.exec("phials.exchange_all", t.chat.choose, "Exchange All: 15 coins")
        t.ticks(2)
        t.check("phials.unnoted", count("bones") == 3 and count("cert_bones") == 0
            and coins_before - count("coins") == 15 and "ok" or "hollow",
            "bones " .. count("bones") .. ", notes " .. count("cert_bones")
            .. ", coins spent " .. (coins_before - count("coins")))

        -- Ali the Leaflet Dropper, north Al Kharid: Take-flyer.
        t.cheat("::tele 0_51_51_20_63")
        t.ticks(3)
        t.exec("ali.take_flyer", t.player.talk_to, "ali_leaflet_dropper", 3)
        t.expect("ali.pages", t.chat.play({
            "npc:Here! Take one and let me get back to work.",
            "npc:I still have hundreds of these flyers to hand out",
            "end" }))
        t.check("ali.flyer", count("leaflet_dropper_flyer") == 1 and "ok" or "hollow",
            "flyers " .. count("leaflet_dropper_flyer"))
    end,
}
