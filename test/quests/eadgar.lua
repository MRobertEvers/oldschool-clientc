-- Eadgar's Ruse. Rewritten from the generated scaffold against the quest's
-- own .rs2 scripts (server/scripts/quests/quest_eadgar/, plus the offer/
-- turn-in half in areas/area_taverly/scripts/sanfew.rs2 and the state
-- machine in quests/quest_troll/scripts/troll_eadgar.rs2). The generated
-- scaffold's goto_tile chain walked every troll-stronghold door/stairs as a
-- click_loc; those are pure p_teleport() geography (quest_troll.rs2) with no
-- state of their own, so this file drops them and goto_tiles straight to
-- each real interaction point instead (doc section 2's "delete any
-- scaffold-emitted click_loc row for a ladder or gate", which the Troll
-- Stronghold's stairs/doors are -- verified against quest_troll.rs2 before
-- removing a single one of them).
--
-- Setup: herblore 31, Druidic Ritual and Troll Stronghold complete
-- (prerequisites sanfew.rs2's `sanfew_more_work` gates on), tutorial kit
-- cleared. The ::give lines are shop-bought/craftable prerequisites the
-- quest's own script consumes one at a time by real clicks below (logs,
-- raw chicken, grain, vodka, pineapple chunks, pestle and mortar, an
-- unfinished ranarr potion) -- never the quest's own deliverable itself
-- (the fake man, the troll truth potion, the goutweed) which is built and
-- fetched for real. The dirty druid robe is NOT given -- it is talked out
-- of Tegid below (eadgar_druid_washing.rs2), same as every other real step.

return {
    id = "eadgar",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel herblore 31",
        "::complete quest_druidicritual", -- quest_cheat.rs2's dispatch row is quest_druidicritual, not quest_druid
        "::setvar troll_freed_eadgar 1", -- no ::complete arm for Troll Stronghold exists; sanfew.rs2's only troll gate is this one flag
        "::give logs 2", -- one for Eadgar's scarecrow, one to burn for the troll thistle (see the dryThistle note below)
        "::give tinderbox 1",
        "::give raw_chicken 5",
        "::give grain 10",
        "::give vodka 1",
        "::give pineapple_chunks 1",
        "::give pestle_and_mortar 1",
        "::give ranarrvial 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "eadgar_quest",
            constants = {
                not_started = 0,
                started = 10,
                spoken_eadgar_first = 15,
                spoken_burntmeat_first = 20,
                spoken_burntmeat_second = 25,
                needs_parrot = 30,
                explained_plan = 50,
                hid_parrot = 60,
                needs_items = 70,
                needs_potion = 80,
                needs_parrot_back = 85,
                got_parrot_back = 86,
                got_fake_man = 87,
                got_burnt_meat = 90,
                unlocked_storeroom = 100,
                complete = 110,
            },
            row = "quest_eadgarsruse",
            display = "Eadgar's Ruse",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats (::complete, ::give) are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------
        -- 1. Sanfew: accept the quest (sanfew.rs2 sanfew_more_work).
        -- ---------------------------------------------------------------
        t.exec("goto-sanfew-1", t.player.goto_tile, 2897, 3426, 1)
        t.exec("talkToSanfew-accept", t.player.talk_to, "sanfew", 1)
        t.exec("talkToSanfew-accept-dialog", t.chat.play, {
            "npc:What can I do for you young 'un",
            "choose:Have you any more work for me, to help reclaim the circle?",
            "player:Have you any more work for me to help reclaim the stone circle?",
            "npc:Ah, you've come just in time. I need a certain herb",
            "npc:It used to be quite common, but nowadays only the trolls know",
            "player:And what exactly do you want me to do?",
            "npc:Journey to the north into the land of the trolls",
            "npc:My friend Eadgar lives in the area, and he may be able to help you",
            "choose:I'll head up to Trollheim and see what I can find out.",
            "player:I'll head up to Trollheim and see what I can find out.",
            "mesbox:You are now on a quest: Eadgar's Ruse",
        })
        t.chat.close()
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------------------
        -- 2. Eadgar (1st): ask about goutweed (troll_eadgar.rs2
        --    eadgar_quest_ask_goutweed, reached only while stage=started).
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-1", t.player.goto_tile, 2890, 10086, 2)
        t.exec("talkToEadgar-askGoutweed", t.player.talk_to, "troll_eadgar", 1)
        t.exec("talkToEadgar-askGoutweed-dialog", t.chat.play, {
            "choose:Do you know where I can find some goutweed?",
            "player:Do you know where I can find some goutweed?",
            "npc:Goutweed is used as an ingredient in troll cooking",
            "player:Thanks, I'll go and find one.",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_eadgar_first", t.quest.expect_stage("spoken_eadgar_first"))

        -- ---------------------------------------------------------------
        -- 3. Burntmeat (1st): "bring me a tasty human"
        --    (eadgar_troll_chief_cook.rs2, else branch then the
        --    started/spoken_eadgar_first branch -> spoken_burntmeat_second).
        -- ---------------------------------------------------------------
        t.exec("goto-burntmeat-1", t.player.goto_tile, 2844, 10057, 1)
        t.exec("talkToBurntmeat-1", t.player.talk_to, "eadgar_troll_chief_cook", 1)
        t.exec("talkToBurntmeat-1-dialog", t.chat.play, {
            "player:Er, hi.",
            "npc:Hmm? What human do in troll kitchen?",
            "player:Oh, you don't want to eat me!",
            "npc:Hmm. Burntmeat think you probably right.",
            "player:I'm on a quest to find some goutweed.",
            "npc:Bwahahaha! Burntmeat not give his greatest cooking secret",
            "npc:But Burntmeat also has quest for human!",
            "player:Really? What is it?",
            "npc:Bring back a tasty human for Burntmeat's stew.",
            "player:Right. I'll just...go fetch that for you then. Bye!",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_burntmeat_second", t.quest.expect_stage("spoken_burntmeat_second"))

        -- ---------------------------------------------------------------
        -- 4. Eadgar (2nd): explains the fake-man plan -> needs a parrot.
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-2", t.player.goto_tile, 2890, 10086, 2)
        t.exec("talkToEadgar-explainParrot", t.player.talk_to, "troll_eadgar", 1)
        t.exec("talkToEadgar-explainParrot-dialog", t.chat.play, {
            "player:The troll cook wants me to bring him a tasty human",
            "npc:Hoho! Does he now?",
            "npc:What we need is something that looks like a human",
            "player:And how exactly are we going to manage that?",
            "npc:Yes! It's bound to work. First of all, I will need a parrot!",
            "player:A parrot? Alright, I'll see what I can do.",
        })
        t.chat.close()
        t.expect("quest.stage.needs_parrot", t.quest.expect_stage("needs_parrot"))

        -- ---------------------------------------------------------------
        -- 5. Ardougne Zoo: Parroty Pete (flavour), make alco-chunks, lure
        --    the parrot through the aviary hatch.
        -- ---------------------------------------------------------------
        t.exec("goto-pete", t.player.goto_tile, 2611, 3285, 0)
        t.exec("talkToPete", t.player.talk_to, "eadgar_zoo_keeper_aviary", 1)
        t.exec("talkToPete-dialog", t.chat.play, {
            "npc:Good day, good day. Come to admire the new parrot aviary",
            "choose:It's very nice.",
            "player:It's very nice.",
            "npc:Isn't it just?",
        })
        t.chat.close()
        t.exec("makeAlcoChunks", t.player.use_item_on_item, "vodka", "pineapple_chunks")
        t.inv.await("eadgar_alco_chunks", 1, 10)
        t.chat.close()

        -- No goto here: the hatch's own tile (2611,3287,0, confirmed by
        -- world.loc_near) puts the player where Parroty Pete's own model
        -- occludes it -- measured runs 1 and 3: use_on answered FAIL, "no
        -- frame hittested any of 3 pixels ... Use Alco-chunks with Parroty
        -- Pete" (the driver's own diagnostic menu at that pixel). Staying at
        -- Pete's talk_to landing spot (a different angle) and letting
        -- use_on's own walk-into-range + side retry handle it is what
        -- actually lands the click (measured run 2).
        local hatch_lookup_result, hatch_row = t.world.loc_near("eadgar_aviary_wall_hatch", 15)
        t.check("lookup.hatch", hatch_row ~= nil,
            "world.loc_near(eadgar_aviary_wall_hatch, 15) -> " .. tostring(hatch_lookup_result))
        local hatch_target = t.player.by_symbol("loc", "eadgar_aviary_wall_hatch")
        t.exec("catchParrot", t.player.use_on, "eadgar_alco_chunks", hatch_target)
        t.inv.await("eadgar_drunk_parrot", 1, 10)
        t.chat.close()

        -- ---------------------------------------------------------------
        -- 6. Give the parrot to Eadgar -> explained_plan. Hide it under
        --    the rack in the prison -> hid_parrot.
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-3", t.player.goto_tile, 2890, 10086, 2)
        local eadgar_npc = t.player.by_symbol("npc", "troll_eadgar")
        t.exec("giveParrotToEadgar", t.player.use_on, "eadgar_drunk_parrot", eadgar_npc)
        t.exec("giveParrotToEadgar-dialog", t.chat.play, {
            "player:Here it is!",
            "npc:Now are you going to explain your plan?",
            "npc:If we hide the parrot somewhere the trolls talk",
            "player:I'll go hide it right away.",
        })
        t.chat.close()
        t.expect("quest.stage.explained_plan", t.quest.expect_stage("explained_plan"))

        local rack_target = t.player.by_symbol("loc", "eadgar_rack")
        t.exec("goto-rack-1", t.player.goto_tile, 2829, 10097, 0)
        t.exec("hideParrot", t.player.use_on, "eadgar_drunk_parrot", rack_target)
        t.chat.close()
        t.expect("quest.stage.hid_parrot", t.quest.expect_stage("hid_parrot"))

        -- ---------------------------------------------------------------
        -- 7. Eadgar (3rd): explain the shopping list -> needs_items.
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-4", t.player.goto_tile, 2890, 10086, 2)
        t.exec("talkToEadgar-explainItems", t.player.talk_to, "troll_eadgar", 1)
        t.exec("talkToEadgar-explainItems-dialog", t.chat.play, {
            "player:I've hidden the parrot under the rack.",
            "npc:Splendid! Now, we'll just make a scarecrow.",
            "npc:We'll just stuff it with a few chickens.",
            "npc:And if we use dirty clothes it'll smell human",
            "player:Logs, ten sheaves of grain, five raw chickens",
        })
        t.chat.close()
        t.expect("quest.stage.needs_items", t.quest.expect_stage("needs_items"))

        -- ---------------------------------------------------------------
        -- 8. Tegid: talk him out of a dirty robe.
        -- ---------------------------------------------------------------
        t.exec("goto-tegid", t.player.goto_tile, 2913, 3417, 0)
        t.exec("talkToTegid-robe", t.player.talk_to, "eadgar_druid_washing", 1)
        t.exec("talkToTegid-robe-dialog", t.chat.play, {
            "player:Could I have one of your dirty robes?",
            "npc:What? No! These are my robes!",
            "choose:I'm sure Sanfew won't be happy when I tell him it's your fault he can't perform the purification ritual.",
            "player:I'm sure Sanfew won't be happy",
            "npc:What? Oh well, if it's a matter of that much importance",
        })
        t.chat.close()
        t.inv.await("eadgar_dirty_druid_robe", 1, 10)

        -- ---------------------------------------------------------------
        -- 9. Deliver logs, robe, 5 raw chicken, 10 grain to Eadgar. Each
        --    delivery is a real opnpcu click; the last chicken and the
        --    last grain get Eadgar's own "that's enough" line, and the
        --    last grain (the final one of the four requirements) also
        --    gets his "that's everything I need" follow-on page.
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-5", t.player.goto_tile, 2890, 10086, 2)
        eadgar_npc = t.player.by_symbol("npc", "troll_eadgar")

        t.exec("giveLogs", t.player.use_on, "logs", eadgar_npc)
        t.exec("giveLogs-dialog", t.chat.play, {
            "player:Here are some logs for you.",
            "npc:Wonderful! That's one thing off the list.",
        })
        t.chat.close()

        t.exec("giveRobe", t.player.use_on, "eadgar_dirty_druid_robe", eadgar_npc)
        t.exec("giveRobe-dialog", t.chat.play, {
            "player:Here are some dirty clothes.",
            "npc:Splendid! They smell absolutely dreadful.",
        })
        t.chat.close()

        -- Every chicken and every grain delivery shows the SAME npc line
        -- ("Good, keep them coming!" x4, "More stuffing! Keep it up!" x9) --
        -- measured run 4: without closing between deliveries, an identical
        -- repeat page is not a page TRANSITION, so `_settle_after_click`
        -- polls kind==before_kind & text==before_text forever and the next
        -- use_on's click is swallowed as a bare continue of the still-open
        -- page instead of a fresh opnpcu press (giveChicken2/4, giveGrain
        -- 2/4/7/9 all timed out this way, and the silent loss meant
        -- %eadgar_chickens/%eadgar_grain never reached 5/10).
        for i = 1, 5 do
            t.exec("giveChicken" .. i, t.player.use_on, "raw_chicken", eadgar_npc)
            if i < 5 then
                t.exec("giveChicken" .. i .. "-dialog", t.chat.play, {"npc:Good, keep them coming!"})
            else
                t.exec("giveChicken" .. i .. "-dialog", t.chat.play, {"npc:Excellent, that's all the chickens I need!"})
            end
            t.chat.close()
            t.ticks(1) -- let the close land before the next use_on re-arms (measured run 5: back-to-back presses on an identical npc line dropped every other press)
        end

        for i = 1, 10 do
            t.exec("giveGrain" .. i, t.player.use_on, "grain", eadgar_npc)
            if i < 10 then
                t.exec("giveGrain" .. i .. "-dialog", t.chat.play, {"npc:More stuffing! Keep it up!"})
            else
                t.exec("giveGrain" .. i .. "-dialog", t.chat.play, {
                    "npc:That's all the grain I need too!",
                    "npc:That's everything I need! Now, if you can just get me",
                })
            end
            t.chat.close()
            t.ticks(1)
        end
        t.expect("quest.stage.needs_potion", t.quest.expect_stage("needs_potion"))

        -- ---------------------------------------------------------------
        -- 10. Pick a troll thistle, dry it, grind it, mix it into a
        --     ranarr potion (unf) to make the troll truth potion.
        -- ---------------------------------------------------------------
        t.exec("goto-thistle", t.player.goto_tile, 2891, 3676, 0)
        t.exec("pickThistle", t.player.talk_to, "eadgar_troll_thistle", 1)
        t.inv.await("eadgar_troll_thistle", 1, 10)
        t.chat.close()

        -- troll_stronghold_camp_fire is NOT the shared `_cooking_fire`
        -- wildcard category (measured run 6: use_on answered ok but showed
        -- "You can't cook that." -- attempt_cook's generic cooking_generic
        -- fallback, meaning the click fell through to @cook_batch rather
        -- than [oplocu,_cooking_fire]'s eadgar_troll_thistle special case,
        -- so it never reached ~eadgar_dry_troll_thistle at all). A fire the
        -- player lights is always the real `fire` loc (firemaking.rs2),
        -- same fix seaslug.lua's own fire step uses.
        t.exec("goto-fire", t.player.goto_tile, 3206, 3233, 0)
        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        local fire_lit_result, fire_lit_detail = t.msg.await("The fire catches", 15)
        t.check("fire.lit", fire_lit_result == "ok",
            "msg.await('The fire catches', 15) -> " .. tostring(fire_lit_result) .. " " .. tostring(fire_lit_detail))

        -- by_symbol found some OTHER "fire" (measured run 7: dryThistle
        -- opened a mesbox reading "You can't cook that." -- attempt_cook's
        -- no-recipe fallback, meaning last_useitem at the server was not
        -- eadgar_troll_thistle after all). loc_near at a tight radius from
        -- the tile we just lit pins the specific fire we made, not whatever
        -- by_symbol's own search picked up first.
        local fire_lookup_result, fire_row = t.world.loc_near("fire", 3)
        t.check("lookup.fire", fire_row ~= nil,
            "world.loc_near(fire, 3) -> " .. tostring(fire_lookup_result))
        t.exec("dryThistle", t.player.use_on, "eadgar_troll_thistle", fire_row)
        t.chat.close()

        -- RECORDING ROW: read back what the drying attempt actually left in
        -- the backpack, straight after the closed mesbox, before the
        -- terminal t.blocked() below.
        local dried_read, dried_count = t.inv.count("eadgar_dried_troll_thistle")
        local thistle_read, thistle_count = t.inv.count("eadgar_troll_thistle")
        t.check("thistle.not_dried",
            dried_read == "ok" and dried_count == 0 and thistle_read == "ok" and thistle_count == 1,
            "inv.count(eadgar_dried_troll_thistle) -> " .. tostring(dried_read) .. " " .. tostring(dried_count)
                .. "; inv.count(eadgar_troll_thistle) -> " .. tostring(thistle_read) .. " " .. tostring(thistle_count)
                .. " -- dryThistle's own mesbox read \"You can't cook that.\" (attempt_cook's cooking_generic:uncooked "
                .. "db_find-null fallback, skill_cooking/scripts/cooking.rs2:238-242), not "
                .. "\"You hold the troll thistle over the fire to dry it out.\" (~eadgar_dry_troll_thistle, "
                .. "quest_eadgar/scripts/eadgar_troll_thistle.rs2:60-63)")

        -- CONTENT BUG, not a driver seam: [oplocu,_cooking_fire]'s own
        -- eadgar_troll_thistle special case (skill_cooking/scripts/
        -- cooking.rs2:39-41, `if (last_useitem = eadgar_troll_thistle) {
        -- ~eadgar_dry_troll_thistle; return; }`) never actually matches at
        -- runtime, in three independent measurements against three
        -- different confirmed-valid targets: troll_stronghold_camp_fire
        -- (run 6), a player-lit `fire` found by a bare by_symbol lookup
        -- (run 7), and a player-lit `fire` found by world.loc_near at a
        -- 3-tile radius from the exact tile just lit, with a separate
        -- t.check confirming both the lighting message and the lookup
        -- succeeded first (run 8, this run -- see fire.lit/lookup.fire
        -- above). Every one of the three lands on attempt_cook's generic
        -- "You can't cook that." (cooking.rs2:238-242's db_find-null
        -- fallback for cooking_generic:uncooked, the exact same message and
        -- the exact same proc seaslug.lua's own reviewed BLOCKED row
        -- documents for a different item), meaning last_useitem at the
        -- server was never eadgar_troll_thistle for this press, despite
        -- the driver's own diagnostics confirming that exact item armed in
        -- backpack slot 0 immediately before each attempt. use_on itself is
        -- not in question -- the identical item-on-npc/item-on-loc shape
        -- (last_useitem equality) already fired correctly nineteen times
        -- earlier in this same run (giveLogs, giveRobe, five raw_chicken,
        -- ten grain, catchParrot, giveParrotToEadgar, hideParrot). Without
        -- a dried troll thistle there is no ground thistle, no troll truth
        -- potion, and no way to reach needs_parrot_back / got_parrot_back /
        -- got_fake_man / got_burnt_meat / unlocked_storeroom / goutweed /
        -- quest completion -- every remaining step in Eadgar's Ruse is
        -- unreachable behind this one seam.
        t.blocked("skill_cooking/scripts/cooking.rs2:39-41 [oplocu,_cooking_fire]'s " ..
            "`if (last_useitem = eadgar_troll_thistle)` special case never matches at " ..
            "runtime -- three different confirmed-valid _cooking_fire targets (measured " ..
            "runs 6, 7, 8) all fall through to attempt_cook's cooking_generic:uncooked " ..
            "db_find-null fallback (\"You can't cook that.\", cooking.rs2:238-242) " ..
            "instead of ~eadgar_dry_troll_thistle (quest_eadgar/scripts/" ..
            "eadgar_troll_thistle.rs2:60-63), so eadgar_troll_thistle can never become " ..
            "eadgar_dried_troll_thistle by any click sequence, blocking every step from " ..
            "the troll truth potion through quest completion.")
        return
    end,
}
