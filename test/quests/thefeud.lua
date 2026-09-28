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
--   feud_confrontation}.rs2, configs/quest_thefeud.constant,
-- OSRS-Content/osrs239-content/server/scripts/areas/area_alkharid/scripts/
--   shantay.rs2, shantay_pass.rs2; areas/area_desert/scripts/
--   magic_carpet.rs2, magic_carpet_talk.rs2; shop/pollnivneach/scripts/
--   the_asp_snake_bar.rs2; quest_ratcatchers/scripts/ratcatchers.rs2
--   (feud_money_bowl's real trigger). docs/quests/the_feud.md.
--
-- GUIDE-GAP: buyDisguiseGear feud_recruitment.rs2:218 (Ali the Operator hands over a FINISHED feud_desert_disguise at the heist briefing -- no separate purchase exists)
-- GUIDE-GAP: createDisguise feud_recruitment.rs2:218 (same grant -- no combine/craft trigger exists anywhere in this quest's scripts)

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
        t.check("carpet-landed", landed_r, landed_d)
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
        -- content_bug: feud_heist.rs2 [oploc1,feud_closed_door_left/right] REPLACES the generic
        -- double-door opener (doors/configs/doubledoors.loc: category door_left_closed) and only
        -- narrates + writes feud_var=11; it never loc_changes the door, so the wall at
        -- x=3370 (locs 6238/6240 at 3370,2971/2970, wall on the east face) stays shut.
        -- The desk room's only doorway (open door 1534 at 3370,2966, east face) is reached from
        -- 3371,2966, i.e. from INSIDE the mansion; the desk/bed/picture/stairs (3373,2978) are all east of the door line.
        local walk_r, walk_d = t.player.walk_to(3372, 2970, 14)
        local walk_tile_r, walk_tile = t.world.tile()
        t.check("openTheDoor-walkin-recorded", walk_tile_r == "ok",
            "walk_to 3372,2970 (through the door line at x=3370) -> " .. tostring(walk_r) .. " " .. tostring(walk_d)
            .. "; player at " .. tostring(walk_tile and walk_tile.x) .. "," .. tostring(walk_tile and walk_tile.z)
            .. " -- the door never opened, so the mansion interior (desk, bed, picture, safe) is unreachable on foot")
        t.blocked("content_bug: feud_heist.rs2 [oploc1,feud_closed_door_left]/[oploc1,feud_closed_door_right] narrates 'You slip inside' and sets feud_var=11 but never opens the door loc (no loc_change to feud_open_door_*; the quest trigger replaces the generic door_left_closed opener), so the player stays outside the wall at x=3370 and searchDesk/searchBed/crackTheSafe (guide steps heist) cannot be reached without a goto_tile teleport past the door, which is a cheat")
        return
    end,
}
