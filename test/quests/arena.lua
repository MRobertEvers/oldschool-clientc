-- Fight Arena (arena). Quest Helper: helpers/quests/fightarena/FightArena.java.
-- Route (guide == content): Lady Servil starts it; search the shut chest for the
-- Khazard helmet + platemail; wear them; talk to the drunk guard; buy a Khali brew
-- from the barman (5 coins, brought along); give the brew for the cell keys; use the
-- keys on Sammy's cell gate; fight the ogre; Hengrad's cell; scorpion; Bouncer; leave
-- through the arena door (the General is optional); Lady Servil ends the quest.
-- Sources: quests/quest_arena/scripts/*.rs2 (lady_servil, khazard_guard,
-- khazard_barman, arena_locs, arena_encounter, hengrad, general_khazard).
-- Brought along: coins for the brew, and the recommended combat gear (weapon, food,
-- stats) -- both listed by the guide (getItemRequirements / getItemRecommended).

return {
    id = "arena",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99", -- combatGear: a fresh level-3 cannot trade blows with Bouncer (120 stats)
        "::give coins 5", -- getItemRequirements(): 5 coins for the Khali brew
        "::give rune_scimitar 1", -- combatGear
        "::give shark 15", -- combatGear: food
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp17_arenaquest",
            constants = {
                complete = 14,
                complete_defeated_genkhazard = 15,
                defeated_bouncer = 11,
                defeated_genkhazard = 13,
                defeated_ogre = 8,
                defeated_scorpion = 10,
                entered_ogre_fight = 6,
                freed_servils = 12,
                given_khali_brew = 5,
                not_started = 0,
                obtained_armour = 2,
                sent_jail = 9,
                spoken_drunkguard = 3,
                started = 1,
            },
            row = "quest_fightarena",
            display = "Fight Arena",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- startQuest: Lady Servil, lady_servil.rs2 case ^arena_not_started
        t.exec("goto-startQuest", t.player.goto_tile, 2565, 3199, 0)
        t.exec("startQuest", t.player.talk_to, "lady_servil", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "player:Hi there. Looks like you're in",
            "npc:The cart?",
            "choose:Can I help?",
            "player:Can I help?",
            "npc:You'd be willing to?",
            "player:Of course. Tell me what happened.",
            "npc:Well, I'm Lady Servil",
            "npc:Some of General Khazard's men",
            "player:General Khazard?",
            "npc:He's a menace",
            "player:That's horrible",
            "npc:The authorities don't care",
            "player:Well they won't stop me",
            "npc:Thank you.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- searchChest: arena_guard_chest_shut op1 (Search), arena_locs.rs2
        t.exec("goto-searchChest", t.player.goto_tile, 2613, 3190, 0)
        t.exec("searchChest", t.player.click_loc, "arena_guard_chest_shut", 1)
        t.exec("searchChest-dialog", t.chat.play, { "mesbox:helmet and" })
        t.exec("searchChest.helmet", t.inv.expect_has, "khazard_helmet", 1)
        t.exec("searchChest.platemail", t.inv.expect_has, "khazard_platemail", 1)
        t.expect("quest.stage.obtained_armour", t.quest.expect_stage("obtained_armour"))

        -- the guide's talkToGuard step lists the armour as EQUIPPED
        t.exec("equip.helmet", t.player.equip, "khazard_helmet")
        t.exec("equip.platemail", t.player.equip, "khazard_platemail")
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")

        -- talkToGuard: arena_guard2 case ^arena_obtained_armour, disguise worn
        t.exec("goto-talkToGuard", t.player.goto_tile, 2615, 3143, 0)
        t.exec("talkToGuard", t.player.talk_to, "arena_guard2", 1)
        t.exec("talkToGuard-dialog", t.chat.play, {
            "player:Long live General Khazard!",
            "npc:Erm",
            "npc:Have you come to laugh",
            "npc:Now I just want a decent drink",
        })
        t.expect("quest.stage.spoken_drunkguard", t.quest.expect_stage("spoken_drunkguard"))

        -- buyKhaliBrew: khazard_barman option 3 (only offered from stage 3)
        t.exec("goto-buyKhaliBrew", t.player.goto_tile, 2567, 3140, 0)
        t.exec("buyKhaliBrew", t.player.talk_to, "khazard_barman", 1)
        t.exec("buyKhaliBrew-dialog", t.chat.play, {
            "player:Hello.",
            "npc:Hi, what can I get you?",
            "choose:I'd like a Khali brew please.",
            "player:I'd like a Khali brew please.",
            "npc:There you go, that's five gold coins",
        })
        t.exec("buyKhaliBrew.brew", t.inv.await, "khali_brew", 1, 10)

        -- giveKhaliBrew: arena_guard2 case ^arena_spoken_drunkguard + brew in hand
        t.exec("goto-giveKhaliBrew", t.player.goto_tile, 2615, 3143, 0)
        t.exec("giveKhaliBrew", t.player.talk_to, "arena_guard2", 1)
        t.exec("giveKhaliBrew-dialog", t.chat.play, {
            "player:Hello again.",
            "npc:Bored, bored, bored",
            "player:Do you still fancy a drink?",
            "npc:I really shouldn't",
            "mesbox:You hand a bottle of Khali brew",
            "npc:Blimey this stuff",
            "player:No, not at all",
            "mesbox:The guard quickly finishes",
            "npc:That is some gooood stuff",
            "player:Are you alright?",
            "npc:Yeesshh",
            "player:Good idea",
            "npc:Yeesh, yes that shounds reasonable",
            "player:No problem, I'll keep them in line.",
        })
        t.expect("quest.stage.given_khali_brew", t.quest.expect_stage("given_khali_brew"))
        t.exec("giveKhaliBrew.keys", t.inv.await, "khazard_cellkeys", 1, 10)

        -- getCellKeys: the guide's step for a player without the keys -- drop the set the
        -- brew handover gave and ask the guard for another (khazard_guard.rs2 "I lost the keys")
        t.exec("getCellKeys.drop", t.player.drop, "khazard_cellkeys")
        t.exec("getCellKeys", t.player.talk_to, "arena_guard2", 1)
        t.exec("getCellKeys-dialog", t.chat.play, {
            "player:Hi, er.. I lost the keys.",
            "npc:What?! You're foolish",
            "player:...and I'm drunk.",
            "player:Hello, how's the job?",
            "npc:Please, leave me alone.",
        })
        t.exec("getCellKeys.keys", t.inv.await, "khazard_cellkeys", 1, 10)

        -- openCell: the cell keys used on arena_jeremydoor (oplocu, arena_locs.rs2)
        t.exec("goto-openCell", t.player.goto_tile, 2617, 3166, 0)
        local cell_gate = t.player.by_symbol("loc", "arena_jeremydoor")
        t.exec("openCell", t.player.use_on, "khazard_cellkeys", cell_gate)
        t.exec("openCell-dialog", t.chat.play, {
            "player:Sammy look, I have the keys.",
            "npc:Wow! Please set me free",
            "player:Ok, we'd better hurry.",
        })
        -- arena_enter (quest_arena.rs2:22-54; LostCity quest_arena.rs2:140-167): the door camera as
        -- the player is marched in. Two sequences with a cam_reset between them, so two awaits: a
        -- read stops at its own reset.
        t.exec("openCell.cutscene", t.cutscene.await, "openCell", { expect = {
            { op = "moveto", coord = "0_40_49_45_24", height = 270 },
            { op = "lookat", coord = "0_40_49_43_17", height = 200 },
            { op = "moveto", coord = "0_40_49_43_26", height = 270 },
            { op = "lookat", coord = "0_40_49_43_15", height = 270 },
            { op = "reset" },
        } })
        t.exec("openCell.cutscene-2", t.cutscene.await, "openCell-2", { expect = {
            { op = "moveto", coord = "0_40_49_43_26", height = 270 },
            { op = "lookat", coord = "0_40_49_43_15", height = 270 },
            { op = "reset" },
        } })
        -- The round's mesbox comes after the walk-in, then ogre_attack_justin (sammy_servil.rs2;
        -- LostCity jeremy_servil.rs2:95-146): the ogre-pen camera, a reset, the turn onto Justin.
        t.exec("openCell-dialog-2", t.chat.play, {
            "mesbox:Sammy's father is being attacked",
        })
        t.exec("ogrePen.cutscene", t.cutscene.await, "ogrePen", { expect = {
            { op = "moveto", coord = "0_40_49_39_25", height = 400 },
            { op = "lookat", coord = "0_40_49_45_29", height = 200 },
            { op = "reset" },
        } })
        t.exec("ogrePen.cutscene-2", t.cutscene.await, "ogrePen-2", { expect = {
            { op = "lookat", coord = "0_40_49_40_32", height = 270 },
            { op = "reset" },
        } })
        t.expect("quest.stage.entered_ogre_fight", t.quest.expect_stage("entered_ogre_fight"))

        -- talkToSammy: the arena Sammy (an owner-private sammy_servil_vis with the cache's
        -- Talk-to, placed by ~arena_spawn_sammy) -- [opnpc1,sammy_servil_vis] at stage 6
        -- (sammy_servil.rs2; LostCity jeremy_servil.rs2:26) asks where Justin is and resumes the round
        t.exec("talkToSammy", t.player.talk_to, "sammy_servil_vis")
        t.exec("talkToSammy-dialog", t.chat.play, {
            "player:Sammy, where's your father?",
            "npc:Quick, help him!",
        })
        t.expect("quest.stage.entered_ogre_fight-2", t.quest.expect_stage("entered_ogre_fight"))
        -- killOgre: the round already began when the gate opened (the ogre is private to the player)
        t.exec("killOgre", t.player.attack, "arena_ogre", 2, 20)
        t.exec("killOgre.dead", t.npc.await_dead_engaged, 240, 40, { eat = { item = "shark", below = 50 } })
        -- talkToKhazard: the General's speech opens the tick the ogre falls
        t.exec("talkToKhazard-dialog", t.chat.play, {
            "npc:Haha, well done",
            "player:They belong to nobody.",
            "npc:Well, I suppose we could find",
            "npc:I'll let them go",
            "npc:Guards! Take them to the cells.",
        })
        -- general_khazard_to_cells (general_khazard.rs2:48-76; LostCity general_khazard.rs2:50-79): the
        -- corridor camera while a guard marches the player into Hengrad's cell, then his page.
        t.exec("talkToKhazard.cutscene", t.cutscene.await, "toCells", { expect = {
            { op = "moveto", coord = "0_40_49_47_3", height = 600 },
            { op = "lookat", coord = "0_40_49_42_5", height = 0 },
            { op = "reset" },
        } })
        t.exec("talkToKhazard-guard", t.chat.play, {
            "npc:The General seems to have taken a liking to you.",
        })
        t.expect("quest.stage.sent_jail", t.quest.expect_stage("sent_jail"))

        -- talkToHengrad in the cell, then the scorpion round begins
        t.exec("talkToHengrad", t.player.talk_to, "hengrad", 1)
        t.exec("talkToHengrad-dialog", t.chat.play, {
            "player:Are you ok stranger?",
            "npc:I'm fine thanks.",
            "npc:So Khazard got his hands on you too?",
            "player:I'm afraid so.",
            "npc:If you're lucky",
            "player:How long have you been here?",
            "npc:I've been in Khazard's prisons",
            "player:Don't give up.",
            "npc:Thanks friend.",
            "npc:Wait.. sshh",
            "mesbox:From above you hear a voice",
        })
        -- arena_release_scorp (quest_arena.rs2:60-80; LostCity quest_arena.rs2:193-228)
        t.exec("scorpionPen.cutscene", t.cutscene.await, "scorpionPen", { expect = {
            { op = "moveto", coord = "0_40_49_35_23", height = 700 },
            { op = "lookat", coord = "0_40_49_47_23", height = 100 },
            { op = "moveto", coord = "0_40_49_41_23", height = 450 },
            { op = "reset" },
        } })
        t.exec("killScorpion", t.player.attack, "arena_scorpion", 2, 20)
        t.exec("killScorpion.dead", t.npc.await_dead_engaged, 240, 40, { eat = { item = "shark", below = 50 } })
        t.exec("killScorpion-dialog", t.chat.play, {
            "npc:Impressive, but now for a proper challenge",
            "mesbox:Today's second round of battle",
        })
        -- arena_release_bouncer (quest_arena.rs2:84-104; LostCity quest_arena.rs2:230-265)
        t.exec("bouncerPen.cutscene", t.cutscene.await, "bouncerPen", { expect = {
            { op = "moveto", coord = "0_40_49_35_26", height = 700 },
            { op = "lookat", coord = "0_40_49_47_26", height = 100 },
            { op = "moveto", coord = "0_40_49_41_26", height = 450 },
            { op = "reset" },
        } })
        t.expect("quest.stage.defeated_scorpion", t.quest.expect_stage("defeated_scorpion"))

        t.exec("killBouncer", t.player.attack, "arena_bouncer", 2, 20)
        t.exec("killBouncer.dead", t.npc.await_dead_engaged, 400, 60, { eat = { item = "shark", below = 50 } })
        t.exec("killBouncer-dialog", t.chat.play, {
            "npc:Bouncer! No!",
            "player:You agreed to let the Servils go",
            "npc:Indeed. I underestimated you",
            "npc:You, however, must remain",
        })
        t.expect("quest.stage.freed_servils", t.quest.expect_stage("freed_servils"))

        -- leaveArena: fightarena_door2 op1 from inside (General ignored)
        t.exec("leaveArena", t.player.click_loc, "fightarena_door2", 1)
        local exit_result, exit_tile = t.world.tile()
        t.check("leaveArena.outside", exit_result == "ok" and exit_tile.x >= 2607, "landed at " .. tostring(exit_tile and exit_tile.x) .. "," .. tostring(exit_tile and exit_tile.z))

        -- endQuest: back to Lady Servil
        t.exec("goto-endQuest", t.player.goto_tile, 2565, 3199, 0)
        local snapshot_result, snapshot = t.skill.snapshot()
        local coins_result, coins_before = t.inv.count("coins")
        t.check("endQuest.snapshot", snapshot_result == "ok" and coins_result == "ok",
            "attack xp=" .. tostring(snapshot and snapshot.attack and snapshot.attack.experience)
            .. " thieving xp=" .. tostring(snapshot and snapshot.thieving and snapshot.thieving.experience)
            .. " coins=" .. tostring(coins_before))
        t.exec("endQuest", t.player.talk_to, "lady_servil", 1)
        t.exec("endQuest-dialog", t.chat.play, {
            "player:Lady Servil.",
            "npc:You're alive",
            "npc:My son and husband are safe",
            "npc:All I can offer in return",
        })
        t.quest.expect_complete()
        t.expect("reward.attack_xp", t.skill.expect_gain("attack", 12175, snapshot))
        t.expect("reward.thieving_xp", t.skill.expect_gain("thieving", 2175, snapshot))
        local coins_after_result, coins_after = t.inv.count("coins")
        t.check("reward.coins", coins_after_result == "ok" and coins_after - coins_before == 1000,
            "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after))
        -- the Khazard armour reward is the set already worn: take it off to count it
        t.exec("reward.khazard_armour.off", t.player.unequip, "khazard_platemail")
        t.exec("reward.khazard_armour", t.inv.expect_has, "khazard_platemail", 1)
        t.finish(0)
    end,
}
