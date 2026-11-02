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
-- `::give spade`/`::give amulet_of_ghostspeak`/`::give strung_sapphire_amulet`
-- are all prerequisite items Quest Helper lists as brought-along (the dig's
-- own tool, the Restless-Ghost-era ghostspeak amulet needed to understand
-- Droalak/Melina at all -- makinghistory_ghost.rs2's own
-- makinghistory_has_ghostspeak proc, worn not carried -- and the strung
-- sapphire amulet Droalak's own dialogue describes handing over but which no
-- `inv_add` anywhere in makinghistory_ghost.rs2 actually grants, so it is
-- carried in exactly the same "bring your own" sense as the spade), never
-- this quest's own deliverable (trap 16).
--
-- RESUMED past the Castle Wars dig seam this file used to block on
-- (queue.py's last_failure after 62c051fb8: app_minimenu_run_option was
-- dropping the fabricated INV_SLOT pick in silence whenever the backpack tab
-- was not yet painted, and the bridge reported that silent drop as a
-- dispatch; inv_op now answers refused with the condition named and
-- re-presses the tab, and the same file, unchanged, measured dig -> ok and
-- haveChest -> ok afterwards). Driven on from the chest: the OPHELDU
-- key-on-chest open (makinghistory_trader.rs2:85-97, t.player.use_item_on_item),
-- Blanin's briefing and Dron's twelve-question riddle in Rellekka
-- (makinghistory_frem.rs2), Droalak's amulet errand to Melina and the scroll
-- (makinghistory_ghost.rs2, both native multi-npc shells), the hand-in to
-- Jorral, the letter to King Lathas and back, and the completion rewards
-- (makinghistory_jorral.rs2's makinghistory_quest_complete).
--
-- THE VARP SEAM (QUEST_AUTHORING.md section 8, still open, and NOT a
-- blocker -- it just means every stage read past not_started goes through
-- ui.journal_open/t.inv.* instead of t.quest.expect_stage/t.var.server):
-- `[makinghistory]` has an EMPTY body in
-- `OSRS-Content/osrs-content/configs/all.varp` (`quest_makinghistory/
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
-- (That seam is fixed now -- see "the committed state" near the end -- so the
-- server probes below grade their literal values.)
--
-- WALLS (fix_b59, re-driven fix_b65; orchestrator rule 2026-10-03, owner
-- ruling 2026-10-05 on the first goto): a goto_tile departs from and lands
-- on an open, walkable tile outside; every door, stair, barrier and gate
-- between the player and the target is pressed by the driver's crossing
-- verbs, going in and coming out. Checked against the map squares with
-- test/quests/orchestrator/matthew-mbp-m4/reports/sample_tools/
-- {reach,comp,locs_near}.py --root <worktree> (doors closed):
--   * The start: the fixture stands in Lumbridge, east of the members'
--     walls (reach 3206,3233 -> 2431,3347 NEEDS-DOOR via membergater
--     2933,3320). Camelot Teleport (magic_spells.dbrow: level 45, 5 air +
--     1 law, lands 2757,3478), then Camelot -> the outpost is open ground
--     (REACH closed-doors len=469).
--   * Jorral's outpost (maps/m38_52.jl2): room x 2434-2438 z 3346-3349, double
--     door makinghistory_doubledoorl/r on the west edge (2433,3347-3348; both
--     leaves swing together). Entered from 2432,3347 on every visit and left
--     the same way before the next goto.
--   * Open ground (REACH with every door closed): outpost -> Ardougne market
--     (300), market -> the Castle Wars dig (394), dig -> Blanin in Rellekka
--     (830 at margin 80), Blanin -> Dron (46), outpost <-> the castle door
--     (231).
--   * Morytania: a flood from outside never crosses the Salve. The way in is
--     the one Priest in Peril opens: Varrock Teleport from Rellekka (level
--     25, 3 air + 1 fire + 1 law, lands 3213,3424), the Varrock members' gate
--     fai_varrock_member_gatel 3319,3468 (the only walk from Varrock to the
--     temple: reach 3213,3424 -> 3318,3468 REACH 149, 3321,3468 -> 3405,3506
--     REACH 122), the Paterdomus trapdoor (3405,3507), the two mausoleum
--     gates, Drezel's advice (mausoleum_drezel.rs2:145-154, 60 -> 61) and
--     the holy barrier (mausoleum_interactions.rs2:26-31, out at 3423,3485).
--     3423,3485 -> the Phasmatys west barrier 3651,3485 is open (REACH 294).
--   * Port Phasmatys is a walled town (flood from inside: 1932 tiles, no way
--     out) behind ahoy_town_barrier_multi; the west barrier (3652,3485, faces
--     east) charges the 2-ectotoken toll on op4 Pay-toll (quest_ghostsahoy/
--     scripts/ahoy_hub.rs2 [label,ahoy_barrier_pass]).
--   * The way OUT of Morytania is a real teleport: Camelot Teleport again.
--   * Melina's house (maps/m57_54.jl2) x 3671-3678 z 3477-3484, door
--     ahoy_harbour_door on the north edge of 3676,3476.
--   * East Ardougne castle (maps/m40_51.jl2): double door w_ardougnedoubledoorl/r
--     on the west edge of 2576,3298-3299; the stairs (2571-2572,3295-3297)
--     climbed from 2571,3298 (no up maplink row: +1 plane on the stand tile);
--     King Lathas's room x 2575-2579 z 3292-3294,1 behind elfdoor on the west
--     edge of 2575,3293,1; down by stairstop from 2571,3294,1 (maplink
--     maplink_1_40_51_11_30_down -> 2571,3298,0).

return {
    id = "makinghistory",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_priestinperil", -- makinghistory_qualifies(): Priest in Peril FINISHED
        "::complete quest_restlessghost", -- makinghistory_qualifies(): Restless Ghost STARTED (>= is enough)
        "::give spade 1", -- the Castle Wars dig's own tool, not this quest's deliverable
        "::give amulet_of_ghostspeak 1", -- worn to understand Droalak/Melina, brought along not granted
        "::give strung_sapphire_amulet 1", -- Melina's reconciliation gift; no inv_add for it anywhere in makinghistory_ghost.rs2
        "::give dagger_wolfbane 1", -- Priest in Peril's own reward (::complete grants no items); Drezel's advice branch needs it held (mausoleum_drezel.rs2:29-34)
        "::give ectotoken 2", -- the Port Phasmatys barrier toll (ahoy_hub.rs2, ^ahoy_barrier_toll = 2), carried like coins
        "::setlevel magic 45", -- Camelot Teleport (magic_spells.dbrow: level 45) to the outpost and out of Morytania
        -- Camelot Teleport x2 (5 air + 1 law each) and Varrock Teleport x1
        -- (3 air + 1 fire + 1 law): the exact runes the three casts cost.
        "::give airrune 13",
        "::give lawrune 3",
        "::give firerune 1",
    },

    run = function(t)
        -- The wire audit's tick log (docs/rsprox-analysis/quests/making_history.md).
        t.ticklog.start()
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tostring(tt.x) .. "," .. tostring(tt.z) .. "," .. tostring(tt.level)
            end
            return tostring(r)
        end

        -- Long hauls are real teleports, cast from the spellbook and graded
        -- on the answer, the exact runes (magic_spells.dbrow) and the landing.
        local function camelot_teleport(name)
            t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = name,
                runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        end
        local function varrock_teleport(name)
            t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = name,
                runes = { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = "Varrock square" })
        end

        -- Jorral's outpost: in and out through its west double door (both
        -- leaves swing together; pass_door presses the closed leaf or walks
        -- through the open one).
        local function in_outpost(tt)
            return tt.level == 0 and tt.x >= 2434 and tt.x <= 2438 and tt.z >= 3346 and tt.z <= 3349
        end
        local function outpost_in(prefix)
            t.exec("goto-" .. prefix, t.player.goto_tile, 2431, 3347, 0)
            t.exec(prefix .. ".outpostDoorIn", t.player.pass_door, { closed = "makinghistory_doubledoorr",
                open = "makinghistory_doubledoorr_open", at = { 2433, 3347, 0 }, near = { 2432, 3347 },
                far = { 2436, 3348 }, far_ok = in_outpost, far_desc = "inside Jorral's outpost, x 2434-2438 z 3346-3349" })
        end
        local function outpost_out(prefix)
            t.exec(prefix .. ".outpostDoorOut", t.player.pass_door, { closed = "makinghistory_doubledoorr",
                open = "makinghistory_doubledoorr_open", at = { 2433, 3347, 0 }, near = { 2435, 3347 },
                far = { 2431, 3347 }, far_ok = function(tt) return tt.level == 0 and tt.x <= 2432 end,
                far_desc = "outside the outpost's west door, x <= 2432" })
        end

        local bind_result, bind_detail = t.quest.bind({
            varp = "varb1383_makinghistory_prog",
            constants = {
                not_started = 0,
                started = 1,
                castle = 2,
                lathas_done = 3,
                complete = 4,
                trader_not_started = 0,
                trader_got_key = 1,
                trader_dug_chest = 2,
                trader_done = 4, -- RSProx 4463 [1009]
                warr_not_started = 0,
                warr_talked_blanin = 2, -- [1242]
                warr_answered = 3, -- [1496]
                warr_done = 4, -- [1554]
                ghost_not_started = 0,
                ghost_talked_droalak = 2, -- RSProx 4462 [3829]
                ghost_melina_done = 4, -- 4463 [295]
                ghost_got_scroll = 5, -- [324]
                ghost_done = 6, -- [490]
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

        local qp_before_result, qp_before = t.var.varp("varp101_qp")
        t.check("qp.baseline", qp_before_result == "ok" and qp_before == 2,
            "t.var.varp(\"qp\") before any quest progress -> " .. tostring(qp_before_result)
                .. " " .. tostring(qp_before) .. " (want 2: the setup's ::complete of Priest in Peril and The Restless Ghost, 1 each)")

        -- Wear the ghostspeak amulet now -- makinghistory_has_ghostspeak
        -- checks inv_total(worn, ...), not the backpack, and Droalak's own
        -- proc answers a dead "impossible to make out" mesbox with nothing
        -- worn at all, long before the Port Phasmatys leg below.
        t.exec("equip-ghostspeak", t.player.equip, "amulet_of_ghostspeak")

        -- Jorral, the outpost south of the Tree Gnome Stronghold
        -- (makinghistory_jorral.rs2 @makinghistory_jorral_offer, since
        -- %makinghistory_prog=not_started and makinghistory_qualifies() is
        -- true with both prerequisite quests staged above). The fixture
        -- stands in Lumbridge, east of the members' walls (WALLS): Camelot
        -- Teleport, then the open walk to the outpost.
        camelot_teleport("talkToJorral.camelotTeleport")
        outpost_in("talkToJorral")
        t.exec("talkToJorral", t.player.talk_to, "makinghistory_jorral")
        -- RSProx index 4460 [192..403]: the pages, the tour, the start, a lead.
        t.exec("talkToJorral-dialog", t.chat.play, {
            "player:Hi there.",
            "npc:All is lost!",
            "player:Sorry?",
            "npc:Just look around you!",
            "player:Great building?",
            "npc:Of course! This building has a history",
            "player:Ah, I see, a great history.",
            "npc:It's to be ripped apart",
            "player:Does the king not respect",
            "npc:Well, actually nobody knows",
            "npc:Wait a minute. You could help me!",
            "player:Me?",
            "npc:Yes! Uncover the history",
            "choose:Tell me more.",
            "player:Tell me more.",
        })
        local outpost_cutscene_mark = t.cutscene.mark()
        t.exec("mh.outpost.tellmore", t.chat.play, {
            "npc:That's what I like to hear.",
        })
        -- The tour runs 57 ticks with no page ([250..307]).
        t.ticks(60)
        t.exec("mh.outpost.page", t.chat.play, {
            "npc:If all goes well, I hope to be able to turn it into a museum",
            "choose:Yes.",
            "player:OK, I will make a stand for history!",
            "npc:Oh thank you so much",
            "player:But where should I start?",
            "player:What do I need to do now?",
            "npc:There are three people that may be able to help:",
            "choose:A ghost in Port Phasmatys",
            "npc:I've been told that there's a ghost",
            "player:Sounds ominous.",
            "npc:He does indeed. It's Droalak.",
            "player:I'll track him down.",
            "npc:It might not be so simple!",
            "player:I've conversed with the dead before",
            "choose:Got to go, bye!",
            "player:Got to go, bye!",
        })
        if outpost_cutscene_mark ~= nil then
            t.exec("mh.outpost.cutscene", t.cutscene.await, "mh.outpost", { since = outpost_cutscene_mark, expect = {
                { op = "moveto", coord = "0_38_52_0_52", height = 1000 }, -- [252]
                { op = "lookat", coord = "0_38_52_4_18", height = 500 },
                { op = "moveto", coord = "0_38_52_0_33", height = 1000 }, -- [254]
                { op = "moveto", coord = "0_38_52_13_33", height = 1000 }, -- [264]
                { op = "moveto", coord = "0_38_52_13_2", height = 1000 }, -- [274]
                { op = "moveto", coord = "0_38_52_0_2", height = 1000 }, -- [284]
                { op = "lookat", coord = "0_38_52_6_22", height = 525 }, -- [294]
                { op = "moveto", coord = "0_38_52_3_19", height = 800 },
                { op = "reset" }, -- [307]
            } })
        end

        local prog_probe_result, prog_probe = t.var.server("varb1383_makinghistory_prog")
        t.check("prog.serverProbeAfterOffer", prog_probe_result == "ok" and prog_probe == 1,
            "t.var.server(makinghistory_prog) right after Jorral's offer -> " .. tostring(prog_probe_result) .. " "
                .. tostring(prog_probe) .. " (want 1, started)")

        -- The client's own copy of %makinghistory_prog is stuck at 0 from
        -- here on (the seam above) -- cross-check through the journal's own
        -- server-side proc instead.
        -- ui.journal_open measured an intermittent mount timeout elsewhere in
        -- this file under load; a short retry ladder absorbs that without
        -- weakening the check itself (it still grades on the final attempt).
        local started_journal_result, started_journal
        for _ = 1, 3 do
            started_journal_result, started_journal = t.ui.journal_open("Making History")
            if started_journal_result == "ok" then
                break
            end
            t.ui.journal_close()
            t.ticks(5)
        end
        t.check("quest.stage.started", started_journal_result == "ok" and started_journal ~= nil
            and started_journal.first_line ~= nil
            and started_journal.first_line:find("Jorral wants to save the outpost", 1, true) ~= nil,
            "journal_open(Making History) -> " .. tostring(started_journal_result) .. " first_line="
                .. tostring(started_journal and started_journal.first_line)
                .. " -- channel: ui.journal_open (server-side ~makinghistory_journal proc, unaffected by "
                .. "the client varp-transmit seam)")
        t.ui.journal_close()
        outpost_out("leaveJorral")

        -- === Trader branch: the silver merchant, East Ardougne market =====
        -- (makinghistory_trader.rs2 @makinghistory_merchant_offer_key,
        -- %makinghistory_trader_prog starts trader_not_started). Her own
        -- greeting is gated on %makinghistory_prog != not_started, so
        -- reaching HER "Ask about the outpost." branch is itself live proof
        -- Jorral's offer landed server-side.
        t.exec("goto-talkToSilverMerchant", t.player.goto_tile, 2658, 3316, 0)
        t.exec("talkToSilverMerchant", t.player.talk_to, "silver_merchant_ardougne")
        -- RSProx index 4463 [538..579].
        t.exec("talkToSilverMerchant-dialog", t.chat.play, {
            "npc:Silver! Silver! Best prices for buying and selling in all",
            "choose:Ask about the outpost.",
            "player:Are you Erin?",
            "npc:That I am. What do you want?",
            "player:This shouldn't take too long.",
            "npc:What's it to you?",
            "player:I'm doing some research for a man called Jorral.",
            "npc:That can only cause chaos!",
            "player:I see.",
            "npc:The only thing I have of his is a key.",
            "player:No idea at all?",
            "npc:Well, I imagine it's to some sort of chest",
            "player:It's better than nothing.",
            "npc:Why not. I can't use it anyway.",
        })
        t.expect("haveKey", t.inv.await("makinghistory_key", 1, 10))
        -- [582]: the first feel since login has no comparison; 2659,3316 is
        -- 220 from the treasure.
        t.exec("feelKey.market", t.player.inv_op, "makinghistory_key", 1)
        t.expect("feelKey.market.line", t.msg.expect("It's freezing"))
        local traderprog_after_key_result, traderprog_after_key = t.var.server("varb1384_makinghistory_trader_prog")
        t.check("traderProg.serverProbeAfterKey", traderprog_after_key_result == "ok" and traderprog_after_key == 1,
            "t.var.server(makinghistory_trader_prog) right after the key lands -> " .. tostring(traderprog_after_key_result)
                .. " " .. tostring(traderprog_after_key) .. " (want 1, trader_got_key)")

        -- Dig north of Castle Wars (general_use spade chain ->
        -- makinghistory_trader.rs2 makinghistory_try_dig, gated on
        -- coord=^makinghistory_dig_coord and
        -- %makinghistory_trader_prog>=trader_got_key). This used to be a
        -- blocker -- app_minimenu_run_option silently dropped the fabricated
        -- INV_SLOT pick whenever the backpack tab was not yet painted -- but
        -- inv_op now re-presses the tab and names a refused condition, so a
        -- direct goto_tile onto the documented coordinate is the whole of it
        -- (QUEST_AUTHORING.md section 2), no more short-land-and-walk needed.
        t.exec("goto-dig", t.player.goto_tile, 2442, 3140, 0)
        local dig_tile_result, dig_tile = t.world.tile()
        t.check("dig.tileProbe", dig_tile_result == "ok" and dig_tile.x == 2442 and dig_tile.z == 3140 and dig_tile.level == 0,
            "world.tile() right before the dig click -> " .. tile_text(dig_tile_result, dig_tile)
                .. " (^makinghistory_dig_coord 0_38_49_10_4 decodes to 2442,3140,0)")
        -- [939]: steaming at 2442,3140 (3 from the treasure), no comparison.
        t.exec("feelKey.dig", t.player.inv_op, "makinghistory_key", 1)
        t.expect("feelKey.dig.line", t.msg.expect("The key is steaming. It must be right below your feet."))
        -- [941..943]: the box on the dig, the chest on its continue.
        t.exec("dig", t.player.inv_op, "spade", 1)
        t.exec("dig-box", t.chat.play, { "mesbox:You use the spade and find a chest. Wonder what's inside?" })
        t.expect("haveChest", t.inv.await("makinghistory_chest", 1, 10))

        -- Open the chest with the enchanted key (makinghistory_trader.rs2's
        -- OPHELDU pair, makinghistory_open_chest label). Its mes() is a
        -- game-message line, not a modal page -- the label runs straight
        -- through to the inv_del/inv_add with nothing to click, so
        -- use_item_on_item settles on the backpack change itself (measured:
        -- "gained makinghistory_journal 0->1; lost makinghistory_chest 1->0"
        -- in the same row, no dialogue ever mounts).
        -- [948..950]: the box on the use, the journal on its continue.
        t.exec("openChest", t.player.use_item_on_item, "makinghistory_key", "makinghistory_chest")
        t.exec("openChest-box", t.chat.play, { "mesbox:You look in the chest and find a journal" })
        t.expect("haveJournal", t.inv.await("makinghistory_journal", 1, 10))
        -- [952..959]: Drozal's Journal, four spreads.
        t.exec("readJournal", t.player.inv_op, "makinghistory_journal", 1)
        t.exec("readJournal.open", t.ui.await_open, "book", 10)
        t.exec("readJournal.row2", t.ui.expect_text, "book:page_left_text_2", "---1st Bennath", 5)
        -- (No page turn here: scriptrun turns no book's page -- _books.lua
        -- fails its turn rows the same way. The walk to Rellekka shuts it.)
        -- The guide's "Use the enchanted key on the chest": the chest leaves
        -- the pack (inv_del in makinghistory_open_chest) and the key stays.
        local chest_after_result, chest_after = t.inv.count("makinghistory_chest")
        local key_kept_result, key_kept = t.inv.count("makinghistory_key")
        t.check("openChest.chestUsed", chest_after_result == "ok" and chest_after == 0
                and key_kept_result == "ok" and key_kept == 1,
            "after the key-on-chest: makinghistory_chest " .. tostring(chest_after_result) .. " " .. tostring(chest_after)
                .. " (want 0), makinghistory_key " .. tostring(key_kept_result) .. " " .. tostring(key_kept) .. " (want 1)")

        -- === Warrior branch: Blanin, then Dron's riddle, Rellekka ========
        -- (makinghistory_frem.rs2, %makinghistory_warr_prog). Blanin's own
        -- opnpc1 plays straight through with no choice the first time --
        -- warr_prog is still not_started, so it takes the top branch. Its
        -- two mes() calls between the third and fourth npc lines are
        -- game-message lines (no page, confirmed by measurement above), so
        -- they are never their own chat.play entry.
        t.exec("goto-talkToBlanin", t.player.goto_tile, 2675, 3671, 0)
        t.exec("talkToBlanin", t.player.talk_to, "makinghistory_blanin")
        -- RSProx index 4463 [1225..1242].
        t.exec("talkToBlanin-dialog", t.chat.play, {
            "player:Hello there. Are you the brother of Dron?",
            "npc:That I am. Why? Has he killed one of your family?",
            "player:Not that I know of...",
            "npc:Oh good, how can I help you?",
            "player:Well I'd like to talk to your brother Dron",
            "npc:I'm afraid he's not easy to talk to",
            "player:Like?",
            "npc:You must be firm with him",
            "npc:His favorite drink is red spider blood",
            "player:O..K....",
            "npc:I know this sounds strange",
        })

        -- Dron demands proof first (~p_choice2), then quizzes back every
        -- fact Blanin just gave -- twelve questions total
        -- (makinghistory_dron_riddle), each answer taken verbatim from the
        -- .rs2's own ~p_choice2/~p_choice3 button text. Split into three
        -- legs so a wrong answer's page names exactly which question missed.
        t.exec("goto-talkToDron", t.player.goto_tile, 2658, 3700, 0)
        t.exec("talkToDron", t.player.talk_to, "makinghistory_dron")
        -- RSProx index 4463 [1419..1500].
        t.exec("talkToDron-dialog-1-intro-q1-q4", t.chat.play, {
            "player:I need to talk to you.",
            "npc:Why should I?",
            "choose:I'm after important answers.",
            "player:I'm after important answers.",
            "npc:But how do you know me?",
            "choose:Why, you're the famous warrior Dron!",
            "player:Why, you're the famous warrior Dron!",
            "npc:If so, what weapon do I use?",
            "choose:An iron mace",
            "player:An iron mace.",
            "npc:When do I like to eat rats?",
            "choose:Breakfast",
            "player:Breakfast.",
            "npc:When are kittens best devoured?",
            "choose:Lunch",
            "player:Lunch.",
            "npc:What do I usually eat for tea?",
            "choose:Bunnies",
            "player:Bunnies.",
        })
        t.exec("talkToDron-dialog-2-q5-q8", t.chat.play, {
            "npc:What colour spider blood tastes the best?",
            "choose:Red",
            "player:Red.",
            "npc:How many years old am I?",
            "choose:36",
            "player:36",
            "npc:and months?",
            "choose:8",
            "player:8",
            "npc:What are the most interesting ages for battles?",
            "choose:Fifth and Fourth",
            "player:Fifth and Fourth.",
        })
        t.exec("talkToDron-dialog-3-q9-q12-conclusion", t.chat.play, {
            "npc:And my house is situated where?",
            "choose:North East side of town",
            "player:North East side of town.",
            "npc:What is my brother's name?",
            "choose:Blanin",
            "player:Blanin.",
            "npc:And my pet cat is called?",
            "choose:Fluffy",
            "player:Fluffy.",
            "npc:What's 5 plus 7?",
            "choose:12, but what does that have to do with anything?",
            "player:12, but what does that have to do with anything?",
            "npc:Everything! Besides, it's 13!",
            "player:Er, I think you'll find it's 12.",
            "npc:....",
            "npc:..........",
            "npc:Very well, you seem to know me quite well",
            "player:Phew!",
            "npc:Well what are they then?",
            "player:I'm trying to find out the history of the outpost",
            "npc:Let me see. Ah yes, I remember reading of a battle",
            "npc:sole survivors because of their superior strength.",
            "player:Ex-friends?",
            "npc:They were once friends",
            "npc:outpost caused them to see the errors of their ways",
            "player:Ah, I like happy endings.",
            "npc:You would.",
            "player:Thank you, I can report this back to Jorral now.",
            "npc:Anything else?",
            "player:Well, I was wondering how you get the blood stains",
            "npc:BE GONE!",
        })

        -- === Ghost branch: Droalak's errand, Melina, the scroll ==========
        -- (makinghistory_ghost.rs2, native multi-npc shells, %makinghistory_
        -- ghost_prog/melina_pres). ghostspeak is already worn
        -- (equip-ghostspeak) so makinghistory_has_ghostspeak's own dead
        -- "impossible to make out" branch never fires.
        --
        -- Into Morytania the way Priest in Peril opened (WALLS in the
        -- header): Varrock Teleport, the Varrock members' gate, the
        -- Paterdomus trapdoor north of the temple, west of the Salve.
        varrock_teleport("enterMorytania.varrockTeleport")
        t.exec("goto-enterMorytania.varrockGate", t.player.goto_tile, 3318, 3468, 0)
        t.exec("enterMorytania.varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
            open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
        t.exec("goto-enterMorytania.trapdoor", t.player.goto_tile, 3405, 3506, 0)
        t.exec("enterMorytania.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
        t.await({
            level = function()
                return t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } }) == "ok"
            end,
            note = "enterMorytania: the trapdoor opens",
        }, 6)
        local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } })
        local tdc_r = t.world.loc_near("trapdoor", 3, { at = { 3405, 3507, 0 } })
        t.check("enterMorytania.trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
            "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. " "
                .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z .. "," .. tdo.level) or tostring(tdo))
                .. "; closed trapdoor there -> " .. tostring(tdc_r) .. " (want the open leaf and no closed one)")
        t.exec("enterMorytania.descend", t.player.climb, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3405, 3507, 0 }, src = { 3405, 3506 }, dest = { 3405, 9906, 0 } })
        t.exec("enterMorytania.gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
            near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
            far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
        t.exec("enterMorytania.gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
            near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
            far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
        -- Priest in Peril's farewell advice (mausoleum_drezel.rs2:145-154,
        -- LostCity drezel.rs2:138-147): 60 -> 61, the holy barrier opens.
        t.exec("enterMorytania.talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("enterMorytania.talkToDrezel-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Into Morytania? Yes, you can.",
            "npc:I must warn you though, Morytania is an evil land.",
            "npc:Worst of all are the vampyres",
            "player:I'll be careful. Anything else I should know?",
            "npc:The first settlement you'll likely come across is Canifis",
            "npc:You should keep that dagger I gave you to hand.",
            "player:I see. Thanks.",
            "npc:One last thing.",
            "player:The Myreque?",
            "npc:Yes. They're a band of freedom fighters.",
            "npc:Veliaf, the leader of their Mort Myre group",
            "player:I'll keep an eye out for them.",
            "npc:Right, I think that's everything.",
            "mesbox:Drezel blesses you",
            "npc:Good luck out there. Stay safe.",
            "choose:And you. See you later.",
            "player:And you. See you later.",
        })
        t.exec("enterMorytania.drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("enterMorytania.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
            at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
            far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })
        t.exec("enterMorytania.holyBarrier-msg", t.msg.expect, "You pass through the holy barrier")

        -- Overland east to the west Energy Barrier (3652,3485, faces east):
        -- op1 "Pass" is the ghost guard's toll talk, heard through the worn
        -- ghostspeak amulet; 2 ectotokens and p_teleport straight across.
        t.exec("goto-enterPhasmatys", t.player.goto_tile, 3651, 3485, 0)
        local tok_before_r, tok_before = t.inv.count("ectotoken")
        t.exec("enterPhasmatys", t.player.cross_gate, { loc = "ahoy_town_barrier_multi", op = 1, at = { 3652, 3485, 0 },
            near = { 3651, 3485 }, far_ok = function(tile) return tile.x >= 3653 end,
            far_desc = "inside Port Phasmatys, east of the west barrier, x >= 3653" })
        local tok_after_r, tok_after = t.inv.count("ectotoken")
        t.check("enterPhasmatys.toll", tok_before_r == "ok" and tok_after_r == "ok" and tok_before == 2 and tok_after == 0,
            "ectotoken " .. tostring(tok_before) .. " -> " .. tostring(tok_after) .. " (want 2 -> 0, ^ahoy_barrier_toll)")

        t.player.walk_to(3658, 3467, 40)
        local dr_r, dr = t.world.tile()
        t.check("walkToDroalak", dr_r == "ok" and dr.level == 0 and math.abs(dr.x - 3658) <= 1 and math.abs(dr.z - 3467) <= 1,
            "walked to 3658,3467 (outside the general store) -> " .. tile_text(dr_r, dr))
        t.exec("talkToDroalak", t.player.talk_to, "makinghistory_droalak_multi")
        -- RSProx index 4462 [3793..3829].
        t.exec("talkToDroalak-intro-dialog", t.chat.play, {
            "player:Hello. Are you Droalak?",
            "npc:Wow. I haven't spoken to the living",
            "player:So your name IS Droalak?",
            "npc:Sorry, yes I am he.",
            "player:Great. Do you know anything about the outpost",
            "npc:I don't really like to talk about it",
            "player:Oh dear.",
            "npc:I do have a scroll",
            "player:Like what?",
            "npc:Well, I left to go to the outpost",
            "player:You want me to patch things up?",
            "npc:Yes, how'd you guess?",
            "player:Call it 'traveller's intuition'.",
            "npc:OK, well perhaps you could give her a strung sapphire amulet",
            "player:No problem.",
        })

        local function in_melina(tt)
            return tt.level == 0 and tt.x >= 3671 and tt.x <= 3678 and tt.z >= 3477 and tt.z <= 3484
        end
        t.exec("talkToMelina.doorIn", t.player.pass_door, { closed = "ahoy_harbour_door", open = "ahoy_harbour_door_open",
            at = { 3676, 3476, 0 }, near = { 3676, 3475 }, far = { 3675, 3479 }, far_ok = in_melina,
            far_desc = "inside Melina's house, x 3671-3678 z 3477-3484" })
        t.exec("talkToMelina", t.player.talk_to, "makinghistory_melina_multi")
        -- RSProx index 4463 [275..295].
        t.exec("talkToMelina-dialog", t.chat.play, {
            "player:If you don't mind me asking, are you Melina?",
            "npc:That I am. What's it to you?",
            "player:I've been talking to Droalak.",
            "npc:He did. I suppose he has asked you",
            "player:Well actually he told me to give you this amulet.",
            "npc:A sapphire amulet! He remembers!",
            "player:I honestly believe he's sorry.",
            "npc:I'm so glad. Please, tell him I forgive him!",
            "player:I will.",
            "npc:At last I feel complete. Fare well.",
            "player:Goodbye.",
        })
        -- inv.expect_absent is an instant read, not a wait -- Melina's own
        -- inv_del runs on the script statement right after the last page
        -- dismissed above and can still be mid-flight the instant this next
        -- line executes (QUEST_AUTHORING.md section 8), so poll for it
        -- consumed the way cooks_assistant.lua's own ingredients_consumed
        -- row does, rather than reading once.
        local amulet_gone_result = t.await({
            level = function()
                local r, c = t.inv.count("strung_sapphire_amulet")
                return r == "ok" and c == 0
            end,
            note = "amuletDelivered",
        }, 10)
        local amulet_after_read, amulet_after = t.inv.count("strung_sapphire_amulet")
        t.check("amuletDelivered", amulet_gone_result == "ok" and amulet_after == 0,
            "strung_sapphire_amulet consumed within 10 ticks (" .. tostring(amulet_gone_result) .. ") count="
                .. tostring(amulet_after) .. " (" .. tostring(amulet_after_read) .. ")")

        t.exec("returnToDroalak.doorOut", t.player.pass_door, { closed = "ahoy_harbour_door", open = "ahoy_harbour_door_open",
            at = { 3676, 3476, 0 }, near = { 3676, 3477 }, far = { 3676, 3474 },
            far_ok = function(tt) return tt.level == 0 and tt.z <= 3475 end, far_desc = "outside Melina's house, z <= 3475" })
        t.player.walk_to(3658, 3467, 40)
        local dr2_r, dr2 = t.world.tile()
        t.check("returnToDroalak.walk", dr2_r == "ok" and dr2.level == 0 and math.abs(dr2.x - 3658) <= 1 and math.abs(dr2.z - 3467) <= 1,
            "walked to 3658,3467 (outside the general store) -> " .. tile_text(dr2_r, dr2))
        t.exec("talkToDroalak2", t.player.talk_to, "makinghistory_droalak_multi")
        -- RSProx index 4463 [312..324].
        t.exec("talkToDroalak-scroll-dialog", t.chat.play, {
            "player:I've given her the amulet.",
            "npc:Excellent! I am so glad she believes me.",
            "player:Could I have that scroll you mentioned first?",
            "npc:Of course. Let me know if it was of any use",
            "player:Thank you.",
        })
        t.expect("haveScroll", t.inv.await("makinghistory_scroll1", 1, 10))

        -- === Back to Jorral: all three leads done, hand in ================
        -- Out of Morytania by Camelot Teleport, pressed in the spellbook
        -- (teleport.rs2 [if_button,magic_spellbook:camelot_teleport]):
        -- 5 air + 1 law (magic_spells.dbrow), lands tele_coord 0_43_54_5_22 =
        -- 2757,3478,0 (map_findsquare within 2); Camelot -> the outpost is
        -- open ground.
        camelot_teleport("leaveMorytania")
        outpost_in("talkToJorral-handin")
        -- One lead per talk, each as recorded: the scroll (RSProx index 4463
        -- [474..490]), the journal ([970..1030], the last lead told straight
        -- away), the warrior and the letter ([1538..1569]). The recorder
        -- brought each back as it was found; this route brings all three.
        t.exec("talkToJorral-handin", t.player.talk_to, "makinghistory_jorral")
        t.exec("talkToJorral-handin-dialog", t.chat.play, {
            "player:Hi there.",
            "npc:How's it going?",
            "player:I have been in contact with the ghost you suggested",
            "mesbox:Jorral skims over the contents of the scroll",
            "npc:Very interesting. So there was a great battle",
            "choose:What's the story so far?",
            "player:What's the story so far?",
            "npc:From the scroll you received from the ghost",
            "npc:Ardougne and another established the market place.",
            "choose:Got to go, bye!",
            "player:Got to go, bye!",
        })
        t.exec("talkToJorral-handin2", t.player.talk_to, "makinghistory_jorral")
        t.exec("talkToJorral-handin2-dialog", t.chat.play, {
            "player:Hi there.",
            "npc:How's it going?",
            "player:I found a journal!",
            "npc:Good work. Let's see what it says.",
            "mesbox:Jorral reads through the journal.",
            "npc:The person who wrote the journal",
            "player:I see.",
            "npc:He talks of all the nasty things",
            "player:Interesting.",
            "choose:What do I need to do now?",
            "player:What do I need to do now?",
            "npc:There is 1 person left that may be able to help:",
            "npc:Up near the mountains, in Rellekka",
            "player:And how's he related to this outpost?",
            "npc:He isn't directly",
            "player:That sounds simple enough.",
            "npc:He isn't the easiest person to talk to",
            "choose:Got to go, bye!",
            "player:Got to go, bye!",
        })
        t.exec("talkToJorral-handin3", t.player.talk_to, "makinghistory_jorral")
        t.exec("talkToJorral-handin3-dialog", t.chat.play, {
            "player:Hi there.",
            "npc:How's it going?",
            "player:I've talked to the warrior.",
            "npc:Excellent! What did he say?",
            "player:Well...",
        })
        -- [1544..1550]: "Well..." closes with the chat into a fade, the next
        -- page six ticks on.
        t.ticks(8)
        t.exec("talkToJorral-handin3-dialog2", t.chat.play, {
            "player:And then I asked him about getting the blood stains",
            "npc:It seems you're lucky he didn't beat you up!",
            "npc:It all makes sense now",
            "player:I'm glad to hear. What's the whole story?",
            "npc:Many years ago, there were two friends",
            "npc:this outpost with a group of others",
            "npc:two ex-friends at the top of the outpost",
            "npc:market where all could trade",
            "player:So what should I do now?",
            "npc:Well, what I'll do is write down the details",
            "player:Then I suppose you want ME to deliver it to the king?",
            "npc:You've got that right!",
        })
        t.expect("haveLetter1", t.inv.await("makinghistory_letter1", 1, 10))
        outpost_out("leaveJorral-handin")
        -- (no journal cross-check here -- ui.journal_open measured an
        -- intermittent timeout at exactly this point, twice reproducibly,
        -- right after this five-page chat.play; haveLetter1 above and the
        -- castle-gated King Lathas dialogue right below already prove
        -- %makinghistory_prog reached castle server-side, so this is
        -- optional evidence, not load-bearing, and is dropped rather than
        -- spent chasing a UI-mount race with the run budget.)

        -- === King Lathas, East Ardougne castle ============================
        -- (additive into king_lathas.rs2's own [opnpc1,kinglathas], gated on
        -- %makinghistory_prog=castle -- fresh fixture has no %sote/%ds2
        -- state active to intercept the click ahead of it).
        local function in_kings_room(tt)
            return tt.level == 1 and tt.x >= 2575 and tt.x <= 2579 and tt.z >= 3292 and tt.z <= 3294
        end
        t.exec("goto-goUpToLathas", t.player.goto_tile, 2577, 3298, 0)
        t.exec("goUpToLathas.castleDoorIn", t.player.pass_door, { closed = "w_ardougnedoubledoorl",
            open = "w_ardougnedoubledoorlopen", at = { 2576, 3298, 0 }, near = { 2577, 3298 }, far = { 2573, 3298 },
            far_ok = function(tt) return tt.level == 0 and tt.x <= 2575 end, far_desc = "in the castle hall, x <= 2575" })
        -- maplink [maplink_0_40_51_11_34_up]: 2571,3298,0 -> 2571,3294,1
        -- (shortest-path transports.tsv, stairs placed 3 tiles from the tsv's
        -- origin; tools/maplink_import.py resolve_on_tile, 2026-10-06). The
        -- landing is in the same upstairs component as the old +1-plane one.
        t.exec("goUpToLathas", t.player.climb, { loc = "stairs", op = 1, op_name = "Climb-up",
            at = { 2571, 3295, 0 }, src = { 2571, 3298 }, dest = { 2571, 3294, 1 } })
        t.exec("talkToLathas.kingsDoorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 2575, 3293, 1 }, near = { 2574, 3293 }, far = { 2577, 3293 }, far_ok = in_kings_room,
            far_desc = "in King Lathas's room, x 2575-2579 z 3292-3294 level 1" })
        t.exec("talkToKingLathas", t.player.talk_to, "kinglathas")
        -- RSProx index 4463 [1665..1676].
        t.exec("talkToKingLathas-dialog", t.chat.play, {
            "choose:Talk about the outpost.",
            "player:Excuse me. I have been asked to hand you this",
            "npc:I see.",
            "mesbox:The King reads the letter.",
            "npc:I had no idea that place had any value at all!",
            "player:I am sure he only wants what is best.",
            "npc:Very well, I will comply with his request.",
            "player:Thank you.",
        })
        t.expect("haveLetter2", t.inv.await("makinghistory_letter2", 1, 10))
        -- Out the way in: the king's door, down the stairs, the castle door.
        t.exec("leaveLathas.kingsDoorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 2575, 3293, 1 }, near = { 2576, 3293 }, far = { 2573, 3293 },
            far_ok = function(tt) return tt.level == 1 and tt.x <= 2574 end, far_desc = "out of the king's room, x <= 2574 level 1" })
        -- maplink_1_40_51_11_30_down is keyed on the stand tile 2571,3294,1.
        t.exec("leaveLathas.downstairs", t.player.climb, { loc = "stairstop", op = 1, op_name = "Climb-down",
            at = { 2571, 3295, 1 }, src = { 2571, 3294 }, dest = { 2571, 3298, 0 } })
        t.exec("leaveLathas.castleDoorOut", t.player.pass_door, { closed = "w_ardougnedoubledoorl",
            open = "w_ardougnedoubledoorlopen", at = { 2576, 3298, 0 }, near = { 2574, 3298 }, far = { 2577, 3298 },
            far_ok = function(tt) return tt.level == 0 and tt.x >= 2576 end, far_desc = "on the street, x >= 2576" })
        -- (no journal cross-check here either -- the SAME ui.journal_open
        -- timeout measured at quest.stage.castle above also reproduced at
        -- this equivalent spot in an earlier run; haveLetter2 and the
        -- lathas_done-gated Jorral finish dialogue right below already
        -- prove %makinghistory_prog reached lathas_done server-side.)

        -- === Back to Jorral once more: finish and collect rewards =========
        -- skill.snapshot() BEFORE the hand-in click that queues
        -- makinghistory_quest_complete (0-tick queue -> t.ticks(3) below is
        -- load-bearing, QUEST_AUTHORING.md section 8).
        -- (the snapshot's read status is not a row of its own: a failed
        -- snapshot fails both reward.* rows below.)
        local _, xp_snapshot = t.skill.snapshot()
        local coins_before_result, coins_before = t.inv.count("coins")
        t.check("coins.baseline", coins_before_result == "ok" and coins_before == 0,
            "inv.count(coins) before hand-in -> " .. tostring(coins_before_result) .. " " .. tostring(coins_before)
                .. " (want 0: ::clearinv, and nothing in this quest pays coins before the reward)")
        local key_before_result, key_before = t.inv.count("makinghistory_key")
        t.check("key.baseline", key_before_result == "ok" and key_before == 1,
            "inv.count(makinghistory_key) before hand-in (the original, never consumed opening the chest) -> "
                .. tostring(key_before_result) .. " " .. tostring(key_before))

        outpost_in("talkToJorral-finish")
        t.exec("talkToJorral-finish", t.player.talk_to, "makinghistory_jorral")
        -- RSProx index 4463 [1829..1848]: a fade between the last two pages,
        -- the completion on the last one's click.
        t.exec("talkToJorral-finish-dialog", t.chat.play, {
            "player:I've been to see the king and he gave me this letter.",
            "npc:Quick, let me see.",
            "mesbox:Jorral reads the letter.",
            "npc:Hurrah! Good work, you've done it.",
            "player:My pleasure.",
            "npc:I can now continue with my plans for this place.",
        })
        -- [1837..1843]: the fade out with the chat, the museum opened under
        -- the fade in, the last page six ticks on.
        t.ticks(8)
        t.exec("talkToJorral-finish-dialog2", t.chat.play, {
            "npc:Take a look at the artefacts in the new museum room.",
        })
        t.ticks(3) -- makinghistory_quest_complete is a 0-tick queue, not client-side yet

        -- Literal rewards makinghistory_jorral.rs2's own
        -- ~quest_complete_rewards call documents (quest_makinghistory.constant:
        -- 1000 Crafting XP, 1000 Prayer XP, 750 coins, an enchanted key),
        -- asserted against those documented numbers, never a number read
        -- back from the scroll -- a row that compares the game against
        -- itself cannot fail.
        t.check("reward.crafting", t.skill.expect_gain("crafting", 1000, xp_snapshot))
        t.check("reward.prayer", t.skill.expect_gain("prayer", 1000, xp_snapshot))

        local coins_after_result, coins_after = t.inv.count("coins")
        t.check("reward.coins",
            coins_before_result == "ok" and coins_after_result == "ok"
                and coins_after == coins_before + 750,
            string.format("coins before=%s after=%s (%s/%s), expected +750",
                tostring(coins_before), tostring(coins_after),
                tostring(coins_before_result), tostring(coins_after_result)))

        -- The key is kept from Erin's loan; the completion adds none.
        local key_after_result, key_after = t.inv.count("makinghistory_key")
        t.check("reward.key", key_after_result == "ok" and key_after == 1,
            "inv.count(makinghistory_key) after completion -> " .. tostring(key_after_result) .. " "
                .. tostring(key_after) .. " (want 1: the key kept, none added -- RSProx index 4463 [1848])")

        -- ------------------------------------------------- the committed state
        -- The varp seam this file used to block on (see queue.py's
        -- last_failure after 68c5e8d9d, and the RESUMED banner) is fixed:
        -- OSRS-Content/osrs-content/server/scripts/quests/quest_makinghistory/
        -- configs/quest_makinghistory.varp now declares `[makinghistory]
        -- transmit=yes`, so %makinghistory_prog (and every other varbit on
        -- that basevar) reaches the client for real: t.quest.expect_stage
        -- below reads client=4 server=4 instead of a stale client=0.
        --
        -- t.quest.expect_complete() itself is NOT called bare here. It was
        -- tried first (build/quest_gate/makinghistory, the run right before
        -- this one): quest.varp_complete/quest.scroll_title/quest.points all
        -- PASSED for real (client=4 server=4, "You have completed Making
        -- History!", qp 2->5), but its own quest.journal row -- a fresh
        -- journal_open() right after its own scroll.close(), no settle
        -- between the two -- FAILed: "row 97 clicked, but no painted
        -- journal within 20 ticks". The SAME journal_open("Making History")
        -- call, retried three times right after on that identical completed
        -- state, answered ok complete=true lines=4 -- a UI-mount race in
        -- quest.lua's own single unretried attempt (QUEST_AUTHORING.md
        -- section 8's "gaps reported by authors" bullet on this exact
        -- shape), not a content bug and not something this file can reach
        -- (trap 7: script/plugins/ is off limits). Per section 7's own
        -- minimum shape ("if you call quest.bind: at least one quest.* row,
        -- and either a passing quest.varp_complete or the ledger's last row
        -- is BLOCKED"), quest.journal is not itself required, so completion
        -- is driven through the rows that DO land, each written by hand --
        -- the same shape rovingelves.lua's own completion uses for the
        -- identical driver seam -- with a retried journal check standing in
        -- as the row that actually grades the journal.
        t.exec("quest.varp_complete", t.quest.expect_stage, "complete")

        -- Same shape as quest.expect_complete()'s own quest.scroll_title row
        -- (quest.lua): the shot is taken EXPLICITLY (t.shot, not t.check's
        -- own auto-shoot) so a byte-identical dedupe against the completion
        -- frame quest.varp_complete's row already captured above folds into
        -- THIS row's detail as "[scroll already photographed: ...]" instead
        -- of silently losing the picture -- gate.py's own completion-scroll
        -- rule (owner's rule, 2026-09-20) checks for exactly that phrase.
        local scroll_title_result, scroll_title = t.scroll.title()
        local scroll_shot_result, scroll_shot_detail = t.shot("quest.scroll")
        local scroll_shot_note = ""
        if scroll_shot_result == "ok" and type(scroll_shot_detail) == "string"
            and string.find(scroll_shot_detail, "unchanged", 1, true) then
            scroll_shot_note = " [scroll already photographed: " .. scroll_shot_detail .. "]"
        end
        local title_name = scroll_title ~= nil and scroll_title.name or nil
        local title_pass = scroll_title_result == "ok" and type(title_name) == "string"
            and title_name:find("Making History", 1, true) ~= nil
        t.step("quest.scroll_title", title_pass and "PASS" or "FAIL",
            "scroll.title() after the hand-in -> " .. tostring(scroll_title_result) .. " name="
                .. tostring(title_name) .. " points="
                .. tostring(scroll_title and scroll_title.points) .. scroll_shot_note)
        t.scroll.close()

        local qp_after_result, qp_after = t.var.varp("varp101_qp")
        t.check("quest.points", qp_after_result == "ok" and qp_before_result == "ok"
            and qp_after == qp_before + 3,
            "qp (varp) " .. tostring(qp_before) .. " -> " .. tostring(qp_after)
                .. " delta=" .. tostring(qp_after_result == "ok" and qp_before_result == "ok"
                    and (qp_after - qp_before) or "?")
                .. " expected=3")

        -- Same retry ladder as quest.stage.started above -- ui.journal_open's
        -- mount timeout measured throughout this file (and above, on
        -- expect_complete()'s own one-shot attempt) is a UI-mount race, not
        -- a content problem, and a short retry absorbs it without weakening
        -- what the row grades.
        local final_journal_result, final_journal
        for _ = 1, 3 do
            final_journal_result, final_journal = t.ui.journal_open("Making History")
            if final_journal_result == "ok" then
                break
            end
            t.ui.journal_close()
            t.ticks(5)
        end
        t.check("quest.journal", final_journal_result == "ok" and final_journal ~= nil
            and final_journal.complete == true,
            "journal_open(Making History) -> " .. tostring(final_journal_result) .. " complete="
                .. tostring(final_journal and final_journal.complete) .. " lines="
                .. tostring(final_journal and final_journal.line_count)
                .. " -- channel: ui.journal_open (server-side proc; makinghistory_journal.rs2's own final "
                .. "else-branch only prints 'QUEST COMPLETE!' once %varb1383_makinghistory_prog is genuinely past "
                .. "lathas_done, so this is not readable from an incomplete quest)")
        t.ui.journal_close()

        -- The museum (RSProx index 4463 [1856..1880]): Jorral's line after
        -- the quest, the room next door, a case and the bookcase.
        t.exec("museum.jorral", t.player.talk_to, "makinghistory_jorral")
        t.exec("museum.jorral-dialog", t.chat.play, { "npc:Thanks for the help again. Feel free to browse the room" })
        t.exec("museum.doorIn", t.player.pass_door, { closed = "makinghistory_door", open = "makinghistory_door_open",
            at = { 2436, 3345, 0 }, near = { 2436, 3346 }, far = { 2436, 3344 },
            far_ok = function(tt) return tt.level == 0 and tt.z <= 3344 end, far_desc = "in the museum room, z <= 3344" })
        t.exec("museum.shield", t.player.click_loc, "makinghistory_shield_display_multiloc", 1, { at = { 2437, 3344, 0 } })
        t.exec("museum.shield-box", t.chat.play, { "mesbox:Inside the case is a giant shield" })
        t.exec("museum.bookcase", t.player.click_loc, "makinghistory_bookcase_multiloc", 1, { at = { 2434, 3344, 0 } })
        t.exec("museum.bookcase-dialog", t.chat.play, {
            "mesbox:There's a great variety of books. Which shall you choose?",
            "choose:The Times of Ardignas.",
            "mesbox:You pick on a new looking book: 'The Times of Ardignas'.",
        })

        t.finish(0)
        return
    end,
}
