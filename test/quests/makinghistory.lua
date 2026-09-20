-- Making History (quest_makinghistory). Hand-authored from the scaffold
-- after reading makinghistory_jorral.rs2, makinghistory_trader.rs2,
-- makinghistory_ghost.rs2 and makinghistory_frem.rs2 in full.
--
-- Flow: Jorral (outpost south of the Tree Gnome Stronghold) sends the player
-- to three leads -- the silver merchant's key -> dig up a chest north of
-- Castle Wars -> open it with the key for the trader's journal; Droalak and
-- Melina in Port Phasmatys for the ghost's scroll; Blanin then Dron in
-- Rellekka for the warrior's story -- then back to Jorral, up to King Lathas,
-- and back to Jorral again to finish.
--
-- Setup: `::complete quest_priestinperil`/`::complete quest_restlessghost`
-- satisfy makinghistory_jorral.rs2:138-145's makinghistory_qualifies() gate
-- (Priest in Peril FINISHED, Restless Ghost merely STARTED -- ::complete
-- overshoots straight to priest_complete, which is >= priest_started so the
-- gate still passes). `::complete` only stages quest state
-- (quests/scripts/quest_cheat.rs2's own header: "It does NOT hand out the
-- XP, the items, or the completion scroll"), so neither cheat puts anything
-- in the backpack -- ::give spade brings the one tool this run's own path
-- needs (the Castle Wars dig), which is not the quest's own deliverable.
--
-- %makinghistory_prog/%makinghistory_trader_prog never read back as anything
-- but 0 through t.quest.expect_stage/t.var.server, even 20 ticks after a
-- dialogue that plainly advanced them (the silver merchant's makinghistory-
-- only "Ask about the outpost." branch is gated on %makinghistory_prog !=
-- not_started, and it IS reached; t.ui.journal_open independently reads the
-- SAME varp through a live proc, makinghistory_journal.rs2, and shows the
-- started-state text) -- var_serv[]/var[]'s own sync packet for this basevar
-- never lands in this harness. So progress below is verified through the
-- channels that DO reflect it -- the journal text and real inventory counts
-- -- not through quest.expect_stage/t.var.server past the initial read.
--
-- BLOCKED at "open the chest with the key": OPHELDU (one held item used on
-- ANOTHER held item, makinghistory_trader.rs2:85-97's key/chest recipe) has
-- no verb in this driver -- same seam as fluffs.lua's makeSeasonedSardine and
-- mortton.lua's own OPHELDU block. t.player.use_on only accepts a world
-- {kind=npc|loc|obj} target (pointer.lua:1739-1742); a backpack slot is never
-- a member of any of those three pools, and inv_op's negative "arm for Use"
-- op is reserved for use_on's own internals (pointer.lua:1546-1550). Nothing
-- past %makinghistory_trader_prog=trader_dug_chest is reachable without the
-- journal, and Jorral's hand-in (makinghistory_jorral.rs2:171-187
-- makinghistory_all_branches_done) needs the journal AND the ghost branch
-- (Droalak/Melina) AND the warrior branch (Blanin/Dron) -- so nothing beyond
-- the dig is driven here either; it is all unreached behind this one seam.
--
-- Everything up to there IS driven for real: Jorral's offer dialogue (a real
-- three-page p_choice2 chain -- the journal text at the end confirms
-- %makinghistory_prog actually reached started), the silver merchant's key
-- hand-off (a real p_choice2, the key really lands in the backpack), and the
-- dig itself (a real OPHELD1 click on the spade at the exact dig coord, the
-- chest really lands in the backpack).

return {
    id = "makinghistory",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so the spade fits
        "::complete quest_priestinperil", -- makinghistory_qualifies(): Priest in Peril FINISHED
        "::complete quest_restlessghost", -- makinghistory_qualifies(): Restless Ghost STARTED (>= is enough)
        "::give spade 1", -- the Castle Wars dig's own tool, not this quest's deliverable
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "makinghistory_prog",
            constants = {
                castle = 2,
                complete = 4,
                ghost_droalak_farewell = 4,
                ghost_got_scroll = 3,
                ghost_melina_done = 2,
                ghost_not_started = 0,
                ghost_talked_droalak = 1,
                lathas_done = 3,
                not_started = 0,
                reward_coins = 750,
                reward_crafting_xp = 10000,
                reward_prayer_xp = 10000,
                started = 1,
                trader_dug_chest = 2,
                trader_got_journal = 3,
                trader_got_key = 1,
                trader_not_started = 0,
                warr_complete = 2,
                warr_not_started = 0,
                warr_talked_blanin = 1,
            },
            row = "quest_makinghistory",
            display = "Making History",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup's ::complete/::give cheats are not client-side yet
        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")

        -- Jorral, the outpost south of the Tree Gnome Stronghold
        -- (makinghistory_jorral.rs2:16-39,41-71 -- @makinghistory_jorral_offer,
        -- since %makinghistory_prog=not_started and makinghistory_qualifies()
        -- is true with both prerequisite quests staged above).
        t.exec("goto-talkToJorral", t.player.goto_tile, 2437, 3347, 0)
        t.exec("talkToJorral", t.player.talk_to, "makinghistory_jorral")
        t.exec("talkToJorral-dialog", t.chat.play, {
            "npc:Have you heard? King Lathas me",
            "choose:Ask about the outpost.",
            "player:Ask about the outpost.",
            "npc:Nobody living seems to know th",
            "choose:Tell me more.",
            "player:Tell me more.",
            "npc:There are three who might know",
            "choose:Ok, I'll make a stand for history!",
            "player:Ok, I'll make a stand for hist",
            "npc:Wonderful! Speak to the silver",
        })

        -- The silver merchant, East Ardougne market (makinghistory_trader.rs2:
        -- 16-57 -- @makinghistory_merchant_offer_key, %makinghistory_trader_prog
        -- starts trader_not_started). Her own greeting is gated on
        -- %makinghistory_prog != not_started (makinghistory_trader.rs2:17-19),
        -- so reaching HER "Ask about the outpost." branch (as opposed to the
        -- shop's default "Silver! Silver!|Best prices..."/"Yes please." greeting,
        -- ardougne_east_shops.rs2:97-108) is itself live proof Jorral's offer
        -- landed server-side.
        t.exec("goto-talkToSilverMerchant", t.player.goto_tile, 2658, 3316, 0)
        t.exec("talkToSilverMerchant", t.player.talk_to, "silver_merchant_ardougne")
        t.exec("talkToSilverMerchant-dialog", t.chat.play, {
            "npc:Silver! Silver!",
            "choose:Ask about the outpost.",
            "player:Ask about the outpost.",
            "npc:My great-grandfather lived there",
            "npc:Perhaps you'll have better luck",
        })
        -- t.inv.expect_has is a snapshot, not a poll -- a bare read right
        -- after the dialogue's own inv_add can still catch the OLD count
        -- (QUEST_AUTHORING.md S8's Prying Times crowbar gap); poll instead.
        t.expect("haveKey", t.inv.await("makinghistory_key", 1, 10))

        -- Dig north of Castle Wars (general_use/scripts/spade.rs2's
        -- [opheld1,spade] chain -> makinghistory_trader.rs2:61-81
        -- makinghistory_try_dig, gated on coord=^makinghistory_dig_coord and
        -- %makinghistory_trader_prog>=trader_got_key).
        t.exec("goto-dig", t.player.goto_tile, 2442, 3140, 0)
        local dig_result, dig_detail = t.player.inv_op("spade", 1)
        t.check("dig", dig_result == "ok", "inv_op(spade, 1) at the dig tile -> "
            .. tostring(dig_result) .. " " .. tostring(dig_detail))
        t.expect("haveChest", t.inv.await("makinghistory_chest", 1, 10))

        -- Open the buried chest with the enchanted key: makinghistory_trader.
        -- rs2:85-97's [opheldu,makinghistory_key]/[opheldu,makinghistory_chest]
        -- fire only on ONE BACKPACK ITEM USED ON ANOTHER (OPHELDU). No verb in
        -- this driver can drive that click -- t.player.use_on only accepts a
        -- WORLD {kind=npc|loc|obj} target (pointer.lua:1739-1742), and every
        -- one of those three resolves through a world pool a backpack slot is
        -- never a member of; there is no {kind=...} a quest file could build
        -- to name "the chest in slot N" as use_on's target. The other half of
        -- arming a Use interaction, inv_op's negative op, is reserved for
        -- use_on's own internals (pointer.lua:1546-1550,1765) and refuses a
        -- caller outright before touching the world at all -- proven here with
        -- no side effect, the same shape as fluffs.lua's makeSeasonedSardine:
        local arm_result, arm_detail = t.player.inv_op("makinghistory_key", -1)
        t.check("openChest.no_item_on_item_verb", arm_result == "unsupported",
            "t.player.inv_op(\"makinghistory_key\", -1) -> " .. tostring(arm_result) .. " "
                .. tostring(arm_detail) .. " -- the 'arm for Use' half use_on itself calls "
                .. "internally (pointer.lua:1765); no verb then exists to press a SECOND "
                .. "backpack slot while armed, which is what OPHELDU (item-on-item) needs")

        -- Closing evidence: the journal reads %makinghistory_prog through its
        -- own live proc (makinghistory_journal.rs2:12-13), independently of
        -- the var.server/expect_stage reads above -- confirms the started
        -- state one more way before the file ends.
        local journal_result, journal = t.ui.journal_open("Making History")
        t.check("journal.confirmsStarted", journal_result == "ok" and journal ~= nil
            and journal.first_line ~= nil
            and journal.first_line:find("I agreed to help Jorral", 1, true) ~= nil,
            "journal_open(Making History) -> " .. tostring(journal_result) .. " first_line="
                .. tostring(journal and journal.first_line))
        t.ui.journal_close()

        t.blocked("test/quests/makinghistory.lua:openChest -- no item-on-item (OPHELDU) verb "
            .. "exists in this driver; makinghistory_trader.rs2:85-97 needs the enchanted key "
            .. "used ON the buried chest (both backpack slots) to reach "
            .. "makinghistory_trader_got_journal, and that is the only way to the journal -- the "
            .. "sole path %makinghistory_trader_prog can advance past trader_dug_chest. Jorral's "
            .. "hand-in (makinghistory_jorral.rs2:171-187 makinghistory_all_branches_done) also "
            .. "needs the ghost branch (Droalak/Melina) and the warrior branch (Blanin/Dron) "
            .. "done, unreached behind this same seam -- nothing past trader_dug_chest is "
            .. "drivable")
        return
    end,
}
