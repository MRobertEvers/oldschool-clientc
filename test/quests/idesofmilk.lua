-- The Ides of Milk -- driven for real: Cassius (giver, hand-placed spawn row
-- landed with seam21) -> Gillie -> Seth -> search the shelves for the book ->
-- return the book -> drink the first milk sample -> Cassius sends the second
-- sample to Duke Horacio -> Duke -> back to Gillie, drink the second sample
-- -> Gillie sends the player to open the bull pen -> a REAL fight against
-- Brutus (level 30, quest_inventory.tsv boss_fight=yes, a plain op2 Attack
-- and a stat block -- QUEST_AUTHORING.md section 6's "fought for real" rule,
-- not the skipboss stub the scaffold guessed) -> Gillie -> Cassius to finish
-- -> back to Gillie for the cowbell amulet + magic lamp (the ONLY other
-- reward Quest Helper's getUnlockRewards() lists beyond the quest point).
--
-- Every t.chat.play list below was rebuilt from idesofmilk.rs2/
-- idesofmilk_locs.rs2 branch by branch (trap 17/18): the scaffold's guessed
-- lists all copied [opnpc1,cowboss_farmer]'s FIRST (not-yet-started) branch
-- for every later talk_to on Cassius/Gillie regardless of %cowquest's actual
-- stage, which is wrong for every stage but the first -- each is replaced
-- here with the branch the read stage actually reaches. Duke's page is
-- [proc,iom_duke_quest] (duke_horacio.rs2:18-19 checks %cowquest = ^iom_duke
-- BEFORE the generic Rune Mysteries/dragon-shield tree the scaffold guessed
-- from), so no p_choice is ever reached there.
--
-- Gear/food/stats are the quest's own bring-along prerequisites (trap 16,
-- hero.lua's Ice Queen idiom): Brutus's ordinary swing cannot kill the
-- player during the quest (~cowquest_try_safe_death caps it at 1 hp,
-- idesofmilk.rs2), but his two specials (docs/quests/the_ides_of_milk.md,
-- wiki-sourced: "ignore protection prayers" and "can hit up to 19") are
-- exempt and roll after every 1-6 swings regardless of the player's own
-- attack speed -- max combat stats + a fast weapon minimise the swing count
-- needed to drop his 58 hp (all three melee defences -7) and shark food
-- covers whatever specials land before he does.
return {
    id = "idesofmilk",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::idesofmilk", -- resets %cowquest and teleports to Cassius's hand-placed spawn tile
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give rune_scimitar 1", -- Brutus's defences are all -7 (stab/slash/crush) -- any weapon lands; scimitar's speed minimises swings before he dies
        "::give mithril_platebody 1",
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 10", -- food for whatever Snort/Growl specials land before Brutus's 58 hp runs out
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb20106_cowquest",
            constants = {
                not_started = 0,
                investigate = 3,
                book = 4,
                return_book = 5,
                drink1 = 6,
                cassius_after = 8,
                duke = 10,
                gillie2 = 12,
                drink2 = 14,
                gillie_after = 16,
                fight = 18,
                gillie_end = 20,
                finish = 21,
                complete = 22,
            },
            row = "quest_idesofmilk",
            display = "The Ides of Milk",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3) -- the setup cheats' effects are not client-side yet (trap 23)
        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")

        t.exec("equipScimitar", t.player.equip, "rune_scimitar")
        t.exec("equipPlatebody", t.player.equip, "mithril_platebody")
        t.exec("equipPlatelegs", t.player.equip, "rune_platelegs")
        t.exec("equipFullHelm", t.player.equip, "rune_full_helm")
        t.exec("equipKiteshield", t.player.equip, "rune_kiteshield")

        -- ---------------------------------------------------------------
        -- Starting off: Cassius, by the Lumbridge pond.
        -- ---------------------------------------------------------------
        t.exec("goto-talkToCassius", t.player.goto_tile, 3171, 3277, 0)
        t.exec("talkToCassius", t.player.talk_to, "cowboss_farmer", 1)
        t.exec("talkToCassius-dialog", t.chat.play, {
            "npc:Something's wrong with the cow",
            "choose:Yes.",
            "player:Yes.",
            "npc:Speak to Gillie Groats at the ",
        })
        t.exec("quest.stage.investigate", t.quest.expect_stage, "investigate")

        -- ---------------------------------------------------------------
        -- Investigation: Gillie, then Seth, then the shelves.
        -- ---------------------------------------------------------------
        t.exec("goto-talkToGillie", t.player.goto_tile, 3254, 3274, 0)
        t.exec("talkToGillie", t.player.talk_to, "gillie_the_milkmaid", 1)
        t.exec("talkToGillie-dialog", t.chat.play, {
            "player:Can you share what makes your cows so productive?",
            "npc:Hard work and family secrets!",
        })
        t.exec("inv.gillieInformation", t.var.await, "varb20107_cowquest_gillie_information", 1, 5)

        t.exec("goto-talkToSeth", t.player.goto_tile, 3223, 3293, 0)
        t.exec("talkToSeth", t.player.talk_to, "favour_seth_groats", 1)
        t.exec("talkToSeth-dialog", t.chat.play, {
            "npc:Looking for Groats' book?",
        })
        t.exec("quest.stage.book", t.quest.expect_stage, "book")

        t.exec("goto-searchShelves", t.player.goto_tile, 3227, 3287, 0)
        t.exec("searchShelves", t.player.click_loc, "cowquest_seth_shelf", 1)
        t.exec("inv.husbandryBook", t.inv.await, "cowquest_husbandry_book", 1, 5)
        t.exec("quest.stage.return_book", t.quest.expect_stage, "return_book")

        -- ---------------------------------------------------------------
        -- Milk tasting: return the book, drink sample 1, talk to Cassius.
        -- ---------------------------------------------------------------
        t.exec("goto-returnToCassiusWithBook", t.player.goto_tile, 3171, 3277, 0)
        t.exec("returnToCassiusWithBook", t.player.talk_to, "cowboss_farmer", 1)
        t.exec("returnToCassiusWithBook-dialog", t.chat.play, {
            "player:I found this book.",
            "npc:Fascinating! Here",
        })
        t.exec("quest.stage.drink1", t.quest.expect_stage, "drink1")
        t.exec("inv.milkSample1", t.inv.expect_has, "cowquest_milk_sample_1", 1)

        t.exec("drinkMilkSample1", t.player.inv_op, "cowquest_milk_sample_1", 1)
        t.exec("quest.stage.cassius_after", t.quest.expect_stage, "cassius_after")

        t.exec("talkToCassiusAfterDrink", t.player.talk_to, "cowboss_farmer", 1)
        t.exec("talkToCassiusAfterDrink-dialog", t.chat.play, {
            "npc:Well? How was my milk?",
            "player:Fine... but maybe get a second opinion.",
            "npc:Take this sample to Duke Horacio.",
        })
        t.exec("quest.stage.duke", t.quest.expect_stage, "duke")
        t.exec("inv.milkSample2", t.inv.expect_has, "cowquest_milk_sample_2", 1)

        -- ---------------------------------------------------------------
        -- The Duke's opinion: upstairs in Lumbridge Castle, then back to
        -- Gillie twice (once to hand off, once after drinking sample 2).
        -- goto_tile onto the destination tile WITH its level is the whole
        -- of the staircase leg (QUEST_AUTHORING.md section 2) -- no
        -- click_loc on the stairs.
        -- ---------------------------------------------------------------
        t.exec("goto-talkToDuke", t.player.goto_tile, 3212, 3220, 1)
        t.exec("talkToDuke", t.player.talk_to, "duke_of_lumbridge", 1)
        t.exec("talkToDuke-dialog", t.chat.play, {
            "player:Cassius asked me to bring you this milk sample.",
            "npc:Hmm. Acceptable, but not extraordinary.",
        })
        t.exec("quest.stage.gillie2", t.quest.expect_stage, "gillie2")

        t.exec("goto-talkToGillieAgain", t.player.goto_tile, 3254, 3274, 0)
        t.exec("talkToGillieAgain", t.player.talk_to, "gillie_the_milkmaid", 1)
        t.exec("talkToGillieAgain-dialog", t.chat.play, {
            "npc:The Duke sent you?",
        })
        t.exec("quest.stage.drink2", t.quest.expect_stage, "drink2")

        t.exec("drinkMilkSample2", t.player.inv_op, "cowquest_milk_sample_2", 1)
        t.exec("quest.stage.gillie_after", t.quest.expect_stage, "gillie_after")

        t.exec("talkToGillieAfterDrink", t.player.talk_to, "gillie_the_milkmaid", 1)
        t.exec("talkToGillieAfterDrink-dialog", t.chat.play, {
            "npc:That milk... something's off.",
        })
        t.exec("quest.stage.fight", t.quest.expect_stage, "fight")

        -- ---------------------------------------------------------------
        -- The bull fight: open the pen, then a real fight against Brutus.
        -- ---------------------------------------------------------------
        t.exec("goto-openBullPen", t.player.goto_tile, 3262, 3294, 0)
        t.exec("openBullPen", t.player.click_loc, "fencegate_l_cowboss_start", 1)
        t.exec("openBullPen-dialog", t.chat.play, { "choose:Yes." })
        -- t.npc.await_present is hollow (trap 12: ok with a nil detail) --
        -- call it directly and read the live copy back with t.npc.nearest
        -- so the row carries its own evidence.
        local present_result = t.npc.await_present("cowboss", 12, 10)
        local nearest_result, nearest_row = t.npc.nearest("cowboss", 12)
        t.check("npc.brutusPresent", present_result == "ok" and nearest_result == "ok",
            "await_present=" .. tostring(present_result) .. " nearest=" .. tostring(nearest_result)
                .. " " .. tostring(nearest_row))

        t.exec("killBrutus.engage", t.player.attack, "cowboss", 2, 15)
        local rounds = 0
        local brutus_dead = false
        while rounds < 20 and not brutus_dead do
            rounds = rounds + 1
            local dead_result = t.npc.await_dead_engaged(60, 10)
            t.note("round " .. tostring(rounds) .. " await_dead_engaged: " .. tostring(dead_result))
            if dead_result == "ok" then
                brutus_dead = true
            elseif dead_result == "no_row" then
                -- the stamped engagement is gone -- re-press and keep going.
                t.player.attack("cowboss", 2, 15)
            end
            if rounds % 4 == 0 then
                t.player.inv_op("shark", 1)
            end
        end
        t.check("killBrutus", brutus_dead, "killed Brutus (58 hp) after " .. tostring(rounds) .. " round(s)")
        t.expect("player.aliveAfterBrutus", t.player.alive())
        t.exec("quest.stage.gillie_end", t.quest.expect_stage, "gillie_end")

        -- ---------------------------------------------------------------
        -- Finishing up: Gillie, then Cassius to complete the quest.
        -- ---------------------------------------------------------------
        t.exec("goto-talkToGillieAfterFight", t.player.goto_tile, 3254, 3274, 0)
        t.exec("talkToGillieAfterFight", t.player.talk_to, "gillie_the_milkmaid", 1)
        t.exec("talkToGillieAfterFight-dialog", t.chat.play, {
            "player:Brutus is down.",
            "npc:Thank goodness!",
        })
        t.exec("quest.stage.finish", t.quest.expect_stage, "finish")

        local snapshot_result = t.skill.snapshot()
        t.step("skill.snapshot", snapshot_result == "ok" and "PASS" or "FAIL",
            "snapshot taken before hand-in: " .. tostring(snapshot_result))

        t.exec("goto-finishQuest", t.player.goto_tile, 3171, 3277, 0)
        t.exec("finishQuest", t.player.talk_to, "cowboss_farmer", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:The bull is dealt with.",
            "npc:Splendid work!",
        })

        t.ticks(3) -- completion is asynchronous (section 8) -- let ~iom_quest_complete's scroll mount before reading it
        t.quest.expect_complete()

        -- Quest Helper's getUnlockRewards(): "Access to the cow boss" (no
        -- separate item/varp to assert -- Brutus's own pen is what was just
        -- fought through) and "Cow bell amulet and magic lamp ... from
        -- Gillie Groats", granted by ONE more dialogue with her
        -- (idesofmilk.rs2's gillie_talk, %cowquest >= ^iom_complete &
        -- %cowquest_reward = 0 branch) -- driven for real, not ::given.
        t.exec("goto-collectRewardFromGillie", t.player.goto_tile, 3254, 3274, 0)
        t.exec("collectRewardFromGillie", t.player.talk_to, "gillie_the_milkmaid", 1)
        t.exec("collectRewardFromGillie-dialog", t.chat.play, {
            "npc:For your help",
        })
        t.exec("reward.cowbellAmulet", t.inv.expect_has, "cowbell_amulet", 1)
        t.exec("reward.cowbossRewardLamp", t.inv.expect_has, "cowboss_reward_lamp", 1)

        t.finish(0)
        return
    end,
}
