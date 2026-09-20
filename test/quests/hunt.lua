-- Pirate's Treasure ("hunt" test id / quest_hunt content dir) -- client-driven
-- end to end through the real client. Redbeard Frank asks for a bottle of
-- Karamja Rum; the setup cheat carries one so the second talk hands it
-- straight over (redbeard_frank.rs2's [label,redboard_progress]/
-- [label,redbeard_hand_rum] only check inv_total(inv, karamja_rum) >= 1 --
-- there is no separate use-on branch to exercise). Then Hector's chest in
-- the Blue Moon Inn (use the key on it -- [oplocu,piratechest]; op1 alone
-- answers "The chest is locked."), reading the pirate message it holds
-- ([opheld1,piratemessage]), and digging at the Falador Park cross
-- ([opheld1,spade] recognises the tile 0_46_52_55_55 = 2999,3383,0 and jumps
-- to quest_hunt/scripts/dig.rs2's [label,hunt_dig]).
--
-- dig.rs2's hunt_dig redirects to [label,pirate_irate_gardener_attack]
-- instead of completing the dig when falador_gardener (hitpoints=7,
-- attack=1, aggressive) is within 10 tiles of the dig spot -- its own spawn
-- (m46_52.spawn) is ~3.6 tiles from 2999,3383, so the FIRST dig is expected
-- to provoke it rather than complete. This file arms a bronze_dagger and
-- fights back; if that ever lands a kill it digs again and finishes the
-- quest for real, but measured here (twice, unarmed and armed) the
-- player's own hp falls steadily while the npc shows no sign of taking
-- damage back from repeated click_minimenu Attack clicks, so the run ends
-- BLOCKED at that fight instead -- see the blocked reason at the bottom.

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
        "::give karamja_rum 1",
        -- Prerequisite combat gear for the gardener the dig provokes (see
        -- dig-treasure below) -- not the quest's own deliverable.
        "::give bronze_dagger 1",
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

        -- Talk to Redbeard again -- [label,redboard_progress] sees
        -- inv_total(inv, karamja_rum) >= 1 (the setup cheat's bottle) and
        -- falls straight through "Yes, I've got some." into [label,
        -- redbeard_hand_rum] in the SAME dialogue.
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
            -- attack=1 (combat_stats.generated.npc) -- trivial on paper,
            -- but not automatic: it only goes hostile here
            -- (npc_setmode(opplayer2)), the player still has to fight
            -- back, so arm up first.
            local equip_result, equip_detail = t.player.equip("bronze_dagger")
            t.step("hunt.equip_dagger", equip_result == "ok" and "PASS" or "FAIL",
                "player.equip(bronze_dagger) -> " .. tostring(equip_result)
                    .. " " .. tostring(equip_detail))

            local gardener = t.player.by_symbol("npc", "falador_gardener")
            local gone_result = "timeout"
            for attempt = 1, 6 do
                t.exec("attack-gardener-" .. attempt, t.drive.click_minimenu, gardener, 2)
                gone_result = t.npc.await_gone("falador_gardener", 10, 8)
                if gone_result == "ok" then
                    break
                end
            end

            if gone_result ~= "ok" then
                t.blocked("dig.rs2:9-11 [label,pirate_irate_gardener_attack] -- "
                    .. "falador_gardener (hitpoints=7 attack=1) is still alive after "
                    .. "6 click_minimenu(target, 2) [Attack] re-engagements (~48 "
                    .. "ticks) armed with a bronze_dagger -- the player's own hp "
                    .. "fell steadily across the fight while the npc showed no sign "
                    .. "of taking damage (measured over two attempts this suite, one "
                    .. "unarmed); this driver has no other combat verb to resolve it")
                return
            end

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
