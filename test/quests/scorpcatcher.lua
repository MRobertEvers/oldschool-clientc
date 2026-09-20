-- Scorpion Catcher. quest_scorpcatcher's own content
-- (OSRS-Content/osrs239-content/server/scripts/quests/quest_scorpcatcher/,
-- areas/area_seers/scripts/{thormac,seer}.rs2) is a from-scratch port, much
-- smaller than the real quest: no Taverley Dungeon traversal gates the
-- scorpions at all -- catching one is a plain opnpcu (use_on the cage) at
-- the npc's own *.spawn tile, and searching the secret wall is flavour, not
-- a gate. Reward is ONLY 1 quest point (~quest_complete_rewards passes an
-- empty rewards string and no xp/coins), already covered by
-- quest.expect_complete()'s own quest.points row -- there is no second
-- reward row to write.

return {
    id = "scorpcatcher",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- fourteen tutorial slots, so the scorpion cage fits
        "::setlevel prayer 31", -- thormac.rs2's only real start requirement
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "scorpcatcher",
            constants = {
                not_started = 0,
                started = 1,
                first_hint = 2,
                second_hint = 3,
                complete = 6,
            },
            row = "quest_scorpioncatcher",
            display = "Scorpion Catcher",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- goto_tile climbs floors for us (docs/QUEST_AUTHORING.md section 2) --
        -- no click_loc on the Sorcerer's Tower ladders. Thormac stands at the
        -- top, plane 3 (areas/area_seers/scripts/thormac.rs2 [opnpc1,thormac]).
        t.exec("goto-thormac", t.player.goto_tile, 2702, 3405, 3)
        t.exec("talk.thormac1", t.player.talk_to, "thormac", 1)
        -- [opnpc1,thormac]: not_started & prayer>=31 -> the quest-offer branch.
        t.exec("talk.thormac1-dialog", t.chat.play, {
            "npc:Hello I am Thormac the sorcere",
            "choose:What do you need assistance with?",
            "player:What do you need assistance wi",
            "npc:I've lost my pet scorpions. Th",
            "npc:I left their cage door open, n",
            "npc:There's three of them, and the",
            "player:How would I go about catching ",
            "npc:Well I have a scorpion cage he",
            "npc:If you go up to the village of",
            "choose:Yes.",
            "player:Okay, I will do it then.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- Seer's Village, first visit: seer.rs2's `started` branch.
        t.exec("goto-seer1", t.player.goto_tile, 2716, 3485, 0)
        t.exec("talk.seer1", t.player.talk_to, "seer", 1)
        t.exec("talk.seer1-dialog", t.chat.play, {
            "npc:Many greetings.",
            "choose:I need to locate some scorpions.",
            "player:I need to locate some scorpion",
            "npc:Well you have come to the righ",
            "npc:Do you need to locate any part",
            "player:I'm looking for some lesser Kh",
            "npc:Let me look into my looking gl",
            "npc:I can see a scorpion that you ",
            "npc:The scorpion seems to be going",
            "npc:Well see if you can find that ",
        })
        t.expect("quest.stage.first_hint", t.quest.expect_stage("first_hint"))

        -- Taverley Dungeon: questscorpiona sits right at its own *.spawn tile
        -- (m44_153.spawn), no gate between it and the ladder down.
        t.exec("goto-scorpiona", t.player.goto_tile, 2877, 9796, 0)
        local scorpion_a, scorpion_a_result = t.player.by_symbol("npc", "questscorpiona")
        t.step("lookup.scorpiona", scorpion_a_result == "ok" and "PASS" or "FAIL",
            "by_symbol npc questscorpiona -> " .. tostring(scorpion_a_result))
        local cage_before_a = select(2, t.inv.count("scorpioncageempty"))
        t.exec("catch.scorpiona", t.player.use_on, "scorpioncageempty", scorpion_a)
        -- [opnpcu,questscorpiona] -> ~scorpcatcher_catch_scorpion: swaps the
        -- held cage for scorpioncagea and deletes the npc. use_on's own `ok`
        -- only proves the click landed, so read the inventory back (trap: ok
        -- is not proof the thing the row is named after happened).
        local cage_a_count = select(2, t.inv.count("scorpioncagea"))
        t.check("catch.scorpiona.inv", cage_a_count == 1,
            "scorpioncageempty " .. tostring(cage_before_a) .. " -> scorpioncagea " .. tostring(cage_a_count))

        -- The secret wall (scorpcatcher_scorpions.rs2's [oploc1,scorpionwall]):
        -- flavour content this quest's own script authored, not a gate.
        t.exec("search.oldwall", t.player.click_loc, "scorpionwall", 1)

        -- Seer's Village, second visit: seer.rs2's has_a branch (bumps to
        -- second_hint and names where the other two scorpions went).
        t.exec("goto-seer2", t.player.goto_tile, 2716, 3485, 0)
        t.exec("talk.seer2", t.player.talk_to, "seer", 1)
        t.exec("talk.seer2-dialog", t.chat.play, {
            "npc:Many greetings.",
            "choose:I've retrieved the scorpion from near the spiders.",
            "player:I've retrieved the scorpion fr",
            "npc:Well, I've checked my looking g",
            "npc:That's all I can tell you about",
            "player:Any more scorpions?",
            "npc:It's good that you should ask.",
            "npc:It seems to be in some sort of ",
        })
        t.expect("quest.stage.second_hint", t.quest.expect_stage("second_hint"))

        -- Edgeville Monastery: questscorpionc, at its own *.spawn tile
        -- (m47_54.spawn), plane 1.
        t.exec("goto-scorpionc", t.player.goto_tile, 3058, 3488, 1)
        local scorpion_c, scorpion_c_result = t.player.by_symbol("npc", "questscorpionc")
        t.step("lookup.scorpionc", scorpion_c_result == "ok" and "PASS" or "FAIL",
            "by_symbol npc questscorpionc -> " .. tostring(scorpion_c_result))
        t.exec("catch.scorpionc", t.player.use_on, "scorpioncagea", scorpion_c)
        -- ~scorpcatcher_next_cage(a, catch_c) -> scorpioncageac.
        local cage_ac_count = select(2, t.inv.count("scorpioncageac"))
        t.check("catch.scorpionc.inv", cage_ac_count == 1, "scorpioncagea -> scorpioncageac " .. tostring(cage_ac_count))

        -- Barbarian Outpost: questscorpionb, at its own *.spawn tile
        -- (m39_55.spawn).
        t.exec("goto-scorpionb", t.player.goto_tile, 2552, 3570, 0)
        local scorpion_b, scorpion_b_result = t.player.by_symbol("npc", "questscorpionb")
        t.step("lookup.scorpionb", scorpion_b_result == "ok" and "PASS" or "FAIL",
            "by_symbol npc questscorpionb -> " .. tostring(scorpion_b_result))
        t.exec("catch.scorpionb", t.player.use_on, "scorpioncageac", scorpion_b)
        -- ~scorpcatcher_next_cage(ac, catch_b) -> scorpioncagefull, all three caught.
        local cage_full_count = select(2, t.inv.count("scorpioncagefull"))
        t.check("catch.scorpionb.inv", cage_full_count == 1, "scorpioncageac -> scorpioncagefull " .. tostring(cage_full_count))

        -- Reward snapshot before hand-in. quest_scorpcatcher's own
        -- ~quest_complete_rewards(quest_scorpioncatcher, "", coins) call
        -- passes an empty rewards string and grants no skill xp and no
        -- coins -- the only reward is the 1 quest point, already covered by
        -- quest.expect_complete()'s own quest.points row below.
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))

        -- Back to Thormac: [opnpc1,thormac]'s cagefull & stage != complete
        -- branch queues scorpcatcher_quest_complete.
        t.exec("goto-thormac2", t.player.goto_tile, 2702, 3405, 3)
        t.exec("talk.thormac2", t.player.talk_to, "thormac", 1)
        t.exec("talk.thormac2-dialog", t.chat.play, {
            "npc:How goes your quest?",
            "player:I have retrieved all your scorpions.",
            "npc:Aha, my little scorpions home at",
        })
        t.ticks(3) -- the queued scorpcatcher_quest_complete is not client-side yet

        t.quest.expect_complete()

        t.finish(0)
    end,
}
