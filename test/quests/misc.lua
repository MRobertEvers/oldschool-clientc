-- Throne of Miscellania (1 QP). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_misc/scripts/
--   misc_king_vargas.rs2, misc_princess_astrid.rs2, misc_prince_brand.rs2,
--   misc_queen_sigrid.rs2, misc_advisor_ghrim.rs2, misc_giant_nib.rs2,
--   misc_courting_emotes.rs2, misc_debug.rs2
-- and areas/area_miscellania/scripts/{flower_girl,derrik,lumberjack_leif}.rs2
-- (Derrik's opnpc1 delegates to misc_smithy.rs2's
-- [label,misc_smithy_giant_nib] at exactly %misc_quest = ^misc_gave_king_treaty).
--
-- Access: misc_door_guard.rs2 hard-gates the throne room on %heroquest =
-- ^hero_complete (Heroes' Quest, unported -- ::complete quest_heroes in
-- setup). goto_tile teleports straight past the door/guard the same way it
-- climbs stairs elsewhere in this pack (QUEST_AUTHORING.md section 2) --
-- Vargas/Brand/Ghrim (all level 1) and the flower girl/Derrik/Leif (level
-- 0) are each reached by a single goto_tile at their own *.spawn tile, no
-- click_loc on any of the castle's spiralstairs.
--
-- Courting partner: Prince Brand (%misc_partner_multivar = 1). Brand also
-- writes the anthem later regardless of who is courted (his own file
-- header), so courting him means only one npc's affection ladder to climb
-- instead of two -- Astrid's Dance leg is never reached from this path and
-- is not driven here.
--
-- Content-parity fix 2026-09-23 (docs/QUEST_HELPER_COVERAGE_2026-09-23.md):
-- two things this port used to soft-skip are real now.
--   (1) King Vargas's own [opnpc1] courting choice now sets %misc_affection
--       = ^misc_affection_not_started (not step0), so brand_talk1 (the
--       five-line courtship intro, "Be still, my heart...") fires on the
--       FIRST live visit as Quest Helper's own talkBrand1 step expects.
--   (2) misc_courting_emotes.rs2 hooks ~emote_perform: the Clap leg (after
--       giving flowers, %misc_affection = s1_step1 -> s1_step5) and the
--       Blow Kiss leg (after talkBrand3's "Truly?", s3_step4 -> s3_step0)
--       both require the player to actually PLAY the emote next to Brand
--       (within ^misc_courting_emote_radius of ^misc_brand_coord) -- giving
--       the item alone no longer narrates it. Quest Helper's courtBrand
--       ladder names both as their own steps (clapForBrand, blowKissToBrand).
--
-- Items: flowers (misc_flowergirl, 15gp, bought live below) are the only
-- courting/anthem/pen ingredient actually sold in the quest area itself;
-- the cake, ring, iron bar and logs are bring-along materials the way
-- Advisor Ghrim's own rake/pickaxe/axe/harpoon/lobster-pot "reputation
-- item" already is (misc_advisor_ghrim.rs2's own dialogue: "Bring a rake, a
-- pickaxe..."), so they are given in setup rather than driven through a
-- shop trip this content pack has no wired source for.
--
-- 75%-support finish gate: content-parity fix 2026-09-23 landed the real
-- Managing Miscellania resource-collection loop (lumberjack_leif.rs2's
-- leif_intercept_wood, ported from LostCity like weed_herbs.rs2/
-- miner_magnus.rs2/fisherman_frodi.rs2 already were) -- Advisor Ghrim's own
-- "put me to work" no longer sets %misc_approval straight to threshold, it
-- only narrates ("Rake the herb patches ... work near any of them and the
-- kingdom's stock grows, and so does your approval.") and Quest Helper's
-- own guide (ThroneOfMiscellania.java's finishOff/get75Support step, stage
-- 90) has NO talk-to-Ghrim leg at this point at all -- it goes straight
-- from king_signed_treaty to the chop/mine/rake/fish activity, then to King
-- Vargas. This file follows the guide: no Ghrim visit here. The player
-- chops the kingdom's maples for real (click_loc("mapletree") beside
-- Lumberjack Leif, Woodcutting 45 + an axe, both set up below) until
-- %misc_approval is demonstrably moving, then ::misc_earnapproval -- the
-- sanctioned GRIND fast-forward for this exact loop (docs/QUEST_SERVER_CHEATS.md)
-- -- finishes it to the 75% threshold (96 of 127); its own guard requires
-- the same real preconditions (stage, inzone, level, axe) the chop already
-- proved, and its effect is read back (t.msg.expect + var.await_server).
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
-- other and the grant is asserted below (reward.coffers: the reading taken
-- immediately before the crowning click, plus exactly 10000). The other two
-- scroll lines really are text only -- nothing in the quest_misc tree
-- inv_adds a ring or grants skill xp, and the scaffold's Quest Helper banner
-- agrees (0 experience, 0 item, quest points 1) -- so those two are graded by
-- the scroll's own literal lines (scroll.rewardLines) and the 1 quest point
-- quest.expect_complete()'s quest.points row already checks.

return {
    id = "misc",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so everything below fits
        "::give coins 20", -- misc_flowergirl wants 15gp for three flowers
        "::give iron_bar 1", -- Derrik forges the giant nib from this (misc_smithy.rs2)
        "::give logs 1", -- combined with the nib to make the giant pen (misc_giant_nib.rs2)
        "::give gold_ring 1", -- Brand's third courting gift (misc_prince_brand.rs2's opnpcu ring case)
        "::give cake 1", -- Brand's second courting gift (opnpcu cake case)
        "::setlevel woodcutting 45", -- the maple row's own level gate (woodcutting_trees), also Ghrim's reputation-tool level for the real support grind
        "::give bronze_axe 1", -- Advisor Ghrim's reputation item AND the real axe ::misc_earnapproval's ~woodcutting_axe_checker needs
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
            "choose:I'll try to win over Prince Brand.",
            "player:I'll try to win over Prince Br",
            "npc:Brand can usually be found ups",
        })

        -- The choice above ("I'll try to win over Prince Brand.") is
        -- mutually exclusive with the Astrid branch (misc_king_vargas.rs2's
        -- own opt=1/opt=2 [opnpc1,misc_king_vargas] choice, %misc_partner_multivar
        -- 1 vs 0) -- Quest Helper's courtAstrid ladder can never run in the
        -- same playthrough as courtBrand, so its eight steps are declared
        -- here rather than driven.
        -- GUIDE-GAP: talkAstrid1 not driven -- Brand was courted instead, the mutually exclusive choice (misc_princess_astrid.rs2:46)
        -- GUIDE-GAP: giveFlowersToAstrid not driven -- Brand was courted instead, the mutually exclusive choice (misc_princess_astrid.rs2:103)
        -- GUIDE-GAP: danceForAstrid not driven -- Brand was courted instead, the mutually exclusive choice (misc_courting_emotes.rs2:50)
        -- GUIDE-GAP: talkAstrid2 not driven -- Brand was courted instead, the mutually exclusive choice (misc_princess_astrid.rs2:64)
        -- GUIDE-GAP: giveBowToAstrid not driven -- Brand was courted instead, the mutually exclusive choice (misc_princess_astrid.rs2:114)
        -- GUIDE-GAP: talkAstrid3 not driven -- Brand was courted instead, the mutually exclusive choice (misc_princess_astrid.rs2:77)
        -- GUIDE-GAP: blowKissToAstrid not driven -- Brand was courted instead, the mutually exclusive choice (misc_courting_emotes.rs2:57)
        -- GUIDE-GAP: useRingOnAstrid not driven -- Brand was courted instead, the mutually exclusive choice (misc_princess_astrid.rs2:122)

        -- ---------------------------------------------------- courting Brand
        t.exec("goto-brand1", t.player.goto_tile, 2502, 3852, 1)
        -- content-parity fix: Vargas now sets %misc_affection to
        -- not_started (not step0), so this FIRST visit reads brand_talk1,
        -- the five-line courtship intro (Quest Helper's own talkBrand1 step).
        t.exec("talkBrand1", t.player.talk_to, "misc_prince_brand", 1)
        t.exec("talkBrand1-dialog", t.chat.play, {
            "player:Be still, my heart -- that's quite",
            "npc:You will be the greatest patron",
            "player:How poetic.",
            "npc:They don't understand your poetry",
            "npc:It's kind of you to listen. If you",
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

        t.exec("goto-brand2", t.player.goto_tile, 2502, 3852, 1)
        -- misc_prince_brand.rs2's opnpcu flowers case ends in a plain mes()
        -- line (a chat-log message, not a ~mesbox), so use_on's own settle
        -- (new chat line / backpack change) is the whole of the row -- it
        -- only gets Brand to ASK for the Clap now, it does not perform it.
        local brand = t.player.by_symbol("npc", "misc_prince_brand")
        t.exec("giveFlowersBrand", t.player.use_on, "flowers_waterfall_quest", brand)
        -- inv.await(name, 0, ticks) never actually waits (total >= 0 is
        -- always true -- QUEST_AUTHORING.md's gaps section) -- ticks then a
        -- direct count read is the real "wait for it to be consumed".
        t.ticks(2)
        local flowers_gone_result, flowers_gone_count = t.inv.count("flowers_waterfall_quest")
        t.check("giveFlowersBrand.consumed", flowers_gone_result == "ok" and flowers_gone_count == 0,
            "inv.count(flowers_waterfall_quest) -> " .. tostring(flowers_gone_result) .. " " .. tostring(flowers_gone_count))

        -- Content-parity leg: brand_need_clap -> the player must actually
        -- play Clap next to Brand (misc_courting_emotes.rs2's
        -- ~misc_emote_performed_brand, hooked off ~emote_perform), s1_step1
        -- (11) -> s1_step5 (15). Quest Helper's own clapForBrand step.
        t.exec("clapForBrand", t.player.emote, "clap")
        t.expect("affection.s1_step5", t.var.expect("misc_affection", 15))

        brand = t.player.by_symbol("npc", "misc_prince_brand")
        t.exec("talkBrand2", t.player.talk_to, "misc_prince_brand", 1)
        t.exec("talkBrand2-dialog", t.chat.play, {
            "player:A much nobler pursuit than swordplay",
            "npc:How inspiring to hear you say so!",
            "player:How poetic.",
            "npc:I'm glad someone appreciates it. Have you brought",
        })

        brand = t.player.by_symbol("npc", "misc_prince_brand")
        t.exec("giveCakeBrand", t.player.use_on, "cake", brand)
        t.ticks(2)
        local cake_gone_result, cake_gone_count = t.inv.count("cake")
        t.check("giveCakeBrand.consumed", cake_gone_result == "ok" and cake_gone_count == 0,
            "inv.count(cake) -> " .. tostring(cake_gone_result) .. " " .. tostring(cake_gone_count))

        -- brand_talk3 now ENDS at "Truly?" (s2_step4 -> s3_step4) -- the
        -- kiss itself is the emote leg below, not narrated inline any more.
        t.exec("talkBrand3", t.player.talk_to, "misc_prince_brand", 1)
        t.exec("talkBrand3-dialog", t.chat.play, {
            "player:I'm glad to hear it's going so well.",
            "npc:I wouldn't presume to have the skill",
            "player:That was lovely. I'm touched!",
            "npc:Truly?",
        })

        -- Content-parity leg: brand_need_kiss -> the player must actually
        -- play Blow Kiss next to Brand, s3_step4 -> s3_step0 (30). Named
        -- after Quest Helper's own blowKissToBrand step (helper_coverage.py
        -- matches a ledger row to a guide step by name).
        t.exec("blowKissToBrand", t.player.emote, "blow kiss")
        t.expect("affection.s3_step0", t.var.expect("misc_affection", 30))

        brand = t.player.by_symbol("npc", "misc_prince_brand")
        t.exec("giveRingBrand", t.player.use_on, "gold_ring", brand)
        t.exec("giveRingBrand-dialog", t.chat.play, {
            "player:Prince Brand, will you vouch for me",
            "npc:I will -- and gladly.",
        })
        -- misc_acceptedtorule is the varBIT the ring case sets (opnpcu
        -- misc_prince_brand, gold_ring branch) -- proven live by the very
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
        t.exec("goto-brand3", t.player.goto_tile, 2502, 3852, 1)
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
        -- "put me to work" reply is narration only now (content-parity fix
        -- 2026-09-23) and sets nothing, so a visit here would be a driven
        -- row for a step the guide does not have -- skipped, following the
        -- guide.
        --
        -- Real leg: chop the kingdom's maples for real beside Lumberjack
        -- Leif (misc_dummy_mapletree is a non-choppable decoy -- the real
        -- placed trees are the ordinary "mapletree" loc, category 222,
        -- resolved by db_find(woodcutting_trees:tree, mapletree) in both
        -- the real woodcut.rs2 chop and ::misc_earnapproval's own loop).
        -- Woodcutting 45 + an axe are set up above; every successful swing
        -- inside the kingdom zone runs the real leif_intercept_wood
        -- (misc_approval +1, real Woodcutting xp, the log to Leif instead
        -- of the backpack) -- click_loc starts the real repeating chop
        -- action (oploc1 -> oploc3 resume, woodcut.rs2), and the approval
        -- read proves it actually landed before the grind cheat is called.
        t.exec("goto-leif", t.player.goto_tile, 2550, 3866, 0)
        -- Level 45 / bronze axe measures ~6% success per swing (4-tick
        -- cadence, misc_debug.rs2's own loop: 1477-1520 swings for 96
        -- successes across two runs). A single click_loc starts a
        -- repeating chop action (oploc1 -> oploc3 resume, woodcut.rs2)
        -- that keeps swinging on its own -- but get_logs rolls a 1-in-8
        -- deplete chance on EVERY successful swing, and a deplete stops
        -- the resume outright (loc_change to the stump stage, no further
        -- p_oploc(3) is issued): after the npc wander parity landing moved
        -- the shared RNG stream (seam15, 2026-09-25), the very tree being
        -- chopped can now fall before three approval points land -- a
        -- single flat 900-tick await read 2 of 3 and then sat idle for the
        -- rest of the wait, because the action had stopped, not slowed.
        -- A longer wait cannot fix a stopped action, so re-find and
        -- re-click "mapletree" (the symbol resolves to whichever live
        -- copy is nearest -- Leif's grove holds a dozen within one map
        -- square, m39_60.jl2, so a depleted tree is never the only one
        -- left) each time the current session ends short of the target,
        -- awaiting one more approval point than is already banked.
        local chop_attempts = 0
        local approval_result, approval_value = t.var.server("misc_approval")
        while (approval_result ~= "ok" or (approval_value or 0) < 3) and chop_attempts < 10 do
            chop_attempts = chop_attempts + 1
            local before_result, before_value = t.var.server("misc_approval")
            t.exec("chopMaple-" .. chop_attempts, t.player.click_loc, "mapletree", 1)
            t.var.await_server("misc_approval", (before_value or 0) + 1, 250)
            approval_result, approval_value = t.var.server("misc_approval")
        end
        t.check("approval.real_chops", approval_result == "ok" and (approval_value or 0) >= 3,
            "var.server(misc_approval) after " .. tostring(chop_attempts) .. " chopMaple attempt(s) -> "
                .. tostring(approval_result) .. " " .. tostring(approval_value))

        -- ::misc_earnapproval is the sanctioned GRIND fast-forward for
        -- exactly this loop (docs/QUEST_SERVER_CHEATS.md) -- it walks the
        -- real ~leif_intercept_wood body (real stat_random rolls, real
        -- Woodcutting xp, real approval +1 per success) in a guarded loop,
        -- skipping only the swing cadence and the maple's regrowth, until
        -- 96 of 127 (75%). t.cheat is hollow (trap 12): record the call with
        -- t.step, then read its own line back with t.msg.expect (not
        -- t.msg.await -- t.cheat has already consumed the reply, per the
        -- cheat doc's own note) and confirm the server varbit itself.
        local earn_cheat_result = t.cheat("::misc_earnapproval")
        t.step("approval.fast_forward_cheat", earn_cheat_result == "ok" and "PASS" or "FAIL",
            "::misc_earnapproval -> " .. tostring(earn_cheat_result))
        t.exec("approval.fast_forward", t.msg.expect, "You worked for the kingdom")
        -- Named after Quest Helper's own get75Support step ("Reach 75%
        -- support...") -- this is the row that proves it reached.
        t.exec("get75Support", t.var.await_server, "misc_approval", 96, 10)

        -- ---------------------------------------------------- back to Vargas: the crowning
        -- vargas_check_support reads %misc_approval >= 75% (just reached
        -- above) and falls straight through to vargas_finish_quest in the
        -- same click (no return between the labels) -- one dialogue, one row.
        t.exec("goto-vargas6", t.player.goto_tile, 2501, 3859, 1)

        -- The coffer reading taken on the tick BEFORE the crowning click, so
        -- the assertion below is a delta this run measured and not a guess at
        -- a starting balance (a fresh character's %misc_coffers is 0, but the
        -- delta is what vargas_finish_quest's own `add(%misc_coffers, 10000)`
        -- is worth). Read server-side: the coffer varbit is the kingdom's
        -- bookkeeping and nothing transmits it to this client's varp cache.
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
        -- Miscellania" as SCROLL TEXT only -- misc_king_vargas.rs2's
        -- vargas_finish_quest adds to %misc_coffers (the KINGDOM's, not the
        -- player's) and nothing else in the tree inv_adds or grants xp, so
        -- the scroll's own reward lines are the only real evidence of this
        -- quest's documented reward (file header above). t.scroll.rewards()
        -- awaits the scroll's own mount itself, so quest.expect_complete()'s
        -- quest.scroll shot below photographs a scroll already on screen
        -- (section 8's "settle before expect_complete" rule).
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
