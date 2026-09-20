-- Sheep Shearer quest test: shear and spin quest with bought-wool walkthrough.
-- Scaffolded from Quest Helper's helpers/quests/sheepshearer/ docs/QUEST_SUITE_KIT.md phase 3.
-- Tier (quest_inventory.tsv): 1.
-- Rewards: 150 Crafting XP + 60 coins + 1 quest point (base reward for bought wool).
-- Setup always starts with ::clearinv: the fresh character carries fourteen
-- slots of tutorial kit that block a non-stackable requirement fitting in the backpack.
--
-- Fixture start: fresh_lumbridge.ini stands the player at 3206,3233,0 (Lumbridge, beside
-- Hans). Every step below carries its own Quest Helper WorldPoint; the
-- FIRST emitted t.player.goto_tile is what actually leaves that tile.
--
-- Three rules the first pilot pass broke -- read before touching this file:
-- (a) "blocked" means a t.blocked("...") row followed by return -- a file
--     that runs on to expect_complete() after a failure is rejected.
-- (b) a talk_to/click answering screen_position or not_visible means you
--     are not standing near the target -- fix the goto, not the verb.
-- (c) never add a fixture or a helper file -- this quest file is the only
--     file you edit.

return {
    id = "sheep",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::sheep",
        "::give ball_of_wool 20",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "sheep",
            constants = {
                complete = 22,
                last_wool = 20,
                not_started = 0,
                questpoints = 1,
                started = 1,
            },
            row = "quest_sheepshearer",
            display = "Sheep Shearer",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.exec("goto-fred", t.player.goto_tile, 3189, 3273, 0) -- Fred's location

        -- Reward snapshot before the quest completion
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        local reward_coins_before_result, reward_coins_before = t.inv.count("coins")

        -- Accept the quest from Fred
        t.exec("fred.talk", t.player.talk_to, "fred_the_farmer", 1)
        t.exec("fred.accept", t.chat.play, {
            "npc:What are you doing on my land?",
            "choose:I'm looking for a quest.",
            "player:I'm looking for a quest.",
            "npc:You're after a quest",
            "npc:My sheep are getting mighty woolly",
            "npc:Bring me 20 balls of wool",
            "choose:Yes okay. I can do that.",
            "player:Yes okay. I can do that.",
            "npc:Ok I'll see you when you have some wool.",
        })

        t.expect("quest.started", t.quest.expect_stage("started"))

        -- Hand in the wool to Fred - this goes through multiple dialogue branches:
        -- 1. Initial greeting: "How are you doing..."
        -- 2. Player offers wool: "I have some."
        -- 3. Fred takes the wool (19 balls, with auto-message)
        -- 4. Final hand-in: "I have your last ball of wool."
        t.exec("fred.handin", t.player.talk_to, "fred_the_farmer", 1)
        t.exec("fred.handin.chat", t.chat.play, {
            "npc:How are you doing getting those balls of wool?",
            "player:I have some.",
            "npc:Give 'em here then.",
            "player:I have your last ball of wool.",
            "npc:I guess I'd better pay you then.",
        })

        t.ticks(3)  -- quest completion is async (see docs)
        t.quest.expect_complete()

        -- Reward checks -- assert the literal reward values.
        -- Base reward is 150 XP (not 200), which proves this is the bought-wool
        -- walkthrough with no self-spun bonus (200 XP is only for spinning all 20 yourself).
        t.check("reward.crafting", t.skill.expect_gain("crafting", 150, reward_before))
        local reward_coins_after_result, reward_coins_after = t.inv.count("coins")
        t.check("reward.coins", reward_coins_before_result == "ok" and reward_coins_after_result == "ok" and reward_coins_after == reward_coins_before + 60, string.format("coins %s -> %s (want +60), reads %s/%s", tostring(reward_coins_before), tostring(reward_coins_after), tostring(reward_coins_before_result), tostring(reward_coins_after_result)))

        t.finish(0)
    end,
}
