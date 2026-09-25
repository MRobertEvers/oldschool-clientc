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
--      eta_varrock -- the real Chaos Temple offer tree (Transcript:
--      Enter_the_Abyss oldid=15263217, content parity fc44e385e0): top
--      menu -> "Where do you get your runes from?" -> care-to-share 4-way
--      -> "Maybe I could make it worth your while?" (mercenary branch) ->
--      "Yes, but I can still help you as well." -> Deal/No deal/think ->
--      "Deal." grants an empty scrying orb and sets
--      %abyssal_miniquest = eta_orb. (The Saradomin-refusal,
--      not-interested, loyal-branch and rat-walk options are dead ends
--      the transcript itself marks as such; this file drives the one real
--      path through to the deal, same as the Wilderness mage's own
--      "Alright, I'll go." choice above.)
--   3. Teleport to the Rune Essence from three distinct NPCs while
--      carrying the orb (aubury 3253,3402,0; head_wizard 3103,9571,0;
--      ardounge_wizard 2683,3326,0) -- ~eta_charge_orb marks one essence
--      spot per distinct source and converts the orb on the third.
--   4. rcu_zammy_mage1_edge again, stage eta_orb with a full orb carried
--      -- hands it over, %abyssal_miniquest = eta_reward.
--   5. rcu_zammy_mage1_edge a third time, stage eta_reward -- the real
--      orb-handover/reward conversation (@eta_reward_handover), ending at
--      a p_choice3 of two OPTIONAL lore topics (Abyss / Z.M.I., which loop
--      back to the same menu) plus "I'd better be off."; choosing that
--      third option is what calls ~eta_quest_complete (XP + items + stage
--      -> eta_complete, opens the reward scroll).

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
            "npc:Ah, you again. The Wilderness is hardly the appropriate place for a conversation",
            "player:Err... I didn't really want anything.",
            "npc:So why did you approach me?",
            "player:I was just wondering why you sell runes in the Wilderness?",
            "npc:Well I can't go doing it in the middle of Varrock",
            "choose:Where do you get your runes from?",
            "player:Where do you get your runes from?",
            "npc:Well we craft them of course.",
            "player:We?",
            "npc:My associates and I. Despite the best attempts",
            "player:I can't imagine they like you crafting runes much",
            "npc:Ha! I'm sure they'd love to, but we have methods of runecrafting",
            "player:Care to share?",
            "npc:Why would I? You are not a member of our institute",
            "choose:Maybe I could make it worth your while?",
            "player:Maybe I could make it worth your while?",
            "npc:How? What do you have to offer?",
            "player:Well what is it you want?",
            "npc:Until recently, our runecrafting secrets allowed us to produce runes",
            "npc:From what we can gather, they've somehow rediscovered how to access the lost Rune Essence Mine.",
            "player:Ah, well I know all about that. I was actually the one to help them do it!",
            "npc:You did what? You helped the Order of Wizards?",
            "player:Err...",
            "choose:Yes, but I can still help you as well.",
            "player:Yes, but I can still help you as well.",
            "npc:So you're a mercenary with no allegiance?",
            "npc:Alright, if you help us access the Rune Essence Mine, we will share our runecrafting secrets",
            "choose:Deal.",
            "player:Deal.",
            "npc:Good. Now, all I need from you is the spell that will teleport me to the Rune Essence Mine.",
            "player:Err... I don't actually know the spell.",
            "npc:What? Then how do you get there.",
            "player:Oh, well the people who do know the spell just teleport me there directly.",
            "npc:Hmm... I see. That makes this slightly more complex",
            "player:How?",
            "npc:I'll give you a scrying orb with a standard cypher spell cast upon it.",
            "npc:If you teleport to the Rune Essence Mine from three different locations",
            "npc:Do you know of three different people who can teleport you there?",
            "player:Maybe?",
            "npc:Well if not, I'm sure one of those fools in the Order of Wizards can tell you. Now, here's the orb.",
        })
        t.ticks(1) -- let the granted orb's inv update land before reading it
        local orb_empty_result, orb_empty_count = t.inv.count("scrying_orb_empty")
        t.check("gather.orb_granted", orb_empty_result == "ok" and orb_empty_count == 1,
            string.format("scrying_orb_empty count=%s (read %s)", tostring(orb_empty_count), tostring(orb_empty_result)))
        t.expect("quest.stage.orb", t.quest.expect_stage("orb"))

        -- ---- 3. Charge the orb: teleport to the Rune Essence from three
        -- distinct sources while carrying it (~eta_charge_orb). Each source's
        -- ~teleport_to_essence_mine closes its own dialogue with if_close,
        -- THEN runs a curse animation (p_delay(4)) before ~eta_charge_orb and
        -- the actual p_telejump to a mine tile (essence_mine.rs2) -- so the
        -- teleport lands several ticks AFTER the chat.play list returns
        -- (docs sec 2's Abyss note: this is the exact case it names). Firing
        -- the next goto_tile before that teleport resolves let the delayed
        -- jump land ON TOP of it, so the click after landed on whatever
        -- terrain the race left on screen -- read as an unrelated-scenery
        -- mis-click in earlier runs of this file, never a real click_minimenu
        -- bug. Fix: await the player's tile actually leaving the npc's tile
        -- (arrival in the essence mine, region 45,75 -- far from all three
        -- npcs, so the predicate is false at the moment of the click and can
        -- genuinely fail) before touching the next goto_tile.
        local charged_spots = {}

        t.exec("goto-talkToAubury", t.player.goto_tile, 3253, 3402, 0)
        local aubury_talk_result, aubury_talk_detail = t.player.talk_to("aubury", 1)
        if aubury_talk_result == "ok" then
            t.exec("talkToAubury-dialog", t.chat.play, {
                "npc:Do you want to buy some runes?",
                "choose:Can you teleport me to the Rune Essence?",
                "player:Can you teleport me to the Rune Essence?",
            })
            local aubury_arrive_result = t.await({
                level = function()
                    local r, tile = t.world.tile()
                    return r == "ok" and tile ~= nil
                        and (math.abs(tile.x - 3253) > 200 or math.abs(tile.z - 3402) > 200)
                end,
                note = "arrival in the essence mine after Aubury's teleport",
            }, 10)
            local aubury_tile_result, aubury_tile_now = t.world.tile()
            t.check("await-essence-aubury", aubury_arrive_result == "ok",
                "await arrival -> " .. tostring(aubury_arrive_result) .. "; t.world.tile() -> "
                .. tostring(aubury_tile_result) .. " " .. tostring(aubury_tile_now and aubury_tile_now.x)
                .. "," .. tostring(aubury_tile_now and aubury_tile_now.z))
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
            local cromperty_arrive_result = t.await({
                level = function()
                    local r, tile = t.world.tile()
                    return r == "ok" and tile ~= nil
                        and (math.abs(tile.x - 2683) > 200 or math.abs(tile.z - 3326) > 200)
                end,
                note = "arrival in the essence mine after Cromperty's teleport",
            }, 10)
            local cromperty_tile_result, cromperty_tile_now = t.world.tile()
            t.check("await-essence-ardounge_wizard", cromperty_arrive_result == "ok",
                "await arrival -> " .. tostring(cromperty_arrive_result) .. "; t.world.tile() -> "
                .. tostring(cromperty_tile_result) .. " " .. tostring(cromperty_tile_now and cromperty_tile_now.x)
                .. "," .. tostring(cromperty_tile_now and cromperty_tile_now.z))
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
            local sedridor_arrive_result = t.await({
                level = function()
                    local r, tile = t.world.tile()
                    return r == "ok" and tile ~= nil
                        and (math.abs(tile.x - 3103) > 200 or math.abs(tile.z - 9571) > 200)
                end,
                note = "arrival in the essence mine after Sedridor's teleport",
            }, 10)
            local sedridor_tile_result, sedridor_tile_now = t.world.tile()
            t.check("await-essence-head_wizard", sedridor_arrive_result == "ok",
                "await arrival -> " .. tostring(sedridor_arrive_result) .. "; t.world.tile() -> "
                .. tostring(sedridor_tile_result) .. " " .. tostring(sedridor_tile_now and sedridor_tile_now.x)
                .. "," .. tostring(sedridor_tile_now and sedridor_tile_now.z))
            charged_spots[#charged_spots + 1] = "head_wizard"
        end

        t.check("charge.spots", #charged_spots == 3,
            string.format("%d of 3 essence-teleport clicks landed (%s). "
                .. "aubury -> %s: %s | ardounge_wizard -> %s: %s | head_wizard -> %s: %s.",
                #charged_spots, table.concat(charged_spots, ","),
                tostring(aubury_talk_result), tostring(aubury_talk_detail),
                tostring(cromperty_talk_result), tostring(cromperty_talk_detail),
                tostring(sedridor_talk_result), tostring(sedridor_talk_detail)))
        if #charged_spots < 3 then
            t.blocked(string.format(
                "entertheabyss.rs2 ~eta_charge_orb needs 3 distinct essence-teleport NPCs "
                .. "carrying the scrying orb; only %d of 3 clicks landed this run (%s), even "
                .. "with the essence-mine-arrival await ahead of each goto_tile.",
                #charged_spots, table.concat(charged_spots, ",")))
            return
        end

        -- eta_charge_orb converts the orb on the third distinct spot and
        -- prints this system line (mes(), not a dialogue page) DURING
        -- ~teleport_to_essence_mine's own p_delay(4), before the p_telejump
        -- that the await-essence-head_wizard row above already waited out --
        -- so the line is already in the ring by here, not one still to
        -- arrive: t.msg.expect (any line still in the ring), never
        -- t.msg.await, which only matches a line newer than the call and so
        -- can never see one that landed during an earlier await (docs sec 8).
        t.expect("gather.orb_charged", t.msg.expect("absorbed enough teleport information"))
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
            "player:Here you go.",
            "mesbox:You hand the orb to the Mage of Zamorak.",
            "npc:Right, let's take a look at this orb",
            "npc:Yes, this will do nicely. Once again, the Zamorak Magical Institute has overcome the Order of Wizards!",
            "npc:You have done well. Now, time for us to uphold our end of the bargain.",
            "npc:The reason we are able to craft so many runes is because we do not visit the runic altars",
            "player:How?",
            "npc:Via another plane known as the Abyss.",
            "player:So can I use the Abyss?",
            "npc:Yes. Visit me in the Wilderness whenever you wish to be teleported there.",
            "player:How is it dangerous?",
            "npc:There are creatures there that will hunt and attack any visitors on sight.",
            "player:What do you mean?",
            "npc:Just don't expect to be using any prayers in there.",
            "npc:Anyway, you may also have this pouch as well. I'm sure you will find it useful. Now, we're done here.",
            "choose:I'd better be off.",
            "player:I'd better be off.",
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
