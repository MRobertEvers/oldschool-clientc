-- obligator_rolls: the obligator thrust's damage against Xarth from the kit
-- the recorded obligator raiders wore (RSProx index 4308..4316: inquisitor's,
-- amulet of rancour, ferocious gloves, avernic treads, infernal cape, an ultor
-- ring), boosted and prayed as a raider is, for tools-side comparison with the
-- recorded hits. ::oblroll writes each roll to the server log.
--
--   ./src/build_opt/torirsserver --scriptrun test/combat/obligator_rolls.lua --bots 1 \
--       --name ob0 --ticks 200 --session /tmp/ob --fixture test/raids/fixtures/fresh_lumbridge.ini 2>ob.log
return {
    id = "obligator_rolls",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::give obligator 1", "::wield obligator",
        "::give inquisitors_helm 1", "::wield inquisitors_helm",
        "::give inquisitors_body 1", "::wield inquisitors_body",
        "::give inquisitors_skirt 1", "::wield inquisitors_skirt",
        "::give amulet_of_rancour 1", "::wield amulet_of_rancour",
        "::give infernal_cape_trouver 1", "::wield infernal_cape_trouver",
        "::give ferocious_gloves 1", "::wield ferocious_gloves",
        "::give avernic_treads_max 1", "::wield avernic_treads_max",
        "::give ultor_ring 1", "::wield ultor_ring",
    },
    run = function(t)
        t.ticks(2)
        -- A super combat (5 + 15%: 99 -> 118) and Piety (Zeal, likelier for
        -- them, is not in this tree); the style row's default thrust.
        t.cheat("::boost strength 5 15", false)
        t.cheat("::boost attack 5 15", false)
        t.prayer.set("piety", true)
        t.ticks(2)
        t.cheat("::oblroll tfa_xarth 4000", false)
        t.ticks(2)
        t.expect("obligator_rolls.run", "ok", "done")
    end,
}
