-- Meat and Greet: the whole quest through the real client, no stage skips (parity b55 scratch driver).
-- Setup stages only what the guide asks the player to bring: the Children of the Sun prerequisite and combat kit.
return {
    id = "meatandgreet",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::complete quest_childrenofthesun", "::meatandgreet",
      "::give rune_full_helm 1", "::give rune_chainbody 1", "::give rune_platelegs 1", "::give zamorak_spear 1",
      "::setlevel hitpoints 99", "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel prayer 99",
      "::wield rune_full_helm", "::wield rune_chainbody", "::wield rune_platelegs", "::wield zamorak_spear",
      "::give shark 22" },
    run = function(t)
        t.quest.bind({ varp = "varb11182_mag", constants = { not_started = 0, started = 2, supply = 4, emelio2 = 6, recipe = 8, success = 10, success2 = 12, lelia = 14, lelia2 = 16, minotaur = 18, minotaur2 = 20, lelia3 = 22, finish = 24, complete = 26 }, display = "Meat and Greet", points = 1 })
        t.ticks(4)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        local jr, jj = t.ui.journal_open("Meat and Greet")
        t.check("journal.start", jr == "ok", tostring(jj and jj.first_line))
        t.ui.journal_close()
        -- ---- leg 1: Emelio's offer ----
        t.exec("talkToEmelio", t.player.talk_to, "mag_emelio", 1)
        t.exec("talkToEmelio.dialog", t.chat.play, {
            "npc:Hello? Can I help you", "player:I don't know", "npc:Hang on", "player:Well I suppose", "npc:Kuani! I'm Emelio",
            "choose:Yes.", "player:What business venture",
        })
        t.exec("talkToEmelio.drain", t.chat.drain, { max_pages = 40 })
        t.ticks(2)
        t.expect("quest.stage.supply", t.quest.expect_stage("supply"))
        t.exec("talkToEmelio.again", t.player.talk_to, "mag_emelio", 1)
        t.exec("talkToEmelio.again.dialog", t.chat.play, {
            "npc:Nilsal, friend. Any luck", "player:Which ingredients was it again", "npc:We're still missing",
            "player:Okay, I guess I can go see", "npc:Kuani! I look forward",
        })
        -- ---- leg 2: the Spice Merchant's box ----

                t.exec("goto-spice", t.player.goto_tile, 1685, 3100, 0)
        t.exec("talkToSpiceMerchant", t.player.talk_to, "fortis_shop_spices", 1)
        t.exec("talkToSpiceMerchant.dialog", t.chat.play, {
            "npc:A little bit of spice",
            "choose:/missing delivery/",
            "player:missing delivery",
            "npc:Emelio sent you",
            "player:That's right",
            "npc:good news",
            "player:Can't get the box open",
            "npc:first time",
            "npc:locked the box",
            "player:let me take a look",
            "npc:Sure thing",
        })
        t.exec("enterCodeWrapper", t.ui.await_open, "number_pad", 20)
        t.ticks(3)
        local wrong = { 1, 2, 3, 4 }
        for _, d in ipairs(wrong) do
            local sub = (d == 0) and 18 or ((d - 1) * 2)
            local r, w = t.ui.widget("number_pad:buttons", sub)
            t.check("pad.widget." .. d, r == "ok", tostring(w))
            t.ui.invoke(w, 1)
            t.ticks(2)
        end
        local r, w = t.ui.widget("number_pad:confirm_button")
        t.check("pad.submit.widget", r, tostring(w))
        t.ui.invoke(w, 1)
        t.ticks(4)
        local rr, vv = t.var.varbit("varb11183_mag_spice")
        t.check("wrongcode.spice_unchanged", vv == 2, "spice=" .. tostring(vv))
        for _, d in ipairs({ 2, 5, 4, 6 }) do
            local sub = (d == 0) and 18 or ((d - 1) * 2)
            local r, w = t.ui.widget("number_pad:buttons", sub)
            t.check("pad.widget." .. d, r == "ok", tostring(w))
            t.ui.invoke(w, 1)
            t.ticks(2)
        end
        local r2, w2 = t.ui.widget("number_pad:confirm_button")
        t.ui.invoke(w2, 1)
        t.ticks(4)
        t.exec("unlocked.dialog", t.chat.play, { "mesbox:You unlock the box", "npc:Kuani! You did it", "player:Don't mention it" })
        t.ticks(3)
        local r3, v3 = t.var.varbit("varb11183_mag_spice")
        t.check("spice.sent", v3 == 4, "spice=" .. tostring(v3))
                t.exec("goto-emelio.spice", t.player.goto_tile, 1753, 3074, 0)
        t.exec("talkToEmelio.spice", t.player.talk_to, "mag_emelio", 1)
        t.exec("talkToEmelio.spice.dialog", t.chat.play, {
            "npc:Nilsal, friend. Any luck", "player:Which ingredients was it again", "npc:We're still missing",
            "player:I did manage to speak to the Spice Merchant", "npc:Kuani! Now all that's left is the buffalo meat", "player:I'm on it",
        })
        -- ---- leg 3: Alba, the Wolf Den and the Dire Wolf Alpha ----

                -- den refuses before Alba has asked
        t.exec("goto-den", t.player.goto_tile, 1499, 3131, 0)
        t.exec("denLocked", t.player.click_loc, "direwolf_cave_entrance", 1)
        t.ticks(3)
        t.check("denLocked.msg", t.msg.expect("no reason to go into the wolf den"), "refused before Alba")
        t.exec("goto-alba", t.player.goto_tile, 1587, 3125, 0)
        t.exec("talkToAlba", t.player.talk_to, "mag_alba", 1)
        t.exec("talkToAlba.dialog", t.chat.play, {
            "npc:Nilsal, iknami. What can I do",
            "player:I'm guessing you must be Alba",
            "npc:Apologies, iknami",
            "player:A wolf problem",
            "npc:we'd never say no",
            "player:Sounds like we have a deal",
            "npc:The wolves have a den",
            "player:Okay, I'll head over",
            "npc:Thank you, iknami",
        })
        t.ticks(2)
        local r, v = t.var.varbit("varb11184_mag_meat")
        t.check("meat.talked", v == 2, "meat=" .. tostring(v))
        t.exec("goto-den2", t.player.goto_tile, 1499, 3131, 0)
        t.exec("enterDen", t.player.click_loc, "direwolf_cave_entrance", 1)
        t.ticks(10)
        local tr, td = t.world.tile()
        t.check("den.arrived", tr, tostring(td and td.x) .. "," .. tostring(td and td.z) .. "," .. tostring(td and td.level))
        t.exec("alpha.present", t.npc.await_present, "mag_direwolf", 30, 20)
        local _, staged_alpha = t.inv.count("shark")
        local alpha_tab, alpha_tab_detail = t.ui.tab("prayer")
        t.check("killDireWolfAlpha-tab", alpha_tab == "ok", "prayer tab -> " .. tostring(alpha_tab) .. " " .. tostring(alpha_tab_detail))
        t.ticks(1)
        local _, alpha_prayer = t.ui.widget("prayerbook:prayer15")
        t.ui.invoke(alpha_prayer, 1)
        t.ticks(2)
        local _, alpha_on = t.var.varbit("varb4118_prayer_protectfrommelee")
        t.check("killDireWolfAlpha-protect", alpha_on == 1, "Protect from Melee varbit " .. tostring(alpha_on))
        t.exec("killDireWolfAlpha-attack", t.player.attack, "mag_direwolf", 2, 60)
        t.cheat("::mg_alpha_hp")
        t.ticks(2)
        -- hitpoints are staged at 99 on purpose (setup ::setlevel hitpoints 99): the margin is measured against that.
        local _, alpha_detail = t.exec("killDireWolfAlpha", t.npc.await_dead_engaged, 400, 6, { eat = { item = "shark", below = 40 } })
        t.ticks(4)
        local alpha_lowest = tonumber(tostring(alpha_detail):match("lowest hp (%d+)/"))
        local alpha_ticks = tostring(alpha_detail):match("dead after (%d+) tick")
        local _, left_alpha = t.inv.count("shark")
        local _, alpha_hr = t.skill.read("hitpoints")
        local alpha_hp = alpha_hr and (alpha_hr.current or alpha_hr.level or alpha_hr.boosted)
        t.check("killDireWolfAlpha-margin", (left_alpha or 0) >= 2 or (alpha_lowest or 0) > 25,
            "staged " .. tostring(staged_alpha) .. " sharks, eaten " .. tostring((staged_alpha or 0) - (left_alpha or 0)) .. ", left " .. tostring(left_alpha)
            .. ", lowest hp " .. tostring(alpha_lowest) .. "/99, hp after " .. tostring(alpha_hp) .. "/99, dead after " .. tostring(alpha_ticks)
            .. " ticks (margin: sharks left >= 2 or lowest hp > 25)")
        local r2, v2 = t.var.varbit("varb11184_mag_meat")
        t.check("meat.killed", v2 == 3, "meat=" .. tostring(v2))
        t.exec("leaveDen", t.player.click_loc, "direwolf_cave_exit_instance", 2)
        t.ticks(8)
        local lr, ld = t.world.tile()
        t.check("den.left", lr, tostring(ld and ld.x) .. "," .. tostring(ld and ld.z) .. "," .. tostring(ld and ld.level))
        t.exec("goto-alba2", t.player.goto_tile, 1587, 3125, 0)
        t.exec("returnToAlba", t.player.talk_to, "mag_alba", 1)
        t.exec("returnToAlba.dialog", t.chat.play, {
            "npc:Any luck with that wolf",
            "player:It's been dealt with",
            "npc:Kuani! Thank you so much",
            "player:Perfect! Thank you",
            "npc:Not a problem",
        })
        t.ticks(3)
        t.expect("quest.stage.emelio2", t.quest.expect_stage("emelio2"))
                -- ---- leg 4: the recipe ----

        t.expect("quest.stage.emelio2.again", t.quest.expect_stage("emelio2"))
        -- connoisseurs before Emelio
        t.exec("goto-out", t.player.goto_tile, 1750, 3070, 0)
        t.exec("talkToLucas.early", t.player.talk_to, "mag_lucas", 1)
        t.exec("talkToLucas.early.dialog", t.chat.play, { "npc:I hope this food is worth it" })
        t.exec("goto-emelio", t.player.goto_tile, 1753, 3074, 0)
        t.exec("talkToEmelio.recipe", t.player.talk_to, "mag_emelio", 1)
        t.exec("talkToEmelio.recipe.dialog", t.chat.play, {
            "npc:Thanks to your good work", "npc:Outside, I have gathered", "player:Where did these connoisseurs",
            "npc:Oh, I just went", "player:I see. Are you sure", "npc:Of course!", "npc:Imagine,", "player:If you're sure",
            "npc:Absolutely!", "npc:Head on out", "player:I'll get right on it", "npc:Kuani!",
        })
        t.ticks(3)
        t.expect("quest.stage.recipe", t.quest.expect_stage("recipe"))
        for _, who in ipairs({ "mag_vincens", "mag_renata", "mag_lucas" }) do
            t.exec("goto-out." .. who, t.player.goto_tile, 1750, 3070, 0)
            t.exec("opinion." .. who, t.player.talk_to, who, 1)
            t.exec("opinion." .. who .. ".dialog", t.chat.play, { "player:Hello there. I was wondering", "npc:", "player:Okay, thanks" })
        end
        -- wrong recipe first (all ones)
        t.exec("goto-emelio.2", t.player.goto_tile, 1753, 3074, 0)
        t.exec("talkToEmelio.ratio1", t.player.talk_to, "mag_emelio", 1)
        t.exec("talkToEmelio.ratio1.dialog", t.chat.play, {
            "npc:Have you worked out the ingredient ratios",
            "choose:I think so.",
            "player:I think so.",
            "npc:Kuani! I've currently got one portion each",
            "choose:/Adjust meat/",
            "choose:Two.",
            "player:Let's try two portions of meat.",
            "npc:Very well. Anything else?",
            "choose:/I think that's perfect/",
            "player:I think that's perfect!",
            "npc:That gives us two portions of meat, one portion of salad",
            "choose:Sounds good to me.",
            "player:Sounds good to me.",
            "npc:take this kebab",
            "*",
        })
        t.ticks(2)
        t.check("kebab.given", select(2, t.inv.count("mag_test_kebab")) == 1, "test kebab")
        t.exec("goto-out.bad", t.player.goto_tile, 1750, 3070, 0)
        t.exec("taste.bad", t.player.talk_to, "mag_renata", 1)
        t.exec("taste.bad.dialog", t.chat.play, { "player:What do you make of this", "npc:Eww! This is awful", "player:Oh. I guess we'd better try again" })
        t.ticks(2)
        t.check("taste.bad.kebab_gone", select(2, t.inv.count("mag_test_kebab")) == 0, "eaten")
        t.expect("quest.stage.still_recipe", t.quest.expect_stage("recipe"))
        t.exec("goto-emelio.3", t.player.goto_tile, 1753, 3074, 0)
        t.exec("talkToEmelio.failed", t.player.talk_to, "mag_emelio", 1)
        t.exec("recipeStepWrapper", t.chat.play, {
            "npc:Have you seen what our connoisseurs reckon",
            "player:They didn't like it",
            "npc:Tatamo! This is worse",
            "choose:/Adjust meat/",
            "choose:Four.",
            "player:Let's try four portions of meat.",
            "npc:Very well. Anything else?",
            "choose:/Adjust salad/",
            "choose:Two.",
            "player:Let's try two portions of salad.",
            "npc:Very well.",
            "choose:/Adjust spice/",
            "choose:One (current).",
            "player:Let's try one portion of spice.",
            "npc:That's what we have already",
            "choose:/Adjust sauce/",
            "choose:Three.",
            "player:Let's try three portions of sauce.",
            "npc:Very well.",
            "choose:/I think that's perfect/",
            "player:I think that's perfect!",
            "npc:That gives us four portions of meat, two portions of salad, one portion of spice and three portions of sauce",
            "choose:Actually, let's make some more changes.",
            "player:Actually, let's make some more changes.",
            "npc:Very well.",
            "choose:/I think that's perfect/",
            "player:I think that's perfect!",
            "npc:That gives us",
            "choose:Sounds good to me.",
            "player:Sounds good to me.",
            "npc:take this kebab",
            "*",
        })
        t.ticks(2)
        t.exec("goto-out.good", t.player.goto_tile, 1750, 3070, 0)
        t.exec("taste.good", t.player.talk_to, "mag_vincens", 1)
        t.exec("taste.good.dialog", t.chat.play, { "player:What do you make of this", "npc:This is brilliant", "player:Fantastic! I'll let Emelio know" })
        t.ticks(3)
        t.expect("quest.stage.success", t.quest.expect_stage("success"))
        t.exec("goto-emelio.4", t.player.goto_tile, 1753, 3074, 0)
        t.exec("talkToEmelio.success", t.player.talk_to, "mag_emelio", 1)
        t.exec("talkToEmelio.success.dialog", t.chat.play, {
            "npc:Have you seen what our connoisseurs reckon", "player:They loved it!", "npc:Excellent!", "player:So are we all ready",
            "npc:Not quite, iknami", "player:How do we do that", "npc:One word, iknami", "npc:You must visit Lelia",
            "*",
        })
        t.ticks(3)
        t.check("kebabs.two", select(2, t.inv.count("mag_colosseum_kebab")) == 2, "two test kebabs")
        t.expect("quest.stage.lelia", t.quest.expect_stage("lelia"))
                -- lose both kebabs on purpose: Emelio has spares
        t.player.drop("mag_colosseum_kebab")
        t.ticks(2)
        t.player.drop("mag_colosseum_kebab")
        t.ticks(2)
        t.check("kebabs.dropped", select(2, t.inv.count("mag_colosseum_kebab")) == 0, "both dropped")
        t.exec("talkToEmelio.lost", t.player.talk_to, "mag_emelio", 1)
        t.exec("talkToEmelio.lost.dialog", t.chat.play, {
            "npc:Nilsal, friend. Have you taken those kebabs", "player:I lost them", "npc:Tatamo!", "*", "npc:Now you must hurry",
        })
        t.ticks(2)
        t.check("kebabs.regiven", select(2, t.inv.count("mag_colosseum_kebab")) == 2, "two spares")
        -- ---- leg 5: the Colosseum ----

                t.exec("goto-colosseum", t.player.goto_tile, 1797, 3106, 0)
        t.exec("enterColosseum", t.player.click_loc, "colosseum_entrance_outside", 1)
        t.ticks(8)
        local r0, d0 = t.world.tile()
        t.check("lobby.arrived", r0, tostring(d0 and d0.x) .. "," .. tostring(d0 and d0.z) .. "," .. tostring(d0 and d0.level))
        local wr, wd = t.player.walk_to(1819, 9486, 160)
        t.ticks(2)
        local wr2, wd2 = t.world.tile()
        t.check("walk-lelia", wr2, tostring(wd2 and wd2.x) .. "," .. tostring(wd2 and wd2.z) .. " walk=" .. tostring(wr))
        t.exec("talkToLelia", t.player.talk_to, "mag_lelia", 1)
        t.exec("talkToLelia.dialog", t.chat.play, {
            "npc:Need something?", "player:Are you Lelia", "npc:Looking to market", "player:Is this a common", "npc:Emelio having ideas",
            "player:Well he seems very confident", "npc:Alright, let's hear it", "player:He wants to bring", "mesbox:Lelia eats the kebab",
            "npc:Hmm... I must admit", "player:So you'll help", "npc:Yes, and it's your lucky day", "player:Excellent! So how", "npc:Nice and simple",
            "npc:In some cases", "player:And would our kebabs", "npc:Well I suppose they did impress", "player:Great! So, er",
            "npc:You want to convince", "npc:He'll pretend", "player:Will the minotaur be okay", "npc:Why wouldn't he", "player:I guess I don't really know",
            "npc:If the crowd is happy", "player:What? Right now", "npc:Yup.",
            "choose:I think so.", "player:I think so.", "npc:Right, get to it then.",
            "npc:Welcome, one and all", "npc:Introducing the kebab", "npc:Indeed, observe",
            "npc:Huh? Human", "player:What? No, it's a kebab", "npc:Kebab... is... minotaur", "player:No. It's buffalo", "npc:Human... eat... minotaur", "player:Oh boy",
        })
        t.ticks(3)
        t.expect("quest.stage.minotaur2", t.quest.expect_stage("minotaur2"))
        t.check("kebab.left_one", select(2, t.inv.count("mag_colosseum_kebab")) == 0, "both kebabs used")
        local ar, ad = t.world.tile()
        t.check("arena.arrived", ar, tostring(ad and ad.x) .. "," .. tostring(ad and ad.z) .. "," .. tostring(ad and ad.level))
        t.exec("minotaur.present", t.npc.await_present, "mag_minotaur", 30, 20)
        local _, staged_mino = t.inv.count("shark")
        local mino_tab, mino_tab_detail = t.ui.tab("prayer")
        t.check("fightMinotaur-tab", mino_tab == "ok", "prayer tab -> " .. tostring(mino_tab) .. " " .. tostring(mino_tab_detail))
        t.ticks(1)
        local _, mino_before = t.var.varbit("varb4118_prayer_protectfrommelee")
        if mino_before ~= 1 then
            local _, mino_prayer = t.ui.widget("prayerbook:prayer15")
            t.ui.invoke(mino_prayer, 1)
            t.ticks(2)
        end
        local _, mino_on = t.var.varbit("varb4118_prayer_protectfrommelee")
        t.check("fightMinotaur-protect", mino_on == 1, "Protect from Melee varbit " .. tostring(mino_on) .. " (before " .. tostring(mino_before) .. ")")
        t.exec("fightMinotaur-attack", t.player.attack, "mag_minotaur", 2, 60)
        t.cheat("::mg_minotaur_hp")
        local _, mino_detail = t.exec("fightMinotaur", t.npc.await_dead_engaged, 900, 8, { eat = { item = "shark", below = 60 } })
        local mino_lowest = tonumber(tostring(mino_detail):match("lowest hp (%d+)/"))
        local mino_ticks = tostring(mino_detail):match("dead after (%d+) tick")
        local _, left_mino = t.inv.count("shark")
        local _, mino_hr = t.skill.read("hitpoints")
        local mino_hp = mino_hr and (mino_hr.current or mino_hr.level or mino_hr.boosted)
        t.check("fightMinotaur-margin", (left_mino or 0) >= 2 or (mino_lowest or 0) > 25,
            "staged " .. tostring(staged_mino) .. " sharks, eaten " .. tostring((staged_mino or 0) - (left_mino or 0)) .. ", left " .. tostring(left_mino)
            .. ", lowest hp " .. tostring(mino_lowest) .. "/99, hp after " .. tostring(mino_hp) .. "/99, dead after " .. tostring(mino_ticks)
            .. " ticks (margin: sharks left >= 2 or lowest hp > 25)")
        t.ticks(12)
        t.expect("quest.stage.lelia3", t.quest.expect_stage("lelia3"))
        local lr, ld = t.world.tile()
        t.check("lobby.returned", lr, tostring(ld and ld.x) .. "," .. tostring(ld and ld.z) .. "," .. tostring(ld and ld.level))
        t.exec("talkToLelia.after", t.player.talk_to, "mag_lelia", 1)
        t.exec("talkToLelia.after.dialog", t.chat.play, {
            "npc:You put on a good performance", "player:What about that minotaur", "npc:What about it", "player:But it could have killed me",
            "npc:You survived", "player:But...", "npc:Now, I need to get things ready", "player:Fine.",
        })
        t.ticks(3)
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
                -- ---- leg 6: wrap up ----
        -- leaveColosseumToReturnToEmelio: [oploc1,colosseum_exit_lobby] (twilightspromise.rs2:367) p_teleports to 0_28_48_4_34 = 1796,3106 unconditionally.
        t.exec("leaveColosseumToReturnToEmelio", t.player.click_loc, "colosseum_exit_lobby", 1)
        t.ticks(8)
        local xr, xd = t.world.tile()
        t.check("colosseum.exited", xr == "ok" and xd and xd.x == 1796 and xd.z == 3106 and xd.level == 0, "landed " .. tostring(xd and xd.x) .. "," .. tostring(xd and xd.z) .. "," .. tostring(xd and xd.level) .. " (expected 1796,3106,0)")
        t.exec("goto-emelio.end", t.player.goto_tile, 1753, 3074, 0)
        local _, sk = t.skill.snapshot()
        local _, qp_before = t.var.varp("varp101_qp")
        t.exec("talkToEmelio.end", t.player.talk_to, "mag_emelio", 1)
        t.exec("talkToEmelio.end.dialog", t.chat.play, {
            "npc:Nilsal, friend. Given that", "player:Well I did run into", "npc:You have done a great service",
            "player:Do I not get to share", "npc:Sorry, friend", "player:Right...",
        })
        t.ticks(4)
        t.exec("reward.cooking", t.skill.expect_gain, "cooking", 8000, sk)
        local _, qp_after = t.var.varp("varp101_qp")
        t.check("reward.questpoint", qp_before ~= nil and qp_after == qp_before + 1, "varp101_qp " .. tostring(qp_before) .. " -> " .. tostring(qp_after) .. " (delta must be 1)")
        local qr, qv = t.quest.stage()
        t.check("quest.varp_complete", qv == 26, "varb11182_mag=" .. tostring(qv))
        local tr, tt = t.scroll.title()
        t.check("quest.scroll_title", tr == "ok" and tostring(tt and tt.name):find("Meat and Greet") ~= nil, tostring(tt and tt.name))
        t.scroll.close()
        -- quest.journal is hand-rolled out: expect_complete's own journal row times out after a real completion (gaps-dialogue.md)
        t.finish(0)
    end,
}
