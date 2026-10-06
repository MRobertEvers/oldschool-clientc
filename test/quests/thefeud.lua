-- The Feud (thefeud). Driven end to end through real clicks and dialogue:
-- Ali Morrisane's offer -> the Shantay Pass gate -> the magic carpet flight
-- to Pollnivneach -> questioning both gangs -> buying camels and handing
-- out receipts -> Ali the Operator's recruitment and three pickpocket
-- proving tasks -> the mayor's-house jewel heist (disguise, door, desk,
-- bed, the Fibonacci safe dial) -> hunting the traitor (barman, kebab
-- sauce, camel dung, the snake-charm minigame, the Hag's poison, the
-- poisoned beer) -> the Menaphite Leader/Tough Guy and Bandit
-- Leader/Bandit champion fights -> Ali the Mayor's reveal -> the carpet
-- back to the Shantay Pass and north through its doorway -> Ali
-- Morrisane's reward.
--
-- Door rule (b67 re-drive): the Shantay Pass doorway is pressed by its op
-- both ways (cross_gate), the mayor's house is entered and left by its door
-- (pass_door feud_closed_door_left 3370,2971) and its stairs are climbed
-- (t.player.climb on the maplink rows); every goto lands on an open tile
-- (reach.py: all REACH closed-doors, none solid). No dialogue on the route
-- branches on combat level (grep combat_level over quest_thefeud, shantay*,
-- magic_carpet*), so the staged 99 melee stats see the only branch there is. Content source for every symbol/text/stage number:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_thefeud/
--   scripts/{feud_alimorrisane,feud_recruitment,feud_heist,feud_traitor,
--   feud_confrontation,feud_villagers}.rs2, configs/quest_thefeud.constant,
-- OSRS-Content/osrs239-content/server/scripts/areas/area_alkharid/scripts/
--   shantay.rs2, shantay_pass.rs2; areas/area_desert/scripts/
--   magic_carpet.rs2, magic_carpet_talk.rs2; shop/pollnivneach/scripts/
--   the_asp_snake_bar.rs2; quest_ratcatchers/scripts/ratcatchers.rs2
--   (feud_money_bowl's real trigger). docs/quests/the_feud.md.
--
-- seam27 (feud_four_legs): the desert disguise is bought from Ali's Discount
-- Wares and made by using the headpiece on the fake beard; the third
-- pickpocket lesson is a real Lure / Knock-Out / Pickpocket; Ali the Hag takes
-- the snake and the dung in separate hand-ins and gives the vial of Hag's
-- poison (Transcript:The_Feud oldid 15325427; wiki Desert_disguise).

return {
    id = "thefeud",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::give leather_gloves 1", -- wiki: gloves that work at the cactus
        "::give bucket_empty 1", -- wiki: a bucket, for the camel dung
        "::give coins 1000", -- wiki: 501+ coins (carpet fare 200, shantay pass 5, beer 30, camels 500, urchin 10, money pot 1)
        "::give rune_scimitar 1",
        "::give shark 5",
        "::setlevel thieving 30", -- wiki: 30 Thieving, not boostable (dbrow requirement_stats)
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb334_feud_var",
            constants = {
                accepted = 1,
                bandit_beaten = 26,
                barman_done = 18,
                beer_poisoned = 23,
                camel_cost = 500,
                camels_bought = 4,
                complete = 28,
                drunken_ali_done = 2,
                dung_bucketed = 20,
                gangs_questioned = 3,
                heist_briefed = 10,
                house_entered = 11,
                jewels_delivered = 15,
                mayor_talked = 27,
                menaphite_beaten = 25,
                not_started = 0,
                note_fib = 13,
                note_numbers = 12,
                operator_joined = 6,
                pickpocket1_done = 7,
                pickpocket2_done = 8,
                pickpocket3_done = 9,
                poison_made = 22,
                ready_confront = 24,
                receipts_given = 5,
                req_thieving = 30,
                reward_coins = 500,
                reward_thieving_xp = 150000,
                safe_opened = 14,
                sauce_bought = 19,
                snake_done = 21,
                thug_questioned = 17,
                traitor_briefed = 16,
            },
            row = "quest_thefeud",
            display = "The Feud",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Fights: sharks eaten inside the attack press and the kill wait below
        -- 60 hp (verbs-combat.md "Eating inside an attack press"). A fight's
        -- margin (brief): the lowest hp either eater read is at least a quarter
        -- of the maximum AND at least one shark is left.
        local FIGHT_EAT = { item = "shark", below = 60 }
        local function fight_margin(name, fight, attack_detail, dead_detail, food_before)
            local lows = {}
            for _, d in ipairs({ tostring(attack_detail), tostring(dead_detail) }) do
                local low = tonumber(d:match("lowest hp (%d+)/"))
                if low then lows[#lows + 1] = low end
            end
            local lowest = nil
            for _, v in ipairs(lows) do
                if lowest == nil or v < lowest then lowest = v end
            end
            local hp_r, hp = t.skill.read("hitpoints")
            local max_hp = (hp_r == "ok" and type(hp) == "table") and hp.base_level or nil
            local food_r, food_left = t.inv.count("shark")
            t.check(name, lowest ~= nil and max_hp ~= nil and food_r == "ok"
                and lowest * 4 >= max_hp and food_left >= 1,
                fight .. ": lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp)
                .. " (from " .. #lows .. " eater reading(s)), sharks " .. tostring(food_before)
                .. " -> " .. tostring(food_left)
                .. " (margin: lowest hp >= a quarter of max AND at least one shark left)")
        end

        local equip_scim_r, equip_scim_d = t.player.equip("rune_scimitar")
        t.step("equipScimitar", equip_scim_r == "ok" and "PASS" or "FAIL", tostring(equip_scim_d))

        -- getBucket: a bring-along item (wiki getItemRequirements), staged
        -- in setup, never gathered in-quest -- read it back here.
        local bucket_r, bucket_has = t.inv.has("bucket_empty")
        t.check("getBucket", bucket_r == "ok" and bucket_has == true,
            "bucket_empty has=" .. tostring(bucket_has) .. " (brought along, ::give in setup)")

        -- ==================== startQuest: Ali Morrisane's offer =============
        t.exec("goto-startQuest", t.player.goto_tile, 3304, 3211, 0)
        t.exec("startQuest", t.player.talk_to, "feud_ali_m", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "npc:Oh, adventurer -- might I trouble you",
            "choose:What's the matter?",
            "player:What's the matter?",
            "npc:My nephew went to Pollnivneach",
            "npc:If you are, then why are you s",
            "choose:I'd like to help you find your nephew.",
            "player:I'd like to help you find your",
            "npc:Thank you! Head south to Polln",
            "npc:town's in such an uproar.",
            "choose:Yes, I'll head there now.",
            "player:Yes, I'll head there now.",
            "npc:Take this -- a Kharidian headp",
        })
        t.expect("quest.stage.accepted", t.quest.expect_stage("accepted"))

        -- ==================== buyDisguiseGear / createDisguise ================
        -- Wiki The_Feud (oldid 15315438): "buy the Kharidian headpiece and fake
        -- beard from Ali Morrisane"; Desert_disguise: made "by combining a fake
        -- beard and a Kharidian headpiece". Ali's Discount Wares is his Trade op
        -- (shop/al_kharid/scripts/alis_discount_wares__1.rs2, inv feud_morrisanes).
        t.exec("buyDisguiseGear", t.shop.open, "feud_ali_m", 3, "feud_morrisanes")
        t.exec("buyDisguiseGear-beard", t.shop.buy, "feud_karidian_fakebeard", 1)
        t.exec("buyDisguiseGear-headpiece", t.shop.buy, "feud_karidian_turban", 1)
        t.check("buyDisguiseGear-close", t.shop.close())
        local beard_r, beard_n = t.inv.count("feud_karidian_fakebeard")
        local turban_r, turban_n = t.inv.count("feud_karidian_turban")
        t.check("buyDisguiseGear-verify", beard_r == "ok" and beard_n == 1 and turban_r == "ok" and turban_n == 1,
            "feud_karidian_fakebeard=" .. tostring(beard_n) .. " feud_karidian_turban=" .. tostring(turban_n))
        t.exec("createDisguise", t.player.use_item_on_item, "feud_karidian_turban", "feud_karidian_fakebeard")
        t.expect("createDisguise-made", t.inv.await("feud_desert_disguise", 1, 10))
        local left_r, left_n = t.inv.count("feud_karidian_turban")
        local leftb_r, leftb_n = t.inv.count("feud_karidian_fakebeard")
        t.check("createDisguise-consumed", left_r == "ok" and left_n == 0 and leftb_r == "ok" and leftb_n == 0,
            "after the combine: feud_karidian_turban=" .. tostring(left_n) .. " feud_karidian_fakebeard=" .. tostring(leftb_n))

        -- ==================== buyShantayPass =================================
        t.exec("goto-buyShantayPass", t.player.goto_tile, 3304, 3123, 0)
        t.exec("buyShantayPass", t.player.talk_to, "shantay", 1)
        t.exec("buyShantayPass-dialog", t.chat.play, {
            "npc:Hello effendi, I am Shantay.",
            "npc:I see you're new. Please read",
            "choose:I want to buy a shantay pass for 5 gold coins.",
            "player:I want to buy a shantay pass for",
            "mesbox:You purchase a Shantay Pass.",
        })
        local pass_r, pass_has = t.inv.has("shantay_pass")
        t.check("buyShantayPass-verify", pass_r == "ok" and pass_has == true, "shantay_pass has=" .. tostring(pass_has))

        -- ==================== goToShantay: through the gate ===================
        -- shantay_pass_henge_doorway (3302,3116) is the desert's only way in on
        -- foot (b64-seam1: a blocking crossing loc is judged like an only-way
        -- gate), so it is crossed by its own op on every trip. From the north
        -- its oploc1 reads the poster, takes the pass, hands over the
        -- disclaimer, then [queue,shantay_pass_enter] carries the player south
        -- (shantay_pass.rs2 [oploc1,shantay_pass_henge_doorway]). cross_gate
        -- walks to the north tile 3304,3118 itself (no goto) and is graded on
        -- the player standing south of the doorway afterwards.
        local pass_before_r, pass_before_n = t.inv.count("shantay_pass")
        t.exec("goToShantay", t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 },
            near = { 3304, 3118 }, far_ok = function(tile) return tile.z <= 3115 end,
            far_desc = "south of the Shantay Pass doorway, z <= 3115",
            chat = {
                "mesbox:There is a large poster on the wall",
                "mesbox:The Desert is a VERY Dangerous place",
                "mesbox:That seems pretty scary!",
                "choose:Yeah, that poster doesn't scare me!",
                "npc:Can I see your Shantay Desert Pass",
                "mesbox:You hand over a Shantay Pass.",
                "player:Sure, here you go!",
                "npc:Here, have a disclaimer",
            } })
        local pass_after_r, pass_after_n = t.inv.count("shantay_pass")
        t.check("goToShantay-passHandedOver", pass_before_r == "ok" and pass_after_r == "ok"
            and pass_before_n == 1 and pass_after_n == 0,
            "shantay_pass " .. tostring(pass_before_n) .. " -> " .. tostring(pass_after_n)
            .. " across the doorway (handed over, shantay_pass.rs2)")

        -- ==================== talkToRugMerchant: carpet flight to Pollnivneach
        t.exec("goto-talkToRugMerchant", t.player.goto_tile, 3311, 3109, 0)
        t.exec("talkToRugMerchant", t.player.talk_to, "magic_carpet_seller1", 1)
        t.exec("talkToRugMerchant-dialog", t.chat.play, {
            "player:Hello.",
            "npc:*",
            "choose:Yes please.",
            "player:Yes please.",
            "npc:*", -- carpet_menu_shantay's first hub line (Uzer-open or not)
            "npc:*", -- its second hub line, always printed too
            "choose:I want to travel to Pollnivneach.",
            "player:I want to travel to Pollnivneach.",
        })
        -- seam24: the flight is ~110 tiles at 3 tiles/tick plus boarding and
        -- banks (magic_carpet.rs2 ~carpet_ride, magic_carpet.constant route 1
        -- ends on ^carpet_pad_npoll 3349,3003) -- t.ticks(20) ended mid-air and
        -- the ::goto that followed teleported a player_lock'd rider whose
        -- ~carpet_glide then flew him back to the pad. Await the landing.
        local landed_r, landed_d = t.await({
            level = function()
                local r, tl = t.world.tile()
                return r == "ok" and tl.x == 3349 and tl.z == 3003 and select(2, t.drive._player_idle()) == true
            end,
            note = "carpet landed on the north Pollnivneach pad 3349,3003",
        }, 80)
        local land_tr, land_tile = t.world.tile()
        t.check("carpet-landed", landed_r == "ok" and land_tr == "ok" and land_tile.x == 3349
            and land_tile.z == 3003 and land_tile.level == 0,
            "carpet await " .. tostring(landed_r) .. " (landing tile reached and player idle)"
            .. "; rider at " .. tostring(land_tile and land_tile.x) .. "," .. tostring(land_tile and land_tile.z)
            .. "," .. tostring(land_tile and land_tile.level) .. " (the north Pollnivneach pad is 3349,3003,0)")
        t.ticks(3) -- ~carpet_ride's carpet_land + p_delay(1) + player_unlock
        -- The pad (carpet_rolledout_multi 3349,3003) is a solid loc the ride
        -- telejumps onto: step off it onto open ground before travelling on.
        t.exec("carpet-stepOff", t.player.walk_to, 3349, 3004, 8)
        -- The Asp & Snake bar has open arches (desertwall_arch_l/r at
        -- 3356-3359,2953/2958, no door loc): land on an open floor tile inside
        -- it, beside the table at 3360,2956 (reach.py: REACH closed-doors).
        t.exec("goto-buyBeers", t.player.goto_tile, 3359, 2956, 0)
        t.exec("buyBeers-open", t.shop.open, "feud_ali_the_barman", 3, "feud_alispub")
        t.exec("buyBeers", t.shop.buy, "beer", 3)
        t.check("buyBeers-close", t.shop.close())
        local br, bn = t.inv.count("beer")
        t.check("buyBeers-verify", br == "ok" and bn == 3, "beer count=" .. tostring(bn))
        local ali = t.player.by_symbol("npc", "feud_drunken_ali")
        for i = 1, 3 do
            -- feud_recruitment.rs2 [opnpcu,feud_drunken_ali]: each beer is
            -- inv_del'd and %feud_var_drink steps 0 -> 1 -> 2 -> 3.
            local beers_before_r, beers_before = t.inv.count("beer")
            t.exec("drunkenAli-beer" .. i, t.player.use_on, "beer", ali)
            local pages = { "player:*", "npc:*" }
            if i == 3 then pages[3] = "npc:*" end
            t.exec("drunkenAli-beer" .. i .. "-dialog", t.chat.play, pages)
            local beers_after_r, beers_after = t.inv.count("beer")
            t.check("drunkenAli-beer" .. i .. "-drunk", beers_before_r == "ok" and beers_after_r == "ok"
                and beers_before == 4 - i and beers_after == 3 - i,
                "beer " .. tostring(beers_before) .. " -> " .. tostring(beers_after)
                .. " (Drunken Ali took beer " .. i .. " of 3)")
        end
        t.expect("quest.stage.drunken_ali_done", t.quest.expect_stage("drunken_ali_done"))
        -- ==================== talkToThug / talkToBandit: question both gangs
        -- feud_recruitment.rs2 [opnpc1,feud_egyptian_doorman_multi] and
        -- [opnpc1,feud_arabian_guard_multi]; feud_var_talk_gangs 1 + 2 -> 3.
        t.exec("goto-talkToThug", t.player.goto_tile, 3333, 2954, 0)
        t.exec("talkToThug", t.player.talk_to, "feud_egyptian_doorman_multi", 1)
        t.exec("talkToThug-dialog", t.chat.play, { "npc:Those thieving bandits accused us" })
        t.exec("goto-talkToBandit", t.player.goto_tile, 3355, 2989, 0)
        t.exec("talkToBandit", t.player.talk_to, "feud_arabian_guard_multi", 1)
        t.exec("talkToBandit-dialog", t.chat.play, { "npc:Those Menaphite dogs stole" })
        t.expect("quest.stage.gangs_questioned", t.quest.expect_stage("gangs_questioned"))

        -- ==================== talkToCamelman: two camels for 500 coins
        t.exec("goto-talkToCamelman", t.player.goto_tile, 3350, 2965, 0)
        t.exec("talkToCamelman", t.player.talk_to, "feud_ali_the_discount_camel_seller", 1)
        t.exec("talkToCamelman-dialog", t.chat.play, {
            "player:Are those camels around the side",
            "npc:They certainly are",
            "player:What price do you want",
            "npc:For the pair?",
            "choose:Would 500 gold coins for the pair of them do?",
            "player:Would 500 gold coins for the pair",
            "npc:Pleasure doing business",
        })
        t.expect("quest.stage.camels_bought", t.quest.expect_stage("camels_bought"))
        local receipt_r, receipt_n = t.inv.count("feud_camel_receipt")
        t.check("returnCamels-receipts", receipt_r == "ok" and receipt_n == 2, "feud_camel_receipt count=" .. tostring(receipt_n) .. " (want 2)")

        -- ==================== returnCamels: one receipt to each gang
        t.exec("goto-returnCamelsMenaphite", t.player.goto_tile, 3333, 2954, 0)
        t.exec("talkToMenaphiteReturnedCamel", t.player.talk_to, "feud_egyptian_doorman_multi", 1)
        t.exec("talkToMenaphiteReturnedCamel-dialog", t.chat.play, { "npc:A receipt?" })
        t.exec("goto-returnCamelsBandit", t.player.goto_tile, 3355, 2989, 0)
        t.exec("talkToBanditReturnedCamel", t.player.talk_to, "feud_arabian_guard_multi", 1)
        t.exec("talkToBanditReturnedCamel-dialog", t.chat.play, { "npc:Pfft." })
        t.expect("quest.stage.receipts_given", t.quest.expect_stage("receipts_given"))

        -- ==================== talkToAliTheOperator: ask to join
        t.exec("goto-talkToAliTheOperator", t.player.goto_tile, 3334, 2951, 0)
        t.exec("talkToAliTheOperator", t.player.talk_to, "feud_egyptian_minder", 1)
        t.exec("talkToAliTheOperator-dialog", t.chat.play, {
            "player:Yes, of course, those bandits",
            "npc:Would you now.",
            "choose:I can prove myself.",
            "player:I can prove myself.",
            "npc:Very well.",
        })
        t.expect("quest.stage.operator_joined", t.quest.expect_stage("operator_joined"))

        -- ==================== pickpocketVillager (task 1): Pickpocket = op 3
        t.exec("goto-pickpocketVillager1", t.player.goto_tile, 3356, 2951, 0)
        t.exec("pickpocketVillager1", t.player.talk_to, "feud_villager_multi_1", 3)
        t.expect("quest.stage.pickpocket1_done", t.quest.expect_stage("pickpocket1_done"))

        -- ==================== task 2: Operator, then the street urchin's distraction
        t.exec("goto-operatorTask2", t.player.goto_tile, 3334, 2951, 0)
        t.exec("operatorTask2", t.player.talk_to, "feud_egyptian_minder", 1)
        t.exec("operatorTask2-dialog", t.chat.play, { "npc:Not bad. Now try again" })
        -- 3352,2961: open street beside the outside stairs (feud_outsidestairs_base
        -- 3353,2958 is solid).
        t.exec("goto-urchin", t.player.goto_tile, 3352, 2961, 0)
        t.exec("urchin", t.player.talk_to, "feud_street_urchin", 1)
        t.exec("urchin-dialog", t.chat.play, {
            "npc:Need a distraction?",
            "choose:Wow, a street urchin. Can I have a go? Please?",
            "player:Wow, a street urchin.",
            "npc:Right you are!",
        })
        t.exec("goto-pickpocketVillager2", t.player.goto_tile, 3356, 2951, 0)
        t.exec("pickpocketVillagerWithUrchin", t.player.talk_to, "feud_villager_multi_1", 3)
        t.expect("quest.stage.pickpocket2_done", t.quest.expect_stage("pickpocket2_done"))

        -- ==================== task 3: the oak blackjack
        t.exec("goto-operatorTask3", t.player.goto_tile, 3334, 2951, 0)
        t.exec("operatorTask3", t.player.talk_to, "feud_egyptian_minder", 1)
        t.exec("operatorTask3-dialog", t.chat.play, {
            "npc:Good work. One more test.",
            "npc:Take this blackjack.",
        })
        t.expect("operatorTask3-blackjack", t.inv.await("blackjack_oak", 1, 10))
        t.exec("equipBlackjack", t.player.equip, "blackjack_oak")
        t.expect("operatorTask3-lureTaught", t.var.await_server("varb340_feud_npc_multi", 2, 5))

        -- blackjackVillager: Lure (op4), Knock-Out (op5), Pickpocket (op3).
        -- Wiki The_Feud (oldid 15315438) "3rd villager": "lure a villager ...,
        -- knock them out with the blackjack, and finally pick their pocket (all
        -- options are right click)". The knock-out is a Thieving roll and
        -- refuses where another villager, bandit or thug can see
        -- (skill_thieving/scripts/blackjack.rs2), so the villager is led east
        -- of the Menaphite houses, onto open ground no one else stands on.
        -- press, not talk_to: the knocked-out window is a few ticks and
        -- talk_to spends five of them waiting for a page that never comes.
        -- The Knock-Out press waits up to 12 ticks: a villager that fell
        -- behind the lead is walked back to first (p_arrivedelay).
        t.exec("goto-blackjackVillager", t.player.goto_tile, 3356, 2951, 0)
        t.exec("blackjackVillager-lure", t.player.talk_to, "feud_villager_multi_1", 4)
        t.exec("blackjackVillager-lure-dialog", t.chat.play, { "player:Oi! Over here, you!" })
        -- walk_to answers ok only on reaching the tile (verbs-pointer: a real graded row).
        t.exec("blackjackVillager-lead", t.player.walk_to, 3371, 2958, 20)
        -- Each Knock-Out press is an attempt (a Thieving roll), graded by a
        -- note; the outcome row is blackjackVillager-knockedOut below.
        local knocked = false
        local ko_tries, ko_last = 0, "no press"
        for attempt = 1, 8 do
            local ko_r, ko_d = t.player.press("feud_villager_multi_1", 5, 12)
            local ko_text = tostring(ko_d)
            ko_tries, ko_last = attempt, tostring(ko_r) .. " " .. ko_text
            t.note("blackjackVillager-knockout attempt " .. attempt .. ": " .. ko_last)
            if ko_r == "ok" and string.find(ko_text, "out cold", 1, true) then
                knocked = true
                break
            end
            -- A failed roll turns the villager on the player, and the Lure
            -- takes it back out of combat (blackjack.rs2 [label,blackjack_lure]);
            -- a witness that wandered in means leading it a little further.
            t.exec("blackjackVillager-relure", t.player.talk_to, "feud_villager_multi_1", 4)
            t.exec("blackjackVillager-relure-dialog", t.chat.play, { "player:Oi! Over here, you!" })
            if string.find(ko_text, "will see me", 1, true) then
                -- An attempt, not an outcome: the knock-out row above is the verdict.
                local far_r, far_d = t.player.walk_to(3374, 2956 - attempt, 20)
                t.note("blackjackVillager-leadFurther: walk_to 3374," .. (2956 - attempt) .. " -> "
                    .. tostring(far_r) .. " " .. tostring(far_d))
            end
        end
        t.check("blackjackVillager-knockedOut", knocked, "knock-out landed: " .. tostring(knocked) .. " after "
            .. ko_tries .. " press(es) of at most 8; last: " .. ko_last)
        t.exec("blackjackVillager", t.player.press, "feud_villager_multi_1", 3, 4)
        t.expect("blackjackVillager-msg", t.msg.expect("You pick the villager's pocket."))
        t.expect("quest.stage.pickpocket3_done", t.quest.expect_stage("pickpocket3_done"))

        -- ==================== talkToAliToGetSecondJob: the heist briefing
        t.exec("goto-heistBriefing", t.player.goto_tile, 3334, 2951, 0)
        t.exec("heistBriefing", t.player.talk_to, "feud_egyptian_minder", 1)
        -- Transcript:The_Feud "Welcome to the Menaphites": keys only.
        t.exec("heistBriefing-dialog", t.chat.play, {
            "npc:Well done! You have finished your first trial.",
            "player:First trial?",
            "npc:You didn't think we'd hire you",
            "player:Ah, of course not.",
            "npc:Good! Now, the next thing you have to do",
            "npc:The only place worth pilfering",
            "npc:I want you to retrieve his wife's jewels.",
            "player:Got any advice?",
            "npc:Well I think that a disguise would be a good start.",
            "npc:You'll need a key to the front door, too.",
            "*", -- objbox: Ali the Operator hands you a set of keys.
            "player:Anything else?",
            "npc:No! Now get going.",
        })
        t.expect("quest.stage.heist_briefed", t.quest.expect_stage("heist_briefed"))
        t.expect("heistBriefing-keys", t.inv.await("feud_mayors_house_keys", 1, 10))
        local own_r, own_n = t.inv.count("feud_desert_disguise")
        t.check("heistBriefing-noDisguiseGrant", own_r == "ok" and own_n == 1,
            "feud_desert_disguise count=" .. tostring(own_n) .. " (the one made at createDisguise; the Operator gives keys only)")

        -- ==================== hideBehindCactus: disguise + gloves worn
        t.exec("equipDisguise", t.player.equip, "feud_desert_disguise")
        t.exec("equipGloves", t.player.equip, "leather_gloves")
        -- 3364,2969: open courtyard tile beside the cactus row (feud_cactus_row
        -- 3363,2967 is solid; reach.py: REACH closed-doors from the Menaphite camp).
        t.exec("goto-hideBehindCactus", t.player.goto_tile, 3364, 2969, 0)
        t.exec("hideBehindCactus", t.player.click_loc, "feud_cactus_row", 1)
        t.expect("hideBehindCactus-msg", t.msg.expect("coast is clear"))

        -- ==================== openTheDoor: the villa door, key in the pack
        -- The door is a real two-leaf door since seam25 (feud_heist.rs2 ~feud_mayors_door calls
        -- doors/scripts/doubledoors.rs2 ~open_double_door_left): feud_closed_door_left 3370,2971
        -- (rot 2, the guide's leaf) swings to feud_open_door_left one tile east and pulls the
        -- right leaf with it; both stand open 500 ticks. Wiki Transcript:The_Feud "Using the keys
        -- on the Mayor's house door": "You hear a satisfying click as the door unlocks."
        -- The house behind it is a closed space: walked into and out of by this door only
        -- (reach.py 3373,2977 -> 3334,2951: NEEDS-DOOR via feud_closed_door_left).
        t.exec("openTheDoor", t.player.pass_door, { closed = "feud_closed_door_left", open = "feud_open_door_left",
            at = { 3370, 2971, 0 }, near = { 3369, 2971 }, far = { 3372, 2971 } })
        t.expect("openTheDoor-unlock", t.msg.expect("satisfying click as the door unlocks"))
        t.expect("openTheDoor-msg", t.msg.expect("You slip inside, disguised"))
        t.expect("quest.stage.house_entered", t.quest.expect_stage("house_entered"))

        -- ==================== heist: the study desk (3367,2966,0) -- the numbers note
        -- (the study opens off the hall through the open curtain desertdooropen 3370,2966)
        t.exec("searchDesk", t.player.click_loc, "feud_mayors_desk", 1)
        t.expect("searchDesk-note", t.inv.await("feud_nos_note", 1, 10))
        t.expect("quest.stage.note_numbers", t.quest.expect_stage("note_numbers"))

        -- ==================== heist: upstairs (feud_insidestairs_base 3373,2978,0), the bed
        -- maplink.dbrow maplink_0_52_46_45_33_up: 3373,2977,0 -> 3374,2979,1.
        t.exec("goUpStairs", t.player.climb, { loc = "feud_insidestairs_base", op = 1, op_name = "Climb-up",
            at = { 3373, 2978, 0 }, dest = { 3374, 2979, 1 }, slack = 1 })
        t.exec("searchBed", t.player.click_loc, "feud_mayors_bed", 1)
        t.expect("searchBed-note", t.inv.await("feud_fib_hint", 1, 10))
        t.expect("quest.stage.note_fib", t.quest.expect_stage("note_fib"))

        -- ==================== crackTheSafe: the landscape picture (3374,2974,1) hides interface 330
        -- QH TheFeud.java:390 "Enter the code 1, 1, 2, 3, 5, 8."
        t.exec("searchPicture", t.player.click_loc, "feud_mayors_picture", 1)
        local safe_r, safe_d = t.ui.await_open("the_feud_safe", 10)
        t.expect("safe-open", safe_r,
            "the_feud_safe (interface 330) await_open -> " .. tostring(safe_r)
            .. " -- mounted by the picture's Search behind 3374,2974,1")
        t.expect("searchPicture-msg", t.msg.expect("covering something: a safe"))
        local SAFE = { "feud_over_model1", "feud_overstate_model2", "feud_overstate_model3",
            "feud_overstate_model4", "feud_overstate_model5", "feud_overstate_model6",
            "feud_overstate_model7", "feud_overstate_model8", "feud_overstate_model9" }
        local code = { 1, 1, 2, 3, 5, 8 }
        for i = 1, #code do
            local wr, w = t.ui.widget("the_feud_safe:" .. SAFE[code[i]])
            t.expect("safe-dial" .. i .. "-widget", wr, "Set to " .. code[i])
            local ir, id = t.ui.invoke(w, 1)
            t.ticks(1)
            t.check("safe-dial" .. i, ir == "ok", "invoke Set to " .. code[i] .. " -> " .. tostring(ir) .. " " .. tostring(id))
        end
        t.expect("safe-jewels", t.inv.await("feud_mayors_jewels", 1, 10))
        t.expect("safe-msg", t.msg.expect("You remove the jewels from the safe."))
        t.expect("quest.stage.safe_opened", t.quest.expect_stage("safe_opened"))

        -- ==================== leave the house: down the stairs and out through the door
        -- maplink.dbrow maplink_1_52_46_46_34/35_down: -> 3373,2977,0.
        t.exec("goDownStairs", t.player.climb, { loc = "feud_insidestairs_top", op = 1, op_name = "Climb-down",
            at = { 3373, 2978, 1 }, dest = { 3373, 2977, 0 } })
        -- Out of the house by the same door: it still stands open from the
        -- way in (500 ticks) or is pressed again (~feud_mayors_door past
        -- ^feud_safe_opened only swings the leaves).
        t.exec("leaveHouse", t.player.pass_door, { closed = "feud_closed_door_left", open = "feud_open_door_left",
            at = { 3370, 2971, 0 }, near = { 3372, 2971 }, far = { 3369, 2971 } })

        -- ==================== returnTheJewels: the Operator's traitor briefing
        t.exec("goto-returnJewels", t.player.goto_tile, 3334, 2951, 0)
        t.exec("returnJewels", t.player.talk_to, "feud_egyptian_minder", 1)
        t.exec("returnJewels-dialog", t.chat.play, {
            "player:Here are the mayor's wife's jewels",
            "npc:Excellent work!",
            "npc:Someone in this camp is drinking",
        })
        t.expect("quest.stage.traitor_briefed", t.quest.expect_stage("traitor_briefed"))

        -- ==================== findTraitor: talkMenaphiteToFindTraitor
        -- feud_recruitment.rs2 [label,feud_thug_questioning]
        t.exec("goto-talkMenaphiteToFindTraitor", t.player.goto_tile, 3333, 2954, 0)
        t.exec("talkMenaphiteToFindTraitor", t.player.talk_to, "feud_egyptian_doorman_multi", 1)
        t.exec("talkMenaphiteToFindTraitor-dialog", t.chat.play, {
            "player:There's a traitor among the gang",
            "npc:A traitor?",
            "npc:If I were you, I'd ask Ali the Barman",
        })
        t.expect("quest.stage.thug_questioned", t.quest.expect_stage("thug_questioned"))

        -- tellAliYouFoundTraitor: the Operator's stage 16..22 line
        t.exec("goto-tellAliYouFoundTraitor", t.player.goto_tile, 3334, 2951, 0)
        t.exec("tellAliYouFoundTraitor", t.player.talk_to, "feud_egyptian_minder", 1)
        t.exec("tellAliYouFoundTraitor-dialog", t.chat.play, { "npc:Find that traitor and deal with them" })

        -- ==================== talkToAliTheBarman: whose drink is it
        t.exec("goto-talkToAliTheBarman", t.player.goto_tile, 3359, 2956, 0) -- open floor beside the table 3360,2956
        t.exec("talkToAliTheBarman", t.player.talk_to, "feud_ali_the_barman", 1)
        t.exec("talkToAliTheBarman-dialog", t.chat.play, {
            "player:I'm looking for Traitorous Ali",
            "npc:Now that you mention it",
            "choose:Thanks, that's useful.",
            "player:Thanks, that's useful.",
        })
        t.expect("quest.stage.barman_done", t.quest.expect_stage("barman_done"))

        -- ==================== talkToAliTheHag: ask for poison (guide stage 17)
        t.exec("goto-talkToAliTheHag", t.player.goto_tile, 3346, 2985, 0)
        t.exec("talkToAliTheHag", t.player.talk_to, "feud_hag", 1)
        t.exec("talkToAliTheHag-dialog", t.chat.play, {
            "player:Good day, Hag.",
            "npc:Good day to you too, Blockhead.",
            "npc:Now what do you want?",
            "player:Actually I just want your help",
            "npc:Ahh, I see.",
            "player:Don't worry about the consequences",
            "npc:Well now this is getting interesting.",
            "player:'Traitorous Ali', please.",
            "npc:Traitorous Ali? Say no more!",
            "player:What payment do you require?",
            "npc:None at all",
            "player:What do you need?",
            "npc:Ah, impatience!",
            "player:Where would I get some snake poison?",
            "npc:From a snake perhaps?",
            "player:What I meant was how do I extract",
            "npc:Sheesh! You'd make a terrible hag.",
        })
        -- %feud_hag_list lives on feud_var_multi, which the server never transmits
        -- (no carrier in general/configs), so the client cannot read it; the
        -- snake hand-in below only plays from %feud_hag_list = 1.

        -- ==================== talkToAliTheKebabSalesman: the special sauce
        t.exec("goto-talkToAliTheKebabSalesman", t.player.goto_tile, 3351, 2974, 0) -- open tile beside cookingshelves 3352,2973
        t.exec("talkToAliTheKebabSalesman", t.player.talk_to, "feud_kebabman", 1)
        t.exec("talkToAliTheKebabSalesman-dialog", t.chat.play, {
            "player:Would you sell me that bottle of special kebab sauce?",
            "npc:This old thing?",
            "choose:Thanks.",
            "player:Thanks.",
        })
        t.expect("quest.stage.sauce_bought", t.quest.expect_stage("sauce_bought"))
        t.expect("talkToAliTheKebabSalesman-sauce", t.inv.await("superhot_kebab_sauce", 1, 10))

        -- ==================== getDung: sauce on the trough, the camel leaves dung in the bucket
        t.exec("goto-getDung", t.player.goto_tile, 3345, 2960, 0)
        local trough = t.player.by_symbol("loc", "feud_foodtrough2")
        t.exec("getDung", t.player.use_on, "superhot_kebab_sauce", trough)
        t.expect("getDung-bucket", t.inv.await("feud_camel_pooh_bucket", 1, 10))
        -- feud_traitor.rs2 [oplocu,feud_foodtrough2]: the sauce and the empty bucket are spent.
        local sauce_r, sauce_n = t.inv.count("superhot_kebab_sauce")
        local bucket_left_r, bucket_left_n = t.inv.count("bucket_empty")
        t.check("getDung-spent", sauce_r == "ok" and sauce_n == 0 and bucket_left_r == "ok" and bucket_left_n == 0,
            "after the trough: superhot_kebab_sauce=" .. tostring(sauce_n) .. " bucket_empty=" .. tostring(bucket_left_n))
        t.expect("quest.stage.dung_bucketed", t.quest.expect_stage("dung_bucketed"))

        -- ==================== giveCoinToSnakeCharmer: coins on the money pot
        t.exec("goto-giveCoinToSnakeCharmer", t.player.goto_tile, 3356, 2951, 0)
        local pot = t.player.by_symbol("loc", "feud_money_bowl")
        local pot_coins_before_r, pot_coins_before = t.inv.count("coins")
        t.exec("giveCoinToSnakeCharmer", t.player.use_on, "coins", pot)
        t.expect("giveCoinToSnakeCharmer-charm", t.inv.await("snake_flute", 1, 10))
        t.expect("giveCoinToSnakeCharmer-basket", t.inv.await("basket_for_snake", 1, 10))
        local pot_coins_after_r, pot_coins_after = t.inv.count("coins")
        t.check("giveCoinToSnakeCharmer-coin", pot_coins_before_r == "ok" and pot_coins_after_r == "ok"
            and pot_coins_before - pot_coins_after == 1,
            "coins " .. tostring(pot_coins_before) .. " -> " .. tostring(pot_coins_after)
            .. " (one coin into the pot, ratcatchers.rs2 [oplocu,feud_money_bowl])")

        -- ==================== catchSnake: the charm on a desert snake
        t.exec("goto-catchSnake", t.player.goto_tile, 3334, 2960, 0)
        local snake = t.player.by_symbol("npc", "feud_desert_snake")
        t.exec("catchSnake", t.player.use_on, "snake_flute", snake)
        t.expect("catchSnake-basket", t.inv.await("basket_with_snake", 1, 10))
        t.expect("quest.stage.snake_done", t.quest.expect_stage("snake_done"))

        -- ==================== giveSnakeToHag: the snake first
        -- Transcript:The_Feud "Talking to Ali the Hag with a Snake basket full"
        t.exec("goto-giveSnakeToHag", t.player.goto_tile, 3346, 2985, 0)
        t.exec("giveSnakeToHag", t.player.talk_to, "feud_hag", 1)
        t.exec("giveSnakeToHag-dialog", t.chat.play, {
            "player:Good day, Hag.",
            "npc:Good day to you too, Blockhead.",
            "player:I have the snake. What else do you need?",
            "npc:Ah yes, that snake will do nicely.",
            "npc:Who's a vindictive little beast?",
            "player:Erm, yes. Do you need anything else?",
            "npc:Now that I have the toxin",
            "player:What? That's disgusting.",
            "npc:Do you want the poison or not?",
            "player:Fine, fine, I'll get some camel dung.",
            "npc:And make that fresh camel dung!",
            "player:Oops! I almost forgot!",
            "mesbox:You hand the basket with the charmed snake over",
        })
        t.expect("giveSnakeToHag-basketGone", t.inv.expect_absent("basket_with_snake"))
        local keep_r, keep_n = t.inv.count("feud_camel_pooh_bucket")
        local pois_r, pois_n = t.inv.count("poison_from_hag")
        t.check("giveSnakeToHag-dungKept", keep_r == "ok" and keep_n == 1 and pois_r == "ok" and pois_n == 0,
            "after the snake hand-in: feud_camel_pooh_bucket=" .. tostring(keep_n) .. " poison_from_hag=" .. tostring(pois_n) .. " (the dung is a separate hand-in)")
        t.expect("quest.stage.snake_done-still", t.quest.expect_stage("snake_done"))

        -- ==================== givenDungToHag: then the dung, for the poison
        -- Transcript:The_Feud "Talking to Ali the Hag with Ughthanki dung"
        t.exec("givenDungToHag", t.player.talk_to, "feud_hag", 1)
        t.exec("givenDungToHag-dialog", t.chat.play, {
            "player:Good day, Hag.",
            "npc:Good day to you too, Blockhead.",
            "npc:Here's your poison!",
            "player:Wow, that was quick!",
            "npc:Actually I already had it brewing.",
            "npc:You wouldn't believe the demand.",
            "player:That's not even remotely funny.",
            "npc:Sorry, I've been dying to say that",
            "player:Just stop. Thanks for the poison",
            "npc:Wait! Just remember, this poison",
            "player:Got it. Thanks again!",
            "*", -- objbox: You hand over your bucket of fresh camel dung and receive a vial of poison in return.
        })
        t.expect("givenDungToHag-poison", t.inv.await("poison_from_hag", 1, 10))
        t.expect("givenDungToHag-dungGone", t.inv.expect_absent("feud_camel_pooh_bucket"))
        t.expect("quest.stage.poison_made", t.quest.expect_stage("poison_made"))

        -- ==================== poisonTheDrink: poison on the traitor's beer
        t.exec("goto-poisonTheDrink", t.player.goto_tile, 3356, 2956, 0)
        local beer_table = t.player.by_symbol("loc", "feud_poison_beer_table")
        t.exec("poisonTheDrink", t.player.use_on, "poison_from_hag", beer_table)
        t.expect("quest.stage.beer_poisoned", t.quest.expect_stage("beer_poisoned"))
        t.expect("poisonTheDrink-vialGone", t.inv.expect_absent("poison_from_hag")) -- feud_traitor.rs2:254 inv_del

        -- ==================== tellAliOperatorPoisoned: final orders
        t.exec("goto-tellAliOperatorPoisoned", t.player.goto_tile, 3334, 2951, 0)
        t.exec("tellAliOperatorPoisoned", t.player.talk_to, "feud_egyptian_minder", 1)
        t.exec("tellAliOperatorPoisoned-dialog", t.chat.play, {
            "npc:The traitor's dealt with",
            "npc:Confront the Menaphite Leader",
        })
        t.expect("quest.stage.ready_confront", t.quest.expect_stage("ready_confront"))
        -- The Bandit Leader waits for the villager talk (Transcript:The_Feud,
        -- varbit 338 0 -> 1 there), not for these orders.
        local bvis_r, bvis = t.var.server("varb338_feud_bandit_boss_vis")
        t.check("ready_confront-banditLeaderHidden", bvis_r == "ok" and bvis == 0, "feud_bandit_boss_vis=" .. tostring(bvis) .. " after the final orders")

        -- ==================== talkToMenaphiteLeader / killMenaphiteThug
        -- the blackjack took the weapon slot in task 3; the scimitar goes back on
        t.exec("equipScimitarForFights", t.player.equip, "rune_scimitar")
        t.exec("goto-talkToMenaphiteLeader", t.player.goto_tile, 3334, 2954, 0)
        t.exec("talkToMenaphiteLeader", t.player.talk_to, "feud_menap_boss", 1)
        t.exec("talkToMenaphiteLeader-dialog", t.chat.play, {
            "npc:So, the outsider who's been sniffing around",
            "choose:I know you started this feud over a stolen camel. It ends now.",
            "player:I know you started this feud",
            "npc:You think you can just walk in here",
        })
        -- Tough Guy (vislevel 75; configs/all.npc stat1..6 = 85/50/50/75/0/80, a real block;
        -- docs/bosses/quest_combat_manifest.json quest-the-feud). Sharks are eaten inside the
        -- press and the wait below 60 hp; the margin row reads the eater's lowest hp.
        local thug_food_before_r, thug_food_before = t.inv.count("shark")
        local _, thug_attack_d = t.exec("killMenaphiteThug", t.player.attack, "feud_menap_toughguy", 2, 15, { eat = FIGHT_EAT })
        local _, thug_dead_d = t.exec("killMenaphiteThug-dead", t.npc.await_dead_engaged, 80, 10, { eat = FIGHT_EAT })
        fight_margin("killMenaphiteThug.margin", "Tough Guy (level 75)", thug_attack_d, thug_dead_d,
            thug_food_before_r == "ok" and thug_food_before or nil)
        t.ticks(5) -- the [ai_queue3] outcome lands after the release
        t.expect("quest.stage.menaphite_beaten", t.quest.expect_stage("menaphite_beaten"))

        -- ==================== talkToAVillager: the balance of power is lost
        -- feud_villagers.rs2 [label,feud_villager_balance_lost] (Transcript:The_Feud,
        -- "Celebrating your victory / Talking to a Villager"): %feud_talk_villager
        -- 0 -> 1 and the Bandit Leader shown (%feud_bandit_boss_vis 1).
        t.exec("goto-talkToAVillager", t.player.goto_tile, 3356, 2951, 0)
        t.exec("talkToAVillager", t.player.talk_to, "feud_villager_multi_1", 1)
        t.exec("talkToAVillager-dialog", t.chat.play, {
            "player:Hello.",
            "npc:You! You're the one that just made everything worse",
            "player:What?! I just single-handedly",
            "npc:You adventurers! You never think",
            "npc:It's not that simple",
            "npc:Who do you think is stronger?",
            "player:Oops. Sorry, I was just trying to help.",
            "npc:Yes. Before your intervention",
            "npc:Now that the balance is lost",
            "player:You know, this adventuring lark",
            "npc:If you're prepared to change something",
            "choose:What do you mean by fix the mess I created?",
            "player:What do you mean by fix the mess",
            "npc:Do I have to spell it out to you?",
            "player:Would you please?",
            "npc:Just run the bandits out of town.",
        })
        t.expect("talkToAVillager-talked", t.var.await_server("varb343_feud_talk_villager", 1, 5))
        t.expect("talkToAVillager-banditLeaderShown", t.var.await_server("varb338_feud_bandit_boss_vis", 1, 5))

        -- ==================== talkToBanditLeader / killBanditChampion
        t.exec("goto-talkToBanditLeader", t.player.goto_tile, 3353, 3000, 0)
        t.exec("talkToBanditLeader", t.player.talk_to, "feud_bandit_boss", 1)
        t.exec("talkToBanditLeader-dialog", t.chat.play, {
            "npc:You've already caused enough trouble",
            "choose:I know about the stolen camel. This feud ends now.",
            "player:I know about the stolen camel",
            "npc:Ha! You'll have to go through me first",
        })
        -- Bandit champion (vislevel 70; configs/all.npc stat1..6 = 59/50/80/50/0/0, a real block).
        local champ_food_before_r, champ_food_before = t.inv.count("shark")
        local _, champ_attack_d = t.exec("killBanditChampion", t.player.attack, "feud_bandit_toughguy", 2, 15, { eat = FIGHT_EAT })
        local _, champ_dead_d = t.exec("killBanditChampion-dead", t.npc.await_dead_engaged, 80, 10, { eat = FIGHT_EAT })
        fight_margin("killBanditChampion.margin", "Bandit champion (level 70)", champ_attack_d, champ_dead_d,
            champ_food_before_r == "ok" and champ_food_before or nil)
        t.ticks(5)
        t.expect("quest.stage.bandit_beaten", t.quest.expect_stage("bandit_beaten"))
        t.expect("bandit_beaten-mayorHidden", t.var.await_server("varb14604_feud_mayor_multivar", 2, 5))

        -- ==================== talkToAVillagerToSpawnMayor: "talk to the mayor"
        -- feud_villagers.rs2 [label,feud_villager_talk_to_mayor] (Transcript:The_Feud,
        -- "Finishing up / Talking to a Villager"): %feud_talk_villager 1 -> 2,
        -- %feud_mayor_multivar 2 -> 0 -- Ali the Mayor is back by the well.
        t.exec("goto-talkToAVillagerToSpawnMayor", t.player.goto_tile, 3358, 2967, 0) -- open tile north of cactus5 3358,2965
        t.exec("talkToAVillagerToSpawnMayor", t.player.talk_to, "feud_villager_multi_1", 1)
        t.exec("talkToAVillagerToSpawnMayor-dialog", t.chat.play, {
            "player:Now are you satisfied?",
            "npc:Oh, it's you again!",
            "player:I have delivered you from two evil tyrants.",
            "npc:Thank you? For what?",
            "player:I wonder why I bother sometimes!",
        })
        t.expect("talkToAVillagerToSpawnMayor-talked", t.var.await_server("varb343_feud_talk_villager", 2, 5))
        t.expect("talkToAVillagerToSpawnMayor-mayorShown", t.var.await_server("varb14604_feud_mayor_multivar", 0, 5))

        -- ==================== talkToMayor: Ali the Mayor's reveal
        t.exec("goto-talkToMayor", t.player.goto_tile, 3360, 2972, 0)
        t.exec("talkToMayor", t.player.talk_to, "feud_mayor", 1)
        t.exec("talkToMayor-dialog", t.chat.play, {
            "player:This whole feud started over a stolen camel",
            "npc:Ha! I do indeed.",
            "npc:Now that the gangs have had the fight",
        })
        t.expect("quest.stage.mayor_talked", t.quest.expect_stage("mayor_talked"))

        -- ==================== back to Al Kharid: the carpet, then the doorway north
        -- Pollnivneach -> Al Kharid crosses the Shantay Pass doorway (the
        -- desert's only way out on foot). A player flies back the way he came:
        -- the north Pollnivneach rug merchant (magic_carpet_seller4, m52_46.spawn
        -- 3350,3001) -> the Shantay pad 3308,3110 (magic_carpet.rs2
        -- @carpet_menu_return, ^carpet_route_npoll; fare ^carpet_fare_full 200).
        t.exec("goto-carpetBack", t.player.goto_tile, 3350, 3004, 0) -- open ground north of the pad
        local fare_before_r, fare_before = t.inv.count("coins")
        t.exec("carpetBack", t.player.talk_to, "magic_carpet_seller4", 1)
        t.exec("carpetBack-dialog", t.chat.play, {
            "player:Hello.",
            "npc:Greetings, desert traveller.",
            "choose:Yes please.",
            "player:Yes please.",
            "npc:From here you can travel to the Shantay Pass",
            "choose:Take me to the Pass then.",
            "player:Take me to the Pass then.",
        })
        local back_r = t.await({
            level = function()
                local r, tl = t.world.tile()
                return r == "ok" and tl.x == 3308 and tl.z == 3110 and select(2, t.drive._player_idle()) == true
            end,
            note = "carpet landed on the Shantay Pass pad 3308,3110",
        }, 80)
        local back_tr, back_tile = t.world.tile()
        local fare_after_r, fare_after = t.inv.count("coins")
        t.check("carpetBack-landed", back_r == "ok" and back_tr == "ok" and back_tile.x == 3308
            and back_tile.z == 3110 and back_tile.level == 0,
            "carpet await " .. tostring(back_r) .. "; rider at " .. tostring(back_tile and back_tile.x) .. ","
            .. tostring(back_tile and back_tile.z) .. "," .. tostring(back_tile and back_tile.level)
            .. " (the Shantay pad is 3308,3110,0)")
        t.check("carpetBack-fare", fare_before_r == "ok" and fare_after_r == "ok" and fare_before - fare_after == 200,
            "coins " .. tostring(fare_before) .. " -> " .. tostring(fare_after) .. " (fare 200, magic_carpet.constant ^carpet_fare_full)")
        t.ticks(3) -- ~carpet_ride's carpet_land + p_delay(1) + player_unlock
        -- Off the pad (a solid rug) onto the open sand south of the doorway.
        t.exec("walk-shantaySouth", t.player.walk_to, 3304, 3113, 12)
        -- North through the doorway: free from the south (shantay_pass.rs2: a
        -- player at or south of the loc is pushed 3 tiles north), pressed by its op.
        t.exec("leaveDesert", t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 },
            near = { 3304, 3114 }, far_ok = function(tile) return tile.z > 3116 end,
            far_desc = "north of the Shantay doorway, z > 3116" })

        -- ==================== finishQuest: Ali Morrisane's reward
        local reward_snap_r, reward_snap = t.skill.snapshot()
        local function count_of(sym)
            local r, n = t.inv.count(sym)
            return r == "ok" and n or nil
        end
        local coins_before = count_of("coins")
        local willow_before = count_of("blackjack_willow")
        local addy_before = count_of("adamant_scimitar")
        local disguise_before = count_of("feud_desert_disguise")
        t.exec("goto-finishQuest", t.player.goto_tile, 3304, 3211, 0)
        t.exec("finishQuest", t.player.talk_to, "feud_ali_m", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:Your nephew's safe",
            "npc:A stolen... camel?",
            "npc:No, I'm really too busy",
        })
        t.ticks(3)
        t.quest.expect_complete()

        -- Rewards (feud_alimorrisane.rs2:57-66; wiki The_Feud "Rewards"): 1 quest
        -- point (quest.points above), 15,000 Thieving xp, 500 coins, a willow
        -- blackjack, an adamant scimitar, and a desert disguise when none is in
        -- the pack (the one made at createDisguise is worn, so the pack holds 0).
        local xp_r, xp_d = t.skill.expect_gain("thieving", 15000, reward_snap)
        t.check("reward.thievingXp", reward_snap_r == "ok" and xp_r == "ok",
            "snapshot " .. tostring(reward_snap_r) .. "; t.skill.expect_gain(thieving, 15000) -> " .. tostring(xp_r)
            .. " " .. tostring(xp_d) .. " -- dbrow stat_xp_awarded 150000 tenths")
        local coins_after = count_of("coins")
        t.check("reward.coins", coins_before ~= nil and coins_after ~= nil and coins_after - coins_before == 500,
            "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. " (+500 documented)")
        local willow_after = count_of("blackjack_willow")
        t.check("reward.willowBlackjack", willow_before == 0 and willow_after == 1,
            "blackjack_willow " .. tostring(willow_before) .. " -> " .. tostring(willow_after)
            .. " (+1, scroll: Willow blackjack, feud_alimorrisane.rs2:63)")
        local addy_after = count_of("adamant_scimitar")
        t.check("reward.adamantScimitar", addy_before == 0 and addy_after == 1,
            "adamant_scimitar " .. tostring(addy_before) .. " -> " .. tostring(addy_after)
            .. " (+1, scroll: An Adamant scimitar, feud_alimorrisane.rs2:64)")
        local disguise_after = count_of("feud_desert_disguise")
        t.check("reward.desertDisguise", disguise_before == 0 and disguise_after == 1,
            "feud_desert_disguise in the pack " .. tostring(disguise_before) .. " -> " .. tostring(disguise_after)
            .. " (+1, the made one is worn; scroll: Desert disguise, feud_alimorrisane.rs2:60-61)")
        t.finish(0)
        return
    end,
}
