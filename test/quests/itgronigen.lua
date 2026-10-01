return {
    id = "itgronigen",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::give woodplank 3",
        "::give bronze_bar 1",
        "::give molten_glass 1",
        "::give steel_scimitar 1",
        "::give lobster 12",
        "::setlevel crafting 10",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::setlevel defence 30",
        "::setlevel hitpoints 50",
        "::wield steel_scimitar",
    },
    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "itgronigen",
            constants = {
                not_started = 0, started = 1, given_planks = 2, given_bronze = 3, given_glass = 4,
                given_mould = 5, sent_telescope = 6, complete = 7, claimed_wine = 8,
            },
            row = "quest_observatoryquest",
            display = "Observatory Quest",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("goto-talkToProfessor", t.player.goto_tile, 2442, 3186, 0)
        t.exec("talkToProfessor", t.player.talk_to, "observatory_professor", 1)
        t.exec("talkToProfessor.dialog", t.chat.play, {
            "npc:Hello adventurer.",
            "choose:I'd like to have a look through that telescope.",
            "player:I'd like to have a look through that telescope.",
            "npc:So would I!",
            "npc:The trouble is",
            "player:What do you mean?",
            "npc:Did you see those houses outside?",
            "player:Up on the hill?",
            "npc:It's a family of goblins.",
            "npc:Since they moved here",
            "npc:Last week, my telescope",
            "npc:Now, parts need replacing",
            "npc:Err, I don't suppose",
            "choose:Sounds interesting, what can I do for you?",
            "player:Sounds interesting",
            "npc:Oh, thanks so much.",
            "npc:I need three new parts",
            "npc:I need wood",
            "npc:My assistant will help",
            "npc:Go talk to him",
            "player:Okay what do I need to do?",
            "npc:First I need three planks",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- assistant hint at stage 1 (LostCity observatory_assistant.rs2 started branch)
        t.exec("assistant.hint", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.hint.dialog", t.chat.play, {
            "npc:How can I help you?",
            "choose:I can't find any planks!",
            "player:I can't find any planks!",
            "npc:I understand planks can be found",
            "end",
        })

        -- planks
        t.exec("goto-professor2", t.player.goto_tile, 2442, 3186, 0)
        t.exec("giveProfessorPlanks", t.player.talk_to, "observatory_professor", 1)
        t.exec("giveProfessorPlanks.dialog", t.chat.play, {
            "npc:It's my helping hand",
            "npc:Do you have the planks yet?",
            "player:Yes, I've got them",
            "npc:Well done",
            "npc:Now, the bronze",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_planks", t.quest.expect_stage("given_planks"))
        t.expect("planks.gone", t.inv.expect_absent("woodplank"))

        t.exec("giveProfessorBar", t.player.talk_to, "observatory_professor", 1)
        t.exec("giveProfessorBar.dialog", t.chat.play, {
            "npc:Hello again, do you have the bronze",
            "player:Yes, I have it.",
            "npc:Great. Now all I need",
            "npc:Please get me some molten glass",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_bronze", t.quest.expect_stage("given_bronze"))

        t.exec("giveProfessorGlass", t.player.talk_to, "observatory_professor", 1)
        t.exec("giveProfessorGlass.dialog", t.chat.play, {
            "npc:How are you getting on finding me some glass?",
            "player:Here it is!",
            "npc:Excellent!",
            "npc:Oh no, I can't use this glass",
            "player:What do you mean, lens mould?",
            "npc:I need my lens mould",
            "npc:I'll have to ask you",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_glass", t.quest.expect_stage("given_glass"))
        t.expect("glass.taken", t.inv.expect_absent("molten_glass"))

        -- dungeon
        t.exec("goto-vstairs", t.player.goto_tile, 2458, 3186, 0)
        t.exec("enterDungeon", t.player.click_loc, "qip_obs_vstairs2", 1)
        t.ticks(4)
        local _, choice = t.var.server("observatory_chestchoice")
        t.step("chestchoice", choice ~= nil and "PASS" or "FAIL", "chest choice = " .. tostring(choice))
        local closed = { [0] = "qip_obs_dungeon_chest_closed", [1] = "qip_obs_dungeon_chest_closed2", [2] = "qip_obs_dungeon_chest_closed3" }
        local open = { [0] = "qip_obs_dungeon_chest_open", [1] = "qip_obs_dungeon_chest_open2", [2] = "qip_obs_dungeon_chest_open3" }
        local at = { [0] = { 2364, 9355 }, [1] = { 2360, 9366 }, [2] = { 2351, 9361 } }
        local wrong = (choice + 1) % 3
        t.exec("goto-wrongchest", t.player.goto_tile, at[wrong][1], at[wrong][2] - 1, 0)
        t.exec("searchChests.wrong.open", t.player.click_loc, closed[wrong], 1)
        t.ticks(2)
        t.exec("searchChests.wrong.search", t.player.click_loc, open[wrong], 1)
        t.expect("wrongchest.empty", t.msg.expect("The chest is empty."))
        t.exec("goto-keychest", t.player.goto_tile, at[choice][1], at[choice][2] - 1, 0)
        t.exec("searchChests.key.open", t.player.click_loc, closed[choice], 1)
        t.ticks(2)
        t.exec("searchChests.key.search", t.player.click_loc, open[choice], 1)
        t.exec("keychest.dialog", t.chat.play, { "mesbox:You find a kitchen key.", "end" })
        t.expect("key.held", t.inv.expect_has("keep_key", 1))

        -- kitchen gate + sleeping guard (the guard and the maze are NORTH of the gate, the stove south)
        t.exec("goto-gate", t.player.goto_tile, 2328, 9395, 0)
        t.exec("gate.click", t.player.click_loc, "keepgate_closed", 1)
        t.expect("gate.warn", t.msg.expect("the guard will hear you"))
        t.exec("prodGuard", t.player.press, "qip_obs_goblin_guard", 1)
        t.ticks(1)
        t.exec("guard.attack", t.player.attack, "goblin_guard", 2)
        t.exec("guard.dead", t.npc.await_dead_engaged, 240, 40, { eat = { item = "lobster", below = 25 } })
        t.ticks(6)
        t.expect("guard.bones", t.world.obj_near("bones", 6))

        -- gate now opens with the key (the guard is dead), then the stove on the south side
        t.exec("gate.open", t.player.click_loc, "keepgate_closed", 1)
        t.ticks(2)
        t.player.walk_to(2327, 9391)
        local _, gt = t.world.tile()
        t.check("gate.passed", gt and (gt.z or gt[2]) == 9391, "player z after the gate = " .. tostring(gt and (gt.z or gt[2])))
        t.exec("inspectStove", t.player.click_loc, "qip_obs_dungeon_stove_top_multi", 1)
        t.exec("inspectStove.dialog", t.chat.play, {
            "mesbox:The goblins appear to have been using the lens mould",
            "mesbox:You shake out its contents",
            "end",
        })
        t.expect("mould.held", t.inv.expect_has("lens_mould", 1))

        -- back north through the gate, out by the dungeon exit stair (fixed leg)
        t.player.walk_to(2327, 9395)
        local _, nt = t.world.tile()
        t.check("gate.back_north", nt and (nt.z or nt[2]) == 9395, "player z after going north = " .. tostring(nt and (nt.z or nt[2])))
        t.exec("goto-exit", t.player.goto_tile, 2355, 9394, 0)
        t.exec("leaveDungeon", t.player.click_loc, "qip_obs_stairs1_dungeon", 1)
        t.ticks(4)
        local _, ut = t.world.tile()
        t.check("exit.surface", ut and (ut.x or ut[1]) == 2458 and (ut.z or ut[2]) == 3185, "surface tile " .. tostring(ut and (ut.x or ut[1])) .. "," .. tostring(ut and (ut.z or ut[2])))

        -- professor takes the mould, hands the glass back (stage 5)
        t.exec("goto-professor3", t.player.goto_tile, 2442, 3186, 0)
        t.exec("giveProfessorMould", t.player.talk_to, "observatory_professor", 1)
        t.exec("giveProfessorMould.dialog", t.chat.play, {
            "npc:Did you bring me the mould?",
            "player:Yes, I've managed to find it.",
            "npc:At last, you've brought all",
            "npc:Oh no! I can't do this.",
            "player:What do you mean?",
            "npc:My crafting skill",
            "npc:You can use the mould with molten glass",
            "npc:As long as you have practiced",
            "mesbox:The professor gives you back the molten glass.",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_mould", t.quest.expect_stage("given_mould"))
        t.expect("glass.returned", t.inv.expect_has("molten_glass", 1))
        t.expect("mould.still_held", t.inv.expect_has("lens_mould", 1))

        -- craft the lens: mould + glass, Crafting 10 (mould is NOT consumed)
        t.exec("useGlassOnMould", t.player.use_item_on_item, "lens_mould", "molten_glass")
        t.exec("useGlassOnMould.dialog", t.chat.play, {
            "mesbox:You pour the molten glass into the mould",
            "mesbox:It has produced a small, convex glass disc.",
            "end",
        })
        t.expect("lens.held", t.inv.expect_has("lens", 1))
        t.expect("glass.spent", t.inv.expect_absent("molten_glass"))
        t.expect("mould.kept_after_cast", t.inv.expect_has("lens_mould", 1))

        t.exec("giveProfessorLensAndMould", t.player.talk_to, "observatory_professor", 1)
        t.exec("giveProfessorLensAndMould.dialog", t.chat.play, {
            "npc:Is the lens finished?",
            "player:Yes, here it is.",
            "npc:Wonderful, at last I can fix the telescope.",
            "npc:I'll take back that mould",
            "npc:Meet me at the Observatory later",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.sent_telescope", t.quest.expect_stage("sent_telescope"))
        t.expect("lens.gone", t.inv.expect_absent("lens"))
        t.expect("mould.taken_back", t.inv.expect_absent("lens_mould"))

        -- assistant at stage 6 (LostCity sent_telescope branch)
        t.exec("assistant.repairman", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.repairman.dialog", t.chat.play, {
            "player:Hello again.",
            "npc:Ah, it's the telescope repairman!",
            "end",
        })

        -- to the Observatory dome through the dungeon's second exit stair
        t.exec("goto-vstairs2", t.player.goto_tile, 2458, 3186, 0)
        t.exec("enterDungeonAgain", t.player.click_loc, "qip_obs_vstairs2", 1)
        t.ticks(4)
        t.exec("goto-dome-stair", t.player.goto_tile, 2335, 9352, 0)
        t.exec("enterObservatory", t.player.click_loc, "qip_obs_stairs1_dungeon", 1, { at = { 2335, 9351 } })
        t.ticks(4)
        local _, dt = t.world.tile()
        t.step("dome.arrival", dt ~= nil and "PASS" or "FAIL", "arrived " .. tostring(dt and (dt.x or dt[1])) .. "," .. tostring(dt and (dt.z or dt[2])) .. "," .. tostring(dt and (dt.level or dt[3])))
        t.exec("goto-dome-stair-up", t.player.goto_tile, 2444, 3160, 0)
        t.exec("goToF2Observatory", t.player.click_loc, "qip_obs_stairs1", 1)
        t.ticks(4)
        local _, ft = t.world.tile()
        t.check("dome.upstairs", ft and (ft.level or ft[3]) == 1, "level after stairs = " .. tostring(ft and (ft.level or ft[3])))

        -- the cutscene approximation (dialogue) then the telescope
        t.exec("professor.cutscene", t.player.talk_to, "observatory_professor_multi2", 1)
        t.exec("professor.cutscene.dialog", t.chat.play, {
            "npc:Oh, hi there.",
            "player:Okay, don't let me interrupt.",
            "npc:Thank you.",
            "npc:Welcome back.",
            "player:Hi, this really is impressive.",
            "npc:Certainly is.",
            "end",
        })
        t.exec("goto-telescope", t.player.goto_tile, 2441, 3162, 1)
        t.exec("viewTelescope", t.player.click_loc, "qip_obs_tele_gear_upper_multi", 1)
        t.ticks(3)
        t.key("escape")
        t.ticks(3)
        local _, sign = t.var.server("observatory_starsign")
        t.step("telescope.sign", sign ~= nil and "PASS" or "FAIL", "starsign = " .. tostring(sign))
        local names = { [0] = "Aquarius", "Capricorn", "Sagittarius", "Scorpio", "Libra", "Virgo", "Leo", "Cancer", "Gemini", "Taurus", "Aries", "Pisces" }
        local pages = { [0] = 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3 }
        local wrong = (sign + 5) % 12
        t.exec("professor.wrong", t.player.talk_to, "observatory_professor_multi2", 1)
        local wl = { "npc:Welcome back.", "player:I've had a look through the telescope.", "npc:What did you see?", "player:It was..." }
        for _ = 1, pages[wrong] do wl[#wl + 1] = "choose:~ next ~" end
        wl[#wl + 1] = "choose:" .. names[wrong]
        wl[#wl + 1] = "player:" .. names[wrong] .. "!"
        wl[#wl + 1] = "npc:I'm afraid not."
        wl[#wl + 1] = "end"
        t.exec("professor.wrong.dialog", t.chat.play, wl)
        t.expect("quest.stage.sent_telescope.still", t.quest.expect_stage("sent_telescope"))

        local _, snap = t.skill.snapshot()
        t.exec("tellProfessorConstellation", t.player.talk_to, "observatory_professor_multi2", 1)
        local rl = { "npc:Welcome back.", "player:I've had a look through the telescope.", "npc:What did you see?", "player:It was..." }
        for _ = 1, pages[sign] do rl[#rl + 1] = "choose:~ next ~" end
        rl[#rl + 1] = "choose:" .. names[sign]
        rl[#rl + 1] = "player:" .. names[sign] .. "!"
        rl[#rl + 1] = "npc:That's exactly it!"
        rl[#rl + 1] = "player:Yes! Woo hoo!"
        rl[#rl + 1] = "*"
        rl[#rl + 1] = "*"
        rl[#rl + 1] = "npc:By Saradomin's earlobes!"
        rl[#rl + 1] = "npc:Look in your backpack"
        t.exec("tellProfessorConstellation.dialog", t.chat.play, rl)
        t.ticks(3)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.expect("reward.crafting", t.skill.expect_gain("crafting", 2250, snap))
        t.expect("reward.sapphire", t.inv.expect_has("uncut_sapphire", 1))
        local rr, rd = t.scroll.rewards()
        t.step("reward.lines", rr == "ok" and "PASS" or "FAIL", tostring(rr) .. " " .. tostring(type(rd) == "table" and table.concat(rd.lines or {}, " / ") or rd))
        t.expect("quest.complete", t.quest.expect_complete())

        -- assistant hands over the wine (stage 8)
        t.exec("goto-assistant", t.player.goto_tile, 2443, 3188, 0)
        t.exec("assistant.wine", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.wine.dialog", t.chat.play, {
            "npc:Well, hello again.",
            "npc:You've made my life much easier!",
            "mesbox:The assistant gives you some wine.",
            "player:Thanks very much.",
            "end",
        })
        t.expect("quest.stage.claimed_wine", t.quest.expect_stage("claimed_wine"))
        t.expect("wine.held", t.inv.expect_has("jug_wine", 1))
    end,
}
