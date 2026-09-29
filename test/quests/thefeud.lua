-- The Feud (thefeud). Driven end to end through real clicks and dialogue:
-- Ali Morrisane's offer -> the Shantay Pass gate -> the magic carpet flight
-- to Pollnivneach -> questioning both gangs -> buying camels and handing
-- out receipts -> Ali the Operator's recruitment and three pickpocket
-- proving tasks -> the mayor's-house jewel heist (disguise, door, desk,
-- bed, the Fibonacci safe dial) -> hunting the traitor (barman, kebab
-- sauce, camel dung, the snake-charm minigame, the Hag's poison, the
-- poisoned beer) -> the Menaphite Leader/Tough Guy and Bandit
-- Leader/Bandit champion fights -> Ali the Mayor's reveal -> Ali
-- Morrisane's reward. Content source for every symbol/text/stage number:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_thefeud/
--   scripts/{feud_alimorrisane,feud_recruitment,feud_heist,feud_traitor,
--   feud_confrontation,feud_villagers}.rs2, configs/quest_thefeud.constant,
-- OSRS-Content/osrs239-content/server/scripts/areas/area_alkharid/scripts/
--   shantay.rs2, shantay_pass.rs2; areas/area_desert/scripts/
--   magic_carpet.rs2, magic_carpet_talk.rs2; shop/pollnivneach/scripts/
--   the_asp_snake_bar.rs2; quest_ratcatchers/scripts/ratcatchers.rs2
--   (feud_money_bowl's real trigger). docs/quests/the_feud.md.
--
-- GUIDE-GAP: buyDisguiseGear feud_recruitment.rs2:218 (Ali the Operator hands over a FINISHED feud_desert_disguise at the heist briefing -- no separate purchase exists)
-- GUIDE-GAP: createDisguise feud_recruitment.rs2:218 (same grant -- no combine/craft trigger exists anywhere in this quest's scripts)
-- GUIDE-GAP: blackjackVillager feud_recruitment.rs2:292 (the stage-8 lesson is the villager's op3 Pickpocket branch narrating the knock-out in a mes(); Lure/Knock-Out ops are only added by ~blackjack_refresh_ops AFTER it, so no Lure press exists at this stage)
-- GUIDE-GAP: givenDungToHag feud_traitor.rs2:166 (the Hag takes the dung and the snake in the ONE giveSnakeToHag conversation; no separate dung hand-in exists)

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
            varp = "feud_var",
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
            "choose:Yes, I'll head there now.",
            "player:Yes, I'll head there now.",
            "npc:Take this -- a Kharidian headp",
        })
        t.expect("quest.stage.accepted", t.quest.expect_stage("accepted"))

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
        -- shantay_pass_henge_doorway sits at z=3116; its oploc1 short-circuits
        -- into a bare 3-tile push (no pass/poster dialogue at all) whenever
        -- coordz(player) <= coordz(loc) -- stand north of it (z=3118, beside
        -- shantay_guard_still) so the real pass-check branch runs.
        t.exec("goto-goToShantay", t.player.goto_tile, 3304, 3118, 0)
        t.exec("goToShantay", t.player.click_loc, "shantay_pass_henge_doorway", 1)
        t.exec("goToShantay-dialog", t.chat.play, {
            "mesbox:There is a large poster on the wall",
            "mesbox:The Desert is a VERY Dangerous place",
            "mesbox:That seems pretty scary!",
            "choose:Yeah, that poster doesn't scare me!",
            "npc:Can I see your Shantay Desert Pass",
            "mesbox:You hand over a Shantay Pass.",
            "player:Sure, here you go!",
            "npc:Here, have a disclaimer",
        })
        t.ticks(3) -- shantay_pass_enter is a queued teleport a tick behind the click

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
        t.check("carpet-landed", landed_r,
            "carpet await " .. tostring(landed_r) .. " (landing tile reached and player idle)"
            .. "; rider at " .. tostring(land_tile and land_tile.x) .. "," .. tostring(land_tile and land_tile.z)
            .. "," .. tostring(land_tile and land_tile.level) .. " (the north Pollnivneach pad is 3349,3003,0)")
        t.ticks(3) -- ~carpet_ride's carpet_land + p_delay(1) + player_unlock
        t.exec("goto-buyBeers", t.player.goto_tile, 3360, 2956, 0)
        t.exec("buyBeers-open", t.shop.open, "feud_ali_the_barman", 3, "feud_alispub")
        t.exec("buyBeers", t.shop.buy, "beer", 3)
        t.check("buyBeers-close", t.shop.close())
        local br, bn = t.inv.count("beer")
        t.check("buyBeers-verify", br == "ok" and bn == 3, "beer count=" .. tostring(bn))
        local ali = t.player.by_symbol("npc", "feud_drunken_ali")
        for i = 1, 3 do
            t.exec("drunkenAli-beer" .. i, t.player.use_on, "beer", ali)
            local pages = { "player:*", "npc:*" }
            if i == 3 then pages[3] = "npc:*" end
            t.exec("drunkenAli-beer" .. i .. "-dialog", t.chat.play, pages)
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
        t.exec("goto-urchin", t.player.goto_tile, 3353, 2960, 0)
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
        t.exec("goto-blackjackVillager", t.player.goto_tile, 3356, 2951, 0)
        t.exec("blackjackVillager", t.player.talk_to, "feud_villager_multi_1", 3)
        t.expect("quest.stage.pickpocket3_done", t.quest.expect_stage("pickpocket3_done"))

        -- ==================== talkToAliToGetSecondJob: the heist briefing
        t.exec("goto-heistBriefing", t.player.goto_tile, 3334, 2951, 0)
        t.exec("heistBriefing", t.player.talk_to, "feud_egyptian_minder", 1)
        t.exec("heistBriefing-dialog", t.chat.play, {
            "npc:Excellent. You've proven yourself.",
            "npc:Now, for your first real job",
            "npc:Take this disguise",
        })
        t.expect("quest.stage.heist_briefed", t.quest.expect_stage("heist_briefed"))
        t.expect("heistBriefing-disguise", t.inv.await("feud_desert_disguise", 1, 10))
        t.expect("heistBriefing-keys", t.inv.await("feud_mayors_house_keys", 1, 10))

        -- ==================== hideBehindCactus: disguise + gloves worn
        t.exec("equipDisguise", t.player.equip, "feud_desert_disguise")
        t.exec("equipGloves", t.player.equip, "leather_gloves")
        t.exec("goto-hideBehindCactus", t.player.goto_tile, 3364, 2968, 0)
        t.exec("hideBehindCactus", t.player.click_loc, "feud_cactus_row", 1)
        t.expect("hideBehindCactus-msg", t.msg.expect("coast is clear"))

        -- ==================== openTheDoor: the villa door, key in the pack
        t.exec("openTheDoor", t.player.click_loc, "feud_closed_door_right", 1)
        t.expect("openTheDoor-msg", t.msg.expect("You slip inside, disguised"))
        t.expect("quest.stage.house_entered", t.quest.expect_stage("house_entered"))
        local door_tile_r, door_tile = t.world.tile()
        t.check("openTheDoor-tile", door_tile_r == "ok", "after the door click the player stands at " .. tostring(door_tile and door_tile.x) .. "," .. tostring(door_tile and door_tile.z) .. "," .. tostring(door_tile and door_tile.level))
        -- The door is a real two-leaf door since seam25 (feud_heist.rs2 ~feud_mayors_door calls
        -- doors/scripts/doubledoors.rs2 ~open_double_door_right): both leaves swing to
        -- feud_open_door_* (maps/m52_46.jl2: 6238/6240 at 3370,2971/2970, rot 2) and the
        -- courtyard behind the door line opens up. Wiki Transcript:The_Feud "Using the keys on
        -- the Mayor's house door": "You hear a satisfying click as the door unlocks."
        t.expect("openTheDoor-unlock", t.msg.expect("satisfying click as the door unlocks"))
        local swung_r, swung = t.world.loc_near("feud_open_door_right", 8)
        t.check("openTheDoor-swung", swung_r == "ok", "feud_open_door_right " .. tostring(swung_r) .. " at " .. tostring(swung and swung.tile_x) .. "," .. tostring(swung and swung.tile_z))
        local walkin_r, walkin_d = t.player.walk_to(3372, 2970, 14)
        local walkin_tile_r, walkin_tile = t.world.tile()
        t.check("openTheDoor-walkin", walkin_tile_r == "ok" and walkin_tile.x == 3372 and walkin_tile.z == 2970,
            "walk_to 3372,2970 through the swung door -> " .. tostring(walkin_r) .. " " .. tostring(walkin_d)
            .. "; player at " .. tostring(walkin_tile and walkin_tile.x) .. "," .. tostring(walkin_tile and walkin_tile.z))

        -- ==================== heist: the study desk (3367,2966,0) -- the numbers note
        t.exec("searchDesk", t.player.click_loc, "feud_mayors_desk", 1)
        t.expect("searchDesk-note", t.inv.await("feud_nos_note", 1, 10))
        t.expect("quest.stage.note_numbers", t.quest.expect_stage("note_numbers"))

        -- ==================== heist: upstairs (feud_insidestairs_base 3373,2978,0), the bed
        t.exec("climbStairs", t.player.click_loc, "feud_insidestairs_base", 1)
        t.ticks(2)
        local lvl_r, lvl = t.world.level()
        t.check("climbStairs-upstairs", lvl_r == "ok" and lvl == 1, "level after Climb-up = " .. tostring(lvl))
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
        t.exec("climbDown", t.player.click_loc, "feud_insidestairs_top", 1)
        t.ticks(2)
        local lvl2_r, lvl2 = t.world.level()
        t.check("climbDown-ground", lvl2_r == "ok" and lvl2 == 0, "level after Climb-down = " .. tostring(lvl2))

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
        t.exec("goto-talkToAliTheBarman", t.player.goto_tile, 3360, 2956, 0)
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
        t.exec("talkToAliTheHag-dialog", t.chat.play, { "npc:Ssss... what do you want, dearie?" })

        -- ==================== talkToAliTheKebabSalesman: the special sauce
        t.exec("goto-talkToAliTheKebabSalesman", t.player.goto_tile, 3352, 2973, 0)
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
        t.expect("quest.stage.dung_bucketed", t.quest.expect_stage("dung_bucketed"))

        -- ==================== giveCoinToSnakeCharmer: coins on the money pot
        t.exec("goto-giveCoinToSnakeCharmer", t.player.goto_tile, 3356, 2951, 0)
        local pot = t.player.by_symbol("loc", "feud_money_bowl")
        t.exec("giveCoinToSnakeCharmer", t.player.use_on, "coins", pot)
        t.expect("giveCoinToSnakeCharmer-charm", t.inv.await("snake_flute", 1, 10))
        t.expect("giveCoinToSnakeCharmer-basket", t.inv.await("basket_for_snake", 1, 10))

        -- ==================== catchSnake: the charm on a desert snake
        t.exec("goto-catchSnake", t.player.goto_tile, 3334, 2960, 0)
        local snake = t.player.by_symbol("npc", "feud_desert_snake")
        t.exec("catchSnake", t.player.use_on, "snake_flute", snake)
        t.expect("catchSnake-basket", t.inv.await("basket_with_snake", 1, 10))
        t.expect("quest.stage.snake_done", t.quest.expect_stage("snake_done"))

        -- ==================== giveSnakeToHag / givenDungToHag: the poison
        t.exec("goto-giveSnakeToHag", t.player.goto_tile, 3346, 2985, 0)
        t.exec("giveSnakeToHag", t.player.talk_to, "feud_hag", 1)
        t.exec("giveSnakeToHag-dialog", t.chat.play, {
            "player:I've a charmed snake and a bucket of fresh dung",
            "npc:Ssss, excellent ingredients!",
        })
        t.expect("giveSnakeToHag-poison", t.inv.await("feud_camel_poison_pooh_bucket", 1, 10))
        t.expect("quest.stage.poison_made", t.quest.expect_stage("poison_made"))

        -- ==================== poisonTheDrink: poison on the traitor's beer
        t.exec("goto-poisonTheDrink", t.player.goto_tile, 3356, 2956, 0)
        local beer_table = t.player.by_symbol("loc", "feud_poison_beer_table")
        t.exec("poisonTheDrink", t.player.use_on, "feud_camel_poison_pooh_bucket", beer_table)
        t.expect("quest.stage.beer_poisoned", t.quest.expect_stage("beer_poisoned"))

        -- ==================== tellAliOperatorPoisoned: final orders
        t.exec("goto-tellAliOperatorPoisoned", t.player.goto_tile, 3334, 2951, 0)
        t.exec("tellAliOperatorPoisoned", t.player.talk_to, "feud_egyptian_minder", 1)
        t.exec("tellAliOperatorPoisoned-dialog", t.chat.play, {
            "npc:The traitor's dealt with",
            "npc:Confront the Menaphite Leader",
        })
        t.expect("quest.stage.ready_confront", t.quest.expect_stage("ready_confront"))

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
        t.exec("killMenaphiteThug", t.player.attack, "feud_menap_toughguy", 2, 15)
        t.exec("killMenaphiteThug-dead", t.npc.await_dead_engaged, 80)
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
        t.expect("talkToAVillager-talked", t.var.await_server("feud_talk_villager", 1, 5))

        -- ==================== talkToBanditLeader / killBanditChampion
        t.exec("goto-talkToBanditLeader", t.player.goto_tile, 3353, 3000, 0)
        t.exec("talkToBanditLeader", t.player.talk_to, "feud_bandit_boss", 1)
        t.exec("talkToBanditLeader-dialog", t.chat.play, {
            "npc:You've already caused enough trouble",
            "choose:I know about the stolen camel. This feud ends now.",
            "player:I know about the stolen camel",
            "npc:Ha! You'll have to go through me first",
        })
        t.exec("killBanditChampion", t.player.attack, "feud_bandit_toughguy", 2, 15)
        t.exec("killBanditChampion-dead", t.npc.await_dead_engaged, 80)
        t.ticks(5)
        t.expect("quest.stage.bandit_beaten", t.quest.expect_stage("bandit_beaten"))
        t.expect("bandit_beaten-mayorHidden", t.var.await_server("feud_mayor_multivar", 2, 5))

        -- ==================== talkToAVillagerToSpawnMayor: "talk to the mayor"
        -- feud_villagers.rs2 [label,feud_villager_talk_to_mayor] (Transcript:The_Feud,
        -- "Finishing up / Talking to a Villager"): %feud_talk_villager 1 -> 2,
        -- %feud_mayor_multivar 2 -> 0 -- Ali the Mayor is back by the well.
        t.exec("goto-talkToAVillagerToSpawnMayor", t.player.goto_tile, 3358, 2966, 0)
        t.exec("talkToAVillagerToSpawnMayor", t.player.talk_to, "feud_villager_multi_1", 1)
        t.exec("talkToAVillagerToSpawnMayor-dialog", t.chat.play, {
            "player:Now are you satisfied?",
            "npc:Oh, it's you again!",
            "player:I have delivered you from two evil tyrants.",
            "npc:Thank you? For what?",
            "player:I wonder why I bother sometimes!",
        })
        t.expect("talkToAVillagerToSpawnMayor-talked", t.var.await_server("feud_talk_villager", 2, 5))
        t.expect("talkToAVillagerToSpawnMayor-mayorShown", t.var.await_server("feud_mayor_multivar", 0, 5))

        -- ==================== talkToMayor: Ali the Mayor's reveal
        t.exec("goto-talkToMayor", t.player.goto_tile, 3360, 2972, 0)
        t.exec("talkToMayor", t.player.talk_to, "feud_mayor", 1)
        t.exec("talkToMayor-dialog", t.chat.play, {
            "player:This whole feud started over a stolen camel",
            "npc:Ha! I do indeed.",
            "npc:Now that the gangs have had the fight",
        })
        t.expect("quest.stage.mayor_talked", t.quest.expect_stage("mayor_talked"))

        -- ==================== finishQuest: Ali Morrisane's reward
        local reward_snap_r, reward_snap = t.skill.snapshot()
        t.check("reward.snapshot", reward_snap_r == "ok" and reward_snap.thieving ~= nil, "thieving experience before hand-in = " .. tostring(reward_snap and reward_snap.thieving and reward_snap.thieving.experience))
        local coins_before_r, coins_before = t.inv.count("coins")
        t.check("reward.coinsBefore", coins_before_r == "ok", "coins before hand-in = " .. tostring(coins_before))
        t.exec("goto-finishQuest", t.player.goto_tile, 3304, 3211, 0)
        t.exec("finishQuest", t.player.talk_to, "feud_ali_m", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:Your nephew's safe",
            "npc:A stolen... camel?",
            "npc:No, I'm really too busy",
        })
        t.ticks(3)
        t.quest.expect_complete()

        local xp_r, xp_d = t.skill.expect_gain("thieving", 15000, reward_snap)
        t.check("reward.thievingXp", xp_r == "ok",
            "t.skill.expect_gain(thieving, 15000) -> " .. tostring(xp_r) .. " " .. tostring(xp_d) .. " -- dbrow stat_xp_awarded 150000 tenths")
        local coins_after_r, coins_after = t.inv.count("coins")
        t.check("reward.coins", coins_after_r == "ok" and coins_after - coins_before == 500,
            "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. " (+500 documented)")
        local wb_r, wb_n = t.inv.count("blackjack_willow")
        t.expect("reward.willowBlackjack", (wb_r == "ok" and wb_n == 1) and "ok" or "refused", "blackjack_willow count=" .. tostring(wb_n) .. " (scroll: Willow blackjack, feud_alimorrisane.rs2:63)")
        local as_r, as_n = t.inv.count("adamant_scimitar")
        t.expect("reward.adamantScimitar", (as_r == "ok" and as_n == 1) and "ok" or "refused", "adamant_scimitar count=" .. tostring(as_n) .. " (scroll: An Adamant scimitar, feud_alimorrisane.rs2:64)")
        local dd_r, dd_n = t.inv.count("feud_desert_disguise")
        t.expect("reward.desertDisguise", (dd_r == "ok" and dd_n == 1) and "ok" or "refused", "feud_desert_disguise count=" .. tostring(dd_n) .. " (scroll: Desert disguise, feud_alimorrisane.rs2:61)")
        t.finish(0)
        return
    end,
}
