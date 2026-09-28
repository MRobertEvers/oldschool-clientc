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
        t.exec("enterPortal", t.player.click_loc, "golem_portal", 1)
        t.exec("goto-throne", t.player.goto_tile, 2720, 4912, 2)
        t.ticks(2)

        -- ---- CONTENT GAP: Denath is never placed in the live world ----
        -- shadowstorm_dye.rs2:56 [opnpc1,agrith_denath] / [opnpc1,agrith_denath_sigil]
        -- and :71 [opnpc1,agrith_jennifer] / [opnpc1,agrith_jennifer_sigil] are the
        -- ONLY triggers on those symbols in the whole tree. Grep confirms zero
        -- npc_add(..., agrith_denath, ...) and zero npc_add(..., agrith_jennifer, ...)
        -- anywhere in server/scripts: the only place either npc (or Matthew's
        -- pre-ritual form) is ever placed is shadowstorm_ritual.rs2's
        -- ~sots_ritual_assemble (line 214-231, the *_sigil forms only, reached
        -- at stage 80 -- AFTER this leg) or the shadowstormritual/recruit/fight
        -- debugprocs (line 635-699). So at ^sots_denath (30, real, reached
        -- above with no cheat) there is no live Denath to advance the stage,
        -- and agrith_sigil_mould -- the ONLY source of which is Jennifer's
        -- handler at shadowstorm_dye.rs2:88 (grep of the whole tree confirms
        -- no other inv_add(..., agrith_sigil_mould, ...)) -- can never be
        -- obtained live either. That blocks ^sots_sigil_tasks (40) and every
        -- stage after it: talkToMatthew (first visit), smeltSigil, the kiln
        -- search, readBook, talkToMatthewAfterBook, the first ritual, the
        -- recruit chase and the fight are all unreachable without a cheat
        -- doing quest work forbidden by rule (d).
        local denath_r, denath_d = t.player.talk_to("agrith_denath")
        t.check("throne.denath_absent", true,
            "talk_to agrith_denath -> " .. tostring(denath_r) .. " " .. tostring(denath_d)
            .. " (no npc_add for agrith_denath/agrith_jennifer anywhere outside"
            .. " the ritual-assemble proc and the debug checkpoints)")
        t.blocked("content_bug: agrith_denath/agrith_jennifer never spawn in real "
            .. "content (shadowstorm_dye.rs2:56,71; only npc_add is "
            .. "shadowstorm_ritual.rs2:214-231's *_sigil forms at stage 80, or "
            .. "the shadowstormritual/recruit/fight debugprocs) -- "
            .. "sots_denath(30) cannot reach sots_sigil_tasks(40) live: "
            .. "agrith_sigil_mould has no other source than Jennifer "
            .. "(shadowstorm_dye.rs2:88)")
        return
    end,
}
