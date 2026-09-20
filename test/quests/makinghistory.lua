-- Making History (quest_makinghistory). Hand-authored from the scaffold
-- after reading makinghistory_jorral.rs2, makinghistory_trader.rs2,
-- makinghistory_frem.rs2, makinghistory_ghost.rs2 and
-- makinghistory_journal.rs2 in full.
--
-- Flow: Jorral (outpost south of the Tree Gnome Stronghold) sends the player
-- to three leads -- the silver merchant's key -> dig up a chest north of
-- Castle Wars -> open it with the key for the trader's journal; Blanin then
-- Dron in Rellekka for the warrior's story; Droalak and Melina outside Port
-- Phasmatys for the ghost's scroll -- then back to Jorral, up to King Lathas,
-- and back to Jorral again to finish.
--
-- Setup: `::complete quest_priestinperil`/`::complete quest_restlessghost`
-- satisfy makinghistory_jorral.rs2's makinghistory_qualifies() gate (Priest
-- in Peril FINISHED, Restless Ghost merely STARTED). `::complete` only
-- stages quest state, so neither cheat puts anything in the backpack --
-- `::give spade` is the Castle Wars dig's own tool, not this quest's
-- deliverable.
--
-- BLOCKED at the Castle Wars dig (makinghistory_trader.rs2's
-- makinghistory_try_dig, spliced additively into general_use/spade.rs2's
-- [opheld1,spade] chain): everything the dig depends on is driven for real
-- and confirmed present -- Jorral's three-page offer (matched verbatim
-- against makinghistory_jorral.rs2:29-45), the silver merchant's
-- quest-specific "Ask about the outpost." branch (matched against
-- makinghistory_trader.rs2:29-40, not the shop's generic greeting), the key
-- really in the backpack (a real t.inv.await poll), and the exact dig tile
-- (2442,3140,0, decoded from ^makinghistory_dig_coord = 0_38_49_10_4) reached
-- four different ways -- but t.player.inv_op("spade", 1) there produces no
-- observable effect at all, confirmed reproducibly. See the t.blocked() call
-- below for the full evidence trail.
--
-- The quest's OWN deliverable past this point -- the OPHELDU chest-opening
-- (makinghistory_trader.rs2:85-97, key used ON the buried chest) -- has a
-- real verb now (t.player.use_item_on_item, landed in b5b720b49,
-- build/quest_gate/q1_useitem3, 21/21) and is unreached only because the
-- dig above never yields a chest to use it on.
--
-- THE VARP SEAM (QUEST_AUTHORING.md section 8, still open, and NOT what this
-- file blocks on): `[makinghistory]` has an EMPTY body in
-- `OSRS-Content/osrs239-content/configs/all.varp` (`quest_makinghistory/
-- configs/` holds only the `.constant` file -- no `makinghistory.varp`
-- declaring `transmit=yes`, unlike `quest_runemysteries/configs/
-- quest_runemysteries.varp`'s own `[runemysteries] transmit=yes`), so the
-- CLIENT'S copy of every varbit on it never updates. Confirmed here too
-- (`prog.serverProbeAfterOffer`): even `t.var.server` reads 0 for
-- %makinghistory_prog immediately after ui.journal_open independently
-- proves it is 1 server-side, so THIS basevar's server-side debug read is
-- not trustworthy evidence either -- progress above is cross-checked only
-- through `t.ui.journal_open` (runs the quest's own `~makinghistory_journal`
-- proc server-side and returns literal text) and `t.inv.*` (real backpack
-- contents), never `t.quest.expect_stage`/`t.var.server` past `not_started`.

return {
    id = "makinghistory",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_priestinperil", -- makinghistory_qualifies(): Priest in Peril FINISHED
        "::complete quest_restlessghost", -- makinghistory_qualifies(): Restless Ghost STARTED (>= is enough)
        "::give spade 1", -- the Castle Wars dig's own tool, not this quest's deliverable
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "makinghistory_prog",
            constants = {
                not_started = 0,
                started = 1,
                castle = 2,
                lathas_done = 3,
                complete = 4,
                trader_not_started = 0,
                trader_got_key = 1,
                trader_dug_chest = 2,
                trader_got_journal = 3,
                warr_not_started = 0,
                warr_talked_blanin = 1,
                warr_complete = 2,
                ghost_not_started = 0,
                ghost_talked_droalak = 1,
                ghost_melina_done = 2,
                ghost_got_scroll = 3,
                ghost_droalak_farewell = 4,
                reward_crafting_xp = 1000,
                reward_prayer_xp = 1000,
                reward_coins = 750,
            },
            row = "quest_makinghistory",
            display = "Making History",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup's ::complete/::give cheats are not client-side yet

        -- The one stage read the client CAN see: nothing has touched the
        -- basevar yet, so client==server==0 trivially, before the seam
        -- above ever bites.
        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")

        local qp_before_result, qp_before = t.var.varp("qp")
        t.check("qp.baseline", qp_before_result == "ok",
            "t.var.varp(\"qp\") before any quest progress -> " .. tostring(qp_before_result)
                .. " " .. tostring(qp_before))

        -- Jorral, the outpost south of the Tree Gnome Stronghold
        -- (makinghistory_jorral.rs2 @makinghistory_jorral_offer, since
        -- %makinghistory_prog=not_started and makinghistory_qualifies() is
        -- true with both prerequisite quests staged above).
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

        local prog_probe_result, prog_probe = t.var.server("makinghistory_prog")
        t.check("prog.serverProbeAfterOffer", true, "t.var.server(makinghistory_prog) right after Jorral's "
            .. "offer -> " .. tostring(prog_probe_result) .. " " .. tostring(prog_probe))

        -- The client's own copy of %makinghistory_prog is stuck at 0 from
        -- here on (the seam above) -- cross-check through the journal's own
        -- server-side proc instead.
        local started_journal_result, started_journal = t.ui.journal_open("Making History")
        t.check("quest.stage.started", started_journal_result == "ok" and started_journal ~= nil
            and started_journal.first_line ~= nil
            and started_journal.first_line:find("I agreed to help Jorral", 1, true) ~= nil,
            "journal_open(Making History) -> " .. tostring(started_journal_result) .. " first_line="
                .. tostring(started_journal and started_journal.first_line)
                .. " -- channel: ui.journal_open (server-side ~makinghistory_journal proc, unaffected by "
                .. "the client varp-transmit seam)")
        t.ui.journal_close()

        -- === Trader branch: the silver merchant, East Ardougne market =====
        -- (makinghistory_trader.rs2 @makinghistory_merchant_offer_key,
        -- %makinghistory_trader_prog starts trader_not_started). Her own
        -- greeting is gated on %makinghistory_prog != not_started, so
        -- reaching HER "Ask about the outpost." branch is itself live proof
        -- Jorral's offer landed server-side.
        t.exec("goto-talkToSilverMerchant", t.player.goto_tile, 2658, 3316, 0)
        t.exec("talkToSilverMerchant", t.player.talk_to, "silver_merchant_ardougne")
        t.exec("talkToSilverMerchant-dialog", t.chat.play, {
            "npc:Silver! Silver!",
            "choose:Ask about the outpost.",
            "player:Ask about the outpost.",
            "npc:My great-grandfather lived there",
            "npc:Perhaps you'll have better luck",
        })
        t.expect("haveKey", t.inv.await("makinghistory_key", 1, 10))
        local traderprog_after_key_result, traderprog_after_key = t.var.server("makinghistory_trader_prog")
        t.check("traderProg.serverProbeAfterKey", true, "t.var.server(makinghistory_trader_prog) right after "
            .. "the key lands -> " .. tostring(traderprog_after_key_result) .. " " .. tostring(traderprog_after_key))

        -- Dig north of Castle Wars (general_use spade chain ->
        -- makinghistory_trader.rs2 makinghistory_try_dig, gated on
        -- coord=^makinghistory_dig_coord and
        -- %makinghistory_trader_prog>=trader_got_key). This is the seam the
        -- file ends on -- see the banner below for the full evidence trail.
        -- Landing a few tiles short and WALKING the last stretch (rather than
        -- letting goto_tile's own teleport land exactly on the dig tile) was
        -- tried first, on the theory that a cheat-teleport skips some
        -- zone-entry step a real arrival triggers; it made no difference.
        t.exec("goto-dig", t.player.goto_tile, 2439, 3140, 0)
        local walk_result, walk_detail = t.player.walk_to(2442, 3140, 15)
        t.check("walk-dig", walk_result == "ok",
            "walk_to(2442, 3140) the last three tiles on foot -> " .. tostring(walk_result)
                .. " " .. tostring(walk_detail))
        local dig_tile_result, dig_tile = t.world.tile()
        t.check("dig.tileProbe", true, "world.tile() right before the dig click -> " .. tostring(dig_tile_result)
            .. " " .. tostring(dig_tile and (dig_tile.x .. "," .. dig_tile.z .. "," .. dig_tile.level))
            .. " (^makinghistory_dig_coord 0_38_49_10_4 decodes to 2442,3140,0)")
        local dig_result, dig_detail = t.player.inv_op("spade", 1)
        t.check("dig", true, "inv_op(spade, 1) at the dig tile, key already in the backpack -> "
            .. tostring(dig_result) .. " " .. tostring(dig_detail)
            .. " -- no chat line, no interface, nothing observable at all: not even content's own "
            .. "fallback ~displaymessage(^dm_default) (\"Nothing interesting happens.\") printed")
        local chest_result, chest_after = t.inv.await("makinghistory_chest", 1, 10)
        t.check("haveChest", true, "inv.await(makinghistory_chest, 1, 10) after the dig click -> "
            .. tostring(chest_result) .. " " .. tostring(chest_after)
            .. " -- the chest was never added; makinghistory_try_dig's own mes()/inv_add never ran")

        t.blocked("test/quests/makinghistory.lua:dig -- t.player.inv_op(\"spade\", 1) at the documented dig "
            .. "tile (2442,3140,0, ^makinghistory_dig_coord 0_38_49_10_4, makinghistory_trader.rs2's own "
            .. "makinghistory_try_dig) produces no observable effect at all: no chat line (not even "
            .. "content's own ~displaymessage(^dm_default) fallback every other unclaimed op prints), no "
            .. "interface, and makinghistory_chest never lands in the backpack even after a 10-tick poll -- "
            .. "confirmed reproducibly across four separate attempts (an immediate click, a 2-tick settle, a "
            .. "5-tick settle, and landing three tiles short and WALKING the last stretch instead of "
            .. "teleporting exactly onto the tile), all landing world.tile()=2442,3140,0 exactly. Everything "
            .. "the dig depends on is confirmed present and correct up to this point: makinghistory_key is "
            .. "in the backpack (haveKey, a real t.inv.await poll), the merchant's OWN quest-specific "
            .. "dialogue branch was reached and driven verbatim (talkToSilverMerchant-dialog, matched "
            .. "against makinghistory_trader.rs2:29-40 -- the shop's generic 'Silver! Silver!|...Yes "
            .. "please.' greeting is a DIFFERENT text and would have mismatched), and %makinghistory_prog "
            .. "reaching 'started' is independently confirmed through ui.journal_open's own server-side "
            .. "~makinghistory_journal proc (quest.stage.started, first_line='I agreed to help Jorral...', "
            .. "a channel that does not depend on the client varp-transmit seam QUEST_AUTHORING.md section 8 "
            .. "documents for this same basevar). t.var.server(\"makinghistory_prog\")/"
            .. "t.var.server(\"makinghistory_trader_prog\") were also probed at both points (prog."
            .. "serverProbeAfterOffer, traderProg.serverProbeAfterKey) and read 0 even where the journal "
            .. "proc independently proves the value is non-zero -- so this driver's server-side varbit read "
            .. "is not trustworthy evidence either way for this basevar, and is not what this row's "
            .. "conclusion rests on. click_loc/use_on's own \"stepped off the target tile\"/\"pressed again "
            .. "from side N\" recovery (QUEST_AUTHORING.md section 8) does not apply here -- inv_op is a "
            .. "held-item op with no world target to step around or re-aim at. This is the quest's own "
            .. "deliverable (the Castle Wars dig cannot be cheated past per trap 16), so the file ends here.")
        return
    end,
}
