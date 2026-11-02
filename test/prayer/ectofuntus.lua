-- The Ectofuntus as RSProx index 4462 records it (docs/rsprox-analysis/quests/
-- ghosts_ahoy.md, "The Ectofuntus"): bones used on the grinder's hopper go on
-- by themselves (load, wind, pour, fill: [437..503]), a bucket on the pool
-- fills every empty bucket ([585..602]), each worship owes 5 ecto-tokens
-- (%varb4769_ahoy_ectotokens_base, [668..678]) and a disciple pays them out
-- ([684..686]).
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_CONTENT=$PWD/OSRS-Content/osrs-content ./src/build_opt/torirsserver \
--       --scriptrun test/prayer/ectofuntus.lua --bots 1 --name ecto0 --ticks 1500 \
--       --session <dir> --fixture test/quests/fixtures/fresh_lumbridge.ini
return {
    id = "ectofuntus",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::clearinv",
        "::give amulet_of_ghostspeak 1",
        "::give dragon_bones 2",
        "::give pot_empty 2",
        "::give bucket_empty 2",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()
        t.exec("equip.amulet", t.player.equip, "amulet_of_ghostspeak")

        -- The grinder, upstairs in the temple.
        t.exec("goto.grinder", t.player.goto_tile, 3659, 3524, 1)
        local hopper = t.player.by_symbol("loc", "ahoy_grinder_loader")
        t.exec("bonesOnHopper", t.player.use_on, "dragon_bones", hopper, { at = { 3660, 3525, 1 } })
        t.exec("var.loaded", t.var.await_server, "varb210_ahoy_grinder_status", 1, 3)
        t.exec("var.ground", t.var.await_server, "varb210_ahoy_grinder_status", 2, 12)
        t.exec("inv.meal1", t.inv.await, "pot_bonemeal_dragon", 1, 12)
        t.exec("inv.meal2", t.inv.await, "pot_bonemeal_dragon", 2, 30)
        t.exec("var.empty", t.var.await_server, "varb210_ahoy_grinder_status", 0, 3)
        t.exec("inv.bones_gone", t.inv.expect_absent, "dragon_bones")

        -- The pool, at the foot of the cavern stairs: teleported (the stairs
        -- are the maplink audit's).
        t.exec("goto.pool", t.player.goto_tile, 3683, 9888, 0)
        local floor = t.player.by_symbol("loc", "ahoy_new_green_floor")
        t.exec("bucketOnPool", t.player.use_on, "bucket_empty", floor)
        t.exec("inv.ecto2", t.inv.await, "bucket_ectoplasm", 2, 14)

        -- Worship twice: 10 tokens owed.
        t.exec("goto.fountain", t.player.goto_tile, 3659, 3517, 0)
        t.exec("worship1", t.player.click_loc, "ahoy_ectofuntus", 1)
        t.ticks(2)
        t.exec("worship2", t.player.click_loc, "ahoy_ectofuntus", 1)
        t.exec("var.owed", t.var.await_server, "varb4769_ahoy_ectotokens_base", 10, 5)
        t.exec("inv.pots_back", t.inv.await, "pot_empty", 2, 3)
        t.exec("inv.buckets_back", t.inv.await, "bucket_empty", 2, 3)

        t.exec("talkToDisciple", t.player.talk_to, "ahoy_disciple", 1)
        t.exec("talkToDisciple-dialog", t.chat.play, {
            "player:Can I have the tokens I have earned?",
            "npc:Certainly, mortal. Here's 10 ectotokens.",
        })
        t.exec("inv.tokens", t.inv.await, "ectotoken", 10, 3)
        t.exec("var.paid", t.var.expect, "varb4769_ahoy_ectotokens_base", 0)

        -- Nothing left to worship with: the box ([679]).
        t.exec("worshipEmpty", t.player.click_loc, "ahoy_ectofuntus", 1)
        t.exec("worshipEmpty-dialog", t.chat.play, {
            "mesbox:You don't have any ectoplasm or crushed bones to put into the Ectofuntus.",
        })
        t.finish(0)
    end,
}
