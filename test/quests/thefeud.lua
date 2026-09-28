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
        t.ticks(20) -- the carpet ride itself (board + glide + land), no further pages

        -- ==================== goToPollnivneach / findBeef: three beers on ======
        -- Drunken Ali (feud_recruitment.rs2:13-45). This must happen before
        -- ANY other Pollnivneach content: the trigger only answers while
        -- feud_var is still < feud_drunken_ali_done(2), i.e. still exactly
        -- feud_accepted(1) -- doing the gang-questioning etc first would
        -- leave feud_var past 2 and turn every beer into a no-op "Cheers,
        -- friend, but I've said all I know."
        --
        -- DRIVER SEAM, confirmed six different ways across five runs: the
        -- only source of a "beer" item in this port is feud_ali_the_barman's
        -- shop (feud_alispub, the_asp_snake_bar.rs2:8), reached with op3
        -- (Trade). Every click path to him (t.shop.open, t.player.talk_to
        -- with a bare symbol, with a {at=tile} selector, with and without
        -- an extra t.ticks(5-10) settle after the goto, standing on his own
        -- spawn tile 3361,2955 and standing one tile clear at 3360,2955)
        -- answers the SAME "screen_position: no npc 3535
        -- (feud_ali_the_barman) in the client's entity pool" -- yet
        -- t.npc.await_present("feud_ali_the_barman", 15, 15) and
        -- t.npc.tiles("feud_ali_the_barman", 20) BOTH find exactly one live
        -- copy at 3361,2955 (slot 144, element 1073756760) on the very same
        -- tick. QD.drive._ensure_visible's own not_found re-ask
        -- (script/plugins/quest_driver/pointer.lua:783-789,
        -- QD.player._live_npc_id) walks the live pool matching npc_id OR
        -- base_npc_id against the symbol's id and still comes up empty, so
        -- this is not the documented multinpc base/child gap (trap 19) --
        -- the live pool itself is visibly populated by every OTHER reader,
        -- just not by the screen-position projector this one npc needs for
        -- ANY click (there is no dialogue-only or item-only path to a
        -- Trade shop). No further row in this file can be driven without
        -- buying beer here first (drunkenAliDone gates every later
        -- Pollnivneach stage). See the notebook
        -- (build/author_state/sonnet-b29/thefeud.author.progress.md) for
        -- the full run-by-run diagnostic trail.
        t.exec("goto-buyBeers", t.player.goto_tile, 3361, 2955, 0)
        local barman_present_r = t.npc.await_present("feud_ali_the_barman", 15, 15)
        t.step("buyBeers-present", barman_present_r == "ok" and "PASS" or "FAIL", "await_present -> " .. tostring(barman_present_r))
        local tiles_r, tiles_summary = t.npc.tiles("feud_ali_the_barman", 20)
        t.step("buyBeers-tiles", tiles_r == "ok" and "PASS" or "FAIL", tostring(tiles_summary))
        -- Not re-attempted here: shop.open/talk_to on this npc were proven
        -- across runs 4-7 (this file's own notebook,
        -- build/author_state/sonnet-b29/thefeud.author.progress.md) to
        -- answer "screen_position: no npc 3535 (feud_ali_the_barman) in
        -- the client's entity pool" every time, from every selector and
        -- settle-tick combination tried, despite the two PASS rows just
        -- above proving the live pool holds exactly one co-located copy on
        -- the same tick. Re-running the same broken click here would only
        -- add a FAIL row without new evidence.
        t.blocked("shop.open/talk_to screen-position resolution for feud_ali_the_barman (id 3535) answers 'not in the client's entity pool' from every approach tried (default symbol resolve, {at=tile} selector, 0/5/10-tick settles, standing on and one tile off his spawn tile), while npc.await_present/npc.tiles both confirm a single live copy at 3361,2955 on the same tick -- no click path reaches his op3 Trade shop, the only source of the beer feud_recruitment.rs2:13-45 needs, so goToPollnivneach/findBeef and every later Pollnivneach stage cannot be driven")
        return
    end,
}
