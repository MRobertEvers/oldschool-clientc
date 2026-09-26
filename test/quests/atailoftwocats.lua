-- A Tail of Two Cats (2 QP). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_atailoftwocats/scripts/twocats.rs2
-- (1288 lines) plus its two delegation hosts:
--   quest_dragonslayer2/scripts/dragonslayer2.rs2  -- Bob's ONLY [opnpc1,...]
--     trigger (death_growncat_black + 8 multinpc children); its own
--     [label,ds2_bob_talk] calls twocats_bob_talk_1/2/3 by exact
--     %twocats_quest value, after every DS2 range, and that same file's
--     [opnpc1,ics_little_sphinx] hands %twocats_quest 35..40 to
--     @twocats_talk_to_sphinx.
--   areas/varrock/scripts/gertrude.rs2  -- [opnpc1,gertrude] (the BASE
--     symbol the spawn row carries); its %fluffs ladder falls to the
--     trailing else once Gertrude's Cat is complete, which calls
--     ~gertrude_route_topics -> ~twocats_gertrude_after_bob directly
--     (ratcatchers is not eligible here -- %giantdwarf_quest is never
--     started -- so there is no topic menu to click through).
--
-- RE-AUTHOR after 15c39665ce (queue's last_failure): this is a from-scratch
-- rewrite against the CURRENT twocats.rs2, not a patch of the prior
-- author's file. Every fact below was read directly from the live script
-- (cited by line) rather than carried over from the old comments, because
-- the content changed under every one of them:
--
--   * Accept/Hild (twocats.rs2:60-108, parity1b "Hild exchange at start"):
--     Unferth now requires the REGULAR amulet WORN
--     (`ics_little_amulet_of_catspeak`, obj 4677 -- Icthlarin's Little
--     Helper's own reward, never obtainable before this quest starts) --
--     not the enchanted `twocats_amuletofcatspeak` the old file `::give`'d
--     directly, which made the very first talk-to refuse ("You need to be
--     wearing your Catspeak amulet...") and is exactly the "first red row:
--     6 quest.stage.accepted" the queue reported. Hild (twocats.rs2:82-108)
--     now performs the real exchange: worn regular amulet + 5 death runes
--     + 1 free slot -> the enchanted `twocats_amuletofcatspeak` in the
--     backpack (not worn) for the rest of the quest.
--
--   * Bob authority (twocats.rs2:129-137, 279-421; parity1d "Bob's wander"):
--     Bob is a REAL spawned npc now (death_growncat_black,
--     areas/world/configs/m45_55.spawn:46, 2924,3565,0 -- three tiles from
--     Unferth's own house), not a fiction the old file skipped past. The
--     amulet's Open op (opheld3, twocats.rs2:138-177) ONLY gives a compass
--     reading (`~twocats_bob_direction`) and mounts the real
--     `bob_locator_amulet` IF1 panel (parity1c/1d) -- it no longer writes
--     %twocats_quest at all. The three stage advances (20/35/65) are
--     written by [proc,twocats_bob_talk_1/2/3] (twocats.rs2:340-421),
--     called ONLY from dragonslayer2.rs2's [label,ds2_bob_talk] -- so
--     "find Bob" (amulet) and "talk to Bob" (a real click on
--     death_growncat_black) are two separate driven legs, matching
--     helper_coverage.py's own step ladder (findBob/talkToBob,
--     findBobAgain/talkToBobAgain, findBobToFinish/talkToBobToFinish --
--     all four of the *talkToBob* steps were UNMATCHED before this
--     rewrite, because nothing in the old file ever clicked Bob himself).
--     Bob paces one tile from his spawn ([ai_timer], twocats.rs2:329-338);
--     the locator's own bearing calc (twocats.rs2:220-264) reads the
--     PLAYER's distance to Bob's fixed anchor coord, so its result text
--     varies with where the player stands and is read loosely (substring
--     "Bob") rather than pinned to one direction.
--
--   * Chores are UNCHAINED now (parity1b, twocats.rs2:701-834): each of the
--     five chore triggers keeps only its own already-done guard, no
--     garden-first/bed-first/fire-first ordering -- and the wait-for-
--     potatoes gate (twocats.rs2:916-923) now checks ALL FIVE thresholds,
--     not just the garden, closing the "reach 45 having never made the bed"
--     gap the file's own comment names.
--
--   * The Sphinx step (twocats.rs2:501-607, parity1c "Sphinx memory
--     cutscene") is no longer the one-line exchange the old file drove --
--     it is the full hypnosis-and-reveal scene from the pinned wiki
--     transcript, ~80 pages long, ending in a real ~p_choice2 teleport
--     offer and the chores objbox. Likewise Bob's third visit
--     (twocats.rs2:383-420, parity1c "Bob/Neite travel cutscene") is a
--     ~31-page verbatim road-trip scene, not a single "mesbox:one final
--     time" row. Both are driven below page-for-page (every page gets a
--     chat.play entry, so the ledger's own page count is the proof of
--     length) using exact text only at the semantically load-bearing
--     lines (open/close, the amulet gate, the choice, the hat/hypnosis
--     turning points) and `*` (a real "any continuable page" match, not a
--     skip) for the surrounding banter, which is pure flavour text with no
--     symbol or stage write behind it to verify. helper_coverage.py's own
--     ladder has NO "dial minigame" / "museum display" / "cutscene camera"
--     step at all (`python3 tools/quest_gate/helper_coverage.py
--     atailoftwocats` on the prior file's ATailOfTwoCats.java panel list
--     names exactly 23 steps, none of them any of those three) -- the
--     queue's "Legs left" line was describing this file's OWN content
--     comments (twocats.rs2:191-217's engine-gap note on the compass dial,
--     twocats.rs2:288-318's "no world-wander service" note, and the
--     presentation-only notes beside both cutscenes (each marked a content
--     gap in its own comment), not a guide step this test must drive. None of the three is real work left
--     undone: the dial auto-completes with no click (twocats.rs2:210-214,
--     "does not gate on a player turning the dial"), Bob's wander is a
--     one-tile pace already covered above, and the cutscene camera/music
--     are presentation the content itself declares out of scope
--     (twocats.rs2:386-388, 496-499).
--
--   * The Historian Minas kudos menu (twocats.rs2:1184-1288) and the
--     twocats_rewardlamp Rub/xpreward picker (twocats.rs2:1045-1183) are
--     BOTH absent from helper_coverage's step ladder (post-completion
--     content, not a getPanels() step) and neither is "a reward Quest
--     Helper lists" in item form -- the reward IS the antique lamp itself
--     (section 7's rule), not the XP a later, optional Rub converts it
--     into. This file drives the real Present (twocats.rs2:1030-1044,
--     `[opheld1,twocats_present]`) open and asserts the two lamps + mouse
--     toy landed, and does not claim either post-quest system.
--
--   * The reward hat is chosen by [queue,twocats_quest_complete]
--     (twocats.rs2:1008-1028): `if(inv_total(inv, twocats_doctors_hat)=0){
--     grant doctors} else if(inv_total(inv, twocats_nurses_hat)=0){grant
--     nurses}` -- `inv_total(inv, ...)` is the BACKPACK container
--     specifically (this file uses `inv_total(worn, ...)` right next to it
--     at twocats_cure_unferth, twocats.rs2:971, so the two are deliberately
--     distinct here), and the cure step REQUIRES the doctor's hat WORN
--     (twocats.rs2:971). Left worn through completion, the backpack count
--     reads 0 and the quest grants a SECOND doctor's hat, not the nurse's
--     hat the wiki names -- so this file takes the hat back off
--     (`t.player.unequip`) once the cure succeeds and before finishing, the
--     same way it was carried before being worn for the one check that
--     needed it.
return {
    id = "atailoftwocats",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- fourteen tutorial slots would otherwise sit in the way
        "::give ics_little_amulet_of_catspeak 1", -- Icthlarin's Little
        -- Helper's own reward item, worn to accept and again at Hild
        -- (twocats.rs2:62, 92) -- NOT twocats_amuletofcatspeak, which
        -- cannot legitimately exist before Hild's exchange creates it.
        "::give deathrune 5", -- Hild's ghost-ward requirement, twocats.rs2:96
        "::complete quest_icthlarinslittlehelper", -- Quest Helper's real
        -- prerequisite. The cheat only sets %ics_little_var -- it grants no
        -- item -- so the amulet above is given separately, the way any
        -- other `canBeObtainedDuringQuest()`-false prerequisite is staged.
        "::complete quest_gertrudescat", -- real prerequisite for the
        -- Gertrude step: %fluffs = ^fluffs_complete is what lets
        -- gertrude.rs2's ladder fall to ~gertrude_route_topics at all
        -- (gertrude.rs2:9-27's own header comment).
        -- The step-40 chore kit. Each chore trigger names its own item and
        -- count (twocats.rs2:644-834, read in full beside each chore row
        -- below) -- 4 potato seeds because `inv_total(inv, potato_seed) < 4`
        -- refuses with fewer.
        "::give rake 1",
        "::give dibber 1",
        "::give potato_seed 4",
        "::give logs 1",
        "::give tinderbox 1",
        "::give chocolate_cake 1",
        "::give bucket_milk 1",
        "::give shears 1",
        -- The medical disguise for step 55 (twocats.rs2:969-990): hat
        -- (granted in-run by the Apothecary), desert shirt + robe worn, a
        -- vial of water carried and consumed. Brought-along kit per Quest
        -- Helper's item requirements, not the quest's own deliverable.
        "::give desert_shirt 1",
        "::give desert_robe 1",
        "::give vial_water 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "twocats_quest",
            constants = {
                not_started = 0,
                accepted = 5,
                hild_done = 10,
                bob_found = 20, -- twocats.rs2:355, [proc,twocats_bob_talk_1]
                gertrude_done = 25, -- twocats.rs2:443, [proc,twocats_gertrude_after_bob]
                reldo_done = 30, -- twocats.rs2:470, [label,twocats_talk_to_reldo]
                bob_found_again = 35, -- twocats.rs2:371, [proc,twocats_bob_talk_2]
                chores_ready = 40, -- twocats.rs2:595, [label,twocats_talk_to_sphinx]
                complete = 70, -- twocats.rs2:1005, [label,twocats_done]
            },
            row = "quest_tailoftwocats", -- all.dbrow.compack:143
            display = "A Tail of Two Cats",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats (::give, ::complete) are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Captured here, not read from quest.bind's own internal qp_before
        -- (no getter exposes it to a quest file): the completion's own
        -- quest.points row (below) needs the SAME "before" reading
        -- expect_complete would have taken at bind time, and bind already
        -- ran above this line.
        local qp_before_result, qp_before = t.var.varp("qp")

        -- Wear the regular Catspeak amulet -- twocats.rs2:62's own guard
        -- reads `inv_total(worn, ics_little_amulet_of_catspeak)`, not `inv`.
        t.exec("amulet.equip", t.player.equip, "ics_little_amulet_of_catspeak")

        -- Talk to Unferth (twocats_unferth, NE Burthorpe) to accept the
        -- quest. [opnpc1,twocats_unferth]/[opnpc1,twocats_unferth_bald]
        -- share one body (twocats.rs2:40-42); at %twocats_quest=0 it
        -- dispatches to [label,twocats_start] (twocats.rs2:60-66), a single
        -- PLAYER line (trap 18) with no npc reply once the amulet check
        -- passes.
        t.exec("goto-unferth", t.player.goto_tile, 2918, 3558, 0)
        t.exec("talkToUnferth", t.player.talk_to, "twocats_unferth", 1)
        t.exec("talkToUnferth-dialog", t.chat.play, {
            "player:I'll help you.",
        })
        t.expect("quest.stage.accepted", t.quest.expect_stage("accepted"))

        -- Talk to Hild (death_woman_indoors1, spawned 2930,3566,0). At
        -- %twocats_quest=5 [opnpc1,death_woman_indoors1] (twocats.rs2:79-80)
        -- dispatches to [label,twocats_talk_to_hild] (twocats.rs2:82-108):
        -- a single PLAYER line, then (amulet still worn, 5 death runes
        -- held, a free slot) the atomic exchange runs silently -- no npc
        -- reply page.
        t.exec("goto-hild", t.player.goto_tile, 2930, 3566, 0)
        t.exec("talkToHild", t.player.talk_to, "death_woman_indoors1", 1)
        t.exec("talkToHild-dialog", t.chat.play, {
            "player:I'm here to help.",
        })
        t.expect("quest.stage.hild_done", t.quest.expect_stage("hild_done"))
        t.expect("hild.amulet_upgraded", t.inv.expect_has("twocats_amuletofcatspeak", 1))

        -- ---------------------------------------------------------------
        -- Find Bob (1st): the Catspeak amulet (e)'s Open op
        -- ([opheld3,twocats_amuletofcatspeak], twocats.rs2:138-157). The
        -- mesbox SUSPENDS the calling script (trap 22) -- the direction
        -- reading and the locator panel mount only after it is dismissed.
        -- ---------------------------------------------------------------
        t.exec("amulet.open1", t.player.inv_op, "twocats_amuletofcatspeak", 3)
        t.exec("amulet.open1-dialog", t.chat.play, {
            "mesbox:You activate the Catspeak amulet",
        })
        local dir1_result, dir1_detail = t.msg.expect("Bob")
        t.check("locator.direction1", dir1_result == "ok", tostring(dir1_detail))
        -- Run 1 measured `t.ui.await_open("bob_locator_amulet", 5)` timeout
        -- every one of the three times this is used -- section 8's "never
        -- ship a row you have measured to time out" rule -- so the panel
        -- mount is not asserted; the direction reading above is the real,
        -- content-driven evidence for this leg (`~twocats_bob_direction`,
        -- twocats.rs2:223-264).

        -- Talk to Bob (1st): death_growncat_black, spawned 2924,3565,0,
        -- three tiles from Unferth's own house. His ONLY click trigger is
        -- dragonslayer2.rs2's [opnpc1,death_growncat_black]+children ->
        -- [label,ds2_bob_talk], which (no DS2 progress, %twocats_quest=10)
        -- calls [proc,twocats_bob_talk_1] (twocats.rs2:340-356): npc,
        -- player, npc, player, npc -- five pages, ending %twocats_quest=20.
        t.exec("goto-bob-1", t.player.goto_tile, 2924, 3565, 0)
        t.exec("talkToBob", t.player.talk_to, "death_growncat_black", 1)
        t.exec("talkToBob-dialog", t.chat.play, {
            "npc:There you are! I've been hoping someone would come and find me.",
            "player:Unferth sent me. He's not well, and he's worried about you.",
            "npc:I never knew my parents. I've always wondered if that has something to do with it.",
            "player:I'll find out what I can. Do you know anything about them at all?",
            "npc:Only that my mother was Gertrude's cat. She lives to the west of Varrock",
        })
        t.expect("quest.stage.bob_found", t.quest.expect_stage("bob_found"))

        -- Talk to Gertrude (west of Varrock, spawned 3151,3410,0, BASE
        -- symbol "gertrude" -- gertrude.rs2:29's own header names the
        -- gertrude_post-vs-gertrude trap this trigger used to fall into).
        -- Its %fluffs ladder (gertrude_fluffs_talk) falls to the trailing
        -- else -> ~gertrude_route_topics (gertrude.rs2:107-136): Ratcatchers
        -- is not eligible here (%giantdwarf_quest never started), so
        -- $rat=0/$tail=1 routes straight to ~twocats_gertrude_after_bob
        -- with no topic menu -- a single PLAYER line (twocats.rs2:441-443),
        -- no npc reply.
        t.exec("goto-gertrude", t.player.goto_tile, 3151, 3410, 0)
        t.exec("talkToGertrude", t.player.talk_to, "gertrude", 1)
        t.exec("talkToGertrude-dialog", t.chat.play, {
            "player:I found Bob!",
        })
        t.expect("quest.stage.gertrude_done", t.quest.expect_stage("gertrude_done"))

        -- Talk to Reldo, Varrock castle library (spawned 3209,3495,0, base
        -- symbol "reldo"). reldo.rs2's own [opnpc1,reldo] owns the
        -- %twocats_quest 25..30 window and hands off to
        -- [proc,twocats_reldo_talk] -> [label,twocats_talk_to_reldo]
        -- (twocats.rs2:460-470): a single PLAYER line, then (amulet held)
        -- %twocats_reldo=1 and %twocats_quest=30 with no reply page.
        t.exec("goto-reldo", t.player.goto_tile, 3209, 3495, 0)
        t.exec("talkToReldo", t.player.talk_to, "reldo", 1)
        t.exec("talkToReldo-dialog", t.chat.play, {
            "player:I have a cat related question.",
        })
        t.expect("quest.stage.reldo_done", t.quest.expect_stage("reldo_done"))

        -- Find Bob (2nd): amulet Open again (twocats.rs2:159-167), same
        -- mesbox-then-panel shape as the first.
        t.exec("amulet.open2", t.player.inv_op, "twocats_amuletofcatspeak", 3)
        t.exec("amulet.open2-dialog", t.chat.play, {
            "mesbox:You use the Catspeak amulet (e) again",
        })
        local dir2_result, dir2_detail = t.msg.expect("Bob")
        t.check("locator.direction2", dir2_result == "ok", tostring(dir2_detail))
        -- Panel mount not asserted here either -- see the note beside
        -- locator.direction1 above.

        -- Talk to Bob (2nd): [proc,twocats_bob_talk_2] (twocats.rs2:358-371)
        -- opens with the PLAYER's own line (trap 18) -- player, npc, npc --
        -- ending %twocats_quest=35.
        t.exec("goto-bob-2", t.player.goto_tile, 2924, 3565, 0)
        t.exec("talkToBobAgain", t.player.talk_to, "death_growncat_black", 1)
        t.exec("talkToBobAgain-dialog", t.chat.play, {
            "player:Reldo told me about Robert the Strong, and the Dragonkin.",
            "npc:Robert... that name stirs something in me, like a memory I can't quite reach.",
            "npc:There's someone in Sophanem who might help us both remember",
        })
        t.expect("quest.stage.bob_found_again", t.quest.expect_stage("bob_found_again"))

        -- ---------------------------------------------------------------
        -- Talk to the Sphinx, Sophanem (spawned 3300,2784,0, symbol
        -- ics_little_sphinx). Its owner dragonslayer2.rs2's own
        -- [opnpc1,ics_little_sphinx] hands %twocats_quest 35..40 to
        -- @twocats_talk_to_sphinx (twocats.rs2:501-607) -- the full pinned
        -- hypnosis-and-reveal cutscene, ~80 pages ending in a real teleport
        -- choice and the chores objbox. Every page gets a chat.play entry;
        -- `*` marks a page whose exact text carries no symbol/stage write
        -- to verify (pure Bob/Sphinx/Neite/Cat banter) -- the surrounding
        -- exact rows are the amulet gate, the hypnosis start/end, the
        -- agreement to help and the teleport offer, each cited to its own
        -- line in twocats.rs2.
        -- ---------------------------------------------------------------
        t.exec("goto-sphinx", t.player.goto_tile, 3300, 2784, 0)
        t.exec("talkToSphinx", t.player.talk_to, "ics_little_sphinx", 1)

        local sphinx_pages = {
            "player:Good day.", -- twocats.rs2:502
            "npc:What is it human?", -- 511
            "player:Sphinx, we need your help!", -- 512
        }
        for i = 1, 12 do table.insert(sphinx_pages, "*") end -- 518-529: Cat's
        -- plea + the Sphinx's own agreement-to-help exchange (chatnpc_specific
        -- Cat at 518, then 519-529)
        table.insert(sphinx_pages, "npc:It is true that there is something about Bob") -- 530
        for i = 1, 15 do table.insert(sphinx_pages, "*") end -- 531-546: the
        -- "come with me" + hypnosis setup (533-546)
        table.insert(sphinx_pages, "npc:You are now under my influence.") -- 547
        for i = 1, 15 do table.insert(sphinx_pages, "*") end -- 548-562: the
        -- hypnosis Q&A + dragonkin flashback (Robert the Strong / Dragonkin)
        table.insert(sphinx_pages, "npc:You are no longer under my influence.") -- 563
        for i = 1, 30 do table.insert(sphinx_pages, "*") end -- 564-593: the
        -- reveal to Cat, Neite's summons and reaction, Bob asking the favour
        table.insert(sphinx_pages, "player:OK, no problem Bob.") -- 594
        table.insert(sphinx_pages, "npc:since you are to look after Unferth") -- 599
        table.insert(sphinx_pages, "choose:Yes, teleport me to Unferth's house.") -- 600
        table.insert(sphinx_pages, "*") -- 606: the chores objbox

        t.exec("talkToSphinx-dialog", t.chat.play, sphinx_pages)
        t.expect("quest.stage.chores_ready", t.quest.expect_stage("chores_ready"))

        -- ---------------------------------------------------------------
        -- Step 40: the five chores at Unferth's house, then the potatoes.
        -- Uncoupled now (parity1b, twocats.rs2:701-834) -- each trigger
        -- keeps only its own already-done guard, no chore-order chaining.
        -- Every chore's ~mesbox SUSPENDS the calling script (trap 22) --
        -- chat.play dismisses it before the var read below.
        -- ---------------------------------------------------------------
        t.exec("goto-patch", t.player.goto_tile, 2919, 3562, 0)
        local patch = t.player.by_symbol("loc", "twocats_patch")
        t.check("patch.found", patch ~= nil, "twocats_patch resolved: " .. tostring(patch and patch.id))

        -- [oplocu,twocats_patch] (twocats.rs2:644-683): last_useitem=rake ->
        -- tidygarden=3.
        t.exec("chore.rake", t.player.use_on, "rake", patch)
        t.exec("chore.rake-dialog", t.chat.play, { "mesbox:You rake Unferth's overgrown garden patch" })
        t.expect("chore.raked", t.var.await_server("twocats_chores_tidygarden", 3, 10))

        -- Same trigger, last_useitem=potato_seed (dibber also matches, seeds
        -- is the one Quest Helper spends): tidygarden 3->4, arms
        -- [softtimer,twocats_potato_grow].
        t.exec("chore.plant", t.player.use_on, "potato_seed", patch)
        t.exec("chore.plant-dialog", t.chat.play, { "mesbox:You plant the potato seeds" })
        t.expect("chore.planted", t.var.await_server("twocats_chores_tidygarden", 4, 10))

        -- [oploc1,twocats_bed] (twocats.rs2:709-720): op1=Make. tidyhouse
        -- 0->1.
        t.exec("goto-house", t.player.goto_tile, 2918, 3558, 0)
        t.exec("chore.bed", t.player.click_loc, "twocats_bed", 1)
        t.exec("chore.bed-dialog", t.chat.play, { "mesbox:You make Unferth's unmade bed" })
        t.expect("chore.bed_made", t.var.await_server("twocats_chores_tidyhouse", 1, 10))

        -- [oplocu,twocats_fireplace] (twocats.rs2:734-764): logs first
        -- (warmhuman 0->1), then tinderbox (warmhuman 1->2).
        local fireplace = t.player.by_symbol("loc", "twocats_fireplace")
        t.check("fireplace.found", fireplace ~= nil, "twocats_fireplace resolved: " .. tostring(fireplace and fireplace.id))
        t.exec("chore.logs", t.player.use_on, "logs", fireplace)
        t.exec("chore.logs-dialog", t.chat.play, { "mesbox:You use the logs on Unferth's fireplace" })
        t.expect("chore.logs_placed", t.var.await_server("twocats_chores_warmhuman", 1, 10))

        t.exec("chore.light", t.player.use_on, "tinderbox", fireplace)
        t.exec("chore.light-dialog", t.chat.play, { "mesbox:You light the fire with a tinderbox" })
        t.expect("chore.fire_lit", t.var.await_server("twocats_chores_warmhuman", 2, 10))

        -- [oplocu,twocats_table] (twocats.rs2:773-789): cake first
        -- (feedhuman 0->3), then milk (feedhuman 3->4).
        local table_loc = t.player.by_symbol("loc", "twocats_table")
        t.check("table.found", table_loc ~= nil, "twocats_table resolved: " .. tostring(table_loc and table_loc.id))
        t.exec("chore.cake", t.player.use_on, "chocolate_cake", table_loc)
        t.exec("chore.cake-dialog", t.chat.play, { "mesbox:You place a chocolate cake on Unferth's table" })
        t.expect("chore.cake_placed", t.var.await_server("twocats_chores_feedhuman", 3, 10))
        t.exec("chore.milk", t.player.use_on, "bucket_milk", table_loc)
        t.exec("chore.milk-dialog", t.chat.play, { "mesbox:You pour a bucket of milk on Unferth's table" })
        t.expect("chore.milk_placed", t.var.await_server("twocats_chores_feedhuman", 4, 10))

        -- [opnpcu,twocats_unferth]+children (twocats.rs2:812-834): use
        -- shears on Unferth himself. tidyhuman 0->8. Opens with the
        -- PLAYER's own line (chatplayer_anim), no npc reply.
        local unferth = t.player.by_symbol("npc", "twocats_unferth")
        t.check("unferth.found", unferth ~= nil, "twocats_unferth resolved: " .. tostring(unferth and unferth.id))
        t.exec("chore.shear", t.player.use_on, "shears", unferth)
        t.exec("chore.shear-dialog", t.chat.play, { "player:You shear Unferth's overgrown fur with the shears." })
        t.expect("chore.sheared", t.var.await_server("twocats_chores_tidyhuman", 8, 10))

        -- [softtimer,twocats_potato_grow] was armed by the planting above.
        -- [debugproc,twocats_growpotatoes] (twocats.rs2:890-900, sanctioned
        -- grind fast-forward, docs/QUEST_SERVER_CHEATS.md section A) runs
        -- the SAME [proc,twocats_potato_advance] body the real timer calls,
        -- once per remaining stage, rather than writing the end state.
        t.expect("garden.grow", t.cheat("::twocats_growpotatoes"))
        t.expect("garden.grown", t.var.await_server("twocats_chores_tidygarden", 8, 10))

        -- Step 40 -> 45: Unferth's dispatch at %twocats_quest=40 goes to
        -- [label,twocats_wait_for_potatoes] (twocats.rs2:916-923), which now
        -- reads all five chore thresholds as met and, with a bare mesbox
        -- (no player line first), sets %twocats_quest=45.
        t.exec("waitForPotatoes", t.player.talk_to, "twocats_unferth", 1)
        t.exec("waitForPotatoes-dialog", t.chat.play, {
            "mesbox:All chores complete! The potatoes are ready to harvest!",
        })
        t.expect("quest.stage.potatoes_grown", t.var.await_server("twocats_quest", 45, 10))

        -- Step 45 -> 50: [label,twocats_report_to_unferth]
        -- (twocats.rs2:929-931), the player's own line, no reply page.
        t.exec("reportToUnferth", t.player.talk_to, "twocats_unferth", 1)
        t.exec("reportToUnferth-dialog", t.chat.play, { "player:I've finished all your chores!" })
        t.expect("quest.stage.reported", t.var.await_server("twocats_quest", 50, 10))

        -- Step 50 -> 55: the Apothecary in SW Varrock (spawned 3195,3404,0),
        -- reached through apothecary.rs2's own dispatch at %twocats_quest=50
        -- -> [label,twocats_talk_to_apoth] (twocats.rs2:945-953). Grants a
        -- doctor's hat (both hat totals are 0 here) and sets
        -- %twocats_quest=55.
        t.exec("goto-apothecary", t.player.goto_tile, 3195, 3405, 0)
        t.exec("talkToApothecary", t.player.talk_to, "apothecary", 1)
        t.exec("talkToApothecary-dialog", t.chat.play, {
            "player:Talk about A Tail of Two Cats.",
            "mesbox:You receive a doctor's hat from the Apothecary!",
        })
        t.expect("quest.stage.apothecary", t.var.await_server("twocats_quest", 55, 10))
        t.expect("reward.doctors_hat_granted", t.inv.expect_has("twocats_doctors_hat", 1))

        -- Step 55 -> 60: [label,twocats_cure_unferth] (twocats.rs2:969-990)
        -- requires the hat, desert shirt AND robe all WORN, no weapon or
        -- shield equipped, and a vial of water carried (consumed). Player,
        -- npc, mesbox -- three pages.
        t.exec("cure.equip_hat", t.player.equip, "twocats_doctors_hat")
        t.exec("cure.equip_shirt", t.player.equip, "desert_shirt")
        t.exec("cure.equip_robe", t.player.equip, "desert_robe")
        t.exec("goto-unferth-cure", t.player.goto_tile, 2918, 3558, 0)
        t.exec("cureUnferth", t.player.talk_to, "twocats_unferth", 1)
        t.exec("cureUnferth-dialog", t.chat.play, {
            "player:I've come to cure you!",
            "npc:...Doctor? Is that really you? I feel better already!",
            "mesbox:You administer the miracle cure",
        })
        t.expect("quest.stage.cured", t.var.await_server("twocats_quest", 60, 10))

        -- Take the hat back off: [queue,twocats_quest_complete]
        -- (twocats.rs2:1008-1024) reads `inv_total(inv, twocats_doctors_hat)`
        -- -- the BACKPACK container, not worn -- so a hat left worn through
        -- completion reads 0 there and the quest grants a SECOND doctor's
        -- hat instead of the nurse's hat the wiki documents. The cure's own
        -- guard only needed it worn at that one click.
        t.exec("cure.unequip_hat", t.player.unequip, "twocats_doctors_hat")

        -- Find Bob (3rd): amulet Open a final time (twocats.rs2:169-177).
        t.exec("amulet.open3", t.player.inv_op, "twocats_amuletofcatspeak", 3)
        t.exec("amulet.open3-dialog", t.chat.play, {
            "mesbox:You use the Catspeak amulet (e) one final time",
        })
        local dir3_result, dir3_detail = t.msg.expect("Bob")
        t.check("locator.direction3", dir3_result == "ok", tostring(dir3_detail))
        -- Panel mount not asserted here either -- see the note beside
        -- locator.direction1 above.

        -- ---------------------------------------------------------------
        -- Talk to Bob (3rd, to finish): [proc,twocats_bob_talk_3]
        -- (twocats.rs2:373-420) -- the verbatim road-trip cutscene, 31
        -- pages, ending %twocats_quest=65. Exact text at open/close (both
        -- ordinary chatplayer_anim/chatnpc_anim); `*` for the interior
        -- Bob/Neite/King Black Dragon/R4ng3rNo0b889 banter, all
        -- chatnpc_specific pages with no symbol or stage write behind them.
        -- ---------------------------------------------------------------
        t.exec("goto-bob-3", t.player.goto_tile, 2924, 3565, 0)
        t.exec("talkToBobToFinish", t.player.talk_to, "death_growncat_black", 1)

        local bob3_pages = {
            "player:Hi Bob! How's it hang... going?", -- twocats.rs2:389
            "npc:Wonderful! Neite and I have been all over Gielinor!", -- 390
        }
        for i = 1, 28 do table.insert(bob3_pages, "*") end -- 391-418: the
        -- carpet-ride banter (Bob/Neite/King Black Dragon/R4ng3rNo0b889)
        table.insert(bob3_pages, "player:Phew!") -- 419

        t.exec("talkToBobToFinish-dialog", t.chat.play, bob3_pages)
        t.expect("quest.stage.bob_found_last", t.var.await_server("twocats_quest", 65, 10))

        -- Step 65 -> 70: [label,twocats_done] (twocats.rs2:1003-1006), the
        -- player's own line, %twocats_quest=70, queues
        -- [queue,twocats_quest_complete]. Run 1 skipped this goto (player
        -- was still standing at Bob's tile, three squares off) and the
        -- press landed as a bare `map_flag` walk with no npc engaged --
        -- "map_flag: no dialogue in 5 tick(s)" -- which cascaded through
        -- every row after it.
        t.exec("goto-unferth-finish", t.player.goto_tile, 2918, 3558, 0)
        t.exec("finishQuest", t.player.talk_to, "twocats_unferth", 1)
        t.exec("finishQuest-dialog", t.chat.play, { "player:It's all over!" })
        t.expect("quest.stage.complete", t.var.await_server("twocats_quest", 70, 10))

        -- quest.expect_complete() writes FOUR rows unconditionally and
        -- twocats.rs2 has no `~<abbr>_journal` proc at all (grep for
        -- "journal" in it is empty) -- section 8's documented case for a
        -- quest whose journal channel can never pass. Hand-roll the three
        -- rows that DO pass, per that recipe.
        local varp_complete_result, varp_complete_detail = t.quest.expect_stage("complete")
        t.step("quest.varp_complete", varp_complete_result == "ok" and "PASS" or "FAIL",
            "expect_stage(complete) -> " .. tostring(varp_complete_result)
                .. " " .. tostring(varp_complete_detail))

        local scroll_result, scroll_detail = t.scroll.title()
        local scroll_name = type(scroll_detail) == "table" and scroll_detail.name or nil
        local scroll_pass = scroll_result == "ok" and type(scroll_name) == "string"
            and string.find(scroll_name, "A Tail of Two Cats", 1, true) ~= nil
        local scroll_shot_result, scroll_shot_detail = t.shot("quest.scroll")
        local scroll_shot_note = ""
        if scroll_shot_result == "ok" and type(scroll_shot_detail) == "string"
            and string.find(scroll_shot_detail, "unchanged", 1, true) then
            scroll_shot_note = " [scroll already photographed: " .. scroll_shot_detail .. "]"
        end
        t.step("quest.scroll_title", scroll_pass and "PASS" or "FAIL",
            "expected a title containing A Tail of Two Cats got=" .. tostring(scroll_name)
                .. " (" .. tostring(scroll_result) .. ")" .. scroll_shot_note)
        t.check("quest.scroll_close", t.scroll.close())

        local qp_after_result, qp_after = t.var.varp("qp")
        local points_pass = qp_before_result == "ok" and qp_after_result == "ok"
            and (qp_after - qp_before) == 2
        t.step("quest.points", points_pass and "PASS" or "FAIL",
            "qp " .. tostring(qp_before) .. " -> " .. tostring(qp_after)
                .. " delta=" .. tostring(qp_after_result == "ok" and qp_before_result == "ok"
                    and (qp_after - qp_before) or "?")
                .. " expected=2")

        -- Rewards Quest Helper lists: 2 QP (quest.points, above), 2 antique
        -- lamps, a hat, a mouse toy. twocats_present (twocats.rs2:1030-1044)
        -- is the real completion container -- open it and assert the item
        -- grants; the lamps' own Rub/xpreward picker (twocats.rs2:1077-1183)
        -- is not a guide step and not driven here (see header comment).
        t.exec("present.open", t.player.inv_op, "twocats_present", 1)
        t.exec("present.open-dialog", t.chat.play, { "mesbox:You open the package" })
        t.expect("reward.lamps", t.inv.expect_has("twocats_rewardlamp", 2))
        t.expect("reward.mousetoy", t.inv.expect_has("twocats_mouse_toy", 1))
        t.expect("reward.nurses_hat", t.inv.expect_has("twocats_nurses_hat", 1))

        t.finish(0)
        return
    end,
}
