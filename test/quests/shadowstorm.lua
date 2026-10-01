-- Shadow of the Storm (shadowstorm). Tier 2, 340c190aae parity2c "partial".
-- Prereqs (The Golem, Demon Slayer) via ::complete -- rule (e), never ::setvar
-- the quest's own varp. Setup gives only what the wiki's Requirements list
-- brings along (a silver bar, three pieces of black clothing, combat stats
-- and food for a level 100 demon). Every Quest Helper step is driven for
-- real, from Father Reen to the completion scroll: Evil Dave's clothing
-- check and escort, the first ritual in Denath's order, the exit portal to
-- the ruin (seam26), Tanya's and Eric's sigils, Badden, Reen, the golem's
-- strange implement, the second ritual in the tome's order at QH's
-- secondCircleSpot (seam26), and Agrith-Naar fought with the dyed
-- Silverlight worn.
return {
    id = "shadowstorm",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_golem",
        "::complete quest_demonslayer",
        -- seam24 copy: the wiki's own requirements (Crafting 30, a silver bar).
        "::setlevel crafting 30",
        "::give silver_bar",
        -- seam26: the wiki's other requirements. "Any black outfit (minimum of
        -- 3 pieces)" -- the priest gown/robe from Thessalia and a black cape
        -- (wiki oldid 15354765 "Starting Out"; Quest Helper darkItems) -- and
        -- "the ability to defeat a level 100 demon" (combat stats + food).
        "::give priest_gown",
        "::give priest_robe",
        "::give black_cape",
        "::setlevel attack 80",
        "::setlevel strength 80",
        "::setlevel defence 80",
        "::setlevel hitpoints 80",
        "::give shark 10",
    },

    run = function(t)
        local bind_r, bind_d = t.quest.bind({
            varp = "agrith_quest",
            constants = {
                not_started = 0,
                see_badden = 10,
                infiltrate = 20,
                denath = 30,
                sigil_tasks = 40,
                matthew = 50,
                golem_ask = 60,
                ritual = 70,
                ritual_done = 90,
                recruit = 100,
                summon = 110,
                fight = 120,
                unequip = 124,
                complete = 125,
            },
            row = "quest_shadowofthestorm",
            display = "Shadow of the Storm",
            points = 1,
        })
        t.step("quest.bind", bind_r == "ok" and "PASS" or "FAIL", bind_d)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---- Father Reen, south of the Al Kharid bank ----
        -- shadowstorm.rs2 [opnpc1,agrith_reen_alkharid] @sots_reen_talk;
        -- live spawn is the _alkharid variant (areas/world/configs/m51_49.spawn).
        t.exec("goto-reen", t.player.goto_tile, 3271, 3159, 0)
        t.exec("talkToReen", t.player.talk_to, "agrith_reen_alkharid")
        t.exec("talkToReen-dialog", t.chat.play, {
            "npc:urgent job for you",
            "npc:Thank Saradomin",
            "npc:recognise this sword",
            "player:Silverlight! That's the sword",
            "mesbox:Father Reen gives you Silverlight",
            "npc:only with Silverlight",
            "npc:dark wizard Denath",
            "npc:moved to Uzer",
            "player:you want me to stop him",
            "npc:kill him once and for all",
            "npc:not an ordinary demon",
            "npc:rid the world of his evil influence",
            "npc:find Badden at once",
            "end",
        })
        t.expect("quest.stage.see_badden", t.quest.expect_stage("see_badden"))
        t.expect("has.silverlight", t.inv.await("silverlight", 1, 5))

        -- ---- Father Badden, Uzer ruins surface ----
        -- shadowstorm.rs2 [opnpc1,agrith_badden_uzer] @sots_badden_talk;
        t.exec("goto-badden", t.player.goto_tile, 3486, 3090, 0)
        t.exec("talkToBadden", t.player.talk_to, "agrith_badden_uzer")
        t.exec("talkToBadden-dialog", t.chat.play, {
            "npc:godsforsaken desert",
            "choose:Reen sent me.",
            "player:Reen sent me.",
            "npc:you have Silverlight",
            "choose:So what do you want me to do?",
            "player:So what do you want me to do?",
            "npc:infiltrate the group",
            "player:How can I do that",
            "npc:convince them you're one of them",
            "end",
        })
        t.expect("quest.stage.infiltrate", t.quest.expect_stage("infiltrate"))

        -- ---- Pick black mushrooms, dye Silverlight ----
        -- shadowstorm_dye.rs2 [oploc1,golem_black_mushrooms]; [opheldu,silverlight]
        -- -> ~sots_dye_silverlight.
        t.exec("goto-mushroom", t.player.goto_tile, 3495, 3088, 0)
        t.exec("pickMushroom", t.player.click_loc, "golem_black_mushrooms", 1)
        t.expect("has.mushroom", t.inv.await("golem_mushroom", 1, 5))
        t.exec("dyeSilverlight", t.player.use_item_on_item, "silverlight", "golem_mushroom")
        t.expect("has.dyed_silverlight", t.inv.await("agrith_silverlight_dyed", 1, 5))

        -- ---- Enter the Uzer ruins, pick up the strange implement ----
        -- shadowstorm_dye.rs2 [oploc1,golem_insidestairs_top] -> ^sots_ruin_dave.
        -- The "strange implement" is a real ground obj, golem_golemkey
        -- (all.obj: name=Strange implement), spawned at 2713,4913,0
        -- (areas/world/configs/m42_76.spawn) -- not a gap, QH's own WorldPoint
        -- for pickUpStrangeImplement.
        t.exec("goto-ruinstairs", t.player.goto_tile, 3493, 3090, 0)
        t.exec("goIntoRuin", t.player.click_loc, "golem_insidestairs_top", 1)
        t.exec("goto-implement", t.player.goto_tile, 2713, 4913, 0)
        -- click_obj is hollow on success (trap 12/8): call it directly and
        -- write the count back ourselves.
        local imp_r, imp_d = t.player.click_obj("golem_golemkey")
        t.check("pickUpStrangeImplement", imp_r == "ok", "click_obj golem_golemkey -> " .. tostring(imp_r) .. " " .. tostring(imp_d))
        t.expect("has.implement", t.inv.await("golem_golemkey", 1, 5))

        -- ---- Evil Dave at the portal ----
        -- shadowstorm_ritual.rs2 [opnpc1,agrith_dave]/[opnpc1,agrith_dave_at_portal].
        t.exec("goto-dave", t.player.goto_tile, 2721, 4911, 0)
        -- seam26: transcript "Infiltrating the wizards" -- undressed first, the
        -- clothing check refuses (shadowstorm_ritual.rs2 ~sots_dark_items_worn).
        t.exec("talkToEvilDave.undressed", t.player.talk_to, "agrith_dave_at_portal")
        t.exec("talkToEvilDave.undressed-dialog", t.chat.play, {
            "npc:What do you want",
            "choose:I want to join your group.",
            "player:I want to join your group",
            "npc:we do need one more person",
            "npc:you have to be evil",
            "choose:I'm evil!",
            "player:I'm evil!",
            "npc:You don't look evil",
            "player:evil in disguise",
            "npc:no need for the disguise",
        })
        t.expect("quest.stage.still_infiltrate", t.quest.expect_stage("infiltrate"))
        -- QH talkToEvilDave: dyed Silverlight and three black items EQUIPPED.
        t.exec("equip.priest_gown", t.player.equip, "priest_gown")
        t.exec("equip.priest_robe", t.player.equip, "priest_robe")
        t.exec("equip.black_cape", t.player.equip, "black_cape")
        t.exec("equip.silverlight_dyed", t.player.equip, "agrith_silverlight_dyed")
        t.exec("talkToEvilDave", t.player.talk_to, "agrith_dave_at_portal")
        t.exec("talkToEvilDave-dialog", t.chat.play, {
            "npc:What do you want",
            "choose:I want to join your group.",
            "player:I want to join your group",
            "npc:we do need one more person",
            "npc:you have to be evil",
            "choose:I'm evil!",
            "player:I'm evil!",
            "npc:totally evil",
            "npc:take you through to see Denath",
            "mesbox:escorts you into the demonic throne room",
            "npc:Master! This person wants to join us",
            "npc:one wizard short",
            "npc:totally evil",
            "npc:Thank you, Dave",
        })
        t.expect("quest.stage.denath", t.quest.expect_stage("denath"))
        t.expect("reen.moved_to_uzer", t.var.await_server("agrith_reen_uzer", 1, 5))
        t.ticks(3)
        local tr0, tile0 = t.world.tile()
        local inroom0 = tr0 == "ok" and tile0.level == 2 and tile0.x >= 2709 and tile0.x <= 2731 and tile0.z >= 4879 and tile0.z <= 4919
        t.step("escort.lands_in_throne_room", inroom0 and "PASS" or "FAIL",
            "after Evil Dave's escort: " .. (tile0 and (tile0.x .. "," .. tile0.z .. "," .. tile0.level) or "?"))
        t.ticks(2)

        -- ---- seam24: the throne room's cast (shadowstorm_ritual.rs2 ~sots_throne_cast) ----
        t.exec("talkToDenath", t.player.talk_to, "agrith_denath")
        t.exec("talkToDenath-dialog", t.chat.play, {
            "npc:another apprentice", "player:What do I have to do", "npc:Speak to Jennifer",
        })
        t.exec("talkToJennifer", t.player.talk_to, "agrith_jennifer")
        t.exec("talkToJennifer-dialog", t.chat.play, { "player:demonic sigil mould", "npc:Take a silver bar" })
        t.expect("has.mould", t.inv.await("agrith_sigil_mould", 1, 5))
        t.expect("quest.stage.sigil_tasks", t.quest.expect_stage("sigil_tasks"))
        t.exec("talkToMatthew", t.player.talk_to, "agrith_matthew")
        t.exec("talkToMatthew-dialog", t.chat.play, { "player:what happened to Josef", "npc:Search the kilns" })
        t.expect("quest.stage.matthew", t.quest.expect_stage("matthew"))

        -- ---- QH smeltSigil (after Matthew, before the golem): smelt the sigil (silver bar on a furnace, silver_crafting:agrith_sigil) ----
        t.exec("goto-furnace", t.player.goto_tile, 2869, 10202, 0)
        local fr, furnace = t.world.loc_near("dwarf_keldagrim_furnace", 60)
        t.check("furnace.locate", fr == "ok", "world.loc_near -> " .. tostring(fr))
        t.exec("smeltSigil.use", t.player.use_on, "silver_bar", furnace)
        -- ui.await_open / npc.await_present answer a bare ok; the row states
        -- what was awaited so no PASS row is empty-detail (seam26 closer).
        local smelt_open_r = t.ui.await_open("silver_crafting", 10)
        t.step("smeltSigil.open", smelt_open_r == "ok" and "PASS" or "FAIL",
            "silver_crafting interface open within 10 ticks -> " .. tostring(smelt_open_r))
        local wr, cell = t.ui.widget("silver_crafting:agrith_sigil")
        t.check("smeltSigil.cell", wr == "ok", "ui.widget silver_crafting:agrith_sigil -> " .. tostring(wr))
        local ir = t.ui.invoke(cell, 1)
        t.check("smeltSigil", ir == "ok", "ui.invoke silver_crafting:agrith_sigil op1 -> " .. tostring(ir))
        t.expect("has.sigil", t.inv.await("agrith_sigil", 1, 10))
        -- ---- The golem (QH talkToGolem, with the sigil) ----
        t.exec("goto-golem", t.player.goto_tile, 3486, 3088, 0)
        t.exec("talkToGolem", t.player.talk_to, "golem_golem")
        t.exec("talkToGolem-dialog", t.chat.play, { "player:Did you see anything", "npc:Denath came", "npc:hid it in one of the kilns" })
        t.expect("quest.stage.golem_ask", t.quest.expect_stage("golem_ask"))

        -- ---- The four kilns (QH searchKiln1..4) ----
        local kilns = {
            { "agrith_kiln_1", 3468, 3124 }, { "agrith_kiln_2", 3479, 3083 },
            { "agrith_kiln_3", 3473, 3093 }, { "agrith_kiln_4", 3501, 3085 },
        }
        for i = 1, #kilns do
            local k = kilns[i]
            t.exec("goto-kiln" .. i, t.player.goto_tile, k[2], k[3] - 1, 0)
            t.exec("searchKiln" .. i, t.player.click_loc, k[1], 1)
            t.ticks(2)
            local _, have = t.inv.count("agrith_book")
            if have ~= nil and have > 0 then break end
        end
        t.expect("has.book", t.inv.await("agrith_book", 1, 5))
        t.expect("quest.stage.ritual", t.quest.expect_stage("ritual"))
        -- QH readBook: the tome's Read op (shadowstorm_ritual.rs2:168 [opheld1,agrith_book]).
        t.exec("readBook", t.player.inv_op, "agrith_book", 1)
        t.exec("readBook-dialog", t.chat.play, { "mesbox:The tome describes the summoning of Agrith-Naar" })


        -- ---- QH enterRuinAfterBook / enterPortalAfterBook / talkToMatthewAfterBook (70 -> 80) ----
        t.exec("goto-ruinstairs2", t.player.goto_tile, 3493, 3090, 0)
        t.exec("enterRuinAfterBook", t.player.click_loc, "golem_insidestairs_top", 1)
        t.exec("goto-portal2", t.player.goto_tile, 2722, 4913, 0)
        t.exec("enterPortalAfterBook", t.player.click_loc, "golem_portal", 1)
        t.ticks(3)
        local tr, tile = t.world.tile()
        local inroom = tr == "ok" and tile.level == 2 and tile.x >= 2709 and tile.x <= 2731 and tile.z >= 4879 and tile.z <= 4919
        t.step("portal.lands_in_throne_room", inroom and "PASS" or "FAIL",
            "after golem_portal: " .. (tile and (tile.x .. "," .. tile.z .. "," .. tile.level) or "?"))
        t.ticks(2)
        t.exec("talkToMatthewAfterBook", t.player.talk_to, "agrith_matthew")
        t.exec("talkToMatthewAfterBook-dialog", t.chat.play, {
            "npc:Did you find that book", "player:Yes. The golem saw", "*", "*", "*", "*", "*",
            "*", "*", "npc:reverse order", "*", "*", "*", "*", "*",
            "npc:Thank goodness", "npc:time for the ritual",
        })
        t.expect("quest.stage.perform_ritual", t.var.await_server("agrith_quest", 80, 5))
        local dn_r, dn_d = t.npc.nearest("agrith_denath", 20)
        local jn_r, jn_d = t.npc.nearest("agrith_jennifer", 20)
        t.check("circle.plain_forms_gone", dn_r ~= "ok" and jn_r ~= "ok", "plain denath " .. tostring(dn_r) .. " " .. tostring(dn_d and (dn_d.tile_x or dn_d)) .. "; plain jennifer " .. tostring(jn_r))
        local circle_denath_sigil_r = t.npc.await_present("agrith_denath_sigil", 20, 5)
        t.step("circle.denath_sigil", circle_denath_sigil_r == "ok" and "PASS" or "FAIL",
            "agrith_denath_sigil in the npc pool within 20 ticks -> " .. tostring(circle_denath_sigil_r))

        -- ---- QH standInCircle 2718,4902,2 + IncantationStep (Denath's order) ----
        local wr = t.player.walk_to(2718, 4902)
        local cr, ct = t.world.tile()
        t.check("standInCircle", wr == "ok" and ct and ct.x == 2718 and ct.z == 4902, "walk_to -> " .. tostring(wr) .. " at " .. (ct and (ct.x .. "," .. ct.z .. "," .. ct.level) or "?"))
        t.exec("chant", t.player.inv_op, "agrith_sigil", 1)
        t.exec("chant-dialog", t.chat.play, {
            "choose:Nahudu", "player:Nahudu", "choose:Camerinthum", "player:Camerinthum",
            "choose:Caldar", "player:Caldar", "choose:Agrith-Naar", "player:Agrith-Naar",
            "choose:Tarren", "player:Tarren!", "mesbox:A magic circle",
            -- seam26: the rest of the scene (transcript "Getting in place").
            "npc:Oh my gods", "npc:He disappeared", "npc:Where'd he go",
            "npc:How could we be so stupid", "player:What happened",
            "npc:summoning ritual backwards", "npc:Denath was Agrith-Naar all along",
            "mesbox:BOOM", "npc:What was that", "npc:The portal's closing",
            "npc:I'm getting out of here", "npc:No, don't leave",
            "npc:Who knows what Denath", "npc:we need eight people",
            "npc:get those three to come back",
        })
        t.expect("quest.stage.ritual_done", t.var.await_server("agrith_quest", 90, 8))
        -- ---- QH steps.put(90): pickUpSigil (Denath's sigil left on the circle floor) ----
        local _, sigils_before = t.inv.count("agrith_sigil")
        local pr, pd = t.player.click_obj("agrith_sigil")
        t.check("pickUpSigil", pr == "ok", "click_obj agrith_sigil -> " .. tostring(pr) .. " " .. tostring(pd) .. " (sigils before " .. tostring(sigils_before) .. ")")
        t.expect("has.sigil_from_floor", t.inv.await("agrith_sigil", sigils_before + 1, 5))

        -- ---- leavePortal (QH 2720,4883,2): the placed exit golem_demon_portal ----
        -- seam26: golem_portal.rs2 [oploc1,golem_demon_portal] SotS branch ->
        -- ^sots_ruin_dave, maplink.dbrow's own destination for this portal.
        t.exec("leavePortal", t.player.click_loc, "golem_demon_portal", 1)
        t.ticks(3)
        local lr, lt = t.world.tile()
        local landed = lt and (lt.x .. "," .. lt.z .. "," .. lt.level) or "?"
        local in_ruin = lr == "ok" and lt.level == 0 and lt.x >= 2706 and lt.x <= 2738 and lt.z >= 4881 and lt.z <= 4918
        t.step("leavePortal.landed_in_ruin", in_ruin and "PASS" or "FAIL", "after golem_demon_portal: " .. landed)
        if not in_ruin then
            t.blocked("leavePortal still does not reach the ruin: " .. landed)
            return
        end
        -- Transcript "Walking out of the portal"; wiki "take her sigil as well".
        t.check("tanya.killed_by_ghosts", t.msg.expect("Tanya killed by ghosts"))
        local _, sig_b2 = t.inv.count("agrith_sigil")
        local ps2, pd2 = t.player.click_obj("agrith_sigil")
        t.check("pickUpSigil2", ps2 == "ok", "click_obj agrith_sigil (Tanya's) -> " .. tostring(ps2) .. " " .. tostring(pd2))
        t.expect("has.tanya_sigil", t.inv.await("agrith_sigil", sig_b2 + 1, 5))

        -- ---- QH tellDaveToReturn (2721,4900,0): Evil Dave in the passage ----
        t.expect("dave.in_passage_var", t.var.await_server("agrith_convinced_dave", 1, 3))
        local dave_in_passage_r = t.npc.await_present("agrith_dave_in_passage", 20, 5)
        t.step("dave.in_passage", dave_in_passage_r == "ok" and "PASS" or "FAIL",
            "agrith_dave_in_passage in the npc pool within 20 ticks -> " .. tostring(dave_in_passage_r))
        local _, sig_b3 = t.inv.count("agrith_sigil")
        t.exec("tellDaveToReturn", t.player.talk_to, "agrith_dave_in_passage")
        t.exec("tellDaveToReturn-dialog", t.chat.play, {
            "npc:Eric's dead",
            "npc:In a BAD way",
            "choose:You've got to get back to the throne room!",
            "player:You've got to get back to the throne room",
            "npc:the portal is closing",
            "player:Our only hope",
            "npc:You can kill him",
            "npc:It was Eric's sigil",
        })
        t.expect("dave.moved", t.var.await_server("agrith_convinced_dave", 2, 5))
        t.expect("has.eric_sigil", t.inv.await("agrith_sigil", sig_b3 + 1, 5))

        -- ---- QH goUpToBadden: leave the ruins by the stairs (2722,4885,0) ----
        local wx = t.player.walk_to(2721, 4886)
        t.check("walk-ruin_exit", wx == "ok", "walk_to 2721,4886 -> " .. tostring(wx))
        t.exec("goUpToBadden", t.player.click_loc, "golem_insidestairs_base", 1)
        t.ticks(3)
        local ur, ut = t.world.tile()
        t.step("ruin.left", (ur == "ok" and ut.x > 3400) and "PASS" or "FAIL", "after golem_insidestairs_base: " .. (ut and (ut.x .. "," .. ut.z .. "," .. ut.level) or "?"))

        -- ---- QH talkToBaddenAfterRitual / talkToReenAfterRitual ----
        t.exec("goto-badden2", t.player.goto_tile, 3486, 3091, 0)
        t.exec("talkToBaddenAfterRitual", t.player.talk_to, "agrith_badden_uzer")
        t.exec("talkToBaddenAfterRitual-dialog", t.chat.play, { "npc:Denath fled", "player:Will you join", "npc:Give me that sigil" })
        t.expect("badden.moved", t.var.await_server("agrith_badden_uzer", 2, 5))
        t.exec("talkToReenAfterRitual", t.player.talk_to, "agrith_reen_uzer")
        t.exec("talkToReenAfterRitual-dialog", t.chat.play, { "npc:A demonic ritual", "player:simple-minded", "npc:For Saradomin" })
        t.expect("reen.moved", t.var.await_server("agrith_reen_uzer", 2, 5))

        -- ---- QH talkToTheGolemAfterRitual / useImplementOnGolem / talkToGolemAfterReprogramming ----
        t.exec("goto-golem2", t.player.goto_tile, 3486, 3088, 0)
        t.exec("talkToTheGolemAfterRitual", t.player.talk_to, "golem_golem")
        t.exec("talkToTheGolemAfterRitual-dialog", t.chat.play, { "npc:I will not help", "mesbox:strange implement" })
        t.expect("golem.rejected", t.var.await_server("agrith_convinced_golem", 1, 5))
        local golem_target, gtr = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol", gtr == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(gtr))
        t.exec("useImplementOnGolem", t.player.use_on, "golem_golemkey", golem_target)
        t.exec("useImplementOnGolem-dialog", t.chat.play, { "mesbox:dusty scrolls", "mesbox:PORTAL OF THAMMARON" })
        t.expect("golem.reprogrammed", t.var.await_server("agrith_convinced_golem", 2, 5))
        t.exec("talkToGolemAfterReprogramming", t.player.talk_to, "golem_golem")
        t.exec("talkToGolemAfterReprogramming-dialog", t.chat.play, { "npc:New task" })
        t.expect("golem.moved", t.var.await_server("agrith_convinced_golem", 3, 5))

        -- ---- QH enterRuinAfterRecruiting / enterPortalAfterRecruiting ----
        t.exec("goto-ruinstairs3", t.player.goto_tile, 3493, 3090, 0)
        t.exec("enterRuinAfterRecruiting", t.player.click_loc, "golem_insidestairs_top", 1)
        t.exec("goto-portal3", t.player.goto_tile, 2722, 4913, 0)
        t.exec("enterPortalAfterRecruiting", t.player.click_loc, "golem_portal", 1)
        t.ticks(3)
        local tr3, tile3 = t.world.tile()
        local inroom3 = tr3 == "ok" and tile3.level == 2 and tile3.x >= 2709 and tile3.x <= 2731 and tile3.z >= 4879 and tile3.z <= 4919
        t.step("portal.recruited_lands_in_throne_room", inroom3 and "PASS" or "FAIL", "after golem_portal: " .. (tile3 and (tile3.x .. "," .. tile3.z .. "," .. tile3.level) or "?"))
        t.ticks(2)

        -- ---- QH talkToMatthewToStartFight (90 -> 110) ----
        local matthew = "agrith_matthew"
        local mr = t.npc.nearest("agrith_matthew_sigil", 20)
        if mr == "ok" then matthew = "agrith_matthew_sigil" end
        t.exec("talkToMatthewToStartFight", t.player.talk_to, matthew)
        t.exec("talkToMatthewToStartFight-dialog", t.chat.play, {
            "npc:eight people now", "choose:Yes.", "player:Yes.", "npc:Okay, here we go",
        })
        t.expect("quest.stage.summon", t.quest.expect_stage("summon"))
        local circle2_golem_sigil_r = t.npc.await_present("agrith_golem_sigil", 20, 5)
        t.step("circle2.golem_sigil", circle2_golem_sigil_r == "ok" and "PASS" or "FAIL",
            "agrith_golem_sigil in the npc pool within 20 ticks -> " .. tostring(circle2_golem_sigil_r))

        -- ---- QH standInCircleAgain 2720,4903,2 + IncantationStep (the tome's order) ----
        local wr2 = t.player.walk_to(2720, 4903)
        local cr2, ct2 = t.world.tile()
        t.check("standInCircleAgain", wr2 == "ok" and ct2 and ct2.x == 2720 and ct2.z == 4903, "walk_to -> " .. tostring(wr2) .. " at " .. (ct2 and (ct2.x .. "," .. ct2.z .. "," .. ct2.level) or "?"))
        t.exec("incantRitual", t.player.inv_op, "agrith_sigil", 1)
        t.exec("incantRitual-dialog", t.chat.play, {
            "choose:Tarren", "player:Tarren", "choose:Agrith-Naar", "player:Agrith-Naar",
            "choose:Caldar", "player:Caldar", "choose:Camerinthum", "player:Camerinthum",
            "choose:Nahudu", "player:Nahudu!",
            "npc:Matthew!", "npc:How dare you summon me", "npc:Aaaargh",
            "player:He didn't summon you", "player:I did!", "npc:Then prepare to die",
        })
        t.expect("quest.stage.fight", t.quest.expect_stage("fight"))

        -- ---- QH killDemon: Agrith-Naar, final blow with the dyed Silverlight worn ----
        t.exec("naar.attack", t.player.attack, "agrith_naar", 2, 15)
        t.exec("naar.dead", t.npc.await_dead_engaged, 240, 10)
        t.ticks(5)
        -- The blade fuses in the hand (seam26): Darklight is WORN, then QH
        -- steps.put(124) unequipDarklight (the wiki's fallback trigger).
        -- (player.unequip answers not_found when nothing of it is worn.)
        t.exec("unequipDarklight", t.player.unequip, "darklight")
        t.expect("naar.darklight", t.inv.await("darklight", 1, 5))
        t.quest.expect_complete()
        t.finish(0)
        return
    end,
}
