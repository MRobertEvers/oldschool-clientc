-- Monk's Friend (test_id drunkmonk, content quest_monksfriend / quest_drunkmonk).
-- Every CHECK from the scaffold resolved against:
--   OSRS-Content/osrs239-content/server/scripts/quests/quest_drunkmonk/configs/quest_drunkmonk.constant
--   OSRS-Content/osrs239-content/server/scripts/quests/quest_drunkmonk/scripts/quest_drunkmonk.rs2
--   OSRS-Content/osrs239-content/server/scripts/quests/quest_drunkmonk/scripts/drunkmonk_journal.rs2
--   OSRS-Content/osrs239-content/server/scripts/areas/ardougne_east/scripts/brother_omad.rs2
--   OSRS-Content/osrs239-content/server/scripts/areas/ardougne_east/scripts/brother_cedric.rs2
--   OSRS-Content/osrs239-content/server/scripts/areas/wilderness/scripts/lava_maze.rs2 (wildymirrorladdertop1)
--
-- Flow: Brother Omad (south of East Ardougne monastery) sends the player to
-- a secret cave under a ring of stones for a stolen child's blanket, then to
-- sober up and re-equip Brother Cedric (logs/water) so he can bring wine
-- back for a party. Rewards: 2000 Woodcutting XP, 8 Law runes, 1 QP
-- (quest_drunkmonk.rs2 [queue,drunkmonk_complete]).

return {
    id = "drunkmonk",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- fourteen tutorial slots would otherwise crowd the backpack
        "::give jug_water 1", -- Quest Helper: brought-along item, not the quest's own deliverable
        "::give logs 1", -- Quest Helper: brought-along item (or a woodplank; logs is simplest)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "drunkmonkquest",
            constants = {
                complete = 80,
                finding_water = 40,
                fixed_cart = 70,
                fixing_cart = 60,
                given_water = 50,
                looking_cedric = 30,
                not_started = 0,
                questpoints = 1,
                retrieved_blanket = 20,
                spoken_to_omad = 10,
            }, -- quest_drunkmonk.constant, verbatim
            row = "quest_monksfriend", -- ~quest_complete_rewards(quest_monksfriend, ...) in quest_drunkmonk.rs2
            display = "Monk's Friend", -- drunkmonk_journal.rs2: ~quest_journal("Monk's Friend", $text) -- list row == journal title
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---- Brother Omad: accept the quest (omad_whats_wrong -> omad_why -> omad_help) ----
        t.exec("goto-talkToOmad", t.player.goto_tile, 2604, 3209, 0) -- brother_omad's m40_50.spawn row
        t.exec("talkToOmad", t.player.talk_to, "brother_omad", 1) -- [opnpc1,brother_omad]
        t.exec("talkToOmad-dialog", t.chat.play, {
            "player:Hello there, What's wrong?",
            "npc:*yawn*...oh, hello...*yawn* I'",
            "choose:Why can't you sleep, what's wrong?",
            "player:Why can't you sleep, what's wr",
            "npc:It's brother Androe's son! Wit",
            "player:I suppose that's what kids do.",
            "npc:He was fine, up until last wee",
            "npc:Now he won't rest until it's r",
            "choose:Can I help at all?",
            "player:Can I help at all?",
            "npc:Please do. We won't be able to",
            "player:Where are they?",
            "npc:They hide in a secret cave in ",
        })
        t.expect("quest.stage.spoken_to_omad", t.quest.expect_stage("spoken_to_omad"))

        -- ---- The hidden ladder: settimer(blanket_ladder,1) armed by omad_help above.
        -- [timer,blanket_ladder] adds loc wildymirrorladdertop1 at ^blanket_ladder_coord
        -- (0_40_50_1_22 = 2561,3222,0) once the player is within 2 tiles of it. ----
        t.exec("goto-blanketLadder", t.player.goto_tile, 2561, 3222, 0)
        local ladder_wait_result, ladder_wait_detail = t.await({
            level = function()
                local r = t.world.loc_near("wildymirrorladdertop1", 3)
                return r == "ok"
            end,
            note = "blanket ladder loc_add",
        }, 5)
        t.check("blanketLadder.spawned", ladder_wait_result == "ok",
            "t.world.loc_near(wildymirrorladdertop1,3) after goto -> " .. tostring(ladder_wait_result)
            .. " (" .. tostring(ladder_wait_detail) .. ")")
        -- lava_maze.rs2 [oploc1,wildymirrorladdertop1]: loc_coord=^blanket_ladder_coord ->
        -- climb down, p_telejump(0_40_150_1_21) (square 40,150 local 1,21 = 2561,9621,0)
        t.exec("goDownLadder", t.player.click_loc, "wildymirrorladdertop1", 1)

        -- ---- The secret cave: fetch the child's blanket (obj childs_blanket, m40_150.spawn
        -- 2570,9604,0), guarded by headthief_blanket/thief_blanket -- no script gates the
        -- pickup itself, it is a plain ground item. ----
        t.exec("goto-blanketLocation", t.player.goto_tile, 2570, 9604, 0)
        -- click_obj's own success path answers ok with a nil detail (the
        -- await it settles on carries none) -- hollow through t.exec, so call
        -- it directly and write the counts ourselves (trap 12).
        local take_before_result, take_before_count = t.inv.count("childs_blanket")
        local take_result, take_detail = t.player.click_obj("childs_blanket")
        local take_after_result, take_after_count = t.inv.count("childs_blanket")
        t.check("takeBlanket",
            take_result == "ok" and take_after_result == "ok"
                and take_after_count > (take_before_result == "ok" and take_before_count or 0),
            "click_obj childs_blanket -> " .. tostring(take_result) .. " (" .. tostring(take_detail)
                .. "); count " .. tostring(take_before_count) .. " -> " .. tostring(take_after_count))
        t.exec("blanket.expect_has", t.inv.expect_has, "childs_blanket", 1)

        -- No scripted climb back UP is bound for this cave (only the descent is wired in
        -- lava_maze.rs2's wildymirrorladdertop1 handler) -- goto_tile back to the surface,
        -- same traversal cheat every far step above already uses.
        t.exec("goto-returnToOmadWithBlanket", t.player.goto_tile, 2604, 3209, 0)
        -- ---- Hand in the blanket (omad_have_blanket, inv_total(childs_blanket)>=1 branch) ----
        t.exec("returnToOmadWithBlanket", t.player.talk_to, "brother_omad", 1)
        t.exec("returnToOmadWithBlanket-dialog", t.chat.play, {
            "player:Hello.",
            "npc:*yawn*...oh, hello again...*ya",
            "npc:Please tell me you have the bl",
            "player:Yes! I've recovered it from",
            "mesbox:You hand the monk the child",
            "npc:Really, that's excellent, well",
            "npc:I'm off to bed! Farewell brave",
        })
        t.expect("quest.stage.retrieved_blanket", t.quest.expect_stage("retrieved_blanket"))
        -- t.inv.expect_absent is always hollow on ok (trap 12) -- poll for the
        -- count reaching zero ourselves (t.inv.await's count=0 form is not
        -- useful: total >= 0 is always true, so it never actually waits) and
        -- write the counts into the detail.
        local blanket_wait_result = t.await({
            level = function()
                local r, c = t.inv.count("childs_blanket")
                return r == "ok" and c == 0
            end,
            note = "childs_blanket consumed",
        }, 5)
        local blanket_after_result, blanket_after_count = t.inv.count("childs_blanket")
        t.check("blanket.expect_consumed",
            blanket_wait_result == "ok" and blanket_after_result == "ok" and blanket_after_count == 0,
            "childs_blanket count after hand-in to Omad: " .. tostring(blanket_after_count)
                .. " (await " .. tostring(blanket_wait_result) .. ")")

        -- ---- Ask Omad about the party (omad_organize_party -> omad_where_look) ----
        t.exec("askAboutCedric", t.player.talk_to, "brother_omad", 1)
        t.exec("askAboutCedric-dialog", t.chat.play, {
            "player:Hello, how are you?",
            "npc:Much better now I'm sleeping w",
            "player:Ooh! What party?",
            "npc:The son of Brother Androe's bi",
            "player:That's sweet!",
            "npc:It's also a great excuse for a",
            "npc:We just need Brother Cedric to",
            "choose:Who's Brother Cedric?",
            "player:Who's Brother Cedric?",
            "npc:Cedric is a member of the orde",
            "npc:He most probably got drunk and",
            "npc:I don't suppose you could look",
            "choose:Where should I look?",
            "player:Where should I look?",
            "npc:Oh, he won't be far. Probably ",
            "player:Ok, I'll go and find him.",
        })
        t.expect("quest.stage.looking_cedric", t.quest.expect_stage("looking_cedric"))

        -- ---- Brother Cedric, north of the monastery (cedric_okay) ----
        t.exec("goto-talkToCedric", t.player.goto_tile, 2614, 3259, 0) -- brother_cedric's m40_50.spawn row
        t.exec("talkToCedric", t.player.talk_to, "brother_cedric", 1) -- [opnpc1,brother_cedric]
        t.exec("talkToCedric-dialog", t.chat.play, {
            "player:Brother Cedric are you okay?",
            "npc:Yeesshhh, I'm very, very drunk",
            "player:Brother Omad needs the wine",
            "npc:Oh dear, oh dear, I knew I had",
            "npc:Pleashhh, find me a jug of wat",
        })
        t.expect("quest.stage.finding_water", t.quest.expect_stage("finding_water"))

        -- ---- Give the jug of water (cedric_need_water), which falls straight into
        -- cedric_fix_cart in the SAME dialogue session (no new click). ----
        t.exec("talkToCedricWithJug", t.player.talk_to, "brother_cedric", 1)
        t.exec("talkToCedricWithJug-dialog", t.chat.play, {
            "player:Are you okay?",
            "npc:Hic up! Oh my head! I need a ",
            "player:Cedric! Here, drink! I hav",
            "npc:Good stuff, my head's spinnin",
            -- (npc_say("Gulp...gulp!") is an overhead line, not a dialogue page -- skipped)
            "mesbox:You hand the monk a jug of w",
            "npc:Aah! That's better!",
            "npc:Now I just need to fix this c",
            "npc:Could you help?",
            "choose:Yes, I'd be happy to!",
            "player:Yes, I'd be happy to!",
            "npc:Excellent, I just need some wo",
            "player:OK, I'll see what I can fin",
        })
        t.expect("quest.stage.fixing_cart", t.quest.expect_stage("fixing_cart"))
        local water_wait_result = t.await({
            level = function()
                local r, c = t.inv.count("jug_water")
                return r == "ok" and c == 0
            end,
            note = "jug_water consumed",
        }, 5)
        local water_after_result, water_after_count = t.inv.count("jug_water")
        t.check("water.expect_consumed",
            water_wait_result == "ok" and water_after_result == "ok" and water_after_count == 0,
            "jug_water count after giving Cedric a drink: " .. tostring(water_after_count)
                .. " (await " .. tostring(water_wait_result) .. ")")

        -- ---- Hand over the logs (cedric_get_wood -- opens on the NPC's own line, not
        -- the player's: this trigger @-jumps straight in, there is no fresh greeting). ----
        t.exec("talkToCedricWithLog", t.player.talk_to, "brother_cedric", 1)
        t.exec("talkToCedricWithLog-dialog", t.chat.play, {
            "npc:Did you manage to get some wo",
            "mesbox:You show Cedric some logs.",
            "player:Here you go!",
            "npc:Well done! Now I'll fix this ",
            "player:Ok! I'll see you later!",
        })
        t.expect("quest.stage.fixed_cart", t.quest.expect_stage("fixed_cart"))
        local logs_wait_result = t.await({
            level = function()
                local r, c = t.inv.count("logs")
                return r == "ok" and c == 0
            end,
            note = "logs consumed",
        }, 5)
        local logs_after_result, logs_after_count = t.inv.count("logs")
        t.check("logs.expect_consumed",
            logs_wait_result == "ok" and logs_after_result == "ok" and logs_after_count == 0,
            "logs count after showing Cedric the wood: " .. tostring(logs_after_count)
                .. " (await " .. tostring(logs_wait_result) .. ")")

        -- ---- Reward snapshot before the hand-in (H2, docs/QUEST_SUITE_KIT.md). ----
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        local reward_lawrune_before_result, reward_lawrune_before = t.inv.count("lawrune")

        -- ---- Tell Omad Cedric is on the way (omad_party -> drunkmonk_party). The party's
        -- own npc_anim/npc_say/anim/say lines are overhead speech, not dialogue pages, and
        -- the reward itself lands two ticks later via [queue,drunkmonk_complete]. ----
        t.exec("goto-finishQuest", t.player.goto_tile, 2604, 3209, 0)
        t.exec("finishQuest", t.player.talk_to, "brother_omad", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:Hi Omad, Brother Cedric is o",
            "npc:Good! Good! Now we can party!",
            "npc:I have little to repay you wit",
            "mesbox:Brother Omad gives you 8 La",
            "player:Thanks Brother Omad!",
            "npc:OK, let's party!",
        })
        t.ticks(3) -- [queue,drunkmonk_complete] lands after p_delay(2) in drunkmonk_party

        t.quest.expect_complete()

        -- ---- Reward checks -- the literal reward Quest Helper/quest_drunkmonk.rs2
        -- document (2000 Woodcutting XP, 8 Law runes), never a number read back. ----
        t.check("reward.woodcutting", t.skill.expect_gain("woodcutting", 2000, reward_before))
        local reward_lawrune_after_result, reward_lawrune_after = t.inv.count("lawrune")
        t.check("reward.lawrune",
            reward_lawrune_before_result == "ok" and reward_lawrune_after_result == "ok"
                and reward_lawrune_after == reward_lawrune_before + 8,
            string.format("lawrune %s -> %s (want +8), reads %s/%s",
                tostring(reward_lawrune_before), tostring(reward_lawrune_after),
                tostring(reward_lawrune_before_result), tostring(reward_lawrune_after_result)))

        t.finish(0)
    end,
}
