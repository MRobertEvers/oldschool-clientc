-- Throne of Miscellania, Astrid path (1 QP). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_misc/scripts/
--   misc_king_vargas.rs2, misc_princess_astrid.rs2, misc_prince_brand.rs2,
--   misc_queen_sigrid.rs2, misc_advisor_ghrim.rs2, misc_giant_nib.rs2,
--   misc_courting_emotes.rs2, misc_debug.rs2
-- and areas/area_miscellania/scripts/{flower_girl,derrik,lumberjack_leif}.rs2
--
-- Second Miscellania test (QUEUE.tsv last_failure): misc.lua already drives
-- the whole quest courting Prince Brand and declares Astrid's eight-step
-- ladder a content gap (mutually exclusive choice, misc_king_vargas.rs2's
-- own opt=1/opt=2 [opnpc1] choice, %misc_partner_multivar 1 vs 0). This file
-- drives the OTHER side: King Vargas is told to court Princess Astrid
-- instead, so the eight courtAstrid steps are real rows here and Brand's
-- eight courtBrand steps are the declared gap (his own bard duty --
-- brand_give_anthem, checked BEFORE the toldking/partner guard in
-- misc_prince_brand.rs2:23 -- fires for either partner, so getAnthem below
-- is still driven, not a gap).
--
-- Access: misc_door_guard.rs2 hard-gates the throne room on %heroquest =
-- ^hero_complete (Heroes' Quest, unported -- ::complete quest_heroes in
-- setup). goto_tile teleports straight past the door/guard and every
-- spiralstairs the same way it climbs stairs elsewhere in this pack
-- (QUEST_AUTHORING.md section 2) -- Vargas/Astrid/Brand/Ghrim (all level 1)
-- and the flower girl/Derrik/Leif (level 0) are each reached by a single
-- goto_tile at their own *.spawn tile; helper_coverage.py grades every
-- "go up/down stairs" Quest Helper step TRAVEL (merged into the step it
-- leads to) with no row of its own, confirmed against misc.lua's own
-- coverage run.
--
-- Courting partner: Princess Astrid (%misc_partner_multivar = 0). Her ladder
-- (misc_princess_astrid.rs2) is the mirror of Brand's: talk1 (flowers
-- request) -> give flowers (opnpcu, mes() only) -> Dance emote (real,
-- misc_courting_emotes.rs2's ~misc_emote_performed_astrid, content-parity
-- fix 2026-09-23) -> talk2 (bow request) -> give a bow (opnpcu) -> talk3
-- (ends "Truly?") -> Blow Kiss emote (real, same hook) -> give a ring
-- (opnpcu) -> %misc_acceptedtorule = 1.
--
-- Items: flowers (misc_flowergirl, 15gp, bought live below) are the only
-- courting/anthem/pen ingredient actually sold in the quest area itself;
-- the bow, iron bar, logs and ring are bring-along materials the same way
-- Advisor Ghrim's own reputation item already is (setup, not driven).
--
-- 75%-support finish gate: same real Managing Miscellania resource loop
-- misc.lua drives (lumberjack_leif.rs2's leif_intercept_wood): chop the
-- kingdom's maples for real beside Lumberjack Leif (Woodcutting 45 + an
-- axe, both set up below) until %misc_approval is demonstrably moving, then
-- ::misc_earnapproval -- the sanctioned GRIND fast-forward for exactly this
-- loop (docs/QUEST_SERVER_CHEATS.md) -- finishes it to the 75% threshold;
-- its effect is read back (t.msg.expect + var.await_server).
--
-- Reward: quest_misc's own ~quest_complete_rewards call lists "10000
-- coins|Management of Miscellania|Ring of wealth teleport to Miscellania"
-- as scroll text, and the FIRST of those three is a real grant this pack
-- makes: misc_king_vargas.rs2's [label,vargas_finish_quest] runs
-- `%misc_coffers = add(%misc_coffers, 10000)` on the line directly above
-- `queue(misc_quest_complete, 0, 0)`. That is the kingdom's coffer, not the
-- player's backpack -- but it is a DECLARED varbit (configs/all.varbit's
-- [misc_coffers], basevar misc_varbit_2, startbit 0 endbit 26;
-- all.varbit.compack 74=misc_coffers), so t.var.server reads it like any
-- other and the grant is asserted below. The other two scroll lines really
-- are text only, graded by the scroll's own literal lines plus the 1 quest
-- point quest.expect_complete()'s quest.points row already checks.

return {
    id = "misc_astrid",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so everything below fits
        "::give coins 20", -- misc_flowergirl wants 15gp for three flowers
        "::give iron_bar 1", -- Derrik forges the giant nib from this (misc_smithy.rs2)
        "::give logs 1", -- combined with the nib to make the giant pen (misc_giant_nib.rs2)
        "::give shortbow 1", -- Astrid's second courting gift (opnpcu bow case, misc_princess_astrid.rs2:114)
        "::give gold_ring 1", -- Astrid's third courting gift (opnpcu ring case, misc_princess_astrid.rs2:122)
        "::setlevel woodcutting 45", -- the maple row's own level gate (woodcutting_trees), also Ghrim's reputation-tool level for the real support grind
        "::give bronze_axe 1", -- the real axe ::misc_earnapproval's ~woodcutting_axe_checker needs
        "::complete quest_heroes", -- misc_door_guard.rs2's hard gate: %heroquest = ^hero_complete
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "misc_quest",
            constants = {
                not_started = 0,
                talked_to_king = 10,
                talked_to_queen = 20,
                queen_requests_recognition = 30,
                need_bard_for_anthem = 40,
                prince_composed_anthem = 50,
                advisor_corrected_anthem = 60,
                queen_gave_treaty = 70,
                gave_king_treaty = 80,
                king_signed_treaty = 90,
                complete = 100, -- managing_miscellania.constant:6, not the quest_misc.constant file (that one stops at 90)
            },
            row = "quest_throneofmiscellania", -- all.dbrow.compack:148
            display = "Throne of Miscellania",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats (::give, ::complete) are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------- King Vargas: offer
        t.exec("goto-vargas1", t.player.goto_tile, 2501, 3859, 1)
        t.exec("talkVargas1", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargas1-dialog", t.chat.play, {
            "player:You wanted to see me, Your Maj",
            "npc:Ah, yes. I am cursed -- I cann",
            "npc:My children Brand and Astrid c",
            "choose:I'll try to win over Princess Astrid.",
            "player:I'll try to win over Princess As",
            "npc:Astrid can usually be found ups",
        })

        -- The choice above ("I'll try to win over Princess Astrid.") is
        -- mutually exclusive with the Brand branch (misc_king_vargas.rs2's
        -- own opt=1/opt=2 [opnpc1,misc_king_vargas] choice, %misc_partner_multivar
        -- 0 vs 1) -- Quest Helper's courtBrand ladder can never run in the
        -- same playthrough as courtAstrid, so its eight steps are declared
        -- here rather than driven (mirror of misc.lua's own Astrid gap).
        -- Astrid was courted instead: getAnthem below still drives Brand's
        -- separate bard duty, which fires for either partner
        -- (misc_prince_brand.rs2:23's own guard runs before the
        -- toldking/partner check).
        -- GUIDE-GAP: talkBrand1 not driven -- Astrid was courted instead, the mutually exclusive choice (misc_prince_brand.rs2:51)
        -- GUIDE-GAP: giveFlowersToBrand not driven -- Astrid was courted instead, the mutually exclusive choice (misc_prince_brand.rs2:112)
        -- GUIDE-GAP: clapForBrand not driven -- Astrid was courted instead, the mutually exclusive choice (misc_courting_emotes.rs2:32)
        -- GUIDE-GAP: talkBrand2 not driven -- Astrid was courted instead, the mutually exclusive choice (misc_prince_brand.rs2:72)
        -- GUIDE-GAP: giveCakeToBrand not driven -- Astrid was courted instead, the mutually exclusive choice (misc_prince_brand.rs2:123)
        -- GUIDE-GAP: talkBrand3 not driven -- Astrid was courted instead, the mutually exclusive choice (misc_prince_brand.rs2:85)
        -- GUIDE-GAP: blowKissToBrand not driven -- Astrid was courted instead, the mutually exclusive choice (misc_courting_emotes.rs2:39)
        -- GUIDE-GAP: useRingOnBrand not driven -- Astrid was courted instead, the mutually exclusive choice (misc_prince_brand.rs2:132)

        -- ---------------------------------------------------- courting Astrid
        t.exec("goto-astrid1", t.player.goto_tile, 2502, 3868, 1)
        -- content-parity fix: Vargas now sets %misc_affection to
        -- not_started (not step0), so this FIRST visit reads astrid_talk1,
        -- the five-line courtship intro (Quest Helper's own talkAstrid1 step).
        t.exec("talkAstrid1", t.player.talk_to, "misc_princess_astrid", 1)
        t.exec("talkAstrid1-dialog", t.chat.play, {
            "player:So, Princess, I hear you're quite the archer.",
            "npc:Archery is a noble art! Not everyone in this castle",
            "player:That doesn't sound very fair.",
            "npc:Derrik has been very helpful, teaching me in secret",
            "npc:It's kind of you to listen. If you brought me some flowers",
        })

        t.exec("goto-flowergirl", t.player.goto_tile, 2514, 3866, 0)
        t.exec("buyFlowers", t.player.talk_to, "misc_flowergirl", 1)
        t.exec("buyFlowers-dialog", t.chat.play, {
            "npc:Hello.",
            "player:Good day. What are you doing?",
            "npc:I'm selling flowers, 15gp for three",
            "choose:Yes, please.",
            "player:Yes, please",
            "npc:Thank you! Here you go.",
        })
        local flowers_await_result, flowers_await_detail = t.inv.await("flowers_waterfall_quest", 1, 10)
        t.check("inv.gotFlowers", flowers_await_result == "ok",
            "inv.await(flowers_waterfall_quest,1) -> " .. tostring(flowers_await_result) .. " " .. tostring(flowers_await_detail))

        t.exec("goto-astrid2", t.player.goto_tile, 2502, 3868, 1)
        -- misc_princess_astrid.rs2's opnpcu flowers case ends in a plain
        -- mes() line (a chat-log message, not a ~mesbox), so use_on's own
        -- settle (new chat line / backpack change) is the whole of the row
        -- -- it only gets Astrid to ASK for the Dance now, it does not
        -- perform it.
        local astrid = t.player.by_symbol("npc", "misc_princess_astrid")
        t.exec("giveFlowersToAstrid", t.player.use_on, "flowers_waterfall_quest", astrid)
        -- inv.await(name, 0, ticks) never actually waits (total >= 0 is
        -- always true -- QUEST_AUTHORING.md's gaps section) -- ticks then a
        -- direct count read is the real "wait for it to be consumed".
        t.ticks(2)
        local flowers_gone_result, flowers_gone_count = t.inv.count("flowers_waterfall_quest")
        t.check("giveFlowersToAstrid.consumed", flowers_gone_result == "ok" and flowers_gone_count == 0,
            "inv.count(flowers_waterfall_quest) -> " .. tostring(flowers_gone_result) .. " " .. tostring(flowers_gone_count))

        -- Content-parity leg: astrid_need_dance -> the player must actually
        -- play Dance next to Astrid (misc_courting_emotes.rs2's
        -- ~misc_emote_performed_astrid, hooked off ~emote_perform), s1_step1
        -- (11) -> s1_step5 (15). Quest Helper's own danceForAstrid step.
        t.exec("danceForAstrid", t.player.emote, "dance")
        t.expect("affection.s1_step5", t.var.expect("misc_affection", 15))

        astrid = t.player.by_symbol("npc", "misc_princess_astrid")
        t.exec("talkAstrid2", t.player.talk_to, "misc_princess_astrid", 1)
        t.exec("talkAstrid2-dialog", t.chat.play, {
            "player:What happened next?",
            "npc:Derrik took me hunting once, out past the walls",
            "player:That sounds like a good idea.",
            "npc:I'm quite fond of it myself, though I'd never say so",
        })

        -- Unlike the flowers/ring cases, the bow branch (misc_princess_
        -- astrid.rs2:114-121) never calls inv_del -- Astrid only examines
        -- it and hands it back, so the real evidence is the affection
        -- state advancing to s2_step4, not the backpack falling.
        astrid = t.player.by_symbol("npc", "misc_princess_astrid")
        t.exec("giveBowToAstrid", t.player.use_on, "shortbow", astrid)
        t.ticks(2)
        local bow_kept_result, bow_kept_count = t.inv.count("shortbow")
        t.check("giveBowToAstrid.kept", bow_kept_result == "ok" and bow_kept_count == 1,
            "inv.count(shortbow) -> " .. tostring(bow_kept_result) .. " " .. tostring(bow_kept_count)
                .. " (misc_princess_astrid.rs2's bow case never inv_dels -- she hands it back)")
        t.expect("affection.s2_step4", t.var.expect("misc_affection", 24))

        -- astrid_talk3 now ENDS at "Truly?" (s2_step4 -> s3_step4) -- the
        -- kiss itself is the emote leg below, not narrated inline any more.
        t.exec("talkAstrid3", t.player.talk_to, "misc_princess_astrid", 1)
        t.exec("talkAstrid3-dialog", t.chat.play, {
            "player:Do you like it here in Miscellania?",
            "npc:It's a lovely little country, though I don't suppose",
            "player:I could say the same about Brand and his poetry.",
            "npc:And what a great bard he makes! Truly, though...",
        })

        -- Content-parity leg: astrid_need_kiss -> the player must actually
        -- play Blow Kiss next to Astrid, s3_step4 -> s3_step0 (30). Named
        -- after Quest Helper's own blowKissToAstrid step.
        t.exec("blowKissToAstrid", t.player.emote, "blow kiss")
        t.expect("affection.s3_step0", t.var.expect("misc_affection", 30))

        astrid = t.player.by_symbol("npc", "misc_princess_astrid")
        t.exec("useRingOnAstrid", t.player.use_on, "gold_ring", astrid)
        t.exec("useRingOnAstrid-dialog", t.chat.play, {
            "player:Princess Astrid, will you vouch for me",
            "npc:I will -- and gladly.",
        })
        -- misc_acceptedtorule is the varBIT the ring case sets (opnpcu
        -- misc_princess_astrid, gold_ring branch) -- proven live by the very
        -- next row below, Vargas's "Wonderful!" branch, which only fires
        -- when it reads 1 (misc_king_vargas.rs2's %misc_acceptedtorule
        -- check); a direct var poll here is redundant with that.

        -- ---------------------------------------------------- back to Vargas
        t.exec("goto-vargas2", t.player.goto_tile, 2501, 3859, 1)
        t.exec("talkVargas2", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargas2-dialog", t.chat.play, {
            "npc:Wonderful! Now, let us discuss securing peace",
        })
        t.expect("quest.stage.talked_to_king", t.quest.expect_stage("talked_to_king"))

        -- ---------------------------------------------------- Etceteria diplomacy
        t.exec("goto-sigrid1", t.player.goto_tile, 2612, 3877, 1)
        t.exec("talkSigrid1", t.player.talk_to, "misc_queen_sigrid", 1)
        t.exec("talkSigrid1-dialog", t.chat.play, {
            "player:King Vargas sent me to discuss peace",
            "npc:Peace? Only if Vargas is willing to formally",
        })
        t.expect("quest.stage.talked_to_queen", t.quest.expect_stage("talked_to_queen"))

        t.exec("goto-vargas3", t.player.goto_tile, 2501, 3859, 1)
        t.exec("talkVargas3", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargas3-dialog", t.chat.play, {
            "player:Queen Sigrid wants you to recognise Etceteria",
            "npc:Recognise Etceteria? After the insults",
        })
        t.expect("quest.stage.queen_requests_recognition", t.quest.expect_stage("queen_requests_recognition"))

        t.exec("goto-sigrid2", t.player.goto_tile, 2612, 3877, 1)
        t.exec("talkSigrid2", t.player.talk_to, "misc_queen_sigrid", 1)
        t.exec("talkSigrid2-dialog", t.chat.play, {
            "player:King Vargas says he'll recognise Etceteria",
            "npc:A new anthem? Our anthem is a fine old song",
        })
        t.expect("quest.stage.need_bard_for_anthem", t.quest.expect_stage("need_bard_for_anthem"))

        -- ---------------------------------------------------- the anthem
        -- Brand's own bard duty (brand_give_anthem) fires regardless of
        -- who is being courted -- misc_prince_brand.rs2:23's guard runs
        -- before the toldking/partner check, so this leg is driven here
        -- exactly as in misc.lua, not a gap.
        t.exec("goto-brand-anthem", t.player.goto_tile, 2502, 3852, 1)
        t.exec("getAnthem", t.player.talk_to, "misc_prince_brand", 1)
        t.exec("getAnthem-dialog", t.chat.play, {
            "player:King Vargas mentioned you fancy yourself a bit of a bard",
            "npc:A bard! Yes, I've always fancied myself",
            "npc:There! A masterpiece, if I do say so",
        })
        t.expect("quest.stage.prince_composed_anthem", t.quest.expect_stage("prince_composed_anthem"))
        local awful_result, awful_detail = t.inv.await("misc_awful_anthem", 1, 10)
        t.check("inv.gotAwfulAnthem", awful_result == "ok",
            "inv.await(misc_awful_anthem,1) -> " .. tostring(awful_result) .. " " .. tostring(awful_detail))

        t.exec("goto-ghrim1", t.player.goto_tile, 2499, 3857, 1)
        t.exec("correctAnthem", t.player.talk_to, "misc_advisor_ghrim", 1)
        t.exec("correctAnthem-dialog", t.chat.play, {
            "player:Prince Brand wrote this anthem for Etceteria",
            "npc:Let me see that. ...Oh dear.",
            "npc:There. A vast improvement, if I may say so.",
        })
        t.expect("quest.stage.advisor_corrected_anthem", t.quest.expect_stage("advisor_corrected_anthem"))
        local good_result, good_detail = t.inv.await("misc_good_anthem", 1, 10)
        t.check("inv.gotGoodAnthem", good_result == "ok",
            "inv.await(misc_good_anthem,1) -> " .. tostring(good_result) .. " " .. tostring(good_detail))

        t.exec("goto-sigrid3", t.player.goto_tile, 2612, 3877, 1)
        t.exec("giveAnthemToSigrid", t.player.talk_to, "misc_queen_sigrid", 1)
        t.exec("giveAnthemToSigrid-dialog", t.chat.play, {
            "player:Advisor Ghrim has finished the new anthem.",
            "npc:Why, this is rather good! Very well",
        })
        t.expect("quest.stage.queen_gave_treaty", t.quest.expect_stage("queen_gave_treaty"))
        local treaty_result, treaty_detail = t.inv.await("misc_treaty", 1, 10)
        t.check("inv.gotTreaty", treaty_result == "ok",
            "inv.await(misc_treaty,1) -> " .. tostring(treaty_result) .. " " .. tostring(treaty_detail))

        -- ---------------------------------------------------- the treaty and the pen
        t.exec("goto-vargas4", t.player.goto_tile, 2501, 3859, 1)
        t.exec("giveTreatyToVargas", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("giveTreatyToVargas-dialog", t.chat.play, {
            "player:Queen Sigrid has agreed to the treaty.",
            "npc:At last! I'll sign this gladly",
        })
        t.expect("quest.stage.gave_king_treaty", t.quest.expect_stage("gave_king_treaty"))

        t.exec("goto-derrik", t.player.goto_tile, 2551, 3897, 0)
        t.exec("forgeNib", t.player.talk_to, "misc_smithy", 1)
        t.exec("forgeNib-dialog", t.chat.play, {
            "player:I have a slightly strange request",
            "npc:Let's see what we can do.",
            "npc:There you are. You'll need to fix that",
        })
        local nib_result, nib_detail = t.inv.await("misc_giant_nib", 1, 10)
        t.check("inv.gotNib", nib_result == "ok",
            "inv.await(misc_giant_nib,1) -> " .. tostring(nib_result) .. " " .. tostring(nib_detail))

        -- misc_giant_nib.rs2's opheldu combine is a single mes() line too --
        -- no dialogue to continue_() through, just an inventory change.
        t.exec("makePen", t.player.use_item_on_item, "misc_giant_nib", "logs")
        local pen_result, pen_detail = t.inv.await("misc_giant_pen", 1, 10)
        t.check("inv.gotPen", pen_result == "ok",
            "inv.await(misc_giant_pen,1) -> " .. tostring(pen_result) .. " " .. tostring(pen_detail))

        t.exec("goto-vargas5", t.player.goto_tile, 2501, 3859, 1)
        t.exec("giveVargasPen", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("giveVargasPen-dialog", t.chat.play, {
            "npc:A giant pen! Now I can sign in a manner",
        })
        t.expect("quest.stage.king_signed_treaty", t.quest.expect_stage("king_signed_treaty"))

        -- ---------------------------------------------------- earning 75% support
        -- Quest Helper's own guide (ThroneOfMiscellania.java, stage-90
        -- finishOff/get75Support) has NO talk-to-Ghrim leg here -- it goes
        -- straight from king_signed_treaty to the chop/mine/rake/fish
        -- activity, then to King Vargas. misc_advisor_ghrim.rs2's own
        -- "put me to work" reply is narration only (content-parity fix
        -- 2026-09-23) and sets nothing, so a visit here would be a driven
        -- row for a step the guide does not have -- skipped, following the
        -- guide (same as misc.lua).
        --
        -- Real leg: chop the kingdom's maples for real beside Lumberjack
        -- Leif. Woodcutting 45 + an axe are set up above; every successful
        -- swing inside the kingdom zone runs the real leif_intercept_wood
        -- (misc_approval +1, real Woodcutting xp) -- click_loc starts the
        -- real repeating chop action (oploc1 -> oploc3 resume, woodcut.rs2)
        -- that keeps swinging on its own -- but get_logs rolls a 1-in-8
        -- deplete chance on EVERY successful swing, and a deplete stops the
        -- resume outright, so re-find and re-click "mapletree" each time
        -- the current session ends short of the target, awaiting one more
        -- approval point than is already banked (same pattern as misc.lua).
        --
        -- Trap 15's "record the loop's OUTCOME row only": an attempt whose
        -- own non-effect is a walk, or that lands on a tree already
        -- depleted this same tick ("Nothing interesting happens." -- a
        -- real, occasional content answer while the neighbouring copy is
        -- mid-regrowth, not a click failure), is not graded per attempt --
        -- called directly rather than through t.exec, so a click that did
        -- not land this time cannot fail the ledger over a loop that still
        -- reaches its target. approval.real_chops below is the row that
        -- proves the outcome.
        t.exec("goto-leif", t.player.goto_tile, 2550, 3866, 0)
        local chop_attempts = 0
        local chop_last_result, chop_last_detail = "n/a", "n/a"
        local approval_result, approval_value = t.var.server("misc_approval")
        while (approval_result ~= "ok" or (approval_value or 0) < 3) and chop_attempts < 10 do
            chop_attempts = chop_attempts + 1
            local before_result, before_value = t.var.server("misc_approval")
            chop_last_result, chop_last_detail = t.player.click_loc("mapletree", 1)
            t.shot("chopMaple-" .. chop_attempts)
            t.var.await_server("misc_approval", (before_value or 0) + 1, 250)
            approval_result, approval_value = t.var.server("misc_approval")
        end
        t.check("approval.real_chops", approval_result == "ok" and (approval_value or 0) >= 3,
            "var.server(misc_approval) after " .. tostring(chop_attempts) .. " chopMaple attempt(s) (last: "
                .. tostring(chop_last_result) .. " " .. tostring(chop_last_detail) .. ") -> "
                .. tostring(approval_result) .. " " .. tostring(approval_value))

        -- ::misc_earnapproval is the sanctioned GRIND fast-forward for
        -- exactly this loop (docs/QUEST_SERVER_CHEATS.md) -- it walks the
        -- real ~leif_intercept_wood body in a guarded loop, skipping only
        -- the swing cadence and the maple's regrowth, until 96 of 127
        -- (75%). t.cheat is hollow (trap 12): record the call with t.step,
        -- then read its own line back with t.msg.expect and confirm the
        -- server varbit itself.
        local earn_cheat_result = t.cheat("::misc_earnapproval")
        t.step("approval.fast_forward_cheat", earn_cheat_result == "ok" and "PASS" or "FAIL",
            "::misc_earnapproval -> " .. tostring(earn_cheat_result))
        t.exec("approval.fast_forward", t.msg.expect, "You worked for the kingdom")
        -- Named after Quest Helper's own get75Support step ("Reach 75%
        -- support...") -- this is the row that proves it reached.
        t.exec("get75Support", t.var.await_server, "misc_approval", 96, 10)

        -- ---------------------------------------------------- back to Vargas: the crowning
        t.exec("goto-vargas6", t.player.goto_tile, 2501, 3859, 1)

        -- The coffer reading taken on the tick BEFORE the crowning click, so
        -- the assertion below is a delta this run measured and not a guess
        -- at a starting balance. Read server-side: the coffer varbit is the
        -- kingdom's bookkeeping and nothing transmits it to this client's
        -- varp cache.
        local coffers_before_result, coffers_before = t.var.server("misc_coffers")

        t.exec("talkVargasFinish", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargasFinish-dialog", t.chat.play, {
            "npc:The people trust you, the treaty is signed, and my own children speak well of you. I hereby name you regent of Miscellania!",
        })

        -- vargas_finish_quest writes %misc_quest = ^misc_complete then
        -- queue(misc_quest_complete, 0, 0) -- the varp write and the scroll
        -- paint it queues are not client-visible in the click's own tick
        -- (section 8's completion-is-asynchronous rule).
        t.ticks(3)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))

        -- The 10000gp the scroll's first reward line promises. Same label,
        -- one line above the `%misc_quest = ^misc_complete` the row above
        -- just proved, so a stage of complete and an unmoved coffer would
        -- mean the grant line was skipped.
        local coffers_after_result, coffers_after = t.var.server("misc_coffers")
        t.check("reward.coffers",
            coffers_before_result == "ok" and coffers_after_result == "ok"
                and type(coffers_before) == "number" and type(coffers_after) == "number"
                and coffers_after == coffers_before + 10000,
            "misc_coffers before the crowning click = " .. tostring(coffers_before)
                .. " (" .. tostring(coffers_before_result) .. "), after = "
                .. tostring(coffers_after) .. " (" .. tostring(coffers_after_result)
                .. "), delta = "
                .. tostring((type(coffers_after) == "number" and type(coffers_before) == "number")
                    and (coffers_after - coffers_before) or "n/a")
                .. ", expected 10000 from misc_king_vargas.rs2's "
                .. "[label,vargas_finish_quest] `%misc_coffers = add(%misc_coffers, 10000)`")

        -- The quest's own ~quest_complete_rewards call lists "10000
        -- coins|Management of Miscellania|Ring of wealth teleport to
        -- Miscellania" as SCROLL TEXT only -- nothing in the tree inv_adds
        -- a ring or grants skill xp, so the scroll's own reward lines are
        -- the only real evidence of the other two documented rewards.
        -- t.scroll.rewards() awaits the scroll's own mount itself, so
        -- quest.expect_complete()'s quest.scroll shot below photographs a
        -- scroll already on screen (section 8's "settle before
        -- expect_complete" rule).
        local rewards_result, rewards_detail = t.scroll.rewards()
        local rewards_text = "nil"
        local rewards_ok = false
        if rewards_result == "ok" and type(rewards_detail) == "table" and type(rewards_detail.lines) == "table" then
            rewards_text = table.concat(rewards_detail.lines, " | ")
            -- The scroll wraps the third reward across two lines ("Ring of
            -- wealth teleport to" / "Miscellania"), so match the two whole
            -- lines the wrap leaves intact rather than the joined phrase.
            rewards_ok = rewards_text:find("10000 coins", 1, true) ~= nil
                and rewards_text:find("Management of Miscellania", 1, true) ~= nil
                and rewards_text:find("Ring of wealth teleport to", 1, true) ~= nil
        end
        t.check("scroll.rewardLines", rewards_ok,
            "scroll.rewards() -> " .. tostring(rewards_result) .. " lines=" .. rewards_text)

        t.quest.expect_complete()
        t.finish(0)
    end,
}
