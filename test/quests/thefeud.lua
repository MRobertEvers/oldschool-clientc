-- The Feud (thefeud), following RSProx index 4448 (part 1), 4452 (part 2),
-- 4451 (part 3) and 4453 (the end), rev 241, 2026-11-01: every stage is
-- Jagex's feud_var value at the click that wrote it. Content:
-- OSRS-Content/osrs-content/server/scripts/quests/quest_thefeud/scripts/
-- {feud_alimorrisane,feud_town,feud_gangs,feud_lessons,feud_heist,
-- feud_traitor,feud_confrontation,feud_journal}.rs2;
-- docs/rsprox-analysis/quests/the_feud.md.
--
-- Menus go through drain{stop_at="options"} + choose (verbs-chat.md): the
-- conversations are long and their page counts follow the stage.

return {
    id = "thefeud",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give leather_gloves 1",
        "::give bucket_empty 1",
        "::give coins 3000",
        "::give rune_scimitar 1",
        "::give shark 8",
        "::setlevel thieving 30", -- the cache's quest_feud row: 30, not boostable
        "::setlevel attack 70",
        "::setlevel strength 70",
        "::setlevel defence 70",
        "::setlevel hitpoints 70",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb334_feud_var",
            constants = {
                not_started = 0, started = 1, drunk_told = 2, gangs_asked = 3,
                camels_bought = 4, receipts_given = 5, joined = 6, picked_one = 7,
                urchin_distracting = 8, picked_two = 9, villager_lured = 10,
                villager_knocked_out = 11, picked_three = 12, burglary_set = 13,
                house_staked = 14, jewels_taken = 15, find_traitor = 16,
                kill_traitor = 17, hag_has_snake = 18, have_poison = 22,
                beer_poisoned = 23, see_leader = 24, menaphites_gone = 25,
                bandits_gone = 26, mayor_told = 27, complete = 28,
            },
            row = "quest_thefeud",
            display = "The Feud",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticklog.start()
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        local FIGHT_EAT = { item = "shark", below = 35 }

        -- ======================= Ali Morrisane (4448 [1054..1101])
        t.ticklog.mark("leg.start")
        t.exec("goto-ali", t.player.goto_tile, 3305, 3211, 0)
        t.exec("start", t.player.talk_to, "feud_ali_m", 1)
        t.exec("start-menu1", t.chat.drain, { stop_at = "options" })
        t.exec("start-ask", t.chat.choose, "Are you really the greatest salesman in the world?")
        t.exec("start-menu2", t.chat.drain, { stop_at = "options" })
        t.exec("start-help", t.chat.choose, "I'd like to help you, but...")
        t.exec("start-menu3", t.chat.drain, { stop_at = "options" })
        t.exec("start-yes", t.chat.choose, "Yes.")
        t.exec("start-menu4", t.chat.drain, { stop_at = "options" })
        t.exec("start-busy", t.chat.choose, "No, I'm really too busy.")
        t.exec("start-end", t.chat.drain, {})
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- The disguise from Ali's shop (4448 [1112..1138]).
        t.exec("buyDisguise", t.shop.open, "feud_ali_m", 3, "feud_morrisanes")
        t.exec("buyDisguise-turban", t.shop.buy, "feud_karidian_turban", 1)
        t.exec("buyDisguise-beard", t.shop.buy, "feud_karidian_fakebeard", 1)
        t.check("buyDisguise-close", t.shop.close())
        t.exec("makeDisguise", t.player.use_item_on_item, "feud_karidian_fakebeard", "feud_karidian_turban")
        t.expect("makeDisguise-made", t.inv.await("feud_desert_disguise", 1, 10))

        -- ======================= Shantay and the doorway (4448 [1186], [1209..1216])
        t.exec("goto-shantay", t.player.goto_tile, 3304, 3122, 0)
        t.exec("buyPass", t.player.talk_to, "shantay", 4)
        t.exec("buyPass-box", t.chat.drain, {})
        t.expect("buyPass-got", t.inv.await("shantay_pass", 1, 10))
        t.exec("doorway", t.player.click_loc, "shantay_pass_henge_doorway", 1)
        t.expect("doorway-warning", t.ui.await_open("cws_warning_10", 15))
        local wr, w = t.ui.widget("cws_warning_10:warn1")
        t.expect("doorway-warn1", wr, "cws_warning_10:warn1")
        t.check("doorway-go", select(1, t.ui.invoke(w, 1)) == "ok", "Yes, through the gate")
        t.ticks(2)
        t.expect("doorway-msg", t.msg.expect("The guard takes your Shantay Pass as you go through the gate."))
        t.expect("doorway-passTaken", t.inv.expect_absent("shantay_pass"))

        -- ======================= The carpet to Pollnivneach (4448 [1433..1504])
        t.exec("goto-rug", t.player.goto_tile, 3310, 3109, 0)
        t.exec("rug", t.player.talk_to, "magic_carpet_seller1", 1)
        t.exec("rug-menu", t.chat.drain, { stop_at = "options" })
        t.exec("rug-yes", t.chat.choose, "Yes please.")
        t.exec("rug-where", t.chat.drain, { stop_at = "options" })
        t.exec("rug-poll", t.chat.choose, "I want to travel to Pollnivneach.")
        t.exec("rug-fare", t.chat.drain, {})
        local landed_r = t.await({
            level = function()
                local r, tl = t.world.tile()
                return r == "ok" and tl.x == 3349 and tl.z == 3003 and select(2, t.drive._player_idle()) == true
            end,
            note = "landed on the north Pollnivneach pad 3349,3003",
        }, 120)
        t.expect("rug-landed", landed_r, "the carpet's landing")
        t.ticks(3)
        t.exec("rug-stepOff", t.player.walk_to, 3349, 3004, 8)

        -- ======================= Drunken Ali's three beers (4448 [2692..2774])
        t.ticklog.mark("leg.drunk")
        t.exec("goto-bar", t.player.goto_tile, 3359, 2956, 0)
        t.exec("beers", t.shop.open, "feud_ali_the_barman_multi", 3, "feud_alispub")
        t.exec("beers-buy", t.shop.buy, "beer", 3)
        t.check("beers-close", t.shop.close())
        local ali = t.player.by_symbol("npc", "feud_drunken_ali_multi")
        t.exec("beer1", t.player.use_on, "beer", ali)
        t.exec("beer1-pages", t.chat.drain, {})
        t.exec("beer2", t.player.use_on, "beer", ali)
        t.exec("beer2-pages", t.chat.drain, {})
        t.exec("beer3", t.player.use_on, "beer", ali)
        t.exec("beer3-pages", t.chat.drain, {})
        t.expect("quest.stage.drunk_told", t.quest.expect_stage("drunk_told"))

        -- ======================= Both gangs (4448 [2805], [2942])
        t.exec("goto-thug", t.player.goto_tile, 3343, 2957, 0)
        t.exec("thug", t.player.talk_to, "feud_egyptian_doorman_multi", 1)
        t.exec("thug-pages", t.chat.drain, {})
        t.exec("goto-banditCamp", t.player.goto_tile, 3364, 2986, 0)
        local bandit_r, bandit_row = t.npc.nearest("feud_arabian_guard_multi", 40)
        t.expect("find-bandit", bandit_r, "feud_arabian_guard_multi")
        if bandit_row then t.exec("goto-bandit", t.player.goto_tile, bandit_row.x, bandit_row.z - 1, 0) end
        t.exec("bandit", t.player.talk_to, "feud_arabian_guard_multi", 1)
        t.exec("bandit-pages", t.chat.drain, {})
        t.expect("quest.stage.gangs_asked", t.quest.expect_stage("gangs_asked"))

        -- ======================= Two camels for 1,000 (4448 [2978..3037])
        t.exec("goto-camelman", t.player.goto_tile, 3350, 2965, 0)
        t.exec("camels", t.player.talk_to, "feud_ali_the_discount_camel_seller_multi", 1)
        t.exec("camels-m1", t.chat.drain, { stop_at = "options" })
        t.exec("camels-forSale", t.chat.choose, "Are those camels around the side for sale?")
        t.exec("camels-m2", t.chat.drain, { stop_at = "options" })
        t.exec("camels-price", t.chat.choose, "What price do you want for both of them?")
        t.exec("camels-m3", t.chat.drain, { stop_at = "options" })
        t.exec("camels-1000", t.chat.choose, "Would 1000 gold coins for the pair of them do?")
        t.exec("camels-m4", t.chat.drain, { stop_at = "options" })
        t.exec("camels-done", t.chat.choose, "No, but thanks for your time.")
        t.exec("camels-end", t.chat.drain, {})
        t.expect("quest.stage.camels_bought", t.quest.expect_stage("camels_bought"))
        t.expect("camels-receipts", t.inv.await("feud_camel_receipt", 2, 5))

        -- ======================= The receipts (4448 [3183], [3237])
        t.exec("goto-thug2", t.player.goto_tile, 3343, 2957, 0)
        t.exec("thugReceipt", t.player.talk_to, "feud_egyptian_doorman_multi", 1)
        t.exec("thugReceipt-pages", t.chat.drain, {})
        t.exec("goto-banditCamp2", t.player.goto_tile, 3364, 2986, 0)
        local bandit2_r, bandit2_row = t.npc.nearest("feud_arabian_guard_multi", 40)
        t.expect("find-bandit2", bandit2_r, "feud_arabian_guard_multi")
        if bandit2_row then t.exec("goto-bandit2", t.player.goto_tile, bandit2_row.x, bandit2_row.z - 1, 0) end
        t.exec("banditReceipt", t.player.talk_to, "feud_arabian_guard_multi", 1)
        t.exec("banditReceipt-pages", t.chat.drain, {})
        t.expect("quest.stage.receipts_given", t.quest.expect_stage("receipts_given"))

        -- ======================= Joining (4448 [3363..3377])
        t.exec("goto-operator", t.player.goto_tile, 3334, 2951, 0)
        t.exec("join", t.player.talk_to, "feud_egyptian_minder_multi", 1)
        t.exec("join-m1", t.chat.drain, { stop_at = "options" })
        t.exec("join-yes", t.chat.choose, "Yes, of course, those bandits should be taught a lesson.")
        t.exec("join-m2", t.chat.drain, { stop_at = "options" })
        t.exec("join-done", t.chat.choose, "No thanks.")
        t.expect("quest.stage.joined", t.quest.expect_stage("joined"))

        -- ======================= The first trial (4452 [42..587])
        t.ticklog.mark("leg.trial")
        t.exec("goto-square", t.player.goto_tile, 3348, 2960, 0)
        local pocket1_r, pocket1_row = t.npc.nearest("feud_villager_multi_2", 40)
        t.expect("find-pocket1", pocket1_r, "feud_villager_multi_2")
        if pocket1_row then t.exec("goto-pocket1", t.player.goto_tile, pocket1_row.x, pocket1_row.z - 1, 0) end
        t.exec("pocket1", t.player.talk_to, "feud_villager_multi_2", 3)
        t.ticks(6)
        t.expect("quest.stage.picked_one", t.quest.expect_stage("picked_one"))
        t.exec("goto-urchin", t.player.goto_tile, 3359, 2973, 0)
        t.exec("urchin", t.player.talk_to, "feud_street_urchin", 1)
        t.exec("urchin-pages", t.chat.drain, {})
        t.expect("quest.stage.urchin_distracting", t.quest.expect_stage("urchin_distracting"))
        local mark_a, mark_b = t.var.server("varb329_feud_closest_villager")
        local mark = tonumber(mark_b) or tonumber(mark_a)
        t.check("urchin-mark", mark ~= nil and mark >= 1 and mark <= 3, "the urchin's mark: villager " .. tostring(mark))
        t.exec("pocket2", t.player.talk_to, "feud_villager_multi_" .. tostring(mark or 2), 3)
        t.ticks(6)
        t.expect("quest.stage.picked_two", t.quest.expect_stage("picked_two"))
        t.exec("goto-operator2", t.player.goto_tile, 3334, 2951, 0)
        t.exec("blackjack", t.player.talk_to, "feud_egyptian_minder_multi", 1)
        t.exec("blackjack-m1", t.chat.drain, { stop_at = "options" })
        t.exec("blackjack-advice", t.chat.choose, "Yeah, I could do with a bit of advice.")
        t.exec("blackjack-end", t.chat.drain, {})
        t.expect("blackjack-got", t.inv.await("blackjack_oak", 1, 5))
        t.exec("wieldBlackjack", t.player.equip, "blackjack_oak")
        t.exec("goto-square2", t.player.goto_tile, 3352, 2966, 0)
        local lure_r, lure_row = t.npc.nearest("feud_villager_multi_1", 40)
        t.expect("find-lure", lure_r, "feud_villager_multi_1")
        if lure_row then t.exec("goto-lure", t.player.goto_tile, lure_row.x, lure_row.z - 1, 0) end
        t.exec("lure", t.player.talk_to, "feud_villager_multi_1", 4)
        t.exec("lure-pages", t.chat.drain, {})
        t.expect("quest.stage.villager_lured", t.quest.expect_stage("villager_lured"))
        t.ticks(2)
        t.exec("knockout", t.player.talk_to, "feud_villager_multi_1", 5)
        t.ticks(3)
        t.expect("quest.stage.villager_knocked_out", t.quest.expect_stage("villager_knocked_out"))
        t.exec("pocket3", t.player.talk_to, "feud_villager_multi_1", 3)
        t.ticks(4)
        t.expect("quest.stage.picked_three", t.quest.expect_stage("picked_three"))
        t.exec("goto-operator3", t.player.goto_tile, 3334, 2951, 0)
        t.exec("burglary", t.player.talk_to, "feud_egyptian_minder_multi", 1)
        t.exec("burglary-pages", t.chat.drain, {})
        t.expect("quest.stage.burglary_set", t.quest.expect_stage("burglary_set"))
        t.expect("burglary-keys", t.inv.await("feud_mayors_house_keys", 1, 5))

        -- ======================= The burglary (4452 [997..1005]; 4451 [48..586])
        t.ticklog.mark("leg.burglary")
        t.exec("wieldScimitar", t.player.equip, "rune_scimitar")
        t.exec("wearDisguise", t.player.equip, "feud_desert_disguise")
        t.exec("wearGloves", t.player.equip, "leather_gloves")
        t.exec("goto-cactus", t.player.goto_tile, 3365, 2967, 0)
        t.exec("stakeout", t.player.click_loc, "feud_cactus_row", 1)
        t.ticks(8)
        t.exec("stakeout-box", t.chat.drain, {})
        t.expect("quest.stage.house_staked", t.quest.expect_stage("house_staked"))
        t.exec("goto-door", t.player.goto_tile, 3369, 2970, 0)
        local door = t.player.by_symbol("loc", "feud_closed_door_right")
        t.exec("unlock", t.player.use_on, "feud_mayors_house_keys", door)
        t.expect("unlock-msg", t.msg.expect("You hear a satisfying click as the door unlocks."))
        t.ticks(3)
        t.exec("desk", t.player.click_loc, "feud_mayors_desk", 1)
        t.exec("desk-box", t.chat.drain, {})
        t.expect("desk-note", t.inv.await("feud_nos_note", 1, 5))
        t.exec("upstairs", t.player.climb, { loc = "feud_insidestairs_base", op = 1, op_name = "Climb-up",
            at = { 3373, 2978, 0 }, dest = { 3374, 2979, 1 }, slack = 1 })
        t.exec("bed", t.player.click_loc, "feud_mayors_bed", 1)
        t.exec("bed-box", t.chat.drain, {})
        t.expect("bed-note", t.inv.await("feud_fib_hint", 1, 5))
        t.exec("picture", t.player.click_loc, "feud_mayors_picture", 1)
        t.exec("picture-box", t.chat.drain, {})
        t.expect("safe-open", t.ui.await_open("the_feud_safe", 10))
        local SAFE = { [1] = "feud_over_model1", [2] = "feud_overstate_model2", [3] = "feud_overstate_model3",
            [5] = "feud_overstate_model5", [8] = "feud_overstate_model8" }
        local code = { 1, 1, 2, 3, 5, 8 }
        for i = 1, #code do
            local sr, sw = t.ui.widget("the_feud_safe:" .. SAFE[code[i]])
            t.expect("safe-digit" .. i .. "-widget", sr, tostring(code[i]))
            t.check("safe-digit" .. i, select(1, t.ui.invoke(sw, 1)) == "ok", "digit " .. tostring(code[i]))
            t.ticks(3)
        end
        t.expect("safe-jewels", t.inv.await("feud_mayors_jewels", 1, 15))
        t.expect("quest.stage.jewels_taken", t.quest.expect_stage("jewels_taken"))
        t.exec("safe-box", t.chat.drain, {})
        t.exec("downstairs", t.player.climb, { loc = "feud_insidestairs_top", op = 1, op_name = "Climb-down",
            at = { 3373, 2978, 1 }, dest = { 3373, 2977, 0 }, slack = 1 })
        t.exec("goto-doorOut", t.player.goto_tile, 3371, 2971, 0)
        t.exec("doorOut", t.player.click_loc, "feud_closed_door_left", 1)
        t.ticks(3)
        t.exec("goto-operator4", t.player.goto_tile, 3334, 2951, 0)
        t.exec("jewels", t.player.talk_to, "feud_egyptian_minder_multi", 1)
        t.exec("jewels-pages", t.chat.drain, {})
        t.expect("quest.stage.find_traitor", t.quest.expect_stage("find_traitor"))

        -- ======================= The traitor (4451 [612..2811])
        t.ticklog.mark("leg.traitor")
        t.exec("goto-thug3", t.player.goto_tile, 3343, 2957, 0)
        t.exec("traitorName", t.player.talk_to, "feud_egyptian_doorman_multi", 1)
        t.exec("traitorName-pages", t.chat.drain, {})
        t.exec("goto-operator5", t.player.goto_tile, 3334, 2951, 0)
        t.exec("traitorTold", t.player.talk_to, "feud_egyptian_minder_multi", 1)
        t.exec("traitorTold-m", t.chat.drain, { stop_at = "options" })
        t.exec("traitorTold-done", t.chat.choose, "No thanks.")
        t.expect("quest.stage.kill_traitor", t.quest.expect_stage("kill_traitor"))
        t.exec("goto-hag", t.player.goto_tile, 3346, 2985, 0)
        t.exec("hagMeet", t.player.talk_to, "feud_hag_multi", 1)
        t.exec("hagMeet-pages", t.chat.drain, {})
        t.exec("hagList", t.player.talk_to, "feud_hag_multi", 1)
        t.exec("hagList-pages", t.chat.drain, {})
        t.exec("goto-pot", t.player.goto_tile, 3355, 2951, 0)
        local pot = t.player.by_symbol("loc", "feud_money_bowl")
        t.exec("pot", t.player.use_on, "coins", pot)
        t.exec("pot-pages", t.chat.drain, {})
        t.expect("pot-flute", t.inv.await("snake_flute", 1, 5))
        t.exec("goto-snake", t.player.goto_tile, 3331, 2958, 0)
        local snake = t.player.by_symbol("npc", "feud_desert_snake")
        t.exec("charm", t.player.use_on, "snake_flute", snake)
        t.expect("charm-basket", t.inv.await("basket_with_snake", 1, 20))
        t.exec("charm-box", t.chat.drain, {})
        t.exec("goto-hag2", t.player.goto_tile, 3346, 2985, 0)
        t.exec("hagSnake", t.player.talk_to, "feud_hag_multi", 1)
        t.exec("hagSnake-pages", t.chat.drain, {})
        t.expect("quest.stage.hag_has_snake", t.quest.expect_stage("hag_has_snake"))
        t.exec("goto-kebab", t.player.goto_tile, 3352, 2972, 0)
        t.exec("kebab", t.player.talk_to, "feud_kebabman_multi", 1)
        t.exec("kebab-m1", t.chat.drain, { stop_at = "options" })
        t.exec("kebab-sauce", t.chat.choose, "Would you sell me that bottle of special kebab sauce?")
        t.exec("kebab-m2", t.chat.drain, { stop_at = "options" })
        t.exec("kebab-done", t.chat.choose, "No thanks, I'm good.")
        t.exec("kebab-end", t.chat.drain, {})
        t.expect("kebab-sauceGot", t.inv.await("superhot_kebab_sauce", 1, 5))
        t.exec("goto-trough", t.player.goto_tile, 3342, 2960, 0)
        local trough = t.player.by_symbol("loc", "feud_foodtrough2")
        t.exec("trough", t.player.use_on, "superhot_kebab_sauce", trough)
        t.expect("trough-msg", t.msg.expect("You pour some hot kebab sauce in the trough."))
        t.ticks(26) -- the camel's dung, 22 ticks after it eats (4451 [2297] -> [2319])
        local dung = t.player.by_symbol("loc", "feud_ugthanki_pooh")
        t.exec("scoop", t.player.use_on, "bucket_empty", dung)
        t.expect("scoop-bucket", t.inv.await("feud_camel_pooh_bucket", 1, 10))
        t.exec("scoop-box", t.chat.drain, {})
        t.exec("goto-hag3", t.player.goto_tile, 3346, 2985, 0)
        t.exec("hagDung", t.player.talk_to, "feud_hag_multi", 1)
        t.exec("hagDung-pages", t.chat.drain, {})
        t.expect("quest.stage.have_poison", t.quest.expect_stage("have_poison"))
        t.exec("goto-table", t.player.goto_tile, 3357, 2957, 0)
        local beer_table = t.player.by_symbol("loc", "feud_poison_beer_table")
        t.exec("poison", t.player.use_on, "poison_from_hag", beer_table)
        t.expect("poison-msg", t.msg.expect("You poison the drink."))
        t.expect("quest.stage.beer_poisoned", t.quest.expect_stage("beer_poisoned"))
        t.exec("goto-operator6", t.player.goto_tile, 3334, 2951, 0)
        t.exec("deedDone", t.player.talk_to, "feud_egyptian_minder_multi", 1)
        t.exec("deedDone-pages", t.chat.drain, {})
        t.expect("quest.stage.see_leader", t.quest.expect_stage("see_leader"))

        -- ======================= The Menaphite Leader and his Tough Guy (4451 [2917..3095])
        t.ticklog.mark("leg.menaphites")
        t.exec("goto-leader", t.player.goto_tile, 3334, 2954, 0)
        t.exec("leader", t.player.talk_to, "feud_menap_boss", 1)
        t.exec("leader-pages", t.chat.drain, {})
        t.exec("toughGuy", t.player.attack, "feud_menap_toughguy", 2, 15, { eat = FIGHT_EAT })
        t.exec("toughGuy-dead", t.npc.await_dead_engaged, 120, 10, { eat = FIGHT_EAT })
        t.expect("quest.stage.menaphites_gone", t.quest.expect_stage("menaphites_gone"))
        t.ticks(12)
        t.exec("toughGuy-line", t.chat.drain, {})
        t.exec("goto-villager", t.player.goto_tile, 3343, 2955, 0)
        t.exec("villagerBalance", t.player.talk_to, "feud_villager_multi_2", 1)
        t.exec("villagerBalance-m", t.chat.drain, { stop_at = "options" })
        t.exec("villagerBalance-ask", t.chat.choose, "What do you mean by 'fix the mess I created'?")
        t.exec("villagerBalance-end", t.chat.drain, {})

        -- ======================= The Bandit Leader and his champion (4451 [3267..3388])
        t.ticklog.mark("leg.bandits")
        t.exec("goto-banditLeader", t.player.goto_tile, 3353, 3000, 0)
        t.exec("banditLeader", t.player.talk_to, "feud_bandit_boss", 1)
        t.exec("banditLeader-pages", t.chat.drain, {})
        t.exec("champion", t.player.attack, "feud_bandit_toughguy", 2, 15, { eat = FIGHT_EAT })
        t.exec("champion-dead", t.npc.await_dead_engaged, 120, 10, { eat = FIGHT_EAT })
        t.expect("quest.stage.bandits_gone", t.quest.expect_stage("bandits_gone"))
        t.exec("champion-pages", t.chat.drain, {})
        t.exec("goto-villager2", t.player.goto_tile, 3343, 2955, 0)
        t.exec("villagerUngrateful", t.player.talk_to, "feud_villager_multi_2", 1)
        t.exec("villagerUngrateful-pages", t.chat.drain, {})
        t.exec("goto-mayor", t.player.goto_tile, 3359, 2970, 0)
        t.exec("mayor", t.player.talk_to, "feud_mayor", 1)
        t.exec("mayor-pages", t.chat.drain, {})
        t.expect("quest.stage.mayor_told", t.quest.expect_stage("mayor_told"))

        -- ======================= Home, and Ali Morrisane (4453 [290..373])
        t.ticklog.mark("leg.end")
        t.exec("goto-rugNorth", t.player.goto_tile, 3350, 3003, 0)
        t.exec("rugHome", t.player.talk_to, "magic_carpet_seller4", 1)
        t.exec("rugHome-m1", t.chat.drain, { stop_at = "options" })
        t.exec("rugHome-yes", t.chat.choose, "Yes please.")
        t.exec("rugHome-m2", t.chat.drain, { stop_at = "options" })
        t.exec("rugHome-go", t.chat.choose, "Take me to the Pass then.")
        t.exec("rugHome-end", t.chat.drain, {})
        local home_r = t.await({
            level = function()
                local r, tl = t.world.tile()
                return r == "ok" and tl.z > 3095 and tl.z < 3115 and select(2, t.drive._player_idle()) == true
            end,
            note = "landed at the Shantay Pass pad",
        }, 120)
        t.expect("rugHome-landed", home_r, "the carpet's landing at the Shantay Pass")
        t.ticks(3)
        t.exec("goto-shantaySouth", t.player.goto_tile, 3303, 3113, 0)
        t.exec("leaveDesert", t.player.click_loc, "shantay_pass_henge_doorway", 1)
        t.ticks(4)
        local snap_r, snap = t.skill.snapshot()
        t.exec("goto-ali2", t.player.goto_tile, 3305, 3211, 0)
        t.exec("finish", t.player.talk_to, "feud_ali_m", 1)
        t.exec("finish-m", t.chat.drain, { stop_at = "options" })
        t.exec("finish-busy", t.chat.choose, "No, I'm really too busy.")
        t.exec("finish-end", t.chat.play, { "player:No, I'm really too busy." })
        t.ticks(3)
        t.quest.expect_complete()
        local xp_r, xp_d = t.skill.expect_gain("thieving", 15000, snap)
        t.check("reward.thievingXp", snap_r == "ok" and xp_r == "ok", tostring(xp_d))
        t.finish(0)
        return
    end,
}
