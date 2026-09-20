-- Pirate's Treasure ("hunt" test id / quest_hunt content dir) -- client-driven
-- end to end through the real client. Redbeard Frank asks for a bottle of
-- Karamja Rum, and redbeard_frank.rs2's [label,redboard_progress]/
-- [label,redbeard_hand_rum] only check inv_total(inv, karamja_rum) >= 1 --
-- but actually GETTING that bottle onto the mainland is a six-NPC/loc
-- smuggling job this pack wires for real (2026-09-20 review): buy a bottle
-- from the Deadman's Chest bartender in Brimhaven (deadmans_bartender.rs2,
-- 27 coins), get employed by Luthas at the banana plantation
-- (luthas.rs2's luthas_employment), stash the rum AND pack 10 bananas into
-- his export crate (banana_crate.rs2's oplocu -- customs_officer.rs2's
-- customs_spot_inspection/customs_search confiscate karamja_rum carried
-- directly, the entire reason the crate trick exists), pass the Karamja
-- customs officer's boarding search, get employed at Wydin's grocery store
-- in Port Sarim (needs a white apron -- wydin.rs2's wydin_job), and dig the
-- smuggled rum back out of his grocery crate (food_store.rs2's oploc1
-- grocerycrate). Then Hector's chest in the Blue Moon Inn (use the key on
-- it -- [oplocu,piratechest]; op1 alone answers "The chest is locked."),
-- reading the pirate message it holds ([opheld1,piratemessage]), and
-- digging at the Falador Park cross ([opheld1,spade] recognises the tile
-- 0_46_52_55_55 = 2999,3383,0 and jumps to quest_hunt/scripts/dig.rs2's
-- [label,hunt_dig]).
--
-- dig.rs2's hunt_dig redirects to [label,pirate_irate_gardener_attack]
-- instead of completing the dig when falador_gardener (hitpoints=7,
-- attack=1, aggressive) is within 10 tiles of the dig spot -- its own spawn
-- (m46_52.spawn) is ~3.6 tiles from 2999,3383, so the FIRST dig is expected
-- to provoke it rather than complete. It was never a click problem: attack
-- 1 / strength 1 with a bronze dagger loses to defence 7 over thirty
-- swings from one click (measured, build/quest_gate/q3probe2). The setup
-- gives combat levels and a rune scimitar instead (trap 16: gear is a
-- prerequisite, not the quest's own deliverable) and this file fights back
-- with t.player.attack/t.npc.await_dead, then goes back to the dig tile
-- for the real dig (proof: build/quest_gate/q3proof, gardener dead in 9
-- ticks and "You dig a hole in the ground...").

return {
    id = "hunt",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        -- Two: the driver's own held-item op ALSO drops the clicked item as
        -- a side effect (pointer.lua's `_inv_dispatch` banner -- "on
        -- rev-239's backpack op 1 is the shift-click-drop chain", measured
        -- here on both piratemessage and spade), so the gardener-interrupted
        -- first dig below spends the first spade before the real dig ever
        -- runs.
        "::give spade 2",
        -- Coins for the rum-smuggling chain the quest's own scripts wire
        -- (trap 16: money in the pocket is a prerequisite, never the
        -- deliverable) -- 27 to buy the bottle from Deadman's Chest
        -- (deadmans_bartender.rs2) and 30 for the Karamja customs
        -- officer's boarding charge (customs_officer.rs2's customs_pay).
        -- The rum itself is fetched for real below, never cheated in.
        "::give coins 100",
        -- Prerequisite combat gear for the gardener the dig provokes (see
        -- dig-treasure below) -- not the quest's own deliverable (trap 16).
        -- attack 1 / strength 1 with a bronze dagger loses to defence 7
        -- (measured, build/quest_gate/q3probe2), so level up and arm a
        -- rune scimitar instead.
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::give rune_scimitar 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "hunt",
            constants = {
                not_started = 0,
                fetch_rum = 1,
                received_key = 2,
                read_note = 3,
                complete = 4,
                questpoints = 2,
            },
            row = "quest_piratestreasure",
            display = "Pirate's Treasure",
            points = 2,
        })

        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------- Redbeard Frank
        t.exec("goto-redbeard", t.player.goto_tile, 3051, 3253, 0)
        t.exec("talk-redbeard-start", t.player.talk_to, "redbeard_frank", 1)

        -- [opnpc1,redbeard_frank] with hunt=not_started -> [label,
        -- redbeard_options]'s 3-way p_choice3 menu.
        t.exec("redbeard-greeting", t.chat.play, {
            "npc:Arrrh Matey!",
            "options",
        })

        t.exec("choose-treasure", t.chat.choose, "I'm in search of treasure.")

        -- [label,redbeard_treasure]: sets %hunt = hunt_fetch_rum, ends on its
        -- own last npc line (no p_choice here).
        t.exec("redbeard-treasure-story", t.chat.play, {
            "player:I'm in search of treasure.",
            "npc:Arr, treasure you be after eh? Well I might be able to tell you where to find some... For a price...",
            "player:What sort of price?",
            "npc:Well for example if you can get me a bottle of rum... Not just any rum mind...",
            "npc:I'd like some rum made on Karamja Island. There's no rum like Karamja Rum!",
        })

        t.expect("quest.stage.fetch_rum", t.quest.expect_stage("fetch_rum"))

        -- ------------------------------------------ fetch the rum, for real
        -- Deadman's Chest, Brimhaven -- buy a bottle of Karamja Rum.
        t.exec("goto-bartender", t.player.goto_tile, 2797, 3155, 0)
        t.exec("talk-bartender", t.player.talk_to, "deadmans_bartender", 1)
        t.exec("buy-rum", t.chat.play, {
            "npc:Yohoho me hearty",
            "options",
            "choose:A bottle of rum please.",
            "player:A bottle of rum please.",
            "npc:27 coins",
        })
        local rum_bought_result, rum_bought_detail = t.inv.await("karamja_rum", 1, 10)
        t.step("hunt.rum_bought", rum_bought_result == "ok" and "PASS" or "FAIL",
            "inv.await(karamja_rum, 1) -> " .. tostring(rum_bought_result) .. " " .. tostring(rum_bought_detail))

        -- Luthas's banana plantation -- get employed so his export crate
        -- will take the rum (banana_crate.rs2's [oplocu,bananacrate]
        -- refuses otherwise: "Why would I want to do that?").
        t.exec("goto-luthas", t.player.goto_tile, 2939, 3154, 0)
        t.exec("talk-luthas", t.player.talk_to, "luthas", 1)
        t.exec("luthas-employ", t.chat.play, {
            "npc:Hello I'm Luthas",
            "options",
            "choose:Could you offer me employment on your plantation?",
            "player:Could you offer me employment on your plantation?",
            "npc:crate ready to be loaded onto the ship",
            "npc:fill it up with bananas",
        })

        -- Stash the rum in the crate FIRST -- banana_crate_pack_rum wants
        -- %hunt = hunt_fetch_rum, still true here -- so it is hidden in
        -- the crate's own state, never carried, and the customs search
        -- below finds nothing on the player (the entire point of the
        -- trick). use_on's own settle can fire on the walk-into-range
        -- map_flag before the resulting mesbox even lands (measured), so
        -- confirm by the karamja_rum count actually dropping, closing any
        -- stray mesbox and retrying if it has not.
        local bananacrate = t.player.by_symbol("loc", "bananacrate")
        local rum_stashed = false
        for stash_attempt = 1, 4 do
            t.player.use_on("karamja_rum", bananacrate)
            t.ticks(2)
            t.chat.close()
            local after_rum_result, after_rum = t.inv.count("karamja_rum")
            if after_rum_result == "ok" and after_rum == 0 then
                rum_stashed = true
                break
            end
        end
        t.step("hunt.stash_rum", rum_stashed and "PASS" or "FAIL",
            "use_on(karamja_rum, bananacrate) -- karamja_rum carried now "
                .. (rum_stashed and "0 (stashed)" or "still >0"))
        t.shot("stash-rum")

        -- Pack 10 bananas into the same crate, picked from the plantation's
        -- own trees (banana_tree.rs2's pick_karamja_banana; next_loc_stage
        -- cycles the tree through its own base symbol -- QUEST_AUTHORING.md
        -- trap 20 -- so the SAME symbol is clicked every time).
        -- A single click_loc does not always land a pick on the same tick
        -- (measured: a map_flag-only settle that just closes the gap, with
        -- the count unmoved) -- that is the walk, not a miss, so the retry
        -- loop itself carries no row of its own (QUEST_AUTHORING.md trap
        -- 15: a shot/row exists because a click changed something, and a
        -- walk-only attempt changed nothing); only the outcome is recorded.
        local bananas_picked = 0
        for attempt = 1, 25 do
            if bananas_picked >= 10 then
                break
            end
            t.player.click_loc("bananatreefull", 1)
            t.ticks(1)
            local after_result, after_banana = t.inv.count("banana")
            if after_result == "ok" then
                bananas_picked = after_banana
            end
        end
        t.step("hunt.bananas_gathered", bananas_picked >= 10 and "PASS" or "FAIL",
            "picked " .. tostring(bananas_picked) .. "/10 bananas from the plantation trees")

        -- Picking wanders between tree instances -- return to the crate
        -- before packing.
        t.exec("goto-luthas-crate", t.player.goto_tile, 2939, 3154, 0)

        -- Each successful pack opens a REAL mesbox ("You pack a banana
        -- into the crate.") that blocks the next use_on until dismissed
        -- (measured: three unconsumed clicks in a row timed out with a
        -- mesbox still open from the one before) -- close it every time
        -- and confirm by the banana count actually dropping.
        -- Same reasoning as the picking loop above -- an attempt whose own
        -- mesbox is still closing from the one before is a walk/settle,
        -- not a click that changed anything, so no per-attempt row.
        local bananas_packed = 0
        for pack_attempt = 1, 14 do
            if bananas_packed >= 10 then
                break
            end
            local before_result, before_banana = t.inv.count("banana")
            t.player.use_on("banana", bananacrate)
            t.ticks(2)
            t.chat.close()
            local after_result, after_banana = t.inv.count("banana")
            if before_result == "ok" and after_result == "ok" and after_banana < before_banana then
                bananas_packed = bananas_packed + 1
            end
        end
        t.step("hunt.bananas_packed", bananas_packed >= 10 and "PASS" or "FAIL",
            "packed " .. tostring(bananas_packed) .. "/10 bananas into the crate")

        -- Confirm the crate itself now reads full -- oploc1 is the plain
        -- read-state trigger, a mes() chat-log line, not a mesbox.
        t.exec("check-crate-full", t.player.click_loc, "bananacrate", 1)
        t.expect("hunt.crate_full_msg", t.msg.expect("crate is full of bananas"))

        -- Hand the crate over -- Luthas pays 30 coins and ships it
        -- (crate_rum 1 -> 2), re-employing for another round; decline it.
        -- luthas.rs2's own payout branch calls if_close right after its
        -- npc page (a plain mes() coin-grant line follows, not a dialogue
        -- page), THEN reopens a fresh p_choice4 after a p_delay(3) --
        -- so one continuous chat.play list dies on its "options" entry
        -- with "the dialogue closed after 2 page(s)" (2026-09-20 review):
        -- the close is real, not a miss, and the reopen is a SEPARATE
        -- dialogue this driver has to wait for rather than continue past.
        t.exec("talk-luthas-payout", t.player.talk_to, "luthas", 1)
        t.exec("luthas-payout", t.chat.play, {
            "player:I've filled a crate with bananas.",
            "npc:here's your payment",
            "end",
        })
        t.expect("hunt.luthas_paid_msg", t.msg.expect("Luthas hands you 30 coins."))

        local payout_reopen_result, payout_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "options"
            end,
            note = "luthas.payout_reopen",
        }, 10)
        t.step("hunt.luthas_payout_reopen", payout_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == options) after if_close+p_delay(3) -> "
                .. tostring(payout_reopen_result) .. " " .. tostring(payout_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))

        t.exec("luthas-payout-decline", t.chat.play, {
            "options",
            "choose:Thank you, I'll be on my way",
            "player:Thank you, I'll be on my way",
        })

        -- Karamja customs officer -- the boarding search. Nothing is
        -- carried (the rum is stashed inside Luthas's shipped crate), so
        -- the spot check finds nothing and only the 30-coin boarding
        -- charge is owed (customs_officer.rs2's customs_search).
        t.exec("goto-customs", t.player.goto_tile, 2953, 3147, 0)
        t.exec("talk-customs", t.player.talk_to, "customs_officer", 1)
        t.exec("customs-pass", t.chat.play, {
            "npc:Can I help you?",
            "options",
            "choose:Can I journey on this ship?",
            "player:Can I journey on this ship?",
            "npc:You need to be searched",
            "options",
            "choose:Search away, I have nothing to hide.",
            "player:Search away, I have nothing to hide.",
            "npc:it's all legal",
            "options",
            "choose:Ok.",
            "player:Ok.",
        })
        t.expect("hunt.customs_paid_msg", t.msg.expect("pay 30 coins and board the ship"))

        -- customs_officer.rs2's customs_pay continues past that last
        -- "Ok." page into a plain mes() coin-charge line, p_delay(2), then
        -- a p_telejump to Port Sarim before its own ~mesbox reopens -- a
        -- second asynchronous reopen exactly like luthas-payout above, so
        -- the same split: wait for the page to actually remount rather
        -- than chaining straight into it (measured: reading immediately
        -- after "player:Ok." still shows that same page's own "Please
        -- wait..." transition frame, not yet closed).
        local customs_reopen_result, customs_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "mesbox"
            end,
            note = "customs.pay_reopen",
        }, 15)
        t.step("hunt.customs_pay_reopen", customs_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == mesbox) after pay+p_delay(2)+telejump -> "
                .. tostring(customs_reopen_result) .. " " .. tostring(customs_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))

        t.exec("customs-arrive", t.chat.play, {
            "mesbox:ship arrives at Port Sarim",
        })

        -- A white apron is Wydin's own health-and-safety requirement for
        -- the job, not the quest's own deliverable (trap 16) -- pick up
        -- the one left on the ground by his shop (areas/world/configs/
        -- m47_50.spawn) rather than cheat it in. click_obj answers `ok`
        -- with a nil detail (trap 12's hollow shape), so called directly
        -- with the before/after count as the real detail.
        t.exec("goto-apron", t.player.goto_tile, 3009, 3204, 0)
        local apron_before_result, apron_before = t.inv.count("white_apron")
        local apron_pick_result, apron_pick_detail = t.player.click_obj("white_apron")
        local apron_after_result, apron_after = t.inv.count("white_apron")
        t.step("hunt.take_apron",
            (apron_pick_result == "ok" and apron_after_result == "ok" and apron_after > (apron_before or 0)) and "PASS" or "FAIL",
            "click_obj(white_apron) -> " .. tostring(apron_pick_result) .. " " .. tostring(apron_pick_detail)
                .. " -- white_apron count " .. tostring(apron_before) .. " -> " .. tostring(apron_after))

        -- Wydin's grocery store, Port Sarim -- get employed (needs the
        -- apron just picked up, carried is enough for the job offer).
        t.exec("goto-wydin", t.player.goto_tile, 3014, 3204, 0)
        t.exec("talk-wydin", t.player.talk_to, "wydin", 1)
        t.exec("wydin-job", t.chat.play, {
            "npc:Welcome to my food store",
            "options",
            "choose:Can I get a job here?",
            "player:Can I get a job here?",
            "npc:Have you got your own white apron?",
            "player:Yes, I have one right here.",
            "npc:You're hired",
        })

        -- food_store.rs2's [oploc1,wydindoor] checks the apron is WORN,
        -- not just carried, before letting an employee through.
        t.exec("wear-apron", t.player.equip, "white_apron")

        -- wydindoor is a SILENT door: food_store.rs2's own
        -- [proc,hunt_walk_door] swaps the loc and teleports with no
        -- dialogue, no chat line, and no route, so click_loc's settle
        -- times out even when the door really opened. Its own after-the-
        -- fact "did the loc change" fallback (pointer.lua's click_loc,
        -- the door-evidence comment) needs the PLAYER's tile unchanged
        -- across the click, which goto-wydin landing exactly on the
        -- door's own tile (3012,3204) breaks -- every click steps off it
        -- first. So click, then check for the back room's own crate
        -- instead of trusting the verdict.
        local door_ok = false
        for door_attempt = 1, 3 do
            t.player.click_loc("wydindoor", 1)
            t.ticks(2)
            local inside_result = t.world.loc_near("grocerycrate", 6)
            if inside_result == "ok" then
                door_ok = true
                break
            end
        end
        t.step("hunt.enter_back_room", door_ok and "PASS" or "FAIL",
            "click_loc(wydindoor) x attempts, then world.loc_near(grocerycrate, 6) -> "
                .. (door_ok and "found (through the door)" or "not_found (still outside)"))
        t.shot("enter-back-room")

        -- The grocery crate -- Wydin's half of the smuggle. crate_rum = 2
        -- from Luthas's shipment, so opening it hands the rum straight
        -- back; decline the free banana.
        t.exec("open-grocery-crate", t.player.click_loc, "grocerycrate", 1)
        -- The crate's own [oploc1,grocerycrate] logs its first mes() line
        -- immediately (that alone settles the click above) but the actual
        -- p_choice2_header is behind a p_delay(3) + p_delay(1) -- chat.play
        -- acts on a dialogue already open, never opening one, so give the
        -- delayed prompt time to land first.
        t.ticks(10)
        t.exec("decline-crate-banana", t.chat.play, { "options", "choose:No" })

        local rum_back_result, rum_back_detail = t.inv.await("karamja_rum", 1, 10)
        t.step("hunt.rum_recovered", rum_back_result == "ok" and "PASS" or "FAIL",
            "inv.await(karamja_rum, 1) -> " .. tostring(rum_back_result) .. " " .. tostring(rum_back_detail))

        -- Talk to Redbeard again -- [label,redboard_progress] sees
        -- inv_total(inv, karamja_rum) >= 1 (the bottle smuggled in above)
        -- and falls straight through "Yes, I've got some." into [label,
        -- redbeard_hand_rum] in the SAME dialogue.
        t.exec("goto-redbeard-rum", t.player.goto_tile, 3051, 3253, 0)
        t.exec("talk-redbeard-rum", t.player.talk_to, "redbeard_frank", 1)
        t.exec("redbeard-hand-rum", t.chat.play, {
            "npc:Arrrh Matey!",
            "npc:Have ye brought some rum for yer ol' mate Frank?",
            "player:Yes, I've got some.",
            "npc:Now a deal's a deal, I'll tell ye about the treasure. I used to serve under a pirate captain called One-Eyed Hector.",
            "npc:Hector were very successful and became very rich. But about a year ago we were boarded by the Customs and Excise Agents.",
            "npc:Hector were killed along with many of the crew, I were one of the few to escape and I escaped with this.",
            "mesbox:Frank happily takes the rum... ... and hands you a key.",
            "npc:This be Hector's key. I believe it opens his chest in his old room in the Blue Moon Inn in Varrock.",
            "npc:With any luck his treasure will be in there.",
            "options",
        })

        t.exec("choose-go-get-it", t.chat.choose, "Ok thanks, I'll go and get it.")

        -- choose-go-get-it lands on its own trailing player echo with
        -- nothing left in the list to continue_ past it -- drain the rest
        -- shut so the next click is not fighting an open dialogue.
        local close_result, close_detail = t.chat.drain({ stop_at = "none" })
        t.expect("hunt.redbeard_dialogue_closed", close_result, close_detail)

        t.expect("quest.stage.received_key", t.quest.expect_stage("received_key"))

        -- ------------------------------------------- Hector's chest, upstairs
        -- goto_tile alone climbs the stairs (fai_varrock_stairs) -- no
        -- click_loc needed first (QUEST_AUTHORING.md section 2).
        t.exec("goto-chest", t.player.goto_tile, 3219, 3396, 1)

        -- [oplocu,piratechest]: op1 alone only answers "The chest is
        -- locked." -- it takes the key through use_on.
        local piratechest = t.player.by_symbol("loc", "piratechest")
        t.exec("use-key-on-chest", t.player.use_on, "chest_key", piratechest)

        -- The chest's own two lines are plain mes() chat-log lines, not a
        -- mesbox -- wait for the message item itself rather than a dialogue.
        local msg_result, msg_detail = t.inv.await("piratemessage", 1, 10)
        t.step("hunt.message_taken", msg_result == "ok" and "PASS" or "FAIL",
            "inv.await(piratemessage, 1) -> " .. tostring(msg_result) .. " " .. tostring(msg_detail))

        -- ------------------------------------------------- read the message
        t.exec("read-message", t.player.inv_op, "piratemessage", 1)
        t.exec("read-message-dialog", t.chat.play, {
            "mesbox:Visit the city of the White Knights. In the park, Saradomin points to the X which marks the spot.",
        })

        t.expect("quest.stage.read_note", t.quest.expect_stage("read_note"))

        -- ---------------------------------------------------- Falador Park
        t.exec("goto-dig", t.player.goto_tile, 2999, 3383, 0)

        local before_gold_ring_result, before_gold_ring = t.inv.count("gold_ring")
        local before_emerald_result, before_emerald = t.inv.count("emerald")
        local before_coins_result, before_coins = t.inv.count("coins")

        -- dig.rs2's [label,hunt_dig] redirects to [label,
        -- pirate_irate_gardener_attack] instead of completing the dig
        -- whenever falador_gardener is within 10 tiles of 2999,3383 -- its
        -- own spawn (m46_52.spawn: 2996,3381,0) is ~3.6 tiles away, so it
        -- always is. That redirect's own response (an npc_say + aggro) is
        -- never a settle t.exec would read as "ok", so the gardener is
        -- checked for FIRST and the click below is graded on what its
        -- OWN script predicts, not failed over a state the script itself
        -- causes (QUEST_AUTHORING.md trap 20's "the resolve WORKING, not a
        -- mis-click", same idea applied to a redirect instead of a loc).
        local gardener_before_result = t.npc.nearest("falador_gardener", 10)

        -- [opheld1,spade]: within 1 tile of 0_46_52_55_55 (2999,3383,0)
        -- jumps to dig.rs2's [label,hunt_dig].
        local dig1_result, dig1_detail = t.player.inv_op("spade", 1)
        if gardener_before_result == "ok" then
            -- A computed verdict, not a rubber stamp: falador_gardener was
            -- within 10 tiles, so hunt_dig's own npc_find redirects this
            -- click and it is NOT expected to answer "ok" -- if it somehow
            -- did (the redirect missed by a tick), that is the surprising
            -- result and this row says so.
            t.step("dig-treasure", dig1_result ~= "ok" and "PASS" or "FAIL",
                "inv_op(spade,1) -> " .. tostring(dig1_result) .. " " .. tostring(dig1_detail)
                    .. " -- falador_gardener was within 10 tiles, so hunt_dig's own "
                    .. "npc_find redirect (not a real dig) was the expected outcome here")
        else
            t.step("dig-treasure", dig1_result == "ok" and "PASS" or "FAIL",
                "inv_op(spade,1) -> " .. tostring(dig1_result) .. " " .. tostring(dig1_detail))
        end
        t.shot("dig-treasure")

        if gardener_before_result == "ok" then
            -- all.npc: falador_gardener's op2 is Attack. hitpoints=7,
            -- attack=1 (combat_stats.generated.npc) -- trivial with real
            -- combat levels, but not automatic: it only goes hostile here
            -- (npc_setmode(opplayer2)), the player still has to fight
            -- back, so arm up first.
            local equip_result, equip_detail = t.player.equip("rune_scimitar")
            t.step("hunt.equip_scimitar", equip_result == "ok" and "PASS" or "FAIL",
                "player.equip(rune_scimitar) -> " .. tostring(equip_result)
                    .. " " .. tostring(equip_detail))

            t.exec("attack-gardener", t.player.attack, "falador_gardener", 2, 20)
            t.exec("gardener-dead", t.npc.await_dead, "falador_gardener", 60)

            -- await_dead resolves on a corpse's bar reading 0 -- the slot
            -- is still IN the pool for a few ticks after that, and
            -- hunt_dig's own npc_find(coord, falador_gardener, 10, 0) does
            -- not check health, so a dig thrown right after await_dead
            -- would still redirect to pirate_irate_gardener_attack. Wait
            -- for the corpse to actually leave the pool. await_gone
            -- answers ok with a nil detail -- a hollow verb -- so call it
            -- directly and write the read-back count ourselves.
            local gone_result = t.npc.await_gone("falador_gardener", 10, 20)
            local gone_check_result, gone_check_row = t.npc.nearest("falador_gardener", 10)
            local gone_check_detail = gone_check_result
            if gone_check_result == "ok" and type(gone_check_row) == "table" then
                gone_check_detail = "slot " .. tostring(gone_check_row.slot)
            end
            t.step("hunt.gardener_gone", gone_result == "ok" and "PASS" or "FAIL",
                "npc.await_gone(falador_gardener, 10) -> " .. tostring(gone_result)
                    .. " -- npc.nearest now " .. tostring(gone_check_detail))

            -- The fight carries the player off the dig tile chasing the
            -- gardener -- spade.rs2:9 wants distance(coord, 2999,3383,0)
            -- <= 1 -- so goto_tile back before the second dig.
            t.exec("goto-dig-again", t.player.goto_tile, 2999, 3383, 0)

            -- The gardener is gone -- the second spade from setup covers
            -- the real dig (the first spade is now a ground item at our
            -- feet, the same op1-drops-it side effect read-message hit
            -- above).
            t.exec("dig-treasure-again", t.player.inv_op, "spade", 1)
        end

        t.ticks(3)

        t.quest.expect_complete()

        -- dig.rs2's [queue,hunt_quest_complete] hands over a pirate_casket,
        -- not the loot directly -- confirm it landed before opening it.
        local casket_result, casket_count = t.inv.count("pirate_casket")
        t.check("reward.pirate_casket", casket_result == "ok" and casket_count == 1,
            string.format("pirate_casket count=%s (%s), want 1",
                tostring(casket_count), tostring(casket_result)))

        -- expect_complete's own journal_open/journal_close cycle (a modal
        -- interface) leaves the sidebar's own currently-DISPLAYED tab
        -- unsettled for a tick -- app_minimenu_ui_pick_live rejects an
        -- inv_op pick whose tab is not yet the one actually shown, a
        -- total no-op (no message, no page, no count change) rather than
        -- a refusal. Switch tabs and let it land before clicking.
        t.ui.tab("inventory")
        t.ticks(2)

        -- [opheld1,pirate_casket] is what actually grants the coins/ring/
        -- emerald.
        t.exec("open-casket", t.player.inv_op, "pirate_casket", 1)
        t.exec("open-casket-dialog", t.chat.play, {
            "mesbox:You open the casket and find 450 coins, a gold ring and an emerald!",
        })

        local after_gold_ring_result, after_gold_ring = t.inv.count("gold_ring")
        t.check("reward.gold_ring",
            before_gold_ring_result == "ok" and after_gold_ring_result == "ok" and
            after_gold_ring == before_gold_ring + 1,
            string.format("gold_ring %s -> %s (want +1), reads %s/%s",
                tostring(before_gold_ring), tostring(after_gold_ring),
                tostring(before_gold_ring_result), tostring(after_gold_ring_result)))

        local after_emerald_result, after_emerald = t.inv.count("emerald")
        t.check("reward.emerald",
            before_emerald_result == "ok" and after_emerald_result == "ok" and
            after_emerald == before_emerald + 1,
            string.format("emerald %s -> %s (want +1), reads %s/%s",
                tostring(before_emerald), tostring(after_emerald),
                tostring(before_emerald_result), tostring(after_emerald_result)))

        local after_coins_result, after_coins = t.inv.count("coins")
        t.check("reward.coins",
            before_coins_result == "ok" and after_coins_result == "ok" and
            after_coins == before_coins + 450,
            string.format("coins %s -> %s (want +450), reads %s/%s",
                tostring(before_coins), tostring(after_coins),
                tostring(before_coins_result), tostring(after_coins_result)))

        t.finish(0)
    end,
}
