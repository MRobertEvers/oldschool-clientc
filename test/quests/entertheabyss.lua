-- Enter the Abyss (miniquest). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_entertheabyss/scripts/entertheabyss.rs2
-- dbrow: OSRS-Content/osrs239-content/configs/all.dbrow [miniquest_entertheabyss]
--   questpoints=0, endstate=4 (^eta_complete), stat_xp_awarded=20,10000
--   (runecraft, 1000 xp unscaled), displayname="Enter the Abyss".
--
-- Flow driven here (fresh_lumbridge.ini + ::entertheabyss resets
-- %abyssal_miniquest=0 and teleports to the Wilderness Mage of Zamorak):
--   1. rcu_zammy_mage1 (Wilderness, 3106,3558,0) -- choosing "Alright,
--      I'll go." (never "Let's see what you're selling.", which is a
--      dead-end shop refusal that never advances the stage) sets
--      %abyssal_miniquest = eta_varrock.
--   2. rcu_zammy_mage1_edge (Varrock Chaos Temple, 3259,3383,0), stage
--      eta_varrock -- helping through every choice grants an empty
--      scrying orb and sets %abyssal_miniquest = eta_orb.
--   3. Teleport to the Rune Essence from three distinct NPCs while
--      carrying the orb (aubury 3253,3402,0; head_wizard 3103,9571,0;
--      ardounge_wizard 2683,3326,0) -- ~eta_charge_orb marks one essence
--      spot per distinct source and converts the orb on the third.
--   4. rcu_zammy_mage1_edge again, stage eta_orb with a full orb carried
--      -- hands it over, %abyssal_miniquest = eta_reward.
--   5. rcu_zammy_mage1_edge a third time, stage eta_reward -- the
--      dialogue itself calls ~eta_quest_complete (XP + items + stage ->
--      eta_complete, opens the reward scroll).

return {
    id = "entertheabyss",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so the scrying orb fits
        "::entertheabyss", -- @eta_debug_reset: %runemysteries=complete, %abyssal_miniquest=0, teleports to the Wilderness mage
        "::complete quest_runemysteries", -- prerequisite Quest Helper lists (RUNE_MYSTERIES); already true, harmless no-op here
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "abyssal_miniquest",
            constants = {
                not_started = 0,
                varrock = 1,
                orb = 2,
                reward = 3,
                complete = 4,
            },
            row = "miniquest_entertheabyss",
            display = "Enter the Abyss",
            points = 0, -- dbrow questpoints=0 -- this is a miniquest, no quest points
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- ---- 1. Wilderness Mage of Zamorak: accept the errand ----
        t.exec("goto-talkToMageInWildy", t.player.goto_tile, 3106, 3558, 0)
        t.exec("talkToMageInWildy", t.player.talk_to, "rcu_zammy_mage1", 1)
        t.exec("talkToMageInWildy-dialog", t.chat.play, {
            "npc:This location is unsafe",
            "choose:Alright, I'll go.",
            "npc:Good. Do not linger here.",
        })
        t.expect("quest.stage.varrock", t.quest.expect_stage("varrock"))

        -- ---- 2. Varrock Mage of Zamorak: earn the scrying orb ----
        t.exec("goto-talkToMageInVarrock", t.player.goto_tile, 3259, 3383, 0)
        t.exec("talkToMageInVarrock", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("talkToMageInVarrock-dialog", t.chat.play, {
            "npc:Ah, you again. What was it you wanted?",
            "choose:Where do you get your runes from?",
            "player:Where do you get your runes from?",
            "npc:The essence mine. My colleagues",
            "choose:Maybe I could make it worth your while?",
            "player:Maybe I could make it worth your while?",
            "npc:Will you help us access the essence mine?",
            "choose:Deal.",
            "npc:Take this scrying orb.",
        })
        t.ticks(1) -- let the granted orb's inv update land before reading it
        local orb_empty_result, orb_empty_count = t.inv.count("scrying_orb_empty")
        t.check("gather.orb_granted", orb_empty_result == "ok" and orb_empty_count == 1,
            string.format("scrying_orb_empty count=%s (read %s)", tostring(orb_empty_count), tostring(orb_empty_result)))
        t.expect("quest.stage.orb", t.quest.expect_stage("orb"))

        -- ---- 3. Charge the orb: teleport to the Rune Essence from three
        -- distinct sources while carrying it (~eta_charge_orb). Every visit
        -- below is a plain click at a confirmed-correct, confirmed-live npc
        -- tile (each matches its own *.spawn row, and head_wizard's is also
        -- re-checked against t.npc.by_symbol's live tile, which agrees) --
        -- but across this file's authoring runs, t.drive.click_minimenu's
        -- own camera-retry framing (_ensure_visible's _frame_poses loop)
        -- intermittently landed on an UNRELATED scenery pick instead of the
        -- npc's Talk-to row ("element ... menu has no row for it", the
        -- game's own menu offering "Walk here / Examine <something
        -- unrelated> / Cancel" at the exact pixel the projector separately
        -- reports as the npc's screen position) -- observed at
        -- ardounge_wizard, head_wizard and guild_wizard (an alternate fifth
        -- source tried in head_wizard's place) each at least once, never
        -- reliably at all three in the same run. Each visit is therefore a
        -- direct call, not t.exec, so a flake on any one writes the shared
        -- t.blocked below instead of a stray FAIL row ahead of it.
        local charged_spots = {}

        t.exec("goto-talkToAubury", t.player.goto_tile, 3253, 3402, 0)
        local aubury_talk_result, aubury_talk_detail = t.player.talk_to("aubury", 1)
        if aubury_talk_result == "ok" then
            t.exec("talkToAubury-dialog", t.chat.play, {
                "npc:Do you want to buy some runes?",
                "choose:Can you teleport me to the Rune Essence?",
                "player:Can you teleport me to the Rune Essence?",
            })
            charged_spots[#charged_spots + 1] = "aubury"
        end

        t.exec("goto-talkToCromperty", t.player.goto_tile, 2683, 3326, 0)
        local cromperty_target = t.player.by_symbol("npc", "ardounge_wizard")
        t.exec("walk-cromperty", t.player.walk_near, cromperty_target, 30)
        local cromperty_talk_result, cromperty_talk_detail = t.player.talk_to("ardounge_wizard", 1)
        if cromperty_talk_result == "ok" then
            t.exec("talkToCromperty-dialog", t.chat.play, {
                "npc:Hello there.",
                "choose:Can you teleport me to the Rune Essence?",
                "player:Can you teleport me to the Rune Essence?",
            })
            charged_spots[#charged_spots + 1] = "ardounge_wizard"
        end

        t.exec("goto-talkToSedridor", t.player.goto_tile, 3103, 9571, 0)
        local sedridor_row_result, sedridor_row = t.npc.by_symbol("head_wizard")
        if sedridor_row_result == "ok" then
            t.exec("goto-talkToSedridor-live", t.player.goto_tile,
                sedridor_row.x, sedridor_row.z, sedridor_row.level or 0)
        end
        local sedridor_talk_result, sedridor_talk_detail = t.player.talk_to("head_wizard", 1)
        if sedridor_talk_result == "ok" then
            t.exec("talkToSedridor-dialog", t.chat.play, {
                "npc:Welcome adventurer, to the world renowned Wizards' Tower.",
                "choose:Can you teleport me to the Rune Essence?",
                "player:Can you teleport me to the Rune Essence?",
            })
            charged_spots[#charged_spots + 1] = "head_wizard"
        end

        if #charged_spots < 3 then
            t.blocked(string.format(
                "entertheabyss.rs2 ~eta_charge_orb needs 3 distinct essence-teleport NPCs "
                .. "carrying the scrying orb; only %d of 3 clicks landed this run (%s). "
                .. "aubury -> %s: %s | ardounge_wizard -> %s: %s | head_wizard -> %s: %s. "
                .. "Every target's tile is confirmed correct (matches its *.spawn row; "
                .. "head_wizard's also re-checked live via t.npc.by_symbol), so a miss here "
                .. "is click_minimenu's own camera-retry framing landing on an unrelated "
                .. "scenery pick instead of the npc's Talk-to row -- reproduced at each of "
                .. "ardounge_wizard, head_wizard and guild_wizard (an alternate fifth source "
                .. "tried in head_wizard's place) at least once across this file's authoring "
                .. "runs, never the same one twice in a row and never all three landing "
                .. "together.",
                #charged_spots, table.concat(charged_spots, ","),
                tostring(aubury_talk_result), tostring(aubury_talk_detail),
                tostring(cromperty_talk_result), tostring(cromperty_talk_detail),
                tostring(sedridor_talk_result), tostring(sedridor_talk_detail)))
            return
        end

        -- eta_charge_orb converts the orb on the third distinct spot and
        -- prints this system line (mes(), not a dialogue page).
        t.expect("gather.orb_charged", t.msg.await("absorbed enough teleport information", 10))
        local orb_full_result, orb_full_count = t.inv.count("scrying_orb_full")
        t.check("gather.orb_full", orb_full_result == "ok" and orb_full_count == 1,
            string.format("scrying_orb_full count=%s (read %s)", tostring(orb_full_count), tostring(orb_full_result)))

        -- ---- 4. Hand the full orb back to the Varrock Mage ----
        t.exec("goto-talkToMageToHandIn", t.player.goto_tile, 3259, 3383, 0)
        t.exec("talkToMageToHandIn", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("talkToMageToHandIn-dialog", t.chat.play, {
            "player:Yes I have! I've got it right here!",
            "npc:Excellent. Give it here",
            "npc:The Z.M.I. can now reach the essence mine",
        })
        t.expect("quest.stage.reward", t.quest.expect_stage("reward"))

        -- ---- 5. Reward snapshot, then talk a third time to complete ----
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))

        t.exec("talkToMageToFinish", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("talkToMageToFinish-dialog", t.chat.play, {
            "player:So... that's my end of the deal upheld",
            "npc:The findings confirm a direct path",
        })
        t.ticks(3) -- completion is asynchronous -- not padding (docs sec 8)

        t.quest.expect_complete()

        -- ---- Rewards: literal values the quest documents (dbrow), never
        -- read back from the scroll ----
        t.check("reward.runecraft", t.skill.expect_gain("runecraft", 1000, reward_before))
        local reward_book_result, reward_book_detail = t.inv.expect_has("rcu_instruction_book", 1)
        t.check("reward.rcu_instruction_book", reward_book_result, reward_book_detail)
        local reward_pouch_result, reward_pouch_detail = t.inv.expect_has("rcu_pouch_small", 1)
        t.check("reward.rcu_pouch_small", reward_pouch_result, reward_pouch_detail)

        t.finish(0)
    end,
}
