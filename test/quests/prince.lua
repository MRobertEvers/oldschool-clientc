-- Prince Ali Rescue -- REAUTHORED (RETRY after b44ce7a2d), not resumed.
--
-- The previous committed file proved the quest wholly BLOCKED: Hassan,
-- Osman's own princequest switch, Lady Keli, Joe and Prince Ali all had
-- zero live *.spawn rows, so acceptance itself was unreachable. Two fixes
-- landed since:
--   * OSRS-Content 033d83f61f added quest_prince/configs/quest_prince.spawn
--     (hassan 3302,3163,0 / joe 3123,3245,0 / prince_ali_prison 3123,3242,0
--     / lady_keli 3128,3244,0) -- all four now spawn.
--   * osman.rs2's princequest switch moved into [label,osman_talk], reached
--     from contact_osman.rs2's own [opnpc1,contact_osman_multi] once
--     %contact < ^contact_met_maisa (docs/QUEST_AUTHORING.md trap 19) --
--     Osman's princequest lines are reachable now too.
-- So this file drives the quest all the way through to hand-in, rather
-- than reasserting seams that no longer exist.
--
-- Items from Quest Helper's getItemRequirements() (PrinceAliRescue.java) --
-- everything the player BRINGS, never a step the quest itself walks you
-- through -- given in setup, never cheated mid-run (trap 16):
--   softClay, ballsOfWool3(3), yellowDye, redberries, ashes, bucketOfWater,
--   potOfFlour, bronzeBar, pinkSkirt, beers3(3), rope, coins100.
-- Everything else (plainwig/blondwig, keyprint, princeskey, skinpaste) is
-- the quest's own deliverable and is obtained through real clicks below:
-- Ned makes the wig from the 3 balls of wool, it is dyed with the yellow
-- dye already carried, Aggie mixes the paste from the carried ingredients,
-- Lady Keli's key-print branch touches the carried soft clay, and the
-- bronze bar is smelted with the print at a furnace (the wiki's 14 Jan
-- 2026 change -- quest_prince.rs2's [label,prince_make_key], dispatched
-- from smelting.rs2's [label,use_furnace] case keyprint).
--
-- The [oplocu,alidoor]/[oploc1,alidoor] door unlock is pure navigation --
-- prince_rescue's own hand-in tests inventory items only, never a door or
-- lever state -- so it is skipped for a goto_tile straight to the cell
-- (docs/QUEST_AUTHORING.md section 8's "goto_tile past a scripted door
-- whose hand-in reads no door state" rule), the same way this suite treats
-- Ernest the Chicken's lever maze.

return {
    id = "prince",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::give softclay 1",     -- bring-along: Lady Keli's key-print step
        "::give ball_of_wool 3", -- bring-along: Ned's wig (ballsOfWool3)
        "::give yellowdye 1",    -- bring-along: dyeing the wig
        "::give redberries 1",   -- bring-along: Aggie's skin paste
        "::give pot_flour 1",    -- bring-along: Aggie's skin paste
        "::give bucket_water 1", -- bring-along: Aggie's skin paste
        "::give ashes 1",        -- bring-along: Aggie's skin paste
        "::give bronze_bar 1",   -- bring-along: smelting the key print
        "::give pink_skirt 1",   -- bring-along: the disguise (Varrock's Fancy Clothes Store sells it; no in-quest step makes one)
        "::give beer 3",         -- bring-along: getting Joe drunk (beers3)
        "::give rope 1",         -- bring-along: tying up Lady Keli
        "::give coins 100",      -- bring-along: coins100 (spare; this run's happy path never spends any)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "princequest",
            constants = {
                not_started = 0,
                started = 10,
                spoken_osman = 20,
                prep_finished = 30,
                guard_drunk = 40,
                tied_keli = 50,
                saved = 100,
                complete = 110,
                questpoints = 3,
                keymade = 1,
                keyclaimed = 2,
            },
            row = "quest_princealirescue",
            display = "Prince Ali Rescue",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        local setup_ok, setup_detail = t.inv.await_all({
            softclay = 1,
            ball_of_wool = 3,
            yellowdye = 1,
            redberries = 1,
            pot_flour = 1,
            bucket_water = 1,
            ashes = 1,
            bronze_bar = 1,
            pink_skirt = 1,
            beer = 3,
            rope = 1,
            coins = 100,
        }, 10)
        t.check("setup.items", setup_ok == "ok",
            "inv.await_all(setup items) -> " .. tostring(setup_ok) .. " " .. tostring(setup_detail))

        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ------------------------------------------------------- Hassan: accept
        -- hassan.rs2:2-10, quest_prince.spawn's own row (3302,3163,0).
        t.exec("goto-hassan", t.player.goto_tile, 3302, 3163, 0)
        t.ticks(2)
        t.exec("hassan.talk", t.player.talk_to, "hassan", 1)
        t.exec("hassan.accept", t.chat.play, {
            "npc:Greetings I am Hassan",
            "choose:Can I help you? You must need some help here in the desert.",
            "player:Can I help you? You must need some help here in the desert.",
            "npc:I need the services of someone",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ------------------------------------------------------- Osman: instructions
        -- osman.rs2's [label,osman_talk] (reached now through contact_osman.rs2's
        -- own hand-back), contact_osman_multi's spawn (m51_49.spawn, 3286,3180,0).
        t.exec("goto-osman", t.player.goto_tile, 3286, 3180, 0)
        t.ticks(2)
        t.exec("osman.talk", t.player.talk_to, "osman", 1)
        t.exec("osman.instructions", t.chat.play, {
            "player:The chancellor trusts me",
            "npc:Our prince is captive by the Lady Keli",
            "choose:What is the first thing I must do?",
            "player:What is the first thing I must do?",
            "npc:guarded by some stupid guards",
            "npc:tie her up. One coil of rope",
            "player:How good must the disguise be?",
            "npc:fool the guards at a distance",
            "npc:Get a blonde wig, too",
            "npc:My daughter and top spy, Leela",
            "npc:near Draynor Village",
            "choose:What is the second thing you need?",
            "player:What is the second thing you need?",
            "npc:We need the key, or we need a copy made",
            "npc:convince Lady Keli to show it to you",
            "npc:Bring the imprint to me, with a bar of bronze",
            "choose:Okay, I better go find some things.",
            "player:Okay, I had better go find some things.",
            "npc:May good luck travel with you",
        })
        t.expect("quest.stage.spoken_osman", t.quest.expect_stage("spoken_osman"))

        -- ------------------------------------------------------- Ned: the wig
        -- ned.rs2:20-43/64-74/138-158, areas/world/configs/m48_50.spawn.
        t.exec("goto-ned", t.player.goto_tile, 3100, 3258, 0)
        t.ticks(2)
        t.exec("ned.talk", t.player.talk_to, "ned", 1)
        t.exec("ned.wig", t.chat.play, {
            "npc:me friends call me Ned",
            "choose:Ned, could you make other things from wool?",
            "player:Ned, could you make other things from wool?",
            "npc:Aye, that I can.",
            "choose:How about some sort of wig?",
            "player:How about some sort of wig?",
            "npc:Give me 3 balls of wool",
            "choose:I have that now. Please, make me a wig.",
            "player:I have that now. Please, make me a wig.",
            "mesbox:You hand Ned 3 balls of wool",
            "mesbox:Ned gives you a pretty good wig",
            "npc:There you go, that should fool anyone",
        })
        local wig_have_result = t.inv.await("plainwig", 1, 10)
        t.check("ned.wig.have", wig_have_result == "ok",
            "inv.await(plainwig,1) after Ned -> " .. tostring(wig_have_result))

        -- Dye it blonde: quest_prince.rs2's [opheldu,plainwig] fires on
        -- last_useitem=yellowdye -- plainwig is the armed half.
        t.exec("wig.dye", t.player.use_item_on_item, "plainwig", "yellowdye")
        local dyed_result = t.inv.await("blondwig", 1, 10)
        t.check("wig.dyed", dyed_result == "ok",
            "inv.await(blondwig,1) after dyeing -> " .. tostring(dyed_result))

        -- ------------------------------------------------------- Aggie: the paste
        -- aggie.rs2's princequest-gated 5th option, areas/world/configs/m48_50.spawn.
        t.exec("goto-aggie", t.player.goto_tile, 3086, 3259, 0)
        t.ticks(2)
        t.exec("aggie.talk", t.player.talk_to, "aggie", 1)
        t.exec("aggie.paste", t.chat.play, {
            "npc:What can I help you with?",
            "choose:Could you think of a way to make skin paste?",
            "player:Could you think of a way to make skin paste?",
            "npc:I see you already have the ingredients",
            "choose:Yes please. Mix me some skin paste.",
            "player:Yes please. Mix me some skin paste.",
            "npc:That should be simple",
            "mesbox:You hand the ash, flour, water and redberries",
            "npc:Tourniquet, Fenderbaum",
            "mesbox:Aggie hands you the skin paste",
            "npc:There you go dearie",
        })
        local paste_result = t.inv.await("skinpaste", 1, 10)
        t.check("aggie.paste.have", paste_result == "ok",
            "inv.await(skinpaste,1) after Aggie -> " .. tostring(paste_result))

        -- ------------------------------------------------------- Lady Keli: key print
        -- lady_keli.rs2, quest_prince.spawn's own row (3128,3244,0). The
        -- softclay-gated "touch the key" option only appears while
        -- %princequest = ^prince_spoken_osman exactly, which still holds.
        t.exec("goto-keli", t.player.goto_tile, 3128, 3244, 0)
        t.ticks(2)
        t.exec("keli.talk", t.player.talk_to, "lady_keli", 1)
        t.exec("keli.keyprint", t.chat.play, {
            "player:Are you the famous Lady Keli",
            "npc:I am Keli, you have heard of me",
            "choose:Heard of you? You are famous in RuneScape!",
            "player:The great Lady Keli, of course I have heard of you",
            "npc:That's very kind of you to say",
            "choose:What is your latest plan then?",
            "player:What is your latest plan then?",
            "npc:I can tell you I have a valuable prisoner",
            "npc:I can expect a high reward",
            "choose:Can you be sure they will not try to get him out?",
            "player:Can you be sure they will not try",
            "npc:There is no way to release him",
            "npc:There is not another key",
            "choose:Could I see the key please?",
            "player:Could I see the key please?",
            "npc:As you put it that way",
            "mesbox:Keli shows you a small key",
            "choose:Could I touch the key for a moment?",
            "player:Could I touch the key a moment please?",
            "npc:Only for a moment then.",
            "mesbox:You put a piece of your soft clay",
            "player:Thank you so much, you are too kind",
            "npc:You are welcome, run along now",
        })
        local keyprint_result = t.inv.await("keyprint", 1, 10)
        t.check("keli.keyprint.have", keyprint_result == "ok",
            "inv.await(keyprint,1) after Keli -> " .. tostring(keyprint_result))

        -- ------------------------------------------------------- Furnace: forge the key
        -- smelting.rs2's [label,use_furnace] case keyprint -> quest_prince.rs2's
        -- [label,prince_make_key] -- the wiki's 14 Jan 2026 change (player
        -- smelts it themselves, no longer handed to Osman). "furnace" at
        -- Quest Helper's own WorldPoint (3227,3256,0) answers loc_near
        -- not_found (no *.loc placement file exists to check the real tile
        -- against, trap 20) -- dwarf_keldagrim_furnace is a confirmed,
        -- locatable furnace (betweenarock.lua's own smeltCannonball step),
        -- and smelting.rs2's [label,use_furnace] dispatches on last_useitem
        -- alone, not on which furnace symbol was clicked.
        t.exec("goto-furnace", t.player.goto_tile, 2869, 10202, 0)
        local furnace_locate_result, furnace_target = t.world.loc_near("dwarf_keldagrim_furnace", 60)
        t.check("furnace.locate", furnace_locate_result == "ok",
            "world.loc_near(dwarf_keldagrim_furnace,60) -> " .. tostring(furnace_locate_result))
        t.exec("furnace.smelt", t.player.use_on, "keyprint", furnace_target)
        local key_result = t.inv.await("princeskey", 1, 10)
        t.check("furnace.key.have", key_result == "ok",
            "inv.await(princeskey,1) after smelting -> " .. tostring(key_result))

        -- ------------------------------------------------------- Leela: prep finished
        -- leela.rs2's [label,leela_help] -- all four items held while
        -- %princequest = ^prince_spoken_osman advances it to prep_finished
        -- in the same click, areas/world/configs/m48_50.spawn (3113,3263,0).
        t.exec("goto-leela", t.player.goto_tile, 3113, 3263, 0)
        t.ticks(2)
        t.exec("leela.talk", t.player.talk_to, "leela", 1)
        t.exec("leela.prep", t.chat.play, {
            "npc:Good, you have all the basic equipment",
        })
        t.expect("quest.stage.prep_finished", t.quest.expect_stage("prep_finished"))

        -- ------------------------------------------------------- Joe: three beers
        -- joe.rs2's [label,joe_distract]/[label,joe_beer], quest_prince.spawn (3123,3245,0).
        t.exec("goto-joe", t.player.goto_tile, 3123, 3245, 0)
        t.ticks(2)
        t.exec("joe.talk", t.player.talk_to, "joe", 1)
        t.exec("joe.beer", t.chat.play, {
            "choose:I have some beer here, fancy one?",
            "player:I have some beer here, fancy one?",
            "npc:that would be lovely",
            "player:it must be tough being here without a drink",
            "mesbox:You hand a beer to the guard",
            "npc:That was perfect",
            "player:How are you? Still ok?",
            "player:Would you care for another",
            "npc:I better not",
            "player:Here, just keep these for later",
            "mesbox:You hand two more beers",
            "npc:Franksh, that wash just what I need",
            "mesbox:The guard is drunk",
        })
        t.expect("quest.stage.guard_drunk", t.quest.expect_stage("guard_drunk"))

        -- ------------------------------------------------------- Tie up Lady Keli
        -- quest_prince.rs2's [opnpcu,lady_keli] -- rope on Keli, npc_del's her.
        t.exec("goto-keli-tie", t.player.goto_tile, 3128, 3244, 0)
        t.ticks(2)
        local keli_tie_target = t.player.by_symbol("npc", "lady_keli")
        t.exec("keli.tie", t.player.use_on, "rope", keli_tie_target)
        -- The mesbox SUSPENDS the [opnpcu,lady_keli] branch (trap 22) --
        -- npc_del and the princequest write both sit AFTER it, so it must
        -- be dismissed with a real continue, not just closed.
        t.exec("keli.tie.dismiss", t.chat.continue_, true)
        t.ticks(1)
        t.expect("quest.stage.tied_keli", t.quest.expect_stage("tied_keli"))

        -- ------------------------------------------------------- Free the prince
        -- alidoor is pure navigation here (prince_rescue's own hand-in tests
        -- inventory items only, never door/lever state) -- goto_tile straight
        -- to the cell, quest_prince.spawn's prince_ali_prison row (3123,3242,0).
        t.exec("goto-prince-cell", t.player.goto_tile, 3123, 3242, 0)
        t.ticks(2)
        t.exec("prince.talk", t.player.talk_to, "prince_ali_prison", 1)
        t.exec("prince.rescue", t.chat.play, {
            "player:Prince, I come to rescue you",
            "npc:That is very very kind of you",
            "player:With a disguise. I have removed the Lady Keli",
            "player:Take this disguise, and this key",
            "mesbox:You hand over the disguise and key",
            "npc:Thank you my friend, I must leave you now",
            "player:Go to Leela, she is close to here",
            "mesbox:The prince has escaped, well done",
        })
        t.expect("quest.stage.saved", t.quest.expect_stage("saved"))

        -- ------------------------------------------------------- Hand in to Hassan
        local reward_coins_before_result, reward_coins_before = t.inv.count("coins")

        t.exec("goto-hassan-return", t.player.goto_tile, 3302, 3163, 0)
        t.ticks(2)
        t.exec("hassan.return.talk", t.player.talk_to, "hassan", 1)
        t.exec("hassan.return.reward", t.chat.play, {
            "npc:You have the eternal gratitude of the Emir",
        })
        t.ticks(3) -- queue(prince_complete) is queued behind this page, not synchronous

        t.quest.expect_complete()

        local reward_coins_after_result, reward_coins_after = t.inv.count("coins")
        t.check("reward.coins", reward_coins_before_result == "ok" and reward_coins_after_result == "ok"
            and reward_coins_after == reward_coins_before + 700,
            string.format("coins %s -> %s (want +700), reads %s/%s",
                tostring(reward_coins_before), tostring(reward_coins_after),
                tostring(reward_coins_before_result), tostring(reward_coins_after_result)))

        t.finish(0)
    end,
}
