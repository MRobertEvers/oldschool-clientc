-- Shadow of the Storm (shadowstorm). Tier 2, 340c190aae parity2c "partial".
-- Prereqs (The Golem, Demon Slayer) via ::complete -- rule (e), never ::setvar
-- the quest's own varp. Everything from Father Reen through Evil Dave's
-- portal is driven for real; the run stops at a confirmed content gap (see
-- the t.blocked at the end) -- Denath and Jennifer are never placed in the
-- live world outside the ritual-assembly proc / debug checkpoints, so stage
-- ^sots_denath (30) can never reach ^sots_sigil_tasks (40) live.
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
        t.exec("talkToEvilDave", t.player.talk_to, "agrith_dave_at_portal")
        t.exec("talkToEvilDave-dialog", t.chat.play, {
            "npc:Who are you",
            "player:I want to join your group",
            "npc:Are you evil",
            "player:I'm evil!",
            "npc:Denath's through the portal",
            "mesbox:Evil Dave lets you join",
        })
        t.expect("quest.stage.denath", t.quest.expect_stage("denath"))

        -- ---- Through the portal to the throne room ----
        -- golem_portal.rs2 [oploc1,golem_portal]: quest>=denath -> p_teleport(^sots_throne).
        t.exec("goto-portal", t.player.goto_tile, 2722, 4913, 0)
        -- seam25: the post-Golem child golem_demon_door_always_open now carries
        -- the SotS branch (golem_portal.rs2); no ::goto past the portal.
        t.exec("enterPortal", t.player.click_loc, "golem_portal", 1)
        t.ticks(3)
        local tr0, tile0 = t.world.tile()
        local inroom0 = tr0 == "ok" and tile0.level == 2 and tile0.x >= 2709 and tile0.x <= 2731 and tile0.z >= 4879 and tile0.z <= 4919
        t.step("portal.first_lands_in_throne_room", inroom0 and "PASS" or "FAIL",
            "after golem_portal: " .. (tile0 and (tile0.x .. "," .. tile0.z .. "," .. tile0.level) or "?"))
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
        t.expect("smeltSigil.open", t.ui.await_open("silver_crafting", 10))
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
        t.expect("circle.denath_sigil", t.npc.await_present("agrith_denath_sigil", 20, 5))

        -- ---- QH standInCircle 2718,4902,2 + IncantationStep (Denath's order) ----
        local wr = t.player.walk_to(2718, 4902)
        local cr, ct = t.world.tile()
        t.check("standInCircle", wr == "ok" and ct and ct.x == 2718 and ct.z == 4902, "walk_to -> " .. tostring(wr) .. " at " .. (ct and (ct.x .. "," .. ct.z .. "," .. ct.level) or "?"))
        t.exec("chant", t.player.inv_op, "agrith_sigil", 1)
        t.exec("chant-dialog", t.chat.play, {
            "choose:Nahudu", "player:Nahudu", "choose:Camerinthum", "player:Camerinthum",
            "choose:Caldar", "player:Caldar", "choose:Agrith-Naar", "player:Agrith-Naar",
            "choose:Tarren", "player:Tarren!", "mesbox:A magic circle",
        })
        t.expect("quest.stage.ritual_done", t.var.await_server("agrith_quest", 90, 8))
        -- ---- QH steps.put(90): pickUpSigil (Denath's sigil left on the circle floor) ----
        local _, sigils_before = t.inv.count("agrith_sigil")
        local pr, pd = t.player.click_obj("agrith_sigil")
        t.check("pickUpSigil", pr == "ok", "click_obj agrith_sigil -> " .. tostring(pr) .. " " .. tostring(pd) .. " (sigils before " .. tostring(sigils_before) .. ")")
        t.expect("has.sigil_from_floor", t.inv.await("agrith_sigil", sigils_before + 1, 5))

        -- ---- leavePortal: QH ObjectStep AGRITH_PORTAL_CLOSING at 2720,4883,2 ----
        -- The cache's agrith_portal_closing (loc 10251) is never placed by a map
        -- square or a loc_add; the map holds golem_demon_portal (6282) at
        -- 2719,4883,2 instead. Prove both, then press the placed one.
        local pr2, pd2 = t.world.loc_near("agrith_portal_closing", 40)
        t.check("leavePortal.closing_loc_absent", pr2 ~= "ok", "world.loc_near agrith_portal_closing r40 -> " .. tostring(pr2) .. " " .. tostring(pd2))
        t.exec("leavePortal", t.player.click_loc, "golem_demon_portal", 1)
        t.ticks(3)
        local lr, lt = t.world.tile()
        local landed = lt and (lt.x .. "," .. lt.z .. "," .. lt.level) or "?"
        local in_ruin = lr == "ok" and lt.level == 0 and lt.x >= 2706 and lt.x <= 2738 and lt.z >= 4881 and lt.z <= 4918
        t.step("leavePortal.landed", lr == "ok" and "PASS" or "FAIL", "observed landing after golem_demon_portal: " .. landed .. "; in ruin (2706..2738 x 4881..4918 level 0) = " .. tostring(in_ruin))
        if not in_ruin then
            t.blocked("content_bug: QH leavePortal (AGRITH_PORTAL_CLOSING, 2720,4883,2) has no live loc -- agrith_portal_closing (10251) is placed by no map square and no loc_add, and the throne room's placed exit portal golem_demon_portal (m42_76.jl2 line 2, 2719,4883,2) runs quest_golem/scripts/golem_portal.rs2:227 [oploc1,golem_demon_portal] -> @golem_enter_demon_lair (golem_portal.rs2:233-240) with no Shadow of the Storm branch, so the player lands at " .. landed .. " instead of the ruin (^sots_ruin_dave). Downstream legs are also dead live: agrith_dave_at_portal is hidden by its multinpc table above quest value 70 and agrith_dave_in_passage needs %agrith_convinced_dave = 1, which nothing writes; agrith_reen_uzer needs %agrith_reen_uzer = 1, which nothing writes (shadowstorm.rs2:44,59; shadowstorm_ritual.rs2:27,528)")
            return
        end
        t.blocked("leavePortal now lands in the ruin: extend this file past it")
        return
    end,
}
